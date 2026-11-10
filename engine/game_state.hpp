#pragma once

// Stato di gioco, come da docs/stato_di_gioco.md.
//
// Regola fondamentale: GameState non contiene puntatori né allocazioni, solo
// tabelle a capacità fissa e indici. È trivially copyable: una copia è un
// memcpy, requisito per la ricerca ad albero (ISMCTS).

#include "card_rules.hpp"
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
    Angel = 1u << 6,             // "becomes an Angel in addition to its other types" (Guide of Souls)
    WarpExiled = 1u << 7,        // in esilio per il warp: il proprietario può lanciarla in un turno successivo (702.185)
    ExiledUntil = 1u << 8,       // in esilio "until" (Seam Rip, 610.3): attached_to è il permanente che la trattiene
    LoyaltyActivated = 1u << 9,  // un'abilità di fedeltà di questo permanente è stata attivata in questo turno (606.3)
};

enum class KeywordCounter : std::uint8_t { Flying, Count };
using KeywordCounters = std::array<std::uint8_t, static_cast<std::size_t>(KeywordCounter::Count)>;

struct Counters {
    std::uint8_t plus_one = 0;   // +1/+1
    std::uint8_t minus_one = 0;  // -1/-1 (separati: azione di stato 704.5q)
    std::uint8_t loyalty = 0;    // fedeltà dei planeswalker (306.5c)
    KeywordCounters keywords{};  // segnalini abilità (Guide of Souls: volare)
};

struct CardObject {
    CardDefId def = kNoDef;
    PlayerId owner = kNoPlayer;
    PlayerId controller = kNoPlayer;
    Zone zone = Zone::None;
    std::uint16_t incarnation = 0;
    std::uint16_t timestamp = 0;
    std::uint16_t zone_turn = 0;  // turno in cui l'oggetto è arrivato nella zona attuale (token "entered this turn", warp)
    std::uint16_t flags = 0;
    std::uint8_t damage = 0;
    Counters counters;
    ObjRef attached_to;         // in campo: l'Aura conferita è attaccata qui; in esilio con ExiledUntil: chi la trattiene
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
// Pila: nel matchup raramente supera poche magie e abilità; superare il limite è un errore esplicito.
inline constexpr std::size_t kMaxStack = 16;
using StackZone = StaticVector<CardId, kMaxStack>;

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
    bool city_blessing = false;          // ascesa (702.131): per il resto della partita
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
    OrderTriggers,      // trigger simultanei diversi: quale mettere in pila per primo (si risolve per ultimo)
    ExileFromGraveyard, // fuga e prove: una carta alla volta verso un insieme minimale
    DiscardCard,        // scarta una carta per un effetto (Fear of Missing Out che entra)
    SacrificeForCost,   // quale creatura sacrificare come costo (Flare of Malice alternativo)
    SacrificeForEffect, // l'avversario sceglie tra pari valore di mana (Flare of Malice)
    PayOptionalCost,    // "you may pay" durante la risoluzione (Guide of Souls: {E}{E}{E}); amount = quanto
    ChooseMode,         // magia modale: il modo (700.2), solo tra quelli con bersagli legali (Thraben Charm)
    ChooseX,            // il valore di X (107.3a) della magia in cima alla pila (March of Otherworldly Light)
    ExileFromHandForCost,  // carte dalla mano da esiliare come costo: evocazione di Solitude, March (una alla volta)
    CastFromExile,      // "you may cast this card from exile" durante la risoluzione (rimbalzo di Ephemerate)
    ChooseReplicate,    // quante volte pagare il costo di replicare (702.56a): ChooseOption = numero di volte
    GameOver,
};

