// Il giocatore forte nelle esecuzioni del passo 2 (tools/forte.hpp): canarino, partite a blocchi, confronto appaiato,
// curva di convergenza, linee delle posizioni test.

#include "esecuzioni.hpp"
#include "forte.hpp"
#include "matchup.hpp"
#include "posizioni.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <span>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;

const CardDatabase& db() { return db_with_tokens(); }

MatchupConfig matchup_config() {
    MatchupConfig c;
    c.db = &db();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

StrongRun strong_run(std::uint32_t worlds, int threads, bool cheat = false) {
    StrongRun r;
    r.base = matchup_config();
    r.strong.worlds = worlds;
    r.threads = threads;
    r.cheat = cheat;
    return r;
}

// Un posto diagnostico di prova: guarda lo stato vero e passa (o la prima azione).
class PeekingSeat final : public Seat {
public:
    explicit PeekingSeat(int* calls) : calls_(calls) {}
    Action choose(const GameState& s, std::span<const Action> legal) override {
        ++*calls_;
        (void)s.players[1].hand.size();  // lo stato vero
        for (const Action& a : legal) {
            if (a.type == ActionType::Pass || a.type == ActionType::Keep) return a;
        }
        return legal.front();
    }
    [[nodiscard]] std::string_view last_rule() const override { return "prova"; }
    [[nodiscard]] bool last_was_default() const override { return false; }
    void add_rule_counts(std::map<std::string, std::uint64_t>&) const override {}

private:
    int* calls_;
};

}  // namespace

TEST_CASE("Matchup: un posto diagnostico riceve lo stato vero; senza fabbrica e' un errore", "[matchup][giocatori]") {
    MatchupConfig config = matchup_config();
    int calls = 0;
    config.uw_seat.kind = AgentKind::Diagnostic;
    config.uw_seat.diagnostic = [&calls](PlayerId, const CardDatabase&, std::uint64_t) {
        return std::make_unique<PeekingSeat>(&calls);
    };
    const MatchupGame g = play_matchup(config, 2);
    CHECK(calls > 0);
    CHECK(static_cast<std::uint64_t>(calls) == g.uw_decisions);
    config.uw_seat.diagnostic = nullptr;
    CHECK_THROWS_AS(play_matchup(config, 2), EngineError);
}

TEST_CASE("Canarino: i mondi sono lo stato vero con generatori nuovi", "[ricerca][canarino]") {
    const GameState s = test_position(db(), uw_sim_deck(), rakdos_deck(), "9b").state;
    const GameState w = cheating_world(s, 42);
    GameState same = w;
    for (PlayerId p = 0; p < kNumPlayers; ++p) same.players[p].rng = s.players[p].rng;
    CHECK(same == s);  // tutto vero, compresa la mano di Rakdos e l'ordine dei grimori
    CHECK_FALSE(w.players[0].rng == s.players[0].rng);
    CHECK_FALSE(w.players[1].rng == s.players[1].rng);
    CHECK(cheating_world(s, 42) == w);
    CHECK_FALSE(cheating_world(s, 43) == w);
}

TEST_CASE("Partite forti: le righe non dipendono dai thread; il canarino gioca le sue", "[ricerca][forte]") {
    const StrongBlock one = play_strong_block(strong_run(2, 1), 1, 4);
    const StrongBlock four = play_strong_block(strong_run(2, 4), 1, 4);
    CHECK(one.games == four.games);
    CHECK(one.searches == four.searches);
    CHECK(one.stats.games == 4);
    CHECK(one.stats.searched > 0);
    CHECK(one.stats.rollouts > 0);
    // Una riga per partita con i campi di matchup.exe, una per decisione cercata.
    std::size_t lines = 0;
    for (const char c : one.games) lines += c == '\n' ? 1 : 0;
    CHECK(lines == 4);
    const auto first = nlohmann::json::parse(one.games.substr(0, one.games.find('\n')));
    CHECK(first["seed"] == 1);
    CHECK(first.contains("vincitore"));
    CHECK(first.contains("cercate"));
    CHECK(first.contains("scostamenti"));
    std::size_t searches = 0;
    for (const char c : one.searches) searches += c == '\n' ? 1 : 0;
    CHECK(searches == one.stats.searched);
    // Il canarino: stesse partite con 1 e 2 thread.
    const StrongBlock cheat1 = play_strong_block(strong_run(2, 1, true), 1, 2);
    const StrongBlock cheat2 = play_strong_block(strong_run(2, 2, true), 1, 2);
    CHECK(cheat1.games == cheat2.games);
}

TEST_CASE("Partite forti: un'esecuzione interrotta e ripresa ha lo stesso MD5", "[ricerca][forte][esecuzioni]") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto a = dir / "mtg_test_forte_intera";
    const auto b = dir / "mtg_test_forte_interrotta";
    std::filesystem::remove_all(a);
    std::filesystem::remove_all(b);
    RunSpec spec;
    spec.config = R"({"prova":"forte"})";
    spec.first = 1;
    spec.last = 3;
    spec.block = 2;
    spec.files = {"partite", "ricerca"};
    const StrongRun run = strong_run(2, 2);
    const BlockWork work = [&run](std::uint64_t first, std::uint64_t last) {
        StrongBlock blk = play_strong_block(run, first, last);
        return std::vector<std::string>{blk.games, blk.searches};
    };
    const RunResult full = run_resumable(a, spec, work);
    RunOptions stop;
    stop.max_blocks = 1;
    CHECK_FALSE(run_resumable(b, spec, work, stop).complete);
    const RunResult resumed = run_resumable(b, spec, work);
    CHECK(resumed.complete);
    CHECK(resumed.md5 == full.md5);
    std::filesystem::remove_all(a);
    std::filesystem::remove_all(b);
}

