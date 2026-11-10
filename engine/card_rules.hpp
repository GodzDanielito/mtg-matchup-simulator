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
    Creature,   // "target creature" (Fear of Missing Out, Aura conferita)
    OpponentArtifactOrEnchantment,  // "target artifact or enchantment an opponent controls" (Witch Enchanter)
    AttackingCreature,              // "target attacking creature" (Guide of Souls)
    OtherCreature,                  // "other target creature" (Solitude)
    CreatureYouControl,             // "target creature you control" (Ephemerate)
    OtherNonlandPermanent,          // "other target nonland permanent" (Phelia)
    Enchantment,                    // "target enchantment" (Thraben Charm)
    ArtifactCreatureEnchantmentMvX, // "target artifact, creature, or enchantment with mana value X or less" (March)
    OpponentNonlandPermanentMv2,    // "target nonland permanent an opponent controls with mana value 2 or less" (Seam Rip)
    Players,                        // "any number of target players" (Thraben Charm): uno o più giocatori
};

// --- Ricerca nel grimorio (701.23) ---

// Che cosa trova una ricerca, oltre alle terre dei tipi indicati a parte (fetch e ciclo
// della terra). Nel grimorio contano le caratteristiche del fronte.
struct SearchFilter {
    LandTypeMask basic_land_types = 0;  // carta di terra BASE con uno di questi tipi ("a basic Plains card")
    bool creature = false;              // oppure una carta creatura...
    std::int8_t max_mana_value = -1;    // ...con valore di mana al massimo N (-1: nessun limite)
    std::int8_t max_toughness = -1;     // ...con costituzione al massimo N (-1: nessun limite)
    bool optional = false;              // "you may search": si può non cercare, e allora non si mescola

    friend bool operator==(const SearchFilter&, const SearchFilter&) = default;
};

// --- Magie (istantanei e stregonerie) ---

enum class SpellEffect : std::uint8_t {
    None,
    DealDamage,   // Lightning Bolt: `amount` danni al bersaglio
    Thoughtseize, // rivela, scegli una carta non terra, scarta; chi lancia perde `life_loss`
    OpponentsSacrificeGreatest,  // Flare of Malice: ogni avversario sacrifica la creatura o il
                                 // planeswalker di valore di mana più alto (sceglie lui tra i pari)
    Blink,                 // Ephemerate: esilia la creatura bersaglio, poi la riporta sotto il proprietario
    ExileTarget,           // March of Otherworldly Light: esilia il bersaglio
    DamageTwiceCreatures,  // Thraben Charm: danni pari al doppio delle creature controllate
    Destroy,               // Thraben Charm: distruggi il bersaglio
    ExileGraveyards,       // Thraben Charm: esilia i cimiteri dei giocatori bersaglio
};

// Un modo di una magia modale ("Choose one —", 700.2).
struct SpellMode {
    SpellEffect effect = SpellEffect::None;
    TargetKind target = TargetKind::None;
};

