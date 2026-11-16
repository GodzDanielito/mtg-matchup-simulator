// Sonda di velocita' della ricerca (docs/ricerca.md, sezione 1). Codice di prova: misura, non cerca; non
// cambia motore ne' agenti.
//
//   sonda_ricerca --misura [--stati N] [--rollout R] [--partite G]   un core: partite, rollout, copia, profilo
//   sonda_ricerca --coppie W [--stati N]                              varianza appaiata di due mosse su W mondi
//   sonda_ricerca --sostenuto SECONDI [--finestra W] [--stati N] [--id K]   rollout in ciclo, uno per processo
//
// Per una misura su un core va fissata l'affinita' a un P-core (sui processori ibridi Windows puo' mettere il
// processo su un E-core, che e' circa due volte piu' lento).
//
// Stati di meta' partita: per i seed 1..N si gioca la partita degli euristici v1 (UW base_v2 contro
// Rakdos base_r3), se ne prende la lunghezza L e la si rigioca fino al turno max(3, (L+1)/2), alla
// prima priorita' del giocatore attivo nella prima fase principale con la pila vuota.
// Rollout: copia dello stato, determinizzazione di prova (permutazione delle definizioni delle carte
// nascoste al giocatore attivo), agenti euristici nuovi, partita fino alla fine, registro spento.

#include "all_cards.hpp"
#include "matchup.hpp"
#include "game_setup.hpp"
#include "zones.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace mtg;
using Clock = std::chrono::steady_clock;

namespace {

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

struct Sample {
    GameState state;
    std::uint64_t seed = 0;
    PlayerId me = kNoPlayer;
    int legal = 0;
};

constexpr int kKinds = static_cast<int>(DecisionKind::GameOver) + 1;

struct KindStats {
    std::array<long long, kKinds> count{}, multi{}, options{};
    std::array<std::array<long long, kKinds>, kNumPlayers> by_player_multi{};
    // Priorita' per contesto: 0 = fase principale propria con la pila vuota, 1 = pila non vuota
    // (risposta), 2 = altro (turno avversario o altri step propri con la pila vuota). Per giocatore.
    std::array<std::array<long long, 3>, kNumPlayers> prio{}, prio_options{}, prio_pass{};
};

int priority_context(const GameState& s) {
    if (!s.stack_items.empty()) return 1;
    const bool own_main = s.pending.player == s.turn.active_player &&
                          (s.turn.step == Step::Main1 || s.turn.step == Step::Main2);
    return own_main ? 0 : 2;
}

void count_decision(KindStats& st, const GameState& s, const std::vector<Action>& actions, const Action& chosen) {
    const int k = static_cast<int>(s.pending.kind);
    ++st.count[k];
    st.options[k] += static_cast<long long>(actions.size());
    if (actions.size() > 1) {
        ++st.multi[k];
        ++st.by_player_multi[s.pending.player][k];
    }
    if (s.pending.kind == DecisionKind::Priority) {
        const int c = priority_context(s);
        ++st.prio[s.pending.player][c];
        st.prio_options[s.pending.player][c] += static_cast<long long>(actions.size());
        if (chosen.type == ActionType::Pass) ++st.prio_pass[s.pending.player][c];
    }
}

Action agent_choice(UwAgent& uw, RakdosAgent& rk, const GameState& s, const std::vector<Action>& actions) {
    return s.pending.player == kMatchupUw ? uw.choose(s, actions) : rk.choose(s, actions);
}

// Determinizzazione di prova: le carte nascoste a `me` (mano avversaria non nota, grimori nelle
// posizioni non note) si scambiano l'identita' (la definizione) tra loro, per proprietario. Nessuna
// carta cambia zona; i generatori dei giocatori si rinnovano. Non e' la determinizzazione proposta
// (docs/ricerca.md, sezione 3: manca la forma canonica e i suoi test): serve solo a misurarne il costo.
void determinize_probe(GameState& s, PlayerId me, std::uint64_t seed) {
    Pcg32 rng(seed, 0x5eed5eed5eedULL);
    const auto bit = static_cast<std::uint8_t>(1u << me);
    std::array<CardId, kMaxObjects> ids{};
    std::array<CardDefId, kMaxObjects> defs{};
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        std::size_t n = 0;
        if (p != me) {
            for (const CardId id : s.players[p].hand) {
                if ((s.objects[id].known_to & bit) == 0) ids[n++] = id;
            }
        }
        for (const CardId id : s.players[p].library) {
            if ((s.objects[id].known_to & bit) == 0) ids[n++] = id;
        }
        for (std::size_t i = 0; i < n; ++i) defs[i] = s.objects[ids[i]].def;
        shuffle(std::span<CardDefId>(defs.data(), n), rng);
        for (std::size_t i = 0; i < n; ++i) s.objects[ids[i]].def = defs[i];
    }
    // Il futuro dei mescolamenti non si conosce: un generatore nuovo per ogni giocatore.
    for (PlayerId p = 0; p < kNumPlayers; ++p) s.players[p].rng = player_generator(seed, p);
}

