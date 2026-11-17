#include "forte.hpp"

#include "esecuzioni.hpp"
#include "game_setup.hpp"
#include "player_view.hpp"
#include "rakdos_agent.hpp"
#include "ricerca_piatta.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace mtg {

namespace {

using nlohmann::ordered_json;

// Sale dei generatori dei mondi del canarino.
constexpr std::uint64_t kCheatSalt = 0x8cb92ba72f3d8dd7ULL;
// Seed della ricerca di riferimento della curva: indipendente da quella con il budget massimo.
constexpr std::uint64_t kReferenceSalt = 0x3c6ef372fe94f82bULL;
constexpr std::uint32_t kShadowWorld = 0xFFFFFFFFu;
constexpr PlayerId kUw = kMatchupUw;
constexpr PlayerId kRakdos = kMatchupRakdos;

// Il canarino: la ricerca del giocatore forte con i mondi presi dallo stato vero.
class CheatingSeat final : public Seat {
public:
    CheatingSeat(const CardDatabase& db, std::uint64_t seed, const StrongConfig& config,
                 std::vector<SearchRecord>* records)
        : player_(db, seed, config, records) {}
    Action choose(const GameState& s, std::span<const Action> legal) override {
        const Action a = player_.decide([&s](std::uint64_t w) { return cheating_world(s, w); }, legal,
                                        [&s](CardId c) { return s.objects[c].def; });
        ++counts_[std::string(player_.last_rule())];
        return a;
    }
    [[nodiscard]] std::string_view last_rule() const override { return player_.last_rule(); }
    [[nodiscard]] bool last_was_default() const override { return false; }
    void add_rule_counts(std::map<std::string, std::uint64_t>& out) const override {
        for (const auto& [rule, n] : counts_) out[rule] += n;
    }

private:
    UwSearchPlayer player_;
    std::map<std::string, std::uint64_t> counts_;
};

std::string decimal(double x, int digits, bool sign = false) {
    char buf[64];
    std::snprintf(buf, sizeof buf, sign ? "%+.*f" : "%.*f", digits, x);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string_view step_name(Step s) {
    switch (s) {
        case Step::PreGame: return "prima della partita";
        case Step::Untap: return "stap";
        case Step::Upkeep: return "mantenimento";
        case Step::Draw: return "acquisizione";
        case Step::Main1: return "fase principale 1";
        case Step::BeginCombat: return "inizio del combattimento";
        case Step::DeclareAttackers: return "attaccanti";
        case Step::DeclareBlockers: return "bloccanti";
        case Step::FirstStrikeDamage: return "danno da attacco improvviso";
        case Step::CombatDamage: return "danno";
        case Step::EndCombat: return "fine del combattimento";
        case Step::Main2: return "fase principale 2";
        case Step::End: return "sottofase finale";
        case Step::Cleanup: return "cancellazione";
    }
    return "?";
}

std::string names_in(const GameState& s, const CardDatabase& db, const PlayerZone& zone) {
    std::string out;
    for (const CardId id : zone) out += (out.empty() ? "" : ", ") + db.get(s.objects[id].def).name;
    return out.empty() ? "niente" : out;
}

std::string permanents_of(const GameState& s, const CardDatabase& db, PlayerId p) {
    std::string out;
    for (const CardId id : s.battlefield) {
        if (s.objects[id].controller == p) out += (out.empty() ? "" : ", ") + effective_def(s, db, id).name;
    }
    return out.empty() ? "niente" : out;
}

std::string exiled_of(const GameState& s, const CardDatabase& db, PlayerId owner) {
    std::string out;
    for (const CardId id : s.exile) {
        if (s.objects[id].owner == owner) out += (out.empty() ? "" : ", ") + db.get(s.objects[id].def).name;
    }
    return out.empty() ? "niente" : out;
}

// Le decisioni che il racconto mostra: le giocate, le scelte del lancio, i bersagli dei trigger, le ricerche, gli
// attacchi e i blocchi veri. Non "passa", i pagamenti, l'ordine dei trigger.
bool worth_telling(const GameState& s, const Action& a) {
    switch (s.pending.kind) {
        case DecisionKind::Priority: return a.type != ActionType::Pass;
        case DecisionKind::DeclareAttacker: return a.type != ActionType::ChooseOption;
        case DecisionKind::DeclareBlockers: return a.type == ActionType::ChooseCard;
        case DecisionKind::PayMana:
        case DecisionKind::OrderTriggers:
        case DecisionKind::PayLifeOrTapped:
        case DecisionKind::Surveil:
        case DecisionKind::DivideDamage: return false;
        default: return true;
    }
}

}  // namespace

// --- Canarino ---------------------------------------------------------------------------------------------------------

GameState cheating_world(const GameState& s, std::uint64_t world_seed) {
    GameState w = s;
    for (PlayerId p = 0; p < kNumPlayers; ++p) w.players[p].rng = player_generator(world_seed ^ kCheatSalt, p);
    return w;
}

SeatFactory cheating_uw_seat(StrongConfig config, std::vector<SearchRecord>* records) {
    return [config, records](PlayerId, const CardDatabase& db, std::uint64_t seed) -> std::unique_ptr<Seat> {
        return std::make_unique<CheatingSeat>(db, seed, config, records);
    };
}

// --- Partite forti ----------------------------------------------------------------------------------------------------

RakdosAgentOptions rakdos_variant(std::string_view name) {
    if (name == "base_r1") return RakdosAgentOptions::base_r1();
    if (name == "base_r2") return RakdosAgentOptions::base_r2();
    if (name == "base_r3") return RakdosAgentOptions{};
    throw EngineError("Rakdos sconosciuto: " + std::string(name) + " (base_r1, base_r2, base_r3)");
}

std::string strong_game_json(const MatchupGame& g, const std::vector<SearchRecord>& records) {
    ordered_json j = ordered_json::parse(matchup_game_json(g));
    j["cercate"] = records.size();
    j["scostamenti"] = std::count_if(records.begin(), records.end(), [](const SearchRecord& r) { return r.deviates; });
    return j.dump();
}

std::string search_record_json(std::uint64_t seed, const SearchRecord& r) {
    ordered_json j;
    j["seed"] = seed;
    j["decisione"] = r.decision;
    j["turno"] = r.turn;
    j["step"] = std::string(step_name(r.step));
    j["tipo"] = std::string(searched_kind_name(r.kind));
    j["mondi"] = r.worlds;
    j["euristica"] = r.heuristic;
    j["regola"] = r.heuristic_rule;
    j["scelta"] = r.chosen;
    j["migliore"] = r.best;
    j["scostamento"] = r.deviates;
    j["differenza"] = r.diff;
    j["basso"] = r.low;
    j["alto"] = r.high;
    ordered_json moves = ordered_json::array();
    for (std::size_t m = 0; m < r.moves.size(); ++m) moves.push_back({{"mossa", r.moves[m]}, {"vittorie", r.wins[m]}});
    j["mosse"] = moves;
    j["decisioni_rollout"] = r.rollout_decisions;
    j["interrotti"] = r.aborted;
    j["scartati"] = r.discarded;
    return j.dump();
}

StrongBlock play_strong_block(const StrongRun& run, std::uint64_t first, std::uint64_t last, std::ostream* progress) {
    using Clock = std::chrono::steady_clock;
    const std::size_t n = static_cast<std::size_t>(last - first + 1);
    std::vector<std::string> games(n), searches(n), anomalies(n);
    std::vector<StrongStats> stats(n);
    std::atomic<std::size_t> done{0};
    std::mutex print;
    const auto start = Clock::now();
    auto last_print = start;
    parallel_for(n, run.threads, [&](std::size_t i) {
        const std::uint64_t seed = first + i;
        std::vector<SearchRecord> records;
        MatchupConfig config = run.base;
        if (run.uw_seat) {
            config.uw_seat = *run.uw_seat;
        } else if (run.cheat) {
            config.uw_seat.kind = AgentKind::Diagnostic;
            config.uw_seat.diagnostic = cheating_uw_seat(run.strong, &records);
        } else {
            config.uw_seat.kind = AgentKind::View;
            const StrongConfig strong = run.strong;
            config.uw_seat.view_agent = [&records, strong, db = config.db](std::uint64_t g) {
                return std::make_unique<StrongUwAgent>(*db, g, strong, &records);
            };
        }
        StrongStats& st = stats[i];
        try {
            const MatchupGame g = play_matchup(config, seed);
            games[i] = strong_game_json(g, records) + "\n";
            st.games = 1;
            st.uw_decisions = g.uw_decisions;
            for (const SearchRecord& r : records) {
                searches[i] += search_record_json(seed, r) + "\n";
                ++st.searched;
                st.deviations += r.deviates ? 1 : 0;
                st.moves += r.moves.size();
                st.rollouts += r.moves.size() * r.worlds;
                st.rollout_decisions += r.rollout_decisions;
                st.search_seconds += r.seconds;
                st.aborted += r.aborted;
                st.discarded += r.discarded;
                ++st.by_kind[static_cast<int>(r.kind)];
                st.deviations_by_kind[static_cast<int>(r.kind)] += r.deviates ? 1 : 0;
            }
        } catch (const EngineLimitError& e) {
            // La partita vera oltre un limite di capacita' del motore (la pila a 16): un'anomalia, fuori dai dati
            // (non entra in partite.jsonl ne' in ricerca.jsonl, quindi nemmeno nel confronto appaiato).
            ordered_json a;
            a["seed"] = seed;
            a["errore"] = e.what();
            a["cercate"] = records.size();
            anomalies[i] = a.dump() + "\n";
            st = StrongStats{};
            st.anomalies = 1;
        } catch (const std::exception& e) {
            throw EngineError("partita forte, seed " + std::to_string(seed) + ": " + e.what());
        }
        const std::size_t now_done = ++done;
        if (progress != nullptr) {
            const std::lock_guard<std::mutex> lock(print);
            const auto now = Clock::now();
            if (now - last_print > std::chrono::seconds(60) || now_done == n) {
                last_print = now;
                const double secs = std::chrono::duration<double>(now - start).count();
                char buf[200];
                std::snprintf(buf, sizeof buf, "  partite %zu/%zu del blocco, %.0f s, circa %.0f s alla fine del blocco\n",
                              now_done, n, secs, secs / static_cast<double>(now_done) * static_cast<double>(n - now_done));
                *progress << buf << std::flush;
            }
        }
    });
    StrongBlock out;
    for (std::size_t i = 0; i < n; ++i) {
        out.games += games[i];
        out.searches += searches[i];
        out.anomalies += anomalies[i];
        const StrongStats& s = stats[i];
        out.stats.games += s.games;
        out.stats.uw_decisions += s.uw_decisions;
        out.stats.searched += s.searched;
        out.stats.deviations += s.deviations;
        out.stats.moves += s.moves;
        out.stats.rollouts += s.rollouts;
        out.stats.rollout_decisions += s.rollout_decisions;
        out.stats.search_seconds += s.search_seconds;
        out.stats.aborted += s.aborted;
        out.stats.discarded += s.discarded;
        out.stats.anomalies += s.anomalies;
        for (int k = 0; k < 4; ++k) {
            out.stats.by_kind[k] += s.by_kind[k];
            out.stats.deviations_by_kind[k] += s.deviations_by_kind[k];
        }
    }
    return out;
}

// --- Diagnosi della pila ------------------------------------------------------------------------------------------------

namespace {

std::string stack_item_text(const GameState& s, const CardDatabase& db, const StackItem& item) {
    const std::string source = item.source_def != kNoDef ? db.get(item.source_def).name : "?";
    const std::string who = item.controller == kUw ? "UW" : "Rakdos";
    std::string target;
    if (item.target.tag == TargetKindTag::Object && item.target.object.card < kMaxObjects) {
        target = " -> " + db.get(s.objects[item.target.object.card].def).name;
    } else if (item.target.tag == TargetKindTag::Player) {
        target = item.target.player == kUw ? " -> UW" : " -> Rakdos";
    } else if (item.target.tag == TargetKindTag::StackItem) {
        target = " -> pila #" + std::to_string(item.target_id);
    }
    if (item.kind == StackKind::Spell) return who + ": magia " + source + (item.copy ? " (copia)" : "") + target;
    return who + ": abilita' di " + source + " (effetto " + std::to_string(static_cast<int>(item.effect)) + ")" + target;
}

std::string stack_text(const GameState& s, const CardDatabase& db) {
    std::string out = "pila " + std::to_string(s.stack_items.size()) + "/" + std::to_string(kMaxStack);
    for (std::size_t k = 0; k < s.stack_items.size(); ++k) {
        out += (k == 0 ? ": " : " | ") + stack_item_text(s, db, s.stack_items[k]);
    }
    if (!s.pending_triggers.empty()) {
        out += "; trigger in attesa " + std::to_string(s.pending_triggers.size()) + ":";
        for (const StackItem& t : s.pending_triggers) out += " [" + stack_item_text(s, db, t) + "]";
    }
    return out;
}

std::string board_text(const GameState& s, const CardDatabase& db) {
    std::string o;
    o += "Turno " + std::to_string(s.turn.turn_number) + ", " + std::string(step_name(s.turn.step)) + "; vite UW " +
         std::to_string(s.players[kUw].life) + ", Rakdos " + std::to_string(s.players[kRakdos].life) + "\n";
    o += "Mano di UW: " + names_in(s, db, s.players[kUw].hand) + "\n";
    o += "Mano di Rakdos (nel mondo campionato): " + names_in(s, db, s.players[kRakdos].hand) + "\n";
    o += "Campo di UW: " + permanents_of(s, db, kUw) + "\n";
    o += "Campo di Rakdos: " + permanents_of(s, db, kRakdos) + "\n";
    o += "Cimitero di UW: " + names_in(s, db, s.players[kUw].graveyard) + "\n";
    o += "Cimitero di Rakdos: " + names_in(s, db, s.players[kRakdos].graveyard) + "\n";
    return o;
}

// Il giocatore forte, con la diagnosi dei rollout interrotti delle sue ricerche.
class OverflowProbe final : public ViewAgent {
public:
    OverflowProbe(const CardDatabase& db, std::uint64_t seed, const StrongConfig& config,
                  std::vector<OverflowReplay>* out, std::size_t max_replays)
        : db_(&db), seed_(seed), config_(config), inner_(db, seed, config, &records_), out_(out), max_(max_replays) {}

