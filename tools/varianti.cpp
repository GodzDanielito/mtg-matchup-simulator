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

int turns_started(const GameState& s, PlayerId p) {
    const int t = s.turn.turn_number;
    return s.turn.starting_player == p ? (t + 1) / 2 : t / 2;
}

namespace {

constexpr PlayerId kRakdosPlayer = 1;
constexpr int kRakdosLifeFloor = 5;  // docs/agente_rakdos.md, sezione 1

}  // namespace

void EarlyRakdosTracker::see(const GameState& s) {
    const int k = turns_started(s, kRakdosPlayer);
    if (k > 3) return;
    int lands = 0;
    for (const CardId c : s.battlefield) {
        if (s.objects[c].controller == kRakdosPlayer && effective_def(s, *db_, c).has_type(CardType::Land)) ++lands;
    }
    if (k <= 2) stats_.lands_by_turn2 = std::max(stats_.lands_by_turn2, lands);
    stats_.lands_by_turn3 = std::max(stats_.lands_by_turn3, lands);
}

void EarlyRakdosTracker::on_decision(const GameState& s, const std::vector<Action>&, const Action& chosen) {
    see(s);
    if (chosen.type == ActionType::CastSpell && s.pending.player == kRakdosPlayer && turns_started(s, kRakdosPlayer) <= 2) {
        ++stats_.spells_before_turn3;
    }
}

void EarlyRakdosTracker::on_end(const GameState& s) { see(s); }

const char* seize_t1_name(SeizeT1 r) {
    switch (r) {
        case SeizeT1::NotInHand: return "non in mano";
        case SeizeT1::Cast: return "lanciata";
        case SeizeT1::CreatureCastable: return "creatura da un mana lanciabile";
        case SeizeT1::NoBlackSource: return "nessuna fonte di mana nera";
        case SeizeT1::LifeFloor: return "soglia di vita";
        case SeizeT1::TrollNoSecondLand: return "ciclo del Troll, nessuna altra terra";
        case SeizeT1::BoltOnSight: return "Bolt a vista su Guide o Ocelot";
        case SeizeT1::NoValidReason: return "senza motivo valido";
    }
    return "?";
}

FirstTurnSeizeTracker::FirstTurnSeizeTracker(const CardDatabase& db)
    : db_(&db),
      seize_(db.id_of("Thoughtseize")),
      troll_(db.id_of("Troll of Khazad-dûm")),
      bolt_(db.id_of("Lightning Bolt")),
      guide_(db.id_of("Guide of Souls")),
      ocelot_(db.id_of("Ocelot Pride")) {}

void FirstTurnSeizeTracker::see_hand(const GameState& s) {
    if (turns_started(s, kRakdosPlayer) != 1) return;
    if (s.pending.kind == DecisionKind::Mulligan || s.pending.kind == DecisionKind::BottomCards) return;
    const PlayerState& me = s.players[kRakdosPlayer];
    if (std::any_of(me.hand.begin(), me.hand.end(), [&](CardId c) { return s.objects[c].def == seize_; })) in_hand_ = true;
}

