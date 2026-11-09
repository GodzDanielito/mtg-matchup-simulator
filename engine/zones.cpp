#include "zones.hpp"

#include "game.hpp"

#include <array>

namespace mtg {

namespace {

constexpr std::uint8_t kAllPlayers = (1u << kNumPlayers) - 1u;

const char* zone_name(Zone z) {
    switch (z) {
        case Zone::None: return "None";
        case Zone::Library: return "Library";
        case Zone::Hand: return "Hand";
        case Zone::Battlefield: return "Battlefield";
        case Zone::Graveyard: return "Graveyard";
        case Zone::Exile: return "Exile";
        case Zone::Stack: return "Stack";
    }
    return "?";
}

// Applica `fn` alla lista della zona: per grimorio, mano e cimitero quella del proprietario.
template <typename State, typename Fn>
void visit_zone(State& s, Zone z, PlayerId owner, Fn&& fn) {
    switch (z) {
        case Zone::Library: fn(s.players[owner].library); return;
        case Zone::Hand: fn(s.players[owner].hand); return;
        case Zone::Graveyard: fn(s.players[owner].graveyard); return;
        case Zone::Battlefield: fn(s.battlefield); return;
        case Zone::Exile: fn(s.exile); return;
        case Zone::Stack: fn(s.stack); return;
        case Zone::None: break;
    }
    throw EngineError("zona non valida");
}

// Aggiunge (sign = +1) o toglie (sign = -1) una carta dal riepilogo del cimitero.
void update_summary(GraveyardSummary& g, const CardDef& def, int sign) {
    g.card_count = static_cast<std::uint8_t>(g.card_count + sign);
    g.total_mana_value = static_cast<std::uint16_t>(g.total_mana_value + sign * def.mana_value);
    for (std::size_t t = 0; t < kNumCardTypes; ++t) {
        const auto bit = type_bit(static_cast<CardType>(t));
        if ((def.types & bit) == 0) continue;
        g.per_type[t] = static_cast<std::uint8_t>(g.per_type[t] + sign);
        g.present = g.per_type[t] > 0 ? static_cast<TypeMask>(g.present | bit)
                                      : static_cast<TypeMask>(g.present & ~bit);
    }
}

// Ciò che l'oggetto perde cambiando zona: diventa un oggetto nuovo (regola 400.7).
void reset_for_new_object(CardObject& o) {
    const bool token = o.has_flag(ObjectFlag::Token);
    o.flags = 0;
    if (token) o.set_flag(ObjectFlag::Token);
    o.damage = 0;
    o.counters = Counters{};
    o.attached_to = ObjRef{};
    o.controller = o.owner;
    o.timestamp = 0;
    ++o.incarnation;
}

void validate_batch(const GameState& s, std::span<const ObjRef> refs, Zone to, LibraryPosition position) {
    if (to == Zone::None) throw EngineError("move_objects: zona di destinazione non valida");
    // La pila ha una capacità piccola: si controlla prima di muovere, così un errore
    // di capacità non lascia uno spostamento a metà.
    if (to == Zone::Stack && s.stack.size() + refs.size() > s.stack.capacity()) {
        throw EngineLimitError("move_objects: pila piena");
    }
    if (to == Zone::Library && position == LibraryPosition::Unspecified) {
        throw EngineError("move_objects: verso il grimorio serve la posizione (cima, fondo o mescolato)");
    }
    if (to != Zone::Library && position != LibraryPosition::Unspecified) {
        throw EngineError(std::string("move_objects: posizione nel grimorio indicata per la zona ") +
                          zone_name(to));
    }
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const ObjRef r = refs[i];
        if (!s.is_valid(r)) {
            throw EngineError("move_objects: riferimento non valido (carta " + std::to_string(r.card) +
                              ", incarnation " + std::to_string(r.incarnation) + ")");
        }
        if (s.objects[r.card].zone == to) {
            throw EngineError("move_objects: la carta " + std::to_string(r.card) + " è già in " + zone_name(to));
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (refs[j].card == r.card) {
                throw EngineError("move_objects: carta " + std::to_string(r.card) + " ripetuta nel gruppo");
            }
        }
    }
}

// Inserisce nei grimori dei proprietari le carte di `refs`, elencate dall'alto
// verso il basso (indice 0 del grimorio = fondo, back() = cima).
void place_in_libraries(GameState& s, std::span<const ObjRef> refs, LibraryPosition position) {
    switch (position) {
        case LibraryPosition::Top:
            // L'ultimo inserito finisce in cima: si inserisce al contrario.
            for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
                s.players[s.objects[it->card].owner].library.push_back(it->card);
            }
            return;
        case LibraryPosition::Bottom:
            // Ogni inserimento in fondo spinge su i precedenti: l'ultimo dell'elenco resta il più in fondo.
            for (const ObjRef r : refs) s.players[s.objects[r.card].owner].library.insert(0, r.card);
            return;
        case LibraryPosition::Shuffle: {
            std::array<bool, kNumPlayers> touched{};
            for (const ObjRef r : refs) {
                const PlayerId owner = s.objects[r.card].owner;
                s.players[owner].library.push_back(r.card);
                touched[owner] = true;
            }
            // Un mescolamento per grimorio, in ordine di giocatore: deterministico.
            for (PlayerId p = 0; p < kNumPlayers; ++p) {
                if (touched[p]) shuffle_library(s, p);
            }
            return;
        }
        case LibraryPosition::Unspecified: break;
    }
    throw EngineError("place_in_libraries: posizione non valida");
}

}  // namespace

