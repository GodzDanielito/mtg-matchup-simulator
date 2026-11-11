#include "varianti.hpp"

#include <algorithm>
#include <cmath>

namespace mtg {

PairedDiff paired_difference(const std::vector<int>& base, const std::vector<int>& variant) {
    PairedDiff d;
    d.n = static_cast<long long>(std::min(base.size(), variant.size()));
    if (d.n == 0) return d;
    double sum = 0.0;
    for (long long i = 0; i < d.n; ++i) sum += variant[static_cast<std::size_t>(i)] - base[static_cast<std::size_t>(i)];
    d.mean = sum / static_cast<double>(d.n);
    if (d.n < 2) {
        d.low = d.high = d.mean;
        return d;
    }
    double squares = 0.0;
    for (long long i = 0; i < d.n; ++i) {
        const double x = variant[static_cast<std::size_t>(i)] - base[static_cast<std::size_t>(i)] - d.mean;
        squares += x * x;
    }
    const double se = std::sqrt(squares / static_cast<double>(d.n - 1)) / std::sqrt(static_cast<double>(d.n));
    d.low = d.mean - 1.96 * se;
    d.high = d.mean + 1.96 * se;
    return d;
}

RefillStats count_refill_warps(const GameLog& log, const std::map<std::size_t, std::string>& rules) {
    RefillStats r;
    bool counting = false;
    bool drew = false;  // la risoluzione del trigger e' cominciata: finisce al primo evento che non e' una pescata
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        const bool uw_draw = e.kind == LogEntry::Kind::ZoneChange && e.player == 0 && e.from == Zone::Library &&
                             e.to == Zone::Hand && e.cause == Cause::Draw;
        if (counting && drew && !uw_draw) counting = false;
        if (e.kind == LogEntry::Kind::StepStart && e.step == Step::Untap) counting = false;
        if (e.kind == LogEntry::Kind::Action && e.player == 0) {
            const ActionType t = e.action.type;
            if (t == ActionType::CastSpell || t == ActionType::PlayLand || t == ActionType::ActivateAbility) counting = false;
            const auto it = rules.find(i);
            if (it != rules.end() && it->second == "UW: Quantum Riddler: warp come ricarica") {
                ++r.casts;
                counting = true;
                drew = false;
            }
        }
        if (counting && uw_draw) {
            ++r.cards;
            drew = true;
        }
    }
    return r;
}

}  // namespace mtg
