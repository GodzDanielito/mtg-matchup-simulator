#include "game.hpp"

#include "combat.hpp"
#include "mana.hpp"
#include "zones.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace mtg {

namespace {

constexpr std::array<PlayerId, kNumPlayers> turn_order(PlayerId first) {
    return {first, static_cast<PlayerId>(1 - first)};
}

PlayerId other(PlayerId p) { return static_cast<PlayerId>(1 - p); }

constexpr std::uint8_t kAllPlayers = (1u << kNumPlayers) - 1u;

bool is_main_phase(Step st) { return st == Step::Main1 || st == Step::Main2; }

PendingDecision decision(DecisionKind kind, PlayerId player) {
    PendingDecision d;
    d.kind = kind;
    d.player = player;
    return d;
}

void record(GameLog* log, const GameState& s, LogEntry::Kind kind, PlayerId player, DecisionKind dk,
            const Action& a) {
    if (log != nullptr) log->entries.push_back(LogEntry{kind, player, s.turn.turn_number, s.turn.step, dk, a});
}

std::span<const ObjRef> one(const ObjRef& r) { return {&r, 1}; }

// Mette in pila una magia o un'abilità con un id nuovo: il bersaglio di Consign to Memory e di
// Force of Negation resta lo stesso oggetto anche se sotto di lui la pila cambia.
void push_stack(GameState& s, StackItem item) {
    if (++s.next_stack_id == 0) ++s.next_stack_id;  // 0 = nessun id
    item.id = s.next_stack_id;
    s.stack_items.push_back(item);
}

// Registro attivo durante start_game e apply. Cambi di zona, vita e inizio degli step
// avvengono in profondità nel motore, dove il GameLog non arriva come parametro: lo
// si rende raggiungibile per la durata della chiamata pubblica, poi si ripristina.
thread_local GameLog* t_log = nullptr;

class LogScope {
public:
    explicit LogScope(GameLog* log) : previous_(t_log) { t_log = log; }
    ~LogScope() { t_log = previous_; }
    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;

private:
    GameLog* previous_;
};

void log_event(const GameState& s, LogEntry e) {
    if (t_log == nullptr) return;
    e.turn = s.turn.turn_number;
    e.step = s.turn.step;
    t_log->entries.push_back(e);
}

void log_moves(const GameState& s, const ZoneChangeBatch& batch) {
    if (t_log == nullptr) return;
    for (const ZoneChange& m : batch.moves) {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = m.owner;
        e.action.card = m.card;
        e.def = m.def;
        e.from = m.from;
        e.to = m.to;
        e.cause = batch.cause;
        log_event(s, e);
    }
}

void log_life(const GameState& s, PlayerId p, int delta, CardId source, Cause cause) {
    if (t_log == nullptr || delta == 0) return;
    LogEntry e;
    e.kind = LogEntry::Kind::LifeChange;
    e.player = p;
    e.action.card = source;
    e.def = source == kNoCard ? kNoDef : s.objects[source].def;
    e.cause = cause;
    e.amount = static_cast<std::int16_t>(delta);
    log_event(s, e);
}

// --- Trigger -------------------------------------------------------------------

StackItem triggered(StackEffect effect, PlayerId controller, ObjRef source, CardDefId source_def, std::uint8_t amount) {
    StackItem t;
    t.kind = StackKind::TriggeredAbility;
    t.effect = effect;
    t.controller = controller;
    t.source = source;
    t.source_def = source_def;
    t.amount = amount;
    return t;
}

TargetKind target_kind_of(const StackItem& item, const CardDatabase& db);
// Tutto ciò che decide se un oggetto è un bersaglio legale di una magia o di un'abilità.
struct TargetContext {
    TargetKind kind = TargetKind::None;
    ColorMask colors = 0;          // colori della fonte (protezione, 702.16b)
    PlayerId chooser = kNoPlayer;  // chi controlla la magia o l'abilità ("you control", "an opponent controls")
    ObjRef source;                 // la fonte: "other target" la esclude
    std::uint8_t x = 0;            // "with mana value X or less"
    bool up_to_one = false;        // "up to one": si può scegliere nessun bersaglio (115.6)
    std::uint16_t self_id = 0;     // la magia o abilità stessa: non è un bersaglio di se stessa (115.5)
};
TargetContext target_context(const StackItem& item, const CardDatabase& db);
void target_actions(const GameState& s, const CardDatabase& db, const TargetContext& c, std::vector<Action>& out);
bool is_creature_on_battlefield(const GameState& s, const CardDatabase& db, CardId id);

// Esamina un evento di gruppo e mette in attesa i trigger che fa scattare.
void collect_triggers(GameState& s, const CardDatabase& db, const ZoneChangeBatch& batch) {
    for (const ZoneChange& m : batch.moves) {
        if (m.to == Zone::Battlefield) {
            // "When this ... enters": le caratteristiche sono quelle con cui è entrato (retro compreso).
            const CardDef& d = effective_def(s, db, m.card);
            const PlayerId controller = s.objects[m.card].controller;
            if (d.rules.land.surveil_on_enter > 0) {
                s.pending_triggers.push_back(
                    triggered(StackEffect::Surveil, controller, m.new_ref, d.id, d.rules.land.surveil_on_enter));
            }
            if (d.rules.triggers.mill_on_enter > 0) {
                s.pending_triggers.push_back(
                    triggered(StackEffect::Mill, controller, m.new_ref, d.id, d.rules.triggers.mill_on_enter));
            }
            if (d.rules.triggers.rummage_on_enter) {
                s.pending_triggers.push_back(triggered(StackEffect::Rummage, controller, m.new_ref, d.id, 1));
            }
            if (d.rules.triggers.search_on_enter) {
                StackItem t = triggered(StackEffect::SearchToHand, controller, m.new_ref, d.id, 0);
                t.search = d.rules.triggers.search;
                s.pending_triggers.push_back(t);
            }
            if (d.rules.triggers.destroy_artifact_enchantment_on_enter) {
                s.pending_triggers.push_back(triggered(StackEffect::DestroyTarget, controller, m.new_ref, d.id, 0));
            }
            if (d.rules.triggers.exile_black_red_graveyards_on_enter) {
                s.pending_triggers.push_back(
                    triggered(StackEffect::ExileBlackRedGraveyards, controller, m.new_ref, d.id, 0));
            }
            if (d.rules.triggers.draw_on_enter > 0) {
                s.pending_triggers.push_back(
                    triggered(StackEffect::Draw, controller, m.new_ref, d.id, d.rules.triggers.draw_on_enter));
            }
            if (d.rules.triggers.exile_other_creature_on_enter) {
                s.pending_triggers.push_back(triggered(StackEffect::ExileOtherCreature, controller, m.new_ref, d.id, 0));
            }
            if (d.rules.triggers.exile_until_leaves_on_enter) {
                s.pending_triggers.push_back(triggered(StackEffect::ExileUntilLeaves, controller, m.new_ref, d.id, 0));
            }
            // Guide of Souls: "Whenever another creature you control enters". Anche una Guide che entra
            // nello stesso evento vede le altre (603.6a); non scatta per se stessa. N creature che entrano
            // prima che i trigger vadano in pila danno N trigger identici e indipendenti della stessa
            // Guide: si tengono come un solo
            // trigger da N vite e N energie (stesso risultato, nessuna scelta tra loro), così decine di
            // gatti copiati con la benedizione della città non riempiono pila e trigger in attesa.
            if (is_creature_on_battlefield(s, db, m.card)) {
                for (const CardId g : s.battlefield) {
                    if (g == m.card || s.objects[g].controller != controller) continue;
                    const CardDef& gd = effective_def(s, db, g);
                    if (!gd.rules.triggers.life_energy_on_other_creature) continue;
                    const ObjRef guide = s.ref(g);
                    auto same = std::find_if(s.pending_triggers.begin(), s.pending_triggers.end(), [&](const StackItem& t) {
                        return t.effect == StackEffect::GainLifeAndEnergy && t.source == guide && t.amount < 255;
                    });
                    if (same != s.pending_triggers.end()) {
                        ++same->amount;
                    } else {
                        s.pending_triggers.push_back(
                            triggered(StackEffect::GainLifeAndEnergy, controller, guide, gd.id, 1));
                    }
                }
            }
        }
        if (m.from == Zone::Battlefield && m.to == Zone::Graveyard) {
            // "When this creature dies": informazioni note per ultime (controllore compreso, 603.10a).
            const CardDef& d = db.get(m.def);
            if (d.has_type(CardType::Creature) && d.rules.triggers.mill_on_death > 0) {
                s.pending_triggers.push_back(
                    triggered(StackEffect::Mill, m.controller, m.new_ref, d.id, d.rules.triggers.mill_on_death));
            }
        }
    }
    // Moonshadow: UNA volta per evento di gruppo, se l'evento porta almeno una carta
    // permanente nel cimitero del suo controllore e lei ha un segnalino -1/-1. Se
    // Moonshadow lascia il campo nello stesso evento non scatta (l'effetto non avrebbe nulla da fare).
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        const CardDef& d = effective_def(s, db, id);
        if (!d.rules.triggers.remove_minus_counter_on_permanent_cards || o.counters.minus_one == 0) continue;
        if (permanent_cards_put_into_graveyard(batch, db, o.controller)) {
            s.pending_triggers.push_back(triggered(StackEffect::RemoveMinusCounter, o.controller, s.ref(id), d.id, 1));
        }
    }
}

// Ogni spostamento del motore passa da qui, così nessun trigger va perso.
ZoneChangeBatch move_and_collect(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                                 Cause cause, LibraryPosition position = LibraryPosition::Unspecified) {
    ZoneChangeBatch batch = move_objects(s, db, refs, to, cause, position);
    log_moves(s, batch);
    for (const ZoneChange& m : batch.moves) {
        // 614.1c: "enters with N counters" si applica mentre entra, prima dei trigger.
        if (m.to != Zone::Battlefield) continue;
        const std::uint8_t n = effective_def(s, db, m.card).rules.statics.enters_with_minus_one;
        if (n > 0) s.objects[m.card].counters.minus_one = n;
    }
    collect_triggers(s, db, batch);
    // 610.3: un permanente che lascia il campo fa tornare subito dopo le carte che tratteneva in esilio
    // "until" (Seam Rip), sotto il controllo del proprietario (610.3c), come oggetti nuovi. I token
    // non tornano (111.8).
    StaticVector<ObjRef, kMaxObjects> back;
    for (const ZoneChange& m : batch.moves) {
        if (m.from != Zone::Battlefield) continue;
        for (const CardId id : s.exile) {
            const CardObject& o = s.objects[id];
            if (o.has_flag(ObjectFlag::ExiledUntil) && !o.has_flag(ObjectFlag::Token) && o.attached_to == m.old_ref) {
                back.push_back(s.ref(id));
            }
        }
    }
    if (!back.empty()) move_and_collect(s, db, {back.begin(), back.size()}, Zone::Battlefield, Cause::Resolve);
    return batch;
}

// Crea i token come un unico evento: si registrano e fanno scattare i trigger di entrata
// (Guide of Souls), come una carta che entra in campo.
void create_tokens(GameState& s, const CardDatabase& db, std::span<const CardDefId> defs, PlayerId controller) {
    if (defs.empty()) return;
    ZoneChangeBatch batch;
    batch.cause = Cause::Resolve;
    for (const CardDefId def : defs) {
        const ObjRef r = create_token(s, db, def, controller);
        batch.moves.push_back(
            ZoneChange{r.card, def, controller, controller, Zone::None, Zone::Battlefield, true, ObjRef{}, r});
    }
    ++s.event_serial;
    log_moves(s, batch);
    collect_triggers(s, db, batch);
}

// Ascesa su un permanente (702.131b): "any time" un giocatore controlla dieci o più permanenti e un
// permanente con ascesa, prende la benedizione della città per il resto della partita. Si controlla
// prima di ogni priorità (con le azioni di stato) e a metà delle risoluzioni che creano permanenti.
void update_city_blessing(GameState& s, const CardDatabase& db) {
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        if (s.players[p].city_blessing) continue;
        int permanents = 0;
        bool ascend = false;
        for (const CardId id : s.battlefield) {
            if (s.objects[id].controller != p) continue;
            ++permanents;
            ascend = ascend || effective_def(s, db, id).rules.statics.ascend;
        }
        if (ascend && permanents >= 10) s.players[p].city_blessing = true;
    }
}

// Il trigger ritardato diventa un'abilità in pila: la fonte è l'oggetto su cui agisce (warp,
// rimbalzo), oppure l'oggetto collegato (Phelia), e allora la carta da riportare va in `target`
// (non è un bersaglio: target_kind_of vale None).
StackItem delayed_item(const DelayedTrigger& d) {
    StackItem t = triggered(d.effect, d.controller, d.object, d.source_def, d.amount);
    if (!d.link.is_none()) {
        t.source = d.link;
        t.target = Target{TargetKindTag::Object, kNoPlayer, d.object};
    }
    return t;
}

// Due trigger dello stesso giocatore sono intercambiabili se fanno esattamente la
// stessa cosa: in quel caso l'ordine in pila non conta e non serve una scelta.
bool interchangeable(const StackItem& a, const StackItem& b) {
    return a.kind == b.kind && a.effect == b.effect && a.amount == b.amount && a.source_def == b.source_def &&
           a.land_types == b.land_types && a.search == b.search && a.target == b.target;
}

// APNAP: prima i trigger del giocatore attivo, poi quelli dell'altro, così
// quelli dell'altro si risolvono per primi.
// Mette in pila un trigger. Se ha un bersaglio, il suo controllore lo sceglie ora
// (decisione ChooseTarget); senza bersagli legali il trigger si toglie dalla pila
// (603.3d). Restituisce false se serve una decisione.
bool push_trigger(GameState& s, const CardDatabase& db, const StackItem& trigger) {
    push_stack(s, trigger);
    s.turn.passes_in_a_row = 0;
    if (target_kind_of(trigger, db) == TargetKind::None) return true;
    std::vector<Action> options;
    target_actions(s, db, target_context(trigger, db), options);
    if (options.empty()) {
        s.stack_items.pop_back();
        return true;
    }
    // "Up to one" senza bersagli possibili: va in pila senza bersaglio, nessuna scelta.
    if (options.size() == 1 && options.front().type == ActionType::ChooseOption) return true;
    PendingDecision d;
    d.kind = DecisionKind::ChooseTarget;
    d.player = trigger.controller;
    d.subject = trigger.source;
    s.pending = d;
    return false;
}

// APNAP: prima i trigger del giocatore attivo, poi quelli dell'altro, così
// quelli dell'altro si risolvono per primi. Se un giocatore ha trigger diversi
// (non intercambiabili), sceglie lui l'ordine (603.3b): decisione OrderTriggers.
void put_triggers_on_stack(GameState& s, const CardDatabase& db) {
    for (const PlayerId p : turn_order(s.turn.active_player)) {
        for (;;) {
            std::size_t first = s.pending_triggers.size();
            bool all_same = true;
            for (std::size_t i = 0; i < s.pending_triggers.size(); ++i) {
                if (s.pending_triggers[i].controller != p) continue;
                if (first == s.pending_triggers.size()) {
                    first = i;
                } else if (!interchangeable(s.pending_triggers[first], s.pending_triggers[i])) {
                    all_same = false;
                }
            }
            if (first == s.pending_triggers.size()) break;
            if (!all_same) {
                s.pending = decision(DecisionKind::OrderTriggers, p);
                return;
            }
            const StackItem trigger = s.pending_triggers[first];
            s.pending_triggers.erase_at(first);
            if (!push_trigger(s, db, trigger)) return;
        }
    }
}

// --- Pescate, macinate, terre -----------------------------------------------------

// Le prime `n` carte dalla cima del grimorio (o meno, se il grimorio è più corto).
StaticVector<ObjRef, kPlayerZoneCapacity> top_cards(const GameState& s, PlayerId p, std::size_t n) {
    const auto& library = s.players[p].library;
    StaticVector<ObjRef, kPlayerZoneCapacity> refs;
    for (std::size_t i = 0; i < n && i < library.size(); ++i) refs.push_back(s.ref(library[library.size() - 1 - i]));
    return refs;
}

