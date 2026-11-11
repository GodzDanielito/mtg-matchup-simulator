#pragma once

// Il motore di gioco: struttura del turno, priorità, azioni di stato, mulligan.
//
// Il motore è una macchina a stati che si ferma a ogni decisione
// (GameState::pending). legal_actions dice cosa si può fare, apply esegue
// l'azione scelta e fa avanzare la partita fino alla decisione successiva.
// Quando l'unica azione legale è "passa", la priorità passa da sola: il
// passaggio viene registrato nel log ma non diventa una decisione.

#include "card_db.hpp"
#include "game_state.hpp"
#include "zones.hpp"

#include <cstdint>
#include <vector>

namespace mtg {

inline constexpr std::size_t kMaxHandSize = 7;
inline constexpr std::size_t kOpeningHandSize = 7;
inline constexpr std::uint8_t kMaxMulligans = 7;

enum class ActionType : std::uint8_t {
    Pass,
    PlayLand,         // card; value = faccia (0 fronte, 1 retro)
    CastSpell,        // card
    ActivateAbility,  // card (in campo, o in mano per il ciclo); value = indice dell'abilità
    TargetPlayer,     // value = giocatore
    TargetObject,     // card = oggetto bersaglio
    TargetStackItem,  // value = indice in GameState::stack_items della magia o abilità bersaglio
    Keep,
    Mulligan,
    ChooseCard,       // card
    ChooseOption,     // value
};

struct Action {
    ActionType type = ActionType::Pass;
    CardId card = kNoCard;
    std::uint8_t value = 0;

    friend bool operator==(const Action&, const Action&) = default;
};

// Valori di ChooseOption.
inline constexpr std::uint8_t kEnterTapped = 0;  // PayLifeOrTapped
inline constexpr std::uint8_t kPayLife = 1;
inline constexpr std::uint8_t kSurveilKeepOnTop = 0;  // Surveil
inline constexpr std::uint8_t kSurveilToGraveyard = 1;
inline constexpr std::uint8_t kDeclineSearch = 0;  // SearchLibrary: non trovare nulla
inline constexpr std::uint8_t kSkipSearch = 1;     // SearchLibrary facoltativa ("you may search"): non cercare, niente mescolamento
inline constexpr std::uint8_t kDeclineCost = 0;    // PayOptionalCost: non pagare
inline constexpr std::uint8_t kPayCost = 1;        // PayOptionalCost: pagare
inline constexpr std::uint8_t kNoTarget = 0;       // ChooseTarget con "up to one": nessun bersaglio
inline constexpr std::uint8_t kStopExiling = 0;    // ExileFromHandForCost: nessun'altra carta
inline constexpr std::uint8_t kDeclineCast = 0;    // CastFromExile: non lanciarla
inline constexpr std::uint8_t kCastIt = 1;         // CastFromExile: lanciarla
inline constexpr std::uint8_t kKeepTarget = 0xFE;  // ChooseTarget di una copia (707.10c): bersaglio invariato
// PayMana: value = indice dell'opzione in payment_options (engine/mana.hpp).
inline constexpr std::uint8_t kNoAttack = 0;      // DeclareAttacker: la creatura non attacca
inline constexpr std::uint8_t kDoneBlocking = 0;  // DeclareBlockers: nessun altro bloccante per questo attaccante
// DivideDamage: value = indice dell'esito.

// Registro della partita in memoria: decisioni, passaggi automatici e tutti gli
// eventi da cui si ricostruisce la partita (tools/replay.hpp lo scrive in JSON Lines).
struct LogEntry {
    // Action: decisione presa (player = chi decide, action = la scelta; subject e def = il
    //   soggetto della decisione, se c'è: la creatura che attacca, l'attaccante da bloccare,
    //   la terra che entra, la magia in pila).
    // AutoPass: priorità passata da sola (solo "passa" era legale).
    // AutoPay: un solo modo di pagare, pagamento eseguito senza decisione.
    // ZoneChange: una carta cambia zona (player = proprietario, action.card = la carta,
    //   def = la sua definizione, from/to, cause). Un token che sparisce va in Zone::None.
    // LifeChange: player = chi cambia vita, amount = variazione; action.card e def = la
    //   fonte (la carta che infligge il danno o che chiede il pagamento), cause = Damage,
    //   Cost (pagamento) o Resolve (perdita per effetto, Thoughtseize).
    // StepStart: inizia uno step (player = giocatore attivo).
    enum class Kind : std::uint8_t { Action, AutoPass, AutoPay, ZoneChange, LifeChange, StepStart };
    Kind kind = Kind::Action;
    PlayerId player = kNoPlayer;
    std::uint16_t turn = 0;
    Step step = Step::PreGame;
    DecisionKind decision = DecisionKind::None;
    Action action;
    CardDefId def = kNoDef;
    Zone from = Zone::None;
    Zone to = Zone::None;
    Cause cause = Cause::Unspecified;
    std::int16_t amount = 0;
    CardId subject = kNoCard;
    // Action con bersaglio in pila (Consign to Memory, Force of Negation): la carta di origine del
    // bersaglio, se e' una magia o un'abilita', e l'effetto dell'abilita'. Per leggere il replay.
    CardDefId target_def = kNoDef;
    bool target_is_spell = false;
    StackEffect target_effect = StackEffect::None;

