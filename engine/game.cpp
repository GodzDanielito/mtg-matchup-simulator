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
    for (const ZoneChange& m : batch.moves) {
        // 614.1c: "enters with N counters" si applica mentre entra, prima dei trigger.
        if (m.to != Zone::Battlefield) continue;
        const std::uint8_t n = effective_def(s, db, m.card).rules.statics.enters_with_minus_one;
        if (n > 0) s.objects[m.card].counters.minus_one = n;
    }
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
// Se un giocatore ha trigger diversi (non intercambiabili), sceglie lui l'ordine
// (603.3b): decisione OrderTriggers, un trigger alla volta.
void put_triggers_on_stack(GameState& s) {
    for (const PlayerId p : turn_order(s.turn.active_player)) {
        const StackItem* first = nullptr;
        bool all_same = true;
        for (const StackItem& t : s.pending_triggers) {
            if (t.controller != p) continue;
            if (first == nullptr) {
                first = &t;
            } else if (!interchangeable(*first, t)) {
                all_same = false;
            }
        }
        if (first == nullptr) continue;
        if (!all_same) {
            s.pending = decision(DecisionKind::OrderTriggers, p);
            return;
        }
        StaticVector<StackItem, 16> others;
        for (const StackItem& t : s.pending_triggers) {
            if (t.controller == p) {
                s.stack_items.push_back(t);
            } else {
                others.push_back(t);
            }
        }
        s.pending_triggers = others;
        s.turn.passes_in_a_row = 0;
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
bool target_still_legal(const GameState& s, const CardDatabase& db, const Target& t, TargetKind kind,
                        ColorMask source_colors) {
    switch (t.tag) {
        case TargetKindTag::Player: return t.player < kNumPlayers;
        case TargetKindTag::Object:
            return kind == TargetKind::AnyTarget && s.is_valid(t.object) &&
                   is_creature_on_battlefield(s, db, t.object.card) &&
                   (protection_of(s, db, t.object.card) & source_colors) == 0;  // 702.16b
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

// `source_colors`: colori della magia o della fonte dell'abilità (protezione, 702.16b).
void target_actions(const GameState& s, const CardDatabase& db, TargetKind kind, ColorMask source_colors,
                    std::vector<Action>& out) {
    if (kind == TargetKind::AnyTarget || kind == TargetKind::Player) {
        for (PlayerId p = 0; p < kNumPlayers; ++p) out.push_back(Action{ActionType::TargetPlayer, kNoCard, p});
    }
    if (kind == TargetKind::AnyTarget) {
        for (const CardId id : s.battlefield) {
            if (!is_creature_on_battlefield(s, db, id)) continue;
            if ((protection_of(s, db, id) & source_colors) != 0) continue;  // protezione: non bersagliabile
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

// Costo di mana della magia o dell'abilità in cima alla pila.
const ManaCost& cost_of(const StackItem& item, const CardDatabase& db) {
    const CardDef& def = db.get(item.source_def);
    if (item.kind == StackKind::Spell) return item.method == CastMethod::Escape ? def.rules.escape.cost : def.cost;
    if (item.effect == StackEffect::CycleSearch) return def.rules.landcycling.cost;
    throw EngineError("cost_of: abilità senza costo di mana");
}

// Dopo il mana, gli altri costi e la fine del lancio o dell'attivazione (601.2h:
// i costi si pagano in qualsiasi ordine).
void after_mana(GameState& s, const CardDatabase& db, PlayerId p) {
    const StackItem& item = s.stack_items.back();
    if (item.kind == StackKind::Spell) {
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
    const ManaCost& cost = cost_of(item, db);
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
    s.objects[item.source.card].set_flag(ObjectFlag::Tapped);
    const ObjRef source = item.source;
    const ObjRef sacrificed = move_and_collect(s, db, one(source), Zone::Graveyard, Cause::Cost).moves[0].new_ref;
    s.stack_items.back().source = sacrificed;  // `item` può non essere più valido dopo lo spostamento
    complete_cast(s, p, false);
}

// --- Esilio dal cimitero per la fuga (Nethergoyf) ---------------------------------
//
// Si offrono solo gli insiemi MINIMALI di carte con almeno N tipi (nessuna carta
// superflua): esiliarne di più è legale ("any number") ma sempre peggio per chi lancia.
// Gli insiemi si costruiscono una carta alla volta in ordine canonico, quindi ognuno
// si raggiunge in un solo modo; carte identiche sono una sola candidata.

// Una carta per definizione, in ordine canonico (definizione, poi CardId).
std::vector<CardId> exile_candidates(const GameState& s, PlayerId p) {
    std::vector<std::pair<CardDefId, CardId>> sorted;
    for (const CardId id : s.players[p].graveyard) {
        if (!s.objects[id].has_flag(ObjectFlag::Token)) sorted.emplace_back(s.objects[id].def, id);
    }
    std::sort(sorted.begin(), sorted.end());
    std::vector<CardId> out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i == 0 || sorted[i].first != sorted[i - 1].first) out.push_back(sorted[i].second);
    }
    return out;
}

int count_types(const GameState& s, const CardDatabase& db, const std::vector<CardId>& cards) {
    TypeMask mask = 0;
    for (const CardId id : cards) mask = static_cast<TypeMask>(mask | db.get(s.objects[id].def).types);
    return std::popcount(mask);
}

// Insiemi minimali con almeno `need` tipi, come elenchi crescenti di posizioni in `cands`.
// Un insieme minimale ha al più `need` carte: ognuna deve portare un tipo che le altre non hanno.
std::vector<std::vector<int>> minimal_type_sets(const GameState& s, const CardDatabase& db,
                                                const std::vector<CardId>& cands, int need) {
    std::vector<std::vector<int>> out;
    std::vector<int> current;
    auto rec = [&](auto&& self, int start) -> void {
        if (!current.empty()) {
            std::vector<CardId> set;
            for (const int i : current) set.push_back(cands[static_cast<std::size_t>(i)]);
            if (count_types(s, db, set) >= need) {
                bool minimal = true;
                for (std::size_t j = 0; j < set.size() && minimal; ++j) {
                    std::vector<CardId> without = set;
                    without.erase(without.begin() + static_cast<std::ptrdiff_t>(j));
                    minimal = count_types(s, db, without) < need;
                }
                if (minimal) out.push_back(current);
                return;  // aggiungere altre carte darebbe insiemi non minimali
            }
        }
        if (static_cast<int>(current.size()) >= need) return;
        for (int i = start; i < static_cast<int>(cands.size()); ++i) {
            current.push_back(i);
            self(self, i + 1);
            current.pop_back();
        }
    };
    rec(rec, 0);
    return out;
}

// Carte offribili come prossima scelta, dato quanto già scelto.
std::vector<CardId> exile_options(const GameState& s, const CardDatabase& db, PlayerId p, const PlayerZone& chosen,
                                  int need) {
    const std::vector<CardId> cands = exile_candidates(s, p);
    std::vector<int> picked;
    for (const CardId id : chosen) {
        const auto it = std::find(cands.begin(), cands.end(), id);
        if (it == cands.end()) throw EngineError("esilio per la fuga: scelta non valida");
        picked.push_back(static_cast<int>(it - cands.begin()));
    }
    std::vector<CardId> out;
    for (const std::vector<int>& set : minimal_type_sets(s, db, cands, need)) {
        if (set.size() <= picked.size() || !std::equal(picked.begin(), picked.end(), set.begin())) continue;
        const CardId next = cands[static_cast<std::size_t>(set[picked.size()])];
        if (std::find(out.begin(), out.end(), next) == out.end()) out.push_back(next);
    }
    return out;
}

void begin_payment(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log);

// Prosegue la scelta delle carte da esiliare; a insieme completo le esilia (un evento)
// e passa al pagamento del mana.
void continue_escape_exile(GameState& s, const CardDatabase& db, PlayerId p, PlayerZone chosen, GameLog* log) {
    const int need = db.get(s.stack_items.back().source_def).rules.escape.min_types;
    for (;;) {
        const std::vector<CardId> chosen_cards(chosen.begin(), chosen.end());
        if (count_types(s, db, chosen_cards) >= need) break;
        const std::vector<CardId> options = exile_options(s, db, p, chosen, need);
        if (options.empty()) throw EngineError("fuga senza insiemi di esilio: l'azione non doveva essere legale");
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
    move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Exile, Cause::Cost);
    begin_payment(s, db, p, log);
}

// Tipi tra le altre carte del cimitero (la carta che sta fuggendo non conta).
int other_graveyard_types(const GameState& s, const CardDatabase& db, PlayerId p, CardId except) {
    std::vector<CardId> others;
    for (const CardId id : s.players[p].graveyard) {
        if (id != except && !s.objects[id].has_flag(ObjectFlag::Token)) others.push_back(id);
    }
    return count_types(s, db, others);
}

// Dopo i bersagli (o subito, se non ce ne sono): costi e fine del lancio.
void after_targets(GameState& s, const CardDatabase& db, PlayerId p, GameLog* log) {
    const StackItem& item = s.stack_items.back();
    if (item.kind == StackKind::Spell) {
        if (item.method == CastMethod::Escape) {
            continue_escape_exile(s, db, p, PlayerZone{}, log);
        } else {
            begin_payment(s, db, p, log);
        }
    } else {
        pay_ability_costs(s, db, p);
    }
}

void begin_cast(GameState& s, const CardDatabase& db, PlayerId p, CardId card, CastMethod method, GameLog* log) {
    const CardDef& def = db.get(s.objects[card].def);
    // 601.2a: la carta va in pila per prima cosa (dalla mano, o dal cimitero con la fuga).
    const ObjRef on_stack = move_and_collect(s, db, one(s.ref(card)), Zone::Stack, Cause::Cast).moves[0].new_ref;
    StackItem item;
    item.kind = StackKind::Spell;
    item.controller = p;
    item.source = on_stack;
    item.source_def = def.id;
    item.method = method;
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
    s.stack_items.push_back(item);
    begin_payment(s, db, p, log);
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

void deal_damage(GameState& s, const CardDatabase& db, const Target& target, int amount, ColorMask source_colors) {
    if (target.tag == TargetKindTag::Player) {
        lose_life(s, target.player, amount);  // il danno a un giocatore gli fa perdere vita (120.3a)
    } else {
        CardObject& o = s.objects[target.object.card];
        const int dealt = combat::damage_after_prevention(s, db, target.object.card, source_colors, amount);
        o.damage = static_cast<std::uint8_t>(std::min(255, o.damage + dealt));
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
    if (spell.target != TargetKind::None && !target_still_legal(s, db, item.target, spell.target, def.colors)) {
        move_and_collect(s, db, one(card), Zone::Graveyard, Cause::Fizzle);
        return;
    }
    // Magia permanente: entra in campo (i trigger di entrata scattano qui).
    if ((def.types & kPermanentTypes) != 0) {
        move_and_collect(s, db, one(card), Zone::Battlefield, Cause::Resolve);
        return;
    }
    switch (spell.effect) {
        case SpellEffect::DealDamage: deal_damage(s, db, item.target, spell.amount, def.colors); break;
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
        case StackEffect::FetchSearch:
        case StackEffect::CycleSearch: {
            PendingDecision d = decision(DecisionKind::SearchLibrary, item.controller);
            d.land_types = item.land_types;
            d.destination = item.effect == StackEffect::FetchSearch ? Zone::Battlefield : Zone::Hand;
            s.pending = d;
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
        // Ciclo della terra: velocità istantaneo, se si può pagare il mana.
        if (front.rules.landcycling.types != 0 && can_pay(s, db, p, front.rules.landcycling.cost)) {
            add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
        }
    }
    // Fuga dal cimitero (Nethergoyf): tempi della carta, mana, e altre carte con abbastanza tipi.
    for (const CardId id : ps.graveyard) {
        const CardDef& def = db.get(s.objects[id].def);
        if (def.rules.escape.min_types == 0 || !def.rules.implemented || !castable_now(s, p, def)) continue;
        if (other_graveyard_types(s, db, p, id) >= def.rules.escape.min_types &&
            can_pay(s, db, p, def.rules.escape.cost)) {
            add_unique(out, s, Action{ActionType::CastSpell, id, 1});
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
        case ActionType::CastSpell:
            begin_cast(s, db, p, a.card, a.value == 1 ? CastMethod::Escape : CastMethod::Normal, log);
            return;
        case ActionType::ActivateAbility: {
            const CardObject& o = s.objects[a.card];
            if (o.zone == Zone::Hand && db.get(o.def).rules.landcycling.types != 0) {
                activate_landcycling(s, db, p, a.card, log);
            } else if (o.zone == Zone::Hand) {
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

void lose_life(GameState& s, PlayerId p, int amount) {
    s.players[p].life = static_cast<std::int16_t>(s.players[p].life - amount);
}

void gain_life(GameState& s, PlayerId p, int amount) {
    if (amount <= 0) return;
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

}  // namespace

int power(const GameState& s, const CardDatabase& db, CardId id) {
    const CardDef& d = effective_def(s, db, id);
    const Counters& c = s.objects[id].counters;
    int value = d.base_power + c.plus_one - c.minus_one;
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
    int value = d.base_toughness + c.plus_one - c.minus_one;
    if (d.toughness_star) value += star_value(s, d, id);
    return value;
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
            target_actions(s, db, target_kind_of(s.stack_items.back(), db),
                           db.get(s.stack_items.back().source_def).colors, out);
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
            for (const CardId id : exile_options(s, db, d.player, d.chosen,
                                                 db.get(s.stack_items.back().source_def).rules.escape.min_types)) {
                out.push_back(Action{ActionType::ChooseCard, id, 0});
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
            // Fetch: si mette in campo e poi si mescola; se la terra chiede di pagare vita, la
            // scelta arriva dopo il mescolamento (non dipende dall'ordine del grimorio).
            // Ciclo della terra: la carta si rivela e va in mano, poi si mescola.
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

        case DecisionKind::OrderTriggers:
            // Il trigger scelto va in pila ora: si risolverà dopo quelli messi dopo di lui.
            s.stack_items.push_back(s.pending_triggers[action.value]);
            s.pending_triggers.erase_at(action.value);
            s.turn.passes_in_a_row = 0;
            break;

        case DecisionKind::ExileFromGraveyard: {
            PlayerZone chosen = d.chosen;
            chosen.push_back(action.card);
            continue_escape_exile(s, db, d.player, chosen, log);
            break;
        }

        case DecisionKind::None:
        case DecisionKind::GameOver: throw EngineError("apply: nessuna decisione in sospeso");
    }
    advance(s, db, log);
}

}  // namespace mtg
