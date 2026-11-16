#include "giocatori.hpp"

namespace mtg {

namespace {

// L'euristico v1 di un lato: Agent e' UwAgent o RakdosAgent.
template <typename Agent, typename Options>
class HeuristicSeat final : public Seat {
public:
    HeuristicSeat(const CardDatabase& db, std::uint64_t seed, const Options& options) : agent_(db, seed, options) {}
    Action choose(const GameState& s, std::span<const Action> legal) override { return agent_.choose(s, legal); }
    [[nodiscard]] std::string_view last_rule() const override { return agent_.last_rule(); }
    [[nodiscard]] bool last_was_default() const override { return agent_.last_rule() == Agent::kDefaultRule; }
    void add_rule_counts(std::map<std::string, std::uint64_t>& out) const override {
        for (const auto& [rule, n] : agent_.rule_counts()) out[rule] += n;
    }

private:
    Agent agent_;
};

// Un ViewAgent: a ogni decisione il posto costruisce la vista del giocatore e passa all'agente solo quella.
class ViewSeat final : public Seat {
public:
    ViewSeat(PlayerId player, const CardDatabase& db, std::unique_ptr<ViewAgent> agent)
        : player_(player), db_(&db), agent_(std::move(agent)) {
        if (!agent_) throw EngineError("ViewSeat: la fabbrica non ha costruito un agente");
    }
    Action choose(const GameState& s, std::span<const Action> legal) override {
        const PlayerView view(s, *db_, player_);
        const Action a = agent_->choose(view, legal);
        ++counts_[std::string(agent_->last_rule())];
        return a;
    }
    [[nodiscard]] std::string_view last_rule() const override { return agent_->last_rule(); }
    [[nodiscard]] bool last_was_default() const override { return false; }
    void add_rule_counts(std::map<std::string, std::uint64_t>& out) const override {
        for (const auto& [rule, n] : counts_) out[rule] += n;
    }

private:
    PlayerId player_;
    const CardDatabase* db_;
    std::unique_ptr<ViewAgent> agent_;
    std::map<std::string, std::uint64_t> counts_;
};

std::unique_ptr<Seat> view_seat(const SeatConfig& config, PlayerId player, const CardDatabase& db, std::uint64_t seed) {
    if (!config.view_agent) throw EngineError("SeatConfig: AgentKind::View senza la fabbrica dell'agente");
    return std::make_unique<ViewSeat>(player, db, config.view_agent(seed));
}

}  // namespace

std::unique_ptr<Seat> make_uw_seat(const SeatConfig& config, PlayerId player, const CardDatabase& db, std::uint64_t seed,
                                   const UwAgentOptions& options) {
    if (config.kind == AgentKind::View) return view_seat(config, player, db, seed);
    return std::make_unique<HeuristicSeat<UwAgent, UwAgentOptions>>(db, seed, options);
}

std::unique_ptr<Seat> make_rakdos_seat(const SeatConfig& config, PlayerId player, const CardDatabase& db,
                                       std::uint64_t seed, const RakdosAgentOptions& options) {
    if (config.kind == AgentKind::View) return view_seat(config, player, db, seed);
    return std::make_unique<HeuristicSeat<RakdosAgent, RakdosAgentOptions>>(db, seed, options);
}

}  // namespace mtg
