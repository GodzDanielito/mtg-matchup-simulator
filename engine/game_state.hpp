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
    BackFace = 1u << 5,          // in campo con il retro (Witch-Blessed Meadow)
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

// Stato del mulligan London di un giocatore.
enum class MulliganState : std::uint8_t {
    Undecided,      // deve ancora dichiarare in questo giro
    WantsMulligan,  // ha dichiarato il mulligan, aspetta la fine del giro
    Kept,           // ha tenuto; se ha fatto mulligan deve ancora mettere carte in fondo
    BottomDone,     // ha tenuto e ha messo le carte in fondo
};

struct PlayerState {
    std::int16_t life = 20;
    std::uint8_t energy = 0;  // Guide of Souls
    ManaPool pool;
    PlayerZone library;  // la cima è l'ultimo elemento
    PlayerZone hand;
    PlayerZone graveyard;  // in ordine di arrivo
    GraveyardSummary gy;
    std::uint8_t mulligans = 0;
    MulliganState mulligan = MulliganState::Undecided;
    std::uint8_t spells_cast_this_turn = 0;
    bool sorceries_as_flash = false;  // +1 di Teferi
    bool drew_from_empty_library = false;
    bool gained_life_this_turn = false;  // Ocelot Pride; azzerato a inizio turno, acceso da gain_life
};

// --- Turno ---

enum class Step : std::uint8_t {
    PreGame,  // prima del primo turno: mulligan
    Untap, Upkeep, Draw,
    Main1,
    BeginCombat, DeclareAttackers, DeclareBlockers,
    FirstStrikeDamage,  // solo se un attaccante o bloccante ha attacco improvviso (510.4)
    CombatDamage, EndCombat,
    Main2,
    End, Cleanup,
};

struct TurnState {
    std::uint16_t turn_number = 0;  // 1 = primo turno di chi gioca per primo
    PlayerId starting_player = 0;
    PlayerId active_player = 0;
    PlayerId priority_player = 0;
    Step step = Step::PreGame;
    bool step_started = false;      // azioni del turno dello step già eseguite
    bool cleanup_priority = false;  // 514.3a: priorità nella cancellazione, poi una nuova cancellazione
    std::uint8_t passes_in_a_row = 0;
    std::uint8_t extra_combats = 0;  // Fear of Missing Out
    std::uint8_t lands_played = 0;
    bool game_over = false;
    PlayerId winner = kNoPlayer;  // kNoPlayer a partita finita = patta
};

// --- Decisioni ---

// Che cosa il motore sta chiedendo, e a chi. Il motore si ferma solo qui.
enum class DecisionKind : std::uint8_t {
    None,
    Mulligan,           // tenere o fare mulligan
    BottomCards,        // una carta della mano da mettere in fondo (mulligan)
    Priority,           // passare, giocare una terra, attivare un'abilità
    PayLifeOrTapped,    // shockland e Witch-Blessed Meadow mentre entrano
    SearchLibrary,      // fetchland: quale terra cercare, o nessuna
    Surveil,            // lasciare la carta in cima o metterla nel cimitero
    DiscardToHandSize,  // una carta da scartare nella cancellazione
    LegendRule,         // quale leggenda tenere
    ChooseTarget,       // bersaglio della magia o dell'abilità in cima alla pila (601.2c)
    PayMana,            // modo di pagare (solo se ce n'è più d'uno, 601.2g-h)
    ChooseFromHand,     // Thoughtseize: quale carta non terra della mano rivelata
    DeclareAttacker,    // per una creatura (soggetto): non attacca, o attacca un difensore
    DeclareBlockers,    // per un attaccante (soggetto): aggiungi un bloccante, o "fine"
    DivideDamage,       // per un attaccante con più bloccanti: un'opzione per esito distinto
    GameOver,
};

struct PendingDecision {
    DecisionKind kind = DecisionKind::None;
    PlayerId player = kNoPlayer;
    ObjRef subject;             // la terra che entra (PayLifeOrTapped), un membro del gruppo (LegendRule)
    std::uint8_t amount = 0;    // carte da scegliere in tutto, o vita da pagare
    LandTypeMask land_types = 0;  // SearchLibrary: tipi cercabili
    PlayerId target_player = kNoPlayer;  // ChooseFromHand: il giocatore che ha rivelato la mano
    PlayerZone chosen;          // BottomCards e DiscardToHandSize: scelte già fatte, in ordine
};

// --- Pila ---
// Una magia è sia una carta nella zona Stack sia un StackItem di tipo Spell;
// le abilità attivate e innescate esistono solo come StackItem.

enum class StackKind : std::uint8_t { Spell, ActivatedAbility, TriggeredAbility };

