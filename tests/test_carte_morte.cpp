// Test della diagnostica delle carte morte (tools/carte_morte.hpp), su stati preparati: una carta
// lanciabile tenuta per due turni, una non lanciabile per mancanza di mana, le terre, le sconfitte
// con carte in mano, l'ordine e il formato del CSV.

#include "carte_morte.hpp"
#include "goldfish_agent.hpp"
#include "matchup.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

// UW (giocatore 0) e' passivo (l'agente del goldfish) o gioca con il suo agente; Rakdos e' passivo.
// Ogni decisione passa dall'osservatore prima di essere applicata, come nel matchup.
struct Observed {
    Staged st;
    DeadCardTracker tracker;
    UwAgent uw;
    GoldfishAgent fish;
    bool uw_plays;

    Observed(Staged staged, bool plays) : st(std::move(staged)), tracker(db_with_tokens()), uw(db_with_tokens(), 3), uw_plays(plays) {}

    void run_until_turn(std::uint16_t turn) {
        for (int guard = 0; guard < 2000 && st.s.turn.turn_number < turn && st.s.pending.kind != DecisionKind::GameOver; ++guard) {
            const std::vector<Action> actions = legal(st.s);
            const Action chosen = (st.s.pending.player == kUw && uw_plays) ? uw.choose(st.s, actions) : fish.choose(st.s, actions);
            tracker.on_decision(st.s, actions, chosen);
            act(st, chosen);
        }
        REQUIRE(st.s.turn.turn_number >= turn);
    }
    void finish() { tracker.on_end(st.s); }

    [[nodiscard]] CardCounts counts(PlayerId deck, const std::string& card, DeckSide side) const {
        const auto& cards = tracker.diagnostics()[deck].cards;
        const auto it = cards.find(card);
        return it == cards.end() ? CardCounts{} : it->second[side];
    }
};

// UW comincia (in play); Plains e Arid Mesa in campo: la Mesa tiene la priorita' a UW a ogni passo.
Staged uw_staged(std::initializer_list<const char*> hand, std::initializer_list<const char*> field) {
    return staged(kUw, hand, {}, field);
}

}  // namespace

TEST_CASE("Carte morte: una carta lanciabile tenuta per due turni conta due turni", "[carte_morte]") {
    Observed o(uw_staged({"Ocelot Pride"}, {"Plains", "Arid Mesa"}), false);
    o.run_until_turn(4);  // i turni di UW sono il 1 e il 3; il 2 e' di Rakdos
    o.finish();
    const CardCounts c = o.counts(kUw, "Ocelot Pride", kOnPlay);
    CHECK(c.games_in_hand == 1);
    CHECK(c.uses == 0);
    CHECK(c.dead_turns == 2);  // non e' lanciabile nel turno avversario (non ha il lampo)
    CHECK(c.in_hand_at_end == 1);
    CHECK(c.dead_turns_per_game() == 2.0);
    CHECK(o.counts(kUw, "Ocelot Pride", kOnDraw).games_in_hand == 0);  // il lato e' quello del proprietario
}

TEST_CASE("Carte morte: una carta non lanciabile per mancanza di mana non conta", "[carte_morte]") {
    Observed o(uw_staged({"Quantum Riddler"}, {"Plains", "Arid Mesa"}), false);  // costa 5, il warp {1}{U}: una sola terra
    o.run_until_turn(4);
    o.finish();
    const CardCounts c = o.counts(kUw, "Quantum Riddler", kOnPlay);
    CHECK(c.games_in_hand == 1);
    CHECK(c.dead_turns == 0);
    CHECK(c.uses == 0);
    CHECK(c.in_hand_at_end == 1);
}

TEST_CASE("Carte morte: la carta lanciata e' un uso e non un turno morto", "[carte_morte]") {
    Observed o(uw_staged({"Ocelot Pride"}, {"Plains", "Arid Mesa"}), true);
    o.run_until_turn(4);
    o.finish();
    const CardCounts c = o.counts(kUw, "Ocelot Pride", kOnPlay);
    CHECK(c.games_in_hand == 1);
    CHECK(c.uses == 1);
    CHECK(c.dead_turns == 0);
    CHECK(c.in_hand_at_end == 0);
}