// Pesca `n` carte come un unico spostamento. Da un grimorio vuoto non si pesca:
// si segna il tentativo, che l'azione di stato trasforma in sconfitta.
void draw_cards(GameState& s, const CardDatabase& db, PlayerId p, std::size_t n) {
    // Quantum Riddler (121.2a): con una carta in mano o nessuna, l'istruzione di pescare N diventa
    // N + 1, prima delle singole pescate; ogni Riddler si applica una volta (614.5), e la mano è
    // ancora la stessa quando si applica il successivo.
    if (n > 0 && s.players[p].hand.size() <= 1) {
        for (const CardId id : s.battlefield) {
            if (s.objects[id].controller == p && effective_def(s, db, id).rules.statics.draw_one_more_small_hand) ++n;
        }
    }
    const auto refs = top_cards(s, p, n);
    if (refs.size() < n) s.players[p].drew_from_empty_library = true;
    if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Hand, Cause::Draw);
}

// Macina `n` carte: un solo evento di gruppo (Moonshadow ne vedrà uno).
void mill(GameState& s, const CardDatabase& db, PlayerId p, std::size_t n) {
    const auto refs = top_cards(s, p, n);
    if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Graveyard, Cause::Mill);
}

int other_lands_controlled(const GameState& s, const CardDatabase& db, PlayerId p, CardId except) {
    int count = 0;
    for (const CardId id : s.battlefield) {
        if (id != except && s.objects[id].controller == p && effective_def(s, db, id).has_type(CardType::Land)) {
            ++count;
        }
    }
    return count;
}

// Una terra entra in campo (giocata o cercata). Le scelte "mentre entra" sono
// una decisione presa subito, prima di qualsiasi altra cosa.
void land_enters(GameState& s, const CardDatabase& db, ObjRef ref, bool back_face, Cause cause) {
    const ZoneChangeBatch batch = move_objects(s, db, one(ref), Zone::Battlefield, cause);
    log_moves(s, batch);
    CardObject& o = s.objects[ref.card];
    if (back_face) o.set_flag(ObjectFlag::BackFace);
    const LandRules& rules = effective_def(s, db, ref.card).rules.land;

    switch (rules.enters) {
        case EntersTapped::Never: break;
        case EntersTapped::Always: o.set_flag(ObjectFlag::Tapped); break;
        case EntersTapped::IfMoreThanTwoOtherLands:
            if (other_lands_controlled(s, db, o.controller, ref.card) > 2) o.set_flag(ObjectFlag::Tapped);
            break;
        case EntersTapped::PayLifeOrTapped: {
            PendingDecision d = decision(DecisionKind::PayLifeOrTapped, o.controller);
            d.subject = s.ref(ref.card);
            d.amount = rules.life_to_untap;
            s.pending = d;
            break;
        }
    }
    collect_triggers(s, db, batch);  // dopo il flag del retro: i trigger dipendono dalla faccia
}

// --- Bersagli ----------------------------------------------------------------------

// Modo effettivo di una magia: per le modali quello scelto (700.2), altrimenti l'unico.
SpellMode spell_mode(const CardDef& def, const StackItem& item) {
    const SpellRules& spell = def.rules.spell;
    if (spell.mode_count > 0) return spell.modes[item.mode];
    return SpellMode{spell.effect, spell.target};
}

TargetKind target_kind_of(const StackItem& item, const CardDatabase& db) {
    const CardDef& def = db.get(item.source_def);
    switch (item.kind) {
        case StackKind::Spell:
            // 702.103b: lanciata conferita è un'Aura con "enchant creature".
            return item.method == CastMethod::Bestow ? TargetKind::Creature : spell_mode(def, item).target;
        case StackKind::ActivatedAbility:
            // Abilità di fedeltà: il bersaglio è quello dell'abilità scelta (item.mode).
            if (item.effect == StackEffect::SorceriesAsFlash || item.effect == StackEffect::BounceAndDraw) {
                return def.rules.loyalty[item.mode].target;
            }
            return def.rules.activated.target;
        case StackKind::TriggeredAbility:
            switch (item.effect) {
                case StackEffect::UntapAndExtraCombat: return TargetKind::Creature;
                case StackEffect::DestroyTarget: return TargetKind::OpponentArtifactOrEnchantment;
                case StackEffect::AttackCounters: return TargetKind::AttackingCreature;
                case StackEffect::ExileOtherCreature: return TargetKind::OtherCreature;
                case StackEffect::PheliaExile: return TargetKind::OtherNonlandPermanent;
                case StackEffect::ExileUntilLeaves: return TargetKind::OpponentNonlandPermanentMv2;
                default: return TargetKind::None;
            }
    }
    return TargetKind::None;
}

TargetContext target_context(const StackItem& item, const CardDatabase& db) {
    TargetContext c;
    c.kind = target_kind_of(item, db);
    c.colors = db.get(item.source_def).colors;
    c.chooser = item.controller;
    c.source = item.source;
    c.x = item.x;
    c.up_to_one = (item.kind == StackKind::TriggeredAbility &&
                   (item.effect == StackEffect::ExileOtherCreature || item.effect == StackEffect::PheliaExile)) ||
                  item.effect == StackEffect::BounceAndDraw;
    c.self_id = item.id;
    return c;
}

// Detective's Phoenix conferita è un'Aura, non una creatura.
bool is_creature_on_battlefield(const GameState& s, const CardDatabase& db, CardId id) {
    return s.objects[id].zone == Zone::Battlefield && !s.objects[id].has_flag(ObjectFlag::Bestowed) &&
           effective_def(s, db, id).has_type(CardType::Creature);
}

bool is_attacking(const GameState& s, CardId id) {
    return std::any_of(s.combat.attackers.begin(), s.combat.attackers.end(),
                       [&](const Attacker& a) { return a.creature.card == id && s.is_valid(a.creature); });
}

// L'oggetto in campo soddisfa il tipo di bersaglio? (protezione esclusa)
bool object_fits(const GameState& s, const CardDatabase& db, CardId id, const TargetContext& c) {
    const CardObject& o = s.objects[id];
    if (o.zone != Zone::Battlefield) return false;
    const CardDef& d = effective_def(s, db, id);
    const bool creature = is_creature_on_battlefield(s, db, id);
    const bool other = s.ref(id) != c.source;
    const TypeMask artifact_enchantment = type_bit(CardType::Artifact) | type_bit(CardType::Enchantment);
    switch (c.kind) {
        case TargetKind::AnyTarget: return creature || d.has_type(CardType::Planeswalker);  // 115.4
        case TargetKind::Creature: return creature;
        case TargetKind::ArtifactCreatureEnchantment: return creature || (d.types & artifact_enchantment) != 0;
        case TargetKind::AttackingCreature: return creature && is_attacking(s, id);
        case TargetKind::OtherCreature: return creature && other;
        case TargetKind::CreatureYouControl: return creature && o.controller == c.chooser;
        case TargetKind::OpponentArtifactOrEnchantment:
            return o.controller != c.chooser && (d.types & artifact_enchantment) != 0;
        case TargetKind::OtherNonlandPermanent: return !d.has_type(CardType::Land) && other;
        case TargetKind::Enchantment: return d.has_type(CardType::Enchantment);
        case TargetKind::ArtifactCreatureEnchantmentMvX:
            return (creature || (d.types & artifact_enchantment) != 0) && d.mana_value <= c.x;
        case TargetKind::OpponentNonlandPermanentMv2:
            return o.controller != c.chooser && !d.has_type(CardType::Land) && d.mana_value <= 2;
        case TargetKind::None:
        case TargetKind::Player:
        case TargetKind::Players:
        case TargetKind::TriggeredAbilityOrColorlessSpell:
        case TargetKind::NoncreatureSpell: return false;
    }
    return false;
}

bool is_stack_target_kind(TargetKind k) {
    return k == TargetKind::TriggeredAbilityOrColorlessSpell || k == TargetKind::NoncreatureSpell;
}

// Una magia o abilità in pila soddisfa il tipo di bersaglio? Mai se stessa (115.5).
bool stack_item_fits(const CardDatabase& db, const StackItem& item, const TargetContext& c) {
    if (item.id == c.self_id) return false;
    const CardDef& d = db.get(item.source_def);
    switch (c.kind) {
        case TargetKind::TriggeredAbilityOrColorlessSpell:
            // Qualsiasi abilità innescata, anche ritardata e anche propria; una magia solo se incolore.
            return item.kind == StackKind::TriggeredAbility || (item.kind == StackKind::Spell && d.colors == 0);
        case TargetKind::NoncreatureSpell:
            // 702.103b: lanciata conferita, la Phoenix è un'Aura, non una magia creatura.
            return item.kind == StackKind::Spell && (item.method == CastMethod::Bestow || !d.has_type(CardType::Creature));
        default: return false;
    }
}

// L'indice in pila della magia o abilità con quell'id (stack_items.size() se non c'è più).
std::size_t stack_index_of(const GameState& s, std::uint16_t id) {
    for (std::size_t i = 0; i < s.stack_items.size(); ++i) {
        if (s.stack_items[i].id == id) return i;
    }
    return s.stack_items.size();
}

// Il bersaglio è ancora legale alla risoluzione? (608.2b) L'ObjRef scopre se l'oggetto
// ha cambiato zona nel frattempo (400.7). Nessun bersaglio è legale solo se non ne serviva
// uno o se era "up to one".
bool target_still_legal(const GameState& s, const CardDatabase& db, const Target& t, const TargetContext& c,
                        std::uint16_t target_id = 0) {
    switch (t.tag) {
        case TargetKindTag::Player: return t.player < kNumPlayers;
        case TargetKindTag::StackItem: {
            const std::size_t i = stack_index_of(s, target_id);
            return i < s.stack_items.size() && stack_item_fits(db, s.stack_items[i], c);
        }
        case TargetKindTag::Object:
            return s.is_valid(t.object) && object_fits(s, db, t.object.card, c) &&
                   (protection_of(s, db, t.object.card) & c.colors) == 0;  // 702.16b
        case TargetKindTag::None: return c.kind == TargetKind::None || c.up_to_one;
    }
    return false;
}

// Due permanenti indistinguibili come bersagli: stessa carta, stesso controllore, stesso stato.
bool same_permanent_state(const GameState& s, CardId a, CardId b) {
    const CardObject& x = s.objects[a];
    const CardObject& y = s.objects[b];
    return x.def == y.def && x.controller == y.controller && x.damage == y.damage && x.flags == y.flags &&
           x.counters.plus_one == y.counters.plus_one && x.counters.minus_one == y.counters.minus_one &&
           x.counters.keywords == y.counters.keywords && bestowed_aura_count(s, a) == bestowed_aura_count(s, b);
}

// Le scelte di bersaglio: giocatori (TargetPlayer), oggetti (TargetObject), insiemi di giocatori
// ("any number of target players": ChooseOption con un bit per giocatore, insieme non vuoto) e,
// per "up to one", nessun bersaglio (ChooseOption kNoTarget).
void target_actions(const GameState& s, const CardDatabase& db, const TargetContext& c, std::vector<Action>& out) {
    if (c.kind == TargetKind::AnyTarget || c.kind == TargetKind::Player) {
        for (PlayerId p = 0; p < kNumPlayers; ++p) out.push_back(Action{ActionType::TargetPlayer, kNoCard, p});
    }
    if (c.kind == TargetKind::Players) {
        for (unsigned mask = 1; mask < (1u << kNumPlayers); ++mask) {
            out.push_back(Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(mask)});
        }
        return;
    }
    if (is_stack_target_kind(c.kind)) {
        // Ogni magia o abilità in pila è un oggetto a sé: due trigger uguali di fonti diverse no.
        for (std::size_t i = 0; i < s.stack_items.size(); ++i) {
            if (stack_item_fits(db, s.stack_items[i], c)) {
                out.push_back(Action{ActionType::TargetStackItem, kNoCard, static_cast<std::uint8_t>(i)});
            }
        }
        return;
    }
    if (c.kind != TargetKind::None && c.kind != TargetKind::Player) {
        // Due creature sono lo stesso bersaglio solo se hanno lo stesso stato E sono già
        // bersaglio dello stesso numero di abilità in pila: con due trigger da bersagliare,
        // "la stessa creatura due volte" e "una ciascuna" sono scelte diverse.
        auto times_targeted = [&](CardId id) {
            return std::count_if(s.stack_items.begin(), s.stack_items.end(), [&](const StackItem& item) {
                return item.target.tag == TargetKindTag::Object && item.target.object.card == id &&
                       s.is_valid(item.target.object);
            });
        };
        for (const CardId id : s.battlefield) {
            if (!object_fits(s, db, id, c)) continue;
            if ((protection_of(s, db, id) & c.colors) != 0) continue;  // protezione: non bersagliabile
            const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Action& a) {
                return a.type == ActionType::TargetObject && same_permanent_state(s, a.card, id) &&
                       times_targeted(a.card) == times_targeted(id);
            });
            if (!duplicate) out.push_back(Action{ActionType::TargetObject, id, 0});
        }
    }
    if (c.up_to_one) out.push_back(Action{ActionType::ChooseOption, kNoCard, kNoTarget});
}

// --- Ricerca nel grimorio (701.23) ---------------------------------------------------------

// La carta del grimorio soddisfa il filtro? Valgono le caratteristiche del fronte; una costituzione
// definita da un'abilità (Nethergoyf) vale in ogni zona (604.3).
bool matches_search(const GameState& s, const CardDatabase& db, CardId id, const SearchFilter& f) {
    const CardDef& def = db.get(s.objects[id].def);
    if (f.basic_land_types != 0 && def.has_type(CardType::Land) && def.has_supertype(Supertype::Basic) &&
        (def.land_types & f.basic_land_types) != 0) {
        return true;
    }
    if (!f.creature || !def.has_type(CardType::Creature)) return false;
    if (f.max_mana_value >= 0 && def.mana_value > f.max_mana_value) return false;
    if (f.max_toughness >= 0) {
        int tough = def.base_toughness;
        if (def.toughness_star) tough += s.players[s.objects[id].owner].gy.distinct_types();
        if (tough > f.max_toughness) return false;
    }
    return true;
}

// --- Lancio delle magie e attivazione delle abilità (601.2, 602.2) ------------------------

// Velocità stregoneria (307.1): il proprio turno, una fase principale, la pila vuota.
bool sorcery_timing(const GameState& s, PlayerId p) {
    const TurnState& t = s.turn;
    return p == t.active_player && is_main_phase(t.step) && s.stack_items.empty();
}

// Teferi: "Each opponent can cast spells only any time they could cast a sorcery."
bool restricted_to_sorcery_timing(const GameState& s, const CardDatabase& db, PlayerId p) {
    return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) {
        return s.objects[id].controller != p && effective_def(s, db, id).rules.statics.opponents_sorcery_timing;
    });
}

bool castable_now(const GameState& s, const CardDatabase& db, PlayerId p, const CardDef& def) {
    const bool sorcery = sorcery_timing(s, p);
    // 101.2: la restrizione di Teferi batte ogni permesso (istantanei, lampo, Teferi +1, rimbalzo).
    if (!sorcery && restricted_to_sorcery_timing(s, db, p)) return false;
    // Velocità istantaneo: istantanei, lampo (702.8a), stregonerie con il +1 di Teferi.
    if (def.has_type(CardType::Instant) || def.rules.flash) return true;
    if (def.has_type(CardType::Sorcery) && s.players[p].sorceries_as_flash) return true;
    return sorcery;
}

void complete_cast(GameState& s, PlayerId p, bool spell) {
    if (spell) ++s.players[p].spells_cast_this_turn;
    s.turn.passes_in_a_row = 0;
    s.turn.priority_player = p;  // 117.3c: chi ha lanciato o attivato riceve la priorità
}

// Costo di mana di una magia lanciata per il suo costo: {X} diventa generico (107.3a); la riduzione
// per carta bianca esiliata tocca solo il generico (118.7a) e non scende sotto zero (601.2f).
ManaCost spell_mana_cost(const CardDef& def, int x, int exiled_white_cards, int replicate = 0) {
    ManaCost c = def.cost;
    int generic = c.generic + c.x * x + replicate * def.rules.spell.replicate_generic;  // 601.2f: costi aggiuntivi
    generic -= std::min(generic, exiled_white_cards * def.rules.spell.reduction_per_white_card);
    c.generic = static_cast<std::uint8_t>(generic);
    c.x = 0;
    return c;
}