struct RolloutResult {
    PlayerId winner = kNoPlayer;
    long long decisions = 0;
    int turns = 0;
};

RolloutResult rollout(const Sample& sample, const CardDatabase& db, const MatchupConfig& cfg, std::uint64_t rseed,
                      KindStats* stats = nullptr) {
    GameState s = sample.state;
    determinize_probe(s, sample.me, rseed);
    UwAgent uw(db, rseed, cfg.uw_agent);
    RakdosAgent rk(db, rseed, cfg.rakdos_agent);
    RolloutResult r;
    const int start_turn = s.turn.turn_number;
    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= cfg.turn_limit) {
        const std::vector<Action> actions = legal_actions(s, db);
        const Action a = agent_choice(uw, rk, s, actions);
        if (stats != nullptr) count_decision(*stats, s, actions, a);
        apply(s, db, a, nullptr);
        ++r.decisions;
    }
    r.turns = s.turn.turn_number - start_turn;
    if (s.pending.kind == DecisionKind::GameOver) r.winner = s.turn.winner;
    return r;
}

// Partita completa degli euristici, registro spento. Se `stop_turn` > 0 si ferma al punto di
// campionamento di quel turno e lo restituisce in `out`.
int play_game(const CardDatabase& db, const MatchupConfig& cfg, std::uint64_t seed, int stop_turn, Sample* out,
              KindStats* stats, long long* decisions) {
    GameState s = create_game(db, cfg.uw, cfg.rakdos, seed);
    start_game(s, db, StartOptions{seed % 2 == 1 ? kMatchupUw : kMatchupRakdos, true}, nullptr);
    UwAgent uw(db, seed, cfg.uw_agent);
    RakdosAgent rk(db, seed, cfg.rakdos_agent);
    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= cfg.turn_limit) {
        if (stop_turn > 0 && s.turn.turn_number >= stop_turn && s.pending.kind == DecisionKind::Priority &&
            s.turn.step == Step::Main1 && s.stack_items.empty() && s.pending.player == s.turn.active_player) {
            out->state = s;
            out->seed = seed;
            out->me = s.turn.active_player;
            out->legal = static_cast<int>(legal_actions(s, db).size());
            return 1;
        }
        const std::vector<Action> actions = legal_actions(s, db);
        const Action a = agent_choice(uw, rk, s, actions);
        if (stats != nullptr) count_decision(*stats, s, actions, a);
        apply(s, db, a, nullptr);
        if (decisions != nullptr) ++*decisions;
    }
    return stop_turn > 0 ? 0 : s.turn.turn_number;
}

std::vector<Sample> build_samples(const CardDatabase& db, const MatchupConfig& cfg, int n) {
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t seed = 1; static_cast<int>(samples.size()) < n; ++seed) {
        const int length = play_game(db, cfg, seed, 0, nullptr, nullptr, nullptr);
        const int mid = std::max(3, (length + 1) / 2);
        Sample sample;
        if (play_game(db, cfg, seed, mid, &sample, nullptr, nullptr) == 1) samples.push_back(sample);
    }
    return samples;
}

