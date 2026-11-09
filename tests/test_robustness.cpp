// Test di robustezza: partite complete giocate dall'agente casuale.
//
// In questo passo le uniche azioni esistenti riguardano le terre (giocarle,
// attivare le fetch, le scelte collegate) più mulligan, scarti e "passa": quindi
// l'agente casuale "gioca solo terre e passa". Dopo ogni azione si controllano
// tutti gli invarianti; ogni partita deve finire senza eccezioni.

#include "test_helpers.hpp"

#include <chrono>
#include <iostream>
#include <string>

using namespace mtg;
using namespace test;

namespace {

constexpr int kGames = 10000;
constexpr std::size_t kMaxDecisions = 100000;  // rete di sicurezza contro i cicli infiniti

struct Outcome {
    PlayerId winner = kNoPlayer;
    bool decked[kNumPlayers] = {false, false};
    std::int16_t life[kNumPlayers] = {0, 0};
    std::size_t decisions = 0;
    std::string failure;  // vuoto = partita regolare
};

Outcome play_random_game(std::uint64_t seed, GameLog* log, GameState* final_state = nullptr) {
    const CardDatabase& db = db_with_tokens();
    Outcome out;
    GameState s = create_game(db, uw_sim_deck(), rakdos_deck(), seed);
    start_game(s, db, StartOptions{static_cast<PlayerId>(seed % 2), true}, log);
    RandomAgent agent(seed);  // generatore proprio, su uno stream diverso da quello della partita

    while (s.pending.kind != DecisionKind::GameOver) {
        const std::vector<Action> actions = legal_actions(s, db);
        if (actions.empty()) {
            out.failure = "decisione senza azioni legali";
            break;
        }
        if (s.pending.kind == DecisionKind::Priority && actions.size() == 1) {
            out.failure = "decisione di priorita' con il solo 'passa' (doveva passare da sola)";
            break;
        }
        apply(s, db, agent.choose(s, actions), log);
        if (const auto problems = check_invariants(s, db); !problems.empty()) {
            out.failure = "invariante violato: " + problems.front();
            break;
        }
        if (++out.decisions > kMaxDecisions) {
            out.failure = "la partita non finisce";
            break;
        }
    }
    out.winner = s.turn.winner;
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        out.decked[p] = s.players[p].drew_from_empty_library;
        out.life[p] = s.players[p].life;
    }
    if (final_state != nullptr) *final_state = s;
    return out;
}

}  // namespace

TEST_CASE("Robustezza: 10.000 partite casuali con sole terre", "[robustness]") {
    int by_decking = 0, by_life = 0, draws = 0;
    int life_losses[kNumPlayers] = {0, 0};  // chi ha perso per punti vita
    std::size_t total_decisions = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int game = 0; game < kGames; ++game) {
        Outcome out;
        try {
            out = play_random_game(static_cast<std::uint64_t>(game), nullptr);
        } catch (const std::exception& e) {
            FAIL("seed " << game << ": eccezione: " << e.what());
        }
        if (!out.failure.empty()) FAIL("seed " << game << ": " << out.failure);

        total_decisions += out.decisions;
        if (out.winner == kNoPlayer) {
            ++draws;
        } else if (out.decked[1 - out.winner]) {
            ++by_decking;
        } else {
            REQUIRE(out.life[1 - out.winner] <= 0);  // l'unica altra fine possibile
            ++by_life;
            ++life_losses[1 - out.winner];
        }
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\n  " << kGames << " partite in " << seconds << " s (" << kGames / seconds << " partite/s, "
              << total_decisions / kGames << " decisioni per partita in media)\n"
              << "  finite per grimorio vuoto: " << by_decking << ", per punti vita: " << by_life
              << " (perse da UW Blink " << life_losses[0] << ", da Rakdos " << life_losses[1]
              << "), patte: " << draws << "\n";
    CHECK(by_decking + by_life + draws == kGames);
    CHECK(by_decking > 0);
}

TEST_CASE("Robustezza: stesso seed, stessa partita", "[robustness]") {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        GameLog log_a, log_b;
        GameState a, b;
        const Outcome first = play_random_game(seed, &log_a, &a);
        const Outcome second = play_random_game(seed, &log_b, &b);
        INFO("seed " << seed);
        REQUIRE(first.failure.empty());
        CHECK(log_a.entries == log_b.entries);  // ogni decisione e ogni passaggio automatico
        CHECK(first.winner == second.winner);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            CHECK(a.players[p].life == b.players[p].life);
            CHECK(std::vector<CardId>(a.players[p].graveyard.begin(), a.players[p].graveyard.end()) ==
                  std::vector<CardId>(b.players[p].graveyard.begin(), b.players[p].graveyard.end()));
            CHECK(std::vector<CardId>(a.players[p].library.begin(), a.players[p].library.end()) ==
                  std::vector<CardId>(b.players[p].library.begin(), b.players[p].library.end()));
        }
        CHECK(std::vector<CardId>(a.battlefield.begin(), a.battlefield.end()) ==
              std::vector<CardId>(b.battlefield.begin(), b.battlefield.end()));
    }
}

TEST_CASE("Robustezza: seed diversi, partite diverse", "[robustness]") {
    GameLog log_a, log_b;
    (void)play_random_game(1, &log_a);
    (void)play_random_game(3, &log_b);  // stesso giocatore iniziale, seed diverso
    CHECK(log_a.entries != log_b.entries);
}
