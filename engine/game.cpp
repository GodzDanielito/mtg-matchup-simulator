#include "game.hpp"

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

// Esamina un evento di gruppo e mette in attesa i trigger che fa scattare.
// Per ora: "When this land enters, surveil N".
void collect_triggers(GameState& s, const CardDatabase& db, const ZoneChangeBatch& batch) {
    for (const ZoneChange& m : batch.moves) {
        if (m.to != Zone::Battlefield) continue;
        const CardDef& d = effective_def(s, db, m.card);
        if (d.rules.land.surveil_on_enter == 0) continue;
        StackItem t;
        t.kind = StackKind::TriggeredAbility;
        t.effect = StackEffect::Surveil;
        t.controller = s.objects[m.card].controller;
        t.source = s.ref(m.card);
        t.source_def = d.id;
        t.amount = d.rules.land.surveil_on_enter;
        s.pending_triggers.push_back(t);
    }
}

// Ogni spostamento del motore passa da qui, così nessun trigger va perso.
ZoneChangeBatch move_and_collect(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                                 Cause cause, LibraryPosition position = LibraryPosition::Unspecified) {
    ZoneChangeBatch batch = move_objects(s, db, refs, to, cause, position);
    collect_triggers(s, db, batch);
    return batch;
}

// APNAP: prima i trigger del giocatore attivo, poi quelli dell'altro, così
// quelli dell'altro si risolvono per primi.
void put_triggers_on_stack(GameState& s) {
    for (const PlayerId p : turn_order(s.turn.active_player)) {
        int count = 0;
        for (const StackItem& t : s.pending_triggers) count += (t.controller == p) ? 1 : 0;
        if (count > 1) {
            // Con le terre del pool due trigger simultanei dello stesso giocatore sono
            // impossibili; l'ordinamento scelto dal giocatore arriverà con le carte che li producono.
            throw EngineError("trigger simultanei dello stesso giocatore: ordinamento non ancora implementato");
        }
        for (const StackItem& t : s.pending_triggers) {
            if (t.controller == p) s.stack_items.push_back(t);
        }
    }
    s.pending_triggers.clear();
    s.turn.passes_in_a_row = 0;
}

// --- Pescate e terre ------------------------------------------------------------

// Pesca `n` carte come un unico spostamento. Da un grimorio vuoto non si pesca:
// si segna il tentativo, che l'azione di stato trasforma in sconfitta.
void draw_cards(GameState& s, const CardDatabase& db, PlayerId p, std::size_t n) {
    const auto& library = s.players[p].library;
    StaticVector<ObjRef, kPlayerZoneCapacity> refs;
    for (std::size_t i = 0; i < n; ++i) {
        if (i >= library.size()) {
            s.players[p].drew_from_empty_library = true;
            break;
        }
        refs.push_back(s.ref(library[library.size() - 1 - i]));
    }
    if (!refs.empty()) move_and_collect(s, db, {refs.begin(), refs.size()}, Zone::Hand, Cause::Draw);
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
    s.players[p].life = static_cast<std::int16_t>(s.players[p].life - def.rules.land.fetch_life);
    item.source = move_and_collect(s, db, one(s.ref(card)), Zone::Graveyard, Cause::Cost).moves[0].new_ref;

    s.stack_items.push_back(item);
    s.turn.passes_in_a_row = 0;  // chi attiva riceve di nuovo la priorità (117.3c)
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

void resolve_top_of_stack(GameState& s) {
    const StackItem item = s.stack_items.back();
    s.stack_items.pop_back();
    s.turn.priority_player = s.turn.active_player;  // dopo la risoluzione, priorità al giocatore attivo
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
    }
}

void pass_priority(GameState& s) {
    TurnState& t = s.turn;
    ++t.passes_in_a_row;
    if (t.passes_in_a_row < kNumPlayers) {
        t.priority_player = other(t.priority_player);
        return;
    }
    // Tutti hanno passato di fila: si risolve la cima della pila, o si va avanti.
    t.passes_in_a_row = 0;
    if (!s.stack_items.empty()) {
        resolve_top_of_stack(s);
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
            pass_priority(s);
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

    // Giocare una terra: giocatore attivo, fase principale, pila vuota, una per turno.
    if (p == t.active_player && is_main_phase(t.step) && s.stack_items.empty() && t.lands_played == 0) {
        for (const CardId id : ps.hand) {
            const CardDef& front = db.get(s.objects[id].def);
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
    }
    // Fetchland: a velocità istantaneo, se si può pagare la vita (119.4).
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller != p || o.has_flag(ObjectFlag::Tapped)) continue;
        const LandRules& rules = effective_def(s, db, id).rules.land;
        if (rules.fetch_types != 0 && ps.life >= rules.fetch_life) {
            add_unique(out, s, Action{ActionType::ActivateAbility, id, 0});
        }
    }
}

// --- Gestione delle decisioni ---------------------------------------------------------

void on_priority(GameState& s, const CardDatabase& db, PlayerId p, const Action& a) {
    switch (a.type) {
        case ActionType::Pass: pass_priority(s); return;
        case ActionType::PlayLand:
            ++s.turn.lands_played;
            s.turn.passes_in_a_row = 0;
            land_enters(s, db, s.ref(a.card), a.value == 1, Cause::Play);
            return;
        case ActionType::ActivateAbility: activate_fetch(s, db, p, a.card); return;
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
        case DecisionKind::Priority: on_priority(s, db, d.player, action); break;

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
                ps.life = static_cast<std::int16_t>(ps.life - d.amount);
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

        case DecisionKind::None:
        case DecisionKind::GameOver: throw EngineError("apply: nessuna decisione in sospeso");
    }
    advance(s, db, log);
}

}  // namespace mtg
