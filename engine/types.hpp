#pragma once

// Tipi di base del motore: identificativi, zone, tipi di carta, colori, errori.

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace mtg {

// --- Identificativi ---

// Carta fisica (o slot di token) nella tabella degli oggetti. 152 oggetti: basta un byte.
using CardId = std::uint8_t;
inline constexpr CardId kNoCard = 0xFF;

// Definizione di carta nel database (non fa parte dello stato).
using CardDefId = std::uint16_t;
inline constexpr CardDefId kNoDef = 0xFFFF;

using PlayerId = std::uint8_t;
inline constexpr PlayerId kNumPlayers = 2;
inline constexpr PlayerId kNoPlayer = 0xFF;

// --- Limiti ---

inline constexpr std::size_t kDeckSize = 60;
inline constexpr std::size_t kMaxCards = kDeckSize * kNumPlayers;  // 120
inline constexpr std::size_t kMaxTokens = 32;
inline constexpr std::size_t kMaxObjects = kMaxCards + kMaxTokens;  // 152
static_assert(kMaxObjects < kNoCard, "CardId deve poter indicizzare tutti gli oggetti");

// Riferimento a un oggetto di regole: carta fisica + incarnation (regola 400.7).
// È valido solo finché la carta non cambia zona.
struct ObjRef {
    CardId card = kNoCard;
    std::uint16_t incarnation = 0;

    [[nodiscard]] constexpr bool is_none() const { return card == kNoCard; }
    friend constexpr bool operator==(const ObjRef&, const ObjRef&) = default;
};

// --- Zone ---

enum class Zone : std::uint8_t {
    None,  // slot di token libero
    Library,
    Hand,
    Battlefield,
    Graveyard,
    Exile,
    Stack,
};

[[nodiscard]] constexpr bool is_public_zone(Zone z) {
    return z == Zone::Battlefield || z == Zone::Graveyard || z == Zone::Exile || z == Zone::Stack;
}

// --- Tipi, supertipi, colori ---

enum class CardType : std::uint8_t {
    Artifact,
    Battle,
    Creature,
    Enchantment,
    Instant,
    Kindred,
    Land,
    Planeswalker,
    Sorcery,
    Count,
};
inline constexpr std::size_t kNumCardTypes = static_cast<std::size_t>(CardType::Count);

using TypeMask = std::uint16_t;
[[nodiscard]] constexpr TypeMask type_bit(CardType t) {
    return static_cast<TypeMask>(1u << static_cast<unsigned>(t));
}

// Tipi che rendono una carta "carta permanente" (Moonshadow).
inline constexpr TypeMask kPermanentTypes =
    type_bit(CardType::Artifact) | type_bit(CardType::Battle) | type_bit(CardType::Creature) |
    type_bit(CardType::Enchantment) | type_bit(CardType::Land) | type_bit(CardType::Planeswalker);

enum class Supertype : std::uint8_t { Basic, Legendary, Snow, World, Ongoing };
using SupertypeMask = std::uint8_t;
[[nodiscard]] constexpr SupertypeMask supertype_bit(Supertype s) {
    return static_cast<SupertypeMask>(1u << static_cast<unsigned>(s));
}

// Ordine WUBRG: l'indice serve anche per ManaCost::colored e ManaPool.
enum class Color : std::uint8_t { White, Blue, Black, Red, Green };
inline constexpr std::size_t kNumColors = 5;
using ColorMask = std::uint8_t;
[[nodiscard]] constexpr ColorMask color_bit(Color c) {
    return static_cast<ColorMask>(1u << static_cast<unsigned>(c));
}

// --- Errori ---

// Uso scorretto del motore (bug): ObjRef non valido, spostamento impossibile...
class EngineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Un limite di capacità fisso è stato superato (token, zone).
class EngineLimitError : public EngineError {
public:
    using EngineError::EngineError;
};

}  // namespace mtg
