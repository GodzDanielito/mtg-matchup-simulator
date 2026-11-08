#pragma once

// Cambi di zona: l'UNICO punto in cui le carte si muovono.
//
// move_objects sposta un gruppo di oggetti simultaneamente e produce UN evento
// di gruppo (ZoneChangeBatch). È qui che si aggiornano incarnation e riepilogo
// del cimitero, così il tracciamento del cimitero non è sparso tra le carte.

#include "card_db.hpp"
#include "game_state.hpp"

#include <span>
#include <string>
#include <vector>

namespace mtg {

enum class Cause : std::uint8_t {
    Unspecified,
    Draw,
    Mill,
    Discard,
    Destroy,
    Sacrifice,
    Exile,
    Cost,
    StateBased,
    Resolve,
    Test,
};

struct ZoneChange {
    CardId card = kNoCard;
    CardDefId def = kNoDef;
    PlayerId owner = kNoPlayer;
    Zone from = Zone::None;
    Zone to = Zone::None;
    bool is_token = false;
    ObjRef old_ref;
    ObjRef new_ref;
};

struct ZoneChangeBatch {
    StaticVector<ZoneChange, kMaxObjects> moves;
    Cause cause = Cause::Unspecified;
};

// Sposta tutti gli oggetti nella zona `to` (per grimorio, mano e cimitero:
// quella del proprietario; in grimorio vanno in cima). Controlla tutto prima
// di muovere: con un riferimento non valido, un duplicato o una carta già in
// `to`, lancia EngineError e lo stato resta invariato.
ZoneChangeBatch move_objects(GameState& s, const CardDatabase& db, std::span<const ObjRef> refs, Zone to,
                             Cause cause);

// Comodità per un oggetto solo: è comunque un evento di gruppo con un elemento.
ZoneChangeBatch move_object(GameState& s, const CardDatabase& db, ObjRef ref, Zone to, Cause cause);

// Crea un token in campo sotto il controllo di `controller` nel primo slot libero.
// Lancia EngineLimitError se i 32 slot sono occupati.
ObjRef create_token(GameState& s, const CardDatabase& db, CardDefId def, PlayerId controller);

// Un token fuori dal campo smette di esistere (azione di stato 704.5d): lo slot si libera.
void remove_token(GameState& s, ObjRef ref);

// Vero se nel gruppo almeno una CARTA permanente (non token) è finita nel
// cimitero di `owner`. È la condizione "una o più carte permanente" di Moonshadow.
[[nodiscard]] bool permanent_cards_put_into_graveyard(const ZoneChangeBatch& batch, const CardDatabase& db,
                                                      PlayerId owner);

// Riepilogo del cimitero ricalcolato da zero (controllo dell'incrementale).
[[nodiscard]] GraveyardSummary compute_graveyard_summary(const GameState& s, const CardDatabase& db,
                                                         PlayerId player);

// Controlla la coerenza dello stato: ogni oggetto sta nella lista della sua
// zona e solo lì, i riepiloghi dei cimiteri coincidono col ricalcolo.
// Restituisce un messaggio per ogni problema; vuoto = stato coerente.
[[nodiscard]] std::vector<std::string> check_invariants(const GameState& s, const CardDatabase& db);

}  // namespace mtg
