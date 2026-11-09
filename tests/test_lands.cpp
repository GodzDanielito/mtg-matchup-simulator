// Test delle terre del pool. Prima di ogni carta c'è il suo testo Oracle,
// letto da Scryfall (bulk oracle_cards aggiornato l'8/11/2026 alle 22:02 UTC).
//
// Le abilità di mana non sono azioni di priorità: verranno attivate durante il
// pagamento dei costi (regola 601.2g), quando ci saranno magie da lanciare.
// Qui si verifica quali colori ogni terra può produrre.

#include "test_helpers.hpp"

#include <algorithm>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

constexpr ColorMask W = color_bit(Color::White);
constexpr ColorMask U = color_bit(Color::Blue);
constexpr ColorMask B = color_bit(Color::Black);
constexpr ColorMask R = color_bit(Color::Red);

ColorMask mana_of(const GameState& s, ObjRef ref) {
    return mana_colors(effective_def(s, db_with_tokens(), ref.card));
}

bool tapped(const GameState& s, ObjRef ref) { return s.objects[ref.card].has_flag(ObjectFlag::Tapped); }

std::vector<CardId> library_of(const GameState& s, PlayerId p) {
    return {s.players[p].library.begin(), s.players[p].library.end()};
}

}  // namespace

// ---------------------------------------------------------------------------
// Plains — Basic Land — Plains
//   ({T}: Add {W}.)
// Swamp — Basic Land — Swamp
//   ({T}: Add {B}.)
// ---------------------------------------------------------------------------
TEST_CASE("Terre base: entrano stappate e producono il loro colore", "[lands]") {
    Staged st = staged(kUw, {"Plains"}, {"Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Plains"));
    const ObjRef plains = find_on_battlefield(st.s, "Plains");
    CHECK_FALSE(tapped(st.s, plains));
    CHECK(mana_of(st.s, plains) == W);

    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::PlayLand, "Swamp"));
    const ObjRef swamp = find_on_battlefield(st.s, "Swamp");
    CHECK_FALSE(tapped(st.s, swamp));
    CHECK(mana_of(st.s, swamp) == B);
}

// ---------------------------------------------------------------------------
// Blood Crypt — Land — Swamp Mountain
//   ({T}: Add {B} or {R}.)
//   As this land enters, you may pay 2 life. If you don't, it enters tapped.
// Hallowed Fountain — Land — Plains Island
//   ({T}: Add {W} or {U}.)
//   As this land enters, you may pay 2 life. If you don't, it enters tapped.
// ---------------------------------------------------------------------------
TEST_CASE("Blood Crypt: pagare 2 vita per entrare stappata", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Blood Crypt"});
    act(st, find_action(st.s, ActionType::PlayLand, "Blood Crypt"));
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kRakdos));
    CHECK(st.s.pending.amount == 2);
    CHECK(legal(st.s).size() == 2);
    act(st, option(kPayLife));

    const ObjRef crypt = find_on_battlefield(st.s, "Blood Crypt");
    CHECK(st.s.players[kRakdos].life == 18);
    CHECK_FALSE(tapped(st.s, crypt));
    CHECK(mana_of(st.s, crypt) == (B | R));
}

TEST_CASE("Hallowed Fountain: senza pagare entra tappata", "[lands]") {
    Staged st = staged(kUw, {"Hallowed Fountain"}, {"Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Hallowed Fountain"));
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kUw));
    act(st, option(kEnterTapped));

    const ObjRef fountain = find_on_battlefield(st.s, "Hallowed Fountain");
    CHECK(st.s.players[kUw].life == 20);
    CHECK(tapped(st.s, fountain));
    CHECK(mana_of(st.s, fountain) == (W | U));
}

TEST_CASE("Shockland: con meno di 2 vita non si puo' pagare", "[lands]") {
    Staged st = staged(kUw, {"Hallowed Fountain"}, {"Swamp"});
    st.s.players[kUw].life = 1;
    act(st, find_action(st.s, ActionType::PlayLand, "Hallowed Fountain"));
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kUw));
    CHECK(legal(st.s) == std::vector<Action>{option(kEnterTapped)});
}

TEST_CASE("Shockland: con 2 vita si puo' pagare e si perde", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Blood Crypt"});
    st.s.players[kRakdos].life = 2;
    act(st, find_action(st.s, ActionType::PlayLand, "Blood Crypt"));
    act(st, option(kPayLife));
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == kUw);
}