    Action choose(const PlayerView& view, std::span<const Action> legal) override {
        const std::size_t before = records_.size();
        const Action chosen = inner_.choose(view, legal);
        if (records_.size() == before || records_.back().aborted_at.empty()) return chosen;
        const SearchRecord& r = records_.back();
        const std::uint64_t search = decision_seed(seed_, r.decision);
        for (const auto& [m, j] : r.aborted_at) {
            if (out_->size() >= max_) break;
            const GameState w = view.sample_world(world_seed(search, j));
            std::vector<std::string> texts;
            const std::vector<CompoundMove> moves = enumerate_compound_moves(w, *db_, legal, &texts);
            OverflowReplay rep;
            rep.seed = seed_;
            rep.decision = r.decision;
            rep.move = m;
            rep.world = j;
            rep.move_text = texts.at(m);
            std::string text = "Seed " + std::to_string(seed_) + ", decisione " + std::to_string(r.decision) +
                               " di UW forte (" + std::string(searched_kind_name(r.kind)) + "), mondo " +
                               std::to_string(j) + ", mossa " + std::to_string(m) + ": " + rep.move_text + "\n\n";
            text += "Il mondo campionato all'inizio del rollout:\n" + board_text(w, *db_) + "\n";
            GameState last;
            Action last_action;
            std::string last_rule;
            const RolloutObserver observer = [&](const GameState& s, const Action& a, std::string_view rule) {
                last = s;
                last_action = a;
                last_rule = std::string(rule);
                const bool tell = worth_telling(s, a) || !s.stack_items.empty() || !s.pending_triggers.empty();
                if (!tell) return;
                text += "T" + std::to_string(s.turn.turn_number) + " " + std::string(step_name(s.turn.step)) + " " +
                        (s.pending.player == kUw ? "UW" : "Rakdos") + ": " + describe_choice(s, *db_, a) + "  (" +
                        last_rule + ")";
                if (!s.stack_items.empty() || !s.pending_triggers.empty()) text += "  [" + stack_text(s, *db_) + "]";
                text += "\n";
            };
            bool aborted = false;
            const bool win = rollout_wins(w, *db_, moves.at(m), rollout_agent_seed(search, j), config_.rollout,
                                          nullptr, &aborted, &observer);
            (void)win;
            text += "\nRollout interrotto: " + std::string(aborted ? "si'" : "no (non riprodotto!)") + "\n";
            if (aborted) {
                GameState again = last;
                try {
                    apply(again, *db_, last_action);
                    text += "(l'ultima azione rigiocata non supera il limite: il limite e' scattato altrove)\n";
                } catch (const std::exception& e) {
                    text += "Ultima azione: " + describe_choice(last, *db_, last_action) + " (" + last_rule + ")\n";
                    text += "Errore del motore: " + std::string(e.what()) + "\n";
                }
                text += "Stato prima dell'ultima azione:\n" + board_text(last, *db_) + stack_text(last, *db_) + "\n";
            }
            rep.text = text;
            out_->push_back(std::move(rep));
        }
        return chosen;
    }
    [[nodiscard]] std::string_view last_rule() const override { return inner_.last_rule(); }

private:
    const CardDatabase* db_;
    std::uint64_t seed_;
    StrongConfig config_;
    std::vector<SearchRecord> records_;
    StrongUwAgent inner_;
    std::vector<OverflowReplay>* out_;
    std::size_t max_;
};

}  // namespace

std::vector<OverflowReplay> replay_stack_overflows(const StrongRun& run, std::uint64_t seed, std::size_t max_replays) {
    std::vector<OverflowReplay> out;
    MatchupConfig config = run.base;
    config.uw_seat.kind = AgentKind::View;
    const StrongConfig strong = run.strong;
    config.uw_seat.view_agent = [&out, strong, max_replays, db = config.db](std::uint64_t g) {
        return std::make_unique<OverflowProbe>(*db, g, strong, &out, max_replays);
    };
    (void)play_matchup(config, seed);
    return out;
}

// --- Confronto appaiato -------------------------------------------------------------------------------------------------

PairedDifference paired_difference(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size() || a.empty()) throw EngineError("confronto appaiato: vettori di lunghezza diversa o vuoti");
    PairedDifference d;
    d.n = a.size();
    double sa = 0, sb = 0, sd = 0, sq = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sa += a[i];
        sb += b[i];
        const double x = static_cast<double>(a[i] - b[i]);
        sd += x;
        sq += x * x;
    }
    const auto n = static_cast<double>(d.n);
    d.a = sa / n;
    d.b = sb / n;
    d.diff = sd / n;
    const double var = d.n > 1 ? std::max(0.0, (sq - n * d.diff * d.diff) / (n - 1)) : 0.0;
    const double half = 1.959963984540054 * std::sqrt(var / n);
    d.low = d.diff - half;
    d.high = d.diff + half;
    return d;
}

