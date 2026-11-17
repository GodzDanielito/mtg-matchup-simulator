#include "ricerca_piatta.hpp"

#include "rng.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <utility>

namespace mtg {

namespace {

// Sali dei semi: la decisione di una partita, poi mondi (pari) e agenti dei rollout (dispari).
constexpr std::uint64_t kDecisionSalt = 0x6a09e667f3bcc909ULL;
constexpr std::uint64_t kSearchSalt = 0xbb67ae8584caa73bULL;

std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t state = a ^ kSearchSalt;
    const std::uint64_t first = splitmix64(state);
    state = first ^ (b * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL);
    return splitmix64(state);
}

const std::string& name(const GameState& w, const CardDatabase& db, CardId id) {
    return effective_def(w, db, id).name;
}

std::string_view method_suffix(CastMethod m) {
    switch (m) {
        case CastMethod::Normal: return "";
        case CastMethod::Escape: return " (fuga)";
        case CastMethod::Bestow: return " (conferimento)";
        case CastMethod::SacrificeAlternative: return " (sacrificando una creatura)";
        case CastMethod::Warp: return " (warp)";
        case CastMethod::Evoke: return " (evocazione)";
        case CastMethod::Free: return " (gratis)";
        case CastMethod::Pitch: return " (esiliando una carta blu)";
    }
    return "";
}

std::string_view effect_name(StackEffect e) {
    switch (e) {
        case StackEffect::WarpExile: return "esilio del warp";
        case StackEffect::PheliaReturn: return "ritorno di Phelia";
        case StackEffect::PheliaExile: return "esilio di Phelia";
        case StackEffect::EvokeSacrifice: return "sacrificio dell'evocazione";
        case StackEffect::ExileOtherCreature: return "esilio di Solitude";
        case StackEffect::UntapAndExtraCombat: return "combattimento in piu' di FOMO";
        case StackEffect::ExileUntilLeaves: return "esilio di Seam Rip";
        case StackEffect::FetchSearch: return "ricerca della fetch";
        case StackEffect::ReplicateCopies: return "copie di replicare";
        default: return "abilita'";
    }
}


}  // namespace

