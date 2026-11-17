#include "giocatore_forte.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace mtg {

namespace {

// Il mondo su cui si interroga l'ombra: fuori dall'intervallo dei mondi dei rollout.
constexpr std::uint32_t kShadowWorld = 0xFFFFFFFFu;

std::string decimal(double x, int digits, bool sign = false) {
    char buf[64];
    std::snprintf(buf, sizeof buf, sign ? "%+.*f" : "%.*f", digits, x);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

}  // namespace

PreparedSearch prepare_search(UwAgent& shadow, const GameState& w, const CardDatabase& db,
                              std::span<const Action> legal) {
    PreparedSearch p;
    const PlayerId me = w.pending.player;
    const Action root = shadow.choose(w, legal);
    p.heuristic_rule = std::string(shadow.last_rule());
    CompoundMove heuristic{root};
    {
        UwAgent probe = shadow;  // stesso piano e stesso generatore: le scelte del lancio dell'ombra
        GameState c = w;
        apply(c, db, root);
        while (is_cast_choice(c, me)) {
            const Action a = probe.choose(c, legal_actions(c, db));
            heuristic.push_back(a);
            apply(c, db, a);
        }
    }
    p.moves = enumerate_compound_moves(w, db, legal, &p.texts);
    const auto it = std::find(p.moves.begin(), p.moves.end(), heuristic);
    if (it == p.moves.end()) throw EngineError("giocatore forte: la mossa dell'euristico non e' tra le mosse composte");
    p.heuristic = static_cast<std::size_t>(it - p.moves.begin());
    return p;
}

std::string search_detail(const SearchRecord& r) {
    const std::uint32_t valid = r.worlds - r.discarded;
    std::string s = "ricerca (" + std::string(searched_kind_name(r.kind)) + ", " + std::to_string(r.worlds) + " mondi";
    if (r.discarded > 0) s += ", " + std::to_string(r.discarded) + " scartati per un limite del motore";
    s += ")";
    for (std::size_t m = 0; m < r.moves.size(); ++m) {
        s += " | " + r.moves[m] + ": " + std::to_string(r.wins[m]) + "/" + std::to_string(valid) + " (" +
             decimal(100.0 * r.wins[m] / std::max<std::uint32_t>(1, valid), 1) + "%)";
        if (m == r.heuristic) s += " [euristico: " + r.heuristic_rule + "]";
        if (m == r.chosen) s += " [scelta]";
    }
    const std::string level = decimal(100.0 * r.confidence, 0) + "%";
    if (r.best == r.heuristic) {
        s += " | la migliore e' quella dell'euristico";
    } else {
        s += " | migliore - euristico: " + decimal(r.diff, 3, true) + " [" + decimal(r.low, 3, true) + "; " +
             decimal(r.high, 3, true) + "] al " + level + (r.deviates ? ": scostamento" : ": si gioca l'euristico");
    }
    return s;
}

UwSearchPlayer::UwSearchPlayer(const CardDatabase& db, std::uint64_t game_seed, StrongConfig config,
                               std::vector<SearchRecord>* records)
    : db_(&db),
      game_seed_(game_seed),
      config_(config),
      records_(records),
      shadow_(db, game_seed, config.rollout.uw_options) {}

Action UwSearchPlayer::heuristic_choice(const GameState& w, std::span<const Action> legal,
                                        const std::function<CardDefId(CardId)>& shown_def) {
    if (w.pending.kind != DecisionKind::SearchLibrary) return shadow_.choose(w, legal);
    // Ricerca nel grimorio: le azioni vere citano carte nascoste, quelle del mondo altre carte con le stesse
    // definizioni (il proprio grimorio ha la stessa composizione in ogni mondo). L'ombra sceglie nel mondo; la carta
    // vera e' quella mostrata con la stessa definizione.
    const Action a = shadow_.choose(w, legal_actions(w, *db_));
    if (a.type != ActionType::ChooseCard) return a;
    const CardDefId wanted = w.objects[a.card].def;
    for (const Action& r : legal) {
        if (r.type != ActionType::ChooseCard) continue;
        const CardDefId shown = shown_def(r.card);
        if ((shown != kNoDef ? shown : w.objects[r.card].def) == wanted) return r;
    }
    throw EngineError("giocatore forte: la carta cercata dall'euristico non e' tra quelle mostrate");
}

Action UwSearchPlayer::decide(const WorldSource& world, std::span<const Action> legal,
                              const std::function<CardDefId(CardId)>& shown_def) {
    const std::uint64_t index = index_++;
    const std::uint64_t seed = decision_seed(game_seed_, index);
    const GameState w = world(world_seed(seed, kShadowWorld));
    const PlayerId me = w.pending.player;

    if (!pending_.empty()) {  // le scelte del lancio della mossa composta in corso
        const Action a = pending_.front();
        pending_.pop_front();
        const Action ignored = shadow_.choose(w, legal);  // chiedi e sostituisci
        (void)ignored;
        if (std::find(legal.begin(), legal.end(), a) == legal.end()) {
            throw EngineError("giocatore forte: una scelta della mossa composta non e' legale");
        }
        last_rule_ = "ricerca: mossa composta";
        last_detail_ = last_rule_;
        return a;
    }

    const SearchedKind kind = classify_decision(w, *db_, legal, me);
    if (kind == SearchedKind::None) {
        const Action a = heuristic_choice(w, legal, shown_def);
        last_rule_ = "euristico: " + std::string(shadow_.last_rule());
        last_detail_ = last_rule_;
        return a;
    }

    const auto start = std::chrono::steady_clock::now();
    PreparedSearch prep;
    try {
        prep = prepare_search(shadow_, w, *db_, legal);
    } catch (const EngineLimitError& e) {  // resta un limite: la partita vera e' un'anomalia, non un errore
        throw EngineLimitError("giocatore forte, preparazione della ricerca (decisione " + std::to_string(index) + "): " + e.what());
    } catch (const std::exception& e) {
        throw EngineError("giocatore forte, preparazione della ricerca (decisione " + std::to_string(index) + "): " + e.what());
    }
    SearchTable table;
    try {
        table = run_search(world, *db_, prep.moves, prep.heuristic, config_.worlds, seed, config_.rollout);
    } catch (const std::exception& e) {
        throw EngineError("giocatore forte, rollout (decisione " + std::to_string(index) + "): " + e.what());
    }
    const SearchChoice choice = choose_with_confidence(table, config_.worlds, config_.confidence);
    const CompoundMove& chosen = table.moves[choice.chosen];
    pending_.assign(chosen.begin() + 1, chosen.end());

    SearchRecord r;
    r.decision = index;
    r.turn = w.turn.turn_number;
    r.step = w.turn.step;
    r.kind = kind;
    r.worlds = config_.worlds;
    r.moves = std::move(prep.texts);
    for (std::size_t m = 0; m < table.moves.size(); ++m) r.wins.push_back(table.wins_of(m, config_.worlds));
    r.heuristic = prep.heuristic;
    r.chosen = choice.chosen;
    r.best = choice.best;
    r.heuristic_rule = prep.heuristic_rule;
    r.diff = choice.diff;
    r.low = choice.low;
    r.high = choice.high;
    r.confidence = config_.confidence;
    r.deviates = choice.deviates;
    r.rollout_decisions = table.rollout_decisions;
    r.aborted = table.aborted;
    r.discarded = table.discarded_worlds();
    r.aborted_at = table.aborted_at;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    last_rule_ = std::string(choice.deviates ? "ricerca: scostamento (" : "ricerca: come l'euristico (") +
                 prep.heuristic_rule + ")";
    last_detail_ = search_detail(r);
    if (records_ != nullptr) records_->push_back(std::move(r));
    return chosen.front();
}

StrongUwAgent::StrongUwAgent(const CardDatabase& db, std::uint64_t game_seed, StrongConfig config,
                             std::vector<SearchRecord>* records)
    : player_(db, game_seed, config, records) {}

Action StrongUwAgent::choose(const PlayerView& view, std::span<const Action> legal) {
    return player_.decide([&view](std::uint64_t seed) { return view.sample_world(seed); }, legal,
                          [&view](CardId c) { return view.shown_def(c); });
}

}  // namespace mtg
