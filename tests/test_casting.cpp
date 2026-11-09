// Test del lancio delle magie (601.2), della risoluzione della pila e delle
// prime carte non terra. Testo Oracle da Scryfall (bulk oracle_cards
// aggiornato l'8/11/2026 alle 22:02 UTC), riportato prima dei test di ogni carta.
//
// Le creature possono essere lanciate ed entrare in campo; il combattimento
// arriva nel passo successivo.

#include "test_helpers.hpp"

#include <algorithm>
#include <set>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

Action target_player(PlayerId p) { return Action{ActionType::TargetPlayer, kNoCard, p}; }
Action target_object(ObjRef r) { return Action{ActionType::TargetObject, r.card, 0}; }

bool tapped(const GameState& s, CardId id) { return s.objects[id].has_flag(ObjectFlag::Tapped); }

bool has(const std::vector<Action>& actions, const Action& a) {
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

bool can_cast(const GameState& s, std::string_view name) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::CastSpell && db_with_tokens().get(s.objects[a.card].def).name == name) return true;
    }
    return false;
}

bool logged_autopay(const GameLog& log) {
    return std::any_of(log.entries.begin(), log.entries.end(),
                       [](const LogEntry& e) { return e.kind == LogEntry::Kind::AutoPay; });
}

// Il giocatore 0 con una fetch in campo può agire in ogni step: passa finché
// la decisione non tocca a `player` nello step indicato.
void pass_until(Staged& st, PlayerId player, Step step) {
    for (int guard = 0; guard < 200; ++guard) {
        if (is_decision(st.s, DecisionKind::Priority, player) && st.s.turn.step == step) return;
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        act(st, pass());
    }
    FAIL("decisione attesa non raggiunta");
}

// Passa (chiunque abbia la priorità) fino alla priorità di `player` nello step e turno indicati.
void pass_until_turn(Staged& st, std::uint16_t turn, PlayerId player, Step step) {
    for (int guard = 0; guard < 400; ++guard) {
        if (st.s.turn.turn_number == turn && st.s.turn.step == step &&
            is_decision(st.s, DecisionKind::Priority, player)) {
            return;
        }
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        act(st, pass());
    }
    FAIL("decisione attesa non raggiunta");
}

}  // namespace

// ---------------------------------------------------------------------------
// Lightning Bolt — {R} — Instant
//   Lightning Bolt deals 3 damage to any target.
// ---------------------------------------------------------------------------

TEST_CASE("Lightning Bolt al giocatore: in pila, bersaglio, pagamento, risoluzione", "[casting][bolt]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {}, {"Blood Crypt"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // istantaneo: già nel mantenimento
    CHECK(st.s.turn.step == Step::Upkeep);
    const Action cast = find_action(st.s, ActionType::CastSpell, "Lightning Bolt");
    act(st, cast);

    // 601.2a: la carta è già in pila mentre si scelgono i bersagli.
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    CHECK(st.s.objects[cast.card].zone == Zone::Stack);
    CHECK(legal(st.s).size() == 2);  // i due giocatori, nessuna creatura in campo
    act(st, target_player(kUw));

    // Un solo modo di pagare: pagamento automatico, registrato.
    CHECK(logged_autopay(st.log));
    CHECK(st.s.players[kUw].life == 17);
    CHECK(st.s.objects[cast.card].zone == Zone::Graveyard);
    CHECK(tapped(st.s, find_on_battlefield(st.s, "Blood Crypt").card));
    CHECK(st.s.stack.empty());
}

TEST_CASE("Lightning Bolt su una creatura: danno letale e morte", "[casting][bolt]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {"Arid Mesa"}, {"Blood Crypt", "Street Wraith"});
    const ObjRef wraith = find_on_battlefield(st.s, "Street Wraith");  // 3/4: sopravvive
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    CHECK(legal(st.s).size() == 3);  // due giocatori e Street Wraith
    act(st, target_object(wraith));

    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));  // risposta possibile (fetch)
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK(st.s.stack_items.back().kind == StackKind::Spell);
    act(st, pass());
    CHECK(st.s.objects[wraith.card].damage == 3);
    CHECK(st.s.objects[wraith.card].zone == Zone::Battlefield);
}