    friend bool operator==(const LogEntry&, const LogEntry&) = default;
};

struct GameLog {
    std::vector<LogEntry> entries;
};

struct StartOptions {
    PlayerId starting_player = 0;
    bool mulligans = true;  // false: niente mano iniziale né mulligan (partite preparate dai test)
};

// Avvia una partita creata con create_game e avanza fino alla prima decisione.
void start_game(GameState& s, const CardDatabase& db, const StartOptions& options, GameLog* log = nullptr);

// Azioni legali per la decisione in sospeso. Le carte identiche (stessa
// definizione) danno una sola azione. Vuoto se la partita è finita.
[[nodiscard]] std::vector<Action> legal_actions(const GameState& s, const CardDatabase& db);

// Esegue un'azione legale e avanza fino alla decisione successiva.
// Lancia EngineError se l'azione non è legale (lo stato resta invariato).
void apply(GameState& s, const CardDatabase& db, const Action& action, GameLog* log = nullptr);

// La definizione con cui l'oggetto esiste ora: il retro se è in campo girato.
[[nodiscard]] const CardDef& effective_def(const GameState& s, const CardDatabase& db, CardId id);

// Vita: ogni guadagno passa da gain_life, che accende gained_life_this_turn.
// `source` e `cause` servono solo al registro: chi ha causato la variazione e come.
void gain_life(GameState& s, PlayerId p, int amount, CardId source = kNoCard, Cause cause = Cause::Unspecified);
void lose_life(GameState& s, PlayerId p, int amount, CardId source = kNoCard, Cause cause = Cause::Unspecified);

// Forza e costituzione correnti: stampate più segnalini. Lancia EngineError per
// le caratteristiche definite da un'abilità (*), non ancora implementate.
[[nodiscard]] int power(const GameState& s, const CardDatabase& db, CardId id);
[[nodiscard]] int toughness(const GameState& s, const CardDatabase& db, CardId id);

// Aure conferite (Detective's Phoenix, 702.103) attaccate a una creatura: bonus di
// forza, parole chiave concesse, quante sono (distingue creature altrimenti uguali).
[[nodiscard]] int enchantment_power_bonus(const GameState& s, const CardDatabase& db, CardId id);
[[nodiscard]] KeywordMask granted_keywords(const GameState& s, const CardDatabase& db, CardId id);
[[nodiscard]] int bestowed_aura_count(const GameState& s, CardId id);

// Insiemi minimali di carte del cimitero di `p` con cui si pagherebbe l'esilio di
// `card` lanciata con `method` (fuga: tipi; conferimento: prove), esclusa la carta
// stessa. Sono gli stessi insiemi che il motore offre una carta alla volta nella
// decisione ExileFromGraveyard, nello stesso ordine. Vuoto se il metodo non esilia.
[[nodiscard]] std::vector<std::vector<CardId>> graveyard_exile_sets(const GameState& s, const CardDatabase& db,
                                                                    PlayerId p, CardId card, CastMethod method);

}  // namespace mtg
