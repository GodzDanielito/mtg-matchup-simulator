#pragma once

// Il giocatore forte di UW nelle esecuzioni del passo 2 (docs/ricerca.md): partite contro Rakdos euristico, il
// canarino diagnostico, la curva di convergenza, le posizioni test.

#include "card_db.hpp"
#include "game.hpp"
#include "giocatore_forte.hpp"
#include "giocatori.hpp"
#include "matchup.hpp"
#include "posizioni.hpp"
#include "uw_agent.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

// --- Il canarino (diagnostica, mai nei dati) ------------------------------------------------------------------------
//
// La stessa ricerca del giocatore forte, ma sullo stato VERO, senza determinizzazione: i mondi sono lo stato vero con
// i generatori dei giocatori nuovi (i mescolamenti futuri non si conoscono neanche barando). Deve vincere almeno quanto
// il giocatore onesto: la differenza misura quanto vale l'informazione nascosta (docs/ricerca.md, sezione 3.5).

[[nodiscard]] GameState cheating_world(const GameState& s, std::uint64_t world_seed);

// Il posto del canarino per UW (AgentKind::Diagnostic). `records` (facoltativo) riceve le decisioni cercate.
[[nodiscard]] SeatFactory cheating_uw_seat(StrongConfig config, std::vector<SearchRecord>* records = nullptr);

// --- Partite del giocatore forte contro gli euristici ---------------------------------------------------------------

struct StrongRun {
    MatchupConfig base;  // liste, opzioni del Rakdos vero (base_r3 per default), limite di turni
    StrongConfig strong;  // i rollout assumono sempre strong.rollout (base_v2 e base_r3), qualunque sia il Rakdos vero
    bool cheat = false;  // il canarino al posto del giocatore onesto
    int threads = 1;
    std::optional<SeatConfig> uw_seat;  // per i test: questo posto al posto del giocatore forte
};

// Le opzioni di Rakdos per nome: "base_r1", "base_r2", "base_r3" (il predefinito). EngineError per un altro nome.
[[nodiscard]] RakdosAgentOptions rakdos_variant(std::string_view name);

struct StrongStats {
    std::uint64_t games = 0;
    std::uint64_t uw_decisions = 0;   // decisioni di UW
    std::uint64_t searched = 0;       // decisioni cercate
    std::uint64_t deviations = 0;     // scostamenti dall'euristico
    std::uint64_t moves = 0;          // mosse composte valutate (somma)
    std::uint64_t rollouts = 0;
    std::uint64_t rollout_decisions = 0;
    std::uint64_t by_kind[4] = {0, 0, 0, 0};       // decisioni cercate per SearchedKind
    std::uint64_t deviations_by_kind[4] = {0, 0, 0, 0};
    std::uint64_t aborted = 0;        // rollout interrotti dal limite di capacita' del motore
    std::uint64_t discarded = 0;      // mondi scartati per questo (per tutte le mosse)
    std::uint64_t anomalies = 0;      // partite vere oltre il limite: fuori dai dati
    double search_seconds = 0.0;      // somma dei tempi delle ricerche (tempo di thread)
};

struct StrongBlock {
    std::string games;      // partite.jsonl: una riga per partita (senza le anomalie)
    std::string searches;   // ricerca.jsonl: una riga per decisione cercata (senza quelle delle anomalie)
    std::string anomalies;  // anomalie.jsonl: una riga per partita vera oltre un limite di capacita' del motore
    StrongStats stats;      // tempi e conteggi: non deterministici i tempi, fuori dai file confrontati
};

// La riga di una partita: i campi di matchup.exe (matchup_game_json) piu' le decisioni cercate e gli scostamenti.
[[nodiscard]] std::string strong_game_json(const MatchupGame& g, const std::vector<SearchRecord>& records);
// La riga di una decisione cercata (senza il tempo).
[[nodiscard]] std::string search_record_json(std::uint64_t seed, const SearchRecord& r);

// Le partite dei seed [first, last], `threads` in parallelo; le righe nell'ordine dei seed.
[[nodiscard]] StrongBlock play_strong_block(const StrongRun& run, std::uint64_t first, std::uint64_t last,
                                            std::ostream* progress = nullptr);

// --- Diagnosi dei rollout interrotti dal limite della pila (passo 3, punto 0a) ------------------------------------------

struct OverflowReplay {
    std::uint64_t seed = 0, decision = 0;
    std::uint32_t move = 0, world = 0;
    std::string move_text;
    std::string text;  // il rollout in parole, dal mondo campionato fino al limite superato
};