// ---------------------------------------------------------------------------
// Blackcleave Cliffs — Land
//   This land enters tapped unless you control two or fewer other lands.
//   {T}: Add {B} or {R}.
// ---------------------------------------------------------------------------
TEST_CASE("Blackcleave Cliffs: stappata con due o meno altre terre", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Blackcleave Cliffs"}, {}, {"Swamp", "Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Blackcleave Cliffs"));
    const ObjRef cliffs = find_on_battlefield(st.s, "Blackcleave Cliffs");
    CHECK_FALSE(tapped(st.s, cliffs));
    CHECK(mana_of(st.s, cliffs) == (B | R));
}

TEST_CASE("Blackcleave Cliffs: tappata con tre altre terre", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Blackcleave Cliffs"}, {}, {"Swamp", "Swamp", "Blood Crypt"});
    act(st, find_action(st.s, ActionType::PlayLand, "Blackcleave Cliffs"));
    CHECK(tapped(st.s, find_on_battlefield(st.s, "Blackcleave Cliffs")));
}

// ---------------------------------------------------------------------------
// Raucous Theater — Land — Swamp Mountain
//   ({T}: Add {B} or {R}.)
//   This land enters tapped.
//   When this land enters, surveil 1. (Look at the top card of your library.
//   You may put it into your graveyard.)
// Meticulous Archive — Land — Plains Island
//   ({T}: Add {W} or {U}.)
//   This land enters tapped.
//   When this land enters, surveil 1. (Look at the top card of your library.
//   You may put it into your graveyard.)
// ---------------------------------------------------------------------------
TEST_CASE("Raucous Theater: entra tappata, il sorvegliare passa dalla pila", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"});
    const CardId top = st.s.players[kRakdos].library.back();
    act(st, find_action(st.s, ActionType::PlayLand, "Raucous Theater"));

    // Il trigger è andato in pila e i due giocatori hanno passato (da soli).
    REQUIRE(is_decision(st.s, DecisionKind::Surveil, kRakdos));
    CHECK(st.s.stack_items.empty());
    const ObjRef theater = find_on_battlefield(st.s, "Raucous Theater");
    CHECK(tapped(st.s, theater));
    CHECK(mana_of(st.s, theater) == (B | R));

    act(st, option(kSurveilToGraveyard));
    CHECK(st.s.objects[top].zone == Zone::Graveyard);
    CHECK(st.s.players[kRakdos].gy.card_count == 1);
}

TEST_CASE("Meticulous Archive: sorvegliare lasciando la carta in cima", "[lands]") {
    Staged st = staged(kUw, {"Meticulous Archive"}, {"Swamp"});
    const CardId top = st.s.players[kUw].library.back();
    CHECK(st.s.objects[top].known_to == 0);
    act(st, find_action(st.s, ActionType::PlayLand, "Meticulous Archive"));
    REQUIRE(is_decision(st.s, DecisionKind::Surveil, kUw));
    act(st, option(kSurveilKeepOnTop));

    CHECK(st.s.players[kUw].library.back() == top);
    CHECK(st.s.objects[top].known_to == (1u << kUw));  // ora il giocatore la conosce
    const ObjRef archive = find_on_battlefield(st.s, "Meticulous Archive");
    CHECK(tapped(st.s, archive));
    CHECK(mana_of(st.s, archive) == (W | U));
}

// ---------------------------------------------------------------------------
// Fetchland (tutte con lo stesso schema):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a <A> or <B>
//   card, put it onto the battlefield, then shuffle.
// Arid Mesa: Mountain or Plains     Bloodstained Mire: Swamp or Mountain
// Flooded Strand: Plains or Island  Marsh Flats: Plains or Swamp
// Polluted Delta: Island or Swamp   Windswept Heath: Forest or Plains
// ---------------------------------------------------------------------------
TEST_CASE("Fetchland: tipi cercabili come da Oracle", "[lands]") {
    const CardDatabase& db = db_with_tokens();
    auto types = [&](const char* name) { return db.find(name)->rules.land.fetch_types; };
    const auto plains = land_type_bit(LandType::Plains), island = land_type_bit(LandType::Island),
               swamp = land_type_bit(LandType::Swamp), mountain = land_type_bit(LandType::Mountain),
               forest = land_type_bit(LandType::Forest);
    CHECK(types("Arid Mesa") == (mountain | plains));
    CHECK(types("Bloodstained Mire") == (swamp | mountain));
    CHECK(types("Flooded Strand") == (plains | island));
    CHECK(types("Marsh Flats") == (plains | swamp));
    CHECK(types("Polluted Delta") == (island | swamp));
    CHECK(types("Windswept Heath") == (forest | plains));
    for (const char* name : {"Arid Mesa", "Bloodstained Mire", "Flooded Strand", "Marsh Flats", "Polluted Delta",
                             "Windswept Heath"}) {
        CHECK(db.find(name)->rules.land.fetch_life == 1);
        CHECK(mana_colors(*db.find(name)) == 0);  // nessuna abilità di mana
    }
}

