// Confronto di varianti delle regole di UW (docs/agente_uw.md, sezione 28).
//
//   varianti_uw [--games N] [--first-seed S] [--limit T] [--out DIR]
//
// Gioca il matchup con la base (opzioni spente), con ogni variante da sola (R1, T1, M1) e con tutte
// insieme, sugli stessi seed. Per ognuna: win rate di UW in play, in draw e in totale, con la
// differenza appaiata rispetto alla base e il suo intervallo al 95%; sconfitte di UW con 3 o piu'
// carte in mano; ricariche di Quantum Riddler e carte pescate; mani da 7, 6 e 5 carte di UW.
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

    std::vector<Variant> variants;
    variants.push_back({"base", "opzioni spente", {}});
    {
        UwAgentOptions o;
        o.riddler_refill_ignores_counters = true;
        variants.push_back({"R1", "warp di Riddler: Force of Negation e Consign non contano nella mano", o});
    }
    {
        UwAgentOptions o;
        o.teferi_any_target = true;
        variants.push_back({"T1", "Teferi -3 su qualsiasi bersaglio utile", o});
    }
    {
        UwAgentOptions o;
        o.mulligan_more_answers = true;
        variants.push_back({"M1", "mulligan: Recruiter e Witch Enchanter contano come risposta", o});
    }
    {
        UwAgentOptions o;
        o.riddler_refill_ignores_counters = true;
        o.teferi_any_target = true;
        o.mulligan_more_answers = true;
        variants.push_back({"tutte", "R1 + T1 + M1", o});
    }

    std::vector<Results> results;
    for (const Variant& v : variants) {
        std::cerr << "variante " << v.name << "...\n";
        results.push_back(run(config, v.options, games, first_seed));
    }

    std::ostringstream o;
    o << "Varianti delle regole di UW - " << games << " partite per variante, seed da " << first_seed << " a "
      << first_seed + static_cast<std::uint64_t>(games) - 1 << " (gli stessi per tutte), limite " << limit << " turni\n";
    o << "Differenza appaiata rispetto alla base, in punti percentuali, con l'intervallo al 95%.\n";
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
            if (k > 0) {
                const PairedDiff d = paired_difference(side_of(results[0], results[0].win, side), w);
                o << "   differenza " << points(d.mean) << " punti [" << points(d.low) << ", " << points(d.high) << "]";
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
    }
    std::cout << o.str();
    std::filesystem::create_directories(out_dir);
    std::ofstream(out_dir / "varianti_uw.txt") << o.str();
    return 0;
}
