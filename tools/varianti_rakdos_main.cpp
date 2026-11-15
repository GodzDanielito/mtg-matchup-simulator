// Confronto di varianti delle regole di Rakdos nel matchup (docs/agente_rakdos.md, sezione 21).
//
//   varianti_rakdos [--games N] [--first-seed S] [--limit T] [--out DIR]
//
// Gioca il matchup con l'agente di UW predefinito (base_v2) contro la base di Rakdos e ogni variante,
// sugli stessi seed: X1 (mulligan con una terra A e C), X2 (mano da 7 senza creatura economica con due
// carte di interazione), X3 (Teferi: togli sempre, ignora, soglia 1 e 3), X4 (Thoughtseize prima della
// creatura al turno 1). Per ognuna: win rate di Rakdos in play, in draw e in totale, con la differenza
// appaiata rispetto alla base e il suo intervallo al 95%; le mani di Rakdos da 7, 6 e 5 carte. Per X1 e
// X2 le partite in cui la decisione di mulligan cambia; per X3 Teferi lanciato, ignorato, tolto e la win
// rate in quelle partite; per X4 Thoughtseize al turno 1 e le carte prese.
// Stampa il confronto e lo salva in DIR/varianti_rakdos.txt (DIR e' data/matchup).

#include "all_cards.hpp"
#include "matchup.hpp"
#include "varianti.hpp"

#include <algorithm>
#include <array>
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

enum class Kind { Base, Mulligan, Teferi, Seize };

struct Variant {
    std::string name;
    std::string description;
    Kind kind;
    RakdosAgentOptions options;
};

struct Results {
    std::vector<int> win;        // 1 se vince Rakdos, per seed
    std::vector<int> on_play;    // 1 se Rakdos gioca per primo
    std::vector<int> mulligans;  // mulligan di Rakdos, per seed
    std::array<long long, 8> hand_size{};
    std::vector<TeferiStats> teferi;  // per seed
    TeferiStats teferi_total;
    long long seize_cast = 0, seize_taken = 0;
    std::map<std::string, long long> seize_cards;  // carta presa al turno 1 -> volte
    std::vector<int> x4_fired;                     // 1 se la regola di X4 ha deciso, per seed
    long long defaults = 0;                        // decisioni "predefinito" di Rakdos
    double seconds = 0.0;
};

Results run(const MatchupConfig& base_config, const RakdosAgentOptions& options, long long games,
            std::uint64_t first_seed) {
    MatchupConfig config = base_config;
    config.rakdos_agent = options;
    const CardDatabase& db = *config.db;
    const CardDefId teferi = db.id_of("Teferi, Time Raveler");
    const CardDefId seize = db.id_of("Thoughtseize");
    Results r;
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        GameLog log;
        std::map<std::size_t, std::string> rules;
        const MatchupGame g = play_matchup(config, seed, &log, &rules);
        r.win.push_back(g.winner == kMatchupRakdos ? 1 : 0);
        r.on_play.push_back(g.uw_on_play ? 0 : 1);
        r.mulligans.push_back(g.rakdos_mulligans);
        ++r.hand_size[static_cast<std::size_t>(std::max(0, 7 - g.rakdos_mulligans))];
        r.defaults += static_cast<long long>(g.rakdos_defaults);
        const TeferiStats t = count_teferi(log, rules, teferi);
        r.teferi.push_back(t);
        r.teferi_total += t;
        r.x4_fired.push_back(std::any_of(rules.begin(), rules.end(), [](const auto& entry) {
                                 return entry.second == "Rakdos: Thoughtseize: turno 1 prima della creatura (variante X4)";
                             })
                                 ? 1
                                 : 0);
        const FirstTurnSeize f = first_turn_thoughtseize(log, seize);
        if (f.cast) ++r.seize_cast;
        if (f.taken != kNoDef) {
            ++r.seize_taken;
            ++r.seize_cards[db.get(f.taken).name];
        }
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
}

// Gli esiti di un lato (0 Rakdos in play, 1 in draw, 2 tutte).
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

