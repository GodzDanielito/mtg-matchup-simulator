// Il giocatore forte di UW, livello A minimo (docs/ricerca.md, passo 2): ricerca piatta con determinizzazione,
// mosse composte, mondi comuni, "a parita' l'euristico" con l'intervallo al 99%.

#include "gemello.hpp"
#include "giocatore_forte.hpp"
#include "matchup.hpp"
#include "player_view.hpp"
#include "posizioni.hpp"
#include "rakdos_agent.hpp"
#include "ricerca_piatta.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <string>
#include <type_traits>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

const CardDatabase& db() { return db_with_tokens(); }
GameState position(const std::string& id) { return test_position(db(), uw_sim_deck(), rakdos_deck(), id).state; }
std::string name_of(const GameState& s, CardId id) { return db().get(s.objects[id].def).name; }

// Una tabella sintetica: le vittorie di ogni mossa, mondo per mondo.
SearchTable table(const std::vector<std::vector<int>>& wins, std::size_t heuristic) {
    SearchTable t;
    t.heuristic = heuristic;
    t.worlds = static_cast<std::uint32_t>(wins.front().size());
    for (const auto& row : wins) {
        t.moves.push_back(CompoundMove{Action{}});
        for (const int x : row) t.wins.push_back(static_cast<std::uint8_t>(x));
    }
    return t;
}

// Passa (o la prima azione legale) finche' `stop` non e' vero.
void advance_until(GameState& s, const std::function<bool(const GameState&)>& stop) {
    for (int i = 0; i < 400 && !stop(s); ++i) {
        REQUIRE(s.pending.kind != DecisionKind::GameOver);
        const auto actions = legal(s);
        const auto pass = std::find_if(actions.begin(), actions.end(),
                                       [](const Action& a) { return a.type == ActionType::Pass; });
        apply(s, db(), pass != actions.end() ? *pass : actions.front());
    }
    REQUIRE(stop(s));
}

StrongConfig small_config(std::uint32_t worlds) {
    StrongConfig c;
    c.worlds = worlds;
    return c;
}

MatchupConfig matchup_config() {
    MatchupConfig c;
    c.db = &db();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// "A parita', l'euristico": l'intervallo al 99% per scostarsi
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Ricerca: quantile della t di Student", "[ricerca]") {
    struct Case {
        double p, df, value;
    };
    for (const Case c : {Case{0.995, 1, 63.657}, Case{0.995, 3, 5.841}, Case{0.995, 15, 2.947}, Case{0.995, 30, 2.750},
                         Case{0.995, 99, 2.626}, Case{0.995, 1e9, 2.5758}, Case{0.975, 20, 2.086},
                         Case{0.975, 1e9, 1.95996}}) {
        INFO("p = " << c.p << ", gradi di liberta' " << c.df);
        CHECK(std::abs(student_t_quantile(c.p, c.df) - c.value) < 0.002 * c.value);
    }
}

