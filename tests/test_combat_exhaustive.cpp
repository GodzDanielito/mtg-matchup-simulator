// Test esaustivo delle dichiarazioni di attacco e di blocco (sezione 13).
//
// Per molte configurazioni piccole si confrontano:
//  - le dichiarazioni raggiunte dal motore esplorando TUTTI i percorsi di decisione;
//  - le dichiarazioni legali generate per forza bruta, con regole riscritte qui
//    in modo indipendente dal motore (volare, protezione, swampwalk, minaccia,
//    minimo di tre bloccanti, un attaccante per bloccante).
// Le dichiarazioni si riducono a forma canonica per nome di carta, quindi due
// creature identiche scambiate contano come la stessa dichiarazione.
// Deve valere: stesso insieme di dichiarazioni, e un percorso per dichiarazione
// (nessun duplicato, nessuna mancante, nessuna illegale).

#include "test_helpers.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace mtg;
using namespace test;

namespace {

using AttackDecl = std::vector<std::string>;                                      // nomi degli attaccanti
using BlockDecl = std::vector<std::pair<std::string, std::vector<std::string>>>;  // attaccante -> bloccanti

struct Config {
    PlayerId active;
    std::vector<std::string> attackers;  // creature del giocatore attivo
    std::vector<std::string> blockers;   // creature del difensore
    bool defender_swamp = false;         // il difensore controlla una Swamp (swampwalk)
};

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

void place(GameState& s, PlayerId owner, const std::string& name) {
    const ObjRef card = find_cards(s, owner, Zone::Library, name)[0];
    move_object(s, db_with_tokens(), card, Zone::Battlefield, Cause::Test);
}

// Partita preparata: creature in campo, una fetch per il giocatore attivo (così
// ogni sua priorità è una decisione e il motore si ferma dove serve).
GameState prepare(const Config& c) {
    const PlayerId defender = static_cast<PlayerId>(1 - c.active);
    GameState s = new_game(1);
    for (const auto& n : c.attackers) place(s, c.active, n);
    for (const auto& n : c.blockers) place(s, defender, n);
    place(s, c.active, c.active == 1 ? "Marsh Flats" : "Arid Mesa");
    if (c.defender_swamp) {
        place(s, 1, "Swamp");  // le Swamp sono di Rakdos: il test ne passa una al difensore
        s.objects[s.battlefield.back()].controller = defender;
    }
    start_game(s, db_with_tokens(), StartOptions{c.active, false});
    for (int guard = 0; guard < 100 && s.pending.kind == DecisionKind::Priority &&
                        s.turn.step != Step::DeclareAttackers;
         ++guard) {
        apply(s, db_with_tokens(), Action{ActionType::Pass});
    }
    REQUIRE(s.pending.kind == DecisionKind::DeclareAttacker);
    return s;
}

// Esplora tutti i percorsi finché la decisione è di tipo `kind`; chiama `leaf` alla fine di ognuno.
template <typename Leaf>
void explore(const GameState& s, DecisionKind kind, Leaf&& leaf, int& paths) {
    if (s.pending.kind != kind) {
        ++paths;
        leaf(s);
        return;
    }
    for (const Action& a : legal_actions(s, db_with_tokens())) {
        GameState next = s;
        apply(next, db_with_tokens(), a);
        explore(next, kind, leaf, paths);
    }
}

AttackDecl attack_decl(const GameState& s) {
    AttackDecl out;
    for (const Attacker& a : s.combat.attackers) out.push_back(name_of(s, a.creature.card));
    std::sort(out.begin(), out.end());
    return out;
}

BlockDecl block_decl(const GameState& s) {
    BlockDecl out;
    for (std::size_t i = 0; i < s.combat.attackers.size(); ++i) {
        std::vector<std::string> by;
        for (const Block& b : s.combat.blocks) {
            if (b.attacker == i) by.push_back(name_of(s, b.blocker.card));
        }
        std::sort(by.begin(), by.end());
        out.emplace_back(name_of(s, s.combat.attackers[i].creature.card), by);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// --- Regole riscritte per la forza bruta ---

const CombatRules& combat_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).rules.combat; }
bool kw(const GameState& s, CardId id, Keyword k) { return (combat_of(s, id).keywords & keyword_bit(k)) != 0; }

bool pair_ok(const GameState& s, CardId blocker, CardId attacker) {
    const CardDef& b = db_with_tokens().get(s.objects[blocker].def);
    if (kw(s, attacker, Keyword::Flying) && !kw(s, blocker, Keyword::Flying) && !kw(s, blocker, Keyword::Reach)) {
        return false;
    }
    if ((combat_of(s, attacker).protection & b.colors) != 0) return false;
    return true;
}

bool unblockable(const GameState& s, CardId attacker, PlayerId defender) {
    const LandTypeMask walk = combat_of(s, attacker).landwalk;
    if (walk == 0) return false;
    for (const CardId id : s.battlefield) {
        const CardDef& d = db_with_tokens().get(s.objects[id].def);
        if (s.objects[id].controller == defender && d.has_type(CardType::Land) && (d.land_types & walk) != 0) {
            return true;
        }
    }
    return false;
}

int minimum(const GameState& s, CardId attacker) {
    int m = 1;
    if (kw(s, attacker, Keyword::Menace)) m = 2;
    return std::max<int>(m, combat_of(s, attacker).min_blockers);
}

std::set<BlockDecl> brute_force_blocks(const GameState& s) {
    const PlayerId defender = static_cast<PlayerId>(1 - s.turn.active_player);
    std::vector<CardId> attackers, blockers;
    for (const Attacker& a : s.combat.attackers) attackers.push_back(a.creature.card);
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller == defender && db_with_tokens().get(o.def).has_type(CardType::Creature) &&
            !o.has_flag(ObjectFlag::Tapped)) {
            blockers.push_back(id);
        }
    }
    std::set<BlockDecl> out;
    std::vector<int> choice(blockers.size(), -1);  // -1 = non blocca, altrimenti indice dell'attaccante
    auto rec = [&](auto&& self, std::size_t i) -> void {
        if (i == blockers.size()) {
            std::vector<int> count(attackers.size(), 0);
            for (std::size_t b = 0; b < blockers.size(); ++b) {
                if (choice[b] >= 0) ++count[static_cast<std::size_t>(choice[b])];
            }
            for (std::size_t a = 0; a < attackers.size(); ++a) {
                if (count[a] > 0 && count[a] < minimum(s, attackers[a])) return;  // minaccia, Troll
            }
            BlockDecl decl;
            for (std::size_t a = 0; a < attackers.size(); ++a) {
                std::vector<std::string> by;
                for (std::size_t b = 0; b < blockers.size(); ++b) {
                    if (choice[b] == static_cast<int>(a)) by.push_back(name_of(s, blockers[b]));
                }
                std::sort(by.begin(), by.end());
                decl.emplace_back(name_of(s, attackers[a]), by);
            }
            std::sort(decl.begin(), decl.end());
            out.insert(decl);
            return;
        }
        choice[i] = -1;
        self(self, i + 1);
        for (std::size_t a = 0; a < attackers.size(); ++a) {
            if (unblockable(s, attackers[a], defender) || !pair_ok(s, blockers[i], attackers[a])) continue;
            choice[i] = static_cast<int>(a);
            self(self, i + 1);
        }
        choice[i] = -1;
    };
    rec(rec, 0);
    return out;
}

