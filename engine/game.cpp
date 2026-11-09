#include "game.hpp"

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
}

// Ogni spostamento del motore passa da qui, così nessun trigger va perso.
ZoneChangeBatch move_and_collect(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                                 Cause cause, LibraryPosition position = LibraryPosition::Unspecified) {
    ZoneChangeBatch batch = move_objects(s, db, refs, to, cause, position);
    collect_triggers(s, db, batch);
    return batch;
}

// Due trigger dello stesso giocatore sono intercambiabili se fanno esattamente la
// stessa cosa: in quel caso l'ordine in pila non conta e non serve una scelta.
bool interchangeable(const StackItem& a, const StackItem& b) {
    return a.kind == b.kind && a.effect == b.effect && a.amount == b.amount && a.source_def == b.source_def &&
           a.land_types == b.land_types && a.target == b.target;
}

// APNAP: prima i trigger del giocatore attivo, poi quelli dell'altro, così
// quelli dell'altro si risolvono per primi.
void put_triggers_on_stack(GameState& s) {
    for (const PlayerId p : turn_order(s.turn.active_player)) {
        const StackItem* first = nullptr;
        for (const StackItem& t : s.pending_triggers) {
            if (t.controller != p) continue;
            if (first != nullptr && !interchangeable(*first, t)) {
                // Con le carte implementate finora trigger simultanei diversi dello stesso
                // giocatore sono impossibili; la scelta dell'ordine arriverà con le carte che li producono.
                throw EngineError("trigger simultanei diversi dello stesso giocatore: ordinamento non ancora implementato");
            }
            if (first == nullptr) first = &t;
        }
        for (const StackItem& t : s.pending_triggers) {
            if (t.controller == p) s.stack_items.push_back(t);
        }
    }
    s.pending_triggers.clear();
    s.turn.passes_in_a_row = 0;
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

void lose_life(GameState& s, PlayerId p, int amount) {
    s.players[p].life = static_cast<std::int16_t>(s.players[p].life - amount);
}

// --- Bersagli ----------------------------------------------------------------------

TargetKind target_kind_of(const StackItem& item, const CardDatabase& db) {
    const CardDef& def = db.get(item.source_def);
    return item.kind == StackKind::Spell ? def.rules.spell.target : def.rules.activated.target;
}

bool is_creature_on_battlefield(const GameState& s, const CardDatabase& db, CardId id) {
    return s.objects[id].zone == Zone::Battlefield && effective_def(s, db, id).has_type(CardType::Creature);
}

// Il bersaglio è ancora legale alla risoluzione? (608.2b) L'ObjRef scopre se l'oggetto
// ha cambiato zona nel frattempo (400.7).
bool target_still_legal(const GameState& s, const CardDatabase& db, const Target& t, TargetKind kind) {
    switch (t.tag) {
        case TargetKindTag::Player: return t.player < kNumPlayers;
        case TargetKindTag::Object:
            return kind == TargetKind::AnyTarget && s.is_valid(t.object) &&
                   is_creature_on_battlefield(s, db, t.object.card);
        case TargetKindTag::None: return kind == TargetKind::None;
    }
    return false;
}

// Due permanenti indistinguibili come bersagli: stessa carta, stesso controllore, stesso stato.
bool same_permanent_state(const GameState& s, CardId a, CardId b) {
    const CardObject& x = s.objects[a];
    const CardObject& y = s.objects[b];
    return x.def == y.def && x.controller == y.controller && x.damage == y.damage && x.flags == y.flags &&
           x.counters.plus_one == y.counters.plus_one && x.counters.minus_one == y.counters.minus_one &&
           x.counters.keywords == y.counters.keywords;
}

void target_actions(const GameState& s, const CardDatabase& db, TargetKind kind, std::vector<Action>& out) {
    if (kind == TargetKind::AnyTarget || kind == TargetKind::Player) {
        for (PlayerId p = 0; p < kNumPlayers; ++p) out.push_back(Action{ActionType::TargetPlayer, kNoCard, p});
    }
    if (kind == TargetKind::AnyTarget) {
        for (const CardId id : s.battlefield) {
            if (!is_creature_on_battlefield(s, db, id)) continue;
            const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Action& a) {
                return a.type == ActionType::TargetObject && same_permanent_state(s, a.card, id);
            });
            if (!duplicate) out.push_back(Action{ActionType::TargetObject, id, 0});
        }
    }
}

