#pragma once

// Record compatto di una partita del matchup per le metriche stile 17lands (docs/metriche.md, sezione 2):
// una riga JSON per partita con le mani, le carte in fondo, gli eventi di ogni copia fisica (codici I, P, C,
// F, R, L, T, Y, X, E, S, K, M, V), le terre a fine turno, le carte in mano alla fine e il delirium di Rakdos.
//
// Il record si costruisce SOLO dal registro in memoria (GameLog) e dall'esito (MatchupGame): e' una funzione
// pura, non tocca lo stato ne' il generatore casuale, quindi non puo' cambiare la partita.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game.hpp"
#include "matchup.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mtg {

// Un evento di una copia: turno della partita (0 prima del turno 1) e codice (docs/metriche.md, sezione 2).
struct CopyEvent {
    std::uint16_t turn = 0;
    char code = '?';

    friend bool operator==(const CopyEvent&, const CopyEvent&) = default;
};

// Una copia fisica che arriva in mano o viene usata da un'altra zona.
struct CopyRecord {
    CardDefId def = kNoDef;
    std::vector<CopyEvent> events;
};

struct PlayerRecord {
    int mulligans = 0;
    std::vector<std::vector<CardDefId>> hands;  // la mano a ogni decisione di mulligan, in ordine di arrivo
    std::vector<CardDefId> bottom;              // le carte messe in fondo, nell'ordine scelto
    std::vector<CopyRecord> cards;              // prima la mano iniziale tenuta, poi per primo evento
    std::vector<int> lands;                     // terre in campo alla fine di ogni proprio turno
    int hand_end = 0;                           // carte in mano a fine partita
};

// Il delirium di Rakdos (giocatore 1). turn: turno della partita in cui il cimitero arriva la prima volta a
// quattro tipi; rakdos_turn: il turno di Rakdos da cui vale, con la convenzione del goldfish
// (rakdos_turn_reached: nel turno di UW conta il turno successivo di Rakdos); lost: quante volte scende
// sotto i quattro tipi; at_end: delirium a fine partita.
struct DeliriumRecord {
    bool reached = false;
    int turn = 0;
    int rakdos_turn = 0;
    int lost = 0;
    bool at_end = false;
};

struct CompactRecord {
    MatchupGame game;
    std::array<PlayerRecord, kNumPlayers> players;  // [0] UW, [1] Rakdos
    DeliriumRecord delirium;
};

// Costruisce il record dal registro completo della partita (play_matchup con `log`): tutte le carte partono
// dal grimorio. Dall'esito servono solo seed, uw_on_play e i campi copiati nella riga.
[[nodiscard]] CompactRecord build_compact_record(const CardDatabase& db, const MatchupGame& game, const GameLog& log);

// Il record come una riga JSON (data/metriche/v1/record.jsonl), con i nomi Oracle delle carte.
[[nodiscard]] std::string compact_record_json(const CardDatabase& db, const CompactRecord& record);

// Le carte in mano a fine partita, ricavate dagli eventi: le copie il cui ultimo evento e' un arrivo in mano
// (I, P, C, R). In ordine di copia.
[[nodiscard]] std::vector<CardDefId> hand_at_end(const PlayerRecord& player);

// Controlli di coerenza del record (docs/metriche.md, sezione 11): nessun codice '?', copie viste di una
// carta al massimo quante nella lista, mano iniziale = 7 - mulligan (e le mani da 7 sono da 7), carte in
// fondo = mulligan, arrivi in mano - uscite = mano_fine, una voce di terre per ogni proprio turno iniziato.
// Restituisce un messaggio per ogni problema; vuoto = record coerente.
[[nodiscard]] std::vector<std::string> check_compact_record(const CardDatabase& db, const CompactRecord& record,
                                                            const Decklist& uw, const Decklist& rakdos);

}  // namespace mtg