struct PendingDecision {
    DecisionKind kind = DecisionKind::None;
    PlayerId player = kNoPlayer;
    ObjRef subject;             // la terra che entra (PayLifeOrTapped), un membro del gruppo (LegendRule)
    std::uint8_t amount = 0;    // carte da scegliere in tutto, o vita da pagare
    LandTypeMask land_types = 0;  // SearchLibrary: tipi cercabili
    Zone destination = Zone::None;  // SearchLibrary: campo (fetch) o mano (ciclo della terra)
    PlayerId target_player = kNoPlayer;  // ChooseFromHand: il giocatore che ha rivelato la mano
    SearchFilter search;        // SearchLibrary: carte non terra cercabili (Recruiter, Starfield Shepherd)
    std::uint8_t stack_index = 0xFF;  // ChooseTarget di una copia: l'indice in pila della copia (0xFF = la cima)
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
    RemoveMinusCounter,  // Moonshadow: togli un segnalino -1/-1 dalla fonte
    CycleSearch,  // ciclo della terra (Troll): cerca una carta del tipo, rivelala, in mano, mescola
    Rummage,      // Fear of Missing Out che entra: scarta una carta, poi pesca
    UntapAndExtraCombat,  // Fear of Missing Out: stappa la creatura bersaglio, combattimento aggiuntivo
    EndStepCats,          // Ocelot Pride: un gatto, poi con la benedizione una copia dei token entrati nel turno
    GainLifeAndEnergy,    // Guide of Souls: un'altra creatura entra, 1 vita e {E}
    EnergyAttack,         // Guide of Souls: "whenever you attack, you may pay {E}{E}{E}"
    AttackCounters,       // ...trigger riflessivo: due +1/+1, un segnalino volare, Angelo
    DestroyTarget,        // Witch Enchanter: distruggi l'artefatto o incantesimo bersaglio
    SearchToHand,         // Recruiter, Starfield Shepherd: cerca, rivela, in mano, mescola
    ExileBlackRedGraveyards,  // Sanctifier en-Vec che entra
    WarpExile,            // warp: all'inizio della prossima sottofase finale, esilia il permanente
    ExileOtherCreature,   // Solitude che entra: esilia fino a un'altra creatura, il controllore guadagna vita
    EvokeSacrifice,       // evocazione (702.74a): il controllore sacrifica il permanente
    PheliaExile,          // Phelia attacca: esilia fino a un altro permanente non terra, poi ritorno
    PheliaReturn,         // ...all'inizio della prossima sottofase finale: torna sotto il proprietario
    ExileUntilLeaves,     // Seam Rip che entra: esilia finché Seam Rip non lascia il campo (610.3)
    ReboundCast,          // rimbalzo (702.88a): al proprio mantenimento, si può lanciare dall'esilio gratis
    ReplicateCopies,      // replicare (702.56a): "When you cast this spell", una copia per ogni pagamento
    SorceriesAsFlash,     // Teferi +1: fino al proprio prossimo turno, stregonerie come se avessero lampo
    BounceAndDraw,        // Teferi -3: fino a un artefatto, creatura o incantesimo in mano al proprietario, pesca
};

// Come è stata lanciata una magia: cambia il costo da pagare.
// Il valore è anche quello di Action::value per CastSpell.
enum class CastMethod : std::uint8_t { Normal, Escape, Bestow, SacrificeAlternative, Warp, Evoke, Free, Pitch };

enum class TargetKindTag : std::uint8_t { None, Player, Object, StackItem };

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
    LandTypeMask land_types = 0;    // fetch e ciclo della terra: tipi cercabili
    SearchFilter search;            // SearchToHand: che cosa si cerca
    Target target;
    std::uint8_t x = 0;             // il valore di X scelto lanciando (107.3a)
    std::uint8_t mode = 0;          // magia modale: il modo scelto (700.2)
    std::uint8_t player_mask = 0;   // bersaglio "any number of target players": un bit per giocatore
    std::uint8_t exiled_for_cost = 0;  // carte esiliate dalla mano come costo (March: riduzione)
    Zone cast_from = Zone::None;    // da dove è stata lanciata la magia (rimbalzo: solo dalla mano)
    std::uint16_t id = 0;           // identità in pila, unica nella partita (bersaglio di Consign to Memory)
    std::uint16_t target_id = 0;    // bersaglio in pila (tag StackItem): l'id della magia o abilità bersaglio
    bool copy = false;              // copia di una magia (707.10): nessuna carta, smette di esistere fuori dalla pila
    std::uint8_t replicate = 0;     // volte in cui è stato pagato il costo di replicare (702.56a)
    CastMethod method = CastMethod::Normal;

    friend bool operator==(const StackItem&, const StackItem&) = default;
};