// --- Curva di convergenza ----------------------------------------------------------------------------------------------

std::vector<SampledDecision> sample_decisions(const MatchupConfig& base, std::size_t n, std::uint64_t first_game_seed) {
    const CardDatabase& db = *base.db;
    std::vector<SampledDecision> out;
    for (std::uint64_t g = first_game_seed; out.size() < n; ++g) {
        GameState s = create_game(db, base.uw, base.rakdos, g);
        start_game(s, db, StartOptions{g % 2 == 1 ? kUw : kRakdos, true});
        UwAgent uw(db, g, base.uw_agent);
        RakdosAgent rakdos(db, g, base.rakdos_agent);
        std::vector<SampledDecision> candidates;
        std::uint64_t ordinal = 0;
        while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= base.turn_limit) {
            const std::vector<Action> legal = legal_actions(s, db);
            Action a;
            if (s.pending.player == kUw) {
                const SearchedKind kind = classify_decision(s, db, legal, kUw);
                if (kind != SearchedKind::None) candidates.push_back(SampledDecision{g, ordinal, kind, s, uw});
                a = uw.choose(s, legal);
                ++ordinal;
            } else {
                a = rakdos.choose(s, legal);
            }
            apply(s, db, a);
        }
        if (candidates.empty()) continue;
        Pcg32 pick(g, 0xc0ffee5eedULL);
        out.push_back(candidates[pick.bounded(static_cast<std::uint32_t>(candidates.size()))]);
    }
    return out;
}

