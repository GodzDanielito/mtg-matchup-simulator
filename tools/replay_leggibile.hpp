#pragma once

// Replay leggibile: legge il registro JSON Lines del matchup (tools/replay.hpp) e lo racconta in
// italiano, turno per turno, per un giocatore. Una partita per ogni evento "partita".
//
// Si racconta: mani e mulligan, terre, magie con i bersagli, attacchi, blocchi e danni, i trigger
// importanti (evocazione, Consign to Memory, delirium, combattimenti aggiuntivi), la vita di
// entrambi a fine turno, e accanto a ogni decisione la regola dell'agente che l'ha presa. Non si
// elencano i cambi di zona tecnici (pescate dal grimorio, carte che passano dalla pila), ne' i
// passaggi di priorita' e i pagamenti del mana.

#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace mtg {

struct ReadableGame {
    std::uint64_t seed = 0;
    std::vector<std::string> players;  // nomi dei due giocatori, come nel registro
    std::string first_player;          // chi gioca per primo (vuoto se la partita non e' iniziata)
    std::string winner;                // vuoto: nessun vincitore
    std::string text;                  // il racconto
};

[[nodiscard]] std::vector<ReadableGame> render_readable_replays(std::istream& jsonl);

// Il nome del file di una partita: seed, vincitore e se il primo giocatore del registro (UW) era in
// play. Esempio: partita_00042_vince_Rakdos_uw_in_play.txt
[[nodiscard]] std::string readable_file_name(const ReadableGame& game);

// Legge il registro e scrive un file di testo per partita in `dir` (creata se manca).
// Restituisce i percorsi scritti, nell'ordine delle partite.
std::vector<std::filesystem::path> write_readable_replays(std::istream& jsonl, const std::filesystem::path& dir);

}  // namespace mtg