TEST_CASE("Carte morte: una terra non giocata conta, una giocata no, e se ne gioca una per turno", "[carte_morte]") {
    SECTION("passivo: la terra e' giocabile a ogni turno di UW e non si gioca") {
        Observed o(uw_staged({"Plains", "Plains"}, {"Arid Mesa"}), false);
        o.run_until_turn(4);
        o.finish();
        const CardCounts c = o.counts(kUw, "Plains", kOnPlay);
        CHECK(c.dead_turns == 2);
        CHECK(c.uses == 0);
    }
    SECTION("con l'agente di UW: una terra al turno, nessun turno morto") {
        Observed o(uw_staged({"Plains", "Plains"}, {"Arid Mesa"}), true);
        o.run_until_turn(4);
        o.finish();
        const CardCounts c = o.counts(kUw, "Plains", kOnPlay);
        CHECK(c.uses >= 1);        // la seconda terra del turno 3 puo' essere una fetch pescata, preferita alla Plains
        CHECK(c.dead_turns == 0);  // ma ogni turno di UW ha una terra giocata
    }
}

TEST_CASE("Carte morte: Rakdos in draw ha il suo lato, e le carte dell'altro mazzo restano al suo mazzo", "[carte_morte]") {
    Staged st = staged(kUw, {}, {"Street Wraith"}, {"Arid Mesa"});
    Observed o(std::move(st), false);
    o.run_until_turn(4);
    o.finish();
    CHECK(o.counts(kRakdos, "Street Wraith", kOnDraw).games_in_hand == 1);
    CHECK(o.counts(kRakdos, "Street Wraith", kOnPlay).games_in_hand == 0);
    CHECK(o.counts(kUw, "Street Wraith", kOnDraw).games_in_hand == 0);
    // Il ciclo di Street Wraith costa solo 2 vita: e' sempre attivabile dalla mano, e passivo non si fa.
    CHECK(o.counts(kRakdos, "Street Wraith", kOnDraw).dead_turns >= 1);
}

TEST_CASE("Carte morte: le sconfitte portano le carte in mano alla sconfitta", "[carte_morte]") {
    Staged st = uw_staged({"Plains", "Guide of Souls", "Solitude", "Teferi, Time Raveler"}, {"Arid Mesa"});
    DeadCardTracker tracker(db_with_tokens());
    const std::size_t hand = st.s.players[kUw].hand.size();
    REQUIRE(hand >= 3);
    st.s.turn.game_over = true;
    st.s.turn.winner = kRakdos;
    tracker.on_end(st.s);
    const DeckDiagnostics& uw = tracker.diagnostics()[kUw];
    const DeckDiagnostics& rakdos = tracker.diagnostics()[kRakdos];
    CHECK(uw.games[kOnPlay] == 1);
    CHECK(uw.losses[kOnPlay] == 1);
    CHECK(uw.loss_hand[kOnPlay].at(static_cast<int>(hand)) == 1);
    CHECK(uw.losses_with_at_least(3) == 1);
    CHECK(uw.losses_with_at_least(static_cast<int>(hand) + 1) == 0);
    CHECK(rakdos.games[kOnDraw] == 1);
    CHECK(rakdos.total_losses() == 0);  // chi vince non perde
    CHECK(rakdos.losses_with_at_least(0) == 0);
}

TEST_CASE("Carte morte: una partita senza vincitore non e' una sconfitta", "[carte_morte]") {
    Staged st = uw_staged({"Plains"}, {"Arid Mesa"});
    DeadCardTracker tracker(db_with_tokens());
    tracker.on_end(st.s);
    CHECK(tracker.diagnostics()[kUw].total_losses() == 0);
    CHECK(tracker.diagnostics()[kUw].games[kOnPlay] == 1);
}

TEST_CASE("Carte morte: l'osservatore non cambia la partita", "[carte_morte][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    DeadCardTracker tracker(db_with_tokens());
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        GameLog without, with;
        const MatchupGame a = play_matchup(c, seed, &without);
        const MatchupGame b = play_matchup(c, seed, &with, nullptr, nullptr, nullptr, &tracker);
        CHECK(matchup_game_json(a) == matchup_game_json(b));
        CHECK(without.entries == with.entries);  // lo stesso registro, evento per evento
    }
    // Sei partite: ogni mazzo ne ha giocate sei, tre per lato.
    CHECK(tracker.diagnostics()[kUw].games[kOnPlay] + tracker.diagnostics()[kUw].games[kOnDraw] == 6);
    CHECK(tracker.diagnostics()[kUw].games[kOnPlay] == 3);
}

