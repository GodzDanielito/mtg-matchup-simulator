// Test del combattimento (docs/stato_di_gioco.md, sezione 13). Regole dalle
// Comprehensive Rules del 6/11/2026 (506-511, 702); testo Oracle da Scryfall
// (bulk oracle_cards dell'8/11/2026).
//
// Le creature con parole chiave sono messe in campo dai test: delle carte non
// ancora implementate esistono solo le parole chiave (cards/), non il lancio
// né le altre abilità.
//
// Per fermare il motore nei momenti giusti il giocatore attivo ha una fetch in
// campo: può agire a ogni priorità, quindi ogni priorità è una decisione.

#include "test_helpers.hpp"

#include <algorithm>
#include <set>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Passa la priorità finché la condizione non è vera (o finché c'è una decisione di priorità).
template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 500 && !done(st.s); ++guard) {
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        act(st, pass());
    }
    REQUIRE(done(st.s));
}

void to_declare_attackers(Staged& st) {
    pass_until(st, [](const GameState& s) {
        return s.pending.kind == DecisionKind::DeclareAttacker ||
               (s.turn.step == Step::DeclareAttackers && s.pending.kind == DecisionKind::Priority);
    });
}

// Dichiara come attaccanti le creature con questi nomi (le altre non attaccano).
void attack_with(Staged& st, std::initializer_list<std::string> names) {
    to_declare_attackers(st);
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        const std::string who = name_of(st.s, st.s.pending.subject.card);
        const bool attacks = std::find(names.begin(), names.end(), who) != names.end();
        const PlayerId defender = static_cast<PlayerId>(1 - st.s.turn.active_player);
        act(st, attacks ? Action{ActionType::TargetPlayer, kNoCard, defender} : option(kNoAttack));
    }
}

// Durante la dichiarazione dei bloccanti: per l'attaccante con quel nome aggiunge
// i bloccanti indicati (in qualsiasi ordine il motore li offra), per gli altri
// sceglie "fine". Le scelte obbligate avvengono da sole.
void block_with(Staged& st, const std::string& attacker, std::initializer_list<std::string> blockers) {
    std::vector<std::string> todo(blockers);
    while (st.s.pending.kind == DecisionKind::DeclareBlockers) {
        if (name_of(st.s, st.s.pending.subject.card) != attacker || todo.empty()) {
            act(st, option(kDoneBlocking));
            continue;
        }
        bool chosen = false;
        for (const Action& a : legal(st.s)) {
            if (a.type != ActionType::ChooseCard) continue;
            const auto it = std::find(todo.begin(), todo.end(), name_of(st.s, a.card));
            if (it == todo.end()) continue;
            todo.erase(it);
            act(st, a);
            chosen = true;
            break;
        }
        REQUIRE(chosen);
    }
}

std::vector<std::string> attacker_names(const GameState& s) {
    std::vector<std::string> out;
    for (const Attacker& a : s.combat.attackers) out.push_back(name_of(s, a.creature.card));
    std::sort(out.begin(), out.end());
    return out;
}

bool step_logged(const GameLog& log, Step step) {
    return std::any_of(log.entries.begin(), log.entries.end(), [&](const LogEntry& e) { return e.step == step; });
}

}  // namespace

// ---------------------------------------------------------------------------
// Dichiarazione degli attaccanti (508.1)
// ---------------------------------------------------------------------------

TEST_CASE("Attaccanti: si tappano, ricevono il flag, infliggono danno", "[combat]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Stitcher's Supplier", "Street Wraith"});
    to_declare_attackers(st);
    REQUIRE(st.s.pending.kind == DecisionKind::DeclareAttacker);
    CHECK(legal(st.s).size() == 2);  // non attacca, attacca il giocatore 0
    attack_with(st, {"Stitcher's Supplier", "Street Wraith"});

    // Il giocatore attivo ha la priorità nello step degli attaccanti (fetch in campo).
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::DeclareAttackers);
    CHECK(attacker_names(st.s) == std::vector<std::string>{"Stitcher's Supplier", "Street Wraith"});
    for (const Attacker& a : st.s.combat.attackers) {
        CHECK(st.s.objects[a.creature.card].has_flag(ObjectFlag::Tapped));
        CHECK(st.s.objects[a.creature.card].has_flag(ObjectFlag::AttackedThisTurn));
        CHECK(a.defender.tag == TargetKindTag::Player);
        CHECK(a.defender.player == kUw);
    }
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK(st.s.players[kUw].life == 16);  // 1 + 3, nessun bloccante
}