struct SpellRules {
    SpellEffect effect = SpellEffect::None;
    TargetKind target = TargetKind::None;
    std::array<SpellMode, 3> modes{};  // magia modale: effetto e bersaglio stanno qui (Thraben Charm)
    std::uint8_t mode_count = 0;       // 0 = non modale
    bool rebound = false;              // rimbalzo (702.88): lanciata dalla mano, si esilia e torna al mantenimento
    // March of Otherworldly Light: "As an additional cost ..., you may exile any number of white cards from
    // your hand. This spell costs {N} less to cast for each card exiled this way." (0 = niente)
    std::uint8_t reduction_per_white_card = 0;
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
    bool rummage_on_enter = false;  // Fear of Missing Out: "When this creature enters, discard a card, then draw a card"
    // Fear of Missing Out: "Delirium — Whenever this creature attacks for the first time each turn, if
    // there are four or more card types among cards in your graveyard, untap target creature. After
    // this phase, there is an additional combat phase."
    bool delirium_first_attack_extra_combat = false;
    // Recruiter of the Guard, Starfield Shepherd: "When this creature enters, search your library for ...,
    // reveal it, put it into your hand, then shuffle."
    bool search_on_enter = false;
    SearchFilter search;
    // Witch Enchanter: "When this creature enters, destroy target artifact or enchantment an opponent controls."
    bool destroy_artifact_enchantment_on_enter = false;
    // Sanctifier en-Vec: "When this creature enters, exile all cards that are black or red from all graveyards."
    bool exile_black_red_graveyards_on_enter = false;
    // Guide of Souls: "Whenever another creature you control enters, you gain 1 life and get {E}."
    bool life_energy_on_other_creature = false;
    // Guide of Souls: "Whenever you attack, you may pay {E}{E}{E}. When you do, put two +1/+1 counters and
    // a flying counter on target attacking creature. It becomes an Angel in addition to its other types."
    bool energy_attack_counters = false;
    // Solitude: "When this creature enters, exile up to one other target creature. That creature's
    // controller gains life equal to its power."
    bool exile_other_creature_on_enter = false;
    // Seam Rip: "When this enchantment enters, exile target nonland permanent an opponent controls with
    // mana value 2 or less until this enchantment leaves the battlefield."
    bool exile_until_leaves_on_enter = false;
    // Phelia: "Whenever Phelia attacks, exile up to one other target nonland permanent. At the beginning of
    // the next end step, return that card to the battlefield under its owner's control. If it entered
    // under your control, put a +1/+1 counter on Phelia."
    bool attack_exile_until_end_step = false;
    // Ocelot Pride: "At the beginning of your end step, if you gained life this turn, create a 1/1 white
    // Cat creature token. Then if you have the city's blessing, for each token you control that entered
    // this turn, create a token that's a copy of it." Il token creato (kNoDef = niente trigger).
    CardDefId end_step_token = kNoDef;
};

// --- Caratteristiche calcolate e abilità statiche ---

struct StaticRules {
    std::uint8_t enters_with_minus_one = 0;     // Moonshadow: "enters with six -1/-1 counters" (sostituzione)
    bool power_from_graveyard_types = false;    // Nethergoyf: * = tipi tra le carte nel cimitero del controllore
    std::uint8_t power_per_graveyard_cards = 0; // Dark Matter Manipulator: ogni N carte nel cimitero...
    std::uint8_t power_bonus = 0;               // ...+X/+0
    bool ascend = false;                        // Ocelot Pride: ascesa su un permanente (702.131b)
    // Sanctifier en-Vec: "If a black or red permanent, spell, or card not on the battlefield would be put
    // into a graveyard, exile it instead." (sostituzione, per tutti i giocatori)
    bool exile_black_red_instead = false;
};

// --- Lancio da altre zone e cicli ---

struct EscapeRules {
    ManaCost cost;                // Nethergoyf: {2}{B}
    std::uint8_t min_types = 0;   // "... other cards ... with four or more card types among them" (0 = niente fuga)
};

// Conferimento (702.103): costo alternativo; da lanciata è un'Aura con "enchant creature".
struct BestowRules {
    ManaCost cost;                 // Detective's Phoenix: {R}...
    std::uint8_t evidence = 0;     // ...più raccogliere prove N (0 = niente conferimento)
    bool from_graveyard = false;   // "You may cast this card from your graveyard using its bestow ability"
    std::int8_t power = 0;         // "Enchanted creature gets +2/+2..."
    std::int8_t toughness = 0;
    KeywordMask grants = 0;        // "...and has flying and haste"
};

// Warp (702.185): costo alternativo dalla mano; il permanente si esilia all'inizio della prossima
// sottofase finale e il proprietario può lanciarlo dall'esilio in un turno successivo.
struct WarpRules {
    ManaCost cost;
    bool available = false;
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
    BestowRules bestow;
    bool alt_sacrifice_black_creature = false;  // Flare of Malice: costo alternativo
    WarpRules warp;                             // Starfield Shepherd, Quantum Riddler
    bool flash = false;                         // lampo (702.8): si lancia quando si potrebbe lanciare un istantaneo
    bool evoke_exile_white = false;             // Solitude: "Evoke—Exile a white card from your hand." (702.74)
    LandcyclingRules landcycling;
    std::uint8_t cycling_life = 0;  // "Cycling—Pay N life" (Street Wraith)
};

}  // namespace mtg
