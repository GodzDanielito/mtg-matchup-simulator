// Test aggiunti dopo l'audit 1 (docs/audit_1.md), gruppo 3 delle correzioni.
//
// - T-1, T-2, T-3: casi limite giusti nel motore ma senza un test.
// - T-5: per 22 regole un solo test falliva se la regola veniva rotta (sezione 3 dell'audit,
//   "copertura sottile"). Qui c'è un secondo test per ognuna, indipendente dal primo: un'altra
//   situazione di gioco, e dove possibile un altro effetto osservabile. Il commento sopra ogni
//   test nomina la mutazione che deve farlo fallire.
// - T-6: un mazzo di prova con una creatura di costituzione 3 per Recruiter of the Guard.
// - T-4 (Quantum Riddler con il grimorio quasi vuoto) è il secondo test di `grimorio_vuoto`.
//
// Testo Oracle delle carte: nei file di test di ogni carta (bulk oracle_cards di Scryfall
// aggiornato il 9/11/2026). Regole: Comprehensive Rules in vigore dal 6/11/2026.

#include "test_helpers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kPlusOne = 0;     // Teferi: +1
constexpr std::uint8_t kMinusThree = 1;  // Teferi: −3
constexpr const char* kTeferi = "Teferi, Time Raveler";
constexpr const char* kPhelia = "Phelia, Exuberant Shepherd";
constexpr const char* kRiddler = "Quantum Riddler";

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Passa la priorità finché la condizione non è vera. Non attacca, non blocca, non rilancia
// con il rimbalzo e mette in pila i trigger nell'ordine offerto. Qualunque altra decisione è
// un errore del test. La condizione si controlla prima di ogni scelta.
template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 800 && !done(st.s); ++guard) {
        switch (st.s.pending.kind) {
            case DecisionKind::DeclareAttacker: act(st, option(kNoAttack)); break;
            case DecisionKind::DeclareBlockers: act(st, option(kDoneBlocking)); break;
            case DecisionKind::CastFromExile: act(st, option(kDeclineCast)); break;
            case DecisionKind::OrderTriggers: act(st, legal(st.s).front()); break;
            default:
                REQUIRE(st.s.pending.kind == DecisionKind::Priority);
                act(st, pass());
        }
    }
    REQUIRE(done(st.s));
}

void to_main(Staged& st, PlayerId p) {
    pass_until(st, [p](const GameState& s) {
        return s.turn.active_player == p && s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority &&
               s.pending.player == p;
    });
}

auto at_turn(int turn) {
    return [turn](const GameState& s) {
        return s.turn.turn_number >= turn || s.pending.kind == DecisionKind::GameOver;
    };
}

void pay_all(Staged& st) {
    while (st.s.pending.kind == DecisionKind::PayMana) act(st, option(0));
}

void resolve(Staged& st) {
    for (int guard = 0; guard < 50 && !st.s.stack_items.empty() && st.s.pending.kind == DecisionKind::Priority;
         ++guard) {
        act(st, pass());
    }
}

Action cast_action(const GameState& s, const char* name, CastMethod method = CastMethod::Normal) {
    return find_action(s, ActionType::CastSpell, name, static_cast<std::uint8_t>(method));
}

bool can_cast(const GameState& s, const char* name, CastMethod method = CastMethod::Normal) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::CastSpell && a.value == static_cast<std::uint8_t>(method) && name_of(s, a.card) == name) {
            return true;
        }
    }
    return false;
}

bool can_activate(const GameState& s, const char* name, std::uint8_t value) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ActivateAbility && a.value == value && name_of(s, a.card) == name) return true;
    }
    return false;
}

Action target(const GameState& s, const char* name) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetObject && name_of(s, a.card) == name) return a;
    }
    FAIL("bersaglio non trovato: " << name);
    return {};
}

Action choose(const GameState& s, const char* name) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ChooseCard && name_of(s, a.card) == name) return a;
    }
    FAIL("carta non trovata tra le scelte: " << name);
    return {};
}

std::set<std::string> names_of(const GameState& s, ActionType type) {
    std::set<std::string> out;
    for (const Action& a : legal(s)) {
        if (a.type == type) out.insert(name_of(s, a.card));
    }
    return out;
}

