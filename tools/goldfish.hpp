#pragma once

// Goldfish (docs/agente_rakdos.md, sezione 16): Rakdos Delirium, giocato
// dall'agente euristico, contro un avversario di 60 Plains che non fa nulla.
// Misura in che turno arriva il delirium e in che turno Rakdos uccide.
//
// Rakdos è il giocatore 0, il goldfish il giocatore 1. Con il seed dispari
// Rakdos gioca per primo: i seed da 1 a N danno metà partite in play e metà
// in draw. I "turni" delle misure sono i turni di Rakdos (1 = il suo primo).

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game.hpp"
#include "rakdos_agent.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mtg {

inline constexpr PlayerId kGoldfishRakdos = 0;
inline constexpr PlayerId kGoldfishOpponent = 1;

struct GoldfishConfig {
    const CardDatabase* db = nullptr;
    Decklist rakdos;
    Decklist goldfish;
    int turn_limit = 15;  // turni di Rakdos; oltre, la partita è "non vinta entro il limite"
    RakdosAgentOptions agent;
};

enum class GoldfishResult : std::uint8_t { Won, TurnLimit, Lost };

struct GoldfishGame {
    std::uint64_t seed = 0;
    bool on_play = false;
    int mulligans = 0;
    GoldfishResult result = GoldfishResult::TurnLimit;
    int win_turn = 0;       // turno di Rakdos in cui l'avversario arriva a 0 (0 = non vinta)
    int delirium_turn = 0;  // primo turno di Rakdos con quattro tipi nel cimitero (0 = mai; vedi rakdos_turn_reached)
    int rakdos_turns = 0;   // turni di Rakdos iniziati
    int life_end = 0;       // vita di Rakdos alla fine

    std::string mulligan_decisions;        // le scelte di mulligan di Rakdos in ordine: 'M' mulligan, 'K' tiene
    std::vector<std::string> kept_hand;    // la mano tenuta, dopo le carte messe in fondo

    std::map<std::string, int> life_paid;  // vita pagata o persa per scelta, per carta (fetch, shockland, Wraith, Thoughtseize)
    std::map<std::string, int> damage;     // danno all'avversario per carta (combattimento e Bolt)
    std::map<std::string, int> seen;       // carte viste: mano iniziale tenuta più pescate e cercate
    std::map<std::string, int> used;       // lanci e attivazioni, con il modo ("Nethergoyf (fuga)")
    std::vector<int> types_end_of_turn;    // tipi nel cimitero alla fine di ogni turno di Rakdos
    int turns_without_land = 0;            // turni di Rakdos senza terra giocata
    int turns_with_extra_lands = 0;        // turni che finiscono con una terra in mano dopo averne giocata una
    int extra_combats = 0;                 // combattimenti aggiuntivi (Fear of Missing Out)
    int bestows = 0;                       // conferimenti della Phoenix

    std::uint64_t decisions = 0;           // decisioni prese dall'agente di Rakdos
    std::uint64_t default_decisions = 0;   // ...di cui con il comportamento predefinito
};

// Turni di Rakdos iniziati quando la partita è al turno `turn` (contato dal motore).
[[nodiscard]] int rakdos_turn(std::uint16_t turn, bool rakdos_on_play);

// Il turno di Rakdos da cui vale qualcosa che succede al turno `turn`: nel turno di Rakdos
// quel turno, nel turno avversario il turno successivo di Rakdos. Il delirium raggiunto
// nella sottofase finale avversaria si usa dal turno dopo.
[[nodiscard]] int rakdos_turn_reached(std::uint16_t turn, bool rakdos_on_play);

// Gioca una partita. `log` (facoltativo) riceve il registro completo; `rules`
// (facoltativo) la regola di ogni decisione di Rakdos, per indice della voce nel registro;
// `rule_counts` accumula le regole usate.
GoldfishGame play_goldfish(const GoldfishConfig& config, std::uint64_t seed, GameLog* log = nullptr,
                           std::map<std::size_t, std::string>* rules = nullptr,
                           std::map<std::string, std::uint64_t>* rule_counts = nullptr);

// La partita come una riga JSON (data/goldfish/*.jsonl).
[[nodiscard]] std::string goldfish_game_json(const GoldfishGame& game);

}  // namespace mtg