void FirstTurnSeizeTracker::on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) {
    see_hand(s);
    if (s.pending.player != kRakdosPlayer || s.pending.kind != DecisionKind::Priority) return;
    if (s.turn.active_player != kRakdosPlayer || turns_started(s, kRakdosPlayer) != 1) return;
    if (s.turn.step != Step::Main1 && s.turn.step != Step::Main2) return;
    const PlayerState& me = s.players[kRakdosPlayer];
    if (std::none_of(me.hand.begin(), me.hand.end(), [&](CardId c) { return s.objects[c].def == seize_; })) return;
    auto name = [&](CardId c) { return effective_def(s, *db_, c).name; };
    bool seize_legal = false;
    for (const Action& a : legal) {
        if (a.type != ActionType::CastSpell || a.card == kNoCard || s.objects[a.card].zone != Zone::Hand) continue;
        const CardDef& d = effective_def(s, *db_, a.card);
        if (s.objects[a.card].def == seize_) seize_legal = true;
        if (d.has_type(CardType::Creature) && d.mana_value == 1) one_drop_ = true;
    }
    for (const CardId c : s.battlefield) {
        const CardObject& o = s.objects[c];
        if (o.controller != kRakdosPlayer || o.has_flag(ObjectFlag::Tapped)) continue;
        const CardDef& d = effective_def(s, *db_, c);
        if (d.has_type(CardType::Land) && (mana_colors(d) & color_bit(Color::Black)) != 0) black_ = true;
    }
    if (chosen.type == ActionType::CastSpell && chosen.card != kNoCard && s.objects[chosen.card].def == seize_) {
        cast_ = true;
        return;
    }
    if (!seize_legal) return;
    legal_ = true;
    if (me.life - 2 >= kRakdosLifeFloor) {
        legal_above_floor_ = true;
        // I motivi validi della correzione del turno 1 (sezione 22): il ciclo del Troll senza un'altra terra
        // in mano, Bolt con Guide of Souls o Ocelot Pride di UW in campo.
        const bool from_hand = chosen.card != kNoCard && s.objects[chosen.card].zone == Zone::Hand;
        const CardDefId what = chosen.card == kNoCard ? kNoDef : s.objects[chosen.card].def;
        const bool land_in_hand = std::any_of(me.hand.begin(), me.hand.end(), [&](CardId c) {
            return effective_def(s, *db_, c).has_type(CardType::Land);
        });
        const bool on_sight = std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
            return s.objects[c].controller != kRakdosPlayer && (s.objects[c].def == guide_ || s.objects[c].def == ocelot_);
        });
        reason_ = SeizeT1::NoValidReason;
        if (chosen.type == ActionType::ActivateAbility && from_hand && what == troll_ && !land_in_hand) {
            reason_ = SeizeT1::TrollNoSecondLand;
        }
        if (chosen.type == ActionType::CastSpell && from_hand && what == bolt_ && on_sight) reason_ = SeizeT1::BoltOnSight;
    }
    switch (chosen.type) {
        case ActionType::Pass: detail_ = "passa"; break;
        case ActionType::CastSpell: detail_ = "lancia " + name(chosen.card); break;
        case ActionType::ActivateAbility: detail_ = "attiva " + name(chosen.card); break;
        case ActionType::PlayLand: detail_ = "terra " + name(chosen.card); break;
        default: detail_ = "azione " + std::to_string(static_cast<int>(chosen.type)); break;
    }
}

SeizeT1 FirstTurnSeizeTracker::result() const {
    if (!in_hand_) return SeizeT1::NotInHand;
    if (cast_) return SeizeT1::Cast;
    if (one_drop_) return SeizeT1::CreatureCastable;
    if (legal_) return legal_above_floor_ ? reason_ : SeizeT1::LifeFloor;
    return black_ ? SeizeT1::NoValidReason : SeizeT1::NoBlackSource;
}

void SeizePickTracker::on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) {
    if (s.pending.kind != DecisionKind::ChooseFromHand || s.pending.player != kRakdosPlayer) return;
    const CardDefId taken = chosen.card == kNoCard ? kNoDef : s.objects[chosen.card].def;
    if (taken != kNoDef) ++picks_[taken];
    // La scelta dell'agente di base_r1 (X5 spenta) nello stesso stato (un agente nuovo: nessuno stato).
    RakdosAgent base(*db_, 1, RakdosAgentOptions::base_r1());
    const Action b = base.choose(s, legal);
    const CardDefId base_taken = b.card == kNoCard ? kNoDef : s.objects[b.card].def;
    if (base_taken != taken) changed_ = true;
}

// --- Osservatori di UW (docs/agente_uw.md, sezione 29) ----------------------------------------------

void KeptHandTracker::see(const GameState& s) {
    if (done_ || s.pending.kind == DecisionKind::Mulligan || s.pending.kind == DecisionKind::BottomCards) return;
    done_ = true;
    for (const CardId c : s.players[player_].hand) kept_.push_back(s.objects[c].def);
}
void KeptHandTracker::on_decision(const GameState& s, const std::vector<Action>&, const Action&) { see(s); }
void KeptHandTracker::on_end(const GameState& s) { see(s); }