const char* kind_name(int k) {
    static const char* names[] = {"None", "Mulligan", "BottomCards", "Priority", "PayLifeOrTapped", "SearchLibrary",
                                  "Surveil", "DiscardToHandSize", "LegendRule", "ChooseTarget", "PayMana",
                                  "ChooseFromHand", "DeclareAttacker", "DeclareBlockers", "DivideDamage",
                                  "OrderTriggers", "ExileFromGraveyard", "DiscardCard", "SacrificeForCost",
                                  "SacrificeForEffect", "PayOptionalCost", "ChooseMode", "ChooseX",
                                  "ExileFromHandForCost", "CastFromExile", "ChooseReplicate", "GameOver"};
    return names[k];
}

void print_stats(const char* title, const KindStats& st, long long units) {
    std::printf("\n%s (per %lld unita')\n", title, units);
    std::printf("%-22s %10s %10s %10s %10s %10s\n", "decisione", "media", "con>=2", "opz.medie", "UW>=2", "Rak>=2");
    long long tot = 0, tot_multi = 0;
    for (int k = 0; k < kKinds; ++k) {
        if (st.count[k] == 0) continue;
        tot += st.count[k];
        tot_multi += st.multi[k];
        std::printf("%-22s %10.2f %10.2f %10.2f %10.2f %10.2f\n", kind_name(k),
                    static_cast<double>(st.count[k]) / static_cast<double>(units),
                    static_cast<double>(st.multi[k]) / static_cast<double>(units),
                    static_cast<double>(st.options[k]) / static_cast<double>(st.count[k]),
                    static_cast<double>(st.by_player_multi[0][k]) / static_cast<double>(units),
                    static_cast<double>(st.by_player_multi[1][k]) / static_cast<double>(units));
    }
    std::printf("%-22s %10.2f %10.2f\n", "totale", static_cast<double>(tot) / static_cast<double>(units),
                static_cast<double>(tot_multi) / static_cast<double>(units));
    const char* ctx[] = {"fase principale propria, pila vuota", "pila non vuota (risposta)", "altro, pila vuota"};
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        for (int c = 0; c < 3; ++c) {
            const long long n = st.prio[p][c];
            if (n == 0) continue;
            std::printf("  priorita' %s, %-38s %8.2f per unita', opzioni medie %5.2f, passa %5.1f%%\n",
                        p == kMatchupUw ? "UW    " : "Rakdos", ctx[c], static_cast<double>(n) / static_cast<double>(units),
                        static_cast<double>(st.prio_options[p][c]) / static_cast<double>(n),
                        100.0 * static_cast<double>(st.prio_pass[p][c]) / static_cast<double>(n));
        }
    }
}

// Esito per il giocatore attivo dello stato dopo l'azione `x` alla radice, nel mondo `ws`.
int outcome_after(const Sample& sample, const CardDatabase& db, const MatchupConfig& cfg, const Action& x,
                  std::uint64_t ws) {
    GameState s = sample.state;
    determinize_probe(s, sample.me, ws);
    UwAgent uw(db, ws, cfg.uw_agent);
    RakdosAgent rk(db, ws, cfg.rakdos_agent);
    apply(s, db, x, nullptr);
    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= cfg.turn_limit) {
        const std::vector<Action> actions = legal_actions(s, db);
        apply(s, db, agent_choice(uw, rk, s, actions), nullptr);
    }
    return s.pending.kind == DecisionKind::GameOver && s.turn.winner == sample.me ? 1 : 0;
}