TEST_CASE("Attaccanti: niente creature tappate o malate, si' con rapidita'", "[combat]") {
    // Detective's Phoenix — Flying, haste.
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Stitcher's Supplier", "Street Wraith"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    // Entrano ora: "malate". Solo la Phoenix ha rapidità.
    put(st.s, kRakdos, {"Detective's Phoenix", "Stitcher's Supplier"}, Zone::Battlefield);
    st.s.objects[find_on_battlefield(st.s, "Street Wraith").card].set_flag(ObjectFlag::Tapped);
    require_invariants(st.s);

    std::vector<std::string> asked;
    to_declare_attackers(st);
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        asked.push_back(name_of(st.s, st.s.pending.subject.card));
        act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    }
    std::sort(asked.begin(), asked.end());
    CHECK(asked == std::vector<std::string>{"Detective's Phoenix", "Stitcher's Supplier"});
    CHECK(attacker_names(st.s) == std::vector<std::string>{"Detective's Phoenix", "Stitcher's Supplier"});
}

TEST_CASE("Attaccanti: creature identiche, si sceglie quante", "[combat]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Stitcher's Supplier", "Stitcher's Supplier"});
    to_declare_attackers(st);
    REQUIRE(st.s.pending.kind == DecisionKind::DeclareAttacker);
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    // La seconda copia identica non può scegliere "non attacca" dopo che la prima ha attaccato:
    // l'unica opzione rimasta è automatica.
    CHECK(st.s.pending.kind != DecisionKind::DeclareAttacker);
    CHECK(st.s.combat.attackers.size() == 2);
}

TEST_CASE("Nessun attaccante: si saltano bloccanti e danno (508.8)", "[combat]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Stitcher's Supplier"});
    attack_with(st, {});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main2; });
    CHECK_FALSE(step_logged(st.log, Step::DeclareBlockers));
    CHECK_FALSE(step_logged(st.log, Step::CombatDamage));
    CHECK(st.s.players[kUw].life == 20);
}

// ---------------------------------------------------------------------------
// Dichiarazione dei bloccanti (509.1) e restrizioni di evasione
// ---------------------------------------------------------------------------

TEST_CASE("Blocco semplice: danno reciproco e trigger di morte", "[combat]") {
    // Guide of Souls 1/2 blocca Stitcher's Supplier 1/1: il Supplier muore e macina 3.
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Stitcher's Supplier"});
    attack_with(st, {"Stitcher's Supplier"});
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
    block_with(st, "Stitcher's Supplier", {"Guide of Souls"});
    REQUIRE(st.s.combat.blocks.size() == 1);
    CHECK(st.s.combat.attackers[0].blocked);

    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    const ObjRef guide = find_on_battlefield(st.s, "Guide of Souls");
    CHECK(st.s.objects[guide.card].damage == 1);
    CHECK(st.s.players[kRakdos].graveyard.size() == 4);  // Supplier + 3 carte macinate
    CHECK(st.s.players[kUw].life == 20);
}

// Starfield Shepherd — Creature — Angel 3/2: Flying ...
TEST_CASE("Volare: solo creature con volare possono bloccare", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Ocelot Pride", "Starfield Shepherd"},
                       {"Marsh Flats", "Detective's Phoenix"});
    attack_with(st, {"Detective's Phoenix"});
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
    std::vector<std::string> options;
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::ChooseCard) options.push_back(name_of(st.s, a.card));
    }
    CHECK(options == std::vector<std::string>{"Starfield Shepherd"});  // Ocelot Pride non vola
}