ZoneChangeBatch move_objects(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                             Cause cause, LibraryPosition position) {
    // Prima si controlla tutto, poi si muove: un gruppo non valido non lascia mezzi spostamenti.
    validate_batch(s, refs, to, position);

    ZoneChangeBatch batch;
    batch.cause = cause;
    for (const ObjRef r : refs) {
        CardObject& o = s.objects[r.card];
        const Zone from = o.zone;
        const PlayerId controller = o.controller;  // informazione nota per ultima
        const bool token = o.has_flag(ObjectFlag::Token);
        const CardDef& def = db.get(o.def);  // per le bifronti è il fronte, come vuole il cimitero

        visit_zone(s, from, o.owner, [&](auto& list) { list.erase_at(list.find(r.card)); });
        if (from == Zone::Graveyard && !token) update_summary(s.players[o.owner].gy, def, -1);

        reset_for_new_object(o);
        o.zone = to;
        // Le capacità sono dimensionate su tutte le carte più tutti i token: push_back non può fallire.
        // Il grimorio si riempie dopo il ciclo, quando si conosce l'intero gruppo (sotto).
        if (to != Zone::Library) visit_zone(s, to, o.owner, [&](auto& list) { list.push_back(r.card); });

        if (to == Zone::Graveyard && !token) update_summary(s.players[o.owner].gy, def, +1);
        if (to == Zone::Battlefield) {
            o.set_flag(ObjectFlag::SummoningSick);
            o.timestamp = s.next_timestamp++;
        }
        if (is_public_zone(to)) {
            o.known_to = kAllPlayers;
        } else if (to == Zone::Hand) {
            o.known_to = static_cast<std::uint8_t>(o.known_to | (1u << o.owner));
        }

        batch.moves.push_back(ZoneChange{r.card, o.def, o.owner, controller, from, to, token, r, s.ref(r.card)});
    }

    if (to == Zone::Library) place_in_libraries(s, refs, position);
    ++s.event_serial;  // un evento di gruppo, qualunque sia il numero di carte
    return batch;
}

ZoneChangeBatch move_object(GameState& s, const CardDatabase& db, ObjRef ref, Zone to, Cause cause,
                            LibraryPosition position) {
    return move_objects(s, db, std::span<const ObjRef>(&ref, 1), to, cause, position);
}

void shuffle_library(GameState& s, PlayerId player) {
    auto& library = s.players[player].library;
    shuffle(std::span<CardId>(library.begin(), library.size()), s.rng);
    for (const CardId id : library) s.objects[id].known_to = 0;  // nessuno sa più dov'è cosa
}

ObjRef create_token(GameState& s, const CardDatabase& db, CardDefId def, PlayerId controller) {
    (void)db.get(def);  // l'id deve esistere
    for (std::size_t slot = kMaxCards; slot < kMaxObjects; ++slot) {
        CardObject& o = s.objects[slot];
        if (o.zone != Zone::None) continue;

        const std::uint16_t incarnation = static_cast<std::uint16_t>(o.incarnation + 1);
        o = CardObject{};
        o.incarnation = incarnation;  // sale anche tra un token e l'altro nello stesso slot
        o.def = def;
        o.owner = controller;
        o.controller = controller;
        o.zone = Zone::Battlefield;
        o.set_flag(ObjectFlag::Token);
        o.set_flag(ObjectFlag::SummoningSick);
        o.timestamp = s.next_timestamp++;
        o.known_to = kAllPlayers;
        const auto id = static_cast<CardId>(slot);
        s.battlefield.push_back(id);
        return s.ref(id);
    }
    throw EngineLimitError("create_token: tutti i " + std::to_string(kMaxTokens) + " slot dei token sono occupati");
}

