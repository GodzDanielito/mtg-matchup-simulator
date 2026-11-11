// Confronto di varianti delle regole di UW (docs/agente_uw.md, sezione 28).
//
//   varianti_uw [--games N] [--first-seed S] [--limit T] [--out DIR]
//
// Gioca il matchup con base_v1 (tutte le opzioni spente), base_v2 (T1, W1, R1 accese: il predefinito),
// base_v2 + M1, le tre varianti da sole e le coppie, sugli stessi seed. Per ognuna: win rate di UW in play, in draw e in totale, con la
// differenza appaiata rispetto alla base e il suo intervallo al 95%; sconfitte di UW con 3 o piu'
// carte in mano; ricariche di Quantum Riddler e carte pescate; mani da 7, 6 e 5 carte di UW. Per W1: in
// quante partite parte, quante volte Consign neutralizza l'esilio, e la win rate in quelle partite
// rispetto alla base sugli stessi seed.
// Stampa il confronto e lo salva in DIR/varianti_uw.txt (DIR e' data/matchup).

#include "all_cards.hpp"
#include "matchup.hpp"
#include "varianti.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace mtg;

namespace {

// Le carte in mano di UW a fine partita.
class EndHand final : public MatchupObserver {
public:
    void on_decision(const GameState&, const std::vector<Action>&, const Action&) override {}
    void on_end(const GameState& s) override { hand = static_cast<int>(s.players[kMatchupUw].hand.size()); }
    int hand = 0;
};

struct Variant {
    std::string name;
    std::string description;
    UwAgentOptions options;
};

struct Results {
    std::vector<int> win;      // 1 se vince UW, per seed
    std::vector<int> on_play;  // 1 se UW gioca per primo
    long long losses = 0, losses_3plus = 0;
    std::array<long long, 2> losses_side{}, losses_3plus_side{};
    std::array<long long, 8> hand_size{};  // mani tenute da UW: 7 - mulligan
    long long refill_games = 0;
    RefillStats refill;
    std::vector<int> w1_fired;  // 1 se W1 parte, per seed
    long long both_games = 0;   // partite con una ricarica di R1 e una partenza di W1
    W1Stats w1;
    double seconds = 0.0;
};

Results run(const MatchupConfig& base_config, const UwAgentOptions& options, long long games, std::uint64_t first_seed) {
    MatchupConfig config = base_config;
    config.uw_agent = options;
    Results r;
    r.win.reserve(static_cast<std::size_t>(games));
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        GameLog log;
        std::map<std::size_t, std::string> rules;
        EndHand end;
        const MatchupGame g = play_matchup(config, seed, &log, &rules, nullptr, nullptr, &end);
        r.win.push_back(g.winner == kMatchupUw ? 1 : 0);
        r.on_play.push_back(g.uw_on_play ? 1 : 0);
        const std::size_t side = g.uw_on_play ? 0 : 1;
        if (g.winner == kMatchupRakdos) {
            ++r.losses;
            ++r.losses_side[side];
            if (end.hand >= 3) {
                ++r.losses_3plus;
                ++r.losses_3plus_side[side];
            }
        }
        ++r.hand_size[static_cast<std::size_t>(std::max(0, 7 - g.uw_mulligans))];
        const RefillStats refill = count_refill_warps(log, rules);
        if (refill.casts > 0) ++r.refill_games;
        r.refill.casts += refill.casts;
        r.refill.cards += refill.cards;
        const W1Stats w1 = count_w1(log, rules);
        r.w1_fired.push_back(w1.casts > 0 ? 1 : 0);
        if (w1.casts > 0 && refill.casts > 0) ++r.both_games;
        r.w1 += w1;
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
}

// Gli esiti di un lato (0 play, 1 draw, 2 tutte).
std::vector<int> side_of(const Results& r, const std::vector<int>& v, int side) {
    std::vector<int> out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (side == 2 || (side == 0) == (r.on_play[i] == 1)) out.push_back(v[i]);
    }
    return out;
}