// Moonshadow — Creature — Elemental 7/7: Menace ...
TEST_CASE("Minaccia: un solo bloccante non basta", "[combat][keywords]") {
    SECTION("Con un solo bloccante possibile non c'e' nessuna decisione") {
        Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Moonshadow"});
        attack_with(st, {"Moonshadow"});
        act(st, pass());
        CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);
        CHECK(st.s.combat.blocks.empty());
    }
    SECTION("Con due bloccanti: o nessuno o entrambi") {
        Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls", "Ocelot Pride"},
                           {"Marsh Flats", "Moonshadow"});
        attack_with(st, {"Moonshadow"});
        act(st, pass());
        REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
        // Due opzioni, non tre: "fine" e il primo bloccante in ordine canonico. Partire
        // dal secondo non è offerto: dopo di lui non resterebbe nessuno per arrivare a 2,
        // e la coppia si costruisce in un solo ordine (nessun duplicato).
        const auto options = legal(st.s);
        REQUIRE(options.size() == 2);
        const auto add = std::find_if(options.begin(), options.end(),
                                      [](const Action& a) { return a.type == ActionType::ChooseCard; });
        REQUIRE(add != options.end());
        act(st, *add);
        // "Fine" con un solo bloccante sarebbe illegale: l'altro bloccante si aggiunge da sé.
        CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);
        CHECK(st.s.combat.blocks.size() == 2);
    }
}

// Troll of Khazad-dûm — This creature can't be blocked except by three or more creatures.
TEST_CASE("Troll of Khazad-dum: servono tre bloccanti", "[combat][keywords]") {
    Staged two = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls", "Guide of Souls"},
                        {"Marsh Flats", "Troll of Khazad-dûm"});
    attack_with(two, {"Troll of Khazad-dûm"});
    act(two, pass());
    CHECK(two.s.pending.kind != DecisionKind::DeclareBlockers);  // con due non si può bloccare

    Staged three = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls", "Guide of Souls", "Ocelot Pride"},
                          {"Marsh Flats", "Troll of Khazad-dûm"});
    attack_with(three, {"Troll of Khazad-dûm"});
    act(three, pass());
    REQUIRE(is_decision(three.s, DecisionKind::DeclareBlockers, kUw));
    block_with(three, "Troll of Khazad-dûm", {"Guide of Souls", "Guide of Souls", "Ocelot Pride"});
    CHECK(three.s.combat.blocks.size() == 3);
}

// Street Wraith — Swampwalk (This creature can't be blocked as long as defending
// player controls a Swamp.)
TEST_CASE("Swampwalk: imbloccabile se il difensore controlla una Swamp", "[combat][keywords]") {
    SECTION("Senza Swamp si puo' bloccare") {
        Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Street Wraith"});
        attack_with(st, {"Street Wraith"});
        act(st, pass());
        CHECK(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
    }
    SECTION("Con una Swamp del difensore no") {
        Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Street Wraith", "Swamp"});
        // UW Blink non ha Swamp nel mazzo: il test passa il controllo di una Swamp al giocatore 0.
        st.s.objects[find_on_battlefield(st.s, "Swamp").card].controller = kUw;
        attack_with(st, {"Street Wraith"});
        act(st, pass());
        CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);
        pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
        CHECK(st.s.players[kUw].life == 17);
    }
}

TEST_CASE("Ogni bloccante blocca un solo attaccante", "[combat]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"},
                       {"Marsh Flats", "Stitcher's Supplier", "Street Wraith"});
    attack_with(st, {"Stitcher's Supplier", "Street Wraith"});
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
    const std::string first = name_of(st.s, st.s.pending.subject.card);
    act(st, find_action(st.s, ActionType::ChooseCard, "Guide of Souls"));
    // Dopo l'unico bloccante resta solo "fine" (automatico); per il secondo attaccante
    // non resta nessun bloccante: nessuna decisione.
    CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);
    REQUIRE(st.s.combat.blocks.size() == 1);
    CHECK(name_of(st.s, st.s.combat.attackers[st.s.combat.blocks[0].attacker].creature.card) == first);
}

