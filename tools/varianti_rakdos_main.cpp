// Confronto di varianti delle regole di Rakdos nel matchup (docs/agente_rakdos.md, sezione 21).
//
//   varianti_rakdos [--games N] [--first-seed S] [--limit T] [--out DIR] [--only NOMI]
//   varianti_rakdos --x1-robustezza [--games N] [--first-seed S] [--limit T] [--out DIR]
//   varianti_rakdos --verifica-t1 [--games N] [--first-seed S] [--limit T] [--out DIR]
//   varianti_rakdos --base-r2 [--games N] [--first-seed S] [--limit T] [--out DIR]
//   varianti_rakdos --x6 [--games N] [--first-seed S] [--limit T] [--out DIR]
//
// Dal 15/11/2026 il predefinito di Rakdos e' base_r2 (docs/agente_rakdos.md, sezione 22). Le modalita'
// dei test A/B del 15/11/2026 (senza modalita' e --x1-robustezza) misurano le varianti sopra base_r1, la
// base di allora, e riproducono le tabelle della sezione 21.
//
// Senza modalita': gioca il matchup con l'agente di UW predefinito (base_v2) contro la base di Rakdos e
// ogni variante, sugli stessi seed: X1 (mulligan con una terra A e C), X2 (mano da 7 senza creatura
// economica con due carte di interazione), X3 (Teferi: togli sempre, ignora, soglia 1 e 3), X4
// (Thoughtseize prima della creatura al turno 1), X5 (cosa prende Thoughtseize, linea guida di
// Daniele). Per ognuna: win rate di Rakdos in play, in draw e in totale, con la differenza appaiata
// rispetto alla base e il suo intervallo al 95%; le mani di Rakdos da 7, 6 e 5 carte. Per X1 e X2 le
// partite in cui la decisione di mulligan cambia; per X3 Teferi lanciato, ignorato, tolto e la win rate
// in quelle partite; per X4 Thoughtseize al turno 1 e le carte prese; per X5 le partite in cui la scelta
// di Thoughtseize cambia e le carte prese da tutte le Thoughtseize. --only base,X5 gioca solo le
// varianti nominate (la base sempre). Uscita in DIR/varianti_rakdos.txt.
//
// --x1-robustezza: A, B e C contro tre versioni di UW (base_v1, base_v2, base_v2 + M1), con la
// differenza appaiata rispetto a B contro la stessa versione; poi, contro base_v2, le partite in cui C
// tiene una mano che B rifiuta: win rate con C e con B, seconda terra entro il turno 2 e 3 di Rakdos,
// nessuna magia nei primi due turni. Uscita in DIR/x1_robustezza.txt.
//
// --verifica-t1: la regola del turno 1 (sezioni 6 e 22) sulle partite di base_r2 e di base_r1:
// Thoughtseize in mano al primo turno di Rakdos, nessuna creatura da un mana lanciabile e Thoughtseize
// non lanciata, per motivo. Se base_r2 ha casi senza motivo valido, i replay leggibili delle prime 5
// partite in DIR/verifica_t1/. Uscita in DIR/verifica_t1.txt.
//
// --base-r2: base_r1, X1-C, X5 e la correzione del turno 1 (T1) da sole sopra base_r1, le tre coppie e
// base_r2, con la differenza appaiata rispetto a base_r1 e le sovrapposizioni. Uscita in DIR/base_r2.txt.
//
// --x6: base_r2 e base_r2 + X6 (al turno 1, con una sola terra in campo, la creatura da un mana invece
// del ciclo del Troll; sezione 23). Le partite in cui la regola scatta e la win rate li' con e senza X6,
// e il totale, in play e in draw. Uscita in DIR/x6.txt.
//
// DIR e' data/matchup.

#include "all_cards.hpp"
#include "matchup.hpp"
#include "replay.hpp"
#include "replay_leggibile.hpp"
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
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace mtg;