std::set<AttackDecl> brute_force_attacks(const std::vector<std::string>& names) {
    std::set<AttackDecl> out;
    const std::size_t n = names.size();
    for (std::size_t mask = 0; mask < (std::size_t{1} << n); ++mask) {
        AttackDecl d;
        for (std::size_t i = 0; i < n; ++i) {
            if ((mask >> i) & 1u) d.push_back(names[i]);
        }
        std::sort(d.begin(), d.end());
        out.insert(d);
    }
    return out;
}

// Totali sulle configurazioni controllate (per verificare che l'esplorazione non sia banale).
int g_attack_decls = 0, g_block_decls = 0, g_multi_blocks = 0;

void check_config(const Config& c) {
    INFO("attivo " << int(c.active) << ", swamp " << c.defender_swamp);
    for (const auto& n : c.attackers) INFO("attaccante " << n);
    for (const auto& n : c.blockers) INFO("bloccante " << n);
    const GameState start = prepare(c);

    // Attacchi: tutti i percorsi contro tutti i sottoinsiemi.
    std::set<AttackDecl> attacks;
    int attack_paths = 0;
    explore(start, DecisionKind::DeclareAttacker, [&](const GameState& leaf) { attacks.insert(attack_decl(leaf)); },
            attack_paths);
    const auto expected_attacks = brute_force_attacks(c.attackers);
    CHECK(attacks == expected_attacks);
    CHECK(attack_paths == static_cast<int>(expected_attacks.size()));

    // Blocchi: attaccano tutti, poi tutti i percorsi del difensore contro la forza bruta.
    GameState s = start;
    while (s.pending.kind == DecisionKind::DeclareAttacker) {
        apply(s, db_with_tokens(), Action{ActionType::TargetPlayer, kNoCard, static_cast<PlayerId>(1 - c.active)});
    }
    REQUIRE(s.combat.attackers.size() == c.attackers.size());
    // Priorità nello step degli attaccanti; con Guide of Souls in campo il suo trigger d'attacco
    // va in pila e si risolve (senza energia non chiede nulla) prima dei bloccanti.
    while (s.pending.kind == DecisionKind::Priority) apply(s, db_with_tokens(), Action{ActionType::Pass});
    std::set<BlockDecl> blocks;
    int block_paths = 0;
    explore(s, DecisionKind::DeclareBlockers, [&](const GameState& leaf) { blocks.insert(block_decl(leaf)); },
            block_paths);
    const auto expected_blocks = brute_force_blocks(s);
    CHECK(blocks == expected_blocks);
    CHECK(block_paths == static_cast<int>(expected_blocks.size()));

    g_attack_decls += static_cast<int>(expected_attacks.size());
    g_block_decls += static_cast<int>(expected_blocks.size());
    for (const BlockDecl& d : expected_blocks) {
        for (const auto& [attacker, by] : d) g_multi_blocks += by.size() >= 2 ? 1 : 0;
    }
}