// --- Lancio delle magie e attivazione delle abilità (601.2, 602.2) ------------------------

bool castable_now(const GameState& s, PlayerId p, const CardDef& def) {
    if (def.has_type(CardType::Instant)) return true;  // velocità istantaneo: basta avere la priorità
    const TurnState& t = s.turn;
    return p == t.active_player && is_main_phase(t.step) && s.stack_items.empty();
}

void complete_cast(GameState& s, PlayerId p, bool spell) {
    if (spell) ++s.players[p].spells_cast_this_turn;
    s.turn.passes_in_a_row = 0;
    s.turn.priority_player = p;  // 117.3c: chi ha lanciato o attivato riceve la priorità
}

// 601.2g-h: abilità di mana e pagamento. Un solo modo di pagare: niente decisione.
void begin_payment(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    const StackItem& item = s.stack_items.back();
    const ManaCost& cost = db.get(item.source_def).cost;
    const std::vector<PaymentOption> options = payment_options(s, db, p, cost);
    if (options.empty()) throw EngineError("lancio senza modi di pagare: l'azione non doveva essere legale");
    if (options.size() == 1) {
        record(log, s, LogEntry::Kind::AutoPay, p, DecisionKind::PayMana, Action{ActionType::ChooseOption, kNoCard, 0});
        pay_mana(s, db, p, cost, options.front());
        complete_cast(s, p, true);
        return;
    }
    PendingDecision d = decision(DecisionKind::PayMana, p);
    d.subject = item.source;
    s.pending = d;
}

// Costi non di mana delle abilità attivate con bersaglio (Mishra's Bauble: {T}, sacrifica).
void pay_ability_costs(GameState& s, const CardDatabase& db, PlayerId p) {
    StackItem& item = s.stack_items.back();
    s.objects[item.source.card].set_flag(ObjectFlag::Tapped);
    const ObjRef source = item.source;
    const ObjRef sacrificed = move_and_collect(s, db, one(source), Zone::Graveyard, Cause::Cost).moves[0].new_ref;
    s.stack_items.back().source = sacrificed;  // `item` può non essere più valido dopo lo spostamento
    complete_cast(s, p, false);
}

// Dopo i bersagli (o subito, se non ce ne sono): costi e fine del lancio.
void after_targets(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    if (s.stack_items.back().kind == StackKind::Spell) {
        begin_payment(s, db, p, log);
    } else {
        pay_ability_costs(s, db, p);
    }
}

void begin_cast(GameState& s, const CardDatabase& db, PlayerId p, CardId card, GameLog* log) {
    const CardDef& def = db.get(s.objects[card].def);
    // 601.2a: la carta va in pila per prima cosa.
    const ObjRef on_stack = move_and_collect(s, db, one(s.ref(card)), Zone::Stack, Cause::Cast).moves[0].new_ref;
    StackItem item;
    item.kind = StackKind::Spell;
    item.controller = p;
    item.source = on_stack;
    item.source_def = def.id;
    s.stack_items.push_back(item);

    if (def.rules.spell.target != TargetKind::None) {  // 601.2c
        PendingDecision d = decision(DecisionKind::ChooseTarget, p);
        d.subject = on_stack;
        s.pending = d;
        return;
    }
    after_targets(s, db, p, log);
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
    lose_life(s, p, def.rules.land.fetch_life);
    item.source = move_and_collect(s, db, one(s.ref(card)), Zone::Graveyard, Cause::Cost).moves[0].new_ref;

    s.stack_items.push_back(item);
    complete_cast(s, p, false);
}

// "Cycling—Pay N life": costo = paga N vita e scarta questa carta; effetto = pesca una carta.
void activate_cycling(GameState& s, const CardDatabase& db, PlayerId p, CardId card) {
    const CardDef& def = db.get(s.objects[card].def);
    lose_life(s, p, def.rules.cycling_life);
    const ObjRef discarded = move_and_collect(s, db, one(s.ref(card)), Zone::Graveyard, Cause::Discard).moves[0].new_ref;

    StackItem item;
    item.kind = StackKind::ActivatedAbility;
    item.effect = StackEffect::Draw;
    item.controller = p;
    item.source = discarded;
    item.source_def = def.id;
    item.amount = 1;
    s.stack_items.push_back(item);
    complete_cast(s, p, false);
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
    s.stack_items.push_back(item);

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
        if (d.has_type(CardType::Creature)) {
            const int tough = toughness(s, db, id);
            dies = tough <= 0 || o.damage >= tough;  // costituzione 0 o meno, danno letale
        }
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
        remove_token(s, token);
        changed = true;
    }
    s.legend_keep.clear();
    return changed;
}

