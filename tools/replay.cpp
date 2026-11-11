#include "replay.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <vector>

namespace mtg {

namespace {

const char* step_name(Step st) {
    switch (st) {
        case Step::PreGame: return "pre-partita";
        case Step::Untap: return "stap";
        case Step::Upkeep: return "mantenimento";
        case Step::Draw: return "pescata";
        case Step::Main1: return "prima fase principale";
        case Step::BeginCombat: return "inizio combattimento";
        case Step::DeclareAttackers: return "attaccanti";
        case Step::DeclareBlockers: return "bloccanti";
        case Step::FirstStrikeDamage: return "danno da attacco improvviso";
        case Step::CombatDamage: return "danno";
        case Step::EndCombat: return "fine combattimento";
        case Step::Main2: return "seconda fase principale";
        case Step::End: return "sottofase finale";
        case Step::Cleanup: return "cancellazione";
    }
    return "?";
}

const char* zone_name(Zone z) {
    switch (z) {
        case Zone::None: return "nessuna";
        case Zone::Library: return "grimorio";
        case Zone::Hand: return "mano";
        case Zone::Battlefield: return "campo";
        case Zone::Graveyard: return "cimitero";
        case Zone::Exile: return "esilio";
        case Zone::Stack: return "pila";
    }
    return "?";
}

const char* decision_name(DecisionKind k) {
    switch (k) {
        case DecisionKind::None: return "nessuna";
        case DecisionKind::Mulligan: return "mulligan";
        case DecisionKind::BottomCards: return "carte in fondo";
        case DecisionKind::Priority: return "priorita'";
        case DecisionKind::PayLifeOrTapped: return "vita o terra tappata";
        case DecisionKind::SearchLibrary: return "ricerca nel grimorio";
        case DecisionKind::Surveil: return "sorveglianza";
        case DecisionKind::DiscardToHandSize: return "scarto a fine turno";
        case DecisionKind::LegendRule: return "regola delle leggende";
        case DecisionKind::ChooseTarget: return "bersaglio";
        case DecisionKind::PayMana: return "pagamento del mana";
        case DecisionKind::ChooseFromHand: return "carta dalla mano rivelata";
        case DecisionKind::DeclareAttacker: return "attacco";
        case DecisionKind::DeclareBlockers: return "blocco";
        case DecisionKind::DivideDamage: return "divisione del danno";
        case DecisionKind::OrderTriggers: return "ordine dei trigger";
        case DecisionKind::ExileFromGraveyard: return "esilio dal cimitero";
        case DecisionKind::DiscardCard: return "scarto";
        case DecisionKind::SacrificeForCost: return "sacrificio come costo";
        case DecisionKind::SacrificeForEffect: return "sacrificio per effetto";
        case DecisionKind::PayOptionalCost: return "costo facoltativo";
        case DecisionKind::ChooseMode: return "modo";
        case DecisionKind::ChooseX: return "valore di X";
        case DecisionKind::ExileFromHandForCost: return "carta dalla mano da esiliare come costo";
        case DecisionKind::CastFromExile: return "lancio dall'esilio";
        case DecisionKind::ChooseReplicate: return "replicare";
        case DecisionKind::GameOver: return "fine";
    }
    return "?";
}

const char* cause_name(Cause c) {
    switch (c) {
        case Cause::Unspecified: return "";
        case Cause::Draw: return "pescata";
        case Cause::Mill: return "macinata";
        case Cause::Discard: return "scartata";
        case Cause::Destroy: return "distrutta";
        case Cause::Sacrifice: return "sacrificata";
        case Cause::Exile: return "esiliata";
        case Cause::Cost: return "costo";
        case Cause::StateBased: return "azione di stato";
        case Cause::Resolve: return "risoluzione";
        case Cause::Play: return "giocata";
        case Cause::Cast: return "lanciata";
        case Cause::Fizzle: return "neutralizzata";
        case Cause::Search: return "cercata";
        case Cause::Surveil: return "sorveglianza";
        case Cause::Mulligan: return "mulligan";
        case Cause::Damage: return "danno";
        case Cause::Counter: return "neutralizzata";
        case Cause::Test: return "test";
    }
    return "";
}

const char* type_name(CardType t) {
    switch (t) {
        case CardType::Artifact: return "artefatto";
        case CardType::Battle: return "battaglia";
        case CardType::Creature: return "creatura";
        case CardType::Enchantment: return "incantesimo";
        case CardType::Instant: return "istantaneo";
        case CardType::Kindred: return "tribale";
        case CardType::Land: return "terra";
        case CardType::Planeswalker: return "planeswalker";
        case CardType::Sorcery: return "stregoneria";
        case CardType::Count: break;
    }
    return "?";
}

const char* method_name(CastMethod m) {
    switch (m) {
        case CastMethod::Normal: return "normale";
        case CastMethod::Escape: return "fuga";
        case CastMethod::Bestow: return "conferimento";
        case CastMethod::SacrificeAlternative: return "costo alternativo";
        case CastMethod::Warp: return "warp";
        case CastMethod::Evoke: return "evocazione";
        case CastMethod::Free: return "senza pagare";
        case CastMethod::Pitch: return "costo alternativo (carta blu)";
    }
    return "?";
}

// Che cosa fa un'abilita' innescata o attivata in pila, per chi legge il replay.
const char* effect_name(StackEffect e) {
    switch (e) {
        case StackEffect::None: return "";
        case StackEffect::FetchSearch: return "ricerca della fetch";
        case StackEffect::Surveil: return "sorveglianza";
        case StackEffect::Draw: return "pescata";
        case StackEffect::Mill: return "macinata";
        case StackEffect::LookAtTop: return "guarda la cima del grimorio";
        case StackEffect::RemoveMinusCounter: return "toglie un segnalino -1/-1";
        case StackEffect::CycleSearch: return "ciclo della terra";
        case StackEffect::Rummage: return "scarta e pesca";
        case StackEffect::UntapAndExtraCombat: return "stappa e combattimento aggiuntivo";
        case StackEffect::EndStepCats: return "gatto di fine turno";
        case StackEffect::GainLifeAndEnergy: return "vita ed energia";
        case StackEffect::EnergyAttack: return "pagare le energie";
        case StackEffect::AttackCounters: return "segnalini sull'attaccante";
        case StackEffect::DestroyTarget: return "distrugge il bersaglio";
        case StackEffect::SearchToHand: return "ricerca in mano";
        case StackEffect::ExileBlackRedGraveyards: return "esilia i cimiteri neri e rossi";
        case StackEffect::WarpExile: return "esilio del warp";
        case StackEffect::ExileOtherCreature: return "esilia una creatura";
        case StackEffect::EvokeSacrifice: return "sacrificio dell'evocazione";
        case StackEffect::PheliaExile: return "esilio di Phelia";
        case StackEffect::PheliaReturn: return "ritorno di Phelia";
        case StackEffect::ExileUntilLeaves: return "esilio finche' resta in campo";
        case StackEffect::ReboundCast: return "rimbalzo";
        case StackEffect::ReplicateCopies: return "copie per replicare";
        case StackEffect::SorceriesAsFlash: return "stregonerie con lampo";
        case StackEffect::BounceAndDraw: return "rimanda in mano e pesca";
    }
    return "";
}

// Significato di ChooseOption per tipo di decisione.
std::string option_name(DecisionKind k, std::uint8_t v) {
    switch (k) {
        case DecisionKind::PayLifeOrTapped: return v == kPayLife ? "paga la vita" : "entra tappata";
        case DecisionKind::Surveil: return v == kSurveilToGraveyard ? "nel cimitero" : "resta in cima";
        case DecisionKind::SearchLibrary: return "non cerca nulla";
        case DecisionKind::DeclareAttacker: return "non attacca";
        case DecisionKind::DeclareBlockers: return "fine dei bloccanti";
        case DecisionKind::PayMana: return "modo di pagare " + std::to_string(v);
        case DecisionKind::DivideDamage: return "ripartizione " + std::to_string(v);
        case DecisionKind::OrderTriggers: return "trigger " + std::to_string(v) + " in pila per primo";
        default: return "opzione " + std::to_string(v);
    }
}

class Writer {
public:
    Writer(std::ostream& out, const CardDatabase& db, const ReplayInfo& info) : out_(out), db_(db), info_(info) {
        def_of_.fill(kNoDef);
        zone_.fill(Zone::Library);
        life_.fill(20);
    }