// Win rate di Rakdos sulle partite scelte da `pick`, nella variante e nella base sugli stessi seed.
template <typename Pick>
std::string subset_line(const Results& base, const Results& r, Pick pick) {
    std::vector<int> b, v;
    for (std::size_t i = 0; i < r.win.size(); ++i) {
        if (!pick(i)) continue;
        b.push_back(base.win[i]);
        v.push_back(r.win[i]);
    }
    long long bw = 0, vw = 0;
    for (const int x : b) bw += x;
    for (const int x : v) vw += x;
    const auto n = static_cast<long long>(v.size());
    std::ostringstream o;
    o << n << " partite; win rate Rakdos " << pct(ratio(vw, n)) << ", base sugli stessi seed " << pct(ratio(bw, n));
    if (n > 0) {
        const PairedDiff d = paired_difference(b, v);
        o << ", differenza " << points(d.mean) << " [" << points(d.low) << ", " << points(d.high) << "]";
    }
    return o.str();
}

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
    config.turn_limit = limit;  // l'agente di UW resta il predefinito (base_v2)

    auto with = [](auto change) {
        RakdosAgentOptions o;
        change(o);
        return o;
    };
    std::vector<Variant> variants;
    variants.push_back({"base", "mulligan B, orologio contro Teferi a 2 turni, creatura prima di Thoughtseize al turno 1",
                        Kind::Base, RakdosAgentOptions{}});
    variants.push_back({"X1-A", "una terra: sempre mulligan", Kind::Mulligan,
                        with([](RakdosAgentOptions& o) { o.mulligan = MulliganVariant::A; })});
    variants.push_back({"X1-C", "una terra: basta una carta che cerca", Kind::Mulligan,
                        with([](RakdosAgentOptions& o) { o.mulligan = MulliganVariant::C; })});
    variants.push_back({"X2", "mano da 7 senza creatura economica con due tra Thoughtseize, Bolt e Flare", Kind::Mulligan,
                        with([](RakdosAgentOptions& o) { o.mulligan_keep_disruption = true; })});
    variants.push_back({"X3-togli", "togli sempre Teferi (soglia 0)", Kind::Teferi,
                        with([](RakdosAgentOptions& o) { o.teferi_race_clock = 0; })});
    variants.push_back({"X3-ignora", "ignora Teferi (sempre in corsa)", Kind::Teferi,
                        with([](RakdosAgentOptions& o) { o.teferi_race_clock = RakdosAgentOptions::kTeferiAlwaysRace; })});
    variants.push_back({"X3-soglia1", "orologio con soglia 1", Kind::Teferi,
                        with([](RakdosAgentOptions& o) { o.teferi_race_clock = 1; })});
    variants.push_back({"X3-soglia3", "orologio con soglia 3", Kind::Teferi,
                        with([](RakdosAgentOptions& o) { o.teferi_race_clock = 3; })});
    variants.push_back({"X4", "Thoughtseize prima della creatura da un mana al turno 1", Kind::Seize,
                        with([](RakdosAgentOptions& o) { o.t1_thoughtseize_first = true; })});

    std::vector<Results> results;
    for (const Variant& v : variants) {
        std::cerr << "variante " << v.name << "...\n";
        results.push_back(run(config, v.options, games, first_seed));
    }
    const Results& base = results[0];

    std::ostringstream o;
    o << "Varianti delle regole di Rakdos contro UW base_v2 - " << games << " partite per variante, seed da " << first_seed
      << " a " << first_seed + static_cast<std::uint64_t>(games) - 1 << " (gli stessi per tutte), limite " << limit << " turni\n";
    o << "Win rate di Rakdos (UW = 100 - Rakdos). Differenza appaiata rispetto alla base in punti percentuali, con "
         "l'intervallo al 95%.\n";
    const char* side_names[] = {"in play", "in draw", "totale"};
    for (std::size_t k = 0; k < variants.size(); ++k) {
        const Variant& v = variants[k];
        const Results& r = results[k];
        o << "\n=== " << v.name << ": " << v.description << " (" << std::fixed << std::setprecision(0) << r.seconds
          << " s) ===\n";
        for (const int side : {0, 1, 2}) {
            const std::vector<int> w = side_of(r, r.win, side);
            long long wins = 0;
            for (const int x : w) wins += x;
            o << "  Win rate Rakdos " << std::left << std::setw(8) << side_names[side] << std::right << " "
              << pct(ratio(wins, static_cast<long long>(w.size())));
            if (k != 0) {
                const PairedDiff d = paired_difference(side_of(base, base.win, side), w);
                o << "   vs base " << points(d.mean) << " [" << points(d.low) << ", " << points(d.high) << "]";
            }
            o << "\n";
        }
        o << "  Mani di Rakdos: 7 carte " << pct(ratio(r.hand_size[7], games)) << ", 6 carte "
          << pct(ratio(r.hand_size[6], games)) << ", 5 carte " << pct(ratio(r.hand_size[5], games)) << ", 4 o meno "
          << pct(ratio(r.hand_size[4] + r.hand_size[3] + r.hand_size[2] + r.hand_size[1] + r.hand_size[0], games))
          << "; decisioni \"predefinito\" di Rakdos: " << r.defaults << "\n";
        if (v.kind == Kind::Mulligan) {
            o << "  Partite in cui la decisione di mulligan cambia: "
              << subset_line(base, r, [&](std::size_t i) { return r.mulligans[i] != base.mulligans[i]; }) << "\n";
            for (const int side : {0, 1}) {
                o << "    Rakdos " << side_names[side] << ": " << subset_line(base, r, [&](std::size_t i) {
                    return r.mulligans[i] != base.mulligans[i] && (side == 0) == (r.on_play[i] == 1);
                }) << "\n";
            }
        }
        if (v.kind == Kind::Base || v.kind == Kind::Teferi) {
            const TeferiStats& t = r.teferi_total;
            long long ignored_games = 0, removed_games = 0;
            for (const TeferiStats& g : r.teferi) {
                ignored_games += g.race_turns > 0 ? 1 : 0;
                removed_games += g.removed > 0 ? 1 : 0;
            }
            o << "  Teferi lanciato da UW: " << t.cast << " volte; turni di Rakdos con Teferi in campo: "
              << t.race_turns + t.long_turns << ", in corsa (lo ignora) " << t.race_turns << ", in partita lunga (da togliere) "
              << t.long_turns << "; Teferi tolto dal campo " << t.removed << " volte (in " << removed_games
              << " partite); almeno un turno in corsa in " << ignored_games << " partite\n";
            o << "  Partite con Teferi: " << subset_line(base, r, [&](std::size_t i) { return r.teferi[i].cast > 0; }) << "\n";
            o << "  Partite con almeno un turno in corsa: "
              << subset_line(base, r, [&](std::size_t i) { return r.teferi[i].race_turns > 0; }) << "\n";
            o << "  Partite con Teferi tolto: "
              << subset_line(base, r, [&](std::size_t i) { return r.teferi[i].removed > 0; }) << "\n";
        }
        if (v.kind == Kind::Seize) {
            o << "  Partite in cui la regola di X4 decide (Thoughtseize al posto della creatura): "
              << subset_line(base, r, [&](std::size_t i) { return r.x4_fired[i] == 1; }) << "\n";
        }
        if (v.kind == Kind::Base || v.kind == Kind::Seize) {
            o << "  Thoughtseize al primo turno di Rakdos: lanciata in " << r.seize_cast << " partite ("
              << pct(ratio(r.seize_cast, games)) << "), prende una carta in " << r.seize_taken << " ("
              << pct(ratio(r.seize_taken, r.seize_cast)) << " dei lanci)\n";
            std::vector<std::pair<std::string, long long>> cards(r.seize_cards.begin(), r.seize_cards.end());
            std::stable_sort(cards.begin(), cards.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [name, n] : cards) {
                o << "    " << std::left << std::setw(34) << name << std::right << std::setw(7) << n << "  "
                  << pct(ratio(n, r.seize_taken)) << "\n";
            }
        }
    }
    std::cout << o.str();
    std::filesystem::create_directories(out_dir);
    std::ofstream(out_dir / "varianti_rakdos.txt") << o.str();
    return 0;
}
