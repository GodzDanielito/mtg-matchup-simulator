// Il giocatore forte di UW, livello A minimo (docs/ricerca.md, passo 2).
//
//   ricerca --partite [--da S] [--a S] [--mondi R] [--thread N] [--blocco B] [--out DIR] [--canarino] [--codice C]
//           [--rakdos base_r1|base_r2|base_r3]
//       UW forte (o il canarino) contro Rakdos euristico (base_r3 per default; i rollout assumono sempre base_r3) sui
//       seed [da, a], esecuzione riprendibile a blocchi: partite.jsonl, ricerca.jsonl, anomalie.jsonl (partite vere
//       oltre il limite della pila, fuori dai dati), manifest.json, riepilogo.txt (differenza appaiata con UW euristico
//       v1 contro lo stesso Rakdos).
//   ricerca --pila --seed S [--mondi R] [--rakdos ...] [--out DIR]
//       rigioca la partita forte del seed e racconta i rollout interrotti dal limite della pila (passo 3, punto 0a).
//   ricerca --confronta DIR_A DIR_B
//       differenza appaiata della win rate di UW tra le partite.jsonl di due esecuzioni (A - B), play e draw.
//   ricerca --curva [--decisioni N] [--budget 16,32,...] [--riferimento M] [--da-seed S] [--thread N] [--blocco B]
//       curva di convergenza (sezione 5.2): curva.jsonl e riepilogo.txt.
//   ricerca --posizioni [--budget 16,...,256] [--ricerche K] [--thread N] [--blocco B] [--solo 1,2c] [--replay 3]
//       le posizioni test (sezione 5.1): posizioni.jsonl, riepilogo.txt, replay leggibili dei casi diversi.
//
// Le esecuzioni si riprendono rilanciando lo stesso comando: si rifanno solo i blocchi mancanti. Il numero di thread
// non cambia i risultati (ogni seme dipende dal seed della partita e dall'indice della decisione).

#include "all_cards.hpp"
#include "esecuzioni.hpp"
#include "forte.hpp"
#include "matchup.hpp"
#include "posizioni.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace mtg;
namespace fs = std::filesystem;
using nlohmann::ordered_json;

namespace {

// Avanzamento dentro un blocco: ogni 60 secondi, quanti elementi sono finiti e quanto manca alla fine del blocco.
class BlockProgress {
public:
    explicit BlockProgress(std::size_t total) : total_(total), start_(Clock::now()), last_(start_) {}
    void one_done() {
        const std::size_t done = ++done_;
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        if (now - last_ < std::chrono::seconds(60) && done != total_) return;
        last_ = now;
        const double secs = std::chrono::duration<double>(now - start_).count();
        char buf[160];
        std::snprintf(buf, sizeof buf, "  %zu/%zu del blocco, %.0f s, circa %.0f s alla fine del blocco\n", done, total_, secs,
                      secs / static_cast<double>(done) * static_cast<double>(total_ - done));
        std::cout << buf << std::flush;
    }

private:
    using Clock = std::chrono::steady_clock;
    std::size_t total_;
    std::atomic<std::size_t> done_{0};
    Clock::time_point start_, last_;
    std::mutex mutex_;
};

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw EngineError("non riesco a leggere " + p.string());
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

std::vector<ordered_json> read_jsonl(const fs::path& p) {
    std::vector<ordered_json> rows;
    std::istringstream in(read_file(p));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) rows.push_back(ordered_json::parse(line));
    }
    return rows;
}