// Costo di mana della magia o dell'abilità in cima alla pila.
ManaCost cost_of(const StackItem& item, const CardDatabase& db) {
    const CardDef& def = db.get(item.source_def);
    if (item.kind == StackKind::Spell) {
        switch (item.method) {
            case CastMethod::Escape: return def.rules.escape.cost;
            case CastMethod::Bestow: return def.rules.bestow.cost;
            case CastMethod::Warp: return def.rules.warp.cost;
            case CastMethod::Evoke:  // costi alternativi senza mana
            case CastMethod::Pitch:
            case CastMethod::Free: return ManaCost{};  // "without paying its mana cost"
            case CastMethod::Normal:
            case CastMethod::SacrificeAlternative:
                return spell_mana_cost(def, item.x, item.exiled_for_cost, item.replicate);
        }
    }
    if (item.effect == StackEffect::CycleSearch) return def.rules.landcycling.cost;
    throw EngineError("cost_of: abilità senza costo di mana");
}

// Dopo il mana, gli altri costi e la fine del lancio o dell'attivazione (601.2h:
// i costi si pagano in qualsiasi ordine).
void after_mana(GameState& s, const CardDatabase& db, PlayerId p) {
    const StackItem& item = s.stack_items.back();
    if (item.kind == StackKind::Spell) {
        if (item.replicate > 0) {
            // 702.56a: "When you cast this spell, if a replicate cost was paid for it, copy it for each
            // time its replicate cost was paid." Le copie nascono con le scelte dell'originale.
            StackItem trigger = triggered(StackEffect::ReplicateCopies, p, item.source, item.source_def, item.replicate);
            trigger.target = item.target;
            trigger.target_id = item.target_id;
            s.pending_triggers.push_back(trigger);
        }
        complete_cast(s, p, true);
        return;
    }
    // Ciclo della terra: si scarta la carta (un evento: Moonshadow lo vede).
    const ObjRef card = item.source;
    const ObjRef discarded = move_and_collect(s, db, one(card), Zone::Graveyard, Cause::Discard).moves[0].new_ref;
    s.stack_items.back().source = discarded;
    complete_cast(s, p, false);
}

// 601.2g-h: abilità di mana e pagamento. Un solo modo di pagare: niente decisione.
void begin_payment(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    const StackItem& item = s.stack_items.back();
    const ManaCost cost = cost_of(item, db);
    const std::vector<PaymentOption> options = payment_options(s, db, p, cost);
    if (options.empty()) throw EngineError("lancio senza modi di pagare: l'azione non doveva essere legale");
    if (options.size() == 1) {
        record(log, s, LogEntry::Kind::AutoPay, p, DecisionKind::PayMana, Action{ActionType::ChooseOption, kNoCard, 0});
        pay_mana(s, db, p, cost, options.front());
        after_mana(s, db, p);
        return;
    }
    PendingDecision d = decision(DecisionKind::PayMana, p);
    d.subject = item.source;
    s.pending = d;
}

// Costi non di mana delle abilità attivate con bersaglio (Mishra's Bauble: {T}, sacrifica).
void pay_ability_costs(GameState& s, const CardDatabase& db, PlayerId p) {
    StackItem& item = s.stack_items.back();
    if (item.effect == StackEffect::SorceriesAsFlash || item.effect == StackEffect::BounceAndDraw) {
        // 606.4: il costo è aggiungere o togliere segnalini fedeltà.
        CardObject& o = s.objects[item.source.card];
        const int cost = db.get(item.source_def).rules.loyalty[item.mode].cost;
        o.counters.loyalty = static_cast<std::uint8_t>(std::clamp(o.counters.loyalty + cost, 0, 255));
        complete_cast(s, p, false);
        return;
    }
    s.objects[item.source.card].set_flag(ObjectFlag::Tapped);
    const ObjRef source = item.source;
    const ObjRef sacrificed = move_and_collect(s, db, one(source), Zone::Graveyard, Cause::Cost).moves[0].new_ref;
    s.stack_items.back().source = sacrificed;  // `item` può non essere più valido dopo lo spostamento
    complete_cast(s, p, false);
}

// --- Esilio dal cimitero come costo: fuga (Nethergoyf) e prove (Phoenix) -----------------
//
// Si offrono solo gli insiemi MINIMALI: almeno la soglia (tipi o valore di mana) e
// nessuna carta superflua. Esiliarne di più è legale ("any number") ma sempre peggio
// per chi lancia. Gli insiemi sono multinsiemi (due Lightning Bolt possono servire
// entrambi alle prove) e si costruiscono una carta alla volta in ordine canonico,
// prendendo sempre le prime copie libere: ogni insieme si raggiunge in un solo modo.

enum class ExileMode : std::uint8_t { CardTypes, ManaValue };

struct ExileRequirement {
    ExileMode mode = ExileMode::CardTypes;
    int need = 0;
};

ExileRequirement exile_requirement(const StackItem& item, const CardDatabase& db) {
    const CardDef& def = db.get(item.source_def);
    if (item.method == CastMethod::Bestow) return {ExileMode::ManaValue, def.rules.bestow.evidence};
    return {ExileMode::CardTypes, def.rules.escape.min_types};
}

int exile_value(const GameState& s, const CardDatabase& db, const std::vector<CardId>& cards, ExileMode mode) {
    if (mode == ExileMode::ManaValue) {
        int sum = 0;
        for (const CardId id : cards) sum += db.get(s.objects[id].def).mana_value;
        return sum;
    }
    TypeMask mask = 0;
    for (const CardId id : cards) mask = static_cast<TypeMask>(mask | db.get(s.objects[id].def).types);
    return std::popcount(mask);
}

// Carte del cimitero (non token, diverse da `except`) in ordine canonico: definizione,
// poi CardId. Le carte di valore di mana 0 non servono mai alle prove.
std::vector<CardId> exile_candidates(const GameState& s, const CardDatabase& db, PlayerId p, ExileMode mode,
                                     CardId except = kNoCard) {
    std::vector<std::pair<CardDefId, CardId>> sorted;
    for (const CardId id : s.players[p].graveyard) {
        if (id == except || s.objects[id].has_flag(ObjectFlag::Token)) continue;
        if (mode == ExileMode::ManaValue && db.get(s.objects[id].def).mana_value == 0) continue;
        sorted.emplace_back(s.objects[id].def, id);
    }
    std::sort(sorted.begin(), sorted.end());
    std::vector<CardId> out;
    for (const auto& [def, id] : sorted) out.push_back(id);
    return out;
}

// Insiemi minimali come elenchi crescenti di posizioni in `cands`. Le copie di una
// definizione si prendono come blocco delle prime k; ci si ferma appena la soglia è
// raggiunta, perché altre carte darebbero insiemi non minimali. Nessuna allocazione
// nei nodi della ricerca: il valore corrente si aggiorna a ogni carta aggiunta.
std::vector<std::vector<int>> minimal_exile_sets(const GameState& s, const CardDatabase& db,
                                                 const std::vector<CardId>& cands, ExileRequirement req) {
    const int n = static_cast<int>(cands.size());
    std::vector<int> value(cands.size());  // valore di mana, o maschera dei tipi
    std::vector<int> group_end(cands.size());
    for (int i = n - 1; i >= 0; --i) {
        const auto ui = static_cast<std::size_t>(i);
        const CardDef& d = db.get(s.objects[cands[ui]].def);
        value[ui] = req.mode == ExileMode::ManaValue ? d.mana_value : d.types;
        const bool same_as_next = i + 1 < n && s.objects[cands[ui + 1]].def == s.objects[cands[ui]].def;
        group_end[ui] = same_as_next ? group_end[ui + 1] : i + 1;
    }
    const bool by_mana = req.mode == ExileMode::ManaValue;
    auto reached = [&](int acc) { return (by_mana ? acc : std::popcount(static_cast<unsigned>(acc))) >= req.need; };

    std::vector<std::vector<int>> out;
    std::vector<int> current;
    auto minimal = [&](int acc) {
        if (by_mana) {  // togliere la carta più piccola è il modo migliore di restare sopra la soglia
            int smallest = acc;
            for (const int i : current) smallest = std::min(smallest, value[static_cast<std::size_t>(i)]);
            return acc - smallest < req.need;
        }
        for (std::size_t j = 0; j < current.size(); ++j) {
            int others = 0;
            for (std::size_t k = 0; k < current.size(); ++k) {
                if (k != j) others |= value[static_cast<std::size_t>(current[k])];
            }
            if (reached(others)) return false;
        }
        return true;
    };
    auto rec = [&](auto&& self, int start, int acc) -> void {
        if (reached(acc)) {
            if (minimal(acc)) out.push_back(current);
            return;
        }
        for (int g = start; g < n; g = group_end[static_cast<std::size_t>(g)]) {
            const int end = group_end[static_cast<std::size_t>(g)];
            // Per i tipi una seconda copia non aggiunge nulla, e nemmeno una carta i cui
            // tipi sono già coperti: l'insieme non sarebbe minimale.
            const int copies = by_mana ? end - g : 1;
            int next = acc;
            int added = 0;
            for (int c = g; c < g + copies; ++c) {
                const int v = value[static_cast<std::size_t>(c)];
                const int grown = by_mana ? next + v : (next | v);
                if (grown == next) break;
                next = grown;
                current.push_back(c);
                ++added;
                self(self, end, next);
            }
            current.resize(current.size() - static_cast<std::size_t>(added));
        }
    };
    rec(rec, 0, 0);
    return out;
}

// Carte offribili come prossima scelta, dato quanto già scelto.
std::vector<CardId> exile_options(const GameState& s, const CardDatabase& db, PlayerId p, const PlayerZone& chosen,
                                  ExileRequirement req) {
    const std::vector<CardId> cands = exile_candidates(s, db, p, req.mode);
    std::vector<int> picked;
    for (const CardId id : chosen) {
        const auto it = std::find(cands.begin(), cands.end(), id);
        if (it == cands.end()) throw EngineError("esilio dal cimitero: scelta non valida");
        picked.push_back(static_cast<int>(it - cands.begin()));
    }
    std::vector<CardId> out;
    for (const std::vector<int>& set : minimal_exile_sets(s, db, cands, req)) {
        if (set.size() <= picked.size() || !std::equal(picked.begin(), picked.end(), set.begin())) continue;
        const CardId next = cands[static_cast<std::size_t>(set[picked.size()])];
        if (std::find(out.begin(), out.end(), next) == out.end()) out.push_back(next);
    }
    return out;
}

// Prosegue la scelta delle carte da esiliare; a insieme completo le esilia (un evento)
// e passa al pagamento del mana.
void continue_graveyard_exile(GameState& s, const CardDatabase& db, PlayerId p, PlayerZone chosen, GameLog* log) {
    const ExileRequirement req = exile_requirement(s.stack_items.back(), db);
    for (;;) {
        const std::vector<CardId> chosen_cards(chosen.begin(), chosen.end());
        if (exile_value(s, db, chosen_cards, req.mode) >= req.need) break;
        const std::vector<CardId> options = exile_options(s, db, p, chosen, req);
        if (options.empty()) throw EngineError("esilio dal cimitero impossibile: l'azione non doveva essere legale");
        if (options.size() == 1) {  // scelta obbligata
            chosen.push_back(options.front());
            continue;
        }
        PendingDecision d = decision(DecisionKind::ExileFromGraveyard, p);
        d.subject = s.stack_items.back().source;
        d.chosen = chosen;
        s.pending = d;
        return;
    }
    StaticVector<ObjRef, kPlayerZoneCapacity> refs;
    for (const CardId id : chosen) refs.push_back(s.ref(id));
    if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Exile, Cause::Cost);
    begin_payment(s, db, p, log);
}

// Valore (tipi o valore di mana) delle carte del cimitero escluso `except`.
int graveyard_value(const GameState& s, const CardDatabase& db, PlayerId p, ExileMode mode, CardId except) {
    return exile_value(s, db, exile_candidates(s, db, p, mode, except), mode);
}

// --- Sacrificio come costo (Flare of Malice) ----------------------------------------

// Creature nere non token del giocatore, una per stato distinto.
std::vector<CardId> black_creatures_for_sacrifice(const GameState& s, const CardDatabase& db, PlayerId p) {
    std::vector<CardId> out;
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller != p || o.has_flag(ObjectFlag::Token) || !is_creature_on_battlefield(s, db, id)) continue;
        if ((effective_def(s, db, id).colors & color_bit(Color::Black)) == 0) continue;
        const bool duplicate =
            std::any_of(out.begin(), out.end(), [&](CardId other_id) { return same_permanent_state(s, other_id, id); });
        if (!duplicate) out.push_back(id);
    }
    return out;
}

void sacrifice_for_cost(GameState& s, const CardDatabase& db, PlayerId p, CardId creature) {
    move_and_collect(s, db, one(s.ref(creature)), Zone::Graveyard, Cause::Cost);  // muore: trigger di morte
    complete_cast(s, p, true);
}

void begin_sacrifice_cost(GameState& s, const CardDatabase& db, PlayerId p) {
    const std::vector<CardId> options = black_creatures_for_sacrifice(s, db, p);
    if (options.empty()) throw EngineError("costo alternativo impossibile: l'azione non doveva essere legale");
    if (options.size() == 1) {
        sacrifice_for_cost(s, db, p, options.front());
        return;
    }
    PendingDecision d = decision(DecisionKind::SacrificeForCost, p);
    d.subject = s.stack_items.back().source;
    s.pending = d;
}

// Il conferimento si può lanciare? (tempi, mana, prove sufficienti, una creatura bersagliabile)
bool can_bestow(const GameState& s, const CardDatabase& db, PlayerId p, CardId card) {
    const CardDef& def = db.get(s.objects[card].def);
    if (def.rules.bestow.evidence == 0 || !def.rules.implemented || !castable_now(s, db, p, def)) return false;
    if (!can_pay(s, db, p, def.rules.bestow.cost)) return false;
    if (graveyard_value(s, db, p, ExileMode::ManaValue, card) < def.rules.bestow.evidence) return false;
    TargetContext c;
    c.kind = TargetKind::Creature;
    c.colors = def.colors;
    c.chooser = p;
    std::vector<Action> targets;
    target_actions(s, db, c, targets);
    return !targets.empty();
}

// Fuori dal campo e dalla pila una bifronte ha le caratteristiche del fronte (Witch Enchanter: bianca).
bool has_color(const GameState& s, const CardDatabase& db, CardId id, Color c) {
    return (db.get(s.objects[id].def).colors & color_bit(c)) != 0;
}

// Carte di quel colore nella mano di `p`, esclusa `except` (la carta che si sta lanciando).
int cards_of_color_in_hand(const GameState& s, const CardDatabase& db, PlayerId p, CardId except, Color c) {
    int n = 0;
    for (const CardId id : s.players[p].hand) n += id != except && has_color(s, db, id, c) ? 1 : 0;
    return n;
}

int white_cards_in_hand(const GameState& s, const CardDatabase& db, PlayerId p, CardId except) {
    return cards_of_color_in_hand(s, db, p, except, Color::White);
}

// Il colore della carta da esiliare: blu per Force of Negation, bianca per l'evocazione e March.
Color hand_exile_color(const StackItem& item) { return item.method == CastMethod::Pitch ? Color::Blue : Color::White; }
bool exiles_exactly_one(const StackItem& item) {
    return item.method == CastMethod::Evoke || item.method == CastMethod::Pitch;
}

// Contesto dei bersagli di una magia non ancora lanciata (o appena messa in pila): X senza limite.
TargetContext spell_context(const CardDef& def, TargetKind kind, PlayerId p, ObjRef spell) {
    TargetContext c;
    c.kind = kind;
    c.colors = def.colors;
    c.chooser = p;
    c.source = spell;
    c.x = 0xFF;
    return c;
}

bool has_targets(const GameState& s, const CardDatabase& db, const TargetContext& c) {
    if (c.kind == TargetKind::None) return true;
    std::vector<Action> out;
    target_actions(s, db, c, out);
    return !out.empty();
}

// Modi con bersagli legali (700.2a), in ordine.
std::vector<std::uint8_t> legal_modes(const GameState& s, const CardDatabase& db, PlayerId p, const CardDef& def,
                                      ObjRef spell) {
    std::vector<std::uint8_t> out;
    for (std::uint8_t m = 0; m < def.rules.spell.mode_count; ++m) {
        if (has_targets(s, db, spell_context(def, def.rules.spell.modes[m].target, p, spell))) out.push_back(m);
    }
    return out;
}