// Creature disponibili per lato, con il numero massimo di copie da usare
// (legendarie: una sola, per non attivare la regola delle leggende).
const std::vector<std::pair<std::string, int>> kRakdosCreatures = {
    {"Stitcher's Supplier", 3}, {"Street Wraith", 3}, {"Moonshadow", 3},
    {"Troll of Khazad-dûm", 1}, {"Detective's Phoenix", 3}, {"Fear of Missing Out", 3},
};
const std::vector<std::pair<std::string, int>> kUwCreatures = {
    {"Ocelot Pride", 3},     {"Guide of Souls", 3}, {"Sanctifier en-Vec", 1},         {"Starfield Shepherd", 3},
    {"Quantum Riddler", 3},  {"Solitude", 3},       {"Phelia, Exuberant Shepherd", 1}, {"Witch Enchanter", 3},
};

std::vector<std::string> pick(Pcg32& rng, const std::vector<std::pair<std::string, int>>& pool, int max_total) {
    std::vector<std::string> out;
    const int kinds = 1 + static_cast<int>(rng.bounded(3));
    std::vector<std::size_t> idx(pool.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    shuffle(std::span<std::size_t>(idx), rng);
    for (int k = 0; k < kinds && static_cast<int>(out.size()) < max_total; ++k) {
        const auto& [name, max_copies] = pool[idx[static_cast<std::size_t>(k)]];
        const int copies = 1 + static_cast<int>(rng.bounded(static_cast<std::uint32_t>(max_copies)));
        for (int c = 0; c < copies && static_cast<int>(out.size()) < max_total; ++c) out.push_back(name);
    }
    return out;
}

}  // namespace