// Trigger ritardato (603.7): "at the beginning of the next turn's upkeep" (Mishra's Bauble) o
// "at the beginning of the next end step" (warp).
struct DelayedTrigger {
    PlayerId controller = kNoPlayer;
    StackEffect effect = StackEffect::None;
    std::uint8_t amount = 0;
    CardDefId source_def = kNoDef;
    std::uint16_t created_turn = 0;  // mantenimento: scatta in un turno successivo
    bool at_end_step = false;        // alla prossima sottofase finale che inizia (anche nello stesso turno)
    bool own_upkeep = false;         // solo al mantenimento del controllore ("your next upkeep": rimbalzo)
    ObjRef object;                   // la carta su cui agisce (warp: da esiliare; Phelia e rimbalzo: in esilio)
    ObjRef link;                     // un altro oggetto collegato (Phelia, che prende il segnalino)

    friend bool operator==(const DelayedTrigger&, const DelayedTrigger&) = default;
};

// --- Combattimento (docs/stato_di_gioco.md, sezione 13) ---

// Creature di un lato in combattimento: tutte quelle che un giocatore può controllare, le sue 60
// carte più tutti i token (i gatti di Ocelot Pride con la benedizione della città raddoppiano).
inline constexpr std::size_t kMaxCombatants = kPlayerZoneCapacity;
// Assegnazioni di danno di uno step: ogni attaccante ne fa al più max(1, bloccanti), ogni
// bloccante una. Con a attaccanti e b blocchi sono al più a + 2b, e a + b non supera gli
// oggetti della partita, b non supera le creature di un lato.
inline constexpr std::size_t kMaxDamageAssignments = kMaxObjects + kMaxCombatants;
static_assert(kMaxCombatants < kNoCard, "gli indici degli attaccanti stanno in un byte (Block::attacker)");
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

// Compatta (4 byte): le assegnazioni esistono solo dentro uno step di danno, in cui nessuna
// creatura cambia zona, quindi basta il CardId del bersaglio senza ObjRef.
struct DamageAssignment {
    CardId source = kNoCard;
    CardId target_card = kNoCard;            // kNoCard: il danno va al giocatore
    PlayerId target_player = kNoPlayer;
    std::uint8_t amount = 0;
};

struct CombatState {
    StaticVector<Attacker, kMaxCombatants> attackers;  // in ordine canonico
    StaticVector<Block, kMaxCombatants> blocks;

    // Dichiarazione in corso (attaccanti o bloccanti): creature in ordine canonico,
    // la loro classe di creature identiche e la scelta fatta per ognuna.
    StaticVector<CardId, kMaxCombatants> candidates;
    StaticVector<std::uint8_t, kMaxCombatants> candidate_class;
    StaticVector<std::uint8_t, kMaxCombatants> choice;  // attacco: opzione; blocco: attaccante (kNoChoice = nessuno)
    std::uint8_t cursor = 0;     // prossimo candidato (attacco), attaccante corrente (blocco e danno)
    std::int16_t last_pos = -1;  // blocco: posizione dell'ultimo bloccante aggiunto all'attaccante corrente

    // Danno: assegnazioni dello step in corso, inflitte tutte insieme (510.2).
    StaticVector<DamageAssignment, kMaxDamageAssignments> assignments;
    bool first_strike_step_done = false;  // c'è stato lo step del danno da attacco improvviso
};

// --- Stato completo ---

struct GameState {
    std::array<CardObject, kMaxObjects> objects{};
    std::array<PlayerState, kNumPlayers> players{};
    SharedZone battlefield;
    SharedZone exile;
    StackZone stack;                              // zona delle magie lanciate
    StaticVector<StackItem, kMaxStack> stack_items;  // pila: l'ultimo è la cima
    StaticVector<StackItem, 16> pending_triggers;  // innescati, in attesa di andare in pila
    StaticVector<ObjRef, 8> legend_keep;           // scelte della regola delle leggende da applicare
    StaticVector<DelayedTrigger, 16> delayed;      // trigger ritardati (Mishra's Bauble)
    std::uint32_t event_serial = 0;                // numero di eventi di cambio zona avvenuti
    CombatState combat;
    PendingDecision pending;
    TurnState turn;
    std::uint16_t next_timestamp = 1;
    std::uint16_t next_stack_id = 0;  // ultimo id assegnato a una magia o abilità in pila
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
