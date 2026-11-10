#include "goldfish.hpp"

#include "game_setup.hpp"
#include "goldfish_agent.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <set>

namespace mtg {

int rakdos_turn(std::uint16_t turn, bool rakdos_on_play) {
    return rakdos_on_play ? (turn + 1) / 2 : turn / 2;
}

int rakdos_turn_reached(std::uint16_t turn, bool rakdos_on_play) {
    const bool rakdos_active = (turn % 2 == 1) == rakdos_on_play;
    return rakdos_turn(turn, rakdos_on_play) + (rakdos_active ? 0 : 1);
}

namespace {

std::string method_suffix(CastMethod m, Zone from) {
    switch (m) {
        case CastMethod::Normal: return "";
        case CastMethod::Escape: return " (fuga)";
        case CastMethod::Bestow: return from == Zone::Graveyard ? " (conferimento dal cimitero)" : " (conferimento)";
        case CastMethod::SacrificeAlternative: return " (costo alternativo)";
        case CastMethod::Warp: return " (warp)";
        case CastMethod::Evoke: return " (evocazione)";
        case CastMethod::Free: return " (senza pagare)";
    }
    return "";
}

// Misure ricavate dal registro: lo si rilegge evento per evento, ricostruendo mano e
// cimitero di Rakdos (i cambi di zona sono tutti registrati, nell'ordine in cui avvengono).
void measure(const CardDatabase& db, const GameLog& log, std::size_t start, GoldfishGame& g) {
    const PlayerId me = kGoldfishRakdos;
    std::array<Zone, kMaxObjects> zone{};
    zone.fill(Zone::Library);
    std::array<int, kNumCardTypes> per_type{};
    auto types = [&] {
        int n = 0;
        for (const int c : per_type) n += c > 0 ? 1 : 0;
        return n;
    };
    std::set<CardId> hand;
    bool opening_counted = false;
    PlayerId last_active = kNoPlayer;
    bool land_played = false;
    int combats_this_turn = 0;
    auto name = [&](CardDefId d) { return d == kNoDef ? std::string("altro") : db.get(d).name; };

    std::array<CardDefId, kMaxObjects> def_of{};
    def_of.fill(kNoDef);
    auto hand_lands = [&] {
        int n = 0;
        for (const CardId c : hand) n += (def_of[c] != kNoDef && db.get(def_of[c]).has_type(CardType::Land)) ? 1 : 0;
        return n;
    };

    for (std::size_t i = start; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        switch (e.kind) {
            case LogEntry::Kind::StepStart:
                if (!opening_counted) {  // la mano iniziale tenuta: viste prima di ogni pescata in partita
                    for (const CardId c : hand) g.kept_hand.push_back(name(def_of[c]));
                    for (const CardId c : hand) ++g.seen[name(def_of[c])];
                    opening_counted = true;
                }
                if (e.step == Step::Untap) {
                    if (last_active == me && e.player != me) {  // fine di un turno di Rakdos
                        g.types_end_of_turn.push_back(types());
                        if (!land_played) {
                            ++g.turns_without_land;
                        } else if (hand_lands() > 0) {
                            ++g.turns_with_extra_lands;
                        }
                    }
                    last_active = e.player;
                    land_played = false;
                    combats_this_turn = 0;
                }
                if (e.step == Step::BeginCombat && e.player == me) {
                    if (combats_this_turn > 0) ++g.extra_combats;
                    ++combats_this_turn;
                }
                break;
            case LogEntry::Kind::ZoneChange: {
                if (e.player != me) break;
                const CardId c = e.action.card;
                def_of[c] = e.def;
                const CardDef& d = db.get(e.def);
                if (e.from == Zone::Hand) hand.erase(c);
                if (e.to == Zone::Hand) {
                    hand.insert(c);
                    if (opening_counted && e.from == Zone::Library) ++g.seen[d.name];
                }
                const bool token = c >= kMaxCards;
                if (!token && e.from == Zone::Graveyard) {
                    for (std::size_t t = 0; t < kNumCardTypes; ++t) {
                        if ((d.types & type_bit(static_cast<CardType>(t))) != 0) --per_type[t];
                    }
                }
                if (!token && e.to == Zone::Graveyard) {
                    for (std::size_t t = 0; t < kNumCardTypes; ++t) {
                        if ((d.types & type_bit(static_cast<CardType>(t))) != 0) ++per_type[t];
                    }
                    if (g.delirium_turn == 0 && types() >= 4) g.delirium_turn = rakdos_turn_reached(e.turn, g.on_play);
                }
                zone[c] = e.to;
                break;
            }
            case LogEntry::Kind::LifeChange:
                if (e.amount >= 0) break;
                if (e.player != me && e.cause == Cause::Damage) g.damage[name(e.def)] -= e.amount;
                if (e.player == me && (e.cause == Cause::Cost || e.cause == Cause::Resolve)) {
                    g.life_paid[name(e.def)] -= e.amount;
                }
                break;
            case LogEntry::Kind::Action: {
                if (e.player != me) break;
                const Action& a = e.action;
                if (e.decision == DecisionKind::Mulligan) g.mulligan_decisions.push_back(a.type == ActionType::Keep ? 'K' : 'M');
                if (a.type == ActionType::PlayLand) land_played = true;
                if (a.type == ActionType::CastSpell) {
                    const auto m = static_cast<CastMethod>(a.value);
                    g.used[name(def_of[a.card]) + method_suffix(m, zone[a.card])] += 1;
                    if (m == CastMethod::Bestow) ++g.bestows;
                }
                if (a.type == ActionType::ActivateAbility) {
                    const char* how = zone[a.card] == Zone::Hand ? " (ciclo)" : " (attivazione)";
                    g.used[name(def_of[a.card]) + how] += 1;
                }
                break;
            }
            case LogEntry::Kind::AutoPass:
            case LogEntry::Kind::AutoPay: break;
        }
    }
}

}  // namespace

std::string mulligan_reason(std::string_view rule, int lands_in_hand) {
    if (rule == "mulligan: una terra senza abbastanza carte che cercano") return "una terra senza abbastanza carte che cercano";
    if (rule == "mulligan: nessuna creatura economica lanciabile") return "nessuna creatura da 1-2 mana";
    if (rule == "mulligan: terre fuori da 2-4") {
        // Le terre si contano dopo le carte in fondo, che tolgono terre solo da mani con piu' di 3:
        // zero o una terra nella mano pescata sono zero o una anche dopo (una: solo nella variante A).
        if (lands_in_hand == 0) return "zero terre";
        if (lands_in_hand == 1) return "una terra senza abbastanza carte che cercano";
        return "cinque o piu' terre";
    }
    return "altro";
}

GoldfishGame play_goldfish(const GoldfishConfig& config, std::uint64_t seed, GameLog* log_out,
                           std::map<std::size_t, std::string>* rules, std::map<std::string, std::uint64_t>* rule_counts) {
    const CardDatabase& db = *config.db;
    GameLog local;
    GameLog& log = log_out != nullptr ? *log_out : local;
    const std::size_t start = log.entries.size();

    GoldfishGame g;
    g.seed = seed;
    g.on_play = seed % 2 == 1;
    GameState s = create_game(db, config.rakdos, config.goldfish, seed);
    start_game(s, db, StartOptions{g.on_play ? kGoldfishRakdos : kGoldfishOpponent, true}, &log);
    RakdosAgent agent(db, seed, config.agent);
    const GoldfishAgent fish;

    while (s.pending.kind != DecisionKind::GameOver && rakdos_turn(s.turn.turn_number, g.on_play) <= config.turn_limit) {
        const std::vector<Action> actions = legal_actions(s, db);
        Action a;
        if (s.pending.player == kGoldfishRakdos) {
            a = agent.choose(s, actions);
            ++g.decisions;
            if (s.pending.kind == DecisionKind::Mulligan && a.type == ActionType::Mulligan) {
                int lands = 0;
                for (const CardId c : s.players[kGoldfishRakdos].hand) {
                    lands += db.get(s.objects[c].def).has_type(CardType::Land) ? 1 : 0;
                }
                g.mulligan_reasons.push_back(mulligan_reason(agent.last_rule(), lands));
            }
            if (agent.last_rule() == RakdosAgent::kDefaultRule) ++g.default_decisions;
            if (rules != nullptr) (*rules)[log.entries.size()] = std::string(agent.last_rule());
        } else {
            a = fish.choose(s, actions);
        }
        apply(s, db, a, &log);
    }

    if (s.pending.kind == DecisionKind::GameOver) {
        g.result = s.turn.winner == kGoldfishRakdos ? GoldfishResult::Won : GoldfishResult::Lost;
    } else {
        g.result = GoldfishResult::TurnLimit;
    }
    g.rakdos_turns = std::min(rakdos_turn(s.turn.turn_number, g.on_play), config.turn_limit);
    if (g.result == GoldfishResult::Won) g.win_turn = rakdos_turn(s.turn.turn_number, g.on_play);
    g.mulligans = s.players[kGoldfishRakdos].mulligans;
    g.life_end = s.players[kGoldfishRakdos].life;
    measure(db, log, start, g);
    if (rule_counts != nullptr) {
        for (const auto& [rule, n] : agent.rule_counts()) (*rule_counts)[rule] += n;
    }
    return g;
}

std::string goldfish_game_json(const GoldfishGame& g) {
    nlohmann::ordered_json j;
    j["seed"] = g.seed;
    j["in_play"] = g.on_play;
    j["mulligan"] = g.mulligans;
    j["mulligan_scelte"] = g.mulligan_decisions;
    j["mano_tenuta"] = g.kept_hand;
    j["mulligan_motivi"] = g.mulligan_reasons;
    j["esito"] = g.result == GoldfishResult::Won ? "vinta" : g.result == GoldfishResult::Lost ? "persa" : "limite";
    j["turno_vittoria"] = g.win_turn;
    j["turno_delirium"] = g.delirium_turn;
    j["turni"] = g.rakdos_turns;
    j["vita_finale"] = g.life_end;
    j["vita_pagata"] = g.life_paid;
    j["danno"] = g.damage;
    j["viste"] = g.seen;
    j["usate"] = g.used;
    j["tipi_fine_turno"] = g.types_end_of_turn;
    j["turni_senza_terra"] = g.turns_without_land;
    j["turni_con_terre_in_piu"] = g.turns_with_extra_lands;
    j["combattimenti_aggiuntivi"] = g.extra_combats;
    j["conferimenti"] = g.bestows;
    j["decisioni"] = g.decisions;
    j["decisioni_predefinite"] = g.default_decisions;
    return j.dump();
}

}  // namespace mtg