std::string decimal(double x, int digits, bool sign = false) {
    char buf[64];
    std::snprintf(buf, sizeof buf, sign ? "%+.*f" : "%.*f", digits, x);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::vector<std::uint32_t> parse_budgets(const std::string& text) {
    std::vector<std::uint32_t> out;
    std::stringstream in(text);
    for (std::string item; std::getline(in, item, ',');) out.push_back(static_cast<std::uint32_t>(std::stoul(item)));
    if (out.empty()) throw EngineError("--budget vuoto");
    return out;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream in(text);
    for (std::string item; std::getline(in, item, ',');) out.push_back(item);
    return out;
}

std::string paired_line(const std::string& label, const PairedDifference& d) {
    return label + " (" + std::to_string(d.n) + " partite): " + decimal(100 * d.a, 2) + "% contro " +
           decimal(100 * d.b, 2) + "%, differenza " + decimal(100 * d.diff, 2, true) + " [" +
           decimal(100 * d.low, 2, true) + "; " + decimal(100 * d.high, 2, true) + "] punti (IC 95%)\n";
}

// UW vincitore per seed, da una partite.jsonl.
std::map<std::uint64_t, int> uw_wins(const std::vector<ordered_json>& rows) {
    std::map<std::uint64_t, int> out;
    for (const auto& r : rows) out[r["seed"].get<std::uint64_t>()] = r["vincitore"] == "UW" ? 1 : 0;
    return out;
}

// A - B appaiato su tutti, UW in play (seed dispari), UW in draw.
std::string compare(const std::map<std::uint64_t, int>& a, const std::map<std::uint64_t, int>& b,
                    const std::string& name_a, const std::string& name_b) {
    std::vector<int> va, vb, pa, pb, da, db_;
    for (const auto& [seed, w] : a) {
        const auto it = b.find(seed);
        if (it == b.end()) throw EngineError("confronto: il seed " + std::to_string(seed) + " manca in " + name_b);
        va.push_back(w);
        vb.push_back(it->second);
        (seed % 2 == 1 ? pa : da).push_back(w);
        (seed % 2 == 1 ? pb : db_).push_back(it->second);
    }
    std::string out = "Win rate di UW: " + name_a + " contro " + name_b + ", differenza appaiata per seed\n";
    out += paired_line("  tutte", paired_difference(va, vb));
    if (!pa.empty()) out += paired_line("  UW in play", paired_difference(pa, pb));
    if (!da.empty()) out += paired_line("  UW in draw", paired_difference(da, db_));
    return out;
}

double manifest_seconds(const fs::path& dir) {
    const ordered_json m = ordered_json::parse(read_file(dir / "manifest.json"));
    double total = 0;
    for (const auto& [key, block] : m["blocchi"].items()) total += block["secondi"].get<double>();
    return total;
}

struct Common {
    CardDatabase db;
    MatchupConfig base;
};

int run_games(const Common& c, const std::map<std::string, std::string>& opt, bool cheat) {
    const std::uint64_t first = std::stoull(opt.count("--da") ? opt.at("--da") : "1");
    const std::uint64_t last = std::stoull(opt.count("--a") ? opt.at("--a") : "2000");
    const auto worlds = static_cast<std::uint32_t>(std::stoul(opt.count("--mondi") ? opt.at("--mondi") : "64"));
    const int threads = std::stoi(opt.count("--thread") ? opt.at("--thread") : "20");
    const std::string rakdos = opt.count("--rakdos") ? opt.at("--rakdos") : "base_r3";
    StrongRun run;
    run.base = c.base;
    run.base.rakdos_agent = rakdos_variant(rakdos);  // il Rakdos vero; i rollout restano base_r3
    run.strong.worlds = worlds;
    run.cheat = cheat;
    run.threads = threads;
    const std::string suffix = rakdos == "base_r3" ? "" : "_contro_" + rakdos;
    const fs::path dir = opt.count("--out") ? fs::path(opt.at("--out"))
                                            : fs::path(DATA_DIR) / "ricerca" /
                                                  ((cheat ? "canarino_R" : "forte_R") + std::to_string(worlds) + suffix);
    if (opt.count("--nuova")) fs::remove_all(dir);  // ricominciare da zero (test di fumo)
    ordered_json config;
    config["modo"] = cheat ? "canarino (diagnostica: stato vero, mai nei dati)" : "partite";
    config["giocatore"] = "UW forte, livello A minimo (passo 2): ricerca piatta, mosse composte, mondi comuni";
    config["mondi"] = worlds;
    config["fiducia"] = run.strong.confidence;
    config["ombra_e_rollout_uw"] = "base_v2";
    config["rakdos"] = rakdos;
    config["rollout_rakdos"] = "base_r3";
    config["liste"] = {"uw_blink_sim.json", "rakdos_delirium.json"};
    config["limite_turni"] = c.base.turn_limit;
    config["codice"] = opt.count("--codice") ? opt.at("--codice") : "non indicato";
    RunSpec spec;
    spec.config = config.dump();
    spec.first = first;
    spec.last = last;
    spec.block = std::stoull(opt.count("--blocco") ? opt.at("--blocco") : "500");
    spec.files = {"partite", "ricerca", "anomalie"};
    RunOptions options;
    options.progress = &std::cout;
    if (opt.count("--max-blocchi")) options.max_blocks = std::stoi(opt.at("--max-blocchi"));
    std::cout << (cheat ? "Canarino" : "UW forte") << " contro Rakdos " << rakdos << " (rollout base_r3), seed " << first << "-" << last << ", R = "
              << worlds << ", " << threads << " thread, blocchi da " << spec.block << " in " << dir.string() << "\n"
              << std::flush;
    const RunResult r = run_resumable(dir, spec, [&](std::uint64_t a, std::uint64_t b) {
        const StrongBlock blk = play_strong_block(run, a, b, &std::cout);
        return std::vector<std::string>{blk.games, blk.searches, blk.anomalies};
    }, options);
    std::cout << "Blocchi: " << r.blocks << " (fatti ora " << r.done_now << ", gia' fatti " << r.skipped << ") in "
              << decimal(r.seconds, 0) << " s\n";
    if (!r.complete) {
        std::cout << "Esecuzione non completa: rilanciare lo stesso comando per riprenderla.\n";
        return 0;
    }

    // Riepilogo: tutto dai file (anche i blocchi fatti in una sessione precedente).
    const std::vector<ordered_json> games = read_jsonl(dir / "partite.jsonl");
    const std::vector<ordered_json> searches = read_jsonl(dir / "ricerca.jsonl");
    const std::vector<ordered_json> anomalies = read_jsonl(dir / "anomalie.jsonl");
    std::vector<MatchupGame> heuristic(games.size());
    parallel_for(games.size(), threads, [&](std::size_t i) {  // UW euristico v1 contro lo stesso Rakdos
        heuristic[i] = play_matchup(run.base, games[i]["seed"].get<std::uint64_t>());
    });
    std::map<std::uint64_t, int> base_wins;
    for (const MatchupGame& g : heuristic) base_wins[g.seed] = g.winner == kMatchupUw ? 1 : 0;
    std::uint64_t rollouts = 0, rollout_decisions = 0, deviations = 0, moves = 0, uw_decisions = 0, aborted_total = 0;
    std::uint64_t discarded_total = 0, searches_with_discards = 0;
    std::map<std::string, std::uint64_t> per_kind, dev_per_kind;
    for (const auto& s : searches) {
        const std::uint64_t k = s["mosse"].size();
        moves += k;
        rollouts += k * s["mondi"].get<std::uint64_t>();
        aborted_total += s.value("interrotti", std::uint64_t{0});
        const std::uint64_t d = s.value("scartati", std::uint64_t{0});
        discarded_total += d;
        searches_with_discards += d > 0 ? 1 : 0;
        rollout_decisions += s["decisioni_rollout"].get<std::uint64_t>();
        const bool dev = s["scostamento"].get<bool>();
        deviations += dev ? 1 : 0;
        ++per_kind[s["tipo"].get<std::string>()];
        dev_per_kind[s["tipo"].get<std::string>()] += dev ? 1 : 0;
    }
    for (const auto& g : games) uw_decisions += g["decisioni_uw"].get<std::uint64_t>();
    const double secs = manifest_seconds(dir);
    const double n = static_cast<double>(games.size());
    std::ostringstream o;
    o << (cheat ? "Canarino (diagnostica, mai nei dati)" : "UW forte") << " contro Rakdos " << rakdos
      << " (rollout con base_r3): seed " << first << "-" << last << ", R = " << worlds
      << " mondi per mossa, IC al 99% per scostarsi\n";
    o << "partite.jsonl " << r.md5[0] << ", ricerca.jsonl " << r.md5[1] << ", anomalie.jsonl " << r.md5[2] << "\n\n";
    o << "Anomalie (partite vere oltre il limite della pila, fuori dai dati): " << anomalies.size();
    for (std::size_t k = 0; k < anomalies.size(); ++k) {
        o << (k == 0 ? ": seed " : ", ") << anomalies[k]["seed"].get<std::uint64_t>();
    }
    o << "\n\n";
    o << compare(uw_wins(games), base_wins, cheat ? "canarino" : "UW forte", "UW euristico v1 (stesso Rakdos)") << "\n";
    o << "Decisioni di UW per partita: " << decimal(uw_decisions / n, 1) << "; cercate: "
      << decimal(static_cast<double>(searches.size()) / n, 2) << " (stima della sezione 1.7: 42 con combattimento e "
      << "mulligan)\n";
    for (const auto& [kind, count] : per_kind) {
        o << "  " << kind << ": " << decimal(static_cast<double>(count) / n, 2) << " a partita, scostamenti "
          << decimal(100.0 * static_cast<double>(dev_per_kind[kind]) / static_cast<double>(count), 2) << "%\n";
    }
    o << "Scostamenti dall'euristico: " << deviations << " su " << searches.size() << " ("
      << decimal(100.0 * static_cast<double>(deviations) / std::max<double>(1.0, static_cast<double>(searches.size())), 2)
      << "%), " << decimal(static_cast<double>(deviations) / n, 2) << " a partita\n";
    o << "Mosse composte per decisione cercata: "
      << decimal(static_cast<double>(moves) / std::max<double>(1.0, static_cast<double>(searches.size())), 2) << "\n";
    // Accordo per regola dell'ombra (sezione 6, punto 2) e gli scostamenti piu' frequenti.
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> per_rule;  // regola -> (decisioni, scostamenti)
    std::map<std::string, std::uint64_t> switches;                            // "euristico -> ricerca"
    for (const auto& s : searches) {
        auto& pr = per_rule[s["regola"].get<std::string>()];
        ++pr.first;
        if (!s["scostamento"].get<bool>()) continue;
        ++pr.second;
        const auto& mv = s["mosse"];
        ++switches[mv[s["euristica"].get<std::size_t>()]["mossa"].get<std::string>() + "  ->  " +
                   mv[s["scelta"].get<std::size_t>()]["mossa"].get<std::string>()];
    }
    std::vector<std::pair<std::uint64_t, std::string>> rules_by_count;
    for (const auto& [rule, pr] : per_rule) rules_by_count.emplace_back(pr.first, rule);
    std::sort(rules_by_count.rbegin(), rules_by_count.rend());
    o << "\nAccordo per regola dell'euristico (decisioni cercate in cui l'ombra ha deciso con quella regola; accordo = "
         "la ricerca gioca la sua scelta):\n";
    for (const auto& [count, rule] : rules_by_count) {
        const std::uint64_t dev = per_rule[rule].second;
        if (count < 20 && dev == 0) continue;
        o << "  " << rule << ": " << count << " decisioni, accordo "
          << decimal(100.0 - 100.0 * static_cast<double>(dev) / static_cast<double>(count), 1) << "% (" << dev
          << (dev == 1 ? " scostamento)\n" : " scostamenti)\n");
    }
    std::vector<std::pair<std::uint64_t, std::string>> top_switches;
    for (const auto& [sw, count] : switches) top_switches.emplace_back(count, sw);
    std::sort(top_switches.rbegin(), top_switches.rend());
    o << "\nScostamenti piu' frequenti (mossa dell'euristico -> mossa della ricerca):\n";
    for (std::size_t i = 0; i < top_switches.size() && i < 25; ++i) {
        o << "  " << top_switches[i].first << "x  " << top_switches[i].second << "\n";
    }
    o << "\nRollout: " << rollouts << " (" << decimal(static_cast<double>(rollouts) / n, 0) << " a partita), "
      << rollout_decisions << " decisioni; interrotti da un limite di capacita' del motore (pila, token): "
      << aborted_total << ", mondi scartati per questo (per tutte le mosse): " << discarded_total << " in "
      << searches_with_discards << " ricerche\n";
    o << "Tempo dei blocchi: " << decimal(secs, 0) << " s con " << threads << " thread; throughput sostenuto "
      << decimal(static_cast<double>(rollouts) / secs, 0) << " rollout/s; tempo di thread per decisione cercata circa "
      << decimal(secs * threads / std::max<double>(1.0, static_cast<double>(searches.size())), 3) << " s\n";
    std::ofstream(dir / "riepilogo.txt", std::ios::binary) << o.str();
    std::cout << "\n" << o.str();
    return 0;
}

int run_stack(const Common& c, const std::map<std::string, std::string>& opt) {
    if (!opt.count("--seed")) throw EngineError("--pila vuole --seed S");
    const std::uint64_t seed = std::stoull(opt.at("--seed"));
    StrongRun run;
    run.base = c.base;
    run.base.rakdos_agent = rakdos_variant(opt.count("--rakdos") ? opt.at("--rakdos") : "base_r3");
    run.strong.worlds = static_cast<std::uint32_t>(std::stoul(opt.count("--mondi") ? opt.at("--mondi") : "256"));
    const fs::path dir = opt.count("--out") ? fs::path(opt.at("--out")) : fs::path(DATA_DIR) / "ricerca" / "pila";
    fs::create_directories(dir);
    std::cout << "Partita forte del seed " << seed << " (R = " << run.strong.worlds << ", un thread)...\n" << std::flush;
    const std::vector<OverflowReplay> reps = replay_stack_overflows(run, seed, 8);
    std::cout << reps.size() << " rollout interrotti raccontati\n";
    for (const OverflowReplay& r : reps) {
        const fs::path p = dir / ("seed_" + std::to_string(r.seed) + "_decisione_" + std::to_string(r.decision) + "_mossa_" +
                                  std::to_string(r.move) + "_mondo_" + std::to_string(r.world) + ".txt");
        std::ofstream(p, std::ios::binary) << r.text;
        std::cout << "  " << p.string() << " (" << r.move_text << ")\n";
    }
    return 0;
}

int run_compare(const std::string& a, const std::string& b) {
    std::cout << compare(uw_wins(read_jsonl(fs::path(a) / "partite.jsonl")), uw_wins(read_jsonl(fs::path(b) / "partite.jsonl")),
                         a, b);
    return 0;
}

int run_curve(const Common& c, const std::map<std::string, std::string>& opt) {
    const std::size_t n = std::stoull(opt.count("--decisioni") ? opt.at("--decisioni") : "1000");
    const std::vector<std::uint32_t> budgets =
        parse_budgets(opt.count("--budget") ? opt.at("--budget") : "16,32,64,128,256,512,1024,2048");
    const auto reference = static_cast<std::uint32_t>(
        std::stoul(opt.count("--riferimento") ? opt.at("--riferimento")
                                              : std::to_string(8 * *std::max_element(budgets.begin(), budgets.end()))));
    const std::uint64_t first_game = std::stoull(opt.count("--da-seed") ? opt.at("--da-seed") : "1000001");
    const int threads = std::stoi(opt.count("--thread") ? opt.at("--thread") : "20");
    const fs::path dir = opt.count("--out") ? fs::path(opt.at("--out")) : fs::path(DATA_DIR) / "ricerca" / "curva";
    StrongConfig cfg;
    std::cout << "Curva: " << n << " decisioni dalle partite degli euristici dal seed " << first_game << ", budget "
              << (opt.count("--budget") ? opt.at("--budget") : std::string("16-2048")) << ", riferimento " << reference << " mondi, " << threads << " thread\n"
              << std::flush;
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<SampledDecision> decisions = sample_decisions(c.base, n, first_game);
    std::cout << "Decisioni campionate in "
              << decimal(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), 1) << " s\n";
    if (opt.count("--nuova")) fs::remove_all(dir);  // ricominciare da zero (test di fumo)
    ordered_json config;
    config["modo"] = "curva";
    config["decisioni"] = n;
    config["da_seed"] = first_game;
    config["budget"] = budgets;
    config["riferimento"] = reference;
    config["fiducia"] = cfg.confidence;
    config["codice"] = opt.count("--codice") ? opt.at("--codice") : "non indicato";
    RunSpec spec;
    spec.config = config.dump();
    spec.first = 1;
    spec.last = n;
    spec.block = std::stoull(opt.count("--blocco") ? opt.at("--blocco") : "50");
    spec.files = {"curva"};
    RunOptions options;
    options.progress = &std::cout;
    const RunResult r = run_resumable(dir, spec, [&](std::uint64_t a, std::uint64_t b) {
        std::vector<std::string> lines(static_cast<std::size_t>(b - a + 1));
        BlockProgress progress(lines.size());
        parallel_for(lines.size(), threads, [&](std::size_t i) {
            const std::uint64_t index = a + i;
            lines[i] = curve_decision_json(c.db, decisions[index - 1], index, budgets, reference, cfg) + "\n";
            progress.one_done();
        });
        std::string all;
        for (const std::string& l : lines) all += l;
        return std::vector<std::string>{all};
    }, options);
    if (!r.complete) return 0;
    std::string summary = curve_summary(read_file(dir / "curva.jsonl"));
    std::uint64_t rollouts = 0;
    for (const auto& row : read_jsonl(dir / "curva.jsonl")) {
        rollouts += row["mosse"].size() * (row["mondi"].get<std::uint64_t>() + reference);
    }
    const double secs = manifest_seconds(dir);
    summary += "\ncurva.jsonl " + r.md5[0] + "; " + std::to_string(rollouts) + " rollout in " + decimal(secs, 0) +
               " s con " + std::to_string(threads) + " thread: " + decimal(static_cast<double>(rollouts) / secs, 0) +
               " rollout/s\n";
    std::ofstream(dir / "riepilogo.txt", std::ios::binary) << summary;
    std::cout << "\n" << summary;
    return 0;
}