std::string curve_decision_json(const CardDatabase& db, const SampledDecision& d, std::uint64_t index,
                                const std::vector<std::uint32_t>& prefixes, std::uint32_t reference,
                                const StrongConfig& config) {
    const std::vector<Action> legal = legal_actions(d.state, db);
    const PlayerView view(d.state, db, kUw, legal);
    const WorldSource world = [&view](std::uint64_t w) { return view.sample_world(w); };
    const std::uint64_t seed = decision_seed(d.game_seed, d.ordinal);
    const std::uint64_t ref_seed = decision_seed(d.game_seed ^ kReferenceSalt, d.ordinal);
    UwAgent shadow = d.agent;
    const PreparedSearch prep = prepare_search(shadow, world(world_seed(seed, kShadowWorld)), db, legal);
    const std::uint32_t rmax = *std::max_element(prefixes.begin(), prefixes.end());
    const SearchTable t = run_search(world, db, prep.moves, prep.heuristic, rmax, seed, config.rollout);
    const SearchTable ref = run_search(world, db, prep.moves, prep.heuristic, reference, ref_seed, config.rollout);
    const SearchChoice cref = choose_with_confidence(ref, reference, config.confidence);

    ordered_json j;
    j["indice"] = index;
    j["seed"] = d.game_seed;
    j["decisione"] = d.ordinal;
    j["tipo"] = std::string(searched_kind_name(d.kind));
    j["turno"] = d.state.turn.turn_number;
    j["step"] = std::string(step_name(d.state.turn.step));
    j["mosse"] = prep.texts;
    j["euristica"] = prep.heuristic;
    j["regola"] = prep.heuristic_rule;
    ordered_json chosen, best, deviates, low;
    for (const std::uint32_t r : prefixes) {
        const SearchChoice c = choose_with_confidence(t, r, config.confidence);
        const std::string key = std::to_string(r);
        chosen[key] = c.chosen;
        best[key] = c.best;
        deviates[key] = c.deviates;
    }
    j["scelte"] = chosen;
    j["migliori"] = best;
    j["scostamenti"] = deviates;
    std::vector<int> wins;
    for (std::size_t m = 0; m < t.moves.size(); ++m) wins.push_back(t.wins_of(m, rmax));
    j["vittorie"] = wins;
    j["mondi"] = rmax;
    std::vector<int> ref_wins;
    for (std::size_t m = 0; m < ref.moves.size(); ++m) ref_wins.push_back(ref.wins_of(m, reference));
    j["riferimento"] = {{"mondi", reference},       {"scelta", cref.chosen},   {"migliore", cref.best},
                        {"scostamento", cref.deviates}, {"differenza", cref.diff}, {"basso", cref.low},
                        {"vittorie", ref_wins}};
    j["decisioni_rollout"] = t.rollout_decisions + ref.rollout_decisions;
    return j.dump();
}