// ---------------------------------------------------------------------------
// Danno (510)
// ---------------------------------------------------------------------------

TEST_CASE("Divisione del danno: un'opzione per esito, copie identiche fuse", "[combat]") {
    // Moonshadow 7/7 bloccata da due Guide of Souls 1/2 identiche. Esiti distinti:
    // muoiono entrambe; ne muore una e l'altra resta con 0 o 1 danno.
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls", "Guide of Souls"}, {"Marsh Flats", "Moonshadow"});
    attack_with(st, {"Moonshadow"});
    act(st, pass());
    block_with(st, "Moonshadow", {"Guide of Souls", "Guide of Souls"});
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DivideDamage; });
    REQUIRE(is_decision(st.s, DecisionKind::DivideDamage, kRakdos));
    CHECK(legal(st.s).size() == 3);

    // Ogni opzione porta a uno stato diverso: (Guide morte, danno sulla sopravvissuta).
    std::set<std::pair<std::size_t, int>> outcomes;
    for (const Action& a : legal(st.s)) {
        Staged copy = st;
        act(copy, a);
        int survivor_damage = -1;
        for (const CardId id : copy.s.battlefield) {
            if (name_of(copy.s, id) == "Guide of Souls") survivor_damage = copy.s.objects[id].damage;
        }
        outcomes.insert({copy.s.players[kUw].graveyard.size(), survivor_damage});
    }
    CHECK(outcomes == std::set<std::pair<std::size_t, int>>{{2, -1}, {1, 0}, {1, 1}});
}

TEST_CASE("Attaccante bloccato i cui bloccanti spariscono: nessun danno", "[combat]") {
    // Lightning Bolt uccide il bloccante dopo i blocchi: Street Wraith resta bloccata e non colpisce.
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {"Guide of Souls"},
                       {"Marsh Flats", "Blood Crypt", "Street Wraith"});
    attack_with(st, {"Street Wraith"});
    act(st, pass());
    block_with(st, "Street Wraith", {"Guide of Souls"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::DeclareBlockers);
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    act(st, Action{ActionType::TargetObject, find_on_battlefield(st.s, "Guide of Souls").card, 0});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK(st.s.combat.attackers[0].blocked);
    CHECK(st.s.players[kUw].life == 20);
}

TEST_CASE("Creature che muoiono insieme in combattimento: un solo evento", "[combat]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls", "Guide of Souls"}, {"Marsh Flats", "Moonshadow"});
    attack_with(st, {"Moonshadow"});
    act(st, pass());
    block_with(st, "Moonshadow", {"Guide of Souls", "Guide of Souls"});
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DivideDamage; });
    const auto events = st.s.event_serial;
    // L'opzione in cui muoiono entrambe.
    for (const Action& a : legal(st.s)) {
        Staged copy = st;
        act(copy, a);
        if (copy.s.players[kUw].graveyard.size() == 2) {
            CHECK(copy.s.event_serial == events + 1);
            return;
        }
    }
    FAIL("nessuna opzione uccide entrambe le Guide of Souls");
}

// ---------------------------------------------------------------------------
// Ocelot Pride — Creature — Cat 1/1
//   First strike, lifelink
//   Ascend ...  At the beginning of your end step, if you gained life this turn,
//   create a 1/1 white Cat creature token. ...        (trigger non implementato)
// ---------------------------------------------------------------------------

