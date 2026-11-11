#pragma once

// Confronto di varianti delle regole di un agente sugli stessi seed (docs/agente_uw.md, sezione 28).
//
// Con gli stessi seed le due versioni giocano gli stessi grimori e le stesse mani: la differenza di
// win rate si stima partita per partita (differenza appaiata), con un intervallo molto piu' stretto
// del confronto tra due campioni indipendenti.

#include "game.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mtg {

// Differenza appaiata: media di (variante - base) su esiti 0/1 dello stesso seed, con l'intervallo di
// confidenza al 95% (approssimazione normale: media +- 1,96 * deviazione standard campionaria / sqrt(n)).
struct PairedDiff {
    long long n = 0;
    double mean = 0.0;
    double low = 0.0;
    double high = 0.0;
};
[[nodiscard]] PairedDiff paired_difference(const std::vector<int>& base, const std::vector<int>& variant);

// Le ricariche di Quantum Riddler in una partita: i lanci con il warp presi dalla regola "Quantum
// Riddler: warp come ricarica" del giocatore 0 (UW) e le carte che il giocatore 0 pesca dopo, fino
// alla sua giocata successiva (lancio, terra, attivazione) o all'inizio del turno dopo. In pratica e'
// la pescata del trigger di entrata di Riddler, con il +1 a mano quasi vuota.
struct RefillStats {
    long long casts = 0;
    long long cards = 0;
};
[[nodiscard]] RefillStats count_refill_warps(const GameLog& log, const std::map<std::size_t, std::string>& rules);

// La variante W1 in una partita, fino all'inizio del turno dopo ogni lancio (o alla fine della partita):
// - casts: i lanci di Quantum Riddler con la regola di W1 del giocatore 0 (UW);
// - consign_cast: Consign to Memory lanciato con la regola "l'esilio del warp";
// - saved: Consign ha davvero neutralizzato l'esilio: lanciato, e Riddler mai uscito dal campo;
// - stayed: Riddler in campo al turno dopo con qualsiasi mezzo (Consign, Ephemerate, Phelia).
struct W1Stats {
    long long casts = 0;
    long long consign_cast = 0;
    long long saved = 0;
    long long stayed = 0;

    W1Stats& operator+=(const W1Stats& o);
};
[[nodiscard]] W1Stats count_w1(const GameLog& log, const std::map<std::size_t, std::string>& rules);

}  // namespace mtg