    void write(const GameLog& log) {
        nlohmann::ordered_json head;
        head["evento"] = "partita";
        head["seed"] = info_.seed;
        head["giocatori"] = info_.players;
        emit(head);
        for (std::size_t i = 0; i < log.entries.size(); ++i) {
            const LogEntry& e = log.entries[i];
            if (e.kind != LogEntry::Kind::ZoneChange) flush_graveyards();
            switch (e.kind) {
                case LogEntry::Kind::StepStart: step(e); break;
                case LogEntry::Kind::ZoneChange: move(e); break;
                case LogEntry::Kind::LifeChange: life(e); break;
                case LogEntry::Kind::Action: action(i, e); break;
                case LogEntry::Kind::AutoPass:
                case LogEntry::Kind::AutoPay: break;
            }
        }
        flush_graveyards();
        nlohmann::ordered_json end;
        end["evento"] = "fine";
        end["vincitore"] = info_.winner == kNoPlayer ? nlohmann::ordered_json() : nlohmann::ordered_json(player(info_.winner));
        end["esito"] = info_.result;
        emit(end);
    }

private:
    void emit(const nlohmann::ordered_json& j) { out_ << j.dump() << '\n'; }
    std::string player(PlayerId p) const {
        return p < kNumPlayers && !info_.players[p].empty() ? info_.players[p] : "giocatore " + std::to_string(p);
    }
    std::string card(CardDefId d) const { return d == kNoDef ? "?" : db_.get(d).name; }
    nlohmann::ordered_json base(const char* event, const LogEntry& e) const {
        nlohmann::ordered_json j;
        j["evento"] = event;
        j["turno"] = e.turn;
        j["fase"] = step_name(e.step);
        return j;
    }

