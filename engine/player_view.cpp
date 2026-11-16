#include "player_view.hpp"

#include "game_setup.hpp"
#include "rng.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mtg {

namespace {

// Stream PCG della permutazione delle carte nascoste, e il sale da cui vengono i generatori dei giocatori del mondo:
// il seed del mondo non deve dare gli stessi generatori di una partita con quel seed.
constexpr std::uint64_t kWorldStream = 0x5eed0f3a11d0c0deULL;
constexpr std::uint64_t kWorldPlayerSalt = 0xd1b54a32d192ed03ULL;

std::uint8_t bit_of(PlayerId p) { return static_cast<std::uint8_t>(1u << p); }

// Le posizioni nascoste a `viewer` tra le carte di `owner`, in ordine fisso: la mano (solo dell'avversario, nel suo
// ordine), poi il grimorio dal fondo alla cima. Ogni posizione e' (zona, indice nella lista).
struct HiddenPositions {
    std::array<std::uint8_t, kPlayerZoneCapacity> hand_index{};
    std::size_t hand = 0;
    std::array<std::uint8_t, kPlayerZoneCapacity> library_index{};
    std::size_t library = 0;

    [[nodiscard]] std::size_t size() const { return hand + library; }
};

HiddenPositions hidden_positions(const GameState& s, PlayerId owner, PlayerId viewer) {
    HiddenPositions h;
    const PlayerState& p = s.players[owner];
    if (owner != viewer) {
        for (std::size_t i = 0; i < p.hand.size(); ++i) {
            if ((s.objects[p.hand[i]].known_to & bit_of(viewer)) == 0) h.hand_index[h.hand++] = static_cast<std::uint8_t>(i);
        }
    }
    for (std::size_t i = 0; i < p.library.size(); ++i) {
        if ((s.objects[p.library[i]].known_to & bit_of(viewer)) == 0) {
            h.library_index[h.library++] = static_cast<std::uint8_t>(i);
        }
    }
    return h;
}

// Il CardId nella posizione k (mano, poi grimorio).
template <typename State>
auto& slot(State& s, PlayerId owner, const HiddenPositions& h, std::size_t k) {
    auto& p = s.players[owner];
    return k < h.hand ? p.hand[h.hand_index[k]] : p.library[h.library_index[k - h.hand]];
}

}  // namespace

bool hidden_from(const GameState& s, CardId id, PlayerId viewer) {
    const CardObject& o = s.objects[id];
    if ((o.known_to & bit_of(viewer)) != 0) return false;
    return o.zone == Zone::Library || (o.zone == Zone::Hand && o.owner != viewer);
}

GameState canonical_form(const GameState& s, PlayerId viewer) {
    GameState c = s;
    for (PlayerId owner = 0; owner < kNumPlayers; ++owner) {
        const HiddenPositions h = hidden_positions(s, owner, viewer);
        const std::size_t n = h.size();
        std::array<CardId, kPlayerZoneCapacity> ids{};
        std::array<CardDefId, kPlayerZoneCapacity> defs{};
        std::array<std::uint8_t, kPlayerZoneCapacity> known{};     // proprietà pubbliche della posizione
        std::array<std::uint16_t, kPlayerZoneCapacity> arrived{};  // turno di arrivo in mano (0 nel grimorio)
        std::uint16_t incarnation = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const CardId id = slot(c, owner, h, k);
            const CardObject& o = s.objects[id];
            ids[k] = id;
            defs[k] = o.def;
            known[k] = o.known_to;
            arrived[k] = k < h.hand ? o.zone_turn : 0;
            incarnation = std::max(incarnation, o.incarnation);
        }
        // Gli identificativi e le definizioni in ordine crescente: la forma dipende solo dagli insiemi.
        std::sort(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(n));
        std::sort(defs.begin(), defs.begin() + static_cast<std::ptrdiff_t>(n));
        for (std::size_t k = 0; k < n; ++k) {
            slot(c, owner, h, k) = ids[k];
            CardObject& o = c.objects[ids[k]];
            o = CardObject{};
            o.def = defs[k];
            o.owner = owner;
            o.controller = owner;
            o.zone = k < h.hand ? Zone::Hand : Zone::Library;
            // La stessa per tutte, la più alta tra le nascoste: nessun riferimento vecchio a una di loro torna valido.
            o.incarnation = incarnation;
            o.zone_turn = arrived[k];
            o.known_to = known[k];
        }
        c.players[owner].rng = Pcg32{};
    }
    return c;
}

PlayerView::PlayerView(const GameState& s, const CardDatabase& db, PlayerId viewer)
    : db_(&db), viewer_(viewer), decision_(s.pending.kind) {
    if (s.pending.player != viewer) {
        throw EngineError("PlayerView: la vista si costruisce alla decisione del giocatore");
    }
    canonical_ = canonical_form(s, viewer);
}

GameState PlayerView::sample_world(std::uint64_t world_seed) const {
    GameState w = canonical_;
    Pcg32 rng(world_seed, kWorldStream);
    for (PlayerId owner = 0; owner < kNumPlayers; ++owner) {
        const HiddenPositions h = hidden_positions(w, owner, viewer_);
        const std::size_t n = h.size();
        std::array<CardDefId, kPlayerZoneCapacity> defs{};
        for (std::size_t k = 0; k < n; ++k) defs[k] = w.objects[slot(w, owner, h, k)].def;
        shuffle(std::span<CardDefId>(defs.data(), n), rng);
        for (std::size_t k = 0; k < n; ++k) w.objects[slot(w, owner, h, k)].def = defs[k];
    }
    // Il futuro dei mescolamenti non si conosce: generatori nuovi, indipendenti dalla partita vera.
    for (PlayerId p = 0; p < kNumPlayers; ++p) w.players[p].rng = player_generator(world_seed ^ kWorldPlayerSalt, p);
    return w;
}

std::vector<CardDefId> PlayerView::hidden_defs(PlayerId owner) const {
    const HiddenPositions h = hidden_positions(canonical_, owner, viewer_);
    std::vector<CardDefId> out;
    out.reserve(h.size());
    for (std::size_t k = 0; k < h.size(); ++k) out.push_back(canonical_.objects[slot(canonical_, owner, h, k)].def);
    return out;  // in ordine crescente: la forma canonica
}

int PlayerView::hidden_in_hand(PlayerId owner) const {
    return static_cast<int>(hidden_positions(canonical_, owner, viewer_).hand);
}

int PlayerView::hidden_in_library(PlayerId owner) const {
    return static_cast<int>(hidden_positions(canonical_, owner, viewer_).library);
}

}  // namespace mtg
