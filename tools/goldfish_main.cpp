// Goldfish di Rakdos Delirium (docs/agente_rakdos.md, sezione 16).
//
//   goldfish [--games N] [--first-seed S] [--limit T] [--bolt-face] [--replays K] [--out DIR]
//
// Scrive una riga JSON per partita in DIR/partite<variante>.jsonl, il registro completo
// delle prime K partite in DIR/replay<variante>.jsonl, e stampa il riepilogo (salvato
// anche in DIR/riepilogo<variante>.txt). Play e draw sono sempre separati.

#include "all_cards.hpp"
#include "goldfish.hpp"
#include "replay.hpp"

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
    int games = 0, won = 0, limit = 0, lost = 0;
    std::map<int, int> delirium;  // turno -> partite (0 = mai)
    std::map<int, int> win;       // turno -> partite vinte
    std::map<int, int> hand_size, hand_size_wins;
    std::map<int, long long> hand_size_win_turns;
    std::map<std::string, long long> used, seen, damage, life_paid;
    long long damage_total = 0, life_paid_won = 0;
    std::map<int, long long> types_sum;
    std::map<int, int> types_n;
    long long no_land = 0, extra_land = 0, extra_combats = 0, bestows = 0, turns = 0;
    std::uint64_t decisions = 0, defaults = 0;
};

void add(Side& side, const GoldfishGame& g) {
    ++side.games;
    side.turns += g.rakdos_turns;
    switch (g.result) {
        case GoldfishResult::Won: ++side.won; ++side.win[g.win_turn]; break;
        case GoldfishResult::TurnLimit: ++side.limit; break;
        case GoldfishResult::Lost: ++side.lost; break;
    }
    ++side.delirium[g.delirium_turn];
    const int size = 7 - g.mulligans;
    ++side.hand_size[size];
    if (g.result == GoldfishResult::Won) {
        ++side.hand_size_wins[size];
        side.hand_size_win_turns[size] += g.win_turn;
        for (const auto& [card, n] : g.life_paid) side.life_paid_won += n;
    }
    for (const auto& [card, n] : g.used) side.used[card] += n;
    for (const auto& [card, n] : g.seen) side.seen[card] += n;
    for (const auto& [card, n] : g.damage) {
        side.damage[card] += n;
        side.damage_total += n;
    }
    for (const auto& [card, n] : g.life_paid) side.life_paid[card] += n;
    for (std::size_t t = 0; t < g.types_end_of_turn.size(); ++t) {
        side.types_sum[static_cast<int>(t) + 1] += g.types_end_of_turn[t];
        ++side.types_n[static_cast<int>(t) + 1];
    }
    side.no_land += g.turns_without_land;
    side.extra_land += g.turns_with_extra_lands;
    side.extra_combats += g.extra_combats;
    side.bestows += g.bestows;
    side.decisions += g.decisions;
    side.defaults += g.default_decisions;
}

double pct(long long part, long long whole) { return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole); }

// Mediana su tutte le partite: chi non arriva mai (turno 0) conta come oltre il limite.
std::string median(const std::map<int, int>& by_turn, int games) {
    int seen = 0;
    for (const auto& [turn, n] : by_turn) {
        if (turn == 0) continue;
        seen += n;
        if (2 * seen >= games) return std::to_string(turn);
    }
    return "oltre il limite";
}