TEST_CASE("Marsh Flats: paga 1 vita, si sacrifica, cerca, mescola", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // mantenimento del turno 1
    act(st, find_action(st.s, ActionType::ActivateAbility, "Marsh Flats"));

    // Costi pagati subito; l'abilità passa dalla pila e si risolve quando entrambi passano.
    CHECK(st.s.players[kRakdos].life == 19);
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));
    CHECK(st.s.stack_items.empty());

    // Plains o Swamp: Swamp, Blood Crypt, Raucous Theater (una scelta per nome) + "non trovare".
    const auto choices = legal(st.s);
    CHECK(choices.size() == 4);
    CHECK(std::count(choices.begin(), choices.end(), option(kDeclineSearch)) == 1);

    const Action take_swamp = find_action(st.s, ActionType::ChooseCard, "Swamp");
    std::vector<CardId> before = library_of(st.s, kRakdos);
    before.erase(std::find(before.begin(), before.end(), take_swamp.card));  // ordine senza la carta cercata
    act(st, take_swamp);
    const ObjRef swamp = find_on_battlefield(st.s, "Swamp");
    CHECK_FALSE(tapped(st.s, swamp));
    CHECK(st.s.players[kRakdos].library.size() == 58);
    CHECK(library_of(st.s, kRakdos) != before);  // stesse carte, ordine diverso: mescolato
}

TEST_CASE("Fetchland: cercare una shockland chiede se pagare", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Bloodstained Mire"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Bloodstained Mire"));
    act(st, find_action(st.s, ActionType::ChooseCard, "Blood Crypt"));
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kRakdos));
    act(st, option(kPayLife));
    CHECK(st.s.players[kRakdos].life == 17);  // 1 per la fetch, 2 per la shockland
    CHECK_FALSE(tapped(st.s, find_on_battlefield(st.s, "Blood Crypt")));
}

TEST_CASE("Fetchland: cercare Raucous Theater fa scattare il sorvegliare", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Polluted Delta"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Polluted Delta"));
    act(st, find_action(st.s, ActionType::ChooseCard, "Raucous Theater"));
    REQUIRE(is_decision(st.s, DecisionKind::Surveil, kRakdos));
    CHECK(tapped(st.s, find_on_battlefield(st.s, "Raucous Theater")));
}

TEST_CASE("Fetchland: si puo' non trovare nulla, ma si mescola", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Marsh Flats"));
    const std::vector<CardId> before = library_of(st.s, kRakdos);
    act(st, option(kDeclineSearch));
    CHECK(st.s.players[kRakdos].library.size() == 59);
    CHECK(library_of(st.s, kRakdos) != before);
}

TEST_CASE("Arid Mesa: trova Plains e terre Plains, non le bifronti in grimorio", "[lands]") {
    Staged st = staged(kUw, {}, {"Swamp"}, {"Arid Mesa"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Arid Mesa"));
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    std::vector<std::string> names;
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::ChooseCard) names.push_back(db_with_tokens().get(st.s.objects[a.card].def).name);
    }
    std::sort(names.begin(), names.end());
    // Witch Enchanter nel grimorio ha solo le caratteristiche del fronte: non è una terra.
    CHECK(names == std::vector<std::string>{"Hallowed Fountain", "Meticulous Archive", "Plains"});
}

// L'abilità della fetch NON è un'abilità di mana (non produce mana, 605.1a):
// va in pila e l'avversario può rispondere prima che si risolva.
TEST_CASE("Fetchland: l'abilita' va in pila e l'avversario puo' rispondere", "[lands][stack]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {"Arid Mesa"}, {"Marsh Flats"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::ActivateAbility, "Marsh Flats"));

    // Rakdos passa da solo; il giocatore 0 ha la priorità con l'abilità in pila.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK(st.s.stack_items.back().kind == StackKind::ActivatedAbility);
    CHECK(st.s.stack_items.back().effect == StackEffect::FetchSearch);
    CHECK(st.s.stack_items.back().controller == kRakdos);
    CHECK(st.s.players[kRakdos].life == 19);  // costi già pagati

    act(st, pass());
    CHECK(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));
    CHECK(st.s.stack_items.empty());
}