// Varianza della differenza tra due mosse alla radice: stessi mondi (numeri casuali comuni) contro
// mondi indipendenti. a = la scelta dell'euristico, b = un'altra azione legale a caso.
void paired_measure(const std::vector<Sample>& samples, const CardDatabase& db, const MatchupConfig& cfg, int worlds) {
    double sum_a = 0, sum_b = 0, sum_aa = 0, sum_bb = 0, sum_ab = 0, sum_bi = 0, sum_bibi = 0;
    long long n = 0, states = 0, identical_states = 0, a_better = 0, b_better = 0, failures = 0;
    double sq_paired = 0, sq_indep = 0;
    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const Sample& smp = samples[i];
        const std::vector<Action> actions = legal_actions(smp.state, db);
        if (actions.size() < 2) continue;
        UwAgent uw(db, smp.seed, cfg.uw_agent);
        RakdosAgent rk(db, smp.seed, cfg.rakdos_agent);
        const Action a = agent_choice(uw, rk, smp.state, actions);
        Pcg32 pick(i, 0xb0b);
        Action b = a;
        while (b == a) b = actions[pick.bounded(static_cast<std::uint32_t>(actions.size()))];
        try {
            long long wa = 0, wb = 0;
            bool all_same = true;
            for (int w = 0; w < worlds; ++w) {
                const std::uint64_t ws = 0x1234567ULL * (i + 1) + static_cast<std::uint64_t>(w);
                const int oa = outcome_after(smp, db, cfg, a, ws);
                const int ob = outcome_after(smp, db, cfg, b, ws);
                const int obi = outcome_after(smp, db, cfg, b, ws ^ 0xabcdef0123ULL);
                sum_a += oa; sum_b += ob; sum_aa += oa * oa; sum_bb += ob * ob; sum_ab += oa * ob;
                sum_bi += obi; sum_bibi += obi * obi;
                sq_paired += (oa - ob) * (oa - ob);
                sq_indep += (oa - obi) * (oa - obi);
                wa += oa; wb += ob;
                if (oa != ob) all_same = false;
                ++n;
            }
            ++states;
            if (all_same) ++identical_states;
            if (wa > wb) ++a_better;
            if (wb > wa) ++b_better;
        } catch (const std::exception& e) {
            if (failures++ < 5) std::printf("errore: %s\n", e.what());
        }
    }
    const double dn = static_cast<double>(n);
    const double ma = sum_a / dn, mb = sum_b / dn;
    const double va = sum_aa / dn - ma * ma, vb = sum_bb / dn - mb * mb;
    const double cov = sum_ab / dn - ma * mb;
    const double rho = cov / std::sqrt(va * vb);
    std::printf("\n[coppie] %lld stati, %d mondi per mossa, %lld coppie in %.1f s; errori %lld\n", states, worlds, n,
                seconds_since(t0), failures);
    std::printf("[coppie] vittorie: scelta dell'euristico %.4f, altra mossa %.4f (mondi comuni), %.4f (mondi indipendenti)\n",
                ma, mb, sum_bi / dn);
    std::printf("[coppie] correlazione tra le due mosse negli stessi mondi: %.3f\n", rho);
    std::printf("[coppie] E[(a-b)^2]: mondi comuni %.4f, indipendenti %.4f, rapporto %.3f\n", sq_paired / dn,
                sq_indep / dn, sq_paired / sq_indep);
    std::printf("[coppie] stati con esiti identici in tutti i mondi: %lld (%.1f%%); euristico meglio %lld, altra meglio %lld\n",
                identical_states, 100.0 * static_cast<double>(identical_states) / static_cast<double>(states), a_better,
                b_better);
}

