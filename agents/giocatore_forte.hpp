#pragma once

// Il giocatore forte di UW, livello A minimo (docs/ricerca.md, passo 2).
//
// E' un ViewAgent: riceve solo la vista del giocatore (PlayerView) e le azioni legali, mai lo stato vero.
// - Decisioni cercate (ricerca_piatta.hpp, classify_decision): priorita' nella propria fase principale; priorita' con
//   la pila non vuota o nel turno avversario quando c'e' un'azione diversa da "passa" che non sia una fetch. Il lancio
//   e' una mossa composta: la ricerca sceglie la sequenza intera e il giocatore ne gioca le scelte una alla volta.
// - Tutto il resto (mulligan, carte in fondo, attacchi, blocchi, ricerche nel grimorio, scarti, pagamenti, bersagli
//   dei trigger) lo decide l'euristico di UW, "l'ombra", interrogato su un mondo campionato dalla vista. L'ombra e'
//   interrogata a ogni decisione, anche quando decide la ricerca ("chiedi e sostituisci"), cosi' il suo piano del
//   lancio evolve come nei rollout.
// - "A parita', l'euristico": la scelta della ricerca sostituisce quella dell'ombra solo se l'intervallo al 99% della
//   differenza appaiata sui mondi comuni esclude lo zero.
// Ogni seme viene dal seed della partita e dall'indice della decisione: la partita e' la stessa con 1 o 20 processi.

#include "card_db.hpp"
#include "game.hpp"
#include "player_view.hpp"
#include "ricerca_piatta.hpp"
#include "uw_agent.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

struct StrongConfig {
    std::uint32_t worlds = 64;   // R: mondi per mossa (budget fisso)
    double confidence = 0.99;    // per scostarsi dall'euristico
    RolloutConfig rollout;       // euristici dei rollout; le opzioni di UW sono anche quelle dell'ombra
};

// Una decisione cercata, per il registro della ricerca (ricerca.jsonl) e i replay leggibili.
struct SearchRecord {
    std::uint64_t decision = 0;  // indice della decisione del giocatore nella partita (da 0)
    std::uint16_t turn = 0;
    Step step = Step::PreGame;
    SearchedKind kind = SearchedKind::None;
    std::uint32_t worlds = 0;
    std::vector<std::string> moves;  // descrizioni delle mosse composte
    std::vector<int> wins;           // vittorie di UW per mossa, sui `worlds` mondi meno quelli scartati
    std::size_t heuristic = 0, chosen = 0, best = 0;
    std::string heuristic_rule;      // la regola dell'ombra per la sua scelta
    double diff = 0.0, low = 0.0, high = 0.0;  // vittorie medie: migliore - euristico, intervallo
    double confidence = 0.99;
    bool deviates = false;
    std::uint64_t rollout_decisions = 0;
    std::uint64_t aborted = 0;    // rollout interrotti dal limite di capacita' del motore
    std::uint32_t discarded = 0;  // mondi scartati per questo, per tutte le mosse (wins conta gli altri)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> aborted_at;  // (mossa, mondo) dei rollout interrotti
    double seconds = 0.0;  // tempo della ricerca: non deterministico, non va nei file confrontati
};

// Le mosse di una decisione cercata e quella dell'euristico ombra. L'ombra sceglie nel mondo `w` (il suo stato cambia
// come nella partita: chiedi e sostituisci); una sua copia gioca poi le scelte del lancio in una copia del mondo: la
// sequenza intera e' la mossa composta dell'euristico. EngineError se non e' tra le mosse enumerate.
struct PreparedSearch {
    std::vector<CompoundMove> moves;
    std::vector<std::string> texts;
    std::size_t heuristic = 0;
    std::string heuristic_rule;
};
[[nodiscard]] PreparedSearch prepare_search(UwAgent& shadow, const GameState& w, const CardDatabase& db,
                                            std::span<const Action> legal);

// Le stime di una decisione cercata in una riga leggibile (replay leggibili, last_detail).
[[nodiscard]] std::string search_detail(const SearchRecord& r);

// Il cuore del giocatore forte, comune al giocatore onesto (StrongUwAgent) e al canarino diagnostico. Riceve i mondi
// da una WorldSource e, per le carte nascoste citate dalle azioni (ricerche nel grimorio), la loro definizione da
// `shown_def`.
class UwSearchPlayer {
public:
    UwSearchPlayer(const CardDatabase& db, std::uint64_t game_seed, StrongConfig config,
                   std::vector<SearchRecord>* records = nullptr);

    Action decide(const WorldSource& world, std::span<const Action> legal,
                  const std::function<CardDefId(CardId)>& shown_def);

    // "euristico: <regola>", "ricerca: come l'euristico (<regola>)", "ricerca: scostamento (<regola>)",
    // "ricerca: mossa composta".
    [[nodiscard]] std::string_view last_rule() const { return last_rule_; }
    // Per una decisione cercata, le stime di tutte le mosse; altrimenti la regola.
    [[nodiscard]] const std::string& last_detail() const { return last_detail_; }
    [[nodiscard]] std::uint64_t decisions() const { return index_; }

private:
    Action heuristic_choice(const GameState& w, std::span<const Action> legal,
                            const std::function<CardDefId(CardId)>& shown_def);

    const CardDatabase* db_;
    std::uint64_t game_seed_;
    StrongConfig config_;
    std::vector<SearchRecord>* records_;
    UwAgent shadow_;
    std::uint64_t index_ = 0;
    std::deque<Action> pending_;  // le scelte del lancio della mossa composta in corso
    std::string last_rule_;
    std::string last_detail_;
};

class StrongUwAgent final : public ViewAgent {
public:
    StrongUwAgent(const CardDatabase& db, std::uint64_t game_seed, StrongConfig config,
                  std::vector<SearchRecord>* records = nullptr);
    Action choose(const PlayerView& view, std::span<const Action> legal) override;
    [[nodiscard]] std::string_view last_rule() const override { return player_.last_rule(); }
    [[nodiscard]] const std::string& last_detail() const { return player_.last_detail(); }

private:
    UwSearchPlayer player_;
};

}  // namespace mtg
