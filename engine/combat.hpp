#pragma once

// Combattimento (docs/stato_di_gioco.md, sezione 13; CR 506-511, 702).
//
// Le dichiarazioni di attacco e di blocco sono decisioni successive in ordine
// canonico: una per creatura (attacco), un bloccante alla volta per attaccante
// (blocco). Tra creature identiche si sceglie "quante", non "quali"; un'opzione
// è offerta solo se la dichiarazione resta completabile in modo legale.
// La divisione del danno offre un'opzione per esito distinto.

#include "card_db.hpp"
#include "game.hpp"
#include "game_state.hpp"

#include <vector>

namespace mtg {

// Parola chiave effettiva (stampata o data da un segnalino, come volare di Guide of Souls).
[[nodiscard]] bool has_keyword(const GameState& s, const CardDatabase& db, CardId id, Keyword k);

// Colori da cui l'oggetto ha protezione (702.16).
[[nodiscard]] ColorMask protection_of(const GameState& s, const CardDatabase& db, CardId id);

// Vero se l'oggetto è una creatura in campo ancora nel combattimento (506.4: chi lascia il campo esce).
[[nodiscard]] bool in_combat(const GameState& s, const CardDatabase& db, ObjRef ref);

namespace combat {

// Azioni del turno (chiamate dal motore all'inizio degli step).
void begin_attack_declaration(GameState& s, const CardDatabase& db);
void begin_block_declaration(GameState& s, const CardDatabase& db);
void begin_damage_step(GameState& s, const CardDatabase& db);  // step FirstStrikeDamage o CombatDamage

// 510.4: serve lo step del danno da attacco improvviso?
[[nodiscard]] bool needs_first_strike_step(const GameState& s, const CardDatabase& db);

// Fine del combattimento (511.3): tutte le creature escono dal combattimento.
void end_combat(GameState& s);

// Decisioni DeclareAttacker, DeclareBlockers, DivideDamage.
void legal_actions(const GameState& s, const CardDatabase& db, std::vector<Action>& out);
void apply(GameState& s, const CardDatabase& db, const PendingDecision& d, const Action& action);

// Danno inflitto da una fonte con quei colori a un oggetto: 0 se prevenuto dalla protezione.
[[nodiscard]] int damage_after_prevention(const GameState& s, const CardDatabase& db, CardId target,
                                          ColorMask source_colors, int amount);

}  // namespace combat

}  // namespace mtg