// Il sorvegliare di Raucous Theater e Meticulous Archive è un'abilità innescata
// ("When this land enters"): va in pila, non avviene mentre la terra entra.
TEST_CASE("Sorvegliare: abilita' innescata che va in pila", "[lands][stack]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {"Arid Mesa"});
    // Con la fetch in campo il giocatore 0 può agire in ogni step: passa fino alla fase principale di Rakdos.
    while (is_decision(st.s, DecisionKind::Priority, kUw)) act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    REQUIRE(st.s.turn.step == Step::Main1);
    act(st, find_action(st.s, ActionType::PlayLand, "Raucous Theater"));

    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK(st.s.stack_items.back().kind == StackKind::TriggeredAbility);
    CHECK(st.s.stack_items.back().effect == StackEffect::Surveil);
    CHECK(st.s.stack_items.back().controller == kRakdos);
    CHECK(st.s.pending_triggers.empty());

    act(st, pass());
    CHECK(is_decision(st.s, DecisionKind::Surveil, kRakdos));
}

TEST_CASE("Fetchland: con 1 vita si puo' attivare e si perde", "[lands]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats"});
    st.s.players[kRakdos].life = 1;
    act(st, find_action(st.s, ActionType::ActivateAbility, "Marsh Flats"));
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == kUw);
}

// ---------------------------------------------------------------------------
// Witch Enchanter // Witch-Blessed Meadow (modal_dfc)
//   Fronte — Creature — Human Warlock: When this creature enters, destroy
//     target artifact or enchantment an opponent controls.   (non implementato)
//   Retro — Land:
//     As this land enters, you may pay 3 life. If you don't, it enters tapped.
//     {T}: Add {W}.
// ---------------------------------------------------------------------------
TEST_CASE("Witch-Blessed Meadow: si gioca solo il retro, pagando 3 vita", "[lands]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {"Swamp"});
    const auto actions = legal(st.s);
    CHECK(actions.size() == 2);  // passa e il retro come terra; il fronte non è una terra
    const Action meadow = find_action(st.s, ActionType::PlayLand, "Witch Enchanter", 1);

    act(st, meadow);
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kUw));
    CHECK(st.s.pending.amount == 3);
    act(st, option(kPayLife));

    const ObjRef land = st.s.ref(meadow.card);
    const CardDef& def = effective_def(st.s, db_with_tokens(), land.card);
    CHECK(def.name == "Witch-Blessed Meadow");
    CHECK(def.has_type(CardType::Land));
    CHECK_FALSE(tapped(st.s, land));
    CHECK(mana_of(st.s, land) == W);
    CHECK(st.s.players[kUw].life == 17);

    // Al cimitero torna Witch Enchanter: conta come creatura, non come terra.
    move_object(st.s, db_with_tokens(), land, Zone::Graveyard, Cause::Test);
    require_invariants(st.s);
    const auto& gy = st.s.players[kUw].gy;
    CHECK(gy.per_type[static_cast<std::size_t>(CardType::Creature)] == 1);
    CHECK(gy.per_type[static_cast<std::size_t>(CardType::Land)] == 0);
}

// ---------------------------------------------------------------------------

TEST_CASE("Ogni terra delle liste e' implementata, il resto non ancora", "[lands][config]") {
    const CardDatabase& db = test::real_db();
    for (const char* file : {"/uw_blink_sim.json", "/rakdos_delirium.json"}) {
        for (const auto& entry : load_decklist(std::string(DECKS_DIR) + file).main) {
            const CardDef& front = *db.find(entry.card);
            const bool land = front.has_type(CardType::Land);
            const bool back_land = front.back_face != kNoDef && db.get(front.back_face).has_type(CardType::Land);
            INFO(entry.card);
            if (land) CHECK(front.rules.implemented);
            if (back_land) CHECK(db.get(front.back_face).rules.implemented);
            if (!land) CHECK_FALSE(front.rules.implemented);  // le carte non terra arrivano nei passi successivi
        }
    }
}