void remove_token(GameState& s, ObjRef ref) {
    if (!s.is_valid(ref)) throw EngineError("remove_token: riferimento non valido");
    CardObject& o = s.objects[ref.card];
    if (!o.has_flag(ObjectFlag::Token)) throw EngineError("remove_token: l'oggetto non è un token");
    if (o.zone == Zone::Battlefield) throw EngineError("remove_token: un token in campo continua a esistere");

    visit_zone(s, o.zone, o.owner, [&](auto& list) { list.erase_at(list.find(ref.card)); });
    const std::uint16_t incarnation = static_cast<std::uint16_t>(o.incarnation + 1);
    o = CardObject{};
    o.incarnation = incarnation;  // zone None: slot libero
}

bool permanent_cards_put_into_graveyard(const ZoneChangeBatch& batch, const CardDatabase& db, PlayerId owner) {
    for (const ZoneChange& m : batch.moves) {
        if (m.to == Zone::Graveyard && !m.is_token && m.owner == owner && db.get(m.def).is_permanent_card()) {
            return true;
        }
    }
    return false;
}

GraveyardSummary compute_graveyard_summary(const GameState& s, const CardDatabase& db, PlayerId player) {
    GraveyardSummary g;
    for (const CardId id : s.players[player].graveyard) {
        const CardObject& o = s.objects[id];
        if (o.has_flag(ObjectFlag::Token)) continue;  // i token non sono carte
        update_summary(g, db.get(o.def), +1);
    }
    return g;
}