TEST_CASE("Dichiarazioni esaustive: configurazioni scelte", "[combat][exhaustive]") {
    SECTION("Copie identiche e minaccia") {
        check_config({1, {"Moonshadow", "Stitcher's Supplier", "Stitcher's Supplier"},
                      {"Guide of Souls", "Guide of Souls", "Ocelot Pride"}});
    }
    SECTION("Attaccanti identici con minaccia, bloccanti identici") {
        check_config({1, {"Moonshadow", "Moonshadow"}, {"Guide of Souls", "Guide of Souls", "Guide of Souls"}});
    }
    SECTION("Troll: tre o piu' bloccanti") {
        check_config({1, {"Troll of Khazad-dûm", "Street Wraith"},
                      {"Guide of Souls", "Guide of Souls", "Ocelot Pride", "Solitude"}});
    }
    SECTION("Volare: Phoenix bloccabile solo da chi vola") {
        check_config({1, {"Detective's Phoenix", "Stitcher's Supplier"},
                      {"Starfield Shepherd", "Quantum Riddler", "Ocelot Pride"}});
    }
    SECTION("Swampwalk con Swamp del difensore") {
        check_config({1, {"Street Wraith", "Stitcher's Supplier"}, {"Guide of Souls", "Ocelot Pride"}, true});
    }
    SECTION("Protezione e volare in attacco per UW") {
        check_config({0, {"Sanctifier en-Vec", "Starfield Shepherd", "Ocelot Pride", "Ocelot Pride"},
                      {"Stitcher's Supplier", "Detective's Phoenix", "Fear of Missing Out"}});
    }
}

TEST_CASE("Dichiarazioni esaustive: configurazioni casuali", "[combat][exhaustive]") {
    Pcg32 rng(20261109);
    for (int i = 0; i < 40; ++i) {
        Config c;
        c.active = static_cast<PlayerId>(i % 2);
        c.attackers = pick(rng, c.active == 1 ? kRakdosCreatures : kUwCreatures, 4);
        c.blockers = pick(rng, c.active == 1 ? kUwCreatures : kRakdosCreatures, 5);
        c.defender_swamp = c.active == 1 && rng.bounded(2) == 1;
        INFO("configurazione " << i);
        check_config(c);
    }
    std::cout << '\n'
              << "  dichiarazioni esaustive: " << g_attack_decls << " di attacco, " << g_block_decls << " di blocco ("
              << g_multi_blocks << " blocchi multipli) confrontate con la forza bruta" << '\n';
    CHECK(g_block_decls > 500);
    CHECK(g_multi_blocks > 100);
}

TEST_CASE("Divisione del danno: opzioni = esiti distinti (forza bruta)", "[combat][exhaustive]") {
    // Troll of Khazad-dûm (6) bloccato da tre Guide of Souls (costituzione 2) identiche.
    Config c{1, {"Troll of Khazad-dûm"}, {"Guide of Souls", "Guide of Souls", "Guide of Souls"}};
    GameState s = prepare(c);
    apply(s, db_with_tokens(), Action{ActionType::TargetPlayer, kNoCard, 0});
    apply(s, db_with_tokens(), Action{ActionType::Pass});
    while (s.pending.kind == DecisionKind::DeclareBlockers) {
        // Aggiunge bloccanti finché può (le scelte obbligate avvengono da sole).
        const auto actions = legal_actions(s, db_with_tokens());
        const auto add = std::find_if(actions.begin(), actions.end(),
                                      [](const Action& a) { return a.type == ActionType::ChooseCard; });
        apply(s, db_with_tokens(), add != actions.end() ? *add : actions.front());
    }
    REQUIRE(s.combat.blocks.size() == 3);
    while (s.pending.kind == DecisionKind::Priority) apply(s, db_with_tokens(), Action{ActionType::Pass});
    REQUIRE(s.pending.kind == DecisionKind::DivideDamage);

    // Forza bruta: ogni ripartizione di 6 danni sulle tre Guide; esito = per ogni Guide
    // "morta" oppure il danno rimasto, a meno dell'ordine (sono identiche).
    std::set<std::vector<int>> expected;
    for (int a = 0; a <= 6; ++a) {
        for (int b = 0; a + b <= 6; ++b) {
            std::vector<int> e;
            for (const int d : {a, b, 6 - a - b}) e.push_back(d >= 2 ? -1 : d);
            std::sort(e.begin(), e.end());
            expected.insert(e);
        }
    }
    CHECK(legal_actions(s, db_with_tokens()).size() == expected.size());

    std::set<std::vector<int>> reached;
    for (const Action& a : legal_actions(s, db_with_tokens())) {
        GameState next = s;
        apply(next, db_with_tokens(), a);
        std::vector<int> e;
        for (const Block& b : s.combat.blocks) {
            const CardObject& o = next.objects[b.blocker.card];
            e.push_back(next.is_valid(b.blocker) && o.zone == Zone::Battlefield ? o.damage : -1);
        }
        std::sort(e.begin(), e.end());
        reached.insert(e);
    }
    CHECK(reached == expected);
}
