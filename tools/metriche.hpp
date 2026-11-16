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
#include <string_view>
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

// --- Ablazione (docs/metriche.md, sezione 3, aggiunta A; passo 2b) ---------------------------------------

// Una sostituzione "Carta:Terra" di --sostituisci.
struct Substitution {
    std::string card;
    std::string land;
};

// "Carta:Terra" -> {Carta, Terra}, separati dall'ultimo ':'. Lancia std::invalid_argument se una parte e' vuota.
[[nodiscard]] Substitution parse_substitution(std::string_view spec);

// Posizione nella lista espansa (una voce per copia, nell'ordine delle voci: quella di create_game, quindi il
// CardId e' giocatore * kDeckSize + posizione) della copia che replace_one_copy sostituisce: l'ultima copia
// della prima voce con quel nome. -1 se la carta non c'e'.
[[nodiscard]] int replaced_copy_position(const Decklist& deck, std::string_view card);

// La lista con UNA copia di `card` sostituita da `land`, nella stessa posizione: la voce perde l'ultima copia e
// subito dopo arriva una voce {land, 1} (con una copia sola la voce diventa {land, 1}). Con lo stesso seed il
// grimorio si mescola uguale (stessi CardId nello stesso ordine) e cambia solo la definizione di quella copia.
// Lancia DeckError se la carta non e' nella lista o se `land` non e' una terra base.
[[nodiscard]] Decklist replace_one_copy(const Decklist& deck, std::string_view card, std::string_view land);

struct AppliedSubstitution {
    PlayerId player = kNoPlayer;
    std::string card;   // nome Oracle risolto
    std::string land;
    int position = -1;  // posizione della copia sostituita nella lista (replaced_copy_position)
};

// Applica la sostituzione al mazzo della configurazione che contiene la carta: nome esatto o prefisso unico tra
// le carte dei due mazzi (sulla riga di comando di Windows i caratteri non ASCII, come in "Troll of Khazad-dûm",
// arrivano storpiati). Lancia std::invalid_argument se la carta non c'e' o il prefisso e' ambiguo, DeckError se
// la terra non e' una terra base.
AppliedSubstitution apply_substitution(MatchupConfig& config, const Substitution& substitution);

}  // namespace mtg
