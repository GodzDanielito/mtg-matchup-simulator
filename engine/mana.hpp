#pragma once

// Pagamento del mana (docs/stato_di_gioco.md, sezione 10).
//
// Le terre stappate di un giocatore si raggruppano per insieme di colori
// prodotti. Un modo di pagare è un vettore di conteggi per classe: quante terre
// tappare di ogni classe. Ogni opzione restituita lascia un insieme DIVERSO di
// fonti di mana disponibili; i colori prodotti non sono una scelta.

#include "card_db.hpp"
#include "game_state.hpp"

#include <vector>

namespace mtg {

struct PaymentOption {
    std::vector<CardId> lands;  // terre da tappare (le prime di ogni classe, in ordine di campo)
};

// Tutti i modi di pagare `cost` che lasciano insiemi di fonti diversi, in ordine
// deterministico. Vuoto se il costo non si può pagare. Per il costo {0}: una
// sola opzione senza terre. {X} vale 0 (nessuna carta con X implementata).
[[nodiscard]] std::vector<PaymentOption> payment_options(const GameState& s, const CardDatabase& db, PlayerId p,
                                                         const ManaCost& cost);

// Vero se esiste almeno un modo di pagare (più veloce di payment_options).
[[nodiscard]] bool can_pay(const GameState& s, const CardDatabase& db, PlayerId p, const ManaCost& cost);

// Paga: tappa le terre (abilità di mana, 601.2g), mette il mana nella riserva e
// lo spende per il costo (601.2h). La riserva torna com'era. Lancia EngineError
// se l'opzione non basta a pagare.
void pay_mana(GameState& s, const CardDatabase& db, PlayerId p, const ManaCost& cost, const PaymentOption& option);

}  // namespace mtg