// Effetto delle abilità (per le magie l'effetto sta nelle regole della carta).
enum class StackEffect : std::uint8_t {
    None,
    FetchSearch,  // fetchland: cerca una terra, mettila in campo, mescola
    Surveil,      // sorvegliare N
    Draw,         // pesca N (ciclo, pescata ritardata di Mishra's Bauble)
    Mill,         // macina N (Stitcher's Supplier)
    LookAtTop,    // Mishra's Bauble: guarda la cima del grimorio del bersaglio, poi pescata ritardata
};

enum class TargetKindTag : std::uint8_t { None, Player, Object };

// Un bersaglio scelto: un giocatore o un oggetto (ricontrollato con ObjRef alla risoluzione).
struct Target {
    TargetKindTag tag = TargetKindTag::None;
    PlayerId player = kNoPlayer;
    ObjRef object;

    friend bool operator==(const Target&, const Target&) = default;
};

struct StackItem {
    StackKind kind = StackKind::ActivatedAbility;
    StackEffect effect = StackEffect::None;
    PlayerId controller = kNoPlayer;
    ObjRef source;                  // la carta (magia) o la fonte (può non esistere più: fetch sacrificata)
    CardDefId source_def = kNoDef;  // informazioni note per ultime sulla fonte
    std::uint8_t amount = 0;        // sorvegliare, pescare, macinare N
    LandTypeMask land_types = 0;    // fetch: tipi cercabili
    Target target;

    friend bool operator==(const StackItem&, const StackItem&) = default;
};

// Trigger ritardato (603.7): "at the beginning of the next turn's upkeep".
struct DelayedTrigger {
    PlayerId controller = kNoPlayer;
    StackEffect effect = StackEffect::None;
    std::uint8_t amount = 0;
    CardDefId source_def = kNoDef;
    std::uint16_t created_turn = 0;  // scatta al mantenimento di un turno successivo

    friend bool operator==(const DelayedTrigger&, const DelayedTrigger&) = default;
};

// --- Combattimento (docs/stato_di_gioco.md, sezione 13) ---

inline constexpr std::size_t kMaxCombatants = 32;
inline constexpr std::uint8_t kNoChoice = 0xFF;

struct Attacker {
    ObjRef creature;  // non più valido = uscito dal combattimento (506.4)
    Target defender;  // il giocatore attaccato (planeswalker con Teferi)
    bool blocked = false;  // resta vero anche se i bloccanti spariscono (509.1h)
};

struct Block {
    ObjRef blocker;
    std::uint8_t attacker = 0;  // indice in CombatState::attackers
};

struct DamageAssignment {
    CardId source = kNoCard;
    Target target;
    std::uint8_t amount = 0;
};

struct CombatState {
    StaticVector<Attacker, kMaxCombatants> attackers;  // in ordine canonico
    StaticVector<Block, kMaxCombatants> blocks;

    // Dichiarazione in corso (attaccanti o bloccanti): creature in ordine canonico,
    // la loro classe di creature identiche e la scelta fatta per ognuna.
    StaticVector<CardId, kMaxObjects> candidates;
    StaticVector<std::uint8_t, kMaxObjects> candidate_class;
    StaticVector<std::uint8_t, kMaxObjects> choice;  // attacco: opzione; blocco: attaccante (kNoChoice = nessuno)
    std::uint8_t cursor = 0;     // prossimo candidato (attacco), attaccante corrente (blocco e danno)
    std::int16_t last_pos = -1;  // blocco: posizione dell'ultimo bloccante aggiunto all'attaccante corrente

    // Danno: assegnazioni dello step in corso, inflitte tutte insieme (510.2).
    StaticVector<DamageAssignment, 64> assignments;
    bool first_strike_step_done = false;  // c'è stato lo step del danno da attacco improvviso
};

// --- Stato completo ---

struct GameState {
    std::array<CardObject, kMaxObjects> objects{};
    std::array<PlayerState, kNumPlayers> players{};
    SharedZone battlefield;
    SharedZone exile;
    SharedZone stack;  // zona delle magie lanciate (ancora vuota)
    StaticVector<StackItem, 32> stack_items;      // pila: l'ultimo è la cima
    StaticVector<StackItem, 16> pending_triggers;  // innescati, in attesa di andare in pila
    StaticVector<ObjRef, 8> legend_keep;           // scelte della regola delle leggende da applicare
    StaticVector<DelayedTrigger, 16> delayed;      // trigger ritardati (Mishra's Bauble)
    std::uint32_t event_serial = 0;                // numero di eventi di cambio zona avvenuti
    CombatState combat;
    PendingDecision pending;
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