// Valori di X da offrire (March of Otherworldly Light): solo i valori di mana dei possibili bersagli
// che si possono pagare, esiliando se serve tutte le carte bianche della mano. Un X che non è il
// valore di mana di un bersaglio ha gli stessi bersagli dell'X più basso, a un costo maggiore o
// uguale: è dominato, come gli insiemi di esilio non minimali.
std::vector<std::uint8_t> x_options(const GameState& s, const CardDatabase& db, PlayerId p, const CardDef& def,
                                    ObjRef spell, CardId except) {
    std::vector<Action> targets;
    target_actions(s, db, spell_context(def, def.rules.spell.target, p, spell), targets);
    const int whites = def.rules.spell.reduction_per_white_card > 0 ? white_cards_in_hand(s, db, p, except) : 0;
    std::vector<std::uint8_t> out;
    for (const Action& a : targets) {
        if (a.type != ActionType::TargetObject) continue;
        const std::uint8_t mv = effective_def(s, db, a.card).mana_value;
        if (std::find(out.begin(), out.end(), mv) != out.end()) continue;
        if (can_pay(s, db, p, spell_mana_cost(def, mv, whites))) out.push_back(mv);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// La magia ha le scelte che servono per lanciarla? (un modo, un X, un bersaglio)
bool spell_has_legal_choices(const GameState& s, const CardDatabase& db, PlayerId p, const CardDef& def, ObjRef spell,
                             CardId except) {
    if (def.rules.spell.mode_count > 0) return !legal_modes(s, db, p, def, spell).empty();
    if (def.cost.x > 0) return !x_options(s, db, p, def, spell, except).empty();
    return has_targets(s, db, spell_context(def, def.rules.spell.target, p, spell));
}

// Costi pagati esiliando carte bianche dalla mano. Evocazione (Solitude): esattamente una. March:
// quante si vuole, ma solo finché riducono ancora il generico, e in ordine canonico di definizione
// (le stesse carte in un altro ordine sono la stessa scelta); una carta si offre solo se dopo di
// lei il costo resta pagabile esiliandone altre.
struct HandExileOptions {
    bool can_stop = false;
    std::vector<CardId> cards;
};

HandExileOptions hand_exile_options(const GameState& s, const CardDatabase& db, PlayerId p, const StackItem& item,
                                    const PlayerZone& chosen) {
    const CardDef& def = db.get(item.source_def);
    // Copie interscambiabili: stessa definizione e note agli stessi giocatori (una copia rivelata
    // all'avversario è una scelta diversa da una che non conosce).
    auto key = [&](CardId id) { return static_cast<std::uint32_t>(s.objects[id].def) << 8 | s.objects[id].known_to; };
    std::vector<std::pair<std::uint32_t, CardId>> cands;
    for (const CardId id : s.players[p].hand) {
        if (has_color(s, db, id, hand_exile_color(item)) && !chosen.contains(id)) cands.emplace_back(key(id), id);
    }
    std::sort(cands.begin(), cands.end());
    HandExileOptions out;
    if (exiles_exactly_one(item)) {
        for (std::size_t i = 0; i < cands.size(); ++i) {
            if (i == 0 || cands[i - 1].first != cands[i].first) out.cards.push_back(cands[i].second);
        }
        return out;
    }
    const int count = static_cast<int>(chosen.size());
    const int per = def.rules.spell.reduction_per_white_card;
    const int generic = def.cost.generic + def.cost.x * item.x;
    const int useful = (generic + per - 1) / per;
    auto payable = [&](int n) { return can_pay(s, db, p, spell_mana_cost(def, item.x, n)); };
    out.can_stop = payable(count);
    if (count >= useful) return out;
    const std::uint32_t last = chosen.empty() ? 0u : key(chosen.back());
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const auto [d, id] = cands[i];
        if (d < last || (i > 0 && cands[i - 1].first == d)) continue;
        const int more = std::min(static_cast<int>(cands.size() - i - 1), useful - count - 1);
        if (payable(count + 1 + more)) out.cards.push_back(id);
    }
    return out;
}

void begin_payment(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log);

// Prosegue la scelta delle carte da esiliare dalla mano; a scelta finita le esilia (un evento) e
// passa al mana (March) o completa il lancio (evocazione). Le scelte obbligate avvengono da sole.
void continue_hand_exile(GameState& s, const CardDatabase& db, PlayerId p, PlayerZone chosen, GameLog* log,
                         bool stop = false) {
    while (!stop) {
        const StackItem& item = s.stack_items.back();
        if (exiles_exactly_one(item) && chosen.size() == 1) break;
        const HandExileOptions o = hand_exile_options(s, db, p, item, chosen);
        const std::size_t n = o.cards.size() + (o.can_stop ? 1 : 0);
        if (n == 0) throw EngineError("costo dalla mano impossibile: l'azione non doveva essere legale");
        if (n == 1) {
            if (o.can_stop) break;
            chosen.push_back(o.cards.front());
            continue;
        }
        PendingDecision d = decision(DecisionKind::ExileFromHandForCost, p);
        d.subject = item.source;
        d.chosen = chosen;
        s.pending = d;
        return;
    }
    const auto refs = [&] {
        StaticVector<ObjRef, kPlayerZoneCapacity> r;
        for (const CardId id : chosen) r.push_back(s.ref(id));
        return r;
    }();
    if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Exile, Cause::Cost);
    StackItem& item = s.stack_items.back();
    item.exiled_for_cost = static_cast<std::uint8_t>(chosen.size());
    if (exiles_exactly_one(item)) {
        complete_cast(s, p, true);
    } else {
        begin_payment(s, db, p, log);
    }
}

// Replicare (702.56a): quante volte pagare il costo aggiuntivo. Al più una copia per ogni altro
// bersaglio legale (una copia in più neutralizzerebbe di nuovo lo stesso bersaglio: dominata), e
// solo finché il costo totale si può pagare.
int replicate_max(const GameState& s, const CardDatabase& db, PlayerId p, const StackItem& item) {
    const CardDef& def = db.get(item.source_def);
    if (def.rules.spell.replicate_generic == 0) return 0;
    std::vector<Action> targets;
    target_actions(s, db, target_context(item, db), targets);
    const int cap = std::max(0, static_cast<int>(targets.size()) - 1);
    int k = 0;
    while (k < cap && can_pay(s, db, p, spell_mana_cost(def, item.x, item.exiled_for_cost, k + 1))) ++k;
    return k;
}

void choose_replicate(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    if (replicate_max(s, db, p, s.stack_items.back()) > 0) {
        PendingDecision d = decision(DecisionKind::ChooseReplicate, p);
        d.subject = s.stack_items.back().source;
        s.pending = d;
        return;
    }
    begin_payment(s, db, p, log);
}

// Dopo i bersagli (o subito, se non ce ne sono): costi e fine del lancio.
void after_targets(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    const StackItem& item = s.stack_items.back();
    if (item.kind == StackKind::TriggeredAbility) return;  // un trigger non ha costi
    if (item.kind == StackKind::Spell) {
        switch (item.method) {
            case CastMethod::Escape:
            case CastMethod::Bestow: continue_graveyard_exile(s, db, p, PlayerZone{}, log); return;
            case CastMethod::SacrificeAlternative: begin_sacrifice_cost(s, db, p); return;
            case CastMethod::Evoke:
            case CastMethod::Pitch: continue_hand_exile(s, db, p, PlayerZone{}, log); return;
            case CastMethod::Free: complete_cast(s, p, true); return;
            case CastMethod::Warp: begin_payment(s, db, p, log); return;
            case CastMethod::Normal: {
                const SpellRules& spell = db.get(item.source_def).rules.spell;
                if (spell.reduction_per_white_card > 0) {
                    continue_hand_exile(s, db, p, PlayerZone{}, log);  // costo aggiuntivo facoltativo (March)
                } else if (spell.replicate_generic > 0) {
                    choose_replicate(s, db, p, log);  // costo aggiuntivo ripetibile (Consign to Memory)
                } else {
                    begin_payment(s, db, p, log);
                }
                return;
            }
        }
    }
    pay_ability_costs(s, db, p);
}

// Scelte di bersaglio di una copia (707.10c): tenere quello dell'originale, o uno nuovo e legale.
std::vector<Action> copy_target_actions(const GameState& s, const CardDatabase& db, std::size_t index) {
    const StackItem& item = s.stack_items[index];
    std::vector<Action> all;
    target_actions(s, db, target_context(item, db), all);
    std::vector<Action> out{Action{ActionType::ChooseOption, kNoCard, kKeepTarget}};
    for (const Action& a : all) {
        const bool same = (a.type == ActionType::TargetStackItem && item.target.tag == TargetKindTag::StackItem &&
                           s.stack_items[a.value].id == item.target_id) ||
                          (a.type == ActionType::TargetObject && item.target.tag == TargetKindTag::Object &&
                           a.card == item.target.object.card) ||
                          (a.type == ActionType::TargetPlayer && item.target.tag == TargetKindTag::Player &&
                           a.value == item.target.player);
        if (!same && a.type != ActionType::ChooseOption) out.push_back(a);
    }
    return out;
}

// Le copie in pila da `index` in su, `count` in tutto: per ognuna, se c'è un altro bersaglio
// possibile, la decisione di cambiarlo.
void continue_copy_targets(GameState& s, const CardDatabase& db, PlayerId p, std::size_t index, int count) {
    for (; count > 0; ++index, --count) {
        if (copy_target_actions(s, db, index).size() <= 1) continue;
        PendingDecision d = decision(DecisionKind::ChooseTarget, p);
        d.subject = s.stack_items[index].source;
        d.stack_index = static_cast<std::uint8_t>(index);
        d.amount = static_cast<std::uint8_t>(count - 1);  // copie ancora da vedere dopo questa
        s.pending = d;
        return;
    }
}

// Neutralizza la magia o abilità in pila con quell'id (701.6a). Una magia con la carta va nel
// cimitero del proprietario, o in esilio (Force of Negation); una copia smette di esistere.
void counter_stack_item(GameState& s, const CardDatabase& db, std::uint16_t id, bool exile) {
    const std::size_t i = stack_index_of(s, id);
    if (i >= s.stack_items.size()) return;
    const StackItem item = s.stack_items[i];
    s.stack_items.erase_at(i);
    if (item.kind != StackKind::Spell || item.copy) return;
    if (s.is_valid(item.source) && s.objects[item.source.card].zone == Zone::Stack) {
        move_and_collect(s, db, one(item.source), exile ? Zone::Exile : Zone::Graveyard, Cause::Counter);
    }
}

// 601.2c: i bersagli.
void choose_targets(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    const StackItem& item = s.stack_items.back();
    if (target_kind_of(item, db) != TargetKind::None) {
        PendingDecision d = decision(DecisionKind::ChooseTarget, p);
        d.subject = item.source;
        s.pending = d;
        return;
    }
    after_targets(s, db, p, log);
}

// 601.2b: il valore di X (solo pagando il costo di mana: senza pagarlo X è 0, 107.3b).
void choose_x(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    StackItem& item = s.stack_items.back();
    const CardDef& def = db.get(item.source_def);
    if (def.cost.x > 0 && item.method == CastMethod::Normal) {
        const std::vector<std::uint8_t> xs = x_options(s, db, p, def, item.source, kNoCard);
        if (xs.empty()) throw EngineError("nessun valore di X: l'azione non doveva essere legale");
        if (xs.size() > 1) {
            PendingDecision d = decision(DecisionKind::ChooseX, p);
            d.subject = item.source;
            s.pending = d;
            return;
        }
        item.x = xs.front();
    }
    choose_targets(s, db, p, log);
}

// 601.2b: il modo delle magie modali, solo tra quelli con bersagli legali (700.2a).
void choose_mode(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    StackItem& item = s.stack_items.back();
    const CardDef& def = db.get(item.source_def);
    if (def.rules.spell.mode_count > 0) {
        const std::vector<std::uint8_t> modes = legal_modes(s, db, p, def, item.source);
        if (modes.empty()) throw EngineError("nessun modo legale: l'azione non doveva essere legale");
        if (modes.size() > 1) {
            PendingDecision d = decision(DecisionKind::ChooseMode, p);
            d.subject = item.source;
            s.pending = d;
            return;
        }
        item.mode = modes.front();
    }
    choose_x(s, db, p, log);
}

void begin_cast(GameState& s, const CardDatabase& db, PlayerId p, CardId card, CastMethod method, GameLog* log) {
    const CardDef& def = db.get(s.objects[card].def);
    const Zone from = s.objects[card].zone;
    // 601.2a: la carta va in pila per prima cosa (dalla mano, dal cimitero, dall'esilio).
    const ObjRef on_stack = move_and_collect(s, db, one(s.ref(card)), Zone::Stack, Cause::Cast).moves[0].new_ref;
    StackItem item;
    item.kind = StackKind::Spell;
    item.controller = p;
    item.source = on_stack;
    item.source_def = def.id;
    item.method = method;
    item.cast_from = from;
    push_stack(s, item);
    if (method == CastMethod::Bestow) s.objects[on_stack.card].set_flag(ObjectFlag::Bestowed);  // 702.103b
    choose_mode(s, db, p, log);
}

void activate_fetch(GameState& s, const CardDatabase& db, PlayerId p, CardId card) {
    const CardDef& def = effective_def(s, db, card);
    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = StackEffect::FetchSearch;
    item.controller = p;
    item.source_def = def.id;
    item.land_types = def.rules.land.fetch_types;

    // Costi: {T}, paga 1 vita, sacrifica. Il sacrificio è un evento (Moonshadow lo vedrà).
    s.objects[card].set_flag(ObjectFlag::Tapped);
    lose_life(s, p, def.rules.land.fetch_life, card, Cause::Cost);
    item.source = move_and_collect(s, db, one(s.ref(card)), Zone::Graveyard, Cause::Cost).moves[0].new_ref;

    push_stack(s, item);
    complete_cast(s, p, false);
}

// "Cycling—Pay N life": costo = paga N vita e scarta questa carta; effetto = pesca una carta.
void activate_cycling(GameState& s, const CardDatabase& db, PlayerId p, CardId card) {
    const CardDef& def = db.get(s.objects[card].def);
    lose_life(s, p, def.rules.cycling_life, card, Cause::Cost);
    const ObjRef discarded = move_and_collect(s, db, one(s.ref(card)), Zone::Graveyard, Cause::Discard).moves[0].new_ref;

    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = StackEffect::Draw;
    item.controller = p;
    item.source = discarded;
    item.source_def = def.id;
    item.amount = 1;
    push_stack(s, item);
    complete_cast(s, p, false);
}

// "Swampcycling {1}": abilità dalla mano; in pila, poi {1}, poi scartare la carta (after_mana).
void activate_landcycling(GameState& s, const CardDatabase& db, PlayerId p, CardId card, GameLog* log) {
    const CardDef& def = db.get(s.objects[card].def);
    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = StackEffect::CycleSearch;
    item.controller = p;
    item.source = s.ref(card);
    item.source_def = def.id;
    item.land_types = def.rules.landcycling.types;
    push_stack(s, item);
    begin_payment(s, db, p, log);
}

// 606.3: con la priorità, a pila vuota, in una fase principale del proprio turno, se in questo
// turno nessuna abilità di fedeltà del permanente è stata attivata; 606.6: un costo negativo
// richiede almeno altrettanti segnalini.
bool loyalty_ability_available(const GameState& s, const CardDatabase& db, PlayerId p, CardId id, std::size_t i) {
    const CardObject& o = s.objects[id];
    const CardDef& d = effective_def(s, db, id);
    if (o.controller != p || i >= d.rules.loyalty_count || o.has_flag(ObjectFlag::LoyaltyActivated)) return false;
    if (!sorcery_timing(s, p)) return false;
    const int cost = d.rules.loyalty[i].cost;
    return cost >= 0 || o.counters.loyalty >= -cost;
}

StackEffect loyalty_stack_effect(LoyaltyEffect e) {
    switch (e) {
        case LoyaltyEffect::SorceriesAsFlash: return StackEffect::SorceriesAsFlash;
        case LoyaltyEffect::BounceAndDraw: return StackEffect::BounceAndDraw;
        case LoyaltyEffect::None: break;
    }
    throw EngineError("abilità di fedeltà sconosciuta");
}

