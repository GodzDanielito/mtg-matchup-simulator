#pragma once

// Registro della partita in JSON Lines (regola 5 di CLAUDE.md): una riga JSON per
// evento, da leggere come un replay. Si ricava dal GameLog del motore, che registra
// decisioni, cambi di zona, vita e inizio degli step.
//
// Eventi: "partita" (intestazione), "turno", "mano" (prima di ogni scelta di
// mulligan), "decisione" (con la regola dell'agente, se nota), "pescata",
// "spostamento", "cimitero" (riepilogo dopo ogni cambio: carte, tipi, valore di mana,
// delirium), "vita" (con la fonte), "fine". I passaggi automatici sono omessi.

#include "card_db.hpp"
#include "game.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>

namespace mtg {

struct ReplayInfo {
    std::uint64_t seed = 0;
    std::array<std::string, kNumPlayers> players;   // nomi dei giocatori
    std::map<std::size_t, std::string> rules;        // regola per indice della voce Action nel registro
    PlayerId winner = kNoPlayer;
    std::string result;                              // esito leggibile ("vinta da Rakdos al turno 5")
};

// Scrive il registro come JSON Lines, una riga per evento.
void write_replay(std::ostream& out, const CardDatabase& db, const GameLog& log, const ReplayInfo& info);

}  // namespace mtg