int run_positions(const Common& c, const std::map<std::string, std::string>& opt) {
    const std::vector<std::uint32_t> budgets = parse_budgets(opt.count("--budget") ? opt.at("--budget") : "16,32,64,128,256");
    const std::uint64_t searches = std::stoull(opt.count("--ricerche") ? opt.at("--ricerche") : "100");
    const int threads = std::stoi(opt.count("--thread") ? opt.at("--thread") : "20");
    const int replays = std::stoi(opt.count("--replay") ? opt.at("--replay") : "3");
    const std::vector<std::string> ids = opt.count("--solo") ? split(opt.at("--solo")) : test_position_ids();
    const fs::path dir = opt.count("--out") ? fs::path(opt.at("--out")) : fs::path(DATA_DIR) / "ricerca" / "posizioni";
    std::vector<TestPosition> positions;
    for (const std::string& id : ids) positions.push_back(test_position(c.db, c.base.uw, c.base.rakdos, id));
    const std::uint64_t per_position = budgets.size() * searches;
    auto item = [&](std::uint64_t index) {  // da 1: (posizione, budget, ricerca)
        const std::uint64_t i = index - 1;
        return std::tuple<std::size_t, std::size_t, std::uint64_t>{i / per_position, (i % per_position) / searches,
                                                                   i % searches + 1};
    };
    if (opt.count("--nuova")) fs::remove_all(dir);  // ricominciare da zero (test di fumo)
    ordered_json config;
    config["modo"] = "posizioni";
    config["posizioni"] = ids;
    config["budget"] = budgets;
    config["ricerche"] = searches;
    config["codice"] = opt.count("--codice") ? opt.at("--codice") : "non indicato";
    RunSpec spec;
    spec.config = config.dump();
    spec.first = 1;
    spec.last = positions.size() * per_position;
    spec.block = std::stoull(opt.count("--blocco") ? opt.at("--blocco") : "500");
    spec.files = {"posizioni"};
    RunOptions options;
    options.progress = &std::cout;
    std::cout << "Posizioni: " << ids.size() << " stati, " << budgets.size() << " budget, " << searches
              << " ricerche ciascuno (" << spec.last << " turni), " << threads << " thread\n" << std::flush;
    const RunResult r = run_resumable(dir, spec, [&](std::uint64_t a, std::uint64_t b) {
        std::vector<std::string> lines(static_cast<std::size_t>(b - a + 1));
        BlockProgress progress(lines.size());
        parallel_for(lines.size(), threads, [&](std::size_t k) {
            const auto [p, bi, seed] = item(a + k);
            StrongConfig cfg;
            cfg.worlds = budgets[bi];
            const PositionOutcome o = play_position(c.db, positions[p], &cfg, seed, false);
            ordered_json j;
            j["posizione"] = positions[p].id;
            j["mondi"] = budgets[bi];
            j["ricerca"] = seed;
            j["linea"] = o.line;
            j["giocate"] = o.signature;
            j["cercate"] = o.records.size();
            j["scostamenti"] = std::count_if(o.records.begin(), o.records.end(), [](const SearchRecord& x) { return x.deviates; });
            lines[k] = j.dump() + "\n";
            progress.one_done();
        });
        std::string all;
        for (const std::string& l : lines) all += l;
        return std::vector<std::string>{all};
    }, options);
    if (!r.complete) return 0;

    // Riepilogo: l'euristico sugli stessi seed, la quota della linea per budget, le giocate piu' frequenti, i replay.
    const std::vector<ordered_json> rows = read_jsonl(dir / "posizioni.jsonl");
    std::vector<int> heuristic_line(positions.size() * searches);
    std::vector<std::string> heuristic_sig(heuristic_line.size());
    parallel_for(heuristic_line.size(), threads, [&](std::size_t k) {
        const PositionOutcome o = play_position(c.db, positions[k / searches], nullptr, k % searches + 1, false);
        heuristic_line[k] = o.line ? 1 : 0;
        heuristic_sig[k] = o.signature;
    });
    std::ostringstream o;
    o << "Posizioni test: quota dei turni in cui UW gioca la linea indicata (la linea del turno), su " << searches
      << " ricerche con seed diversi per budget; accanto l'euristico (base_v2) sugli stessi seed.\n\n";
    o << "| Posizione | Linea contata | Euristico |";
    for (const std::uint32_t b : budgets) o << " R = " << b << " |";
    o << " Giocate piu' frequenti con R = " << budgets.back() << " |\n| --- | --- | --- |";
    for (std::size_t i = 0; i < budgets.size(); ++i) o << " --- |";
    o << " --- |\n";
    const fs::path replay_dir = dir / "replay";
    fs::create_directories(replay_dir);
    for (std::size_t p = 0; p < positions.size(); ++p) {
        const std::string& id = positions[p].id;
        int h = 0;
        for (std::uint64_t k = 0; k < searches; ++k) h += heuristic_line[p * searches + k];
        o << "| " << id << " | " << indicated_line_text(id) << " | " << decimal(100.0 * h / static_cast<double>(searches), 0) << "% |";
        std::map<std::string, int> sigs;
        std::vector<std::uint64_t> different;
        for (std::size_t bi = 0; bi < budgets.size(); ++bi) {
            int yes = 0, total = 0;
            for (const auto& row : rows) {
                if (row["posizione"] != id || row["mondi"].get<std::uint32_t>() != budgets[bi]) continue;
                ++total;
                yes += row["linea"].get<bool>() ? 1 : 0;
                if (bi + 1 == budgets.size()) {
                    ++sigs[row["giocate"].get<std::string>()];
                    if (!row["linea"].get<bool>()) different.push_back(row["ricerca"].get<std::uint64_t>());
                }
            }
            o << " " << (total > 0 ? decimal(100.0 * yes / total, 0) : "-") << "% |";
        }
        std::vector<std::pair<int, std::string>> top;
        for (const auto& [sig, count] : sigs) top.emplace_back(count, sig);
        std::sort(top.rbegin(), top.rend());
        std::string top_text;
        for (std::size_t t = 0; t < top.size() && t < 2; ++t) {
            top_text += (t > 0 ? "; " : "") + std::to_string(top[t].first) + "x " + top[t].second;
        }
        o << " " << top_text << " |\n";
        // I casi in cui la ricerca (al budget massimo) non gioca la linea: replay leggibili con le stime.
        std::sort(different.begin(), different.end());
        for (std::size_t k = 0; k < different.size() && static_cast<int>(k) < replays; ++k) {
            StrongConfig cfg;
            cfg.worlds = budgets.back();
            const PositionOutcome po = play_position(c.db, positions[p], &cfg, different[k], true);
            std::ofstream(replay_dir / ("posizione_" + id + "_R" + std::to_string(budgets.back()) + "_ricerca_" +
                                        std::to_string(different[k]) + ".txt"),
                          std::ios::binary)
                << po.replay;
        }
    }
    o << "\nposizioni.jsonl " << r.md5[0] << "; tempo dei blocchi " << decimal(manifest_seconds(dir), 0) << " s con "
      << threads << " thread\n";
    std::ofstream(dir / "riepilogo.txt", std::ios::binary) << o.str();
    std::cout << "\n" << o.str();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        std::map<std::string, std::string> opt;
        std::string mode;
        std::vector<std::string> positional;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--partite" || a == "--curva" || a == "--posizioni" || a == "--confronta" || a == "--pila") {
                mode = a;
            } else if (a == "--canarino" || a == "--nuova") {
                opt[a] = "1";
            } else if (a.rfind("--", 0) == 0) {
                if (i + 1 >= args.size()) throw EngineError("manca il valore di " + a);
                opt[a] = args[++i];
            } else {
                positional.push_back(a);
            }
        }
        if (mode == "--confronta") {
            if (positional.size() != 2) throw EngineError("--confronta DIR_A DIR_B");
            return run_compare(positional[0], positional[1]);
        }
        Common c{CardDatabase::load(CARDS_JSON), {}};
        register_all_cards(c.db);
        c.base.db = &c.db;
        c.base.uw = load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");
        c.base.rakdos = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
        if (mode == "--partite") return run_games(c, opt, opt.count("--canarino") > 0);
        if (mode == "--curva") return run_curve(c, opt);
        if (mode == "--posizioni") return run_positions(c, opt);
        if (mode == "--pila") return run_stack(c, opt);
        std::cerr << "uso: ricerca --partite | --curva | --posizioni | --confronta DIR_A DIR_B (vedi tools/ricerca_main.cpp)\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "errore: " << e.what() << "\n";
        return 1;
    }
}
