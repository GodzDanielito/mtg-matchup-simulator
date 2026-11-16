// Record compatto per le metriche stile 17lands (docs/metriche.md, sezioni 2, 10 e 11): gioca il matchup con
// gli agenti euristici v1 (UW base_v2 contro Rakdos base_r3) e scrive una riga JSON per partita.
//
//   metriche [--games N] [--first-seed S] [--limit T] [--out DIR] [--sostituisci "Carta:Terra"]...
//            [--senza-record]
//
// Scrive in DIR (predefinita data/metriche/v1):
//   record.jsonl    il record compatto, una riga per partita (tools/metriche.hpp);
//   partite.jsonl   la stessa riga per partita di matchup.exe: deve avere gli MD5 di docs/metriche.md, sezione 1;
//   riepilogo.txt   win rate e mani tenute (come matchup.exe), i controlli di coerenza di ogni record, i codici
//                   degli eventi, il turno del delirium con le due convenzioni, la dimensione del record.
// Esce con il codice 1 se un record non supera i controlli.
//
// Ablazione (docs/metriche.md, sezione 3, aggiunta A): --sostituisci "Carta:Terra" sostituisce UNA copia della
// carta con la terra base nella stessa posizione della lista (replace_one_copy), prima di giocare; si puo'
// ripetere (ablazione di coppia). La carta si indica con il nome Oracle o con un prefisso unico. --senza-record
// non costruisce ne' scrive il record: solo partite.jsonl e il riepilogo (per l'ablazione basta l'esito).
//
// Mulligan contestuale (sezione 9.2): --mulligan-forzato uw|rakdos gioca due partite per seed, K (prima decisione
// di mulligan del giocatore forzata a "tieni") e M (a "mulligan"), e scrive in DIR (predefinita
// data/metriche/v1/mulligan/<giocatore>) mulligan.jsonl (una riga per seed: prima mano, scelta dell'agente a 7 e a 6
// carte, esiti di K e M, per UW le sonde M1, M1-R e M3 e le partite delle varianti che decidono diversamente),
// record_K.jsonl e record_M.jsonl (senza --senza-record) e riepilogo.txt. Esce con 1 se la partita di una
// variante che cambia la decisione a 7 carte non e' identica a K o M.

#include "all_cards.hpp"
#include "matchup.hpp"
#include "metriche.hpp"

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

struct Side {
    long long games = 0, uw = 0, rakdos = 0;
    std::map<int, long long> uw_hand, rakdos_hand;
};