bool in_zone(const GameState& s, PlayerId owner, Zone z, const char* name) {
    for (std::size_t id = 0; id < kMaxCards; ++id) {
        const CardObject& o = s.objects[id];
        if (o.owner == owner && o.zone == z && db_with_tokens().get(o.def).name == name) return true;
    }
    return false;
}

int count_tokens(const GameState& s, PlayerId p) {
    return static_cast<int>(std::count_if(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) {
        return s.objects[id].has_flag(ObjectFlag::Token) && s.objects[id].controller == p;
    }));
}

int loyalty(const GameState& s) { return s.objects[find_on_battlefield(s, kTeferi).card].counters.loyalty; }

bool at_end_step_with_stack(const GameState& s) {
    return s.turn.step == Step::End && !s.stack_items.empty() && s.pending.kind == DecisionKind::Priority;
}

// Dichiara gli attaccanti: per ogni creatura nominata "Teferi" o "giocatore"; le altre non attaccano.
void attack(Staged& st, std::initializer_list<std::pair<std::string, std::string>> plan) {
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        const std::string who = name_of(st.s, st.s.pending.subject.card);
        const auto it = std::find_if(plan.begin(), plan.end(), [&](const auto& p) { return p.first == who; });
        if (it == plan.end()) {
            act(st, option(kNoAttack));
        } else if (it->second == "Teferi") {
            act(st, target(st.s, kTeferi));
        } else {
            act(st, Action{ActionType::TargetPlayer, kNoCard, static_cast<PlayerId>(1 - st.s.turn.active_player)});
        }
    }
}

// Durante la dichiarazione dei bloccanti: il bloccante nominato blocca il primo attaccante.
void block_with(Staged& st, const char* blocker) {
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareBlockers; });
    act(st, choose(st.s, blocker));
    while (st.s.pending.kind == DecisionKind::DeclareBlockers) act(st, option(kDoneBlocking));
}

// Mazzo di prova: una copia della carta indicata al posto di un Windswept Heath, che nessuno
// di questi test usa (`create_game` vuole liste legali, 60 carte esatte).
Decklist with_copy(Decklist deck, const char* card) {
    const auto heath =
        std::find_if(deck.main.begin(), deck.main.end(), [](const DeckEntry& e) { return e.card == "Windswept Heath"; });
    REQUIRE(heath != deck.main.end());
    --heath->count;
    const auto it = std::find_if(deck.main.begin(), deck.main.end(), [&](const DeckEntry& e) { return e.card == card; });
    if (it == deck.main.end()) {
        deck.main.push_back(DeckEntry{card, 1});
    } else {
        ++it->count;
    }
    return deck;
}

// Come `staged`, con le liste scelte dal test.
Staged staged_decks(const Decklist& deck0, const Decklist& deck1, PlayerId starting,
                    std::initializer_list<const char*> hand0, std::initializer_list<const char*> field0,
                    std::initializer_list<const char*> field1 = {}) {
    Staged st{create_game(db_with_tokens(), deck0, deck1, 1), {}};
    put(st.s, 0, hand0, Zone::Hand);
    put(st.s, 0, field0, Zone::Battlefield);
    put(st.s, 1, field1, Zone::Battlefield);
    start_game(st.s, db_with_tokens(), StartOptions{starting, false}, &st.log);
    require_invariants(st.s);
    return st;
}

}  // namespace

// ===========================================================================
// T-1: una creatura lanciata con il warp ed esiliata prima della sottofase finale torna come
// oggetto nuovo (400.7); il trigger ritardato del warp cerca l'oggetto di prima (603.7c) e non
// lo trova più. La creatura resta in campo per sempre.
// ===========================================================================

TEST_CASE("Audit 1, T-1: Quantum Riddler con il warp salvato da Ephemerate resta in campo per sempre",
          "[audit1][riddler][warp][ephemerate]") {
    Staged st = staged(kUw, {kRiddler, "Ephemerate"}, {}, {"Hallowed Fountain", "Plains", "Plains", "Arid Mesa"});
    to_main(st, kUw);
    act(st, cast_action(st.s, kRiddler, CastMethod::Warp));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, kRiddler));
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, kRiddler));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, kRiddler));
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));  // rimbalzo
    // Due sottofasi finali di UW e una di Rakdos: nessuno lo esilia, e al terzo turno di UW c'è ancora.
    pass_until(st, at_turn(5));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, kRiddler));
    CHECK_FALSE(in_zone(st.s, kUw, Zone::Exile, kRiddler));
}

