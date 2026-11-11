#pragma once

// Diagnostica delle "carte morte": le carte che le regole degli agenti non lanciano mai, o lanciano
// troppo tardi. Un osservatore passivo del matchup (tools/matchup.hpp) che per ogni carta di
// ciascun mazzo, con play e draw separati (dal punto di vista del proprietario della carta), misura:
//
//   1. partite in cui la carta e' stata in mano almeno una volta (dopo i mulligan: dal turno 1);
//   2. quante volte e' stata lanciata, giocata (terre) o attivata (ciclo, fetch, Teferi). Ogni
//      lancio, terra giocata e attivazione e' un uso: una fetch giocata e rotta ne vale 2, un
//      Mishra's Bauble lanciato e sacrificato 2;
//   3. turni passati in mano mentre era lanciabile, senza essere lanciata;
//   4. partite in cui e' ancora in mano alla fine.
//
// Per chi perde, la distribuzione delle carte in mano alla sconfitta.
//
// "Lanciabile in un turno" = ad almeno una priorita' del proprietario in quel turno (anche nel turno
// avversario: le istantanee) esiste un'azione legale che lancia la carta dalla mano, la gioca come
// terra o la cicla. Il turno conta come "lanciabile ma non lanciata" se nello stesso turno non si e'
// lanciata ne' giocata ne' attivata nessuna copia di quella carta. Per le terre vale "non si e'
// giocata nessuna terra in quel turno" (se ne gioca una sola). Il conto e' per carta (nome), non per
// copia: con due copie in mano, il turno e' evitato se se ne usa una.
//
// L'osservatore non cambia la partita: riceve lo stato in sola lettura.

#include "card_db.hpp"
#include "game.hpp"
#include "matchup.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mtg {

enum DeckSide : std::size_t { kOnPlay = 0, kOnDraw = 1 };

struct CardCounts {
    long long games_in_hand = 0;    // partite in cui e' stata in mano almeno una volta
    long long uses = 0;             // lanci, terre giocate, attivazioni
    long long dead_turns = 0;       // turni lanciabile in mano, senza essere lanciata
    long long in_hand_at_end = 0;   // partite in cui e' ancora in mano alla fine

    // Media dei turni "lanciabile ma non lanciata" per partita in cui la carta e' stata in mano.
    [[nodiscard]] double dead_turns_per_game() const {
        return games_in_hand == 0 ? 0.0 : static_cast<double>(dead_turns) / static_cast<double>(games_in_hand);
    }
    CardCounts& operator+=(const CardCounts& o) {
        games_in_hand += o.games_in_hand;
        uses += o.uses;
        dead_turns += o.dead_turns;
        in_hand_at_end += o.in_hand_at_end;
        return *this;
    }
};

// Le misure di un mazzo. `cards` e' per nome di carta, [play, draw].
struct DeckDiagnostics {
    std::map<std::string, std::array<CardCounts, 2>> cards;
    std::array<long long, 2> games{};    // partite giocate da questo mazzo, per lato
    std::array<long long, 2> losses{};   // ...di cui perse
    std::array<std::map<int, long long>, 2> loss_hand;  // carte in mano alla sconfitta -> partite

    [[nodiscard]] long long total_losses() const { return losses[kOnPlay] + losses[kOnDraw]; }
    // Sconfitte con almeno `n` carte in mano, sul lato indicato (-1: tutti i lati).
    [[nodiscard]] long long losses_with_at_least(int n, int side = -1) const;
};

class DeadCardTracker final : public MatchupObserver {
public:
    explicit DeadCardTracker(const CardDatabase& db) : db_(&db) {}

    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override;
    void on_end(const GameState& s) override;

    // I risultati accumulati su tutte le partite osservate: [0] il mazzo del giocatore 0 (UW), [1] Rakdos.
    [[nodiscard]] const std::array<DeckDiagnostics, 2>& diagnostics() const { return diagnostics_; }

private:
    struct PerCard {
        bool seen = false;
        long long uses = 0;
        long long dead_turns = 0;
    };
    void finish_turn();

    const CardDatabase* db_;
    std::array<DeckDiagnostics, 2> diagnostics_;
    // Stato della partita in corso.
    std::array<std::map<CardDefId, PerCard>, 2> game_;
    std::array<std::set<CardDefId>, 2> castable_;  // lanciabili in questo turno
    std::array<std::set<CardDefId>, 2> used_;      // usate in questo turno
    std::array<bool, 2> land_played_{};
    int turn_ = -1;
};

// Il CSV (data/matchup/carte_morte.csv): una riga per carta e lato ("tutte", "play", "draw"), per
// mazzo, ordinato per turni lanciabile-ma-non-lanciata per partita in mano, decrescente.
[[nodiscard]] std::string dead_cards_csv(const std::array<DeckDiagnostics, 2>& diagnostics,
                                         const std::array<std::string, 2>& deck_names);

// Il riassunto stampato: le prime `top` carte di ogni mazzo, e le sconfitte con carte in mano.
[[nodiscard]] std::string dead_cards_summary(const std::array<DeckDiagnostics, 2>& diagnostics,
                                             const std::array<std::string, 2>& deck_names, std::size_t top = 10);

}  // namespace mtg