TEST_CASE("Ricerca: a parita' l'euristico; ci si scosta solo con l'intervallo al 99%", "[ricerca]") {
    SECTION("mosse uguali: l'euristico") {
        const SearchTable t = table({{1, 0, 1, 1}, {1, 0, 1, 1}, {0, 0, 1, 0}}, 1);
        const SearchChoice c = choose_with_confidence(t, 4);
        CHECK(c.best == 1);  // a pari vittorie conta l'euristico
        CHECK(c.chosen == 1);
        CHECK_FALSE(c.deviates);
    }
    SECTION("una mossa vince in tutti i mondi e l'euristico in nessuno: ci si scosta") {
        const SearchTable t = table({std::vector<int>(64, 0), std::vector<int>(64, 1)}, 0);
        const SearchChoice c = choose_with_confidence(t, 64);
        CHECK(c.best == 1);
        CHECK(c.chosen == 1);
        CHECK(c.deviates);
        CHECK(c.diff == 1.0);
        CHECK(c.low > 0.0);
    }
    SECTION("differenza significativa al 95% ma non al 99%: l'euristico") {
        // 100 mondi: la mossa 1 vince e l'euristico perde in 20, il contrario in 7, uguali negli altri. Media +0,13,
        // deviazione standard 0,506: al 99% l'intervallo comincia sotto lo zero (-0,003), al 95% sopra (+0,030).
        std::vector<int> h(100, 0), m(100, 0);
        for (int j = 0; j < 20; ++j) m[static_cast<std::size_t>(j)] = 1;
        for (int j = 20; j < 27; ++j) h[static_cast<std::size_t>(j)] = 1;
        for (int j = 27; j < 60; ++j) h[static_cast<std::size_t>(j)] = m[static_cast<std::size_t>(j)] = 1;
        const SearchTable t = table({h, m}, 0);
        const SearchChoice c99 = choose_with_confidence(t, 100, 0.99);
        CHECK(c99.best == 1);
        CHECK(c99.chosen == 0);
        CHECK_FALSE(c99.deviates);
        CHECK(std::abs(c99.diff - 0.13) < 1e-12);
        CHECK(c99.low < 0.0);
        const SearchChoice c95 = choose_with_confidence(t, 100, 0.95);
        CHECK(c95.chosen == 1);
        CHECK(c95.deviates);
        CHECK(c95.low > 0.0);
    }
    SECTION("il prefisso: con i primi mondi soltanto") {
        std::vector<int> h(32, 1), m(32, 0);
        for (int j = 0; j < 16; ++j) {
            h[static_cast<std::size_t>(j)] = 0;
            m[static_cast<std::size_t>(j)] = 1;
        }
        const SearchTable t = table({h, m}, 0);
        CHECK(t.wins_of(1, 16) == 16);
        CHECK(t.wins_of(0, 16) == 0);
        CHECK(t.wins_of(0, 32) == 16);
        CHECK(choose_with_confidence(t, 16).chosen == 1);  // nei primi 16 la mossa 1 vince sempre
        CHECK(choose_with_confidence(t, 32).chosen == 0);  // su 32 sono pari
    }
    SECTION("la migliore tra piu' mosse: a pari vittorie la prima") {
        const SearchTable t = table({std::vector<int>(40, 0), std::vector<int>(40, 1), std::vector<int>(40, 1)}, 0);
        CHECK(choose_with_confidence(t, 40).best == 1);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Quali decisioni si cercano
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Ricerca: quali decisioni si cercano", "[ricerca]") {
    auto kind = [](const GameState& s) { return classify_decision(s, db(), legal(s), kUw); };
    SECTION("le posizioni test") {
        CHECK(kind(position("2")) == SearchedKind::Main);           // propria fase principale, pila vuota
        CHECK(kind(position("4")) == SearchedKind::Main);
        CHECK(kind(position("7a")) == SearchedKind::Response);      // ritorno di Phelia in pila
        CHECK(kind(position("8b")) == SearchedKind::Response);      // trigger di FOMO in pila
        CHECK(kind(position("1")) == SearchedKind::OpponentTurn);   // turno di Rakdos, March e Solitude
        CHECK(kind(position("10a")) == SearchedKind::None);         // ricerca di Recruiter: all'euristico
    }
    SECTION("nel turno avversario con la sola fetch: non si cerca") {
        Staged st = staged(kRakdos, {}, {}, {"Flooded Strand"}, {});
        advance_until(st.s, [](const GameState& g) { return is_decision(g, DecisionKind::Priority, kUw); });
        REQUIRE(st.s.turn.active_player == kRakdos);
        const auto actions = legal(st.s);
        REQUIRE(std::any_of(actions.begin(), actions.end(),
                            [&](const Action& a) { return is_fetch_activation(st.s, db(), a); }));
        CHECK(kind(st.s) == SearchedKind::None);
    }
    SECTION("nel turno avversario con Solitude evocabile: si cerca") {
        Staged st = staged(kRakdos, {"Solitude", "Ephemerate"}, {}, {"Plains"}, {"Nethergoyf"});
        advance_until(st.s, [](const GameState& g) { return is_decision(g, DecisionKind::Priority, kUw); });
        REQUIRE(st.s.turn.active_player == kRakdos);
        CHECK(kind(st.s) == SearchedKind::OpponentTurn);
    }
    SECTION("nel proprio turno fuori dalla fase principale, pila vuota: non si cerca") {
        Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Ocelot Pride"}, {});
        advance_until(st.s, [](const GameState& g) {
            return is_decision(g, DecisionKind::Priority, kUw) && g.turn.step != Step::Main1 &&
                   g.turn.step != Step::Main2 && g.stack_items.empty();
        });
        CHECK(kind(st.s) == SearchedKind::None);
    }
    CHECK(searched_kind_name(SearchedKind::Main) == "fase principale");
    CHECK(searched_kind_name(SearchedKind::Response) == "risposta");
    CHECK(searched_kind_name(SearchedKind::OpponentTurn) == "turno avversario");
}

// ---------------------------------------------------------------------------------------------------------------
// Mosse composte
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Ricerca: le mosse composte sono sequenze complete di scelte del lancio", "[ricerca]") {
    for (const char* id : {"1", "5", "8b", "9a", "6", "7a"}) {
        DYNAMIC_SECTION("posizione " << id) {
            const GameState s = position(id);
            const auto actions = legal(s);
            std::vector<std::string> texts;
            const std::vector<CompoundMove> moves = enumerate_compound_moves(s, db(), actions, &texts);
            REQUIRE(moves.size() >= actions.size());
            REQUIRE(texts.size() == moves.size());
            std::set<std::vector<std::tuple<int, int, int>>> distinct;
            for (std::size_t m = 0; m < moves.size(); ++m) {
                INFO("mossa " << texts[m]);
                REQUIRE_FALSE(moves[m].empty());
                CHECK(std::find(actions.begin(), actions.end(), moves[m].front()) != actions.end());
                CHECK_FALSE(texts[m].empty());
                CHECK(describe_move(s, db(), moves[m]) == texts[m]);
                GameState t = s;
                for (std::size_t k = 0; k < moves[m].size(); ++k) {
                    if (k > 0) CHECK(is_cast_choice(t, kUw));
                    apply(t, db(), moves[m][k]);
                }
                CHECK_FALSE(is_cast_choice(t, kUw));  // la mossa e' completa
                require_invariants(t);
                std::vector<std::tuple<int, int, int>> key;
                for (const Action& a : moves[m]) key.emplace_back(static_cast<int>(a.type), a.card, a.value);
                distinct.insert(key);
            }
            CHECK(distinct.size() == moves.size());  // nessuna mossa ripetuta
            // Ogni azione legale e' l'inizio di almeno una mossa.
            for (const Action& a : actions) {
                CHECK(std::any_of(moves.begin(), moves.end(), [&](const CompoundMove& m) { return m.front() == a; }));
            }
        }
    }
    SECTION("posizione 1: March con X=1 su Moonshadow senza esiliare carte e' una mossa") {
        const GameState s = position("1");
        const std::vector<CompoundMove> moves = enumerate_compound_moves(s, db(), legal(s));
        const bool found = std::any_of(moves.begin(), moves.end(), [&](const CompoundMove& m) {
            return m.size() == 3 && m[0].type == ActionType::CastSpell && name_of(s, m[0].card) == "March of Otherworldly Light" &&
                   m[1].type == ActionType::TargetObject && name_of(s, m[1].card) == "Moonshadow" &&
                   m[2] == Action{ActionType::ChooseOption, kNoCard, kStopExiling};
        });
        CHECK(found);
        CHECK(std::any_of(moves.begin(), moves.end(),
                          [](const CompoundMove& m) { return m.size() == 1 && m[0].type == ActionType::Pass; }));
    }
    SECTION("posizione 8b: Thraben Charm, danni su FOMO ed esilio del cimitero di Rakdos sono due mosse") {
        const GameState s = position("8b");
        const std::vector<CompoundMove> moves = enumerate_compound_moves(s, db(), legal(s));
        auto has = [&](std::uint8_t mode, auto&& target) {
            return std::any_of(moves.begin(), moves.end(), [&](const CompoundMove& m) {
                return m.size() == 3 && m[0].type == ActionType::CastSpell && name_of(s, m[0].card) == "Thraben Charm" &&
                       m[1] == Action{ActionType::ChooseOption, kNoCard, mode} && target(m[2]);
            });
        };
        CHECK(has(0, [&](const Action& a) { return a.type == ActionType::TargetObject && name_of(s, a.card) == "Fear of Missing Out"; }));
        CHECK(has(2, [](const Action& a) { return a == Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(1u << kRakdos)}; }));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Rollout e mondi comuni
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Ricerca: i semi dipendono solo dalla partita, dalla decisione e dal mondo", "[ricerca]") {
    std::set<std::uint64_t> seeds;
    for (std::uint64_t g = 1; g <= 20; ++g) {
        for (std::uint64_t i = 0; i < 20; ++i) seeds.insert(decision_seed(g, i));
    }
    CHECK(seeds.size() == 400);
    CHECK(decision_seed(7, 3) == decision_seed(7, 3));
    std::set<std::uint64_t> worlds;
    for (std::uint32_t j = 0; j < 100; ++j) {
        worlds.insert(world_seed(12345, j));
        worlds.insert(rollout_agent_seed(12345, j));
    }
    CHECK(worlds.size() == 200);  // mondi e agenti con semi diversi
}

