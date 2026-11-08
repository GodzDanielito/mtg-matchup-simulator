#include "zones.hpp"

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

void validate_batch(const GameState& s, std::span<const ObjRef> refs, Zone to) {
    if (to == Zone::None) throw EngineError("move_objects: zona di destinazione non valida");
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

}  // namespace

ZoneChangeBatch move_objects(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                             Cause cause) {
    // Prima si controlla tutto, poi si muove: un gruppo non valido non lascia mezzi spostamenti.
    validate_batch(s, refs, to);

    ZoneChangeBatch batch;
    batch.cause = cause;
    for (const ObjRef r : refs) {
        CardObject& o = s.objects[r.card];
        const Zone from = o.zone;
        const bool token = o.has_flag(ObjectFlag::Token);
        const CardDef& def = db.get(o.def);  // per le bifronti è il fronte, come vuole il cimitero

        visit_zone(s, from, o.owner, [&](auto& list) { list.erase_at(list.find(r.card)); });
        if (from == Zone::Graveyard && !token) update_summary(s.players[o.owner].gy, def, -1);

        reset_for_new_object(o);
        o.zone = to;
        // Le capacità sono dimensionate su tutte le carte più tutti i token: push_back non può fallire.
        visit_zone(s, to, o.owner, [&](auto& list) { list.push_back(r.card); });

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

        batch.moves.push_back(ZoneChange{r.card, o.def, o.owner, from, to, token, r, s.ref(r.card)});
    }
    return batch;
}

ZoneChangeBatch move_object(GameState& s, const CardDatabase& db, ObjRef ref, Zone to, Cause cause) {
    return move_objects(s, db, std::span<const ObjRef>(&ref, 1), to, cause);
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
    std::vector<std::string> problems;
    std::array<int, kMaxObjects> seen{};

    auto check_list = [&](const auto& list, Zone zone, PlayerId owner, const std::string& label) {
        for (const CardId id : list) {
            if (id >= kMaxObjects) {
                problems.push_back(label + ": id fuori tabella " + std::to_string(id));
                continue;
            }
            ++seen[id];
            const CardObject& o = s.objects[id];
            if (o.zone != zone) {
                problems.push_back(label + ": la carta " + std::to_string(id) + " ha zona " + zone_name(o.zone));
            }
            if (owner != kNoPlayer && o.owner != owner) {
                problems.push_back(label + ": la carta " + std::to_string(id) + " appartiene a un altro giocatore");
            }
        }
    };

    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        const std::string who = "giocatore " + std::to_string(p);
        check_list(s.players[p].library, Zone::Library, p, who + ", grimorio");
        check_list(s.players[p].hand, Zone::Hand, p, who + ", mano");
        check_list(s.players[p].graveyard, Zone::Graveyard, p, who + ", cimitero");

        if (s.players[p].gy != compute_graveyard_summary(s, db, p)) {
            problems.push_back(who + ": il riepilogo incrementale del cimitero non coincide con il ricalcolo");
        }
    }
    check_list(s.battlefield, Zone::Battlefield, kNoPlayer, "campo");
    check_list(s.exile, Zone::Exile, kNoPlayer, "esilio");
    check_list(s.stack, Zone::Stack, kNoPlayer, "pila");

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