namespace {

enum class Kind { Base, Mulligan, Teferi, Seize, Pick };

struct Variant {
    std::string name;
    std::string description;
    Kind kind;
    RakdosAgentOptions options;
};

// Gli osservatori di una partita (tools/varianti.hpp): non cambiano la partita.
struct Observers final : MatchupObserver {
    EarlyRakdosTracker early;
    FirstTurnSeizeTracker t1;
    SeizePickTracker picks;
    explicit Observers(const CardDatabase& db) : early(db), t1(db), picks(db) {}
    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override {
        early.on_decision(s, legal, chosen);
        t1.on_decision(s, legal, chosen);
        picks.on_decision(s, legal, chosen);
    }
    void on_end(const GameState& s) override {
        early.on_end(s);
        t1.on_end(s);
        picks.on_end(s);
    }
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
    std::vector<int> t1fix_fired;                  // 1 se la correzione del turno 1 ha deciso, per seed
    std::vector<int> x6_fired;                     // 1 se la regola di X6 ha deciso, per seed
    std::vector<EarlyStats> early;                 // prime giocate di Rakdos, per seed
    std::vector<SeizeT1> t1;                       // verifica del turno 1, per seed
    std::vector<std::string> t1_detail;
    std::vector<int> pick_changed;                 // 1 se una scelta di Thoughtseize e' diversa dalla base
    std::map<std::string, long long> picks;        // carte prese da tutte le Thoughtseize -> volte
    long long picks_total = 0;
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
        Observers obs(db);
        const MatchupGame g = play_matchup(config, seed, &log, &rules, nullptr, nullptr, &obs);
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
        r.t1fix_fired.push_back(std::any_of(rules.begin(), rules.end(), [](const auto& entry) {
                                    return entry.second == "Rakdos: Thoughtseize: turno 1 prima del ciclo del Troll";
                                })
                                    ? 1
                                    : 0);
        r.x6_fired.push_back(std::any_of(rules.begin(), rules.end(), [](const auto& entry) {
                                 return entry.second == "Rakdos: creatura: turno 1 prima del ciclo del Troll (variante X6)";
                             })
                                 ? 1
                                 : 0);
        const FirstTurnSeize f = first_turn_thoughtseize(log, seize);
        if (f.cast) ++r.seize_cast;
        if (f.taken != kNoDef) {
            ++r.seize_taken;
            ++r.seize_cards[db.get(f.taken).name];
        }
        r.early.push_back(obs.early.stats());
        r.t1.push_back(obs.t1.result());
        r.t1_detail.push_back(obs.t1.detail());
        r.pick_changed.push_back(obs.picks.changed() ? 1 : 0);
        for (const auto& [def, k] : obs.picks.picks()) {
            r.picks[db.get(def).name] += k;
            r.picks_total += k;
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
bool on_side(const Results& r, std::size_t i, int side) { return side == 2 || (side == 0) == (r.on_play[i] == 1); }

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
std::string diff_text(const PairedDiff& d) {
    return points(d.mean) + " [" + points(d.low) + ", " + points(d.high) + "]";
}

const char* const kSideNames[] = {"in play", "in draw", "totale"};

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
    if (n > 0) o << ", differenza " << diff_text(paired_difference(b, v));
    return o.str();
}

// Le tre righe di win rate di Rakdos, con la differenza appaiata rispetto a `ref` (se c'e').
void win_lines(std::ostringstream& o, const Results& r, const Results* ref) {
    for (const int side : {0, 1, 2}) {
        const std::vector<int> w = side_of(r, r.win, side);
        long long wins = 0;
        for (const int x : w) wins += x;
        o << "  Win rate Rakdos " << std::left << std::setw(8) << kSideNames[side] << std::right << " "
          << pct(ratio(wins, static_cast<long long>(w.size())));
        if (ref != nullptr) o << "   vs riferimento " << diff_text(paired_difference(side_of(*ref, ref->win, side), w));
        o << "\n";
    }
}

void hand_line(std::ostringstream& o, const Results& r, long long games) {
    o << "  Mani di Rakdos: 7 carte " << pct(ratio(r.hand_size[7], games)) << ", 6 carte "
      << pct(ratio(r.hand_size[6], games)) << ", 5 carte " << pct(ratio(r.hand_size[5], games)) << ", 4 o meno "
      << pct(ratio(r.hand_size[4] + r.hand_size[3] + r.hand_size[2] + r.hand_size[1] + r.hand_size[0], games))
      << "; decisioni \"predefinito\" di Rakdos: " << r.defaults << "\n";
}

void picks_lines(std::ostringstream& o, const Results& r) {
    o << "  Carte prese da tutte le Thoughtseize: " << r.picks_total << "\n";
    std::vector<std::pair<std::string, long long>> cards(r.picks.begin(), r.picks.end());
    std::stable_sort(cards.begin(), cards.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [name, n] : cards) {
        o << "    " << std::left << std::setw(34) << name << std::right << std::setw(7) << n << "  "
          << pct(ratio(n, r.picks_total)) << "\n";
    }
}

std::string header(const std::string& what, long long games, std::uint64_t first_seed, int limit) {
    std::ostringstream o;
    o << what << " - " << games << " partite per riga, seed da " << first_seed << " a "
      << first_seed + static_cast<std::uint64_t>(games) - 1 << " (gli stessi per tutte), limite " << limit << " turni\n";
    o << "Win rate di Rakdos (UW = 100 - Rakdos). Differenza appaiata in punti percentuali, con l'intervallo al 95%.\n";
    return o.str();
}

// --- Modalita' predefinita: le varianti contro la base ------------------------------------------------

std::string compare_variants(const MatchupConfig& config, const std::set<std::string>& only, long long games,
                             std::uint64_t first_seed, int limit) {
    // Le varianti dei test A/B del 15/11/2026 si misurano sopra base_r1, la base di allora (sezione 21).
    auto with = [](auto change) {
        RakdosAgentOptions o = RakdosAgentOptions::base_r1();
        change(o);
        return o;
    };
    std::vector<Variant> all;
    all.push_back({"base", "base_r1: mulligan B, orologio contro Teferi a 2 turni, creatura prima di Thoughtseize al turno 1",
                   Kind::Base, RakdosAgentOptions::base_r1()});
    all.push_back({"X1-A", "una terra: sempre mulligan", Kind::Mulligan,
                   with([](RakdosAgentOptions& o) { o.mulligan = MulliganVariant::A; })});
    all.push_back({"X1-C", "una terra: basta una carta che cerca", Kind::Mulligan,
                   with([](RakdosAgentOptions& o) { o.mulligan = MulliganVariant::C; })});
    all.push_back({"X2", "mano da 7 senza creatura economica con due tra Thoughtseize, Bolt e Flare", Kind::Mulligan,
                   with([](RakdosAgentOptions& o) { o.mulligan_keep_disruption = true; })});
    all.push_back({"X3-togli", "togli sempre Teferi (soglia 0)", Kind::Teferi,
                   with([](RakdosAgentOptions& o) { o.teferi_race_clock = 0; })});
    all.push_back({"X3-ignora", "ignora Teferi (sempre in corsa)", Kind::Teferi,
                   with([](RakdosAgentOptions& o) { o.teferi_race_clock = RakdosAgentOptions::kTeferiAlwaysRace; })});
    all.push_back({"X3-soglia1", "orologio con soglia 1", Kind::Teferi,
                   with([](RakdosAgentOptions& o) { o.teferi_race_clock = 1; })});
    all.push_back({"X3-soglia3", "orologio con soglia 3", Kind::Teferi,
                   with([](RakdosAgentOptions& o) { o.teferi_race_clock = 3; })});
    all.push_back({"X4", "Thoughtseize prima della creatura da un mana al turno 1", Kind::Seize,
                   with([](RakdosAgentOptions& o) { o.t1_thoughtseize_first = true; })});
    all.push_back({"X5", "Thoughtseize: la risposta piu' forte, poi la minaccia che non possiamo rimuovere (linea guida di Daniele)",
                   Kind::Pick, with([](RakdosAgentOptions& o) { o.thoughtseize_answer_or_threat = true; })});
    std::vector<Variant> variants;
    for (const Variant& v : all) {
        if (v.kind == Kind::Base || only.empty() || only.contains(v.name)) variants.push_back(v);
    }

    std::vector<Results> results;
    for (const Variant& v : variants) {
        std::cerr << "variante " << v.name << "...\n";
        results.push_back(run(config, v.options, games, first_seed));
    }
    const Results& base = results[0];

    std::ostringstream o;
    o << header("Varianti delle regole di Rakdos contro UW base_v2", games, first_seed, limit);
    o << "Il riferimento delle differenze e' la base.\n";
    for (std::size_t k = 0; k < variants.size(); ++k) {
        const Variant& v = variants[k];
        const Results& r = results[k];
        o << "\n=== " << v.name << ": " << v.description << " (" << std::fixed << std::setprecision(0) << r.seconds
          << " s) ===\n";
        win_lines(o, r, k == 0 ? nullptr : &base);
        hand_line(o, r, games);
        if (v.kind == Kind::Mulligan) {
            o << "  Partite in cui la decisione di mulligan cambia: "
              << subset_line(base, r, [&](std::size_t i) { return r.mulligans[i] != base.mulligans[i]; }) << "\n";
            for (const int side : {0, 1}) {
                o << "    Rakdos " << kSideNames[side] << ": " << subset_line(base, r, [&](std::size_t i) {
                    return r.mulligans[i] != base.mulligans[i] && on_side(r, i, side);
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
        if (v.kind == Kind::Pick) {
            o << "  Partite in cui la scelta di Thoughtseize cambia (diversa dalla base nello stesso stato): "
              << subset_line(base, r, [&](std::size_t i) { return r.pick_changed[i] == 1; }) << "\n";
            for (const int side : {0, 1}) {
                o << "    Rakdos " << kSideNames[side] << ": " << subset_line(base, r, [&](std::size_t i) {
                    return r.pick_changed[i] == 1 && on_side(r, i, side);
                }) << "\n";
            }
        }
        if (v.kind == Kind::Base || v.kind == Kind::Pick) picks_lines(o, r);
    }
    return o.str();
}

// --- --x1-robustezza ----------------------------------------------------------------------------------

std::string x1_robustness(MatchupConfig config, long long games, std::uint64_t first_seed, int limit) {
    UwAgentOptions v1;
    v1.riddler_refill_ignores_counters = false;
    v1.teferi_any_target = false;
    v1.mulligan_more_answers = false;
    v1.riddler_warp_with_consign = false;
    UwAgentOptions m1;
    m1.mulligan_more_answers = true;
    const std::array<std::pair<std::string, UwAgentOptions>, 3> uws = {
        {{"base_v1", v1}, {"base_v2", UwAgentOptions{}}, {"base_v2 + M1", m1}}};
    const std::array<std::pair<std::string, MulliganVariant>, 3> rules = {
        {{"B", MulliganVariant::B}, {"A", MulliganVariant::A}, {"C", MulliganVariant::C}}};

    std::ostringstream o;
    o << header("Robustezza di X1: mulligan con una terra A, B, C contro tre versioni di UW", games, first_seed, limit);
    o << "Il riferimento delle differenze e' B contro la stessa versione di UW.\n";
    Results b_v2, c_v2;
    for (const auto& [uw_name, uw] : uws) {
        config.uw_agent = uw;
        std::vector<Results> rs;
        for (const auto& [name, variant] : rules) {
            std::cerr << "UW " << uw_name << ", mulligan " << name << "...\n";
            RakdosAgentOptions opt = RakdosAgentOptions::base_r1();  // la misura del 15/11/2026, sopra base_r1
            opt.mulligan = variant;
            rs.push_back(run(config, opt, games, first_seed));
        }
        o << "\n##### UW " << uw_name << " #####\n";
        for (std::size_t k = 0; k < rs.size(); ++k) {
            o << "\n=== mulligan " << rules[k].first << " (" << std::fixed << std::setprecision(0) << rs[k].seconds << " s) ===\n";
            win_lines(o, rs[k], k == 0 ? nullptr : &rs[0]);
            hand_line(o, rs[k], games);
        }
        if (uw_name == "base_v2") {
            b_v2 = rs[0];
            c_v2 = rs[2];
        }
    }

    // Le partite in cui C tiene una mano che B rifiuta (contro base_v2).
    long long reverse = 0;
    for (std::size_t i = 0; i < b_v2.win.size(); ++i) reverse += c_v2.mulligans[i] > b_v2.mulligans[i] ? 1 : 0;
    o << "\n##### Contro base_v2: partite in cui C tiene una mano che B rifiuta (meno mulligan con C) #####\n";
    o << "  Partite con piu' mulligan con C che con B (attese 0): " << reverse << "\n";
    for (const int side : {0, 1, 2}) {
        std::vector<std::size_t> idx;
        for (std::size_t i = 0; i < b_v2.win.size(); ++i) {
            if (c_v2.mulligans[i] < b_v2.mulligans[i] && on_side(b_v2, i, side)) idx.push_back(i);
        }
        const auto n = static_cast<long long>(idx.size());
        std::vector<int> bw, cw;
        long long b_wins = 0, c_wins = 0;
        std::array<long long, 2> land2{}, land3{}, nothing{};  // [B, C]
        for (const std::size_t i : idx) {
            bw.push_back(b_v2.win[i]);
            cw.push_back(c_v2.win[i]);
            b_wins += b_v2.win[i];
            c_wins += c_v2.win[i];
            const std::array<const EarlyStats*, 2> e = {&b_v2.early[i], &c_v2.early[i]};
            for (std::size_t k = 0; k < 2; ++k) {
                land2[k] += e[k]->lands_by_turn2 >= 2 ? 1 : 0;
                land3[k] += e[k]->lands_by_turn3 >= 2 ? 1 : 0;
                nothing[k] += e[k]->spells_before_turn3 == 0 ? 1 : 0;
            }
        }
        o << "\n  Rakdos " << kSideNames[side] << ": " << n << " partite\n";
        o << "    Win rate Rakdos con C " << pct(ratio(c_wins, n)) << ", con B " << pct(ratio(b_wins, n));
        if (n > 0) o << ", differenza C - B " << diff_text(paired_difference(bw, cw));
        o << "\n";
        o << "    Seconda terra entro la fine del turno 2 di Rakdos: C " << pct(ratio(land2[1], n)) << " (B "
          << pct(ratio(land2[0], n)) << ")\n";
        o << "    Seconda terra entro la fine del turno 3 di Rakdos: C " << pct(ratio(land3[1], n)) << " (B "
          << pct(ratio(land3[0], n)) << ")\n";
        o << "    Nessuna magia lanciata nei primi due turni:          C " << pct(ratio(nothing[1], n)) << " (B "
          << pct(ratio(nothing[0], n)) << ")\n";
    }
    return o.str();
}

// --- --verifica-t1 ------------------------------------------------------------------------------------

// La tabella della verifica del turno 1 per un agente; i replay leggibili dei primi casi senza motivo
// valido solo con `replays`.
std::string t1_table(const MatchupConfig& base_config, const std::string& name, const RakdosAgentOptions& options,
                     long long games, std::uint64_t first_seed, const std::filesystem::path& out_dir, bool replays) {
    MatchupConfig config = base_config;
    config.rakdos_agent = options;
    std::cerr << name << "...\n";
    const Results r = run(config, options, games, first_seed);
    const std::array<SeizeT1, 8> kinds = {SeizeT1::NotInHand,         SeizeT1::Cast,        SeizeT1::CreatureCastable,
                                          SeizeT1::NoBlackSource,     SeizeT1::LifeFloor,   SeizeT1::TrollNoSecondLand,
                                          SeizeT1::BoltOnSight,       SeizeT1::NoValidReason};
    std::ostringstream o;
    o << "\n##### " << name << " #####\n";
    o << "  " << std::left << std::setw(40) << "Caso" << std::right << std::setw(10) << "in play" << std::setw(10)
      << "in draw" << std::setw(10) << "totale" << "\n";
    for (const SeizeT1 kind : kinds) {
        std::array<long long, 3> n{};
        for (std::size_t i = 0; i < r.t1.size(); ++i) {
            if (r.t1[i] != kind) continue;
            ++n[r.on_play[i] == 1 ? 0 : 1];
            ++n[2];
        }
        o << "  " << std::left << std::setw(40) << seize_t1_name(kind) << std::right << std::setw(10) << n[0]
          << std::setw(10) << n[1] << std::setw(10) << n[2] << "\n";
    }
    std::vector<std::size_t> bad;
    for (std::size_t i = 0; i < r.t1.size(); ++i) {
        if (r.t1[i] == SeizeT1::NoValidReason) bad.push_back(i);
    }
    o << "\n  Senza motivo valido: " << bad.size() << ", per azione scelta al posto di Thoughtseize:\n";
    std::map<std::string, std::array<long long, 3>> by_detail;
    for (const std::size_t i : bad) {
        const std::string key = r.t1_detail[i].empty() ? "fonte nera stappata, lancio mai legale" : r.t1_detail[i];
        ++by_detail[key][r.on_play[i] == 1 ? 0 : 1];
        ++by_detail[key][2];
    }
    for (const auto& [key, n] : by_detail) {
        o << "    " << std::left << std::setw(40) << key << std::right << " in play " << n[0] << ", in draw " << n[1]
          << ", totale " << n[2] << "\n";
    }
    o << "  Le prime partite:\n";
    for (std::size_t k = 0; k < bad.size() && k < 20; ++k) {
        const std::size_t i = bad[k];
        o << "    seed " << first_seed + i << " (Rakdos " << (r.on_play[i] == 1 ? "in play" : "in draw") << "): "
          << (r.t1_detail[i].empty() ? "fonte nera stappata, lancio mai legale" : "scelta al posto di Thoughtseize: " + r.t1_detail[i])
          << "\n";
    }
    if (replays && !bad.empty()) {
        const std::filesystem::path dir = out_dir / "verifica_t1";
        std::filesystem::create_directories(dir);
        std::ostringstream jsonl;
        for (std::size_t k = 0; k < bad.size() && k < 5; ++k) {
            const std::uint64_t seed = first_seed + bad[k];
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
            write_replay(jsonl, *config.db, log, info);
        }
        std::ofstream(dir / "replay_scelti.jsonl") << jsonl.str();
        std::istringstream in(jsonl.str());
        o << "\n  Replay leggibili delle prime 5:\n";
        for (const std::filesystem::path& file : write_readable_replays(in, dir)) o << "    " << file.string() << "\n";
    }
    return o.str();
}

std::string t1_check(const MatchupConfig& config, long long games, std::uint64_t first_seed, int limit,
                     const std::filesystem::path& out_dir) {
    std::ostringstream o;
    o << header("Verifica della regola del turno 1 di Rakdos contro UW base_v2", games, first_seed, limit);
    o << "Thoughtseize in mano al primo turno di Rakdos (dal suo primo turno al turno di UW che segue, mulligan esclusi).\n";
    o << "Motivi validi: nessuna fonte nera, soglia di vita, ciclo del Troll senza un'altra terra in mano, Bolt a vista su\n"
         "Guide of Souls o Ocelot Pride (sezione 22). Il predefinito (base_r2) deve avere zero casi senza motivo valido;\n"
         "i replay leggibili dei primi casi, se ce ne sono, in DIR/verifica_t1/.\n";
    o << t1_table(config, "base_r2 (predefinito)", RakdosAgentOptions{}, games, first_seed, out_dir, true);
    o << t1_table(config, "base_r1 (per confronto, sezione 21)", RakdosAgentOptions::base_r1(), games, first_seed, out_dir,
                  false);
    return o.str();
}

// --- --base-r2: le tre modifiche del 15/11/2026, da sole, in coppia e insieme ------------------------

std::string compare_r2(const MatchupConfig& config, long long games, std::uint64_t first_seed, int limit) {
    struct Row {
        std::string name;
        bool c, x5, t1;  // X1-C, X5, correzione del turno 1, sopra base_r1
    };
    const std::vector<Row> rows = {{"base_r1", false, false, false}, {"X1-C", true, false, false},
                                   {"X5", false, true, false},       {"T1", false, false, true},
                                   {"X1-C+X5", true, true, false},   {"X1-C+T1", true, false, true},
                                   {"X5+T1", false, true, true},     {"base_r2", true, true, true}};
    std::vector<Results> rs;
    for (const Row& row : rows) {
        RakdosAgentOptions o = RakdosAgentOptions::base_r1();
        if (row.c) o.mulligan = MulliganVariant::C;
        o.thoughtseize_answer_or_threat = row.x5;
        o.t1_seize_before_troll = row.t1;
        std::cerr << row.name << "...\n";
        rs.push_back(run(config, o, games, first_seed));
    }
    const Results& r1 = rs[0];

    std::ostringstream o;
    o << header("base_r2: X1-C, X5 e la correzione del turno 1 (T1) sopra base_r1, contro UW base_v2", games, first_seed,
                limit);
    o << "Il riferimento delle differenze e' base_r1 (docs/agente_rakdos.md, sezione 22).\n";
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const Row& row = rows[k];
        const Results& r = rs[k];
        o << "\n=== " << row.name << " (" << std::fixed << std::setprecision(0) << r.seconds << " s) ===\n";
        win_lines(o, r, k == 0 ? nullptr : &r1);
        hand_line(o, r, games);
        if (k == 0) continue;
        if (row.c) {
            o << "  Partite in cui la decisione di mulligan cambia: "
              << subset_line(r1, r, [&](std::size_t i) { return r.mulligans[i] != r1.mulligans[i]; }) << "\n";
        }
        if (row.x5) {
            o << "  Partite in cui la scelta di Thoughtseize e' diversa da base_r1 nello stesso stato: "
              << subset_line(r1, r, [&](std::size_t i) { return r.pick_changed[i] == 1; }) << "\n";
        }
        if (row.t1) {
            o << "  Partite in cui la correzione del turno 1 decide (Thoughtseize al posto del ciclo del Troll): "
              << subset_line(r1, r, [&](std::size_t i) { return r.t1fix_fired[i] == 1; }) << "\n";
            o << "  Thoughtseize al primo turno di Rakdos: lanciata in " << r.seize_cast << " partite (base_r1 " << r1.seize_cast
              << ")\n";
        }
    }

    // Sovrapposizioni: la somma delle differenze da sole meno la differenza insieme, per seed, con
    // l'intervallo al 95% (positiva: insieme valgono meno della somma).
    auto overlap = [&](std::vector<std::size_t> solos, std::size_t together) {
        std::vector<int> sum_solos, base_and_together;
        const int k = static_cast<int>(solos.size());
        for (std::size_t i = 0; i < r1.win.size(); ++i) {
            int s = 0;
            for (const std::size_t j : solos) s += rs[j].win[i];
            sum_solos.push_back(s);
            base_and_together.push_back((k - 1) * r1.win[i] + rs[together].win[i]);
        }
        double sum = 0.0;
        for (const std::size_t j : solos) sum += paired_difference(r1.win, rs[j].win).mean;
        const double both = paired_difference(r1.win, rs[together].win).mean;
        std::ostringstream line;
        line << "da sole " << points(sum) << ", insieme " << points(both) << ", sovrapposizione "
             << diff_text(paired_difference(base_and_together, sum_solos));
        return line.str();
    };
    o << "\n=== Sovrapposizioni (differenza totale rispetto a base_r1, in punti) ===\n";
    o << "  X1-C e X5:      " << overlap({1, 2}, 4) << "\n";
    o << "  X1-C e T1:      " << overlap({1, 3}, 5) << "\n";
    o << "  X5 e T1:        " << overlap({2, 3}, 6) << "\n";
    o << "  X1-C, X5 e T1:  " << overlap({1, 2, 3}, 7) << "\n";
    return o.str();
}

// --- --x6: la creatura da un mana invece del ciclo del Troll al turno 1 (sezione 23) ------------------

std::string compare_x6(const MatchupConfig& config, long long games, std::uint64_t first_seed, int limit) {
    RakdosAgentOptions x6;  // sopra base_r2, il predefinito
    x6.t1_creature_before_troll = true;
    std::cerr << "base_r2...\n";
    const Results base = run(config, RakdosAgentOptions{}, games, first_seed);
    std::cerr << "base_r2 + X6...\n";
    const Results r = run(config, x6, games, first_seed);

    std::ostringstream o;
    o << header("Variante X6: la creatura da un mana invece del ciclo del Troll al turno 1, sopra base_r2, contro UW base_v2",
                games, first_seed, limit);
    o << "Il riferimento delle differenze e' base_r2 (docs/agente_rakdos.md, sezione 23).\n";
    o << "\n=== base_r2 (" << std::fixed << std::setprecision(0) << base.seconds << " s) ===\n";
    win_lines(o, base, nullptr);
    o << "\n=== base_r2 + X6 (" << std::fixed << std::setprecision(0) << r.seconds << " s) ===\n";
    win_lines(o, r, &base);
    hand_line(o, r, games);
    long long base_fired = 0;
    for (const int x : base.x6_fired) base_fired += x;
    o << "  Regola di X6 nelle partite di base_r2 (attese 0): " << base_fired << "\n";
    o << "\n=== Partite in cui la regola di X6 scatta (la creatura al posto del ciclo del Troll) ===\n";
    for (const int side : {0, 1, 2}) {
        o << "  Rakdos " << std::left << std::setw(8) << kSideNames[side] << std::right << ": "
          << subset_line(base, r, [&](std::size_t i) { return r.x6_fired[i] == 1 && on_side(r, i, side); }) << "\n";
    }
    o << "  Le prime partite:";
    int shown = 0;
    for (std::size_t i = 0; i < r.x6_fired.size() && shown < 10; ++i) {
        if (r.x6_fired[i] == 0) continue;
        o << " " << first_seed + i << (r.on_play[i] == 1 ? " (play)" : " (draw)");
        ++shown;
    }
    o << "\n";
    return o.str();
}

}  // namespace

int main(int argc, char** argv) {
    long long games = 100000;
    std::uint64_t first_seed = 1;
    int limit = 60;
    std::filesystem::path out_dir = std::filesystem::path(DATA_DIR) / "matchup";
    std::string mode;
    std::set<std::string> only;
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
        else if (arg == "--only") {
            std::istringstream names(next());
            for (std::string name; std::getline(names, name, ',');) only.insert(name);
        } else if (arg == "--x1-robustezza" || arg == "--verifica-t1" || arg == "--base-r2" || arg == "--x6") mode = arg;
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
    config.turn_limit = limit;  // l'agente di UW resta il predefinito (base_v2), salvo --x1-robustezza

    std::string text, file;
    if (mode == "--x1-robustezza") {
        text = x1_robustness(config, games, first_seed, limit);
        file = "x1_robustezza.txt";
    } else if (mode == "--verifica-t1") {
        text = t1_check(config, games, first_seed, limit, out_dir);
        file = "verifica_t1.txt";
    } else if (mode == "--base-r2") {
        text = compare_r2(config, games, first_seed, limit);
        file = "base_r2.txt";
    } else if (mode == "--x6") {
        text = compare_x6(config, games, first_seed, limit);
        file = "x6.txt";
    } else {
        text = compare_variants(config, only, games, first_seed, limit);
        file = "varianti_rakdos.txt";
    }
    std::cout << text;
    std::filesystem::create_directories(out_dir);
    std::ofstream(out_dir / file) << text;
    return 0;
}