int run_measure(const CardDatabase& db, const MatchupConfig& cfg, int n_states, int per_state, int n_games) {
    std::printf("sizeof(GameState) = %zu byte\n", sizeof(GameState));
    std::printf("sizeof(UwAgent) = %zu, sizeof(RakdosAgent) = %zu byte\n", sizeof(UwAgent), sizeof(RakdosAgent));

    // 1. Partite complete da inizio partita, registro spento.
    {
        KindStats st;
        long long decisions = 0, turns = 0;
        const auto t0 = Clock::now();
        for (int g = 1; g <= n_games; ++g) {
            turns += play_game(db, cfg, static_cast<std::uint64_t>(g), 0, nullptr, &st, &decisions);
        }
        const double t = seconds_since(t0);
        std::printf("\n[partite da inizio] %d partite in %.3f s: %.1f partite/s, %.1f us a partita; "
                    "decisioni %.1f a partita, %.3f us a decisione; turni medi %.2f\n",
                    n_games, t, n_games / t, 1e6 * t / n_games, static_cast<double>(decisions) / n_games,
                    1e6 * t / static_cast<double>(decisions), static_cast<double>(turns) / n_games);
        print_stats("Decisioni per partita completa", st, n_games);
    }

    // 2. Stati di meta' partita.
    const auto t_build = Clock::now();
    const std::vector<Sample> samples = build_samples(db, cfg, n_states);
    std::printf("\n[stati] %zu stati di meta' partita in %.2f s\n", samples.size(), seconds_since(t_build));
    {
        long long legal = 0, uw_me = 0, turn = 0;
        int max_legal = 0;
        for (const Sample& smp : samples) {
            legal += smp.legal;
            max_legal = std::max(max_legal, smp.legal);
            uw_me += smp.me == kMatchupUw ? 1 : 0;
            turn += smp.state.turn.turn_number;
        }
        std::printf("[stati] UW attivo in %lld; turno medio %.2f; azioni legali medie %.2f (max %d)\n", uw_me,
                    static_cast<double>(turn) / static_cast<double>(samples.size()),
                    static_cast<double>(legal) / static_cast<double>(samples.size()), max_legal);
    }

    // 3. Costo della copia dello stato.
    {
        constexpr int kCopies = 2'000'000;
        std::vector<GameState> dst(2);
        unsigned sink = 0;
        const auto t0 = Clock::now();
        for (int i = 0; i < kCopies; ++i) {
            GameState& d = dst[static_cast<std::size_t>(i & 1)];
            d = samples[static_cast<std::size_t>(i) % samples.size()].state;
            sink += d.turn.turn_number;
        }
        const double t = seconds_since(t0);
        std::printf("\n[copia] %d copie in %.3f s: %.1f ns a copia, %.2f GB/s (controllo %u)\n", kCopies, t,
                    1e9 * t / kCopies, static_cast<double>(sizeof(GameState)) * kCopies / t / 1e9, sink);
    }

    // 4. Costo della determinizzazione di prova e della costruzione degli agenti.
    {
        constexpr int kN = 200'000;
        GameState d;
        unsigned sink = 0;
        auto t0 = Clock::now();
        for (int i = 0; i < kN; ++i) {
            d = samples[static_cast<std::size_t>(i) % samples.size()].state;
            determinize_probe(d, samples[static_cast<std::size_t>(i) % samples.size()].me, static_cast<std::uint64_t>(i));
            sink += d.objects[static_cast<std::size_t>(i) % 120].def;
        }
        double t = seconds_since(t0);
        std::printf("[determinizzazione] %.1f ns (copia compresa) (controllo %u)\n", 1e9 * t / kN, sink);
        // Gli invarianti dopo la determinizzazione (il cimitero non cambia).
        int bad = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            d = samples[i].state;
            determinize_probe(d, samples[i].me, i);
            if (!check_invariants(d, db).empty()) ++bad;
        }
        std::printf("[determinizzazione] stati con invarianti violati: %d su %zu\n", bad, samples.size());

        constexpr int kAgents = 100'000;
        t0 = Clock::now();
        for (int i = 0; i < kAgents; ++i) {
            UwAgent uw(db, static_cast<std::uint64_t>(i), cfg.uw_agent);
            RakdosAgent rk(db, static_cast<std::uint64_t>(i), cfg.rakdos_agent);
            sink += static_cast<unsigned>(uw.last_rule().size() + rk.last_rule().size());
        }
        t = seconds_since(t0);
        std::printf("[agenti] costruzione dei due agenti: %.1f ns (controllo %u)\n", 1e9 * t / kAgents, sink);
    }

    // 5. Rollout da meta' partita, un core.
    {
        KindStats st;
        long long decisions = 0, turns = 0, uw_wins = 0, rollouts = 0, failures = 0;
        const auto t0 = Clock::now();
        for (int r = 0; r < per_state; ++r) {
            for (std::size_t i = 0; i < samples.size(); ++i) {
                try {
                    const RolloutResult res = rollout(samples[i], db, cfg, 1'000'003ULL * (r + 1) + i, &st);
                    decisions += res.decisions;
                    turns += res.turns;
                    uw_wins += res.winner == kMatchupUw ? 1 : 0;
                } catch (const std::exception& e) {
                    if (failures++ < 5) std::printf("errore nel rollout: %s\n", e.what());
                }
                ++rollouts;
            }
        }
        const double t = seconds_since(t0);
        std::printf("\n[rollout] %lld rollout in %.3f s: %.1f rollout/s su un core, %.1f us a rollout; "
                    "decisioni %.1f a rollout (%.3f us a decisione); turni restanti %.2f; UW vince %.2f%%; errori %lld\n",
                    rollouts, t, static_cast<double>(rollouts) / t, 1e6 * t / static_cast<double>(rollouts),
                    static_cast<double>(decisions) / static_cast<double>(rollouts),
                    1e6 * t / static_cast<double>(decisions), static_cast<double>(turns) / static_cast<double>(rollouts),
                    100.0 * static_cast<double>(uw_wins) / static_cast<double>(rollouts), failures);
        print_stats("Decisioni per rollout da meta' partita", st, rollouts);
    }

    // 6. Dove va il tempo: legal_actions, scelta dell'agente, apply (su una parte dei rollout).
    {
        double t_legal = 0, t_choose = 0, t_apply = 0;
        long long n = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            for (int r = 0; r < 3; ++r) {
                GameState s = samples[i].state;
                const std::uint64_t rseed = 77ULL * (r + 1) + i;
                determinize_probe(s, samples[i].me, rseed);
                UwAgent uw(db, rseed, cfg.uw_agent);
                RakdosAgent rk(db, rseed, cfg.rakdos_agent);
                while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= cfg.turn_limit) {
                    auto a0 = Clock::now();
                    const std::vector<Action> actions = legal_actions(s, db);
                    auto a1 = Clock::now();
                    const Action a = agent_choice(uw, rk, s, actions);
                    auto a2 = Clock::now();
                    apply(s, db, a, nullptr);
                    auto a3 = Clock::now();
                    t_legal += std::chrono::duration<double>(a1 - a0).count();
                    t_choose += std::chrono::duration<double>(a2 - a1).count();
                    t_apply += std::chrono::duration<double>(a3 - a2).count();
                    ++n;
                }
            }
        }
        const double tot = t_legal + t_choose + t_apply;
        std::printf("\n[profilo] %lld decisioni: legal_actions %.1f%%, scelta degli agenti %.1f%%, apply %.1f%% "
                    "(%.3f us a decisione con i cronometri)\n",
                    n, 100 * t_legal / tot, 100 * t_choose / tot, 100 * t_apply / tot, 1e6 * tot / static_cast<double>(n));
    }
    return 0;
}