// In pila, poi il bersaglio (se c'è), poi il costo in segnalini (602.2: i costi dopo i bersagli).
void activate_loyalty(GameState& s, const CardDatabase& db, PlayerId p, CardId card, std::uint8_t index) {
    const CardDef& def = effective_def(s, db, card);
    s.objects[card].set_flag(ObjectFlag::LoyaltyActivated);
    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = loyalty_stack_effect(def.rules.loyalty[index].effect);
    item.controller = p;
    item.source = s.ref(card);
    item.source_def = def.id;
    item.mode = index;
    push_stack(s, item);
    if (target_kind_of(s.stack_items.back(), db) != TargetKind::None) {
        std::vector<Action> options;
        target_actions(s, db, target_context(s.stack_items.back(), db), options);
        // "Up to one" senza bersagli possibili: nessuna decisione.
        if (!(options.size() == 1 && options.front().type == ActionType::ChooseOption)) {
            PendingDecision d = decision(DecisionKind::ChooseTarget, p);
            d.subject = s.ref(card);
            s.pending = d;
            return;
        }
    }
    pay_ability_costs(s, db, p);
}

// Abilità attivata di un permanente con bersaglio (Mishra's Bauble): in pila, bersaglio, poi i costi.
void activate_permanent_ability(GameState& s, const CardDatabase& db, PlayerId p, CardId card) {
    const CardDef& def = effective_def(s, db, card);
    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = StackEffect::LookAtTop;
    item.controller = p;
    item.source = s.ref(card);
    item.source_def = def.id;
    push_stack(s, item);

    PendingDecision d = decision(DecisionKind::ChooseTarget, p);
    d.subject = s.ref(card);
    s.pending = d;
}

// --- Azioni di stato (704) --------------------------------------------------------

bool same_legend_group(const GameState& s, const CardDatabase& db, CardId a, CardId b) {
    return s.objects[a].controller == s.objects[b].controller &&
           effective_def(s, db, a).id == effective_def(s, db, b).id;
}

// Esegue un passaggio di azioni di stato, tutte insieme. Restituisce true se è
// cambiato qualcosa o se serve una decisione (regola delle leggende): il
// chiamante ripete finché non cambia più nulla (704.3).
bool run_state_based_actions(GameState& s, const CardDatabase& db) {
    TurnState& t = s.turn;
    update_city_blessing(s, db);  // non è un'azione di stato, ma vale "in qualsiasi momento" (702.131b)

    // Sconfitte: punti vita a 0 o meno, pescata da grimorio vuoto.
    bool lose[kNumPlayers] = {};
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        lose[p] = s.players[p].life <= 0 || s.players[p].drew_from_empty_library;
    }
    if (lose[0] || lose[1]) {
        t.game_over = true;
        t.winner = (lose[0] && lose[1]) ? kNoPlayer : (lose[0] ? PlayerId{1} : PlayerId{0});
        return true;
    }

    // 702.103f: un'Aura conferita staccata, o attaccata a un oggetto illegale, si stacca e
    // smette di essere conferita: resta in campo come creatura (non va al cimitero).
    bool unattached = false;
    for (const CardId id : s.battlefield) {
        CardObject& o = s.objects[id];
        if (!o.has_flag(ObjectFlag::Bestowed)) continue;
        const ObjRef host = o.attached_to;
        const bool legal = s.is_valid(host) && is_creature_on_battlefield(s, db, host.card) &&
                           (protection_of(s, db, host.card) & effective_def(s, db, id).colors) == 0;
        if (!legal) {
            o.clear_flag(ObjectFlag::Bestowed);
            o.attached_to = ObjRef{};
            unattached = true;
        }
    }
    if (unattached) return true;

    // Regola delle leggende: prima tutte le scelte, poi si applica tutto insieme.
    for (const CardId id : s.battlefield) {
        if (!effective_def(s, db, id).has_supertype(Supertype::Legendary)) continue;
        int members = 0;
        bool chosen = false;
        for (const CardId other_id : s.battlefield) {
            if (!same_legend_group(s, db, id, other_id)) continue;
            ++members;
            chosen = chosen || s.legend_keep.contains(s.ref(other_id));
        }
        if (members >= 2 && !chosen) {
            PendingDecision d = decision(DecisionKind::LegendRule, s.objects[id].controller);
            d.subject = s.ref(id);
            s.pending = d;
            return true;
        }
    }

    StaticVector<ObjRef, kMaxObjects> to_graveyard;
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        const CardDef& d = effective_def(s, db, id);
        bool dies = false;
        if (is_creature_on_battlefield(s, db, id)) {  // un'Aura conferita non è una creatura
            const int tough = toughness(s, db, id);
            dies = tough <= 0 || o.damage >= tough;  // costituzione 0 o meno, danno letale
        }
        if (d.has_type(CardType::Planeswalker) && o.counters.loyalty == 0) dies = true;  // 704.5i
        if (!dies && d.has_supertype(Supertype::Legendary) && !s.legend_keep.contains(s.ref(id))) {
            for (const CardId other_id : s.battlefield) {
                if (other_id != id && same_legend_group(s, db, id, other_id) &&
                    s.legend_keep.contains(s.ref(other_id))) {
                    dies = true;  // un'altra copia è stata scelta
                }
            }
        }
        if (dies) to_graveyard.push_back(s.ref(id));
    }

    // Token fuori dal campo: si guardano prima degli spostamenti di questo passaggio.
    StaticVector<ObjRef, kMaxTokens> tokens_gone;
    for (std::size_t slot = kMaxCards; slot < kMaxObjects; ++slot) {
        const Zone z = s.objects[slot].zone;
        if (z != Zone::None && z != Zone::Battlefield) tokens_gone.push_back(s.ref(static_cast<CardId>(slot)));
    }

    bool changed = false;
    // Segnalini +1/+1 e -1/-1 si annullano (sui permanenti che restano in campo).
    for (const CardId id : s.battlefield) {
        Counters& c = s.objects[id].counters;
        if (c.plus_one > 0 && c.minus_one > 0 && !to_graveyard.contains(s.ref(id))) {
            const std::uint8_t n = std::min(c.plus_one, c.minus_one);
            c.plus_one = static_cast<std::uint8_t>(c.plus_one - n);
            c.minus_one = static_cast<std::uint8_t>(c.minus_one - n);
            changed = true;
        }
    }
    if (!to_graveyard.empty()) {
        move_and_collect(s, db, {to_graveyard.begin(), to_graveyard.size()}, Zone::Graveyard, Cause::StateBased);
        changed = true;
    }
    for (const ObjRef token : tokens_gone) {
        if (t_log != nullptr) {  // il token smette di esistere (704.5d): nel registro va in Zone::None
            const CardObject& o = s.objects[token.card];
            LogEntry e;
            e.kind = LogEntry::Kind::ZoneChange;
            e.player = o.owner;
            e.action.card = token.card;
            e.def = o.def;
            e.from = o.zone;
            e.to = Zone::None;
            e.cause = Cause::StateBased;
            log_event(s, e);
        }
        remove_token(s, token);
        changed = true;
    }
    s.legend_keep.clear();
    return changed;
}

// --- Risoluzione ------------------------------------------------------------------------

void deal_damage(GameState& s, const CardDatabase& db, const Target& target, int amount, ColorMask source_colors,
                 CardId source) {
    if (target.tag == TargetKindTag::Player) {
        lose_life(s, target.player, amount, source, Cause::Damage);  // il danno fa perdere vita (120.3a)
    } else {
        CardObject& o = s.objects[target.object.card];
        const int dealt = combat::damage_after_prevention(s, db, target.object.card, source_colors, amount);
        if (!is_creature_on_battlefield(s, db, target.object.card) &&
            effective_def(s, db, target.object.card).has_type(CardType::Planeswalker)) {
            o.counters.loyalty = static_cast<std::uint8_t>(std::max(0, o.counters.loyalty - dealt));  // 120.3c
        } else {
            o.damage = static_cast<std::uint8_t>(std::min(255, o.damage + dealt));
        }
    }
}

void finish_thoughtseize(GameState& s, const CardDatabase& db, PlayerId caster, ObjRef spell) {
    lose_life(s, caster, db.get(s.objects[spell.card].def).rules.spell.life_loss, spell.card, Cause::Resolve);
    move_and_collect(s, db, one(spell), Zone::Graveyard, Cause::Resolve);
}

// Creature e planeswalker di `p` con il valore di mana più alto, uno per stato distinto
// (Flare of Malice). Un'Aura conferita non è una creatura.
std::vector<CardId> greatest_mana_value_permanents(const GameState& s, const CardDatabase& db, PlayerId p) {
    std::vector<CardId> out;
    int best = -1;
    for (const CardId id : s.battlefield) {
        if (s.objects[id].controller != p) continue;
        const CardDef& d = effective_def(s, db, id);
        if (!is_creature_on_battlefield(s, db, id) && !d.has_type(CardType::Planeswalker)) continue;
        const int mv = d.mana_value;
        if (mv > best) {
            best = mv;
            out.clear();
        }
        if (mv == best && std::none_of(out.begin(), out.end(), [&](CardId o) { return same_permanent_state(s, o, id); })) {
            out.push_back(id);
        }
    }
    return out;
}

void resolve_spell(GameState& s, const CardDatabase& db, const StackItem& item) {
    const ObjRef card = item.source;
    if (!item.copy && (!s.is_valid(card) || s.objects[card.card].zone != Zone::Stack)) {
        throw EngineError("risoluzione: la carta della magia non è più in pila");
    }
    const CardDef& def = db.get(item.source_def);
    const SpellRules& spell = def.rules.spell;
    const TargetContext ctx = target_context(item, db);

    if (item.method == CastMethod::Bestow) {
        // 702.103e: con il bersaglio illegale smette di essere conferita e si risolve come creatura.
        const bool attach = target_still_legal(s, db, item.target, ctx);
        move_and_collect(s, db, one(card), Zone::Battlefield, Cause::Resolve);
        if (attach) {
            CardObject& o = s.objects[card.card];
            o.set_flag(ObjectFlag::Bestowed);
            o.attached_to = item.target.object;
        }
        return;
    }

    // 608.2b: se tutti i bersagli sono illegali la magia non si risolve. I giocatori restano legali.
    if (ctx.kind != TargetKind::None && ctx.kind != TargetKind::Players &&
        !target_still_legal(s, db, item.target, ctx, item.target_id)) {
        if (!item.copy) move_and_collect(s, db, one(card), Zone::Graveyard, Cause::Fizzle);  // una copia sparisce
        return;
    }
    // Magia permanente: entra in campo (i trigger di entrata scattano qui).
    if ((def.types & kPermanentTypes) != 0) {
        const ObjRef permanent =
            move_and_collect(s, db, one(card), Zone::Battlefield, Cause::Resolve).moves[0].new_ref;
        if (item.method == CastMethod::Warp) {
            // 702.185a: costo di warp pagato, si esilia all'inizio della prossima sottofase finale.
            DelayedTrigger d;
            d.controller = item.controller;
            d.effect = StackEffect::WarpExile;
            d.source_def = def.id;
            d.created_turn = s.turn.turn_number;
            d.at_end_step = true;
            d.object = permanent;
            s.delayed.push_back(d);
        }
        if (item.method == CastMethod::Evoke) {
            // 702.74a: "When this permanent enters, if its evoke cost was paid, its controller sacrifices
            // it." Scatta insieme ai trigger di entrata appena raccolti: l'ordine lo sceglie il controllore.
            s.pending_triggers.push_back(triggered(StackEffect::EvokeSacrifice, item.controller, permanent, def.id, 0));
        }
        return;
    }
    switch (spell_mode(def, item).effect) {
        case SpellEffect::DealDamage: deal_damage(s, db, item.target, spell.amount, def.colors, item.source.card); break;
        case SpellEffect::Thoughtseize: {
            const PlayerId victim = item.target.player;
            bool any_nonland = false;
            for (const CardId id : s.players[victim].hand) {
                s.objects[id].known_to = kAllPlayers;  // la mano viene rivelata
                any_nonland = any_nonland || !db.get(s.objects[id].def).has_type(CardType::Land);
            }
            if (any_nonland) {
                PendingDecision d = decision(DecisionKind::ChooseFromHand, item.controller);
                d.subject = card;  // la magia resta in pila finché la risoluzione non finisce
                d.target_player = victim;
                s.pending = d;
                return;
            }
            finish_thoughtseize(s, db, item.controller, card);
            return;
        }
        case SpellEffect::OpponentsSacrificeGreatest: {
            const PlayerId victim = other(item.controller);
            const std::vector<CardId> options = greatest_mana_value_permanents(s, db, victim);
            if (options.size() > 1) {  // sceglie l'avversario tra i pari
                PendingDecision d = decision(DecisionKind::SacrificeForEffect, victim);
                d.subject = card;  // la magia resta in pila finché la risoluzione non finisce
                s.pending = d;
                return;
            }
            if (options.size() == 1) {
                move_and_collect(s, db, one(s.ref(options.front())), Zone::Graveyard, Cause::Sacrifice);
            }
            break;
        }
        case SpellEffect::Blink: {
            // Ephemerate: esilio e ritorno sotto il proprietario, come oggetto nuovo (400.7). Un token che
            // ha lasciato il campo non torna (111.8): smetterà di esistere con le azioni di stato.
            const bool token = s.objects[item.target.object.card].has_flag(ObjectFlag::Token);
            const ObjRef exiled =
                move_and_collect(s, db, one(item.target.object), Zone::Exile, Cause::Exile).moves[0].new_ref;
            if (!token && s.is_valid(exiled) && s.objects[exiled.card].zone == Zone::Exile) {
                move_and_collect(s, db, one(exiled), Zone::Battlefield, Cause::Resolve);
            }
            break;
        }
        case SpellEffect::ExileTarget:
            move_and_collect(s, db, one(item.target.object), Zone::Exile, Cause::Exile);
            break;
        case SpellEffect::DamageTwiceCreatures: {
            // Le creature si contano alla risoluzione.
            int creatures = 0;
            for (const CardId id : s.battlefield) {
                if (s.objects[id].controller == item.controller && is_creature_on_battlefield(s, db, id)) ++creatures;
            }
            deal_damage(s, db, item.target, 2 * creatures, def.colors, card.card);
            break;
        }
        case SpellEffect::Destroy:
            move_and_collect(s, db, one(item.target.object), Zone::Graveyard, Cause::Destroy);
            break;
        case SpellEffect::ExileGraveyards: {
            StaticVector<ObjRef, kMaxObjects> refs;
            for (PlayerId p = 0; p < kNumPlayers; ++p) {
                if ((item.player_mask & (1u << p)) == 0) continue;
                for (const CardId id : s.players[p].graveyard) refs.push_back(s.ref(id));
            }
            if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Exile, Cause::Exile);
            break;
        }
        case SpellEffect::CounterTarget: counter_stack_item(s, db, item.target_id, false); break;
        case SpellEffect::CounterAndExile: counter_stack_item(s, db, item.target_id, true); break;
        case SpellEffect::None: break;
    }
    if (item.copy) return;  // 707.10a: una copia fuori dalla pila smette di esistere
    if (spell.rebound && item.cast_from == Zone::Hand) {
        // 702.88a: lanciata dalla mano, invece del cimitero va in esilio; al prossimo mantenimento del
        // controllore si può lanciare dall'esilio senza pagare il costo di mana.
        const ObjRef exiled = move_and_collect(s, db, one(card), Zone::Exile, Cause::Resolve).moves[0].new_ref;
        DelayedTrigger d;
        d.controller = item.controller;
        d.effect = StackEffect::ReboundCast;
        d.source_def = def.id;
        d.created_turn = s.turn.turn_number;
        d.own_upkeep = true;
        d.object = exiled;
        s.delayed.push_back(d);
        return;
    }
    move_and_collect(s, db, one(card), Zone::Graveyard, Cause::Resolve);
}