void print_side(std::ostream& o, const char* title, const Side& s) {
    o << std::fixed << std::setprecision(1);
    o << "\n=== " << title << ": " << s.games << " partite ===\n";
    o << "Esito: vinte " << s.won << " (" << pct(s.won, s.games) << "%), non vinte entro il limite " << s.limit << " ("
      << pct(s.limit, s.games) << "%), perse " << s.lost << "\n";

    o << "\nTurno del delirium: mediana " << median(s.delirium, s.games) << "; mai " << (s.delirium.contains(0) ? s.delirium.at(0) : 0)
      << " (" << pct(s.delirium.contains(0) ? s.delirium.at(0) : 0, s.games) << "%)\n";
    o << "  turno   partite      %   cumulata\n";
    long long cumulative = 0;
    for (const auto& [turn, n] : s.delirium) {
        if (turn == 0) continue;
        cumulative += n;
        o << "  " << std::setw(5) << turn << std::setw(10) << n << std::setw(7) << pct(n, s.games) << std::setw(10)
          << pct(cumulative, s.games) << "\n";
    }

    long long win_sum = 0;
    for (const auto& [turn, n] : s.win) win_sum += static_cast<long long>(turn) * n;
    o << "\nTurno della vittoria: media " << (s.won == 0 ? 0.0 : static_cast<double>(win_sum) / s.won) << " (sulle vinte), mediana "
      << median(s.win, s.games) << "\n";
    o << "  turno   partite      %   cumulata\n";
    cumulative = 0;
    for (const auto& [turn, n] : s.win) {
        cumulative += n;
        o << "  " << std::setw(5) << turn << std::setw(10) << n << std::setw(7) << pct(n, s.games) << std::setw(10)
          << pct(cumulative, s.games) << "\n";
    }

    o << "\nMulligan (carte tenute: partite, %, turno medio di vittoria)\n";
    for (auto it = s.hand_size.rbegin(); it != s.hand_size.rend(); ++it) {
        const int size = it->first;
        const int wins = s.hand_size_wins.contains(size) ? s.hand_size_wins.at(size) : 0;
        const double mean = wins == 0 ? 0.0 : static_cast<double>(s.hand_size_win_turns.at(size)) / wins;
        o << "  " << size << " carte: " << it->second << " (" << pct(it->second, s.games) << "%), vittoria media al turno "
          << std::setprecision(2) << mean << std::setprecision(1) << "\n";
    }

    o << "\nCarte (per partita: viste; lanci o attivazioni per modo)\n";
    std::map<std::string, std::pair<double, double>> rows;
    for (const auto& [card, n] : s.seen) rows[card].first = static_cast<double>(n) / s.games;
    for (const auto& [card, n] : s.used) rows[card].second = static_cast<double>(n) / s.games;
    o << std::setprecision(2);
    for (const auto& [card, v] : rows) {
        o << "  " << std::left << std::setw(48) << card << std::right << std::setw(7) << v.first << std::setw(7) << v.second << "\n";
    }

    o << std::setprecision(1) << "\nDanno all'avversario per carta (% del totale)\n";
    std::vector<std::pair<long long, std::string>> dmg;
    for (const auto& [card, n] : s.damage) dmg.emplace_back(n, card);
    std::sort(dmg.rbegin(), dmg.rend());
    for (const auto& [n, card] : dmg) o << "  " << std::left << std::setw(30) << card << std::right << std::setw(7) << pct(n, s.damage_total) << "%\n";

    o << "\nVita pagata da Rakdos, media nelle partite vinte: " << std::setprecision(2)
      << (s.won == 0 ? 0.0 : static_cast<double>(s.life_paid_won) / s.won) << "; per carta (media su tutte):";
    for (const auto& [card, n] : s.life_paid) o << " " << card << " " << static_cast<double>(n) / s.games << ";";
    o << "\n";

    o << "Tipi nel cimitero a fine turno (media):";
    for (const auto& [turn, sum] : s.types_sum) {
        if (turn > 8) break;
        o << " T" << turn << " " << static_cast<double>(sum) / s.types_n.at(turn);
    }
    o << "\n";
    o << "Terre: turni senza terra giocata " << static_cast<double>(s.no_land) / s.games << " per partita, turni con terre in piu' "
      << static_cast<double>(s.extra_land) / s.games << " per partita (su " << static_cast<double>(s.turns) / s.games
      << " turni)\n";
    o << "FOMO: combattimenti aggiuntivi " << static_cast<double>(s.extra_combats) / s.games << " per partita; Phoenix: conferimenti "
      << static_cast<double>(s.bestows) / s.games << " per partita\n";
    o << "Decisioni dell'agente: " << s.decisions << ", predefinite " << s.defaults << " (" << std::setprecision(3)
      << pct(static_cast<long long>(s.defaults), static_cast<long long>(s.decisions)) << "%)\n";
}

}  // namespace