std::string curve_summary(const std::string& jsonl) {
    std::vector<ordered_json> rows;
    std::istringstream in(jsonl);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) rows.push_back(ordered_json::parse(line));
    }
    if (rows.empty()) return "Curva di convergenza: nessuna decisione\n";
    std::vector<std::string> budgets;
    for (const auto& [key, value] : rows.front()["scelte"].items()) budgets.push_back(key);
    const std::vector<std::string> kinds = {"tutte", "fase principale", "risposta", "turno avversario"};
    std::ostringstream o;
    std::map<std::string, int> per_kind;
    long long moves = 0;
    int ref_dev = 0;
    for (const auto& r : rows) {
        ++per_kind[r["tipo"].get<std::string>()];
        moves += static_cast<long long>(r["mosse"].size());
        ref_dev += r["riferimento"]["scostamento"].get<bool>() ? 1 : 0;
    }
    const int ref_worlds = rows.front()["riferimento"]["mondi"].get<int>();
    o << "Curva di convergenza: " << rows.size() << " decisioni (fase principale " << per_kind["fase principale"]
      << ", risposta " << per_kind["risposta"] << ", turno avversario " << per_kind["turno avversario"]
      << "); mosse composte in media " << decimal(static_cast<double>(moves) / static_cast<double>(rows.size()), 2)
      << "; riferimento con " << ref_worlds << " mondi per mossa (scostamenti dall'euristico nel riferimento: "
      << decimal(100.0 * ref_dev / static_cast<double>(rows.size()), 1) << "%).\n\n";
    o << "Scelte uguali a quelle del riferimento (con la regola del 99%); in parentesi la migliore stimata uguale alla "
         "migliore del riferimento; valore perso = valore stimato dal riferimento della sua scelta meno quello della "
         "scelta al budget R (punti di vittoria, media); scostamenti = quota di scelte diverse dall'euristico.\n\n";
    for (const std::string& b : budgets) {
        o << "R = " << b << "\n";
        for (const std::string& kind : kinds) {
            int n = 0, same = 0, same_best = 0, dev = 0;
            double loss = 0.0;
            for (const auto& r : rows) {
                if (kind != "tutte" && r["tipo"].get<std::string>() != kind) continue;
                ++n;
                const auto choice = r["scelte"][b].get<std::size_t>();
                const auto ref_choice = r["riferimento"]["scelta"].get<std::size_t>();
                same += choice == ref_choice ? 1 : 0;
                same_best += r["migliori"][b].get<std::size_t>() == r["riferimento"]["migliore"].get<std::size_t>() ? 1 : 0;
                dev += r["scostamenti"][b].get<bool>() ? 1 : 0;
                const auto& v = r["riferimento"]["vittorie"];
                loss += (v[ref_choice].get<double>() - v[choice].get<double>()) / ref_worlds;
            }
            if (n == 0) continue;
            o << "  " << kind << " (" << n << "): scelte uguali " << decimal(100.0 * same / n, 1) << "% (migliore "
              << decimal(100.0 * same_best / n, 1) << "%), valore perso " << decimal(100.0 * loss / n, 2)
              << " punti, scostamenti " << decimal(100.0 * dev / n, 1) << "%\n";
        }
    }
    return o.str();
}