std::string describe_choice(const GameState& w, const CardDatabase& db, const Action& a) {
    const PlayerId me = w.pending.player;
    switch (w.pending.kind) {
        case DecisionKind::Priority:
            switch (a.type) {
                case ActionType::Pass: return "passa";
                case ActionType::PlayLand:
                    return "terra " + name(w, db, a.card) + (a.value == 1 ? " (retro)" : "");
                case ActionType::CastSpell: {
                    std::string s = name(w, db, a.card);
                    s += method_suffix(static_cast<CastMethod>(a.value));
                    const Zone z = w.objects[a.card].zone;
                    if (z == Zone::Exile) s += " dall'esilio";
                    if (z == Zone::Graveyard) s += " dal cimitero";
                    return s;
                }
                case ActionType::ActivateAbility: {
                    const CardDef& d = effective_def(w, db, a.card);
                    if (d.rules.land.fetch_types != 0) return name(w, db, a.card) + ": cerca una terra";
                    if (a.value < d.rules.loyalty_count) {
                        const int cost = d.rules.loyalty[a.value].cost;
                        return name(w, db, a.card) + " " + (cost > 0 ? "+" : "") + std::to_string(cost);
                    }
                    if (w.objects[a.card].zone == Zone::Hand) return name(w, db, a.card) + ": ciclo";
                    return name(w, db, a.card) + ": abilita'";
                }
                default: break;
            }
            break;
        case DecisionKind::ChooseMode: {
            const StackItem& top = w.stack_items.back();
            const SpellRules& spell = db.get(top.source_def).rules.spell;
            if (a.value < spell.mode_count) {
                switch (spell.modes[a.value].target) {
                    case TargetKind::Creature: return "modo: danni";
                    case TargetKind::Enchantment: return "modo: distrugge un incantesimo";
                    case TargetKind::Players: return "modo: esilia i cimiteri";
                    default: break;
                }
            }
            return "modo " + std::to_string(a.value);
        }
        case DecisionKind::ChooseX: return "X=" + std::to_string(a.value);
        case DecisionKind::ChooseTarget:
            switch (a.type) {
                case ActionType::TargetObject: {
                    const CardObject& o = w.objects[a.card];
                    return "su " + name(w, db, a.card) + (o.controller == me ? " (nostra)" : "");
                }
                case ActionType::TargetPlayer: return a.value == me ? "su di noi" : "sull'avversario";
                case ActionType::TargetStackItem: {
                    const StackItem& item = w.stack_items[a.value];
                    const std::string source = item.source_def != kNoDef ? db.get(item.source_def).name : "?";
                    if (item.kind == StackKind::Spell) return "sulla magia " + source;
                    return "su " + std::string(effect_name(item.effect)) + " (" + source + ")";
                }
                case ActionType::ChooseOption: {
                    if (a.value == kNoTarget) return "nessun bersaglio";
                    if (a.value == kKeepTarget) return "stesso bersaglio";
                    const bool mine = ((a.value >> me) & 1u) != 0;
                    const bool theirs = (a.value & ~(1u << me) & 0xFFu) != 0;
                    if (mine && theirs) return "sui cimiteri di entrambi";
                    return mine ? "sul nostro cimitero" : "sul cimitero dell'avversario";
                }
                default: break;
            }
            break;
        case DecisionKind::ExileFromHandForCost:
            if (a.type == ActionType::ChooseCard) return "esilia " + name(w, db, a.card);
            return "nessun'altra carta da esiliare";
        case DecisionKind::ChooseReplicate: return "replica " + std::to_string(a.value);
        case DecisionKind::SearchLibrary:
            if (a.type == ActionType::ChooseCard) return "cerca " + name(w, db, a.card);
            return a.value == kSkipSearch ? "non cerca" : "non trova niente";
        case DecisionKind::DeclareAttacker: {
            const std::string who = w.pending.subject.card < kMaxObjects ? name(w, db, w.pending.subject.card) : "?";
            if (a.type == ActionType::ChooseOption) return who + " non attacca";
            if (a.type == ActionType::TargetObject) return who + " attacca " + name(w, db, a.card);
            return who + " attacca";
        }
        case DecisionKind::DeclareBlockers: {
            const std::string who = w.pending.subject.card < kMaxObjects ? name(w, db, w.pending.subject.card) : "?";
            if (a.type == ActionType::ChooseCard) return name(w, db, a.card) + " blocca " + who;
            return "nessun altro bloccante per " + who;
        }
        case DecisionKind::CastFromExile: return a.value == kCastIt ? "lo rilancia (rimbalzo)" : "non lo rilancia";
        case DecisionKind::PayLifeOrTapped: return a.value == kPayLife ? "paga la vita" : "entra tappata";
        case DecisionKind::ChooseFromHand: return "prende " + name(w, db, a.card);
        default: break;
    }
    return "azione " + std::to_string(static_cast<int>(a.type)) + "/" + std::to_string(a.value);
}

