// Matchup UW Blink contro Rakdos Delirium, gara 1: agente euristico di UW contro agente
// euristico di Rakdos (docs/agente_uw.md, docs/agente_rakdos.md).
//
//   matchup [--games N] [--first-seed S] [--limit T] [--replays K] [--out DIR] [--readable-dir DIR]
//
// Scrive una riga JSON per partita in DIR/partite.jsonl, il registro completo delle prime K
// partite in DIR/replay.jsonl, e stampa il riepilogo (salvato anche in DIR/riepilogo.txt).
// Play e draw sono sempre separati (dal punto di vista di UW).
//
// Con --readable-dir sceglie 10 partite (pick_replay_seeds: 5 con UW in play e 5 in draw, meta' per
// vincitore), le rigioca con il registro completo in DIR/replay_scelti.jsonl e ne scrive il racconto
// in italiano, un file di testo per partita, in DIR (tools/replay_leggibile.hpp).

#include "all_cards.hpp"
#include "matchup.hpp"
#include "replay.hpp"
#include "replay_leggibile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mtg;

namespace {

struct Side {
    long long games = 0, uw = 0, rakdos = 0, none = 0, turns = 0;
    std::map<std::string, long long> endings;
    std::map<int, long long> uw_hand, rakdos_hand;
    std::uint64_t uw_decisions = 0, uw_defaults = 0, rakdos_decisions = 0, rakdos_defaults = 0;
};

void add(Side& side, const MatchupGame& g) {
    ++side.games;
    side.turns += g.turns;
    if (g.winner == kMatchupUw) ++side.uw;
    if (g.winner == kMatchupRakdos) ++side.rakdos;
    if (g.winner == kNoPlayer) ++side.none;
    const std::string who = g.winner == kMatchupUw ? "UW vince: " : g.winner == kMatchupRakdos ? "Rakdos vince: " : "";
    ++side.endings[who + g.ending];
    ++side.uw_hand[7 - g.uw_mulligans];
    ++side.rakdos_hand[7 - g.rakdos_mulligans];
    side.uw_decisions += g.uw_decisions;
    side.uw_defaults += g.uw_defaults;
    side.rakdos_decisions += g.rakdos_decisions;
    side.rakdos_defaults += g.rakdos_defaults;
}

double pct(long long part, long long whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

void print_side(std::ostream& o, const std::string& title, const Side& s) {
    o << "\n=== " << title << " ===\n" << std::fixed << std::setprecision(1);
    o << "Partite: " << s.games << "\n";
    o << "UW vince " << s.uw << " (" << pct(s.uw, s.games) << "%), Rakdos vince " << s.rakdos << " ("
      << pct(s.rakdos, s.games) << "%), nessuno " << s.none << " (" << pct(s.none, s.games) << "%)\n";
    o << "Turni medi della partita (di entrambi): " << (s.games == 0 ? 0.0 : static_cast<double>(s.turns) / static_cast<double>(s.games))
      << "\n";
    o << "Come finiscono:\n";
    for (const auto& [ending, n] : s.endings) {
        o << "  " << std::left << std::setw(36) << ending << std::right << std::setw(8) << n << std::setw(8) << pct(n, s.games)
          << "%\n";
    }
    o << "Mano tenuta, UW:";
    for (const auto& [size, n] : s.uw_hand) o << " " << size << " carte " << pct(n, s.games) << "%;";
    o << "\nMano tenuta, Rakdos:";
    for (const auto& [size, n] : s.rakdos_hand) o << " " << size << " carte " << pct(n, s.games) << "%;";
    o << "\nDecisioni predefinite: UW " << s.uw_defaults << " su " << s.uw_decisions << ", Rakdos " << s.rakdos_defaults
      << " su " << s.rakdos_decisions << "\n";
}

void print_rules(std::ostream& o, const std::string& title, const std::map<std::string, std::uint64_t>& counts) {
    std::uint64_t total = 0;
    for (const auto& [rule, n] : counts) total += n;
    std::vector<std::pair<std::uint64_t, std::string>> sorted;
    for (const auto& [rule, n] : counts) sorted.emplace_back(n, rule);
    std::sort(sorted.rbegin(), sorted.rend());
    o << "\n=== " << title << " ===\n";
    for (const auto& [n, rule] : sorted) {
        o << "  " << std::left << std::setw(70) << rule << std::right << std::setw(10) << n << std::setw(8)
          << std::setprecision(2) << pct(static_cast<long long>(n), static_cast<long long>(total)) << "%\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    long long games = 10000;
    std::uint64_t first_seed = 1;
    int limit = 60;
    int replays = 10;
    std::filesystem::path readable_dir;
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
        else if (arg == "--replays") replays = std::stoi(next());
        else if (arg == "--out") out_dir = next();
        else if (arg == "--readable-dir") readable_dir = next();
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

    std::filesystem::create_directories(out_dir);
    std::ofstream games_out(out_dir / "partite.jsonl");
    std::ofstream replay_out(out_dir / "replay.jsonl");

    Side all, play, draw;
    std::vector<MatchupGame> played;
    std::map<std::string, std::uint64_t> uw_rules, rakdos_rules;
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        MatchupGame g;
        if (n < replays) {
            GameLog log;
            std::map<std::size_t, std::string> rules;
            g = play_matchup(config, seed, &log, &rules, &uw_rules, &rakdos_rules);
            ReplayInfo info;
            info.seed = seed;
            info.players = {"UW Blink", "Rakdos Delirium"};
            info.rules = std::move(rules);
            info.winner = g.winner;
            info.result = g.winner == kMatchupUw       ? "vinta da UW (" + g.ending + ") al turno " + std::to_string(g.turns)
                          : g.winner == kMatchupRakdos ? "vinta da Rakdos (" + g.ending + ") al turno " + std::to_string(g.turns)
                                                       : g.ending;
            write_replay(replay_out, db, log, info);
        } else {
            g = play_matchup(config, seed, nullptr, nullptr, &uw_rules, &rakdos_rules);
        }
        games_out << matchup_game_json(g) << '\n';
        add(all, g);
        if (!readable_dir.empty()) played.push_back(g);
        add(g.uw_on_play ? play : draw, g);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::ostringstream report;
    report << "Matchup UW Blink contro Rakdos Delirium, gara 1 - agenti euristici\n";
    report << games << " partite, seed da " << first_seed << " a " << first_seed + static_cast<std::uint64_t>(games) - 1
           << ", limite " << limit << " turni della partita\n";
    report << std::fixed << std::setprecision(1) << "Tempo: " << seconds << " s ("
           << static_cast<double>(games) / seconds << " partite/s)\n";
    print_side(report, "Tutte le partite", all);
    print_side(report, "UW in play (UW gioca per primo)", play);
    print_side(report, "UW in draw (Rakdos gioca per primo)", draw);
    print_rules(report, "Regole dell'agente di UW", uw_rules);
    print_rules(report, "Regole dell'agente di Rakdos", rakdos_rules);
    std::cout << report.str();
    std::ofstream(out_dir / "riepilogo.txt") << report.str();

    if (!readable_dir.empty()) {
        // Le partite scelte si rigiocano con il registro completo (stesso seed, stessa partita).
        std::filesystem::create_directories(readable_dir);
        std::ostringstream jsonl;
        for (const std::uint64_t seed : pick_replay_seeds(played)) {
            GameLog log;
            std::map<std::size_t, std::string> rules;
            const MatchupGame g = play_matchup(config, seed, &log, &rules);
            ReplayInfo info;
            info.seed = seed;
            info.players = {"UW Blink", "Rakdos Delirium"};
            info.rules = std::move(rules);
            info.winner = g.winner;
            info.result = g.winner == kNoPlayer ? g.ending
                                                   : std::string("vinta da ") + (g.winner == kMatchupUw ? "UW" : "Rakdos") +
                                                         " (" + g.ending + ") al turno " + std::to_string(g.turns);
            write_replay(jsonl, db, log, info);
        }
        std::ofstream(readable_dir / "replay_scelti.jsonl") << jsonl.str();
        std::istringstream in(jsonl.str());
        for (const std::filesystem::path& file : write_readable_replays(in, readable_dir)) std::cout << file.string() << '\n';
    }
    return 0;
}
