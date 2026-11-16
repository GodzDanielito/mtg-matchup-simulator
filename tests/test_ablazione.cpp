// Test dell'ablazione (docs/metriche.md, sezione 3, aggiunta A; passo 2b): --sostituisci "Carta:Terra" di
// metriche.exe sostituisce UNA copia di una carta con una terra base nella stessa posizione della lista, cosi'
// lo stesso seed mescola gli stessi CardId nello stesso ordine e le due partite coincidono finche' la copia
// sostituita non conta.
//
// (a) senza sostituzioni la configurazione e le partite non cambiano (i registri della sezione 1 si
//     controllano con metriche.exe);
// (b) Plains al posto di una Plains, Swamp al posto di una Swamp: partite identiche voce per voce;
// (c) partite scelte: la prima differenza tra i due registri e' l'arrivo in mano della copia sostituita.

#include "game.hpp"
#include "matchup.hpp"
#include "metriche.hpp"
#include "test_helpers.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kU = 0;  // UW
constexpr PlayerId kR = 1;  // Rakdos

// La lista espansa: un nome per copia, nell'ordine in cui create_game assegna i CardId.
std::vector<std::string> expand(const Decklist& d) {
    std::vector<std::string> out;
    for (const DeckEntry& e : d.main) {
        for (int i = 0; i < e.count; ++i) out.push_back(e.card);
    }
    return out;
}

std::map<std::string, int> counts(const Decklist& d) {
    std::map<std::string, int> out;
    for (const DeckEntry& e : d.main) out[e.card] += e.count;
    return out;
}

MatchupConfig v1_config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

// Indice della prima voce diversa tra due registri (la lunghezza del piu' corto se uno e' l'inizio dell'altro;
// la lunghezza comune se sono identici).
std::size_t first_difference(const GameLog& a, const GameLog& b) {
    const std::size_t n = std::min(a.entries.size(), b.entries.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!(a.entries[i] == b.entries[i])) return i;
    }
    return n;
}

// La prima voce in cui la copia `id` lascia il grimorio; entries.size() se non lo lascia mai.
std::size_t first_move_from_library(const GameLog& log, CardId id) {
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind == LogEntry::Kind::ZoneChange && e.action.card == id && e.from == Zone::Library) return i;
    }
    return log.entries.size();
}

}  // namespace

TEST_CASE("Ablazione: la stringa Carta:Terra", "[ablazione]") {
    const Substitution s = parse_substitution("Solitude:Plains");
    CHECK(s.card == "Solitude");
    CHECK(s.land == "Plains");
    // Il separatore e' l'ultimo ':' (i nomi Oracle con la virgola restano interi).
    const Substitution t = parse_substitution("Teferi, Time Raveler:Plains");
    CHECK(t.card == "Teferi, Time Raveler");
    CHECK(t.land == "Plains");
    CHECK_THROWS_AS(parse_substitution("Solitude"), std::invalid_argument);
    CHECK_THROWS_AS(parse_substitution(":Plains"), std::invalid_argument);
    CHECK_THROWS_AS(parse_substitution("Solitude:"), std::invalid_argument);
}