// --- Posizioni test -----------------------------------------------------------------------------------------------------

bool indicated_line(const CardDatabase& db, const std::string& id, const std::vector<LinePlay>& plays,
                    const GameState& end) {
    (void)end;
    auto def = [&](const char* n) { return db.id_of(n); };
    auto any = [&](auto&& pred) { return std::any_of(plays.begin(), plays.end(), pred); };
    auto cast = [&](const LinePlay& p, const char* n) { return p.kind == LinePlay::Kind::Cast && p.card == def(n); };
    const std::uint8_t rakdos_graveyard = static_cast<std::uint8_t>(1u << kRakdos);
    if (id == "1") {
        return any([&](const LinePlay& p) {
            return cast(p, "March of Otherworldly Light") && p.target == def("Moonshadow") && p.exiled.empty() &&
                   (p.x == -1 || p.x == 1);
        });
    }
    if (id == "1c") {
        return any([&](const LinePlay& p) { return cast(p, "March of Otherworldly Light") && p.target == def("Moonshadow"); });
    }
    if (id == "2" || id == "2c") return any([&](const LinePlay& p) { return cast(p, "Sanctifier en-Vec"); });
    if (id == "3" || id == "3c") {
        return any([&](const LinePlay& p) {
            return cast(p, "Thraben Charm") && p.mode == 2 && (p.target_players & rakdos_graveyard) != 0;
        });
    }
    if (id == "4" || id == "4c") {
        return any([&](const LinePlay& p) { return cast(p, "Teferi, Time Raveler") && p.before_attackers; });
    }
    if (id == "5" || id == "5c") {
        const CardDefId target = def(id == "5" ? "Nethergoyf" : "Stitcher's Supplier");
        return any([&](const LinePlay& p) {
            return p.kind == LinePlay::Kind::TriggerTarget && p.top_at_cast == StackEffect::ExileOtherCreature &&
                   p.target == target;
        });
    }
    if (id == "6" || id == "6c") {
        return any([&](const LinePlay& p) { return cast(p, "Quantum Riddler") && p.method == CastMethod::Warp; }) &&
               any([&](const LinePlay& p) {
                   return cast(p, "Consign to Memory") && p.target_effect == StackEffect::WarpExile;
               });
    }
    if (id == "7a" || id == "7c") {
        return any([&](const LinePlay& p) {
            return cast(p, "Consign to Memory") && p.target_effect == StackEffect::PheliaReturn;
        });
    }
    if (id == "8a") {
        return any([&](const LinePlay& p) {
            return cast(p, "Thraben Charm") && p.mode == 0 && p.target == def("Fear of Missing Out") && p.before_attackers;
        });
    }
    if (id == "8b") {
        return any([&](const LinePlay& p) {
            return cast(p, "Thraben Charm") && p.mode == 2 && (p.target_players & rakdos_graveyard) != 0 &&
                   p.top_at_cast == StackEffect::UntapAndExtraCombat;
        });
    }
    if (id == "8c") return any([&](const LinePlay& p) { return cast(p, "Thraben Charm"); });
    if (id == "9a" || id == "9b") {
        return any([&](const LinePlay& p) {
            return cast(p, "Ephemerate") && p.target == def("Solitude") && p.top_at_cast == StackEffect::EvokeSacrifice;
        });
    }
    if (id == "10a" || id == "10b" || id == "10c") {
        const CardDefId wanted = def(id == "10b" ? "Sanctifier en-Vec" : "Solitude");
        return any([&](const LinePlay& p) { return p.kind == LinePlay::Kind::Search && p.card == wanted; });
    }
    throw EngineError("posizione sconosciuta: " + id);
}