    void step(const LogEntry& e) {
        if (e.step == Step::Untap) {
            nlohmann::ordered_json j;
            j["evento"] = "turno";
            j["turno"] = e.turn;
            j["attivo"] = player(e.player);
            j["vita"] = {{player(0), life_[0]}, {player(1), life_[1]}};
            emit(j);
        } else if (e.step == Step::BeginCombat) {
            nlohmann::ordered_json j = base("combattimento", e);
            emit(j);
        }
    }

    void move(const LogEntry& e) {
        const CardId c = e.action.card;
        def_of_[c] = e.def;
        const Zone from = e.from;
        zone_[c] = e.to;
        PlayerZoneSet& z = zones_[e.player];
        if (from == Zone::Hand) std::erase(z.hand, c);
        if (from == Zone::Graveyard) {
            std::erase(z.graveyard, c);
            dirty_[e.player] = true;
            dirty_turn_[e.player] = e.turn;
        }
        if (e.to == Zone::Hand) z.hand.push_back(c);
        if (e.to == Zone::Graveyard) {
            z.graveyard.push_back(c);
            dirty_[e.player] = true;
            dirty_turn_[e.player] = e.turn;
        }
        if (e.turn == 0) return;  // mani iniziali e mulligan: le mostra l'evento "mano"
        if (e.from == Zone::Library && e.to == Zone::Hand && e.cause == Cause::Draw) {
            nlohmann::ordered_json j = base("pescata", e);
            j["giocatore"] = player(e.player);
            j["carta"] = card(e.def);
            emit(j);
            return;
        }
        nlohmann::ordered_json j = base("spostamento", e);
        j["giocatore"] = player(e.player);
        j["carta"] = card(e.def);
        j["da"] = zone_name(e.from);
        j["a"] = zone_name(e.to);
        j["causa"] = cause_name(e.cause);
        emit(j);
    }

    void flush_graveyards() {
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            if (!dirty_[p]) continue;
            dirty_[p] = false;
            if (dirty_turn_[p] == 0) continue;  // mani iniziali e mulligan: nessun riepilogo
            TypeMask present = 0;
            int mana_value = 0;
            for (const CardId c : zones_[p].graveyard) {
                if (c >= kMaxCards) continue;  // i token non contano (e spariscono subito)
                present = static_cast<TypeMask>(present | db_.get(def_of_[c]).types);
                mana_value += db_.get(def_of_[c]).mana_value;
            }
            std::vector<std::string> types;
            for (std::size_t t = 0; t < kNumCardTypes; ++t) {
                if ((present & type_bit(static_cast<CardType>(t))) != 0) types.emplace_back(type_name(static_cast<CardType>(t)));
            }
            nlohmann::ordered_json j;
            j["evento"] = "cimitero";
            j["turno"] = dirty_turn_[p];
            j["giocatore"] = player(p);
            j["carte"] = zones_[p].graveyard.size();
            j["tipi"] = types;
            j["valore_di_mana"] = mana_value;
            j["delirium"] = std::popcount(present) >= 4;
            emit(j);
        }
    }