TEST_CASE("Ablazione: una copia sostituita nella stessa posizione della lista", "[ablazione]") {
    const Decklist& uw = uw_sim_deck();
    const std::vector<std::string> before = expand(uw);

    SECTION("una carta da quattro copie") {
        const Decklist d = replace_one_copy(uw, "Solitude", "Plains");
        const std::vector<std::string> after = expand(d);
        REQUIRE(after.size() == before.size());
        CHECK(d.main_size() == 60);
        CHECK(validate_decklist(d).empty());
        const int pos = replaced_copy_position(uw, "Solitude");
        REQUIRE(pos >= 0);
        // L'ultima copia della voce: la posizione dopo c'e' gia' un'altra carta.
        CHECK(before[static_cast<std::size_t>(pos)] == "Solitude");
        CHECK(before[static_cast<std::size_t>(pos) + 1] != "Solitude");
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (static_cast<int>(i) == pos) {
                CHECK(after[i] == "Plains");
            } else {
                CHECK(after[i] == before[i]);
            }
        }
        CHECK(counts(d)["Solitude"] == 3);
        CHECK(counts(d)["Plains"] == counts(uw)["Plains"] + 1);
    }
    SECTION("una carta da una copia sparisce dalla lista") {
        const Decklist d = replace_one_copy(uw, "Teferi, Time Raveler", "Plains");
        const std::vector<std::string> after = expand(d);
        const int pos = replaced_copy_position(uw, "Teferi, Time Raveler");
        REQUIRE(pos >= 0);
        CHECK(after[static_cast<std::size_t>(pos)] == "Plains");
        CHECK(counts(d).count("Teferi, Time Raveler") == 0);
        CHECK(validate_decklist(d).empty());
    }
    SECTION("due sostituzioni nella stessa lista: ognuna nella sua posizione") {
        const Decklist d = replace_one_copy(replace_one_copy(uw, "Solitude", "Plains"), "Ephemerate", "Plains");
        const std::vector<std::string> after = expand(d);
        int changed = 0;
        for (std::size_t i = 0; i < before.size(); ++i) changed += after[i] != before[i];
        CHECK(changed == 2);
        CHECK(after[static_cast<std::size_t>(replaced_copy_position(uw, "Ephemerate"))] == "Plains");
    }
    SECTION("errori") {
        CHECK(replaced_copy_position(uw, "Lightning Bolt") == -1);
        CHECK_THROWS_AS(replace_one_copy(uw, "Lightning Bolt", "Plains"), DeckError);
        CHECK_THROWS_AS(replace_one_copy(uw, "Solitude", "Hallowed Fountain"), DeckError);
    }
}

TEST_CASE("Ablazione: stesso seed, stesso grimorio a parte la copia sostituita", "[ablazione]") {
    const Decklist uw2 = replace_one_copy(uw_sim_deck(), "Solitude", "Plains");
    const Decklist rk2 = replace_one_copy(rakdos_deck(), "Thoughtseize", "Swamp");
    const auto u_id = static_cast<CardId>(kU * kDeckSize + static_cast<std::size_t>(replaced_copy_position(uw_sim_deck(), "Solitude")));
    const auto r_id = static_cast<CardId>(kR * kDeckSize + static_cast<std::size_t>(replaced_copy_position(rakdos_deck(), "Thoughtseize")));
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const GameState a = create_game(db_with_tokens(), uw_sim_deck(), rakdos_deck(), seed);
        const GameState b = create_game(db_with_tokens(), uw2, rk2, seed);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            REQUIRE(a.players[p].library.size() == b.players[p].library.size());
            for (std::size_t i = 0; i < a.players[p].library.size(); ++i) {
                const CardId id = a.players[p].library[i];
                CHECK(b.players[p].library[i] == id);
                const bool replaced = id == u_id || id == r_id;
                CHECK((a.objects[id].def == b.objects[id].def) == !replaced);
            }
        }
        CHECK(b.objects[u_id].def == db_with_tokens().id_of("Plains"));
        CHECK(b.objects[r_id].def == db_with_tokens().id_of("Swamp"));
    }
}