std::string indicated_line_text(const std::string& id) {
    if (id == "1") return "March X=1 su Moonshadow, senza esiliare carte";
    if (id == "1c") return "March su Moonshadow (con un mana costa una carta bianca)";
    if (id == "2" || id == "2c") return "Sanctifier en-Vec nel turno";
    if (id == "3" || id == "3c") return "Thraben Charm esilia il cimitero di Rakdos";
    if (id == "4" || id == "4c") return "Teferi prima degli attacchi";
    if (id == "5") return "Solitude esilia Nethergoyf (con la Phoenix)";
    if (id == "5c") return "Solitude esilia la creatura con la Phoenix (Stitcher's Supplier)";
    if (id == "6" || id == "6c") return "Riddler con il warp e Consign sull'esilio del warp";
    if (id == "7a" || id == "7c") return "Consign sul ritorno di Phelia";
    if (id == "8a") return "Thraben Charm, danni su FOMO, prima degli attacchi";
    if (id == "8b") return "Thraben Charm esilia i cimiteri con il trigger di FOMO in pila";
    if (id == "8c") return "Thraben Charm lanciato nel turno";
    if (id == "9a" || id == "9b") return "Ephemerate su Solitude con il sacrificio dell'evocazione in pila";
    if (id == "10a" || id == "10c") return "Recruiter cerca Solitude";
    if (id == "10b") return "Recruiter cerca Sanctifier en-Vec";
    return {};
}