TEST_CASE("Ricerca: rollout deterministico, mondi comuni a tutte le mosse", "[ricerca]") {
    const GameState s = position("2");
    const PlayerView view(s, db(), kUw);
    const RolloutConfig cfg;
    const std::vector<CompoundMove> moves = enumerate_compound_moves(s, db(), legal(s));
    REQUIRE(moves.size() >= 2);
    SECTION("stesso mondo e stesso seed degli agenti: stesso esito, stesse decisioni") {
        for (std::uint64_t w = 1; w <= 5; ++w) {
            std::uint64_t d1 = 0, d2 = 0;
            const bool a = rollout_wins(view.sample_world(w), db(), moves[0], 99 + w, cfg, &d1);
            const bool b = rollout_wins(view.sample_world(w), db(), moves[0], 99 + w, cfg, &d2);
            CHECK(a == b);
            CHECK(d1 == d2);
            CHECK(d1 > 20);  // la partita e' andata avanti fino alla fine
        }
    }
    SECTION("la stessa mossa due volte ha gli stessi esiti mondo per mondo") {
        const WorldSource world = [&view](std::uint64_t seed) { return view.sample_world(seed); };
        const SearchTable t = run_search(world, db(), {moves[1], moves[0], moves[1]}, 1, 24, 777, cfg);
        REQUIRE(t.wins.size() == 3 * 24);
        CHECK(t.worlds == 24);
        CHECK(t.heuristic == 1);
        CHECK(t.rollout_decisions > 0);
        for (std::uint32_t j = 0; j < 24; ++j) CHECK(t.wins[j] == t.wins[2 * 24 + j]);
        // Stessa ricerca, stessi esiti.
        const SearchTable again = run_search(world, db(), {moves[1], moves[0], moves[1]}, 1, 24, 777, cfg);
        CHECK(again.wins == t.wins);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Il giocatore forte
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Giocatore forte: e' un agente della vista, non riceve mai lo stato vero", "[ricerca][forte]") {
    static_assert(std::is_base_of_v<ViewAgent, StrongUwAgent>);
    static_assert(!std::is_invocable_v<decltype(&StrongUwAgent::choose), StrongUwAgent&, const GameState&,
                                       std::span<const Action>>);
    static_assert(!std::is_convertible_v<const GameState&, const PlayerView&>);
    SUCCEED();
}

TEST_CASE("Giocatore forte (b): sullo stato e sul gemello, la stessa scelta", "[ricerca][forte][hidden]") {
    int compared = 0, searched = 0, searches_in_library = 0;
    for (const std::uint64_t seed : {3u, 8u, 12u}) {
        GameState s = create_game(db(), uw_sim_deck(), rakdos_deck(), seed);
        start_game(s, db(), StartOptions{seed % 2 == 1 ? kUw : kRakdos, true});
        std::vector<SearchRecord> rec_a, rec_b;
        StrongUwAgent a(db(), seed, small_config(4), &rec_a);
        StrongUwAgent b(db(), seed, small_config(4), &rec_b);
        RakdosAgent rakdos(db(), seed);
        Pcg32 rng(seed, 0xabc);
        for (int i = 0; i < 3000 && s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= 60; ++i) {
            const auto actions = legal(s);
            if (s.pending.player != kUw) {
                apply(s, db(), rakdos.choose(s, actions));
                continue;
            }
            const GameState twin = twin_of(s, kUw, rng);
            const auto twin_actions = legal(twin);
            const Action x = a.choose(PlayerView(s, db(), kUw, actions), actions);
            const Action y = b.choose(PlayerView(twin, db(), kUw, twin_actions), twin_actions);
            INFO("seed " << seed << ", turno " << s.turn.turn_number << ", decisione " << static_cast<int>(s.pending.kind));
            CHECK(x.type == y.type);
            CHECK(x.value == y.value);
            if (x.card != kNoCard) {
                REQUIRE(y.card != kNoCard);
                CHECK(s.objects[x.card].def == twin.objects[y.card].def);
                if (!hidden_from(s, x.card, kUw)) CHECK(x.card == y.card);
                if (s.pending.kind == DecisionKind::SearchLibrary) ++searches_in_library;
            }
            CHECK(a.last_rule() == b.last_rule());
            CHECK(a.last_detail() == b.last_detail());
            ++compared;
            apply(s, db(), x);
        }
        REQUIRE(rec_a.size() == rec_b.size());
        for (std::size_t r = 0; r < rec_a.size(); ++r) {
            CHECK(rec_a[r].moves == rec_b[r].moves);
            CHECK(rec_a[r].wins == rec_b[r].wins);
            CHECK(rec_a[r].chosen == rec_b[r].chosen);
        }
        searched += static_cast<int>(rec_a.size());
    }
    CHECK(compared > 100);
    CHECK(searched > 10);
    CHECK(searches_in_library > 0);  // anche le ricerche nel grimorio (azioni con carte nascoste)
}

TEST_CASE("Giocatore forte: fino alla prima decisione cercata la partita e' quella degli euristici", "[ricerca][forte]") {
    for (const std::uint64_t seed : {1u, 2u, 6u}) {
        MatchupConfig heur = matchup_config();
        MatchupConfig strong = matchup_config();
        std::vector<SearchRecord> records;
        strong.uw_seat.kind = AgentKind::View;
        strong.uw_seat.view_agent = [&records](std::uint64_t g) {
            return std::make_unique<StrongUwAgent>(db(), g, small_config(4), &records);
        };
        GameLog a, b;
        std::map<std::size_t, std::string> rules;
        play_matchup(heur, seed, &a);
        play_matchup(strong, seed, &b, &rules);
        REQUIRE_FALSE(records.empty());
        // La prima voce della partita forte presa da una ricerca.
        std::size_t first_search = b.entries.size();
        for (const auto& [index, rule] : rules) {
            if (rule.rfind("UW: ricerca", 0) == 0) {
                first_search = index;
                break;
            }
        }
        REQUIRE(first_search < b.entries.size());
        REQUIRE(a.entries.size() > first_search);
        INFO("seed " << seed << ", prima ricerca alla voce " << first_search);
        CHECK(std::equal(a.entries.begin(), a.entries.begin() + static_cast<std::ptrdiff_t>(first_search),
                         b.entries.begin()));
    }
}

TEST_CASE("Giocatore forte: con R basso le partite finiscono, ogni azione e' legale, la regola del 99% e' rispettata",
          "[ricerca][forte]") {
    std::vector<SearchRecord> records;
    MatchupConfig config = matchup_config();
    config.uw_seat.kind = AgentKind::View;
    config.uw_seat.view_agent = [&records](std::uint64_t g) {
        return std::make_unique<StrongUwAgent>(db(), g, small_config(2), &records);
    };
    std::set<SearchedKind> kinds;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const MatchupGame g = play_matchup(config, seed);  // un'azione illegale lancerebbe EngineError
        INFO("seed " << seed);
        CHECK_FALSE(g.ending.empty());
        CHECK(g.turns <= config.turn_limit + 1);
    }
    REQUIRE(records.size() > 30);
    for (const SearchRecord& r : records) {
        kinds.insert(r.kind);
        REQUIRE(r.moves.size() == r.wins.size());
        CHECK(r.moves.size() >= 2);
        CHECK(r.worlds == 2);
        CHECK(r.kind != SearchedKind::None);
        if (r.deviates) {
            CHECK(r.chosen != r.heuristic);
            CHECK(r.chosen == r.best);
            CHECK(r.low > 0.0);
        } else {
            CHECK(r.chosen == r.heuristic);
        }
        CHECK_FALSE(r.heuristic_rule.empty());
    }
    CHECK(kinds.count(SearchedKind::Main) == 1);
}