TEST_CASE("Audit 1, T-1: Quantum Riddler con il warp esiliato da Phelia torna e resta in campo",
          "[audit1][riddler][warp][phelia]") {
    Staged st = staged(kUw, {kRiddler}, {}, {kPhelia, "Hallowed Fountain", "Plains", "Arid Mesa"});
    to_main(st, kUw);
    act(st, cast_action(st.s, kRiddler, CastMethod::Warp));
    pay_all(st);
    resolve(st);
    attack(st, {{kPhelia, "giocatore"}});
    act(st, target(st.s, kRiddler));
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, kRiddler));
    pass_until(st, at_turn(5));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, kRiddler));
    // È rientrato sotto il controllo di UW: Phelia ha il segnalino (anche `phelia_segnalino`).
    CHECK(st.s.objects[find_on_battlefield(st.s, kPhelia).card].counters.plus_one == 1);
}

// ===========================================================================
// T-2: Phelia esilia il proprio Teferi. Torna come oggetto nuovo con la fedeltà stampata
// (306.5b) e senza il ricordo dell'abilità già attivata (606.3); Phelia prende il segnalino.
// Seconda copertura di `phelia_segnalino`.
// ===========================================================================

TEST_CASE("Audit 1, T-2: Phelia esilia Teferi a fedelta' 1, torna con 4 e Phelia prende il segnalino",
          "[audit1][phelia][teferi]") {
    Staged st = staged(kUw, {}, {}, {kPhelia, kTeferi, "Arid Mesa"});
    to_main(st, kUw);
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kPlusOne));
    resolve(st);
    st.s.objects[find_on_battlefield(st.s, kTeferi).card].counters.loyalty = 1;
    attack(st, {{kPhelia, "giocatore"}});
    act(st, target(st.s, kTeferi));
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, kTeferi));
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    const CardObject& teferi = st.s.objects[find_on_battlefield(st.s, kTeferi).card];
    CHECK(teferi.counters.loyalty == 4);
    CHECK_FALSE(teferi.has_flag(ObjectFlag::LoyaltyActivated));
    CHECK(st.s.objects[find_on_battlefield(st.s, kPhelia).card].counters.plus_one == 1);
}

// ===========================================================================
// T-3: trigger dei due giocatori in attesa insieme (603.3b): prima in pila quelli del giocatore
// attivo, poi quelli dell'altro, che quindi si risolvono per primi. Partita a specchio con due
// Mishra's Bauble attivate nello stesso turno: le due pescate ritardate scattano allo stesso
// mantenimento, una per giocatore. Si prova con ciascuno dei due come giocatore attivo.
// ===========================================================================

TEST_CASE("Audit 1, T-3: trigger dei due giocatori allo stesso mantenimento, in pila in ordine APNAP",
          "[audit1][triggers][bauble]") {
    for (const PlayerId first : {PlayerId{0}, PlayerId{1}}) {
        DYNAMIC_SECTION("inizia il giocatore " << static_cast<int>(first)) {
            const auto second = static_cast<PlayerId>(1 - first);
            Staged st{create_game(db_with_tokens(), rakdos_deck(), rakdos_deck(), 1), {}};
            for (const PlayerId p : {PlayerId{0}, PlayerId{1}}) put(st.s, p, {"Mishra's Bauble", "Marsh Flats"}, Zone::Battlefield);
            start_game(st.s, db_with_tokens(), StartOptions{first, false}, &st.log);
            require_invariants(st.s);
            // Turno 1: le due Bauble, prima quella del giocatore attivo, poi l'altra.
            for (const PlayerId p : {first, second}) {
                pass_until(st, [p](const GameState& s) {
                    return s.pending.kind == DecisionKind::Priority && s.pending.player == p && s.stack_items.empty();
                });
                act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
                act(st, Action{ActionType::TargetPlayer, kNoCard, p});
            }
            REQUIRE(st.s.turn.turn_number == 1);
            pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2 && s.stack_items.size() == 2; });
            REQUIRE(st.s.turn.step == Step::Upkeep);
            REQUIRE(st.s.turn.active_player == second);
            CHECK(st.s.stack_items[0].controller == second);  // giocatore attivo: in pila per primo
            CHECK(st.s.stack_items[1].controller == first);   // l'altro: in cima, si risolve per primo
            pass_until(st, [](const GameState& s) { return s.stack_items.size() == 1; });
            CHECK(st.s.players[first].hand.size() == 1);
            CHECK(st.s.players[second].hand.empty());
        }
    }
}