TEST_CASE("Ablazione: la carta si trova nel mazzo giusto, anche per prefisso", "[ablazione]") {
    MatchupConfig c = v1_config();
    const AppliedSubstitution a = apply_substitution(c, parse_substitution("Thoughtseize:Swamp"));
    CHECK(a.player == kR);
    CHECK(a.card == "Thoughtseize");
    CHECK(a.land == "Swamp");
    CHECK(a.position == replaced_copy_position(rakdos_deck(), "Thoughtseize"));
    CHECK(counts(c.rakdos)["Thoughtseize"] == 3);
    CHECK(counts(c.uw) == counts(uw_sim_deck()));

    // Il Troll ha una lettera non ASCII: sulla riga di comando di Windows si scrive con un prefisso.
    const AppliedSubstitution t = apply_substitution(c, parse_substitution("Troll:Swamp"));
    CHECK(t.player == kR);
    CHECK(t.card == "Troll of Khazad-d\xC3\xBBm");
    const AppliedSubstitution p = apply_substitution(c, parse_substitution("Phelia:Plains"));
    CHECK(p.player == kU);
    CHECK(p.card == "Phelia, Exuberant Shepherd");

    CHECK_THROWS_AS(apply_substitution(c, parse_substitution("Brainstorm:Plains")), std::invalid_argument);
    // "S" e' il prefisso di piu' carte (Solitude, Seam Rip, Swamp, ...).
    CHECK_THROWS_AS(apply_substitution(c, parse_substitution("S:Plains")), std::invalid_argument);
    CHECK_THROWS_AS(apply_substitution(c, parse_substitution("Solitude:Hallowed Fountain")), DeckError);
}

TEST_CASE("Ablazione (a): senza sostituzioni la configurazione e le partite non cambiano", "[ablazione][matchup]") {
    const MatchupConfig base = v1_config();
    MatchupConfig c = v1_config();
    const std::vector<Substitution> none;
    for (const Substitution& s : none) apply_substitution(c, s);
    CHECK(expand(c.uw) == expand(base.uw));
    CHECK(expand(c.rakdos) == expand(base.rakdos));
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameLog la, lb;
        CHECK(matchup_game_json(play_matchup(base, seed, &la)) == matchup_game_json(play_matchup(c, seed, &lb)));
        CHECK(la.entries == lb.entries);
    }
}

TEST_CASE("Ablazione (b): una terra base al posto della stessa terra base non cambia nessuna partita",
          "[ablazione][matchup]") {
    const MatchupConfig base = v1_config();
    MatchupConfig c = v1_config();
    const AppliedSubstitution u = apply_substitution(c, parse_substitution("Plains:Plains"));
    const AppliedSubstitution r = apply_substitution(c, parse_substitution("Swamp:Swamp"));
    CHECK(u.player == kU);
    CHECK(r.player == kR);
    // La lista e' diversa (una voce in piu' per mazzo), le carte nelle posizioni no.
    CHECK(c.uw.main.size() == base.uw.main.size() + 1);
    CHECK(expand(c.uw) == expand(base.uw));
    CHECK(expand(c.rakdos) == expand(base.rakdos));
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        GameLog la, lb;
        const MatchupGame ga = play_matchup(base, seed, &la);
        const MatchupGame gb = play_matchup(c, seed, &lb);
        CHECK(matchup_game_json(ga) == matchup_game_json(gb));
        CHECK(la.entries == lb.entries);
    }
}