// --- Risoluzione ------------------------------------------------------------------------

void deal_damage(GameState& s, const Target& target, int amount) {
    if (target.tag == TargetKindTag::Player) {
        lose_life(s, target.player, amount);  // il danno a un giocatore gli fa perdere vita (120.3a)
    } else {
        CardObject& o = s.objects[target.object.card];
        o.damage = static_cast<std::uint8_t>(std::min(255, o.damage + amount));
    }
}

void finish_thoughtseize(GameState& s, const CardDatabase& db, PlayerId caster, ObjRef spell) {
    lose_life(s, caster, db.get(s.objects[spell.card].def).rules.spell.life_loss);
    move_and_collect(s, db, one(spell), Zone::Graveyard, Cause::Resolve);
}

void resolve_spell(GameState& s, const CardDatabase& db, const StackItem& item) {
    const ObjRef card = item.source;
    if (!s.is_valid(card) || s.objects[card.card].zone != Zone::Stack) {
        throw EngineError("risoluzione: la carta della magia non è più in pila");
    }
    const CardDef& def = db.get(item.source_def);
    const SpellRules& spell = def.rules.spell;

    // 608.2b: se tutti i bersagli sono illegali la magia non si risolve.
    if (spell.target != TargetKind::None && !target_still_legal(s, db, item.target, spell.target)) {
        move_and_collect(s, db, one(card), Zone::Graveyard, Cause::Fizzle);
        return;
    }
    // Magia permanente: entra in campo (i trigger di entrata scattano qui).
    if ((def.types & kPermanentTypes) != 0) {
        move_and_collect(s, db, one(card), Zone::Battlefield, Cause::Resolve);
        return;
    }
    switch (spell.effect) {
        case SpellEffect::DealDamage: deal_damage(s, item.target, spell.amount); break;
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
        case SpellEffect::None: break;
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
        case StackEffect::FetchSearch: {
            PendingDecision d = decision(DecisionKind::SearchLibrary, item.controller);
            d.land_types = item.land_types;
            s.pending = d;
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
            // "malati"; nessuna creatura ha ancora attaccato in questo turno.
            for (CardObject& o : s.objects) o.clear_flag(ObjectFlag::AttackedThisTurn);
            for (const CardId id : s.battlefield) {
                CardObject& o = s.objects[id];
                if (o.controller != active) continue;
                o.clear_flag(ObjectFlag::Tapped);
                o.clear_flag(ObjectFlag::SummoningSick);
            }
            t.lands_played = 0;
            for (PlayerState& p : s.players) p.spells_cast_this_turn = 0;
            s.players[active].sorceries_as_flash = false;  // il +1 di Teferi dura fino al prossimo turno
            break;
        case Step::Upkeep: {
            // "At the beginning of the next turn's upkeep": i trigger ritardati creati nei turni precedenti.
            StaticVector<DelayedTrigger, 16> waiting;
            for (const DelayedTrigger& d : s.delayed) {
                if (d.created_turn < t.turn_number) {
                    s.pending_triggers.push_back(triggered(d.effect, d.controller, ObjRef{}, d.source_def, d.amount));
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

void next_step(GameState& s) {
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
        // 508.8: senza attaccanti dichiarati si saltano bloccanti e danno. Il combattimento
        // non esiste ancora, quindi nessuno attacca.
        case Step::DeclareAttackers: t.step = Step::EndCombat; return;
        case Step::DeclareBlockers: t.step = Step::CombatDamage; return;
        case Step::CombatDamage: t.step = Step::EndCombat; return;
        case Step::EndCombat:
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
        next_step(s);
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
            turn_based_actions(s, db);
            continue;
        }
        if (t.step == Step::Untap) {  // nessuna priorità nello stap
            next_step(s);
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
            next_step(s);
            continue;
        }

        // Prima della priorità: azioni di stato ripetute finché serve, poi i trigger in pila.
        if (run_state_based_actions(s, db)) continue;
        if (!s.pending_triggers.empty()) {
            put_triggers_on_stack(s);
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

// Aggiunge l'azione se non ce n'è già una identica su una carta con la stessa definizione.
void add_unique(std::vector<Action>& out, const GameState& s, const Action& a) {
    const CardDefId def = s.objects[a.card].def;
    for (const Action& b : out) {
        if (b.type == a.type && b.value == a.value && b.card != kNoCard && s.objects[b.card].def == def) return;
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
        if (front.rules.implemented && !front.has_type(CardType::Land) && castable_now(s, p, front) &&
            can_pay(s, db, p, front.cost)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, 0});
        }
        // Ciclo: velocità istantaneo, se si può pagare la vita (119.4).
        if (front.rules.cycling_life > 0 && ps.life >= front.rules.cycling_life) {
            add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
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
        case ActionType::CastSpell: begin_cast(s, db, p, a.card, log); return;
        case ActionType::ActivateAbility: {
            const CardObject& o = s.objects[a.card];
            if (o.zone == Zone::Hand) {
                activate_cycling(s, db, p, a.card);
            } else if (effective_def(s, db, a.card).rules.land.fetch_types != 0) {
                activate_fetch(s, db, p, a.card);
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

const CardDef& effective_def(const GameState& s, const CardDatabase& db, CardId id) {
    const CardObject& o = s.objects[id];
    const CardDef& front = db.get(o.def);
    if (o.has_flag(ObjectFlag::BackFace) && front.back_face != kNoDef) return db.get(front.back_face);
    return front;
}

int power(const GameState& s, const CardDatabase& db, CardId id) {
    const CardDef& d = effective_def(s, db, id);
    if (d.power_star) throw EngineError(d.name + ": forza definita da un'abilità, non ancora implementata");
    const Counters& c = s.objects[id].counters;
    return d.base_power + c.plus_one - c.minus_one;
}

int toughness(const GameState& s, const CardDatabase& db, CardId id) {
    const CardDef& d = effective_def(s, db, id);
    if (d.toughness_star) throw EngineError(d.name + ": costituzione definita da un'abilità, non ancora implementata");
    const Counters& c = s.objects[id].counters;
    return d.base_toughness + c.plus_one - c.minus_one;
}

void start_game(GameState& s, const CardDatabase& db, const StartOptions& options, GameLog* log) {
    if (s.turn.step != Step::PreGame || s.pending.kind != DecisionKind::None) {
        throw EngineError("start_game: la partita è già iniziata");
    }
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
                if (def.has_type(CardType::Land) && (def.land_types & d.land_types) != 0) {
                    add_unique(out, s, Action{ActionType::ChooseCard, id, 0});
                }
            }
            out.push_back(Action{ActionType::ChooseOption, kNoCard, kDeclineSearch});  // 701.19b
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
            target_actions(s, db, target_kind_of(s.stack_items.back(), db), out);
            break;
        case DecisionKind::PayMana: {
            const ManaCost& cost = db.get(s.stack_items.back().source_def).cost;
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
    const PendingDecision d = s.pending;
    record(log, s, LogEntry::Kind::Action, d.player, d.kind, action);
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
                lose_life(s, d.player, d.amount);
            } else if (s.is_valid(d.subject)) {
                s.objects[d.subject.card].set_flag(ObjectFlag::Tapped);
            }
            break;

        case DecisionKind::SearchLibrary:
            // Si mette in campo e poi si mescola. Se la terra chiede di pagare vita, la
            // scelta arriva dopo il mescolamento: non dipende dall'ordine del grimorio.
            if (action.type == ActionType::ChooseCard) land_enters(s, db, s.ref(action.card), false, Cause::Search);
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
            Target& target = s.stack_items.back().target;
            if (action.type == ActionType::TargetPlayer) {
                target = Target{TargetKindTag::Player, action.value, ObjRef{}};
            } else {
                target = Target{TargetKindTag::Object, kNoPlayer, s.ref(action.card)};
            }
            after_targets(s, db, d.player, log);
            break;
        }

        case DecisionKind::PayMana: {
            const ManaCost& cost = db.get(s.stack_items.back().source_def).cost;
            const std::vector<PaymentOption> options = payment_options(s, db, d.player, cost);
            pay_mana(s, db, d.player, cost, options[action.value]);
            complete_cast(s, d.player, true);
            break;
        }

        case DecisionKind::ChooseFromHand:
            move_and_collect(s, db, one(s.ref(action.card)), Zone::Graveyard, Cause::Discard);
            finish_thoughtseize(s, db, d.player, d.subject);
            break;

        case DecisionKind::None:
        case DecisionKind::GameOver: throw EngineError("apply: nessuna decisione in sospeso");
    }
    advance(s, db, log);
}

}  // namespace mtg