TEST_CASE("Magia neutralizzata se il bersaglio non e' piu' legale", "[casting][stack]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt", "Lightning Bolt"}, {},
                       {"Blood Crypt", "Blood Crypt", "Stitcher's Supplier"});
    const ObjRef supplier = find_on_battlefield(st.s, "Stitcher's Supplier");
    const Action first = find_action(st.s, ActionType::CastSpell, "Lightning Bolt");
    act(st, first);
    act(st, target_object(supplier));

    // In risposta, il secondo Bolt sulla stessa creatura.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    const Action second = find_action(st.s, ActionType::CastSpell, "Lightning Bolt");
    CHECK(second.card != first.card);
    act(st, second);
    act(st, target_object(supplier));

    // Il secondo Bolt uccide la creatura; il primo trova il bersaglio sparito.
    CHECK(st.s.objects[supplier.card].zone == Zone::Graveyard);
    CHECK(st.s.objects[first.card].zone == Zone::Graveyard);
    CHECK(st.s.objects[second.card].zone == Zone::Graveyard);
    CHECK(st.s.players[kUw].life == 20);
    CHECK(st.s.players[kRakdos].life == 20);
    CHECK(st.s.stack_items.empty());
}

// ---------------------------------------------------------------------------
// Velocità di lancio
// ---------------------------------------------------------------------------

TEST_CASE("Velocita': istantanei sempre, il resto solo in fase principale a pila vuota", "[casting]") {
    Staged st = staged(kUw, {"Plains"}, {"Thoughtseize", "Lightning Bolt", "Lightning Bolt", "Stitcher's Supplier"},
                       {}, {"Blood Crypt", "Blood Crypt", "Swamp"});
    // Mantenimento del giocatore 0: Rakdos può lanciare solo Bolt.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(can_cast(st.s, "Lightning Bolt"));
    CHECK_FALSE(can_cast(st.s, "Thoughtseize"));
    CHECK_FALSE(can_cast(st.s, "Stitcher's Supplier"));

    // Fase principale di Rakdos (turno 2): ora sì.
    for (int guard = 0; guard < 200 && !(st.s.turn.turn_number == 2 && st.s.turn.step == Step::Main1 &&
                                         is_decision(st.s, DecisionKind::Priority, kRakdos));
         ++guard) {
        act(st, pass());
    }
    REQUIRE(st.s.turn.turn_number == 2);
    CHECK(can_cast(st.s, "Thoughtseize"));
    CHECK(can_cast(st.s, "Stitcher's Supplier"));

    // Con una magia in pila, stregonerie e creature non si possono lanciare.
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    act(st, target_player(kUw));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(can_cast(st.s, "Lightning Bolt"));  // la seconda Blood Crypt
    CHECK_FALSE(can_cast(st.s, "Thoughtseize"));
    CHECK_FALSE(can_cast(st.s, "Stitcher's Supplier"));
}

// ---------------------------------------------------------------------------
// Pagamento: scelta tra modi che lasciano fonti diverse
// ---------------------------------------------------------------------------

TEST_CASE("Pagamento: l'agente sceglie quale terra lasciare stappata", "[casting][mana]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Stitcher's Supplier"}, {}, {"Swamp", "Blood Crypt"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::CastSpell, "Stitcher's Supplier"));
    REQUIRE(is_decision(st.s, DecisionKind::PayMana, kRakdos));
    const auto choices = legal(st.s);
    REQUIRE(choices.size() == 2);

    std::multiset<bool> crypt_untapped;
    for (const Action& choice : choices) {
        Staged copy = st;
        act(copy, choice);
        crypt_untapped.insert(!tapped(copy.s, find_on_battlefield(copy.s, "Blood Crypt").card));
    }
    CHECK(crypt_untapped == std::multiset<bool>{false, true});  // una volta resta la Crypt, una volta la Swamp
}

// ---------------------------------------------------------------------------
// Thoughtseize — {B} — Sorcery
//   Target player reveals their hand. You choose a nonland card from it. That
//   player discards that card. You lose 2 life.
// ---------------------------------------------------------------------------

TEST_CASE("Thoughtseize: rivela, sceglie una carta non terra, 2 vita", "[casting][thoughtseize]") {
    Staged st = staged(kRakdos, {"Plains", "Solitude", "Ephemerate", "Solitude"}, {"Thoughtseize"}, {}, {"Swamp"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::Main1);
    const Action cast = find_action(st.s, ActionType::CastSpell, "Thoughtseize");
    act(st, cast);
    act(st, target_player(kUw));

    REQUIRE(is_decision(st.s, DecisionKind::ChooseFromHand, kRakdos));
    for (const CardId id : st.s.players[kUw].hand) {
        CHECK((st.s.objects[id].known_to & (1u << kRakdos)) != 0);  // la mano è stata rivelata
    }
    CHECK(legal(st.s).size() == 2);  // Solitude ed Ephemerate; la Plains no
    act(st, find_action(st.s, ActionType::ChooseCard, "Solitude"));

    // Il motore prosegue fino alla decisione successiva (dopo la pescata del giocatore 0).
    REQUIRE(st.s.players[kUw].graveyard.size() == 1);
    CHECK(db_with_tokens().get(st.s.objects[st.s.players[kUw].graveyard[0]].def).name == "Solitude");
    CHECK(st.s.players[kUw].hand.size() + st.s.players[kUw].library.size() == 59);
    CHECK(st.s.players[kRakdos].life == 18);
    CHECK(st.s.objects[cast.card].zone == Zone::Graveyard);
}

TEST_CASE("Thoughtseize su se stessi senza carte non terra", "[casting][thoughtseize]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Thoughtseize"}, {}, {"Swamp"});
    act(st, find_action(st.s, ActionType::CastSpell, "Thoughtseize"));
    act(st, target_player(kRakdos));
    CHECK(st.s.players[kRakdos].life == 18);  // si perde vita anche senza scarto
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
}