TEST_CASE("Attacco improvviso: Ocelot Pride uccide prima di subire danno", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Ocelot Pride"}, {"Marsh Flats", "Stitcher's Supplier"});
    attack_with(st, {"Stitcher's Supplier"});
    act(st, pass());
    block_with(st, "Stitcher's Supplier", {"Ocelot Pride"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });

    CHECK(step_logged(st.log, Step::FirstStrikeDamage));
    const ObjRef ocelot = find_on_battlefield(st.s, "Ocelot Pride");
    CHECK(st.s.objects[ocelot.card].damage == 0);  // il Supplier era già morto
    CHECK(st.s.players[kUw].life == 21);           // legame vitale
    CHECK(st.s.players[kUw].gained_life_this_turn);
}

TEST_CASE("Attacco improvviso: chi ha gia' colpito non colpisce di nuovo", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Ocelot Pride"}, {"Marsh Flats", "Street Wraith"});
    attack_with(st, {"Street Wraith"});
    act(st, pass());
    block_with(st, "Street Wraith", {"Ocelot Pride"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });

    const ObjRef wraith = find_on_battlefield(st.s, "Street Wraith");
    CHECK(st.s.objects[wraith.card].damage == 1);  // solo il danno del primo step
    CHECK(st.s.players[kUw].graveyard.size() == 1);  // Ocelot Pride è morto nel secondo step
    CHECK(st.s.players[kUw].life == 21);            // un solo guadagno di vita
}

TEST_CASE("Senza attacco improvviso non c'e' lo step aggiuntivo", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Guide of Souls"}, {"Marsh Flats", "Stitcher's Supplier"});
    attack_with(st, {"Stitcher's Supplier"});
    act(st, pass());
    block_with(st, "Stitcher's Supplier", {"Guide of Souls"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK_FALSE(step_logged(st.log, Step::FirstStrikeDamage));
    CHECK(step_logged(st.log, Step::CombatDamage));
}

TEST_CASE("Legame vitale in attacco e flag azzerato al turno successivo", "[combat][keywords]") {
    Staged st = staged(kUw, {"Plains"}, {"Swamp"}, {"Arid Mesa", "Ocelot Pride"});
    attack_with(st, {"Ocelot Pride"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK(st.s.players[kRakdos].life == 19);
    CHECK(st.s.players[kUw].life == 21);
    CHECK(st.s.players[kUw].gained_life_this_turn);
    CHECK_FALSE(st.s.players[kRakdos].gained_life_this_turn);

    pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2; });
    CHECK_FALSE(st.s.players[kUw].gained_life_this_turn);
}

// ---------------------------------------------------------------------------
// Sanctifier en-Vec — Creature — Human Cleric 2/2
//   Protection from black and from red
//   ...                                                  (altre abilità non implementate)
// ---------------------------------------------------------------------------

TEST_CASE("Protezione: Lightning Bolt non puo' bersagliare Sanctifier en-Vec", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {"Sanctifier en-Vec", "Guide of Souls"},
                       {"Blood Crypt"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::TargetObject) CHECK(name_of(st.s, a.card) != "Sanctifier en-Vec");
    }
    CHECK(legal(st.s).size() == 3);  // due giocatori e Guide of Souls
}

TEST_CASE("Protezione: una creatura nera bloccata da Sanctifier non gli infligge danno", "[combat][keywords]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Sanctifier en-Vec"}, {"Marsh Flats", "Street Wraith"});
    attack_with(st, {"Street Wraith"});
    act(st, pass());
    block_with(st, "Street Wraith", {"Sanctifier en-Vec"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK(st.s.objects[find_on_battlefield(st.s, "Sanctifier en-Vec").card].damage == 0);
    CHECK(st.s.objects[find_on_battlefield(st.s, "Street Wraith").card].damage == 2);
}

TEST_CASE("Protezione: le creature nere e rosse non bloccano Sanctifier", "[combat][keywords]") {
    Staged st = staged(kUw, {"Plains"}, {"Swamp"}, {"Arid Mesa", "Sanctifier en-Vec"},
                       {"Stitcher's Supplier", "Detective's Phoenix"});
    attack_with(st, {"Sanctifier en-Vec"});
    act(st, pass());
    CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
    CHECK(st.s.players[kRakdos].life == 18);
}