int main(int argc, char** argv) {
    long long games = 100000;
    std::uint64_t first_seed = 1;
    int limit = 15;
    int replays = 100;
    bool bolt_face = false;
    std::filesystem::path out_dir = std::filesystem::path(DATA_DIR) / "goldfish";
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
        else if (arg == "--bolt-face") bolt_face = true;
        else if (arg == "--out") out_dir = next();
        else {
            std::cerr << "opzione sconosciuta: " << arg << "\n";
            return 2;
        }
    }

    CardDatabase db = CardDatabase::load(CARDS_JSON);
    register_all_cards(db);
    GoldfishConfig config;
    config.db = &db;
    config.rakdos = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    config.goldfish = load_decklist(std::string(DECKS_DIR) + "/goldfish_plains.json");
    config.turn_limit = limit;
    config.agent.bolt_face_asap = bolt_face;

    std::filesystem::create_directories(out_dir);
    const std::string variant = bolt_face ? "_bolt_faccia" : "";
    std::ofstream games_out(out_dir / ("partite" + variant + ".jsonl"));
    std::ofstream replay_out(out_dir / ("replay" + variant + ".jsonl"));

    Side play, draw;
    std::map<std::string, std::uint64_t> rule_counts;
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        GoldfishGame g;
        if (n < replays) {
            GameLog log;
            std::map<std::size_t, std::string> rules;
            g = play_goldfish(config, seed, &log, &rules, &rule_counts);
            ReplayInfo info;
            info.seed = seed;
            info.players = {"Rakdos", "Goldfish"};
            info.rules = std::move(rules);
            info.winner = g.result == GoldfishResult::Won ? kGoldfishRakdos : kNoPlayer;
            info.result = g.result == GoldfishResult::Won ? "vinta da Rakdos al suo turno " + std::to_string(g.win_turn)
                          : g.result == GoldfishResult::Lost ? "persa da Rakdos"
                                                              : "non vinta entro il limite";
            write_replay(replay_out, db, log, info);
        } else {
            g = play_goldfish(config, seed, nullptr, nullptr, &rule_counts);
        }
        games_out << goldfish_game_json(g) << '\n';
        add(g.on_play ? play : draw, g);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::ostringstream report;
    report << "Goldfish di Rakdos Delirium" << (bolt_face ? " - variante: Bolt in faccia appena possibile" : "") << "\n";
    report << games << " partite, seed da " << first_seed << " a " << first_seed + static_cast<std::uint64_t>(games) - 1
           << ", limite " << limit << " turni di Rakdos\n";
    report << std::fixed << std::setprecision(1) << "Tempo: " << seconds << " s (" << static_cast<double>(games) / seconds
           << " partite/s)\n";
    print_side(report, "In play (Rakdos gioca per primo)", play);
    print_side(report, "In draw (Rakdos pesca per primo)", draw);
    std::uint64_t total = 0;
    for (const auto& [rule, n] : rule_counts) total += n;
    std::vector<std::pair<std::uint64_t, std::string>> sorted;
    for (const auto& [rule, n] : rule_counts) sorted.emplace_back(n, rule);
    std::sort(sorted.rbegin(), sorted.rend());
    report << "\n=== Regole dell'agente (tutte le partite) ===\n";
    for (const auto& [n, rule] : sorted) {
        report << "  " << std::left << std::setw(58) << rule << std::right << std::setw(10) << n << std::setw(8)
               << std::setprecision(2) << pct(static_cast<long long>(n), static_cast<long long>(total)) << "%\n";
    }
    std::cout << report.str();
    std::ofstream(out_dir / ("riepilogo" + variant + ".txt")) << report.str();
    return 0;
}