    void life(const LogEntry& e) {
        life_[e.player] = life_[e.player] + e.amount;
        nlohmann::ordered_json j = base("vita", e);
        j["giocatore"] = player(e.player);
        j["variazione"] = e.amount;
        j["vita"] = life_[e.player];
        j["fonte"] = e.def == kNoDef ? "" : card(e.def);
        j["causa"] = e.cause == Cause::Cost ? "pagamento" : e.cause == Cause::Damage ? "danno" : "perdita";
        emit(j);
    }

    void action(std::size_t index, const LogEntry& e) {
        if (e.decision == DecisionKind::Mulligan) {
            nlohmann::ordered_json hand;
            hand["evento"] = "mano";
            hand["giocatore"] = player(e.player);
            std::vector<std::string> cards;
            for (const CardId c : zones_[e.player].hand) cards.push_back(card(def_of_[c]));
            hand["carte"] = cards;
            hand["mulligan_fatti"] = mulligans_[e.player];
            emit(hand);
            if (e.action.type == ActionType::Mulligan) ++mulligans_[e.player];
        }
        nlohmann::ordered_json j = base("decisione", e);
        j["giocatore"] = player(e.player);
        j["tipo"] = decision_name(e.decision);
        if (e.subject != kNoCard && e.def != kNoDef) j["soggetto"] = card(e.def);
        const Action& a = e.action;
        const std::string chosen = a.card == kNoCard || def_of_[a.card] == kNoDef ? "" : card(def_of_[a.card]);
        switch (a.type) {
            case ActionType::Pass: j["azione"] = "passa"; break;
            case ActionType::PlayLand: j["azione"] = "gioca la terra"; break;
            case ActionType::CastSpell:
                j["azione"] = "lancia";
                j["modo"] = method_name(static_cast<CastMethod>(a.value));
                j["da"] = zone_name(zone_[a.card]);
                break;
            case ActionType::ActivateAbility:
                j["azione"] = zone_[a.card] == Zone::Hand ? "ciclo" : "attiva";
                j["abilita"] = a.value;  // indice dell'abilita' (Teferi: 0 = +1, 1 = -3)
                break;
            case ActionType::TargetPlayer:
                j["azione"] = e.decision == DecisionKind::DeclareAttacker ? "attacca" : "bersaglio";
                j["giocatore_bersaglio"] = player(a.value);
                break;
            case ActionType::TargetObject:
            case ActionType::TargetStackItem: j["azione"] = "bersaglio"; break;
            case ActionType::Keep: j["azione"] = "tiene"; break;
            case ActionType::Mulligan: j["azione"] = "mulligan"; break;
            case ActionType::ChooseCard: j["azione"] = "sceglie"; break;
            case ActionType::ChooseOption: j["azione"] = option_name(e.decision, a.value); break;
        }
        if (!chosen.empty()) j["carta"] = chosen;
        if (e.target_def != kNoDef) {
            nlohmann::ordered_json target;
            target["carta"] = card(e.target_def);
            target["tipo"] = e.target_is_spell ? "magia" : "abilita'";
            if (!e.target_is_spell) target["effetto"] = effect_name(e.target_effect);
            j["bersaglio_in_pila"] = target;
        }
        if (const auto it = info_.rules.find(index); it != info_.rules.end()) j["regola"] = it->second;
        emit(j);
    }

    struct PlayerZoneSet {
        std::vector<CardId> hand;
        std::vector<CardId> graveyard;
    };

    std::ostream& out_;
    const CardDatabase& db_;
    const ReplayInfo& info_;
    std::array<CardDefId, kMaxObjects> def_of_{};
    std::array<Zone, kMaxObjects> zone_{};
    std::array<PlayerZoneSet, kNumPlayers> zones_;
    std::array<bool, kNumPlayers> dirty_{};
    std::array<std::uint16_t, kNumPlayers> dirty_turn_{};  // turno dell'ultimo spostamento che ha cambiato il cimitero
    std::array<int, kNumPlayers> life_{};
    std::array<int, kNumPlayers> mulligans_{};
};

}  // namespace

void write_replay(std::ostream& out, const CardDatabase& db, const GameLog& log, const ReplayInfo& info) {
    Writer(out, db, info).write(log);
}

}  // namespace mtg
