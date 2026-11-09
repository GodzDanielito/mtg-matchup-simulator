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

#include <cstdint>
#include <vector>

namespace mtg {

inline constexpr std::size_t kMaxHandSize = 7;
inline constexpr std::size_t kOpeningHandSize = 7;
inline constexpr std::uint8_t kMaxMulligans = 7;

enum class ActionType : std::uint8_t {
    Pass,
    PlayLand,         // card; value = faccia (0 fronte, 1 retro)
    ActivateAbility,  // card; value = indice dell'abilità
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

// Registro della partita in memoria (il formato JSON Lines arriverà con la fase 3).
struct LogEntry {
    enum class Kind : std::uint8_t { Action, AutoPass };
    Kind kind = Kind::Action;
    PlayerId player = kNoPlayer;
    std::uint16_t turn = 0;
    Step step = Step::PreGame;
    DecisionKind decision = DecisionKind::None;
    Action action;

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

// Forza e costituzione correnti: stampate più segnalini. Lancia EngineError per
// le caratteristiche definite da un'abilità (*), non ancora implementate.
[[nodiscard]] int power(const GameState& s, const CardDatabase& db, CardId id);
[[nodiscard]] int toughness(const GameState& s, const CardDatabase& db, CardId id);

}  // namespace mtg