// ===========================================================================
// T-6: Recruiter of the Guard cerca "a creature card with toughness 2 or less". Nella lista di
// UW nessuna creatura ha costituzione 3, quindi la mutazione `recruiter_costituzione` ("3 o
// meno") non era distinguibile. Mazzo di prova: UW con un Fear of Missing Out (2/3).
// ===========================================================================

TEST_CASE("Audit 1, T-6: Recruiter of the Guard non offre una creatura di costituzione 3", "[audit1][recruiter]") {
    const Decklist deck = with_copy(uw_sim_deck(), "Fear of Missing Out");
    Staged st = staged_decks(deck, rakdos_deck(), kUw, {"Recruiter of the Guard"}, {"Plains", "Plains", "Plains"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Recruiter of the Guard"));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    const auto choices = names_of(st.s, ActionType::ChooseCard);
    CHECK_FALSE(choices.contains("Fear of Missing Out"));
    CHECK(choices.contains("Solitude"));  // 3/2: costituzione 2
}

// ===========================================================================
// T-5: secondi test indipendenti, uno per ogni regola presidiata da un solo test.
// ===========================================================================

// rimbalzo_solo_dalla_mano: la copia rilanciata dall'esilio non è lanciata dalla mano, quindi al
// mantenimento successivo non c'è un secondo rimbalzo.
TEST_CASE("Audit 1, T-5: Ephemerate rilanciata con il rimbalzo non rimbalza di nuovo", "[audit1][ephemerate]") {
    Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Guide of Souls", "Arid Mesa"});
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Guide of Souls"));
    pay_all(st);
    resolve(st);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::CastFromExile; });
    act(st, option(kCastIt));
    act(st, target(st.s, "Guide of Souls"));
    resolve(st);
    pass_until(st, [](const GameState& s) {
        return s.pending.kind == DecisionKind::CastFromExile || s.turn.turn_number >= 6;
    });
    CHECK(st.s.pending.kind != DecisionKind::CastFromExile);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Ephemerate"));
}

// attacco_improvviso: Ocelot Pride attacca ed è bloccata (l'altro test: blocca).
TEST_CASE("Audit 1, T-5: Ocelot Pride attacca, bloccata da un 1/1 lo uccide senza subire danno",
          "[audit1][combat][keywords]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Arid Mesa"}, {"Stitcher's Supplier"});
    attack(st, {{"Ocelot Pride", "giocatore"}});
    block_with(st, "Stitcher's Supplier");
    pass_until(st, at_turn(2));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Ocelot Pride"));
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Stitcher's Supplier"));
}

// protezione_danno: il danno di una creatura rossa (l'altro test: nera), con Sanctifier che blocca.
TEST_CASE("Audit 1, T-5: Sanctifier en-Vec blocca Fear of Missing Out e sopravvive", "[audit1][combat][keywords]") {
    Staged st = staged(kRakdos, {}, {}, {"Sanctifier en-Vec", "Arid Mesa"}, {"Fear of Missing Out", "Marsh Flats"});
    attack(st, {{"Fear of Missing Out", "giocatore"}});
    block_with(st, "Sanctifier en-Vec");
    pass_until(st, at_turn(2));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Sanctifier en-Vec"));  // 2/2: senza protezione morirebbe
    CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Fear of Missing Out"));
}

// guide_trigger_accorpati: due token copia entrano insieme, due vite e due energie.
TEST_CASE("Audit 1, T-5: Guide of Souls con un gatto copiato due volte: tre vite e tre energie",
          "[audit1][guide][ocelot]") {
    Staged st = staged(kUw, {}, {},
                       {"Ocelot Pride", "Guide of Souls", "Arid Mesa", "Plains", "Plains", "Plains",
                        "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain"});
    REQUIRE(st.s.players[kUw].city_blessing);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);  // entrato in questo turno
    require_invariants(st.s);
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    resolve(st);
    // Un gatto nuovo (una vita, un'energia), poi due copie insieme (due vite, due energie).
    CHECK(count_tokens(st.s, kUw) == 4);
    CHECK(st.s.players[kUw].energy == 3);
    CHECK(st.s.players[kUw].life == 21 + 3);
}

