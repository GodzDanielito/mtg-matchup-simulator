#pragma once

// La vista di un giocatore e la determinizzazione (docs/ricerca.md, sezione 3).
//
// Il giocatore forte non riceve mai lo stato vero: riceve una PlayerView, costruita dal motore alla sua
// decisione, che contiene solo l'informazione legittima. Dentro c'è lo stato in **forma canonica**: le carte
// nascoste al giocatore (mano avversaria non nota, grimori nelle posizioni non note, il proprio compreso) hanno
// le definizioni in ordine crescente, i CardId in ordine crescente nelle posizioni nascoste, la stessa
// incarnation e nessun generatore. La forma canonica dipende solo dall'insieme delle carte nascoste, non da
// dove stavano davvero: non rivela niente di nascosto.
//
// Da una vista si campionano **mondi** (`sample_world`): le definizioni nascoste si permutano a caso tra le
// posizioni nascoste di ogni proprietario e i generatori dei giocatori ripartono dal seed del mondo. Nessuna
// carta cambia zona: dimensioni delle zone, età delle carte in mano, carte note, cimiteri e invarianti restano
// quelli veri. Un mondo è un GameState qualsiasi: la ricerca ci gioca sopra (azioni legali, rollout).
//
// La separazione è nei tipi: un ViewAgent riceve `const PlayerView&`, e PlayerView non espone lo stato.

#include "card_db.hpp"
#include "game.hpp"
#include "game_state.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mtg {

// Vero se l'oggetto `id` è nascosto a `viewer`: nel grimorio di chiunque, o nella mano dell'avversario, senza il
// bit di `viewer` in known_to. Le carte nelle zone pubbliche e nella propria mano non sono mai nascoste.
[[nodiscard]] bool hidden_from(const GameState& s, CardId id, PlayerId viewer);

// La forma canonica dello stato per `viewer` (vedi sopra). Funzione del solo stato vero: la usano PlayerView e i
// test.
[[nodiscard]] GameState canonical_form(const GameState& s, PlayerId viewer);

class PlayerView {
public:
    // Si costruisce alla decisione di `viewer` (s.pending.player == viewer), altrimenti EngineError: una
    // decisione in corso dell'avversario (le carte già scelte da mettere in fondo) sarebbe informazione nascosta.
    PlayerView(const GameState& s, const CardDatabase& db, PlayerId viewer);

    [[nodiscard]] PlayerId viewer() const { return viewer_; }
    [[nodiscard]] const CardDatabase& db() const { return *db_; }
    [[nodiscard]] DecisionKind decision() const { return decision_; }

    // Un mondo coerente con la vista: stesso seed, stesso mondo. Le carte nascoste di ogni proprietario sono
    // una permutazione uniforme delle sue definizioni nascoste; i generatori dei giocatori sono nuovi.
    [[nodiscard]] GameState sample_world(std::uint64_t world_seed) const;

    // Le definizioni nascoste tra le carte di `owner`, in ordine crescente: l'insieme che ogni mondo distribuisce.
    [[nodiscard]] std::vector<CardDefId> hidden_defs(PlayerId owner) const;
    // Quante carte nascoste nella mano di `owner` (0 per la propria) e nel suo grimorio.
    [[nodiscard]] int hidden_in_hand(PlayerId owner) const;
    [[nodiscard]] int hidden_in_library(PlayerId owner) const;

private:
    GameState canonical_;
    const CardDatabase* db_;
    PlayerId viewer_;
    DecisionKind decision_;
};

// I giocatori che vedono solo la vista (il giocatore forte, docs/ricerca.md): non ricevono mai lo stato vero.
// Le azioni legali sono quelle della decisione vera: riguardano la propria mano, le zone pubbliche e le carte
// che il giocatore vede in quel momento (una ricerca nel proprio grimorio, le carte rivelate da Thoughtseize).
class ViewAgent {
public:
    virtual ~ViewAgent() = default;
    virtual Action choose(const PlayerView& view, std::span<const Action> legal) = 0;
    // La regola o il motivo dell'ultima scelta (per il registro delle regole del matchup).
    [[nodiscard]] virtual std::string_view last_rule() const = 0;
};

}  // namespace mtg
