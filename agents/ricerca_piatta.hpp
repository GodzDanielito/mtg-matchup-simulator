#pragma once

// Ricerca piatta del livello A (docs/ricerca.md, sezione 2; passo 2 della ricerca).
//
// A una decisione cercata: le mosse composte legali, R mondi (determinizzazione), per ogni mossa e ogni mondo un
// rollout con gli euristici v1 per entrambi i giocatori e il registro spento. I mondi sono COMUNI a tutte le mosse:
// la mossa m nel mondo j parte dallo stesso stato campionato, con gli stessi generatori dei giocatori e lo stesso
// seed degli agenti dei rollout. Le differenze tra le mosse sono quindi appaiate (sezione 1.6).
//
// Questo file non vede mai lo stato vero: riceve i mondi da una WorldSource. Il giocatore forte onesto
// (giocatore_forte.hpp) la costruisce dalla PlayerView; solo il canarino diagnostico (tools/forte.hpp) dallo stato
// vero, e non entra mai nei dati.

#include "card_db.hpp"
#include "game.hpp"
#include "rakdos_agent.hpp"
#include "uw_agent.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

// Che decisione e' per la ricerca del passo 2 (docs/ricerca.md, sezione 4.1; richiesta di Daniele del 16/11/2026).
enum class SearchedKind : std::uint8_t {
    None,          // non cercata: la prende l'euristico
    Main,          // priorita' nella propria fase principale con la pila vuota
    Response,      // priorita' con la pila non vuota e un'azione diversa da "passa" che non sia una fetch
    OpponentTurn,  // priorita' nel turno avversario, pila vuota, un'azione diversa da "passa" che non sia una fetch
};
[[nodiscard]] std::string_view searched_kind_name(SearchedKind k);

// La classe della decisione in sospeso di `me`. `w` e' un mondo (la parte pubblica e la propria mano sono quelle
// vere); `legal` le azioni legali. Le priorita' nel proprio turno fuori dalla fase principale con la pila vuota non
// si cercano.
[[nodiscard]] SearchedKind classify_decision(const GameState& w, const CardDatabase& db, std::span<const Action> legal,
                                             PlayerId me);

// Vero se `a` attiva una fetchland (le fetch non bastano a cercare una priorita' fuori dalla fase principale).
[[nodiscard]] bool is_fetch_activation(const GameState& w, const CardDatabase& db, const Action& a);

// Una mossa composta (docs/ricerca.md, sezione 4.3): l'azione della priorita' e le scelte del lancio che la
// seguono (modo, X, bersaglio, carte esiliate per il costo, replicare), in ordine. Il pagamento del mana, la vita o
// la terra tappata e tutto cio' che arriva dopo il lancio (bersagli dei trigger, ricerche) restano all'euristico.
using CompoundMove = std::vector<Action>;

// Vero se la decisione in sospeso e' una scelta del lancio di `me`: modo, X, carte esiliate dalla mano, replicare,
// bersaglio di una magia o di un'abilita' attivata (non di un trigger, non di una copia).
[[nodiscard]] bool is_cast_choice(const GameState& w, PlayerId me);

// Tutte le mosse composte, enumerate nel mondo `w` alla decisione di priorita' di `me`: per ogni azione di `legal`,
// nell'ordine, tutte le sequenze complete di scelte del lancio. Le scelte del lancio dipendono solo da cio' che il
// giocatore vede (zone pubbliche, propria mano, mana), quindi le mosse sono le stesse in ogni mondo. Se `texts` non
// e' nullo riceve una descrizione in italiano di ogni mossa.
[[nodiscard]] std::vector<CompoundMove> enumerate_compound_moves(const GameState& w, const CardDatabase& db,
                                                                 std::span<const Action> legal,
                                                                 std::vector<std::string>* texts = nullptr);

// Una scelta di chi decide nel mondo `w`, in italiano (mosse composte, replay delle posizioni).
[[nodiscard]] std::string describe_choice(const GameState& w, const CardDatabase& db, const Action& a);

// La descrizione di una mossa composta, giocata nel mondo `w` (che non cambia).
[[nodiscard]] std::string describe_move(const GameState& w, const CardDatabase& db, const CompoundMove& move);