// planeswalker_uscito: due attaccanti, uno su Teferi che lascia il campo nello step degli attaccanti.
TEST_CASE("Audit 1, T-5: Teferi esce dopo la dichiarazione, il danno sul giocatore resta", "[audit1][teferi][combat]") {
    Staged st = staged(kRakdos, {}, {}, {kTeferi, "Arid Mesa"}, {"Stitcher's Supplier", "Street Wraith", "Marsh Flats"});
    attack(st, {{"Stitcher's Supplier", "Teferi"}, {"Street Wraith", "giocatore"}});
    REQUIRE(st.s.combat.attackers.size() == 2);
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, kTeferi), Zone::Exile, Cause::Test);
    require_invariants(st.s);
    pass_until(st, at_turn(2));
    CHECK(st.s.players[kUw].life == 17);  // solo Street Wraith (3); il Supplier non infligge danno
}

// regola_leggende: due planeswalker leggendari (l'altro test: due creature), mazzo con due Teferi.
TEST_CASE("Audit 1, T-5: regola delle leggende con due Teferi", "[audit1][sba][teferi]") {
    const Decklist deck = with_copy(uw_sim_deck(), kTeferi);
    Staged st = staged_decks(deck, rakdos_deck(), kUw, {}, {kTeferi, kTeferi, "Arid Mesa"});
    REQUIRE(is_decision(st.s, DecisionKind::LegendRule, kUw));
    act(st, legal(st.s).front());
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(std::count_if(st.s.battlefield.begin(), st.s.battlefield.end(),
                        [&](CardId id) { return name_of(st.s, id) == kTeferi; }) == 1);
}

// volare_segnalino: il segnalino volare messo a mano (l'altro test: dal trigger di Guide of Souls).
TEST_CASE("Audit 1, T-5: una creatura con un segnalino volare non si puo' bloccare da terra",
          "[audit1][combat][keywords]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Arid Mesa"}, {"Nethergoyf"});
    st.s.objects[find_on_battlefield(st.s, "Ocelot Pride").card]
        .counters.keywords[static_cast<std::size_t>(KeywordCounter::Flying)] = 1;
    attack(st, {{"Ocelot Pride", "giocatore"}});
    pass_until(st, [](const GameState& s) {
        return s.pending.kind == DecisionKind::DeclareBlockers || s.turn.turn_number >= 2;
    });
    CHECK(st.s.pending.kind != DecisionKind::DeclareBlockers);  // Nethergoyf non ha bloccanti da scegliere
    pass_until(st, at_turn(2));
    CHECK(st.s.players[kRakdos].life == 19);
}

// blackcleave_cliffs: tre altre terre di tipo diverso (fetch, terra tappata, shockland).
TEST_CASE("Audit 1, T-5: Blackcleave Cliffs tappata con una fetch, Raucous Theater e Blood Crypt", "[audit1][lands]") {
    Staged st = staged(kRakdos, {}, {"Blackcleave Cliffs"}, {}, {"Bloodstained Mire", "Raucous Theater", "Blood Crypt"});
    to_main(st, kRakdos);
    act(st, find_action(st.s, ActionType::PlayLand, "Blackcleave Cliffs"));
    CHECK(st.s.objects[find_on_battlefield(st.s, "Blackcleave Cliffs").card].has_flag(ObjectFlag::Tapped));
}

// dmm_sette_carte: il bonus si vede nel danno da combattimento (l'altro test: forza calcolata).
TEST_CASE("Audit 1, T-5: Dark Matter Manipulator con sette carte nel cimitero infligge 3 danni",
          "[audit1][rakdos][dmm]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Arid Mesa"}, Zone::Battlefield);
    put(st.s, kRakdos, {"Dark Matter Manipulator", "Marsh Flats"}, Zone::Battlefield);
    put(st.s, kRakdos,
        {"Swamp", "Swamp", "Lightning Bolt", "Thoughtseize", "Street Wraith", "Mishra's Bauble", "Nethergoyf"},
        Zone::Graveyard);
    start_game(st.s, db_with_tokens(), StartOptions{kRakdos, false}, &st.log);
    require_invariants(st.s);
    attack(st, {{"Dark Matter Manipulator", "giocatore"}});
    pass_until(st, at_turn(2));
    CHECK(st.s.players[kUw].life == 17);
}