namespace {

constexpr PlayerId kUwPlayer = 0;

int mana_value_of(const ManaCost& c) {
    int n = c.generic + c.colorless;
    for (const std::uint8_t k : c.colored) n += k;
    return n;
}

}  // namespace

WitchLandTracker::WitchLandTracker(const CardDatabase& db) : db_(&db), witch_(db.id_of("Witch Enchanter")) {
    for (const char* name : {"Guide of Souls", "Ocelot Pride", "Sanctifier en-Vec", "Recruiter of the Guard", "Witch Enchanter",
                             "Starfield Shepherd", "Quantum Riddler", "Teferi, Time Raveler", "Seam Rip"}) {
        planned_.push_back(db.id_of(name));
    }
}

void WitchLandTracker::on_decision(const GameState& s, const std::vector<Action>&, const Action& chosen) {
    if (s.pending.player != kUwPlayer) return;
    // Le terre di UW in campo (escluso `skip`), e quante stappate.
    auto lands = [&](CardId skip, bool untapped_only) {
        int n = 0;
        for (const CardId c : s.battlefield) {
            const CardObject& o = s.objects[c];
            if (c == skip || o.controller != kUwPlayer || !effective_def(s, *db_, c).has_type(CardType::Land)) continue;
            if (untapped_only && o.has_flag(ObjectFlag::Tapped)) continue;
            ++n;
        }
        return n;
    };
    // Una giocata prevista in mano (esclusa `skip`) con valore di mana (o warp) in (low, high].
    auto planned_in = [&](CardId skip, int low, int high) {
        for (const CardId c : s.players[kUwPlayer].hand) {
            if (c == skip) continue;
            const CardDef& d = db_->get(s.objects[c].def);
            if (std::find(planned_.begin(), planned_.end(), s.objects[c].def) == planned_.end()) continue;
            std::vector<int> costs = {d.mana_value};
            if (d.rules.warp.cost != ManaCost{}) costs.push_back(mana_value_of(d.rules.warp.cost));
            for (const int mv : costs) {
                if (mv > low && mv <= high) return true;
            }
        }
        return false;
    };
    if (chosen.type == ActionType::PlayLand && chosen.value == 1 && s.objects[chosen.card].def == witch_) {
        ++stats_.plays;
        const PlayerState& me = s.players[kUwPlayer];
        const bool other_land = std::any_of(me.hand.begin(), me.hand.end(), [&](CardId c) {
            return c != chosen.card && db_->get(s.objects[c].def).has_type(CardType::Land);
        });
        const int l = lands(kNoCard, false);
        if (other_land || !planned_in(chosen.card, l, l + 2)) ++stats_.plays_not_d1;  // (a), (b)
        return;
    }
    if (s.pending.kind != DecisionKind::PayLifeOrTapped) return;
    const CardId land = s.pending.subject.card;
    if (land == kNoCard || s.objects[land].def != witch_) return;
    if (chosen.type == ActionType::ChooseOption && chosen.value == kPayLife) {
        ++stats_.paid;
        const int l = lands(land, false);
        if (!planned_in(land, lands(land, true), l + 1)) ++stats_.payments_not_d1;  // (c)
        stats_.lowest_life_after_payment = std::min(stats_.lowest_life_after_payment, s.players[kUwPlayer].life - s.pending.amount);
    } else {
        ++stats_.tapped;
    }
}

int land_life_paid(const GameLog& log, const CardDatabase& db, PlayerId p) {
    const CardDefId witch = db.id_of("Witch Enchanter");
    int paid = 0;
    for (const LogEntry& e : log.entries) {
        if (e.kind != LogEntry::Kind::LifeChange || e.player != p || e.cause != Cause::Cost || e.amount >= 0) continue;
        if (e.def == kNoDef) continue;
        if (e.def == witch || db.get(e.def).has_type(CardType::Land)) paid -= e.amount;
    }
    return paid;
}

}  // namespace mtg