TEST_CASE("Giocatore forte: stessa partita con lo stesso seed, anche dopo altre partite", "[ricerca][forte]") {
    MatchupConfig config = matchup_config();
    config.uw_seat.kind = AgentKind::View;
    config.uw_seat.view_agent = [](std::uint64_t g) { return std::make_unique<StrongUwAgent>(db(), g, small_config(3)); };
    GameLog first, again;
    play_matchup(config, 9, &first);
    play_matchup(config, 4);
    play_matchup(config, 9, &again);
    CHECK(first.entries == again.entries);
}

TEST_CASE("Ricerca: a meta' partita gli esiti cambiano da un mondo all'altro", "[ricerca]") {
    // Prima priorita' di UW nella sua fase principale dal quinto turno della partita, seed 3 (euristici v1).
    GameState s = create_game(db(), uw_sim_deck(), rakdos_deck(), 3);
    start_game(s, db(), StartOptions{kUw, true});
    UwAgent uw(db(), 3);
    RakdosAgent rakdos(db(), 3);
    while (!(s.turn.turn_number >= 5 && classify_decision(s, db(), legal(s), kUw) == SearchedKind::Main)) {
        REQUIRE(s.pending.kind != DecisionKind::GameOver);
        const auto actions = legal(s);
        apply(s, db(), s.pending.player == kUw ? uw.choose(s, actions) : rakdos.choose(s, actions));
    }
    const PlayerView view(s, db(), kUw);
    const std::vector<CompoundMove> moves = enumerate_compound_moves(s, db(), legal(s));
    const SearchTable t =
        run_search([&view](std::uint64_t w) { return view.sample_world(w); }, db(), moves, 0, 40, 5, RolloutConfig{});
    int varying = 0;
    for (std::size_t m = 0; m < moves.size(); ++m) {
        const int w = t.wins_of(m, 40);
        varying += (w > 0 && w < 40) ? 1 : 0;
    }
    CHECK(varying > 0);
}