double pct(long long part, long long whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

void print_side(std::ostream& o, const std::string& title, const Side& s) {
    o << title << ": " << s.games << " partite, UW vince " << s.uw << " (" << pct(s.uw, s.games) << "%), Rakdos "
      << s.rakdos << " (" << pct(s.rakdos, s.games) << "%)\n";
    o << "  mano tenuta UW:";
    for (const auto& [size, n] : s.uw_hand) o << " " << size << " carte " << pct(n, s.games) << "%;";
    o << "\n  mano tenuta Rakdos:";
    for (const auto& [size, n] : s.rakdos_hand) o << " " << size << " carte " << pct(n, s.games) << "%;";
    o << "\n";
}

// Categoria del turno del delirium: 1..5, 6 = "6 o piu'", 0 = mai.
int category(int rakdos_turn) { return rakdos_turn >= 6 ? 6 : rakdos_turn; }

// --mulligan-forzato: due partite per seed (K e M) e le sonde; vedi il commento in testa.
int run_forced_mulligan(const CardDatabase& db, const MatchupConfig& config, PlayerId player, std::uint64_t first_seed,
                        long long games, bool with_record, const std::filesystem::path& out_dir) {
    std::filesystem::create_directories(out_dir);
    std::ofstream pairs_out(out_dir / "mulligan.jsonl", std::ios::binary);
    std::ofstream keep_out, mull_out;
    if (with_record) {
        keep_out.open(out_dir / "record_K.jsonl", std::ios::binary);
        mull_out.open(out_dir / "record_M.jsonl", std::ios::binary);
    }
    long long keep_wins = 0, mull_wins = 0, agent_keeps = 0, not_equal = 0;
    std::map<std::string, std::array<long long, 2>> changed;  // variante -> [a 7 carte, a 6 carte]
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        const MulliganPair p = play_mulligan_pair(config, seed, player, with_record);
        pairs_out << mulligan_pair_json(db, p) << '\n';
        if (with_record) {
            keep_out << compact_record_json(db, p.keep_record) << '\n';
            mull_out << compact_record_json(db, p.mull_record) << '\n';
        }
        keep_wins += p.keep_game.winner == player;
        mull_wins += p.mull_game.winner == player;
        agent_keeps += p.agent7.keep;
        for (const MulliganVariantGame& v : p.variants) {
            ++changed[v.name][v.changed_at == 7 ? 0 : 1];
            if (v.changed_at == 7 && !v.equals_pair_game) ++not_equal;
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ostringstream report;
    report << std::fixed << std::setprecision(2);
    report << "Mulligan forzato (docs/metriche.md, sezione 9.2) - " << (player == kMatchupUw ? "UW" : "Rakdos")
           << ", euristici v1\n";
    report << games << " seed, da " << first_seed << " a " << first_seed + static_cast<std::uint64_t>(games) - 1
           << "; tempo " << std::setprecision(1) << seconds << " s\n" << std::setprecision(2);
    report << "Prima mano tenuta dall'agente: " << agent_keeps << " (" << pct(agent_keeps, games) << "%)\n";
    report << "K (tieni): vince " << keep_wins << " (" << pct(keep_wins, games) << "%); M (mulligan): vince "
           << mull_wins << " (" << pct(mull_wins, games) << "%); K - M " << pct(keep_wins - mull_wins, games) << " punti\n";
    for (const auto& [name, c] : changed) {
        report << "Variante " << name << ": decide diversamente da D-2 a 7 carte in " << c[0] << " seed, a 6 carte in "
               << c[1] << "\n";
    }
    report << "Partite di una variante a 7 carte diverse da K o M: " << not_equal << "\n";
    std::cout << report.str();
    std::ofstream(out_dir / "riepilogo.txt") << report.str();
    return not_equal == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    long long games = 100000;
    std::uint64_t first_seed = 1;
    int limit = 60;
    std::filesystem::path out_dir = std::filesystem::path(DATA_DIR) / "metriche" / "v1";
    std::vector<Substitution> substitutions;
    bool with_record = true;
    std::string forced_player;
    bool out_given = false;
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
        else if (arg == "--out") {
            out_dir = next();
            out_given = true;
        } else if (arg == "--senza-record") with_record = false;
        else if (arg == "--mulligan-forzato") {
            forced_player = next();
            if (forced_player != "uw" && forced_player != "rakdos") {
                std::cerr << "--mulligan-forzato vuole uw o rakdos\n";
                return 2;
            }
        }
        else if (arg == "--sostituisci") {
            try {
                substitutions.push_back(parse_substitution(next()));
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 2;
            }
        } else {
            std::cerr << "opzione sconosciuta: " << arg << "\n";
            return 2;
        }
    }

    CardDatabase db = CardDatabase::load(CARDS_JSON);
    register_all_cards(db);
    MatchupConfig config;  // euristici v1: le opzioni predefinite dei due agenti
    config.db = &db;
    config.uw = load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");
    config.rakdos = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    config.turn_limit = limit;
    std::vector<AppliedSubstitution> applied;
    try {
        for (const Substitution& s : substitutions) applied.push_back(apply_substitution(config, s));
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }

    if (!forced_player.empty()) {
        if (!out_given) out_dir = std::filesystem::path(DATA_DIR) / "metriche" / "v1" / "mulligan" / forced_player;
        return run_forced_mulligan(db, config, forced_player == "uw" ? kMatchupUw : kMatchupRakdos, first_seed, games,
                                   with_record, out_dir);
    }

    std::filesystem::create_directories(out_dir);
    std::ofstream record_out;
    if (with_record) record_out.open(out_dir / "record.jsonl", std::ios::binary);
    std::ofstream games_out(out_dir / "partite.jsonl");

    Side all, play, draw;
    long long bad_records = 0;
    std::vector<std::string> first_problems;
    std::array<std::map<char, long long>, 2> codes;
    // Turno del delirium: [convenzione][categoria] -> partite. 0 = goldfish (adottata), 1 = "il turno di Rakdos
    // appena giocato" (proposta del 15/11).
    std::array<std::map<int, long long>, 2> delirium_turns;
    long long delirium_in_uw_turn = 0, delirium_reached = 0, delirium_lost_games = 0;
    std::uintmax_t record_bytes = 0;
    const auto count_game = [&](const MatchupGame& g) {
        for (Side* side : {&all, g.uw_on_play ? &play : &draw}) {
            ++side->games;
            if (g.winner == kMatchupUw) ++side->uw;
            if (g.winner == kMatchupRakdos) ++side->rakdos;
            ++side->uw_hand[7 - g.uw_mulligans];
            ++side->rakdos_hand[7 - g.rakdos_mulligans];
        }
    };
    const auto start = std::chrono::steady_clock::now();
    for (long long n = 0; n < games; ++n) {
        const std::uint64_t seed = first_seed + static_cast<std::uint64_t>(n);
        if (!with_record) {
            const MatchupGame g = play_matchup(config, seed);
            games_out << matchup_game_json(g) << '\n';
            count_game(g);
            continue;
        }
        GameLog log;
        const MatchupGame g = play_matchup(config, seed, &log);
        const CompactRecord r = build_compact_record(db, g, log);
        const std::string line = compact_record_json(db, r);
        record_out << line << '\n';
        record_bytes += line.size() + 1;
        games_out << matchup_game_json(g) << '\n';

        const auto problems = check_compact_record(db, r, config.uw, config.rakdos);
        if (!problems.empty()) {
            ++bad_records;
            for (const auto& p : problems) {
                if (first_problems.size() < 20) first_problems.push_back(p);
            }
        }
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            for (const CopyRecord& c : r.players[p].cards) {
                for (const CopyEvent& e : c.events) ++codes[p][e.code];
            }
        }
        if (r.delirium.reached) {
            ++delirium_reached;
            if (r.delirium.lost > 0) ++delirium_lost_games;
            const bool rakdos_active = (r.delirium.turn % 2 == 1) == !g.uw_on_play;
            if (!rakdos_active) ++delirium_in_uw_turn;
            ++delirium_turns[0][category(r.delirium.rakdos_turn)];
            ++delirium_turns[1][category(rakdos_active ? r.delirium.rakdos_turn : r.delirium.rakdos_turn - 1)];
        } else {
            ++delirium_turns[0][0];
            ++delirium_turns[1][0];
        }
        count_game(g);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::ostringstream report;
    report << std::fixed << std::setprecision(2);
    report << "Record compatto per le metriche (docs/metriche.md, sezione 2) - euristici v1\n";
    report << games << " partite, seed da " << first_seed << " a " << first_seed + static_cast<std::uint64_t>(games) - 1
           << ", limite " << limit << " turni della partita\n";
    for (const AppliedSubstitution& a : applied) {
        report << "Sostituzione (ablazione): una copia di " << a.card << " (" << (a.player == kMatchupUw ? "UW" : "Rakdos")
               << ", posizione " << a.position << " nella lista) -> " << a.land << "\n";
    }
    if (!with_record) report << "Record non costruito (--senza-record): i controlli e i codici qui sotto sono vuoti\n";
    report << "Tempo: " << std::setprecision(1) << seconds << " s (" << static_cast<double>(games) / seconds
           << " partite/s)\n";
    report << "Record: " << record_bytes << " byte, " << std::setprecision(0)
           << (games == 0 ? 0.0 : static_cast<double>(record_bytes) / static_cast<double>(games)) << " byte a partita\n\n";
    report << std::setprecision(2);
    print_side(report, "Tutte", all);
    print_side(report, "UW in play", play);
    print_side(report, "UW in draw", draw);

    report << "\nControlli di coerenza (sezione 11): record con problemi " << bad_records << " su " << games << "\n";
    for (const auto& p : first_problems) report << "  " << p << "\n";

    report << "\nCodici degli eventi (copie):\n";
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        report << (p == kMatchupUw ? "  UW    " : "  Rakdos");
        for (const auto& [code, n] : codes[p]) report << "  " << code << " " << n;
        report << "\n";
    }

    report << "\nDelirium di Rakdos: raggiunto in " << delirium_reached << " partite (" << pct(delirium_reached, games)
           << "%); nel turno di UW " << delirium_in_uw_turn << " (" << pct(delirium_in_uw_turn, delirium_reached)
           << "% dei raggiunti); perso almeno una volta in " << delirium_lost_games << "\n";
    report << "Turno di Rakdos del delirium   goldfish (adottata)   turno appena giocato\n";
    for (int c = 1; c <= 6; ++c) {
        report << "  " << (c == 6 ? "6+" : std::to_string(c) + " ") << "                           " << std::setw(8)
               << delirium_turns[0][c] << " (" << std::setw(6) << pct(delirium_turns[0][c], games) << "%)   " << std::setw(8)
               << delirium_turns[1][c] << " (" << std::setw(6) << pct(delirium_turns[1][c], games) << "%)\n";
    }
    // Con l'altra convenzione il delirium raggiunto nel turno di UW prima del primo turno di Rakdos darebbe il turno
    // 0 e finirebbe qui: le due righe "mai" coincidono solo se non succede.
    report << "  mai                          " << std::setw(8) << delirium_turns[0][0] << " (" << std::setw(6)
           << pct(delirium_turns[0][0], games) << "%)   " << std::setw(8) << delirium_turns[1][0] << " (" << std::setw(6)
           << pct(delirium_turns[1][0], games) << "%)\n";

    std::cout << report.str();
    std::ofstream(out_dir / "riepilogo.txt") << report.str();
    return bad_records == 0 ? 0 : 1;
}
