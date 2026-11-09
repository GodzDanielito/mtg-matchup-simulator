#pragma once

// Regole implementate di una carta: si dichiarano in cards/ (una carta per
// file) e si agganciano alla CardDef con CardDatabase::set_rules.
// Ogni carta dichiara le sue abilità come dati; il motore (engine/game.cpp)
// sa eseguire ogni tipo di effetto elencato qui.

#include "types.hpp"

#include <cstdint>

namespace mtg {

// --- Terre ---

enum class EntersTapped : std::uint8_t {
    Never,
    Always,                   // Raucous Theater, Meticulous Archive
    IfMoreThanTwoOtherLands,  // Blackcleave Cliffs
    PayLifeOrTapped,          // shockland, Witch-Blessed Meadow
};

struct LandRules {
    ColorMask mana = 0;  // abilità di mana scritte sulla carta (oltre a quelle dei tipi base)
    EntersTapped enters = EntersTapped::Never;
    std::uint8_t life_to_untap = 0;     // PayLifeOrTapped: vita da pagare
    std::uint8_t surveil_on_enter = 0;  // "When this land enters, surveil N"
    LandTypeMask fetch_types = 0;       // fetchland: tipi cercabili (0 = non è una fetch)
    std::uint8_t fetch_life = 0;        // fetchland: vita da pagare
};

// --- Bersagli ---

enum class TargetKind : std::uint8_t {
    None,
    AnyTarget,  // creatura, giocatore (planeswalker e battaglie: nessuno nel pool in campo per ora)
    Player,
};

// --- Magie (istantanei e stregonerie) ---

enum class SpellEffect : std::uint8_t {
    None,
    DealDamage,   // Lightning Bolt: `amount` danni al bersaglio
    Thoughtseize, // rivela, scegli una carta non terra, scarta; chi lancia perde `life_loss`
};

struct SpellRules {
    SpellEffect effect = SpellEffect::None;
    TargetKind target = TargetKind::None;
    std::uint8_t amount = 0;
    std::uint8_t life_loss = 0;
};

// --- Abilità attivate dei permanenti (oltre alla fetch) ---

enum class ActivatedEffect : std::uint8_t {
    None,
    LookAtTopThenDelayedDraw,  // Mishra's Bauble: {T}, sacrifica; guarda la cima; pesca al prossimo mantenimento
};

struct ActivatedRules {
    ActivatedEffect effect = ActivatedEffect::None;
    TargetKind target = TargetKind::None;
};

// --- Abilità innescate ---

struct TriggerRules {
    std::uint8_t mill_on_enter = 0;  // Stitcher's Supplier: "When this creature enters ... mill three"
    std::uint8_t mill_on_death = 0;  // "... or dies, mill three cards"
};

struct CardRules {
    bool implemented = false;  // false = la carta esiste solo come dati, nessuna azione possibile
    LandRules land;
    SpellRules spell;
    ActivatedRules activated;
    TriggerRules triggers;
    std::uint8_t cycling_life = 0;  // "Cycling—Pay N life" (Street Wraith)
};

}  // namespace mtg