// Gioca la partita forte del seed (un thread) e, a ogni decisione cercata con rollout interrotti, rigioca quei rollout
// con il racconto di ogni azione, della pila e dei trigger in attesa. Al piu' `max_replays` racconti.
[[nodiscard]] std::vector<OverflowReplay> replay_stack_overflows(const StrongRun& run, std::uint64_t seed,
                                                                 std::size_t max_replays = 4);

// --- Confronto appaiato -----------------------------------------------------------------------------------------

struct PairedDifference {
    std::uint64_t n = 0;
    double a = 0.0, b = 0.0;        // win rate di A e di B
    double diff = 0.0;              // A - B, in proporzione
    double low = 0.0, high = 0.0;   // intervallo al 95% (normale)
};

// Differenza appaiata per seed di due vettori di esiti (1 = vittoria), con l'intervallo al 95%.
[[nodiscard]] PairedDifference paired_difference(const std::vector<int>& a, const std::vector<int>& b);

// --- Curva di convergenza (docs/ricerca.md, sezione 5.2) --------------------------------------------------------------

// Una decisione vera di una partita degli euristici v1, con l'agente di UW com'era a quel punto (l'ombra).
struct SampledDecision {
    std::uint64_t game_seed = 0;
    std::uint64_t ordinal = 0;  // indice della decisione di UW nella partita
    SearchedKind kind = SearchedKind::None;
    GameState state;
    UwAgent agent;
};

// `n` decisioni cercate vere: dalle partite degli euristici dei seed first_game_seed, +1, ..., una per partita scelta a
// caso (generatore con seed fisso) tra le decisioni di UW che il giocatore forte cerca.
[[nodiscard]] std::vector<SampledDecision> sample_decisions(const MatchupConfig& base, std::size_t n,
                                                            std::uint64_t first_game_seed);

// La ricerca a ogni budget di `prefixes` (prefissi di un'unica ricerca con il budget massimo) e quella di riferimento
// con `reference` mondi (seed indipendente); una riga JSON (curva.jsonl).
[[nodiscard]] std::string curve_decision_json(const CardDatabase& db, const SampledDecision& d, std::uint64_t index,
                                              const std::vector<std::uint32_t>& prefixes, std::uint32_t reference,
                                              const StrongConfig& config);

// Il riepilogo della curva da curva.jsonl: per budget e per tipo, quota di scelte uguali al riferimento e
// differenza di valore stimata.
[[nodiscard]] std::string curve_summary(const std::string& jsonl);

// --- Posizioni test (docs/ricerca.md, sezione 5.1) ------------------------------------------------------------------

// Cosa ha giocato UW nel turno: una voce per lancio, attivazione, terra, bersaglio di un trigger o ricerca.
struct LinePlay {
    enum class Kind : std::uint8_t { Cast, Ability, Land, TriggerTarget, Search };
    Kind kind = Kind::Cast;
    Step step = Step::PreGame;
    bool before_attackers = true;  // prima della dichiarazione degli attaccanti del turno
    CardDefId card = kNoDef;       // la carta lanciata, attivata, giocata o cercata; la fonte del trigger
    CastMethod method = CastMethod::Normal;
    int mode = -1;
    int x = -1;
    CardDefId target = kNoDef;              // bersaglio oggetto
    StackEffect target_effect = StackEffect::None;  // bersaglio in pila
    bool target_spell = false;
    std::uint8_t target_players = 0;        // bersaglio "giocatori" (Thraben Charm)
    std::vector<CardDefId> exiled;          // carte esiliate dalla mano come costo
    StackEffect top_at_cast = StackEffect::None;  // la cima della pila quando si lancia
};

struct PositionOutcome {
    bool line = false;       // la linea indicata (del turno)
    std::string signature;   // le giocate di UW nel turno, in parole
    std::vector<LinePlay> plays;
    std::vector<SearchRecord> records;
    std::string replay;      // il turno in parole, con le stime delle ricerche (se richiesto)
    GameState end;
};

// Vero se le giocate del turno sono la linea indicata della posizione `id` (per una contro-posizione: la linea della
// posizione principale giocata qui). `end` e' lo stato alla fine del turno.
[[nodiscard]] bool indicated_line(const CardDatabase& db, const std::string& id, const std::vector<LinePlay>& plays,
                                  const GameState& end);
// La linea indicata, in parole (per le tabelle).
[[nodiscard]] std::string indicated_line_text(const std::string& id);

// Gioca il turno della posizione: UW forte (o l'euristico, se `strong` e' nullo) contro Rakdos euristico, con il seed
// `seed` per la ricerca e per Rakdos. Si ferma quando cambia il turno o finisce la partita.
[[nodiscard]] PositionOutcome play_position(const CardDatabase& db, const TestPosition& p, const StrongConfig* strong,
                                            std::uint64_t seed, bool with_replay);

}  // namespace mtg