void resolve_top_of_stack(GameState& s, const CardDatabase& db) {
    const StackItem item = s.stack_items.back();
    s.stack_items.pop_back();
    s.turn.priority_player = s.turn.active_player;  // dopo la risoluzione, priorità al giocatore attivo
    if (item.kind == StackKind::Spell) {
        resolve_spell(s, db, item);
        return;
    }
    switch (item.effect) {
        case StackEffect::FetchSearch:
        case StackEffect::CycleSearch: {
            PendingDecision d = decision(DecisionKind::SearchLibrary, item.controller);
            d.land_types = item.land_types;
            d.destination = item.effect == StackEffect::FetchSearch ? Zone::Battlefield : Zone::Hand;
            s.pending = d;
            return;
        }
        case StackEffect::Rummage: {
            // "discard a card, then draw a card": con la mano vuota si pesca e basta.
            if (s.players[item.controller].hand.empty()) {
                draw_cards(s, db, item.controller, 1);
                return;
            }
            PendingDecision d = decision(DecisionKind::DiscardCard, item.controller);
            d.subject = item.source;
            s.pending = d;
            return;
        }
        case StackEffect::UntapAndExtraCombat: {
            if (!target_still_legal(s, db, item.target, target_context(item, db))) return;
            if (!s.players[item.controller].gy.delirium()) return;  // 603.4: "if" ricontrollato alla risoluzione
            s.objects[item.target.object.card].clear_flag(ObjectFlag::Tapped);
            ++s.turn.extra_combats;  // "After this phase, there is an additional combat phase."
            return;
        }
        case StackEffect::RemoveMinusCounter: {
            if (s.is_valid(item.source) && s.objects[item.source.card].zone == Zone::Battlefield) {
                Counters& c = s.objects[item.source.card].counters;
                if (c.minus_one > 0) --c.minus_one;
            }
            return;
        }
        case StackEffect::Surveil:
            if (item.amount != 1) throw EngineError("sorvegliare N > 1 non ancora implementato");
            if (!s.players[item.controller].library.empty()) {
                PendingDecision d = decision(DecisionKind::Surveil, item.controller);
                d.amount = item.amount;
                s.pending = d;
            }
            return;
        case StackEffect::Draw: draw_cards(s, db, item.controller, item.amount); return;
        case StackEffect::Mill: mill(s, db, item.controller, item.amount); return;
        case StackEffect::LookAtTop: {
            const auto& library = s.players[item.target.player].library;
            if (!library.empty()) {
                CardObject& top = s.objects[library.back()];
                top.known_to = static_cast<std::uint8_t>(top.known_to | (1u << item.controller));
            }
            DelayedTrigger delayed;  // "Draw a card at the beginning of the next turn's upkeep."
            delayed.controller = item.controller;
            delayed.effect = StackEffect::Draw;
            delayed.amount = 1;
            delayed.source_def = item.source_def;
            delayed.created_turn = s.turn.turn_number;
            s.delayed.push_back(delayed);
            return;
        }
        case StackEffect::EndStepCats: {
            PlayerState& ps = s.players[item.controller];
            if (!ps.gained_life_this_turn) return;  // 603.4: "if" ricontrollato alla risoluzione
            const CardDefId cat = db.get(item.source_def).rules.triggers.end_step_token;
            create_tokens(s, db, std::span<const CardDefId>(&cat, 1), item.controller);
            update_city_blessing(s, db);  // il gatto può essere il decimo permanente
            if (!ps.city_blessing) return;
            // "for each token you control that entered this turn, create a token that's a copy of it":
            // un solo evento; le copie prendono i valori copiabili (707.2), non segnalini né tipi aggiunti.
            std::vector<CardDefId> copies;
            for (const CardId id : s.battlefield) {
                const CardObject& o = s.objects[id];
                if (o.has_flag(ObjectFlag::Token) && o.controller == item.controller &&
                    o.zone_turn == s.turn.turn_number) {
                    copies.push_back(o.def);
                }
            }
            create_tokens(s, db, copies, item.controller);
            return;
        }
        case StackEffect::GainLifeAndEnergy: {
            const CardId source = s.is_valid(item.source) ? item.source.card : kNoCard;
            gain_life(s, item.controller, item.amount, source, Cause::Resolve);
            PlayerState& ps = s.players[item.controller];
            ps.energy = static_cast<std::uint8_t>(std::min(255, ps.energy + item.amount));
            return;
        }
        case StackEffect::EnergyAttack: {
            // "you may pay {E}{E}{E}": senza tre energie non c'è nulla da scegliere.
            if (s.players[item.controller].energy < 3) return;
            PendingDecision d = decision(DecisionKind::PayOptionalCost, item.controller);
            d.subject = item.source;
            d.amount = 3;
            s.pending = d;
            return;
        }
        case StackEffect::AttackCounters: {
            if (!target_still_legal(s, db, item.target, target_context(item, db))) return;
            CardObject& o = s.objects[item.target.object.card];
            o.counters.plus_one = static_cast<std::uint8_t>(std::min(255, o.counters.plus_one + 2));
            auto& flying = o.counters.keywords[static_cast<std::size_t>(KeywordCounter::Flying)];
            flying = static_cast<std::uint8_t>(std::min(255, flying + 1));
            o.set_flag(ObjectFlag::Angel);
            return;
        }
        case StackEffect::DestroyTarget: {
            if (!target_still_legal(s, db, item.target, target_context(item, db))) return;
            move_and_collect(s, db, one(item.target.object), Zone::Graveyard, Cause::Destroy);
            return;
        }
        case StackEffect::SearchToHand: {
            PendingDecision d = decision(DecisionKind::SearchLibrary, item.controller);
            d.destination = Zone::Hand;
            d.search = item.search;
            s.pending = d;
            return;
        }
        case StackEffect::ExileBlackRedGraveyards: {
            constexpr ColorMask black_red = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
            StaticVector<ObjRef, kMaxObjects> refs;
            for (PlayerId p = 0; p < kNumPlayers; ++p) {
                for (const CardId id : s.players[p].graveyard) {
                    if ((db.get(s.objects[id].def).colors & black_red) != 0) refs.push_back(s.ref(id));
                }
            }
            if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Exile, Cause::Exile);
            return;
        }
        case StackEffect::WarpExile: {
            // Il permanente lanciato con il warp; se ha già lasciato il campo è un oggetto nuovo (400.7).
            if (!s.is_valid(item.source) || s.objects[item.source.card].zone != Zone::Battlefield) return;
            const ZoneChange m = move_and_collect(s, db, one(item.source), Zone::Exile, Cause::Exile).moves[0];
            if (m.to == Zone::Exile) s.objects[m.card].set_flag(ObjectFlag::WarpExiled);  // 702.185b
            return;
        }
        case StackEffect::ExileOtherCreature: {
            // Solitude: senza bersaglio ("up to one") non fa nulla. La vita è pari alla forza con cui la
            // creatura lascia il campo, al suo controllore di allora (informazioni note per ultime).
            if (item.target.tag != TargetKindTag::Object || !target_still_legal(s, db, item.target, target_context(item, db))) {
                return;
            }
            const CardId target = item.target.object.card;
            const PlayerId controller = s.objects[target].controller;
            const int gained = power(s, db, target);
            move_and_collect(s, db, one(item.target.object), Zone::Exile, Cause::Exile);
            gain_life(s, controller, gained, item.source.card, Cause::Resolve);
            return;
        }
        case StackEffect::EvokeSacrifice:
            // Se il permanente ha già lasciato il campo (Ephemerate), è un oggetto nuovo: niente sacrificio.
            if (s.is_valid(item.source) && s.objects[item.source.card].zone == Zone::Battlefield) {
                move_and_collect(s, db, one(item.source), Zone::Graveyard, Cause::Sacrifice);
            }
            return;
        case StackEffect::PheliaExile: {
            if (item.target.tag != TargetKindTag::Object || !target_still_legal(s, db, item.target, target_context(item, db))) {
                return;
            }
            const ObjRef exiled =
                move_and_collect(s, db, one(item.target.object), Zone::Exile, Cause::Exile).moves[0].new_ref;
            DelayedTrigger d;  // "At the beginning of the next end step, return that card"
            d.controller = item.controller;
            d.effect = StackEffect::PheliaReturn;
            d.source_def = item.source_def;
            d.created_turn = s.turn.turn_number;
            d.at_end_step = true;
            d.object = exiled;
            d.link = item.source;  // Phelia: prende il segnalino
            s.delayed.push_back(d);
            return;
        }
        case StackEffect::PheliaReturn: {
            // 603.7c: se la carta ha lasciato l'esilio (un token smette di esistere) non torna nulla.
            const ObjRef card = item.target.object;
            if (!s.is_valid(card) || s.objects[card.card].zone != Zone::Exile) return;
            const ObjRef back = move_and_collect(s, db, one(card), Zone::Battlefield, Cause::Resolve).moves[0].new_ref;
            // "If it entered under your control": torna sotto il proprietario, quindi solo le carte proprie.
            if (s.objects[back.card].controller == item.controller && s.is_valid(item.source) &&
                s.objects[item.source.card].zone == Zone::Battlefield) {
                Counters& c = s.objects[item.source.card].counters;
                c.plus_one = static_cast<std::uint8_t>(std::min(255, c.plus_one + 1));
            }
            return;
        }
        case StackEffect::ExileUntilLeaves: {
            // 610.3b: se Seam Rip ha già lasciato il campo, il permanente non si muove affatto.
            if (!s.is_valid(item.source) || s.objects[item.source.card].zone != Zone::Battlefield) return;
            if (!target_still_legal(s, db, item.target, target_context(item, db))) return;
            const ObjRef exiled =
                move_and_collect(s, db, one(item.target.object), Zone::Exile, Cause::Exile).moves[0].new_ref;
            CardObject& o = s.objects[exiled.card];
            o.set_flag(ObjectFlag::ExiledUntil);
            o.attached_to = item.source;  // torna quando Seam Rip lascia il campo (move_and_collect)
            return;
        }
        case StackEffect::ReboundCast: {
            // "you may cast this card from exile without paying its mana cost": solo se ha un bersaglio.
            const ObjRef card = item.source;
            if (!s.is_valid(card) || s.objects[card.card].zone != Zone::Exile) return;
            // 101.2: con Teferi dell'avversario si lancia solo a velocità stregoneria (al mantenimento mai).
            if (restricted_to_sorcery_timing(s, db, item.controller) && !sorcery_timing(s, item.controller)) return;
            const CardDef& def = db.get(s.objects[card.card].def);
            if (!spell_has_legal_choices(s, db, item.controller, def, card, kNoCard)) return;
            PendingDecision d = decision(DecisionKind::CastFromExile, item.controller);
            d.subject = card;
            s.pending = d;
            return;
        }
        case StackEffect::ReplicateCopies: {
            // Le copie nascono con le scelte dell'originale (707.10), anche se l'originale non è più in
            // pila; poi, una alla volta, si può scegliere un bersaglio nuovo (707.10c).
            const std::size_t first = s.stack_items.size();
            for (int i = 0; i < item.amount; ++i) {
                StackItem c;
                c.kind = StackKind::Spell;
                c.controller = item.controller;
                c.source = item.source;
                c.source_def = item.source_def;
                c.target = item.target;
                c.target_id = item.target_id;
                c.copy = true;
                push_stack(s, c);
            }
            continue_copy_targets(s, db, item.controller, first, item.amount);
            return;
        }
        case StackEffect::SorceriesAsFlash: s.players[item.controller].sorceries_as_flash = true; return;
        case StackEffect::BounceAndDraw:
            // 608.2b: con il bersaglio scelto diventato illegale non si risolve (niente pescata).
            if (item.target.tag == TargetKindTag::Object) {
                if (!target_still_legal(s, db, item.target, target_context(item, db))) return;
                move_and_collect(s, db, one(item.target.object), Zone::Hand, Cause::Resolve);
            }
            draw_cards(s, db, item.controller, 1);
            return;
        case StackEffect::None: break;
    }
    throw EngineError("risoluzione: effetto sconosciuto");
}

// --- Turno ---------------------------------------------------------------------

void finish_cleanup(GameState& s) {
    for (const CardId id : s.battlefield) s.objects[id].damage = 0;  // 514.2
}

void turn_based_actions(GameState& s, const CardDatabase& db) {
    TurnState& t = s.turn;
    const PlayerId active = t.active_player;
    switch (t.step) {
        case Step::Untap:
            // Inizio del turno: i permanenti del giocatore attivo stappano e non sono più
            // "malati"; nessuna creatura ha ancora attaccato e nessuna abilità di fedeltà è stata
            // attivata in questo turno.
            for (CardObject& o : s.objects) {
                o.clear_flag(ObjectFlag::AttackedThisTurn);
                o.clear_flag(ObjectFlag::LoyaltyActivated);
            }
            for (const CardId id : s.battlefield) {
                CardObject& o = s.objects[id];
                if (o.controller != active) continue;
                o.clear_flag(ObjectFlag::Tapped);
                o.clear_flag(ObjectFlag::SummoningSick);
            }
            t.lands_played = 0;
            for (PlayerState& p : s.players) {
                p.spells_cast_this_turn = 0;
                p.gained_life_this_turn = false;  // Ocelot Pride guarda la vita guadagnata "this turn"
            }
            s.players[active].sorceries_as_flash = false;  // il +1 di Teferi dura fino al prossimo turno
            break;
        case Step::Upkeep: {
            // "At the beginning of the next turn's upkeep": i trigger ritardati creati nei turni precedenti.
            StaticVector<DelayedTrigger, 16> waiting;
            for (const DelayedTrigger& d : s.delayed) {
                if (!d.at_end_step && d.created_turn < t.turn_number && (!d.own_upkeep || d.controller == active)) {
                    s.pending_triggers.push_back(delayed_item(d));
                } else {
                    waiting.push_back(d);
                }
            }
            s.delayed = waiting;
            break;
        }
        case Step::Draw:
            if (t.turn_number > 1) draw_cards(s, db, active, 1);  // 103.8a: chi gioca per primo salta la prima pescata
            break;
        case Step::End: {
            // Ocelot Pride: "At the beginning of your end step, if you gained life this turn" (603.4).
            for (const CardId id : s.battlefield) {
                const CardObject& o = s.objects[id];
                const CardDef& d = effective_def(s, db, id);
                if (o.controller != active || d.rules.triggers.end_step_token == kNoDef) continue;
                if (!s.players[active].gained_life_this_turn) continue;
                s.pending_triggers.push_back(triggered(StackEffect::EndStepCats, active, s.ref(id), d.id, 0));
            }
            // "At the beginning of the next end step" (warp): quelli creati prima che lo step iniziasse.
            StaticVector<DelayedTrigger, 16> waiting;
            for (const DelayedTrigger& d : s.delayed) {
                if (d.at_end_step) {
                    s.pending_triggers.push_back(delayed_item(d));
                } else {
                    waiting.push_back(d);
                }
            }
            s.delayed = waiting;
            break;
        }
        case Step::DeclareAttackers: combat::begin_attack_declaration(s, db); break;  // 508.1
        case Step::DeclareBlockers: combat::begin_block_declaration(s, db); break;    // 509.1
        case Step::FirstStrikeDamage:
        case Step::CombatDamage: combat::begin_damage_step(s, db); break;  // 510.1-510.2
        case Step::Cleanup: {
            const std::size_t hand = s.players[active].hand.size();
            if (hand > kMaxHandSize) {
                PendingDecision d = decision(DecisionKind::DiscardToHandSize, active);
                d.amount = static_cast<std::uint8_t>(hand - kMaxHandSize);
                s.pending = d;
            } else {
                finish_cleanup(s);
            }
            break;
        }
        default: break;
    }
}

void next_step(GameState& s, const CardDatabase& db) {
    TurnState& t = s.turn;
    for (PlayerState& p : s.players) p.pool = ManaPool{};  // 500.4: la riserva si svuota
    t.step_started = false;
    t.passes_in_a_row = 0;
    t.priority_player = t.active_player;
    switch (t.step) {
        case Step::PreGame: throw EngineError("next_step: la partita non è iniziata");
        case Step::Untap: t.step = Step::Upkeep; return;
        case Step::Upkeep: t.step = Step::Draw; return;
        case Step::Draw: t.step = Step::Main1; return;
        case Step::Main1: t.step = Step::BeginCombat; return;
        case Step::BeginCombat: t.step = Step::DeclareAttackers; return;
        // 508.8: senza attaccanti dichiarati si saltano bloccanti e danno.
        case Step::DeclareAttackers:
            t.step = s.combat.attackers.empty() ? Step::EndCombat : Step::DeclareBlockers;
            return;
        // 510.4: lo step del danno da attacco improvviso c'è solo se serve.
        case Step::DeclareBlockers:
            t.step = combat::needs_first_strike_step(s, db) ? Step::FirstStrikeDamage : Step::CombatDamage;
            return;
        case Step::FirstStrikeDamage: t.step = Step::CombatDamage; return;
        case Step::CombatDamage: t.step = Step::EndCombat; return;
        case Step::EndCombat:
            combat::end_combat(s);  // 511.3: tutte le creature escono dal combattimento
            if (t.extra_combats > 0) {
                --t.extra_combats;
                t.step = Step::BeginCombat;
            } else {
                t.step = Step::Main2;
            }
            return;
        case Step::Main2: t.step = Step::End; return;
        case Step::End: t.step = Step::Cleanup; return;
        case Step::Cleanup:
            if (t.cleanup_priority) {  // 514.3a: un'altra sottofase di cancellazione
                t.cleanup_priority = false;
                return;
            }
            t.active_player = other(t.active_player);
            t.priority_player = t.active_player;
            ++t.turn_number;
            t.step = Step::Untap;
            return;
    }
}