TEST_CASE("Ricerca: un rollout che supera la capacita' del motore vale 0 e viene contato, non ferma la ricerca",
          "[ricerca]") {
    // Seed 227 del passo 2 (R = 256, decisione 59): una mossa che porta Rakdos a una catena di Flare of Malice e di
    // trigger riempiva la pila (capacita' 16) e il motore lanciava EngineLimitError, che fermava tutto il blocco.
    // Qui la pila e' riempita a mano: ogni mossa che lancia una magia supera il limite.
    const GameState open = position("1");
    const std::vector<CompoundMove> moves = enumerate_compound_moves(open, db(), legal(open));
    GameState s = open;
    while (s.stack.size() < s.stack.capacity()) s.stack.push_back(0);
    REQUIRE(moves.size() >= 2);
    std::size_t casts = 0;
    for (const CompoundMove& m : moves) casts += m.front().type == ActionType::CastSpell ? 1 : 0;
    REQUIRE(casts > 0);
    SearchTable t;
    REQUIRE_NOTHROW(t = run_search([&s](std::uint64_t) { return s; }, db(), moves, 0, 3, 5, RolloutConfig{}));
    CHECK(t.aborted >= casts * 3);  // almeno un rollout interrotto per ogni mossa che lancia, per ogni mondo
    for (std::size_t m = 0; m < moves.size(); ++m) {
        if (moves[m].front().type == ActionType::CastSpell) CHECK(t.wins_of(m, 3) == 0);
    }
}