// phoenix_bonus: una Phoenix già conferita (l'altro test: il lancio dalla mano).
TEST_CASE("Audit 1, T-5: la Phoenix conferita da' +2/+2 alla creatura", "[audit1][phoenix]") {
    Staged st{new_game(1), {}};
    put(st.s, kRakdos, {"Stitcher's Supplier", "Detective's Phoenix"}, Zone::Battlefield);
    const ObjRef host = find_on_battlefield(st.s, "Stitcher's Supplier");
    CardObject& phoenix = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    phoenix.set_flag(ObjectFlag::Bestowed);
    phoenix.attached_to = host;
    start_game(st.s, db_with_tokens(), StartOptions{kRakdos, false}, &st.log);
    require_invariants(st.s);
    CHECK(power(st.s, db_with_tokens(), host.card) == 3);
    CHECK(toughness(st.s, db_with_tokens(), host.card) == 3);
}

// teferi_una_per_turno: dopo il −3 il +1 non è più disponibile (l'altro test: dopo il +1).
TEST_CASE("Audit 1, T-5: Teferi dopo il -3 non attiva il +1 nello stesso turno", "[audit1][teferi]") {
    Staged st = staged(kUw, {}, {}, {kTeferi, "Arid Mesa"});
    to_main(st, kUw);
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kMinusThree));  // nessun bersaglio possibile
    resolve(st);
    REQUIRE(loyalty(st.s) == 1);
    CHECK_FALSE(can_activate(st.s, kTeferi, kPlusOne));
}

// teferi_costo_negativo e danno_planeswalker_magia: Lightning Bolt porta Teferi a 1, poi niente −3.
TEST_CASE("Audit 1, T-5: Teferi colpito da Lightning Bolt scende a 1 e non puo' attivare il -3",
          "[audit1][teferi][bolt]") {
    Staged st = staged(kRakdos, {}, {"Lightning Bolt"}, {kTeferi, "Arid Mesa"}, {"Blood Crypt"});
    to_main(st, kRakdos);
    act(st, cast_action(st.s, "Lightning Bolt"));
    act(st, target(st.s, kTeferi));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, kTeferi));
    CHECK(loyalty(st.s) == 1);
    to_main(st, kUw);
    CHECK_FALSE(can_activate(st.s, kTeferi, kMinusThree));  // 606.6
    CHECK(can_activate(st.s, kTeferi, kPlusOne));
}

// ephemerate_token: il token non torna, quindi Guide of Souls non vede entrare nessuna creatura.
TEST_CASE("Audit 1, T-5: Ephemerate su un gatto non fa scattare Guide of Souls", "[audit1][ephemerate][guide]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Plains", "Guide of Souls"}, Zone::Battlefield);
    put(st.s, kUw, {"Ephemerate"}, Zone::Hand);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    require_invariants(st.s);
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Cat Token"));
    pay_all(st);
    resolve(st);
    CHECK(count_tokens(st.s, kUw) == 0);
    CHECK(st.s.players[kUw].energy == 0);
    CHECK(st.s.players[kUw].life == 20);
}