// ---------------------------------------------------------------------------
// Street Wraith — {3}{B}{B} — Creature — Wraith 3/4
//   Swampwalk (This creature can't be blocked as long as defending player
//   controls a Swamp.)                                  (arriva con il combattimento)
//   Cycling—Pay 2 life. (Pay 2 life, Discard this card: Draw a card.)
// ---------------------------------------------------------------------------

TEST_CASE("Street Wraith: ciclo pagando 2 vita", "[casting][wraith]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Street Wraith"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // velocità istantaneo, senza mana
    CHECK(legal(st.s).size() == 2);  // passa e ciclo: per lanciarla servono 5 mana
    act(st, find_action(st.s, ActionType::ActivateAbility, "Street Wraith"));

    CHECK(st.s.players[kRakdos].life == 18);
    CHECK(st.s.players[kRakdos].gy.per_type[static_cast<std::size_t>(CardType::Creature)] == 1);
    CHECK(st.s.players[kRakdos].hand.size() == 1);  // ha pescato
    CHECK(st.s.players[kRakdos].library.size() == 58);
}

TEST_CASE("Street Wraith: niente ciclo con meno di 2 vita", "[casting][wraith]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Street Wraith"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    st.s.players[kRakdos].life = 1;
    CHECK(legal(st.s) == std::vector<Action>{pass()});
}

TEST_CASE("Street Wraith: si lancia come creatura con 5 mana", "[casting][wraith]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Street Wraith"}, {},
                       {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Blood Crypt"});
    pass_until(st, kRakdos, Step::Main1);
    act(st, find_action(st.s, ActionType::CastSpell, "Street Wraith"));
    const ObjRef wraith = find_on_battlefield(st.s, "Street Wraith");
    CHECK(st.s.objects[wraith.card].has_flag(ObjectFlag::SummoningSick));
    CHECK(std::all_of(st.s.battlefield.begin(), st.s.battlefield.end(), [&](CardId id) {
        return st.s.objects[id].controller != kRakdos || id == wraith.card || tapped(st.s, id);
    }));
}

// ---------------------------------------------------------------------------
// Mishra's Bauble — {0} — Artifact
//   {T}, Sacrifice this artifact: Look at the top card of target player's
//   library. Draw a card at the beginning of the next turn's upkeep.
// ---------------------------------------------------------------------------

TEST_CASE("Mishra's Bauble: guarda la cima e pesca al prossimo mantenimento", "[casting][bauble]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Mishra's Bauble"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::Main1);  // artefatto: velocità stregoneria
    act(st, find_action(st.s, ActionType::CastSpell, "Mishra's Bauble"));  // costo {0}: nessun pagamento

    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    const ObjRef bauble = find_on_battlefield(st.s, "Mishra's Bauble");
    act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    const CardId top = st.s.players[kUw].library.back();
    act(st, target_player(kUw));

    // Costi pagati (tappare e sacrificare), abilità risolta: la cima è nota a Rakdos.
    CHECK(st.s.objects[bauble.card].zone == Zone::Graveyard);
    CHECK(st.s.players[kRakdos].gy.per_type[static_cast<std::size_t>(CardType::Artifact)] == 1);

    // Turno 2 del giocatore 0: al mantenimento Rakdos pesca; poi il giocatore 0 pesca proprio la carta vista.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK(st.s.turn.turn_number == 2);
    CHECK(st.s.players[kRakdos].hand.size() == 1);
    CHECK(st.s.delayed.empty());
    CHECK(st.s.objects[top].zone == Zone::Hand);
    CHECK((st.s.objects[top].known_to & (1u << kRakdos)) != 0);  // Rakdos sa che quella carta è in mano
}

