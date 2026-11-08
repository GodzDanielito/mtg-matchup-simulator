#pragma once

// Stato di gioco, come da docs/stato_di_gioco.md.
//
// Regola fondamentale: GameState non contiene puntatori né allocazioni, solo
// tabelle a capacità fissa e indici. È trivially copyable: una copia è un
// memcpy, requisito per la ricerca ad albero (ISMCTS).

#include "rng.hpp"
#include "static_vector.hpp"
#include "types.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>

namespace mtg {

// --- Oggetto in partita ---

enum class ObjectFlag : std::uint16_t {
    Tapped = 1u << 0,
    SummoningSick = 1u << 1,
    Token = 1u << 2,
    Bestowed = 1u << 3,          // Detective's Phoenix in campo come Aura
    AttackedThisTurn = 1u << 4,  // Fear of Missing Out: per oggetto, non per giocatore
};

enum class KeywordCounter : std::uint8_t { Flying, Count };
using KeywordCounters = std::array<std::uint8_t, static_cast<std::size_t>(KeywordCounter::Count)>;

struct Counters {
    std::uint8_t plus_one = 0;   // +1/+1
    std::uint8_t minus_one = 0;  // -1/-1 (separati: azione di stato 704.5q)
    KeywordCounters keywords{};  // segnalini abilità (Guide of Souls: volare)
};

struct CardObject {
    CardDefId def = kNoDef;
    PlayerId owner = kNoPlayer;
    PlayerId controller = kNoPlayer;
    Zone zone = Zone::None;
    std::uint16_t incarnation = 0;
    std::uint16_t timestamp = 0;
    std::uint16_t flags = 0;
    std::uint8_t damage = 0;
    Counters counters;
    ObjRef attached_to;
    std::uint8_t known_to = 0;  // bit per giocatore

    [[nodiscard]] bool has_flag(ObjectFlag f) const { return (flags & static_cast<std::uint16_t>(f)) != 0; }
    void set_flag(ObjectFlag f) { flags = static_cast<std::uint16_t>(flags | static_cast<std::uint16_t>(f)); }
    void clear_flag(ObjectFlag f) { flags = static_cast<std::uint16_t>(flags & ~static_cast<std::uint16_t>(f)); }
};

// --- Cimitero ---

struct GraveyardSummary {
    std::uint8_t card_count = 0;
    std::uint16_t total_mana_value = 0;
    std::array<std::uint8_t, kNumCardTypes> per_type{};
    TypeMask present = 0;  // bit acceso se per_type[t] > 0

    [[nodiscard]] int distinct_types() const { return std::popcount(present); }  // forza di Nethergoyf
    [[nodiscard]] bool delirium() const { return distinct_types() >= 4; }

    friend bool operator==(const GraveyardSummary&, const GraveyardSummary&) = default;
};

// --- Giocatori ---

struct ManaPool {
    std::array<std::uint8_t, kNumColors + 1> amount{};  // W U B R G, poi incolore
};

// Zone per giocatore: 60 carte più i token che vi transitano prima di sparire.
inline constexpr std::size_t kPlayerZoneCapacity = kDeckSize + kMaxTokens;
using PlayerZone = StaticVector<CardId, kPlayerZoneCapacity>;
using SharedZone = StaticVector<CardId, kMaxObjects>;

struct PlayerState {
    std::int16_t life = 20;
    std::uint8_t energy = 0;  // Guide of Souls
    ManaPool pool;
    PlayerZone library;  // la cima è l'ultimo elemento
    PlayerZone hand;
    PlayerZone graveyard;  // in ordine di arrivo
    GraveyardSummary gy;
    std::uint8_t mulligans = 0;
    std::uint8_t spells_cast_this_turn = 0;
    bool sorceries_as_flash = false;  // +1 di Teferi
    bool drew_from_empty_library = false;
};

// --- Turno ---

enum class Step : std::uint8_t {
    Untap, Upkeep, Draw,
    Main1,
    BeginCombat, DeclareAttackers, DeclareBlockers, CombatDamage, EndCombat,
    Main2,
    End, Cleanup,
};

struct TurnState {
    std::uint16_t turn_number = 0;
    PlayerId active_player = 0;
    PlayerId priority_player = 0;
    Step step = Step::Untap;
    std::uint8_t passes_in_a_row = 0;
    std::uint8_t extra_combats = 0;  // Fear of Missing Out
    std::uint8_t lands_played = 0;
    bool game_over = false;
    PlayerId winner = kNoPlayer;
};

// --- Stato completo ---

struct GameState {
    std::array<CardObject, kMaxObjects> objects{};
    std::array<PlayerState, kNumPlayers> players{};
    SharedZone battlefield;
    SharedZone exile;
    SharedZone stack;  // solo la zona: gli elementi della pila arriveranno con le abilità
    TurnState turn;
    std::uint16_t next_timestamp = 1;
    Pcg32 rng;

    [[nodiscard]] ObjRef ref(CardId id) const { return ObjRef{id, objects[id].incarnation}; }

    // Vero se il riferimento indica ancora lo stesso oggetto di regole.
    [[nodiscard]] bool is_valid(ObjRef r) const {
        return r.card < kMaxObjects && objects[r.card].zone != Zone::None &&
               objects[r.card].incarnation == r.incarnation;
    }
};

static_assert(std::is_trivially_copyable_v<GameState>, "GameState deve copiarsi con un memcpy");

}  // namespace mtg