// fon_turno_avversario: nel proprio turno, in risposta a una magia avversaria (l'altro test: senza).
TEST_CASE("Audit 1, T-5: Force of Negation non e' gratis nel proprio turno, nemmeno contro Lightning Bolt",
          "[audit1][fon]") {
    Staged st = staged(kUw, {"Force of Negation", "Consign to Memory"}, {"Lightning Bolt"}, {"Arid Mesa"}, {"Blood Crypt"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, cast_action(st.s, "Lightning Bolt"));
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    pay_all(st);
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(st.s.turn.active_player == kUw);
    CHECK_FALSE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
}

// thraben_doppio: tre creature, sei danni uccidono il Troll (l'altro test: due creature, danno segnato).
TEST_CASE("Audit 1, T-5: Thraben Charm con tre creature uccide Troll of Khazad-dum", "[audit1][thraben]") {
    Staged st = staged(kUw, {"Thraben Charm"}, {},
                       {"Plains", "Plains", "Ocelot Pride", "Guide of Souls", "Recruiter of the Guard"},
                       {"Troll of Khazad-dûm"});
    act(st, cast_action(st.s, "Thraben Charm"));
    act(st, option(0));  // modo: danno
    act(st, target(st.s, "Troll of Khazad-dûm"));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
}

// solitude_vita_controllore: Solitude evocata (l'altro test: lanciata pagando il mana).
TEST_CASE("Audit 1, T-5: Solitude evocata esilia Stitcher's Supplier, la vita va a Rakdos", "[audit1][solitude]") {
    Staged st = staged(kUw, {"Solitude", "Ocelot Pride"}, {}, {"Arid Mesa"}, {"Stitcher's Supplier"});
    act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));  // Ocelot Pride, unica altra carta bianca: esiliata
    // Fino all'esilio del Supplier: la pila si svuota anche mentre i due trigger aspettano l'ordine.
    for (int guard = 0; guard < 30 && !(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier") && st.s.stack_items.empty());
         ++guard) {
        if (st.s.pending.kind == DecisionKind::OrderTriggers) {
            act(st, legal(st.s).front());
        } else if (st.s.pending.kind == DecisionKind::ChooseTarget) {
            act(st, target(st.s, "Stitcher's Supplier"));
        } else {
            act(st, pass());
        }
    }
    REQUIRE(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    CHECK(st.s.players[kRakdos].life == 21);
    CHECK(st.s.players[kUw].life == 20);
}

// witch_solo_avversario: con un incantesimo proprio e due bersagli avversari (l'altro test: nessuno).
TEST_CASE("Audit 1, T-5: Witch Enchanter non offre Seam Rip di UW tra i bersagli", "[audit1][witch]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {}, {"Plains", "Plains", "Plains", "Hallowed Fountain", "Seam Rip"},
                       {"Fear of Missing Out", "Mishra's Bauble"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Witch Enchanter"));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(names_of(st.s, ActionType::TargetObject) == std::set<std::string>{"Fear of Missing Out", "Mishra's Bauble"});
}

// starfield_valore: lanciata con il warp (l'altro test: costo pieno); Phelia ha valore di mana 2.
TEST_CASE("Audit 1, T-5: Starfield Shepherd con il warp non trova Phelia", "[audit1][starfield][warp]") {
    Staged st = staged(kUw, {"Starfield Shepherd"}, {}, {"Plains", "Hallowed Fountain", "Arid Mesa"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Starfield Shepherd", CastMethod::Warp));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    const auto choices = names_of(st.s, ActionType::ChooseCard);
    CHECK_FALSE(choices.contains(kPhelia));
    CHECK(choices.contains("Ocelot Pride"));
}

// grimorio_vuoto (e T-4): Quantum Riddler fa pescare due carte da un grimorio di una.
TEST_CASE("Audit 1, T-5: con Quantum Riddler e una carta nel grimorio la pescata del turno fa perdere",
          "[audit1][riddler][sba]") {
    Staged st = staged(kRakdos, {}, {}, {kRiddler}, {"Marsh Flats"});
    std::vector<ObjRef> rest;
    for (std::size_t i = 1; i < st.s.players[kUw].library.size(); ++i) rest.push_back(st.s.ref(st.s.players[kUw].library[i]));
    move_objects(st.s, db_with_tokens(), rest, Zone::Exile, Cause::Test);
    require_invariants(st.s);
    REQUIRE(st.s.players[kUw].library.size() == 1);
    REQUIRE(st.s.players[kUw].hand.empty());
    pass_until(st, [](const GameState& s) {
        return s.pending.kind == DecisionKind::GameOver || (s.turn.turn_number == 2 && s.turn.step == Step::Main1);
    });
    CHECK(st.s.players[kUw].drew_from_empty_library);
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == kRakdos);
}

// shockland_vita_esatta: Witch-Blessed Meadow con esattamente 3 vita (l'altro test: Blood Crypt e 2).
TEST_CASE("Audit 1, T-5: Witch-Blessed Meadow si paga con esattamente 3 vita", "[audit1][lands]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {"Swamp"});
    st.s.players[kUw].life = 3;
    act(st, find_action(st.s, ActionType::PlayLand, "Witch Enchanter", 1));
    REQUIRE(is_decision(st.s, DecisionKind::PayLifeOrTapped, kUw));
    act(st, option(kPayLife));
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == kRakdos);
}