// "the next turn's upkeep" è il mantenimento del turno successivo, chiunque sia
// il giocatore attivo: attivata nel turno dell'avversario, si pesca nel proprio.
TEST_CASE("Mishra's Bauble: attivata nel turno avversario, pesca nel proprio mantenimento", "[casting][bauble]") {
    Staged st = staged(kUw, {"Plains"}, {"Swamp"}, {}, {"Mishra's Bauble"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // mantenimento del turno 1 (giocatore 0)
    CHECK(st.s.turn.active_player == kUw);
    act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
    act(st, target_player(kUw));

    // Per tutto il turno del giocatore 0 Rakdos non pesca.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK(st.s.turn.turn_number == 1);
    CHECK(st.s.players[kRakdos].hand.size() == 1);
    CHECK(st.s.delayed.size() == 1);

    pass_until_turn(st, 2, kRakdos, Step::Main1);
    CHECK(st.s.delayed.empty());
    CHECK(st.s.players[kRakdos].hand.size() == 3);  // Swamp, pescata ritardata, pescata del turno
}

TEST_CASE("Mishra's Bauble: attivata nel proprio mantenimento, pesca al turno successivo", "[casting][bauble]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Swamp"}, {}, {"Mishra's Bauble"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::Upkeep);
    act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
    act(st, target_player(kRakdos));

    // Lo stesso mantenimento è già cominciato: niente pescata ora.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK(st.s.turn.step == Step::Main1);
    CHECK(st.s.players[kRakdos].hand.size() == 1);  // solo la Swamp
    CHECK(st.s.delayed.size() == 1);

    // Turno successivo (del giocatore 0): al suo mantenimento Rakdos pesca.
    act(st, pass());
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK(st.s.turn.turn_number == 2);
    CHECK(st.s.players[kRakdos].hand.size() == 2);
    CHECK(st.s.delayed.empty());
}

TEST_CASE("Mishra's Bauble: due pescate ritardate identiche", "[casting][bauble]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Mishra's Bauble", "Mishra's Bauble"});
    for (int i = 0; i < 2; ++i) {
        act(st, find_action(st.s, ActionType::CastSpell, "Mishra's Bauble"));
        act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
        act(st, target_player(i == 0 ? kUw : kRakdos));
    }
    // Due trigger identici dello stesso giocatore allo stesso mantenimento: l'ordine non conta.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK(st.s.turn.turn_number == 2);
    CHECK(st.s.players[kRakdos].hand.size() == 2);
}

// ---------------------------------------------------------------------------
// Stitcher's Supplier — {B} — Creature — Zombie 1/1
//   When this creature enters or dies, mill three cards. (Put the top three
//   cards of your library into your graveyard.)
// ---------------------------------------------------------------------------

TEST_CASE("Stitcher's Supplier: entra e macina 3 carte in un solo evento", "[casting][supplier]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Stitcher's Supplier"}, {"Arid Mesa"}, {"Swamp"});
    pass_until(st, kRakdos, Step::Main1);
    act(st, find_action(st.s, ActionType::CastSpell, "Stitcher's Supplier"));

    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));  // magia in pila
    CHECK(st.s.stack_items.back().kind == StackKind::Spell);
    act(st, pass());

    // La creatura è entrata; il trigger è in pila e il giocatore 0 può rispondere.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK(st.s.stack_items.back().kind == StackKind::TriggeredAbility);
    CHECK(st.s.stack_items.back().effect == StackEffect::Mill);
    const auto events = st.s.event_serial;
    const auto library = st.s.players[kRakdos].library.size();
    act(st, pass());

    CHECK(st.s.event_serial == events + 1);  // UN evento di gruppo per tre carte
    CHECK(st.s.players[kRakdos].graveyard.size() == 3);
    CHECK(st.s.players[kRakdos].library.size() == library - 3);
}

TEST_CASE("Stitcher's Supplier: quando muore macina 3 carte", "[casting][supplier]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {"Arid Mesa"}, {"Blood Crypt", "Stitcher's Supplier"});
    const ObjRef supplier = find_on_battlefield(st.s, "Stitcher's Supplier");
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    act(st, target_object(supplier));
    act(st, pass());  // il giocatore 0 lascia risolvere il Bolt

    // Muore per azione di stato; il trigger di morte è in pila.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK(st.s.objects[supplier.card].zone == Zone::Graveyard);
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK(st.s.stack_items.back().effect == StackEffect::Mill);
    CHECK(st.s.stack_items.back().controller == kRakdos);
    const auto events = st.s.event_serial;
    act(st, pass());
    CHECK(st.s.event_serial == events + 1);
    CHECK(st.s.players[kRakdos].graveyard.size() == 5);  // Bolt, Supplier e 3 carte macinate
}