namespace {

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

std::array<DeckDiagnostics, 2> sample() {
    std::array<DeckDiagnostics, 2> d;
    auto card = [&](int deck, const std::string& name, CardCounts play, CardCounts draw) {
        d[static_cast<std::size_t>(deck)].cards[name] = {play, draw};
    };
    card(0, "Solitude", CardCounts{10, 5, 5, 1}, CardCounts{10, 5, 5, 1});            // 0,5 per partita
    card(0, "Teferi, Time Raveler", CardCounts{4, 0, 8, 2}, CardCounts{0, 0, 0, 0});  // 2,0 sul lato play
    card(0, "Ocelot Pride", CardCounts{20, 20, 0, 0}, CardCounts{20, 20, 2, 0});      // 0,05 in tutto
    card(1, "Flare of Malice", CardCounts{5, 1, 5, 0}, CardCounts{5, 1, 5, 0});
    d[0].games = {30, 30};
    d[0].losses = {2, 3};
    d[0].loss_hand[kOnPlay] = {{1, 1}, {4, 1}};
    d[0].loss_hand[kOnDraw] = {{0, 1}, {3, 1}, {5, 1}};
    d[1].games = {30, 30};
    return d;
}

}  // namespace

TEST_CASE("Carte morte: il CSV ha un'intestazione, i campi con la virgola tra apici, l'ordine decrescente", "[carte_morte][csv]") {
    const auto lines = lines_of(dead_cards_csv(sample(), {"UW Blink", "Rakdos Delirium"}));
    REQUIRE(lines.size() > 1);
    CHECK(lines[0] ==
          "mazzo,lato,carta,partite_in_mano,lanci_o_attivazioni,turni_lanciabile_non_lanciata,"
          "turni_lanciabile_non_lanciata_per_partita,partite_ancora_in_mano_alla_fine");
    // Il mazzo di UW prima, poi quello di Rakdos; dentro ogni mazzo i lati "tutte", "play", "draw".
    std::vector<std::string> uw_all;
    for (const std::string& l : lines) {
        if (l.rfind("UW Blink,tutte,", 0) == 0) uw_all.push_back(l);
    }
    REQUIRE(uw_all.size() == 3);
    CHECK(uw_all[0].find("\"Teferi, Time Raveler\"") != std::string::npos);  // 8 turni su 4 partite: 2,0
    CHECK(uw_all[1].find("Solitude") != std::string::npos);                   // 10 turni su 20: 0,5
    CHECK(uw_all[2].find("Ocelot Pride") != std::string::npos);               // 2 su 40: 0,05
    std::size_t first_uw = 0, first_rakdos = 0;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (first_uw == 0 && lines[i].rfind("UW Blink,", 0) == 0) first_uw = i;
        if (first_rakdos == 0 && lines[i].rfind("Rakdos Delirium,", 0) == 0) first_rakdos = i;
    }
    CHECK(first_uw == 1);
    CHECK(first_rakdos > first_uw);
    CHECK(lines[first_uw].find(",tutte,") != std::string::npos);
    // Il lato play di Teferi ha 4 partite e 8 turni.
    bool found = false;
    for (const std::string& l : lines) {
        if (l == "UW Blink,play,\"Teferi, Time Raveler\",4,0,8,2.0000,2") found = true;
    }
    CHECK(found);
}

TEST_CASE("Carte morte: il riassunto dice le prime carte e le sconfitte con tre o piu' carte in mano", "[carte_morte][csv]") {
    const std::string text = dead_cards_summary(sample(), {"UW Blink", "Rakdos Delirium"}, 2);
    // UW: 5 sconfitte; con 3 o piu' carte: 4 (4, 3, 5 e 3... ) -> in play {1, 4}, in draw {0, 3, 5}: 4, 3, 5 = 3 su 5.
    CHECK(text.find("Sconfitte con 3 o piu' carte in mano: 3 su 5 (60.0%)") != std::string::npos);
    CHECK(text.find("Teferi, Time Raveler") != std::string::npos);
    CHECK(text.find("Ocelot Pride") == std::string::npos);  // solo le prime due
    CHECK(text.find("Rakdos Delirium") != std::string::npos);
}