namespace {

void extend(const GameState& w, const CardDatabase& db, PlayerId me, CompoundMove& prefix, const std::string& text,
            std::vector<CompoundMove>& out, std::vector<std::string>* texts) {
    if (!is_cast_choice(w, me)) {
        out.push_back(prefix);
        if (texts != nullptr) texts->push_back(text);
        return;
    }
    for (const Action& a : legal_actions(w, db)) {
        GameState next = w;
        const std::string more = text + ", " + describe_choice(w, db, a);
        apply(next, db, a);
        prefix.push_back(a);
        extend(next, db, me, prefix, more, out, texts);
        prefix.pop_back();
    }
}

double normal_quantile(double p) {
    // Bisezione sulla funzione di ripartizione (erfc): lenta ma esatta, si usa poche volte.
    double lo = -40.0, hi = 40.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (0.5 * std::erfc(-mid / std::numbers::sqrt2) < p) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Funzione di ripartizione della t di Student con df gradi di liberta', per x >= 0 (integrale di Simpson).
double student_t_cdf(double x, double df) {
    const double c = std::exp(std::lgamma((df + 1.0) / 2.0) - std::lgamma(df / 2.0)) / std::sqrt(df * std::numbers::pi);
    auto f = [&](double t) { return c * std::pow(1.0 + t * t / df, -(df + 1.0) / 2.0); };
    constexpr int kSteps = 4000;
    const double h = x / kSteps;
    double sum = f(0.0) + f(x);
    for (int i = 1; i < kSteps; ++i) sum += f(i * h) * (i % 2 == 1 ? 4.0 : 2.0);
    return 0.5 + sum * h / 3.0;
}

}  // namespace

std::string_view searched_kind_name(SearchedKind k) {
    switch (k) {
        case SearchedKind::None: return "non cercata";
        case SearchedKind::Main: return "fase principale";
        case SearchedKind::Response: return "risposta";
        case SearchedKind::OpponentTurn: return "turno avversario";
    }
    return "";
}

bool is_fetch_activation(const GameState& w, const CardDatabase& db, const Action& a) {
    return a.type == ActionType::ActivateAbility && a.card < kMaxObjects &&
           effective_def(w, db, a.card).rules.land.fetch_types != 0;
}

SearchedKind classify_decision(const GameState& w, const CardDatabase& db, std::span<const Action> legal, PlayerId me) {
    if (w.pending.kind != DecisionKind::Priority || w.pending.player != me || legal.size() < 2) return SearchedKind::None;
    const bool own_turn = w.turn.active_player == me;
    const bool main = w.turn.step == Step::Main1 || w.turn.step == Step::Main2;
    if (w.stack_items.empty() && own_turn) return main ? SearchedKind::Main : SearchedKind::None;
    const bool something = std::any_of(legal.begin(), legal.end(), [&](const Action& a) {
        return a.type != ActionType::Pass && !is_fetch_activation(w, db, a);
    });
    if (!something) return SearchedKind::None;
    return w.stack_items.empty() ? SearchedKind::OpponentTurn : SearchedKind::Response;
}

bool is_cast_choice(const GameState& w, PlayerId me) {
    if (w.pending.player != me) return false;
    switch (w.pending.kind) {
        case DecisionKind::ChooseMode:
        case DecisionKind::ChooseX:
        case DecisionKind::ExileFromHandForCost:
        case DecisionKind::ChooseReplicate: return true;
        case DecisionKind::ChooseTarget:
            return w.pending.stack_index == 0xFF && !w.stack_items.empty() &&
                   w.stack_items.back().kind != StackKind::TriggeredAbility;
        default: return false;
    }
}

std::vector<CompoundMove> enumerate_compound_moves(const GameState& w, const CardDatabase& db,
                                                   std::span<const Action> legal, std::vector<std::string>* texts) {
    std::vector<CompoundMove> out;
    if (texts != nullptr) texts->clear();
    const PlayerId me = w.pending.player;
    for (const Action& root : legal) {
        GameState next = w;
        const std::string text = describe_choice(w, db, root);
        apply(next, db, root);
        CompoundMove prefix{root};
        extend(next, db, me, prefix, text, out, texts);
    }
    return out;
}

std::string describe_move(const GameState& w, const CardDatabase& db, const CompoundMove& move) {
    std::string text;
    GameState s = w;
    for (std::size_t k = 0; k < move.size(); ++k) {
        if (k > 0) text += ", ";
        text += describe_choice(s, db, move[k]);
        apply(s, db, move[k]);
    }
    return text;
}

bool rollout_wins(GameState w, const CardDatabase& db, const CompoundMove& move, std::uint64_t agent_seed,
                  const RolloutConfig& cfg, std::uint64_t* decisions, bool* aborted) {
    UwAgent uw(db, agent_seed, cfg.uw_options);
    RakdosAgent rakdos(db, agent_seed, cfg.rakdos_options);
    auto ask = [&](const std::vector<Action>& legal) {
        return w.pending.player == cfg.uw ? uw.choose(w, legal) : rakdos.choose(w, legal);
    };
    std::uint64_t n = 0;
    try {
        for (const Action& a : move) {  // chiedi e sostituisci
            const Action ignored = ask(legal_actions(w, db));
            (void)ignored;
            apply(w, db, a);
            ++n;
        }
        while (w.pending.kind != DecisionKind::GameOver && w.turn.turn_number <= cfg.turn_limit) {
            const Action a = ask(legal_actions(w, db));
            apply(w, db, a);
            ++n;
        }
    } catch (const EngineLimitError&) {
        // Un limite di capacita' del motore (pila, trigger in attesa) nel corso del rollout: la partita non si puo'
        // continuare. Vale 0 (come la patta e il limite di turni) e si conta; non e' un errore della ricerca.
        if (decisions != nullptr) *decisions += n;
        if (aborted != nullptr) *aborted = true;
        return false;
    }
    if (decisions != nullptr) *decisions += n;
    return w.pending.kind == DecisionKind::GameOver && w.turn.winner == cfg.uw;
}

std::uint64_t decision_seed(std::uint64_t game_seed, std::uint64_t index) { return mix(game_seed ^ kDecisionSalt, index); }
std::uint64_t world_seed(std::uint64_t search_seed, std::uint32_t j) { return mix(search_seed, 2ULL * j); }
std::uint64_t rollout_agent_seed(std::uint64_t search_seed, std::uint32_t j) { return mix(search_seed, 2ULL * j + 1); }

int SearchTable::wins_of(std::size_t m, std::uint32_t prefix) const {
    const std::uint32_t n = std::min(prefix, worlds);
    int total = 0;
    for (std::uint32_t j = 0; j < n; ++j) total += wins[m * worlds + j];
    return total;
}

SearchTable run_search(const WorldSource& world, const CardDatabase& db, std::vector<CompoundMove> moves,
                       std::size_t heuristic, std::uint32_t worlds, std::uint64_t search_seed, const RolloutConfig& cfg) {
    SearchTable t;
    t.moves = std::move(moves);
    t.heuristic = heuristic;
    t.worlds = worlds;
    t.wins.assign(t.moves.size() * worlds, 0);
    for (std::uint32_t j = 0; j < worlds; ++j) {
        const GameState w = world(world_seed(search_seed, j));  // un mondo, comune a tutte le mosse
        const std::uint64_t agents = rollout_agent_seed(search_seed, j);
        for (std::size_t m = 0; m < t.moves.size(); ++m) {
            try {
                bool aborted = false;
                t.wins[m * worlds + j] = rollout_wins(w, db, t.moves[m], agents, cfg, &t.rollout_decisions, &aborted) ? 1 : 0;
                if (aborted) ++t.aborted;
            } catch (const std::exception& e) {
                throw EngineError("mossa " + std::to_string(m) + " (" + describe_move(w, db, t.moves[m]) + "), mondo " + std::to_string(j) + ": " + e.what());
            }
        }
    }
    return t;
}

SearchChoice choose_with_confidence(const SearchTable& t, std::uint32_t prefix, double confidence) {
    SearchChoice c;
    const std::uint32_t n = std::min(prefix, t.worlds);
    c.best = t.heuristic;
    c.chosen = t.heuristic;
    int best_wins = t.wins_of(t.heuristic, n);
    for (std::size_t m = 0; m < t.moves.size(); ++m) {
        const int w = t.wins_of(m, n);
        if (w > best_wins) {
            best_wins = w;
            c.best = m;
        }
    }
    if (c.best == t.heuristic || n < 2) return c;
    double sum = 0.0, squares = 0.0;
    for (std::uint32_t j = 0; j < n; ++j) {
        const double d = static_cast<double>(t.wins[c.best * t.worlds + j]) - t.wins[t.heuristic * t.worlds + j];
        sum += d;
        squares += d * d;
    }
    const double mean = sum / n;
    const double var = std::max(0.0, (squares - n * mean * mean) / (n - 1));
    const double half = student_t_quantile(1.0 - (1.0 - confidence) / 2.0, n - 1.0) * std::sqrt(var / n);
    c.diff = mean;
    c.low = mean - half;
    c.high = mean + half;
    c.deviates = c.low > 0.0;
    if (c.deviates) c.chosen = c.best;
    return c;
}

double student_t_quantile(double p, double df) {
    if (df > 1e7) return normal_quantile(p);
    if (df == 1.0) return std::tan(std::numbers::pi * (p - 0.5));
    if (df == 2.0) return (2.0 * p - 1.0) / std::sqrt(2.0 * p * (1.0 - p));
    if (df >= 30.0) {
        const double z = normal_quantile(p);
        const double z3 = z * z * z, z5 = z3 * z * z, z7 = z5 * z * z, z9 = z7 * z * z;
        const double g1 = (z3 + z) / 4.0;
        const double g2 = (5 * z5 + 16 * z3 + 3 * z) / 96.0;
        const double g3 = (3 * z7 + 19 * z5 + 17 * z3 - 15 * z) / 384.0;
        const double g4 = (79 * z9 + 776 * z7 + 1482 * z5 - 1920 * z3 - 945 * z) / 92160.0;
        return z + g1 / df + g2 / (df * df) + g3 / (df * df * df) + g4 / (df * df * df * df);
    }
    // Gradi di liberta' piccoli: bisezione sulla ripartizione (si memorizza per ogni thread).
    thread_local std::map<std::pair<double, double>, double> cache;
    const auto key = std::make_pair(p, df);
    if (const auto it = cache.find(key); it != cache.end()) return it->second;
    const bool upper = p >= 0.5;
    const double target = upper ? p : 1.0 - p;
    double lo = 0.0, hi = 1000.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (student_t_cdf(mid, df) < target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double q = (upper ? 1.0 : -1.0) * 0.5 * (lo + hi);
    cache.emplace(key, q);
    return q;
}

}  // namespace mtg