TEST_CASE("Confronto appaiato: differenza e intervallo al 95%", "[ricerca]") {
    const std::vector<int> a = {1, 1, 0, 1, 0, 1, 1, 0, 1, 1};
    const std::vector<int> b = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0};
    const PairedDifference d = paired_difference(a, b);
    CHECK(d.n == 10);
    CHECK(d.a == 0.7);
    CHECK(d.b == 0.4);
    CHECK(std::abs(d.diff - 0.3) < 1e-12);
    // Differenze: 3 volte +1, 7 volte 0: deviazione standard sqrt((3 - 10*0.09)/9) = 0.4830; errore 0.1528.
    CHECK(std::abs(d.low - (0.3 - 1.959964 * 0.152753)) < 1e-4);
    CHECK(std::abs(d.high - (0.3 + 1.959964 * 0.152753)) < 1e-4);
    CHECK_THROWS_AS(paired_difference({1, 0}, {1}), EngineError);
}

TEST_CASE("Curva: decisioni vere degli euristici, una per partita, e la riga di una decisione", "[ricerca][curva]") {
    const std::vector<SampledDecision> d = sample_decisions(matchup_config(), 4, 1000001);
    REQUIRE(d.size() == 4);
    for (const SampledDecision& x : d) {
        CHECK(x.kind != SearchedKind::None);
        CHECK(x.state.pending.player == kUw);
        CHECK(classify_decision(x.state, db(), legal_actions(x.state, db()), kUw) == x.kind);
    }
    CHECK(d[0].game_seed < d[1].game_seed);
    StrongConfig cfg;
    const std::string line = curve_decision_json(db(), d[0], 1, {2, 4}, 8, cfg);
    CHECK(line == curve_decision_json(db(), d[0], 1, {2, 4}, 8, cfg));  // deterministica
    const auto j = nlohmann::json::parse(line);
    CHECK(j["indice"] == 1);
    CHECK(j["tipo"] == std::string(searched_kind_name(d[0].kind)));
    CHECK(j["scelte"].size() == 2);
    CHECK(j["riferimento"]["mondi"] == 8);
    CHECK(j["riferimento"]["vittorie"].size() == j["mosse"].size());
    const std::string summary = curve_summary(line + "\n" + curve_decision_json(db(), d[1], 2, {2, 4}, 8, cfg) + "\n");
    CHECK(summary.find("R = 2") != std::string::npos);
    CHECK(summary.find("R = 4") != std::string::npos);
}

TEST_CASE("Posizioni: la linea indicata riconosce cosa gioca l'euristico", "[ricerca][posizioni]") {
    // Cosa fa l'euristico nel turno (tests/test_posizioni.cpp): la linea in 1, 2, 2c, 3, 4, 4c, 5, 6, 6c, 7a, 7c, 8b,
    // 9a, 9b, 10b; non in 1c (evoca Solitude), 3c, 5c (esilia Nethergoyf, non la creatura con la Phoenix), 8a (aspetta
    // il trigger), 8c, 10a e 10c (cerca Sanctifier).
    const std::vector<std::string> yes = {"1", "2", "2c", "3", "4", "4c", "5", "6", "6c", "7a", "7c", "8b", "9a", "9b", "10b"};
    const std::vector<std::string> no = {"1c", "3c", "5c", "8a", "8c", "10a", "10c"};
    for (const std::string& id : test_position_ids()) {
        INFO("posizione " << id);
        const TestPosition p = test_position(db(), uw_sim_deck(), rakdos_deck(), id);
        const PositionOutcome o = play_position(db(), p, nullptr, 11, false);
        INFO("giocate: " << o.signature);
        const bool expected = std::find(yes.begin(), yes.end(), id) != yes.end();
        REQUIRE((expected || std::find(no.begin(), no.end(), id) != no.end()));
        CHECK(o.line == expected);
        CHECK_FALSE(indicated_line_text(id).empty());
    }
}

TEST_CASE("Posizioni: il giocatore forte gioca il turno e ne racconta le ricerche", "[ricerca][posizioni]") {
    const TestPosition p = test_position(db(), uw_sim_deck(), rakdos_deck(), "8b");
    StrongConfig cfg;
    cfg.worlds = 4;
    const PositionOutcome o = play_position(db(), p, &cfg, 3, true);
    REQUIRE_FALSE(o.records.empty());
    CHECK(o.replay.find("ricerca (risposta, 4 mondi)") != std::string::npos);
    CHECK(o.replay.find(p.situation) != std::string::npos);
    CHECK(o.end.turn.turn_number != p.state.turn.turn_number);  // il turno e' finito
    const PositionOutcome again = play_position(db(), p, &cfg, 3, true);
    CHECK(again.replay == o.replay);
}