TEST_CASE("Ablazione (c): la prima differenza e' l'arrivo in mano della copia sostituita", "[ablazione][matchup]") {
    const MatchupConfig base = v1_config();
    struct Case {
        const char* card;
        const char* land;
        PlayerId owner;
    };
    for (const Case& k : {Case{"Solitude", "Plains", kU}, Case{"Ocelot Pride", "Plains", kU},
                          Case{"Thoughtseize", "Swamp", kR}, Case{"Nethergoyf", "Swamp", kR}}) {
        INFO(k.card);
        MatchupConfig c = v1_config();
        const AppliedSubstitution a = apply_substitution(c, Substitution{k.card, k.land});
        REQUIRE(a.player == k.owner);
        const auto id = static_cast<CardId>(static_cast<std::size_t>(k.owner) * kDeckSize +
                                            static_cast<std::size_t>(a.position));
        // Le definizioni per CardId sono le stesse in ogni partita (create_game le assegna nell'ordine della
        // lista), tranne quella della copia sostituita.
        const GameState layout = create_game(db_with_tokens(), base.uw, base.rakdos, 1);
        const auto name_of = [&](CardId c) -> std::string {
            return c == kNoCard ? "-" : db_with_tokens().get(layout.objects[c].def).name;
        };
        const auto searches = [&](CardId c) {
            if (c == kNoCard) return false;
            const CardDef& d = db_with_tokens().get(layout.objects[c].def);
            return d.rules.land.fetch_types != 0 || d.rules.landcycling.types != 0;
        };
        int drawn_first = 0;
        for (std::uint64_t seed = 1; seed <= 40; ++seed) {
            INFO("seed " << seed);
            GameLog la, lb;
            play_matchup(base, seed, &la);
            play_matchup(c, seed, &lb);
            const std::size_t move = first_move_from_library(la, id);
            const std::size_t diff = first_difference(la, lb);
            // I registri non cambiano mai prima che la copia lasci il grimorio, tranne in una decisione del suo
            // proprietario che guarda il grimorio (cercare una terra, sorvegliare la carta in cima): la voce
            // della decisione viene prima dello spostamento.
            CHECK(diff <= move);
            if (diff < move) {
                REQUIRE(diff < lb.entries.size());
                const LogEntry& e = la.entries[diff];
                const LogEntry& f = lb.entries[diff];
                INFO("voce " << diff << ", turno " << e.turn << ", decisione " << static_cast<int>(e.decision)
                             << ": " << name_of(e.action.card) << " contro " << name_of(f.action.card));
                CHECK(e.kind == LogEntry::Kind::Action);
                CHECK(e.player == k.owner);
                // Con la priorita': l'agente sceglie una terra o un ciclo guardando cosa c'e' da cercare nel
                // grimorio (Rakdos: la fetch o il Troll quando nel grimorio c'e' una Swamp in piu').
                CHECK((e.decision == DecisionKind::SearchLibrary || e.decision == DecisionKind::Surveil ||
                       (e.decision == DecisionKind::Priority && (searches(e.action.card) || searches(f.action.card)))));
                continue;
            }
            if (move == la.entries.size()) {
                // La copia non lascia mai il grimorio: partite identiche.
                CHECK(diff == la.entries.size());
                CHECK(la.entries.size() == lb.entries.size());
                continue;
            }
            // Lo spostamento della copia e' la prima differenza.
            CHECK(diff == move);
            const LogEntry& m = la.entries[move];
            if (m.to != Zone::Hand) continue;
            // La copia arriva in mano (mano iniziale, pescata o cercata): la voce differisce solo nella
            // definizione della carta.
            ++drawn_first;
            REQUIRE(move < lb.entries.size());
            LogEntry same = lb.entries[move];
            CHECK(same.def == db_with_tokens().id_of(k.land));
            CHECK(m.def == db_with_tokens().id_of(a.card));
            same.def = m.def;
            CHECK(same == m);
        }
        CHECK(drawn_first >= 5);
    }
}

TEST_CASE("Metriche: i controlli contano le copie di una carta divisa in piu' voci", "[metriche][ablazione]") {
    // Con una sostituzione la lista ha due voci Plains: le copie viste si confrontano con la somma delle voci.
    const MatchupConfig base = v1_config();
    MatchupConfig c = v1_config();
    apply_substitution(c, parse_substitution("Plains:Plains"));
    REQUIRE(c.uw.main.size() == base.uw.main.size() + 1);
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        GameLog log;
        const MatchupGame g = play_matchup(c, seed, &log);
        const CompactRecord r = build_compact_record(db_with_tokens(), g, log);
        int plains = 0;
        for (const CopyRecord& cr : r.players[kU].cards) plains += cr.def == db_with_tokens().id_of("Plains");
        if (plains < 2) continue;  // con una sola copia vista anche il conto sbagliato passa
        ++checked;
        INFO("seed " << seed << ", Plains viste " << plains);
        CHECK(check_compact_record(db_with_tokens(), r, c.uw, c.rakdos).empty());
    }
    CHECK(checked >= 3);
}
