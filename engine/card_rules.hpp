#pragma once

// Regole implementate di una carta: si dichiarano in cards/ (una carta per
// file) e si agganciano alla CardDef con CardDatabase::set_rules.
// Ogni carta dichiara le sue abilità come dati; il motore (engine/game.cpp)
// sa eseguire ogni tipo di effetto elencato qui.

#include "types.hpp"

#include <array>
#include <cstdint>

namespace mtg {

struct ManaCost {
    std::uint8_t generic = 0;
    std::array<std::uint8_t, kNumColors> colored{};  // indice = Color (WUBRG)
    std::uint8_t colorless = 0;                      // {C}
    std::uint8_t x = 0;                              // quante {X}

    friend bool operator==(const ManaCost&, const ManaCost&) = default;
};

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

// --- Combattimento (702) ---

enum class Keyword : std::uint8_t { Flying, Reach, Haste, Lifelink, FirstStrike, Menace, Count };
using KeywordMask = std::uint16_t;
[[nodiscard]] constexpr KeywordMask keyword_bit(Keyword k) {
    return static_cast<KeywordMask>(1u << static_cast<unsigned>(k));
}

struct CombatRules {
    KeywordMask keywords = 0;
    std::uint8_t min_blockers = 0;  // "can't be blocked except by N or more creatures" (Troll: 3)
    LandTypeMask landwalk = 0;      // passaterra (Street Wraith: swampwalk)
    ColorMask protection = 0;       // protezione dai colori (Sanctifier en-Vec: nero e rosso)
};

// --- Abilità innescate ---

struct TriggerRules {
    std::uint8_t mill_on_enter = 0;  // Stitcher's Supplier, Dark Matter Manipulator: "When this creature enters, mill three"
    std::uint8_t mill_on_death = 0;  // Stitcher's Supplier: "... or dies, mill three cards"
    // Moonshadow: "Whenever one or more permanent cards are put into your graveyard
    // from anywhere while this creature has a -1/-1 counter on it, remove a -1/-1 counter".
    bool remove_minus_counter_on_permanent_cards = false;
};

// --- Caratteristiche calcolate e abilità statiche ---

struct StaticRules {
    std::uint8_t enters_with_minus_one = 0;     // Moonshadow: "enters with six -1/-1 counters" (sostituzione)
    bool power_from_graveyard_types = false;    // Nethergoyf: * = tipi tra le carte nel cimitero del controllore
    std::uint8_t power_per_graveyard_cards = 0; // Dark Matter Manipulator: ogni N carte nel cimitero...
    std::uint8_t power_bonus = 0;               // ...+X/+0
};

// --- Lancio da altre zone e cicli ---

struct EscapeRules {
    ManaCost cost;                // Nethergoyf: {2}{B}
    std::uint8_t min_types = 0;   // "... other cards ... with four or more card types among them" (0 = niente fuga)
};

struct LandcyclingRules {
    ManaCost cost;                // Troll of Khazad-dûm: {1}
    LandTypeMask types = 0;       // "Search your library for a Swamp card" (0 = niente ciclo della terra)
};

struct CardRules {
    // false = la carta non si può lanciare né giocare. Le parole chiave di
    // combattimento possono essere dichiarate anche prima (servono ai test con la
    // carta messa in campo direttamente).
    bool implemented = false;
    LandRules land;
    CombatRules combat;
    SpellRules spell;
    ActivatedRules activated;
    TriggerRules triggers;
    StaticRules statics;
    EscapeRules escape;
    LandcyclingRules landcycling;
    std::uint8_t cycling_life = 0;  // "Cycling—Pay N life" (Street Wraith)
};

}  // namespace mtg