std::vector<std::string> check_invariants(const GameState& s, const CardDatabase& db) {
    // Viene chiamata dopo ogni azione nei test (milioni di volte nel test di
    // robustezza): sullo stato coerente non alloca nulla, i messaggi si
    // costruiscono solo quando c'è un problema.
    std::vector<std::string> problems;
    std::array<int, kMaxObjects> seen{};

    auto label = [](const char* zone, PlayerId owner) {
        return owner == kNoPlayer ? std::string(zone) : "giocatore " + std::to_string(owner) + ", " + zone;
    };
    auto check_list = [&](const auto& list, Zone zone, PlayerId owner, const char* zone_label) {
        for (const CardId id : list) {
            if (id >= kMaxObjects) {
                problems.push_back(label(zone_label, owner) + ": id fuori tabella " + std::to_string(id));
                continue;
            }
            ++seen[id];
            const CardObject& o = s.objects[id];
            if (o.zone != zone) {
                problems.push_back(label(zone_label, owner) + ": la carta " + std::to_string(id) + " ha zona " +
                                   zone_name(o.zone));
            }
            if (owner != kNoPlayer && o.owner != owner) {
                problems.push_back(label(zone_label, owner) + ": la carta " + std::to_string(id) +
                                   " appartiene a un altro giocatore");
            }
        }
    };

    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        check_list(s.players[p].library, Zone::Library, p, "grimorio");
        check_list(s.players[p].hand, Zone::Hand, p, "mano");
        check_list(s.players[p].graveyard, Zone::Graveyard, p, "cimitero");

        if (s.players[p].gy != compute_graveyard_summary(s, db, p)) {
            problems.push_back(label("cimitero", p) + ": il riepilogo incrementale non coincide con il ricalcolo");
        }
    }
    check_list(s.battlefield, Zone::Battlefield, kNoPlayer, "campo");
    check_list(s.exile, Zone::Exile, kNoPlayer, "esilio");
    check_list(s.stack, Zone::Stack, kNoPlayer, "pila");

    // Le abilità in pila e i trigger in attesa hanno un controllore valido.
    auto check_items = [&](const auto& items) {
        for (const StackItem& item : items) {
            if (item.controller >= kNumPlayers) problems.push_back("abilità in pila senza controllore valido");
        }
    };
    check_items(s.stack_items);
    check_items(s.pending_triggers);

    // Ogni carta nella zona Stack è una magia in pila (StackItem di tipo Spell), o la
    // magia che si sta risolvendo in attesa di una scelta (Thoughtseize, Flare of Malice).
    for (const CardId id : s.stack) {
        bool found = (s.pending.kind == DecisionKind::ChooseFromHand ||
                      s.pending.kind == DecisionKind::SacrificeForEffect) &&
                     s.pending.subject.card == id;
        for (const StackItem& item : s.stack_items) {
            found = found || (item.kind == StackKind::Spell && item.source.card == id);
        }
        if (!found) problems.push_back("carta " + std::to_string(id) + " nella zona pila senza magia corrispondente");
    }
    for (const StackItem& item : s.stack_items) {
        if (item.kind == StackKind::Spell && !s.stack.contains(item.source.card)) {
            problems.push_back("magia in pila senza la sua carta nella zona pila");
        }
    }
    // Nethergoyf: la forza (calcolata dal riepilogo incrementale) coincide con il
    // ricalcolo da zero dei tipi nel cimitero del controllore.
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (!effective_def(s, db, id).rules.statics.power_from_graveyard_types) continue;
        const int expected = compute_graveyard_summary(s, db, o.controller).distinct_types() +
                             o.counters.plus_one - o.counters.minus_one + enchantment_power_bonus(s, db, id);
        if (power(s, db, id) != expected) {
            problems.push_back("forza di Nethergoyf diversa dal ricalcolo dei tipi nel cimitero");
        }
    }
    // Detective's Phoenix conferita: in campo è sempre attaccata a una creatura in campo
    // (702.103f). Si controlla quando un giocatore ha la priorità, cioè dopo le azioni di
    // stato, che staccano l'Aura quando la creatura se ne va.
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (!o.has_flag(ObjectFlag::Bestowed) || s.pending.kind != DecisionKind::Priority) continue;
        const ObjRef host = o.attached_to;
        const bool ok = s.is_valid(host) && s.objects[host.card].zone == Zone::Battlefield &&
                        !s.objects[host.card].has_flag(ObjectFlag::Bestowed) &&
                        effective_def(s, db, host.card).has_type(CardType::Creature);
        if (!ok) problems.push_back("Aura conferita non attaccata a una creatura in campo");
    }
    for (const CardId id : s.stack) {  // in pila è conferita solo se lanciata così
        if (!s.objects[id].has_flag(ObjectFlag::Bestowed)) continue;
        const bool bestow_cast = std::any_of(s.stack_items.begin(), s.stack_items.end(), [&](const StackItem& item) {
            return item.kind == StackKind::Spell && item.source.card == id && item.method == CastMethod::Bestow;
        });
        if (!bestow_cast) problems.push_back("carta conferita in pila non lanciata con il conferimento");
    }
    // Combattimento: esiste solo durante la fase di combattimento; ogni bloccante
    // blocca un solo attaccante; gli attaccanti ancora in campo sono del giocatore attivo.
    const Step step = s.turn.step;
    const bool combat_phase = step >= Step::BeginCombat && step <= Step::EndCombat;
    const CombatState& c = s.combat;
    if (!combat_phase && !s.turn.game_over && (!c.attackers.empty() || !c.blocks.empty())) {
        problems.push_back("stato del combattimento non vuoto fuori dalla fase di combattimento");
    }
    for (const Attacker& a : c.attackers) {
        if (s.is_valid(a.creature) && s.objects[a.creature.card].zone == Zone::Battlefield &&
            s.objects[a.creature.card].controller != s.turn.active_player) {
            problems.push_back("attaccante non controllato dal giocatore attivo");
        }
    }
    for (std::size_t i = 0; i < c.blocks.size(); ++i) {
        if (c.blocks[i].attacker >= c.attackers.size()) problems.push_back("blocco su un attaccante inesistente");
        for (std::size_t j = 0; j < i; ++j) {
            if (c.blocks[j].blocker == c.blocks[i].blocker) problems.push_back("bloccante che blocca due attaccanti");
        }
    }
    // La partita finita e la decisione GameOver vanno insieme.
    if (s.turn.game_over != (s.pending.kind == DecisionKind::GameOver)) {
        problems.push_back("partita finita e decisione in sospeso non coerenti");
    }

    for (std::size_t id = 0; id < kMaxObjects; ++id) {
        const CardObject& o = s.objects[id];
        const int expected = o.zone == Zone::None ? 0 : 1;
        if (seen[id] != expected) {
            problems.push_back("oggetto " + std::to_string(id) + " (zona " + zone_name(o.zone) + ") compare in " +
                               std::to_string(seen[id]) + " liste invece di " + std::to_string(expected));
        }
        if (o.zone != Zone::None && o.has_flag(ObjectFlag::Token) != (id >= kMaxCards)) {
            problems.push_back("oggetto " + std::to_string(id) + ": flag Token incoerente con lo slot");
        }
    }
    return problems;
}

}  // namespace mtg