void pass_priority(GameState& s, const CardDatabase& db) {
    TurnState& t = s.turn;
    ++t.passes_in_a_row;
    if (t.passes_in_a_row < kNumPlayers) {
        t.priority_player = other(t.priority_player);
        return;
    }
    // Tutti hanno passato di fila: si risolve la cima della pila, o si va avanti.
    t.passes_in_a_row = 0;
    if (!s.stack_items.empty()) {
        resolve_top_of_stack(s, db);
    } else {
        next_step(s, db);
    }
}

void begin_first_turn(GameState& s) {
    TurnState& t = s.turn;
    t.turn_number = 1;
    t.active_player = t.starting_player;
    t.priority_player = t.starting_player;
    t.step = Step::Untap;
    t.step_started = false;
    t.passes_in_a_row = 0;
}

// Fa avanzare la partita finché non serve una decisione.
void advance(GameState& s, const CardDatabase& db, GameLog* log) {
    for (;;) {
        TurnState& t = s.turn;
        if (t.game_over) {
            s.pending = decision(DecisionKind::GameOver, kNoPlayer);
            return;
        }
        if (s.pending.kind != DecisionKind::None || t.step == Step::PreGame) return;

        if (!t.step_started) {
            t.step_started = true;
            LogEntry e;
            e.kind = LogEntry::Kind::StepStart;
            e.player = t.active_player;
            log_event(s, e);
            turn_based_actions(s, db);
            continue;
        }
        if (t.step == Step::Untap) {  // nessuna priorità nello stap
            next_step(s, db);
            continue;
        }
        if (t.step == Step::Cleanup && !t.cleanup_priority) {
            // 514.3: nessuna priorità, a meno che servano azioni di stato o ci siano trigger.
            const bool changed = run_state_based_actions(s, db);
            if (t.game_over || s.pending.kind != DecisionKind::None) continue;
            if (changed || !s.pending_triggers.empty()) {
                t.cleanup_priority = true;
                continue;
            }
            next_step(s, db);
            continue;
        }

        // Prima della priorità: azioni di stato ripetute finché serve, poi i trigger in pila.
        if (run_state_based_actions(s, db)) continue;
        if (!s.pending_triggers.empty()) {
            put_triggers_on_stack(s, db);
            continue;
        }

        s.pending = decision(DecisionKind::Priority, t.priority_player);
        if (legal_actions(s, db).size() == 1) {  // solo "passa": nessuna decisione
            record(log, s, LogEntry::Kind::AutoPass, t.priority_player, DecisionKind::Priority, Action{});
            s.pending = PendingDecision{};
            pass_priority(s, db);
            continue;
        }
        return;
    }
}

// --- Mulligan London (103.5) --------------------------------------------------------

void continue_mulligans(GameState& s, const CardDatabase& db) {
    const auto order = turn_order(s.turn.starting_player);

    // 1. Chi deve ancora dichiarare in questo giro, in ordine di turno.
    for (const PlayerId p : order) {
        if (s.players[p].mulligan == MulliganState::Undecided) {
            s.pending = decision(DecisionKind::Mulligan, p);
            return;
        }
    }
    // 2. Giro concluso: chi ha chiesto il mulligan rimescola la mano e ripesca 7.
    bool new_round = false;
    for (const PlayerId p : order) {
        PlayerState& ps = s.players[p];
        if (ps.mulligan != MulliganState::WantsMulligan) continue;
        StaticVector<ObjRef, kPlayerZoneCapacity> hand;
        for (const CardId id : ps.hand) hand.push_back(s.ref(id));
        move_and_collect(s, db, {hand.begin(), hand.size()}, Zone::Library, Cause::Mulligan, LibraryPosition::Shuffle);
        draw_cards(s, db, p, kOpeningHandSize);
        ++ps.mulligans;
        ps.mulligan = MulliganState::Undecided;
        new_round = true;
    }
    if (new_round) {
        continue_mulligans(s, db);
        return;
    }
    // 3. Tutti hanno tenuto: chi ha fatto mulligan mette carte in fondo, in ordine di turno.
    for (const PlayerId p : order) {
        PlayerState& ps = s.players[p];
        if (ps.mulligan != MulliganState::Kept) continue;
        const auto amount = static_cast<std::uint8_t>(std::min<std::size_t>(ps.mulligans, ps.hand.size()));
        if (amount == 0) {
            ps.mulligan = MulliganState::BottomDone;
            continue;
        }
        PendingDecision d = decision(DecisionKind::BottomCards, p);
        d.amount = amount;
        s.pending = d;
        return;
    }
    begin_first_turn(s);
}

// --- Azioni legali ---------------------------------------------------------------

// Aggiunge l'azione se non ce n'è già una identica su una carta con la stessa definizione
// nella stessa zona e nota agli stessi giocatori: conferire la Phoenix dalla mano o dal cimitero
// sono scelte diverse, e lo sono due copie in mano se l'avversario ne conosce una sola.
void add_unique(std::vector<Action>& out, const GameState& s, const Action& a) {
    const CardObject& o = s.objects[a.card];
    for (const Action& b : out) {
        if (b.type == a.type && b.value == a.value && b.card != kNoCard && s.objects[b.card].def == o.def &&
            s.objects[b.card].zone == o.zone && s.objects[b.card].known_to == o.known_to) {
            return;
        }
    }
    out.push_back(a);
}

void priority_actions(const GameState& s, const CardDatabase& db, PlayerId p, std::vector<Action>& out) {
    out.push_back(Action{ActionType::Pass});
    const TurnState& t = s.turn;
    const PlayerState& ps = s.players[p];

    for (const CardId id : ps.hand) {
        const CardDef& front = db.get(s.objects[id].def);
        // Giocare una terra: giocatore attivo, fase principale, pila vuota, una per turno.
        if (p == t.active_player && is_main_phase(t.step) && s.stack_items.empty() && t.lands_played == 0) {
            if (front.has_type(CardType::Land) && front.rules.implemented) {
                add_unique(out, s, Action{ActionType::PlayLand, id, 0});
            }
            if (front.back_face != kNoDef) {
                const CardDef& back = db.get(front.back_face);
                if (back.has_type(CardType::Land) && back.rules.implemented) {
                    add_unique(out, s, Action{ActionType::PlayLand, id, 1});
                }
            }
        }
        // Lanciare una magia: solo se il lancio si può completare (tempi, bersagli, mana).
        // I bersagli delle carte implementate (giocatori, qualsiasi bersaglio) esistono sempre.
        if (front.rules.implemented && !front.has_type(CardType::Land) && castable_now(s, db, p, front) &&
            can_pay(s, db, p, front.cost) && spell_has_legal_choices(s, db, p, front, s.ref(id), id)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, 0});
        }
        // Force of Negation: "If it's not your turn", esiliare un'altra carta blu dalla mano.
        if (front.rules.implemented && front.rules.pitch_blue && t.active_player != p && castable_now(s, db, p, front) &&
            cards_of_color_in_hand(s, db, p, id, Color::Blue) > 0 &&
            spell_has_legal_choices(s, db, p, front, s.ref(id), id)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Pitch)});
        }
        // Evocazione (702.74a): costo alternativo, esiliare un'altra carta bianca dalla mano.
        if (front.rules.implemented && front.rules.evoke_exile_white && castable_now(s, db, p, front) &&
            white_cards_in_hand(s, db, p, id) > 0) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Evoke)});
        }
        // Warp dalla mano (702.185a): costo alternativo, stessi tempi della carta.
        if (front.rules.implemented && front.rules.warp.available && castable_now(s, db, p, front) &&
            can_pay(s, db, p, front.rules.warp.cost)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Warp)});
        }
        // Conferimento dalla mano (Detective's Phoenix).
        if (can_bestow(s, db, p, id)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Bestow)});
        }
        // Costo alternativo (Flare of Malice): sacrificare una creatura nera non token.
        if (front.rules.implemented && front.rules.alt_sacrifice_black_creature && castable_now(s, db, p, front) &&
            !black_creatures_for_sacrifice(s, db, p).empty()) {
            add_unique(out, s,
                       Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::SacrificeAlternative)});
        }
        // Ciclo: velocità istantaneo, se si può pagare la vita (119.4).
        if (front.rules.cycling_life > 0 && ps.life >= front.rules.cycling_life) {
            add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
        }
        // Ciclo della terra: velocità istantaneo, se si può pagare il mana.
        if (front.rules.landcycling.types != 0 && can_pay(s, db, p, front.rules.landcycling.cost)) {
            add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
        }
    }
    // Dal cimitero: fuga (Nethergoyf) e conferimento (Detective's Phoenix, solo conferita).
    for (const CardId id : ps.graveyard) {
        const CardDef& def = db.get(s.objects[id].def);
        if (!def.rules.implemented) continue;
        if (def.rules.escape.min_types > 0 && castable_now(s, db, p, def) &&
            graveyard_value(s, db, p, ExileMode::CardTypes, id) >= def.rules.escape.min_types &&
            can_pay(s, db, p, def.rules.escape.cost)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Escape)});
        }
        if (def.rules.bestow.from_graveyard && can_bestow(s, db, p, id)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Bestow)});
        }
    }
    // Dall'esilio: una carta esiliata dal warp si lancia per il suo costo, in un turno successivo (702.185a).
    for (const CardId id : s.exile) {
        const CardObject& o = s.objects[id];
        if (o.owner != p || !o.has_flag(ObjectFlag::WarpExiled) || o.zone_turn >= t.turn_number) continue;
        const CardDef& def = db.get(o.def);
        if (def.rules.implemented && castable_now(s, db, p, def) && can_pay(s, db, p, def.cost)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, static_cast<std::uint8_t>(CastMethod::Normal)});
        }
    }
    // Abilità dei permanenti, a velocità istantaneo.
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller != p || o.has_flag(ObjectFlag::Tapped)) continue;
        const CardRules& rules = effective_def(s, db, id).rules;
        const bool fetch = rules.land.fetch_types != 0 && ps.life >= rules.land.fetch_life;
        const bool other_ability = rules.activated.effect != ActivatedEffect::None;
        if (fetch || other_ability) add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
        for (std::size_t i = 0; i < rules.loyalty_count; ++i) {
            if (loyalty_ability_available(s, db, p, id, i)) {
                add_unique(out, s, Action{ActionType::ActivateAbility, id, static_cast<std::uint8_t>(i)});
            }
        }
    }
}

// --- Gestione delle decisioni ---------------------------------------------------------

void on_priority(GameState& s, const CardDatabase& db, PlayerId p, const Action& a, GameLog* log) {
    switch (a.type) {
        case ActionType::Pass: pass_priority(s, db); return;
        case ActionType::PlayLand:
            ++s.turn.lands_played;
            s.turn.passes_in_a_row = 0;
            land_enters(s, db, s.ref(a.card), a.value == 1, Cause::Play);
            return;
        case ActionType::CastSpell:
            begin_cast(s, db, p, a.card, static_cast<CastMethod>(a.value), log);
            return;
        case ActionType::ActivateAbility: {
            const CardObject& o = s.objects[a.card];
            if (o.zone == Zone::Hand && db.get(o.def).rules.landcycling.types != 0) {
                activate_landcycling(s, db, p, a.card, log);
            } else if (o.zone == Zone::Hand) {
                activate_cycling(s, db, p, a.card);
            } else if (effective_def(s, db, a.card).rules.land.fetch_types != 0) {
                activate_fetch(s, db, p, a.card);
            } else if (effective_def(s, db, a.card).rules.loyalty_count > 0) {
                activate_loyalty(s, db, p, a.card, a.value);
            } else {
                activate_permanent_ability(s, db, p, a.card);
            }
            return;
        }
        default: throw EngineError("azione di priorità sconosciuta");
    }
}

// Scelte di carte una alla volta (fondo del grimorio, scarti): restituisce true
// quando sono state fatte tutte.
bool add_choice(GameState& s, PendingDecision d, CardId card) {
    d.chosen.push_back(card);
    if (d.chosen.size() < d.amount) {
        s.pending = d;
        return false;
    }
    return true;
}

StaticVector<ObjRef, kPlayerZoneCapacity> refs_of(const GameState& s, const PlayerZone& cards) {
    StaticVector<ObjRef, kPlayerZoneCapacity> refs;
    for (const CardId id : cards) refs.push_back(s.ref(id));
    return refs;
}

}  // namespace

// --- Interfaccia pubblica ---------------------------------------------------------------

void lose_life(GameState& s, PlayerId p, int amount, CardId source, Cause cause) {
    s.players[p].life = static_cast<std::int16_t>(s.players[p].life - amount);
    log_life(s, p, -amount, source, cause);
}

void gain_life(GameState& s, PlayerId p, int amount, CardId source, Cause cause) {
    if (amount <= 0) return;
    log_life(s, p, amount, source, cause);
    s.players[p].life = static_cast<std::int16_t>(s.players[p].life + amount);
    s.players[p].gained_life_this_turn = true;
}

const CardDef& effective_def(const GameState& s, const CardDatabase& db, CardId id) {
    const CardObject& o = s.objects[id];
    const CardDef& front = db.get(o.def);
    if (o.has_flag(ObjectFlag::BackFace) && front.back_face != kNoDef) return db.get(front.back_face);
    return front;
}

namespace {

// Valore dell'asterisco (604.3): per Nethergoyf, i tipi tra le carte nel cimitero del controllore.
int star_value(const GameState& s, const CardDef& d, CardId id) {
    if (!d.rules.statics.power_from_graveyard_types) {
        throw EngineError(d.name + ": caratteristica definita da un'abilità non implementata");
    }
    return s.players[s.objects[id].controller].gy.distinct_types();
}

// Chiama f(aura) per ogni Aura conferita attaccata all'oggetto `id`.
template <typename F>
void for_each_aura(const GameState& s, CardId id, F&& f) {
    for (const CardId a : s.battlefield) {
        const CardObject& o = s.objects[a];
        if (o.has_flag(ObjectFlag::Bestowed) && o.attached_to.card == id && s.is_valid(o.attached_to)) f(a);
    }
}

// Bonus delle Aure conferite attaccate all'oggetto ("Enchanted creature gets +2/+2").
int aura_bonus(const GameState& s, const CardDatabase& db, CardId id, bool for_power) {
    int bonus = 0;
    for_each_aura(s, id, [&](CardId a) {
        const BestowRules& b = effective_def(s, db, a).rules.bestow;
        bonus += for_power ? b.power : b.toughness;
    });
    return bonus;
}

}  // namespace

int enchantment_power_bonus(const GameState& s, const CardDatabase& db, CardId id) {
    return aura_bonus(s, db, id, true);
}

KeywordMask granted_keywords(const GameState& s, const CardDatabase& db, CardId id) {
    KeywordMask mask = 0;
    for_each_aura(s, id, [&](CardId a) {
        mask = static_cast<KeywordMask>(mask | effective_def(s, db, a).rules.bestow.grants);
    });
    return mask;
}

int bestowed_aura_count(const GameState& s, CardId id) {
    int n = 0;
    for_each_aura(s, id, [&](CardId) { ++n; });
    return n;
}

int power(const GameState& s, const CardDatabase& db, CardId id) {
    const CardDef& d = effective_def(s, db, id);
    const Counters& c = s.objects[id].counters;
    int value = d.base_power + c.plus_one - c.minus_one + aura_bonus(s, db, id, true);
    if (d.power_star) value += star_value(s, d, id);
    if (d.rules.statics.power_per_graveyard_cards > 0) {  // Dark Matter Manipulator
        const int cards = s.players[s.objects[id].controller].gy.card_count;
        value += d.rules.statics.power_bonus * (cards / d.rules.statics.power_per_graveyard_cards);
    }
    return value;
}