PositionOutcome play_position(const CardDatabase& db, const TestPosition& p, const StrongConfig* strong,
                              std::uint64_t seed, bool with_replay) {
    PositionOutcome o;
    GameState s = p.state;
    std::optional<StrongUwAgent> forte;
    std::optional<UwAgent> heuristic;
    if (strong != nullptr) {
        forte.emplace(db, seed, *strong, &o.records);
    } else {
        heuristic.emplace(db, seed, UwAgentOptions{});
    }
    RakdosAgent rakdos(db, seed, RakdosAgentOptions{});
    std::string text;
    if (with_replay) {
        text += "Posizione " + p.id + ": " + p.situation + "\n";
        text += "Linea indicata: " + p.indicated + "\n";
        text += "Linea contata: " + indicated_line_text(p.id) + "\n";
        text += "Turno " + std::to_string(s.turn.turn_number) + ", " + std::string(step_name(s.turn.step)) + "; vite UW " +
                std::to_string(s.players[kUw].life) + ", Rakdos " + std::to_string(s.players[kRakdos].life) + "\n";
        text += "Mano di UW: " + names_in(s, db, s.players[kUw].hand) + "\n";
        text += "Mano di Rakdos (nascosta a UW, salvo le carte note): " + names_in(s, db, s.players[kRakdos].hand) + "\n";
        text += "Campo di UW: " + permanents_of(s, db, kUw) + "\n";
        text += "Campo di Rakdos: " + permanents_of(s, db, kRakdos) + "\n";
        text += "Cimitero di Rakdos: " + names_in(s, db, s.players[kRakdos].graveyard) + "\n\n";
    }
    std::vector<std::string> segments;
    const std::uint16_t turn = s.turn.turn_number;
    for (int guard = 0; guard < 5000 && s.pending.kind != DecisionKind::GameOver && s.turn.turn_number == turn; ++guard) {
        const std::vector<Action> legal = legal_actions(s, db);
        Action a;
        std::string note;
        const PlayerId who = s.pending.player;
        if (who == kUw) {
            if (forte) {
                a = forte->choose(PlayerView(s, db, kUw, legal), legal);
                note = forte->last_detail();
            } else {
                a = heuristic->choose(s, legal);
                note = std::string(heuristic->last_rule());
            }
            // Le giocate di UW per la linea del turno.
            const std::string desc = describe_choice(s, db, a);
            const std::string where = std::string(step_name(s.turn.step));
            const StackEffect top = s.stack_items.empty() ? StackEffect::None : s.stack_items.back().effect;
            if (s.pending.kind == DecisionKind::Priority && a.type != ActionType::Pass) {
                LinePlay lp;
                lp.kind = a.type == ActionType::CastSpell     ? LinePlay::Kind::Cast
                          : a.type == ActionType::PlayLand    ? LinePlay::Kind::Land
                                                              : LinePlay::Kind::Ability;
                lp.step = s.turn.step;
                lp.before_attackers = s.turn.step < Step::DeclareAttackers;
                lp.card = s.objects[a.card].def;
                if (a.type == ActionType::CastSpell) lp.method = static_cast<CastMethod>(a.value);
                lp.top_at_cast = top;
                o.plays.push_back(lp);
                segments.push_back(where + ": " + desc);
            } else if (is_cast_choice(s, kUw) && !o.plays.empty()) {
                LinePlay& lp = o.plays.back();
                switch (s.pending.kind) {
                    case DecisionKind::ChooseMode: lp.mode = a.value; break;
                    case DecisionKind::ChooseX: lp.x = a.value; break;
                    case DecisionKind::ChooseTarget:
                        if (a.type == ActionType::TargetObject) lp.target = s.objects[a.card].def;
                        if (a.type == ActionType::TargetStackItem) {
                            lp.target_effect = s.stack_items[a.value].effect;
                            lp.target_spell = s.stack_items[a.value].kind == StackKind::Spell;
                        }
                        if (a.type == ActionType::ChooseOption && a.value != kNoTarget) lp.target_players = a.value;
                        break;
                    case DecisionKind::ExileFromHandForCost:
                        if (a.type == ActionType::ChooseCard) lp.exiled.push_back(s.objects[a.card].def);
                        break;
                    default: break;
                }
                if (!segments.empty()) segments.back() += ", " + desc;
            } else if (s.pending.kind == DecisionKind::ChooseTarget) {
                LinePlay lp;
                lp.kind = LinePlay::Kind::TriggerTarget;
                lp.step = s.turn.step;
                lp.before_attackers = s.turn.step < Step::DeclareAttackers;
                lp.card = s.stack_items.empty() ? kNoDef : s.stack_items.back().source_def;
                lp.top_at_cast = top;
                if (a.type == ActionType::TargetObject) lp.target = s.objects[a.card].def;
                o.plays.push_back(lp);
                segments.push_back(where + ": bersaglio di " + (lp.card != kNoDef ? db.get(lp.card).name : "?") + " " + desc);
            } else if (s.pending.kind == DecisionKind::SearchLibrary) {
                LinePlay lp;
                lp.kind = LinePlay::Kind::Search;
                lp.step = s.turn.step;
                lp.card = a.type == ActionType::ChooseCard ? s.objects[a.card].def : kNoDef;
                o.plays.push_back(lp);
                segments.push_back(where + ": " + desc);
            }
        } else {
            a = rakdos.choose(s, legal);
            note = std::string(rakdos.last_rule());
        }
        if (with_replay && (worth_telling(s, a) || note.rfind("ricerca (", 0) == 0)) {
            text += "[" + std::string(step_name(s.turn.step)) + "] " + (who == kUw ? "UW: " : "Rakdos: ") +
                    describe_choice(s, db, a) + "\n    " + note + "\n";
        }
        apply(s, db, a);
    }
    o.end = s;
    o.line = indicated_line(db, p.id, o.plays, s);
    for (const std::string& seg : segments) o.signature += (o.signature.empty() ? "" : " -> ") + seg;
    if (o.signature.empty()) o.signature = "nessuna giocata";
    if (with_replay) {
        text += "\nFine del turno: vite UW " + std::to_string(s.players[kUw].life) + ", Rakdos " +
                std::to_string(s.players[kRakdos].life) + "\n";
        text += "Campo di UW: " + permanents_of(s, db, kUw) + "\n";
        text += "Campo di Rakdos: " + permanents_of(s, db, kRakdos) + "\n";
        text += "Esilio di Rakdos: " + exiled_of(s, db, kRakdos) + "\n";
        text += "Mano di UW: " + names_in(s, db, s.players[kUw].hand) + "\n";
        text += "Giocate di UW: " + o.signature + "\n";
        text += std::string("Linea indicata giocata: ") + (o.line ? "si'" : "no") + "\n";
        o.replay = text;
    }
    return o;
}

}  // namespace mtg
