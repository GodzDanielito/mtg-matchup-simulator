// Diagnostica delle carte morte (tools/carte_morte.hpp).
//
//   carte_morte [--games N] [--first-seed S] [--limit T] [--out DIR]
//
// Gioca il matchup (agente di UW contro agente di Rakdos, gara 1, seed da S a S+N-1) con un osservatore
// passivo e scrive DIR/carte_morte.csv (DIR e' data/matchup), piu' il riassunto stampato e salvato in
// DIR/riepilogo_carte_morte.txt. Le partite sono quelle del matchup: stessi seed, stesse scelte.

#include "all_cards.hpp"
#include "carte_morte.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace mtg;

int main(int argc, char** argv) {
    long long games = 100000;
    std::uint64_t first_seed = 1;
    int limit = 60;
    std::filesystem::path out_dir = std::filesystem::path(DATA_DIR) / "matchup";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "manca il valore di " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--games") games = std::stoll(next());
        else if (arg == "--first-seed") first_seed = std::stoull(next());
        else if (arg == "--limit") limit = std::stoi(next());
        else if (arg == "--out") out_dir = next();
        else {
            std::cerr << "opzione sconosciuta: " << arg << "\n";
            return 2;
        }
    }

    CardDatabase db = CardDatabase::load(CARDS_JSON);
    register_all_cards(db);
    MatchupConfig config;
    config.db = &db;
    config.uw = load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");
    config.rakdos = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    config.turn_limit = limit;

    DeadCardTracker tracker(db);
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        (void)play_matchup(config, first_seed + static_cast<std::uint64_t>(n), nullptr, nullptr, nullptr, nullptr, &tracker);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    const std::array<std::string, 2> names = {"UW Blink", "Rakdos Delirium"};
    std::filesystem::create_directories(out_dir);
    std::ofstream(out_dir / "carte_morte.csv") << dead_cards_csv(tracker.diagnostics(), names);

    std::ostringstream report;
    report << games << " partite del matchup, seed da " << first_seed << " a " << first_seed + static_cast<std::uint64_t>(games) - 1
           << ", limite " << limit << " turni. Tempo: " << std::fixed << std::setprecision(1) << seconds << " s\n";
    report << dead_cards_summary(tracker.diagnostics(), names);
    std::cout << report.str();
    std::ofstream(out_dir / "riepilogo_carte_morte.txt") << report.str();
    return 0;
}