int toughness(const GameState& s, const CardDatabase& db, CardId id) {
    const CardDef& d = effective_def(s, db, id);
    const Counters& c = s.objects[id].counters;
    int value = d.base_toughness + c.plus_one - c.minus_one + aura_bonus(s, db, id, false);
    if (d.toughness_star) value += star_value(s, d, id);
    return value;
}

void start_game(GameState& s, const CardDatabase& db, const StartOptions& options, GameLog* log) {
    if (s.turn.step != Step::PreGame || s.pending.kind != DecisionKind::None) {
        throw EngineError("start_game: la partita è già iniziata");
    }
    const LogScope scope(log);
    s.turn.starting_player = options.starting_player;
    s.turn.active_player = options.starting_player;
    s.turn.priority_player = options.starting_player;
    if (options.mulligans) {
        for (const PlayerId p : turn_order(options.starting_player)) draw_cards(s, db, p, kOpeningHandSize);
        continue_mulligans(s, db);
    } else {
        begin_first_turn(s);
    }
    advance(s, db, log);
}

std::vector<Action> legal_actions(const GameState& s, const CardDatabase& db) {
    std::vector<Action> out;
    const PendingDecision& d = s.pending;
    if (d.kind == DecisionKind::None || d.kind == DecisionKind::GameOver) return out;
    const PlayerState& ps = s.players[d.player];

    switch (d.kind) {
        case DecisionKind::Mulligan:
            out.push_back(Action{ActionType::Keep});
            if (ps.mulligans < kMaxMulligans) out.push_back(Action{ActionType::Mulligan});
            break;
        case DecisionKind::BottomCards:
        case DecisionKind::DiscardToHandSize:
            for (const CardId id : ps.hand) {
                if (!d.chosen.contains(id)) add_unique(out, s, Action{ActionType::ChooseCard, id, 0});
            }
            break;
        case DecisionKind::Priority: priority_actions(s, db, d.player, out); break;
        case DecisionKind::PayLifeOrTapped:
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kEnterTapped});
            if (ps.life >= d.amount) out.push_back(Action{ActionType::ChooseOption, kNoCard, kPayLife});
            break;
        case DecisionKind::SearchLibrary:
            // Nel grimorio una bifronte ha solo le caratteristiche del fronte.
            for (const CardId id : ps.library) {
                const CardDef& def = db.get(s.objects[id].def);
                const bool land = def.has_type(CardType::Land) && (def.land_types & d.land_types) != 0;
                if (land || matches_search(s, db, id, d.search)) {
                    add_unique(out, s, Action{ActionType::ChooseCard, id, 0});
                }
            }
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kDeclineSearch});  // 701.23b
            if (d.search.optional) out.push_back(Action{ActionType::ChooseOption, kNoCard, kSkipSearch});
            break;
        case DecisionKind::Surveil:
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kSurveilKeepOnTop});
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kSurveilToGraveyard});
            break;
        case DecisionKind::LegendRule:
            // Nessuna fusione delle copie identiche: possono differire per segnalini o stato.
            for (const CardId id : s.battlefield) {
                if (same_legend_group(s, db, id, d.subject.card)) out.push_back(Action{ActionType::ChooseCard, id, 0});
            }
            break;
        case DecisionKind::ChooseTarget:
            if (d.stack_index != 0xFF) {
                out = copy_target_actions(s, db, d.stack_index);  // una copia (707.10c)
            } else {
                target_actions(s, db, target_context(s.stack_items.back(), db), out);
            }
            break;
        case DecisionKind::PayMana: {
            const ManaCost& cost = cost_of(s.stack_items.back(), db);
            const std::size_t n = payment_options(s, db, d.player, cost).size();
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(i)});
            }
            break;
        }
        case DecisionKind::ChooseFromHand:
            for (const CardId id : s.players[d.target_player].hand) {
                if (!db.get(s.objects[id].def).has_type(CardType::Land)) {
                    add_unique(out, s, Action{ActionType::ChooseCard, id, 0});
                }
            }
            break;
        case DecisionKind::DeclareAttacker:
        case DecisionKind::DeclareBlockers:
        case DecisionKind::DivideDamage: combat::legal_actions(s, db, out); break;
        case DecisionKind::OrderTriggers:
            // Un'opzione per trigger distinto (quelli intercambiabili sono la stessa scelta).
            for (std::size_t i = 0; i < s.pending_triggers.size(); ++i) {
                const StackItem& t = s.pending_triggers[i];
                if (t.controller != d.player) continue;
                bool seen = false;
                for (std::size_t j = 0; j < i; ++j) {
                    const StackItem& u = s.pending_triggers[j];
                    seen = seen || (u.controller == d.player && interchangeable(u, t));
                }
                if (!seen) out.push_back(Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(i)});
            }
            break;
        case DecisionKind::ExileFromGraveyard:
            for (const CardId id :
                 exile_options(s, db, d.player, d.chosen, exile_requirement(s.stack_items.back(), db))) {
                out.push_back(Action{ActionType::ChooseCard, id, 0});
            }
            break;
        case DecisionKind::DiscardCard:
            for (const CardId id : ps.hand) add_unique(out, s, Action{ActionType::ChooseCard, id, 0});
            break;
        case DecisionKind::SacrificeForCost:
            for (const CardId id : black_creatures_for_sacrifice(s, db, d.player)) {
                out.push_back(Action{ActionType::ChooseCard, id, 0});
            }
            break;
        case DecisionKind::SacrificeForEffect:
            for (const CardId id : greatest_mana_value_permanents(s, db, d.player)) {
                out.push_back(Action{ActionType::ChooseCard, id, 0});
            }
            break;
        case DecisionKind::PayOptionalCost:
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kDeclineCost});
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kPayCost});
            break;
        case DecisionKind::ChooseMode: {
            const StackItem& item = s.stack_items.back();
            for (const std::uint8_t m : legal_modes(s, db, d.player, db.get(item.source_def), item.source)) {
                out.push_back(Action{ActionType::ChooseOption, kNoCard, m});
            }
            break;
        }
        case DecisionKind::ChooseX: {
            const StackItem& item = s.stack_items.back();
            for (const std::uint8_t x : x_options(s, db, d.player, db.get(item.source_def), item.source, kNoCard)) {
                out.push_back(Action{ActionType::ChooseOption, kNoCard, x});
            }
            break;
        }
        case DecisionKind::ExileFromHandForCost: {
            const HandExileOptions o = hand_exile_options(s, db, d.player, s.stack_items.back(), d.chosen);
            if (o.can_stop) out.push_back(Action{ActionType::ChooseOption, kNoCard, kStopExiling});
            for (const CardId id : o.cards) out.push_back(Action{ActionType::ChooseCard, id, 0});
            break;
        }
        case DecisionKind::CastFromExile:
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kDeclineCast});
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kCastIt});
            break;
        case DecisionKind::ChooseReplicate: {
            const int max = replicate_max(s, db, d.player, s.stack_items.back());
            for (int k = 0; k <= max; ++k) out.push_back(Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(k)});
            break;
        }
        case DecisionKind::None:
        case DecisionKind::GameOver: break;
    }
    return out;
}

void apply(GameState& s, const CardDatabase& db, const Action& action, GameLog* log) {
    const std::vector<Action> legal = legal_actions(s, db);
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
        throw EngineError("apply: azione non legale per la decisione corrente");
    }
    const LogScope scope(log);
    const PendingDecision d = s.pending;
    record(log, s, LogEntry::Kind::Action, d.player, d.kind, action);
    if (log != nullptr && s.is_valid(d.subject)) {  // il soggetto della decisione, se c'è
        log->entries.back().subject = d.subject.card;
        log->entries.back().def = s.objects[d.subject.card].def;
    }
    if (log != nullptr && action.type == ActionType::TargetStackItem && action.value < s.stack_items.size()) {
        const StackItem& target = s.stack_items[action.value];
        LogEntry& entry = log->entries.back();
        entry.target_def = target.source_def;
        entry.target_is_spell = target.kind == StackKind::Spell;
        entry.target_effect = target.effect;
    }
    s.pending = PendingDecision{};
    PlayerState& ps = s.players[d.player];

    switch (d.kind) {
        case DecisionKind::Priority: on_priority(s, db, d.player, action, log); break;

        case DecisionKind::Mulligan:
            ps.mulligan = action.type == ActionType::Keep ? MulliganState::Kept : MulliganState::WantsMulligan;
            continue_mulligans(s, db);
            break;

        case DecisionKind::BottomCards:
            if (add_choice(s, d, action.card)) {
                // Scelte in ordine: la prima resta più in alto, l'ultima va più in fondo.
                PlayerZone chosen = d.chosen;
                chosen.push_back(action.card);
                const auto refs = refs_of(s, chosen);
                move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Library, Cause::Mulligan,
                                 LibraryPosition::Bottom);
                ps.mulligan = MulliganState::BottomDone;
                continue_mulligans(s, db);
            }
            break;

        case DecisionKind::DiscardToHandSize:
            if (add_choice(s, d, action.card)) {
                // Gli scarti avvengono insieme: un solo evento di gruppo.
                PlayerZone chosen = d.chosen;
                chosen.push_back(action.card);
                const auto refs = refs_of(s, chosen);
                move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Graveyard, Cause::Discard);
                finish_cleanup(s);
            }
            break;

        case DecisionKind::PayLifeOrTapped:
            if (action.value == kPayLife) {
                lose_life(s, d.player, d.amount, d.subject.card, Cause::Cost);
            } else if (s.is_valid(d.subject)) {
                s.objects[d.subject.card].set_flag(ObjectFlag::Tapped);
            }
            break;

        case DecisionKind::SearchLibrary:
            // Fetch: si mette in campo e poi si mescola; se la terra chiede di pagare vita, la
            // scelta arriva dopo il mescolamento (non dipende dall'ordine del grimorio).
            // Ciclo della terra e trigger di entrata: la carta si rivela e va in mano, poi si mescola.
            // "You may search" rifiutato: nessuna ricerca, nessun mescolamento.
            if (action.type == ActionType::ChooseOption && action.value == kSkipSearch) break;
            if (action.type == ActionType::ChooseCard) {
                if (d.destination == Zone::Hand) {
                    move_and_collect(s, db, one(s.ref(action.card)), Zone::Hand, Cause::Search);
                    s.objects[action.card].known_to = kAllPlayers;  // "reveal it"
                } else {
                    land_enters(s, db, s.ref(action.card), false, Cause::Search);
                }
            }
            shuffle_library(s, d.player);
            break;

        case DecisionKind::Surveil: {
            const ObjRef top = s.ref(ps.library.back());
            if (action.value == kSurveilToGraveyard) {
                move_and_collect(s, db, one(top), Zone::Graveyard, Cause::Surveil);
            } else {
                s.objects[top.card].known_to = static_cast<std::uint8_t>(s.objects[top.card].known_to | (1u << d.player));
            }
            break;
        }

        case DecisionKind::LegendRule: s.legend_keep.push_back(s.ref(action.card)); break;

        case DecisionKind::ChooseTarget: {
            const bool copy = d.stack_index != 0xFF;
            StackItem& item = copy ? s.stack_items[d.stack_index] : s.stack_items.back();
            if (action.type == ActionType::TargetPlayer) {
                item.target = Target{TargetKindTag::Player, action.value, ObjRef{}};
            } else if (action.type == ActionType::TargetObject) {
                item.target = Target{TargetKindTag::Object, kNoPlayer, s.ref(action.card)};
            } else if (action.type == ActionType::TargetStackItem) {
                item.target = Target{TargetKindTag::StackItem, kNoPlayer, ObjRef{}};
                item.target_id = s.stack_items[action.value].id;
            } else if (!copy && target_kind_of(item, db) == TargetKind::Players) {
                item.player_mask = action.value;  // "any number of target players"
            }  // altrimenti kNoTarget ("up to one") o kKeepTarget (copia): niente da cambiare
            if (copy) {
                continue_copy_targets(s, db, d.player, d.stack_index + std::size_t{1}, d.amount);
            } else {
                after_targets(s, db, d.player, log);
            }
            break;
        }

        case DecisionKind::PayMana: {
            const ManaCost& cost = cost_of(s.stack_items.back(), db);
            const std::vector<PaymentOption> options = payment_options(s, db, d.player, cost);
            pay_mana(s, db, d.player, cost, options[action.value]);
            after_mana(s, db, d.player);
            break;
        }

        case DecisionKind::ChooseFromHand:
            move_and_collect(s, db, one(s.ref(action.card)), Zone::Graveyard, Cause::Discard);
            finish_thoughtseize(s, db, d.player, d.subject);
            break;

        case DecisionKind::DeclareAttacker:
        case DecisionKind::DeclareBlockers:
        case DecisionKind::DivideDamage: combat::apply(s, db, d, action); break;

        case DecisionKind::OrderTriggers: {
            // Il trigger scelto va in pila ora: si risolverà dopo quelli messi dopo di lui.
            const StackItem trigger = s.pending_triggers[action.value];
            s.pending_triggers.erase_at(action.value);
            push_trigger(s, db, trigger);
            break;
        }

        case DecisionKind::ExileFromGraveyard: {
            PlayerZone chosen = d.chosen;
            chosen.push_back(action.card);
            continue_graveyard_exile(s, db, d.player, chosen, log);
            break;
        }

        case DecisionKind::DiscardCard:  // Fear of Missing Out: scarta, poi pesca
            move_and_collect(s, db, one(s.ref(action.card)), Zone::Graveyard, Cause::Discard);
            draw_cards(s, db, d.player, 1);
            break;

        case DecisionKind::SacrificeForCost: sacrifice_for_cost(s, db, d.player, action.card); break;

        case DecisionKind::SacrificeForEffect:  // Flare of Malice: l'avversario ha scelto
            move_and_collect(s, db, one(s.ref(action.card)), Zone::Graveyard, Cause::Sacrifice);
            move_and_collect(s, db, one(d.subject), Zone::Graveyard, Cause::Resolve);
            break;

        case DecisionKind::ChooseMode:
            s.stack_items.back().mode = action.value;
            choose_x(s, db, d.player, log);
            break;

        case DecisionKind::ChooseX:
            s.stack_items.back().x = action.value;
            choose_targets(s, db, d.player, log);
            break;

        case DecisionKind::ExileFromHandForCost: {
            PlayerZone chosen = d.chosen;
            if (action.type == ActionType::ChooseCard) chosen.push_back(action.card);
            continue_hand_exile(s, db, d.player, chosen, log, action.type == ActionType::ChooseOption);
            break;
        }

        case DecisionKind::ChooseReplicate:
            s.stack_items.back().replicate = action.value;
            begin_payment(s, db, d.player, log);
            break;

        case DecisionKind::CastFromExile:  // rimbalzo: lanciata senza pagare il costo di mana
            if (action.value == kCastIt) begin_cast(s, db, d.player, d.subject.card, CastMethod::Free, log);
            break;

        case DecisionKind::PayOptionalCost:  // Guide of Souls: paga {E}{E}{E}, poi "When you do" (603.12)
            if (action.value == kPayCost) {
                ps.energy = static_cast<std::uint8_t>(ps.energy - d.amount);
                const CardDefId source_def = s.objects[d.subject.card].def;  // una carta: la definizione resta
                s.pending_triggers.push_back(
                    triggered(StackEffect::AttackCounters, d.player, d.subject, source_def, 0));
            }
            break;

        case DecisionKind::None:
        case DecisionKind::GameOver: throw EngineError("apply: nessuna decisione in sospeso");
    }
    advance(s, db, log);
}

std::vector<std::vector<CardId>> graveyard_exile_sets(const GameState& s, const CardDatabase& db, PlayerId p,
                                                     CardId card, CastMethod method) {
    const CardDef& def = db.get(s.objects[card].def);
    ExileRequirement req;
    if (method == CastMethod::Bestow && def.rules.bestow.evidence > 0) {
        req = {ExileMode::ManaValue, def.rules.bestow.evidence};
    } else if (method == CastMethod::Escape && def.rules.escape.min_types > 0) {
        req = {ExileMode::CardTypes, def.rules.escape.min_types};
    } else {
        return {};
    }
    const std::vector<CardId> cands = exile_candidates(s, db, p, req.mode, card);
    std::vector<std::vector<CardId>> out;
    for (const std::vector<int>& set : minimal_exile_sets(s, db, cands, req)) {
        std::vector<CardId> cards;
        for (const int i : set) cards.push_back(cands[static_cast<std::size_t>(i)]);
        out.push_back(std::move(cards));
    }
    return out;
}

}  // namespace mtg