std::string pct(double x) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2) << 100.0 * x << "%";
    return o.str();
}
std::string points(double x) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2) << std::showpos << 100.0 * x;
    return o.str();
}
double ratio(long long a, long long b) { return b == 0 ? 0.0 : static_cast<double>(a) / static_cast<double>(b); }

}  // namespace

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

    // base_v1: tutte le opzioni spente (l'agente prima della decisione dell'11/11/2026).
    // base_v2: T1, W1 e R1 accese (il predefinito di oggi), M1 spenta.
    auto options = [](bool r1, bool t1, bool w1, bool m1) {
        UwAgentOptions o;
        o.riddler_refill_ignores_counters = r1;
        o.teferi_any_target = t1;
        o.riddler_warp_with_consign = w1;
        o.mulligan_more_answers = m1;
        return o;
    };
    std::vector<Variant> variants;
    variants.push_back({"base_v1", "tutte le opzioni spente", options(false, false, false, false)});
    variants.push_back({"R1", "da sola", options(true, false, false, false)});
    variants.push_back({"T1", "da sola", options(false, true, false, false)});
    variants.push_back({"W1", "da sola", options(false, false, true, false)});
    variants.push_back({"W1+R1", "senza T1", options(true, false, true, false)});
    variants.push_back({"W1+T1", "senza R1", options(false, true, true, false)});
    variants.push_back({"R1+T1", "senza W1", options(true, true, false, false)});
    variants.push_back({"base_v2", "R1 + T1 + W1 (il predefinito)", options(true, true, true, false)});
    variants.push_back({"base_v2+M1", "R1 + T1 + W1 + M1", options(true, true, true, true)});
    const std::size_t kV1 = 0, kR1 = 1, kT1 = 2, kW1 = 3, kV2 = 7;

    std::vector<Results> results;
    for (const Variant& v : variants) {
        std::cerr << "variante " << v.name << "...\n";
        results.push_back(run(config, v.options, games, first_seed));
    }

    std::ostringstream o;
    o << "Varianti delle regole di UW - " << games << " partite per variante, seed da " << first_seed << " a "
      << first_seed + static_cast<std::uint64_t>(games) - 1 << " (gli stessi per tutte), limite " << limit << " turni\n";
    o << "Differenza appaiata in punti percentuali, con l'intervallo al 95%: prima rispetto a base_v1, poi rispetto a base_v2.\n";
    const char* side_names[] = {"in play", "in draw", "totale"};
    for (std::size_t k = 0; k < variants.size(); ++k) {
        const Results& r = results[k];
        o << "\n=== " << variants[k].name << ": " << variants[k].description << " (" << std::fixed << std::setprecision(0)
          << r.seconds << " s) ===\n";
        for (const int side : {0, 1, 2}) {
            const std::vector<int> w = side_of(r, r.win, side);
            long long wins = 0;
            for (const int x : w) wins += x;
            o << "  Win rate UW " << std::left << std::setw(8) << side_names[side] << std::right << " " << pct(ratio(wins, static_cast<long long>(w.size())));
            if (k != kV1) {
                const PairedDiff d = paired_difference(side_of(results[kV1], results[kV1].win, side), w);
                o << "   vs base_v1 " << points(d.mean) << " [" << points(d.low) << ", " << points(d.high) << "]";
            }
            if (k != kV2) {
                const PairedDiff d = paired_difference(side_of(results[kV2], results[kV2].win, side), w);
                o << "   vs base_v2 " << points(d.mean) << " [" << points(d.low) << ", " << points(d.high) << "]";
            }
            o << "\n";
        }
        o << "  Sconfitte di UW con 3+ carte in mano: " << r.losses_3plus << " su " << r.losses << " ("
          << pct(ratio(r.losses_3plus, r.losses)) << "); in play " << pct(ratio(r.losses_3plus_side[0], r.losses_side[0]))
          << ", in draw " << pct(ratio(r.losses_3plus_side[1], r.losses_side[1])) << "\n";
        o << "  Ricarica di Quantum Riddler con il warp: in " << r.refill_games << " partite (" << pct(ratio(r.refill_games, games))
          << "), " << r.refill.casts << " lanci, in media " << std::fixed << std::setprecision(2)
          << ratio(r.refill.cards, r.refill.casts) << " carte pescate per lancio\n";
        o << "  Mani di UW: 7 carte " << pct(ratio(r.hand_size[7], games)) << ", 6 carte " << pct(ratio(r.hand_size[6], games))
          << ", 5 carte " << pct(ratio(r.hand_size[5], games)) << ", 4 o meno "
          << pct(ratio(r.hand_size[4] + r.hand_size[3] + r.hand_size[2] + r.hand_size[1] + r.hand_size[0], games)) << "\n";
        if (variants[k].options.riddler_warp_with_consign) {
            std::vector<int> base_w, var_w;
            for (std::size_t i = 0; i < r.w1_fired.size(); ++i) {
                if (r.w1_fired[i] == 0) continue;
                base_w.push_back(results[kV1].win[i]);
                var_w.push_back(r.win[i]);
            }
            const long long fired = static_cast<long long>(var_w.size());
            long long base_wins = 0, var_wins = 0;
            for (const int x : base_w) base_wins += x;
            for (const int x : var_w) var_wins += x;
            const PairedDiff d = paired_difference(base_w, var_w);
            o << "  Partite con una ricarica di R1 e una partenza di W1: " << r.both_games << " (" << pct(ratio(r.both_games, games)) << ")\n";
            o << "  W1: parte in " << fired << " partite (" << pct(ratio(fired, games)) << "), " << r.w1.casts
              << " lanci; Consign lanciato sull'esilio in " << r.w1.consign_cast << ", lo neutralizza davvero in " << r.w1.saved
              << " (" << pct(ratio(r.w1.saved, r.w1.casts)) << " dei lanci); Riddler in campo al turno dopo in " << r.w1.stayed
              << " (" << pct(ratio(r.w1.stayed, r.w1.casts)) << ", anche con Ephemerate o Phelia)\n";
            o << "  W1: win rate UW nelle partite in cui parte " << pct(ratio(var_wins, fired)) << ", base sugli stessi seed "
              << pct(ratio(base_wins, fired)) << ", differenza " << points(d.mean) << " punti [" << points(d.low) << ", "
              << points(d.high) << "]\n";
        }
    }
    // Quanto R1 e W1 si sovrappongono: la somma delle differenze da sole contro la differenza insieme.
    auto total_diff = [&](std::size_t k) { return paired_difference(results[kV1].win, results[k].win).mean; };
    o << "\n=== Sovrapposizioni (differenza totale rispetto a base_v1, in punti) ===\n";
    struct Pair {
        const char* name;
        std::size_t a, b, both;
    };
    const Pair pairs[] = {{"W1+R1", kW1, kR1, 4}, {"W1+T1", kW1, kT1, 5}, {"R1+T1", kR1, kT1, 6}};
    for (const Pair& pr : pairs) {
        const double sum = total_diff(pr.a) + total_diff(pr.b);
        const double both = total_diff(pr.both);
        o << "  " << pr.name << ": da sole " << points(total_diff(pr.a)) << " + " << points(total_diff(pr.b)) << " = " << points(sum)
          << ", insieme " << points(both) << ", sovrapposizione " << points(sum - both) << "\n";
    }
    const double sum3 = total_diff(kR1) + total_diff(kT1) + total_diff(kW1);
    o << "  R1+T1+W1: da sole, somma " << points(sum3) << ", insieme (base_v2) " << points(total_diff(kV2)) << ", sovrapposizione "
      << points(sum3 - total_diff(kV2)) << "\n";
    std::cout << o.str();
    std::filesystem::create_directories(out_dir);
    std::ofstream(out_dir / "varianti_uw.txt") << o.str();
    return 0;
}
