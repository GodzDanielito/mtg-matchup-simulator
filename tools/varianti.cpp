#include "varianti.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

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

W1Stats& W1Stats::operator+=(const W1Stats& o) {
    casts += o.casts;
    consign_cast += o.consign_cast;
    saved += o.saved;
    stayed += o.stayed;
    return *this;
}

W1Stats count_w1(const GameLog& log, const std::map<std::size_t, std::string>& rules) {
    W1Stats w;
    CardId riddler = kNoCard;  // la Riddler dell'ultima partenza di W1, finche' si guarda
    bool consign = false;
    bool left = false;      // uscita dal campo almeno una volta
    bool on_field = false;  // in campo adesso
    auto close = [&] {
        if (riddler != kNoCard) {
            if (consign) ++w.consign_cast;
            if (consign && !left) ++w.saved;
            if (on_field) ++w.stayed;
        }
        riddler = kNoCard;
    };
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind == LogEntry::Kind::StepStart && e.step == Step::Untap) close();
        if (e.kind == LogEntry::Kind::ZoneChange && riddler != kNoCard && e.action.card == riddler) {
            if (e.from == Zone::Battlefield) left = true;
            on_field = e.to == Zone::Battlefield;
        }
        if (e.kind != LogEntry::Kind::Action || e.player != 0) continue;
        const auto it = rules.find(i);
        if (it == rules.end()) continue;
        if (it->second == "UW: Quantum Riddler: warp con Consign per l'esilio (variante W1)") {
            close();
            ++w.casts;
            riddler = e.action.card;
            consign = false;
            left = false;
            on_field = true;  // i cambi di zona che seguono (pila, poi campo) la aggiornano
        } else if (riddler != kNoCard && it->second == "UW: Consign to Memory: l'esilio del warp") {
            consign = true;
        }
    }
    close();
    return w;
}

TeferiStats& TeferiStats::operator+=(const TeferiStats& o) {
    cast += o.cast;
    race_turns += o.race_turns;
    long_turns += o.long_turns;
    removed += o.removed;
    return *this;
}

TeferiStats count_teferi(const GameLog& log, const std::map<std::size_t, std::string>& rules, CardDefId teferi) {
    constexpr std::string_view kRace = " [corsa]";
    constexpr std::string_view kLong = " [partita lunga]";
    TeferiStats t;
    PlayerId active = kNoPlayer;
    bool turn_classified = false;  // il turno di Rakdos in corso ha gia' avuto una decisione con modalita'
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind == LogEntry::Kind::StepStart) {
            if (e.step == Step::Untap) turn_classified = false;
            active = e.player;
        } else if (e.kind == LogEntry::Kind::ZoneChange && e.def == teferi) {
            if (e.player == 0 && e.to == Zone::Stack && e.cause == Cause::Cast) ++t.cast;
            if (e.from == Zone::Battlefield && e.to == Zone::Graveyard) ++t.removed;
        } else if (e.kind == LogEntry::Kind::Action && e.player == 1 && active == 1 && !turn_classified) {
            const auto it = rules.find(i);
            if (it == rules.end()) continue;
            const std::string_view rule = it->second;
            if (rule.ends_with(kRace)) {
                ++t.race_turns;
                turn_classified = true;
            } else if (rule.ends_with(kLong)) {
                ++t.long_turns;
                turn_classified = true;
            }
        }
    }
    return t;
}

FirstTurnSeize first_turn_thoughtseize(const GameLog& log, CardDefId thoughtseize) {
    FirstTurnSeize f;
    bool started = false;  // il primo turno di Rakdos e' cominciato
    std::uint16_t turn = 0;
    for (const LogEntry& e : log.entries) {
        if (e.kind == LogEntry::Kind::StepStart && e.step == Step::Untap && e.player == 1) {
            if (started) break;  // il secondo turno di Rakdos: fine
            started = true;
            turn = e.turn;
        }
        if (!started || e.turn != turn || e.kind != LogEntry::Kind::ZoneChange) continue;
        if (e.player == 1 && e.def == thoughtseize && e.to == Zone::Stack && e.cause == Cause::Cast) f.cast = true;
        // Nel turno di Rakdos UW scarta solo per Thoughtseize.
        if (f.cast && f.taken == kNoDef && e.player == 0 && e.from == Zone::Hand && e.to == Zone::Graveyard &&
            e.cause == Cause::Discard) {
            f.taken = e.def;
        }
    }
    return f;
}

}  // namespace mtg