int run_sustained(const CardDatabase& db, const MatchupConfig& cfg, double duration, double window, int n_states,
                  int id) {
    const std::vector<Sample> samples = build_samples(db, cfg, n_states);
    std::printf("pronto %d stati\n", static_cast<int>(samples.size()));
    std::fflush(stdout);
    const auto t0 = Clock::now();
    double next = window;
    long long in_window = 0, total = 0;
    std::uint64_t k = static_cast<std::uint64_t>(id) * 1'000'000'007ULL;
    for (;;) {
        const Sample& smp = samples[k % samples.size()];
        (void)rollout(smp, db, cfg, k++);
        ++in_window;
        ++total;
        if ((total & 7) == 0) {
            const double t = seconds_since(t0);
            if (t >= next) {
                std::printf("%.1f,%lld\n", t, in_window);
                std::fflush(stdout);
                in_window = 0;
                next += window;
                if (t >= duration) break;
            }
        }
    }
    std::printf("totale,%lld,%.3f\n", total, seconds_since(t0));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool measure = false;
    double sustained = 0, window = 10;
    int n_states = 1000, per_state = 10, n_games = 10000, id = 0, pairs = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--misura") measure = true;
        else if (arg == "--sostenuto") sustained = std::stod(next());
        else if (arg == "--finestra") window = std::stod(next());
        else if (arg == "--stati") n_states = std::stoi(next());
        else if (arg == "--rollout") per_state = std::stoi(next());
        else if (arg == "--partite") n_games = std::stoi(next());
        else if (arg == "--id") id = std::stoi(next());
        else if (arg == "--coppie") pairs = std::stoi(next());
        else {
            std::cerr << "opzione sconosciuta: " << arg << "\n";
            return 2;
        }
    }
    CardDatabase db = CardDatabase::load(CARDS_JSON);
    register_all_cards(db);
    MatchupConfig cfg;
    cfg.db = &db;
    cfg.uw = load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");
    cfg.rakdos = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    if (pairs > 0) {
        paired_measure(build_samples(db, cfg, n_states), db, cfg, pairs);
        return 0;
    }
    if (measure) return run_measure(db, cfg, n_states, per_state, n_games);
    if (sustained > 0) return run_sustained(db, cfg, sustained, window, n_states, id);
    std::cerr << "serve --misura o --sostenuto\n";
    return 2;
}