// Gli euristici dei rollout: UW (base_v2 per default) contro Rakdos (base_r3), come negli euristici v1.
struct RolloutConfig {
    PlayerId uw = 0;  // il giocatore di UW; l'altro e' Rakdos
    UwAgentOptions uw_options;
    RakdosAgentOptions rakdos_options;
    int turn_limit = 60;  // come MatchupConfig
};

// Un rollout nel mondo `w` (copiato): la mossa si applica una scelta alla volta, e a ogni scelta l'agente del
// giocatore che decide sceglie comunque e la sua scelta viene sostituita ("chiedi e sostituisci": il suo piano del
// lancio evolve come quello dell'euristico ombra nella partita vera). Poi gli euristici giocano fino alla fine,
// registro spento. Vero se vince UW (patta e limite di turni: falso). `decisions` conta le decisioni giocate.
[[nodiscard]] bool rollout_wins(GameState w, const CardDatabase& db, const CompoundMove& move, std::uint64_t agent_seed,
                                const RolloutConfig& cfg, std::uint64_t* decisions = nullptr,
                                bool* aborted = nullptr);

// I semi di una ricerca: la decisione `index` della partita `game_seed`; il mondo j (determinizzazione e generatori)
// e gli agenti dei rollout nel mondo j. Stessa partita, stessa decisione: stessi semi, con 1 o 20 processi.
[[nodiscard]] std::uint64_t decision_seed(std::uint64_t game_seed, std::uint64_t index);
[[nodiscard]] std::uint64_t world_seed(std::uint64_t search_seed, std::uint32_t j);
[[nodiscard]] std::uint64_t rollout_agent_seed(std::uint64_t search_seed, std::uint32_t j);

// Da dove vengono i mondi: dalla vista (giocatore onesto) o, solo per il canarino, dallo stato vero.
using WorldSource = std::function<GameState(std::uint64_t world_seed)>;

// Gli esiti di una ricerca: wins[m * worlds + j] = 1 se UW vince il rollout della mossa m nel mondo j.
struct SearchTable {
    std::vector<CompoundMove> moves;
    std::size_t heuristic = 0;  // la mossa dell'euristico ombra
    std::uint32_t worlds = 0;
    std::vector<std::uint8_t> wins;
    std::uint64_t rollout_decisions = 0;
    std::uint64_t aborted = 0;  // rollout interrotti dal limite di capacita' del motore (valgono 0)

    // Vittorie della mossa m nei primi `prefix` mondi.
    [[nodiscard]] int wins_of(std::size_t m, std::uint32_t prefix) const;
};

// R mondi comuni per tutte le mosse. Budget fisso, nessuna potatura.
[[nodiscard]] SearchTable run_search(const WorldSource& world, const CardDatabase& db, std::vector<CompoundMove> moves,
                                     std::size_t heuristic, std::uint32_t worlds, std::uint64_t search_seed,
                                     const RolloutConfig& cfg);

struct SearchChoice {
    std::size_t chosen = 0;  // la mossa da giocare
    std::size_t best = 0;    // la mossa con piu' vittorie
    bool deviates = false;   // chosen != heuristic
    double diff = 0.0;       // vittorie medie: best - euristico, appaiate sui mondi
    double low = 0.0, high = 0.0;  // intervallo della differenza al livello `confidence`
};

// "A parita', l'euristico" (docs/ricerca.md, sezione 4.2): `best` e' la mossa con piu' vittorie nei primi `prefix`
// mondi (a pari vittorie l'euristico, poi la prima); la si gioca solo se l'intervallo al livello `confidence` della
// differenza appaiata best - euristico esclude lo zero (t di Student con prefix - 1 gradi di liberta').
[[nodiscard]] SearchChoice choose_with_confidence(const SearchTable& t, std::uint32_t prefix, double confidence = 0.99);

// Quantile p della t di Student con `df` gradi di liberta' (sviluppo di Cornish-Fisher, Abramowitz e Stegun 26.7.5;
// errore sotto 0,01 per df >= 10). Con df molto grande e' il quantile della normale.
[[nodiscard]] double student_t_quantile(double p, double df);

}  // namespace mtg
