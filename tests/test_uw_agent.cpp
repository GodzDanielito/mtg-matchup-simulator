// Test dell'agente euristico di UW Blink (docs/agente_uw.md). Ogni regola ha almeno un test su
// una posizione preparata, comprese le posizioni test della specifica e le interazioni della
// sezione 25. UW è il giocatore 0; Rakdos (giocatore 1) è passivo (l'agente del goldfish) o è
// l'agente euristico di Rakdos, quando la posizione richiede che attacchi.

#include "game_setup.hpp"
#include "goldfish_agent.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr const char* kTeferi = "Teferi, Time Raveler";
constexpr const char* kPhelia = "Phelia, Exuberant Shepherd";
constexpr const char* kRiddler = "Quantum Riddler";

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

bool in_zone(const GameState& s, PlayerId owner, Zone z, const char* name) {
    for (std::size_t id = 0; id < kMaxCards; ++id) {
        const CardObject& o = s.objects[id];
        if (o.owner == owner && o.zone == z && db_with_tokens().get(o.def).name == name) return true;
    }
    return false;
}

// Una scelta di UW, con il nome della carta coinvolta e del soggetto al momento della scelta.
struct Choice {
    DecisionKind kind;
    Action action;
    std::string card;
    std::string rule;
    std::uint16_t turn;
    Step step;
    std::string subject;
};

struct Duel {
    Staged st;
    UwAgent uw;
    std::optional<RakdosAgent> rakdos;  // assente: Rakdos non fa nulla (agente del goldfish)
    GoldfishAgent fish;
    std::vector<Choice> choices;

    explicit Duel(Staged staged, bool rakdos_plays = false) : st(std::move(staged)), uw(db_with_tokens(), 7) {
        if (rakdos_plays) rakdos.emplace(db_with_tokens(), 11);
    }

    void step() {
        const auto actions = legal(st.s);
        REQUIRE_FALSE(actions.empty());
        if (st.s.pending.player == kUw) {
            const Action a = uw.choose(st.s, actions);
            const CardId subject = st.s.pending.subject.card;
            choices.push_back(Choice{st.s.pending.kind, a, a.card == kNoCard ? "" : name_of(st.s, a.card),
                                     std::string(uw.last_rule()), st.s.turn.turn_number, st.s.turn.step,
                                     subject == kNoCard ? "" : name_of(st.s, subject)});
            act(st, a);
        } else {
            act(st, rakdos ? rakdos->choose(st.s, actions) : fish.choose(st.s, actions));
        }
    }

    template <typename Pred>
    void run_until(Pred done, int max_steps = 400) {
        for (int i = 0; i < max_steps && !done(st.s) && st.s.pending.kind != DecisionKind::GameOver; ++i) step();
        REQUIRE(done(st.s));
    }

    void run_to_turn(std::uint16_t turn) {
        run_until([turn](const GameState& s) {
            return s.turn.turn_number >= turn || s.pending.kind == DecisionKind::GameOver;
        });
    }

    [[nodiscard]] const Choice* first(ActionType type, std::string_view card) const {
        for (const Choice& c : choices) {
            if (c.action.type == type && c.card == card) return &c;
        }
        return nullptr;
    }
    [[nodiscard]] bool rule_used(std::string_view prefix) const {
        return std::any_of(choices.begin(), choices.end(), [&](const Choice& c) { return c.rule.starts_with(prefix); });
    }
};

// Partita preparata: mani e campo, il cimitero di Rakdos riempito prima dell'avvio.
Staged staged_uw(PlayerId starting, std::initializer_list<const char*> hand0, std::initializer_list<const char*> field0,
                 std::initializer_list<const char*> field1 = {}, std::initializer_list<const char*> graveyard1 = {},
                 std::initializer_list<const char*> hand1 = {}) {
    Staged st{new_game(1), {}};
    put(st.s, kUw, hand0, Zone::Hand);
    put(st.s, kRakdos, hand1, Zone::Hand);
    put(st.s, kUw, field0, Zone::Battlefield);
    put(st.s, kRakdos, field1, Zone::Battlefield);
    for (const char* name : graveyard1) put(st.s, kRakdos, {name}, Zone::Graveyard);
    start_game(st.s, db_with_tokens(), StartOptions{starting, false}, &st.log);
    require_invariants(st.s);
    return st;
}

// Porta in cima al grimorio di UW le carte nominate: la mano iniziale sarà esattamente quella.
void stack_library(GameState& s, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const CardId id = find_cards(s, kUw, Zone::Library, name)[0].card;
        auto& lib = s.players[kUw].library;
        lib.erase_at(lib.find(id));
        lib.push_back(id);
    }
}

// La scelta di mulligan dell'agente di UW sulla mano iniziale indicata.
ActionType mulligan_choice(std::initializer_list<const char*> seven, PlayerId starting = kUw) {
    GameState s = new_game(3);
    stack_library(s, seven);
    start_game(s, db_with_tokens(), StartOptions{starting, true});
    if (is_decision(s, DecisionKind::Mulligan, kRakdos)) apply(s, db_with_tokens(), Action{ActionType::Keep});
    REQUIRE(is_decision(s, DecisionKind::Mulligan, kUw));
    UwAgent agent(db_with_tokens(), 5);
    return agent.choose(s, legal(s)).type;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sezione 2: mulligan
// ---------------------------------------------------------------------------

TEST_CASE("Agente UW: tiene 7 carte con 2-4 terre, una giocata e una risposta", "[uwagent][mulligan]") {
    CHECK(mulligan_choice({"Plains", "Hallowed Fountain", "Guide of Souls", "Solitude", "Ephemerate", "Consign to Memory",
                           kRiddler}) == ActionType::Keep);
}

TEST_CASE("Agente UW: una terra con Solitude ed Ephemerate insieme si tiene", "[uwagent][mulligan]") {
    CHECK(mulligan_choice({"Plains", "Solitude", "Ephemerate", "Guide of Souls", "Consign to Memory", kRiddler,
                           kTeferi}) == ActionType::Keep);
}

TEST_CASE("Agente UW: una terra senza Solitude ed Ephemerate insieme e' mulligan", "[uwagent][mulligan]") {
    CHECK(mulligan_choice({"Plains", "Solitude", "Guide of Souls", "Ocelot Pride", "Consign to Memory", kRiddler,
                           "Thraben Charm"}) == ActionType::Mulligan);
}

TEST_CASE("Agente UW: Witch Enchanter conta come terra nel mulligan", "[uwagent][mulligan]") {
    CHECK(mulligan_choice({"Plains", "Witch Enchanter", "Guide of Souls", "Solitude", "Consign to Memory", kRiddler,
                           "Thraben Charm"}) == ActionType::Keep);
}

TEST_CASE("Agente UW: senza risposte si tiene solo sul play con tre creature da un mana", "[uwagent][mulligan]") {
    const auto hand = {"Plains", "Arid Mesa", "Guide of Souls", "Guide of Souls", "Ocelot Pride", "Consign to Memory",
                       kRiddler};
    CHECK(mulligan_choice(hand, kUw) == ActionType::Keep);
    CHECK(mulligan_choice(hand, kRakdos) == ActionType::Mulligan);
}

// ---------------------------------------------------------------------------
// Posizioni test della specifica (docs/progetto.md, "Posizioni test")
// ---------------------------------------------------------------------------

TEST_CASE("Agente UW, posizione test: March con X=1 su Moonshadow, Solitude resta in mano", "[uwagent][march]") {
    Duel d(staged_uw(kRakdos, {"March of Otherworldly Light", "Solitude"}, {"Plains", "Plains"}, {"Moonshadow"}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Moonshadow"));
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
    const Choice* march = d.first(ActionType::CastSpell, "March of Otherworldly Light");
    REQUIRE(march != nullptr);
    CHECK(march->action.value == static_cast<std::uint8_t>(CastMethod::Normal));
    // Il motore chiede se esiliare carte bianche per ridurre il costo: nessuna, basta il mana.
    CHECK(std::all_of(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.kind != DecisionKind::ExileFromHandForCost || c.action == option(kStopExiling);
    }));
}

TEST_CASE("Agente UW, posizione test: Sanctifier en-Vec subito se l'avversario ha mostrato il nero o il rosso",
          "[uwagent][sanctifier]") {
    SECTION("Rakdos ha una Swamp in campo: Sanctifier prima di Guide of Souls") {
        Duel d(staged_uw(kUw, {"Sanctifier en-Vec", "Guide of Souls"}, {"Plains", "Plains"}, {"Swamp"}));
        d.run_until([](const GameState& s) { return s.turn.step > Step::Main1 || s.turn.turn_number > 1; });
        CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Sanctifier en-Vec"));
    }
    SECTION("nulla di nero o rosso in vista: prima Guide of Souls") {
        Duel d(staged_uw(kUw, {"Sanctifier en-Vec", "Guide of Souls"}, {"Plains", "Plains"}));
        d.run_until([](const GameState& s) { return s.turn.step > Step::Main1 || s.turn.turn_number > 1; });
        CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Guide of Souls"));
    }
}

TEST_CASE("Agente UW, posizione test: Thraben Charm esilia il cimitero a un tipo dal delirium", "[uwagent][thraben]") {
    Duel d(staged_uw(kRakdos, {"Thraben Charm"}, {"Plains", "Plains"}, {"Nethergoyf"},
                     {"Lightning Bolt", "Swamp", "Stitcher's Supplier"}));
    d.run_to_turn(2);
    CHECK(d.st.s.players[kRakdos].graveyard.empty());
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Lightning Bolt"));
}

TEST_CASE("Agente UW, posizione test: Teferi prima di attaccare con il rosso avversario aperto", "[uwagent][teferi]") {
    Duel d(staged_uw(kUw, {kTeferi}, {"Hallowed Fountain", "Plains", "Plains", "Guide of Souls"}, {"Blood Crypt"}));
    d.run_to_turn(2);
    const Choice* teferi = d.first(ActionType::CastSpell, kTeferi);
    REQUIRE(teferi != nullptr);
    CHECK(teferi->step == Step::Main1);
}

TEST_CASE("Agente UW, posizione test: Solitude esilia Nethergoyf con la Phoenix conferita", "[uwagent][solitude]") {
    // Nethergoyf 2/3 (istantaneo e terra) con la Phoenix: 4/5 volante con rapidità, attacca.
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Solitude", "Witch Enchanter", "Ocelot Pride"}, Zone::Hand);
    put(st.s, kUw, {"Plains", "Plains", "Plains", "Hallowed Fountain"}, Zone::Battlefield);
    put(st.s, kRakdos, {"Nethergoyf", "Detective's Phoenix"}, Zone::Battlefield);
    put(st.s, kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    const ObjRef host = find_on_battlefield(st.s, "Nethergoyf");
    CardObject& phoenix = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    phoenix.set_flag(ObjectFlag::Bestowed);
    phoenix.attached_to = host;
    start_game(st.s, db_with_tokens(), StartOptions{kRakdos, false}, &st.log);
    require_invariants(st.s);
    Duel d(std::move(st), true);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Battlefield, "Detective's Phoenix"));
    // Witch Enchanter, con quattro terre in campo, è la carta bianca da esiliare (D-1).
    CHECK(in_zone(d.st.s, kUw, Zone::Exile, "Witch Enchanter"));
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Ocelot Pride"));
}

// ---------------------------------------------------------------------------
// Interazioni della sezione 25
// ---------------------------------------------------------------------------

TEST_CASE("Agente UW: Solitude evocata ed Ephemerate in risposta al sacrificio, due esili e Solitude resta",
          "[uwagent][solitude][ephemerate]") {
    Duel d(staged_uw(kRakdos, {"Solitude", "Ephemerate", "Ocelot Pride"}, {"Plains"}, {"Nethergoyf", "Dark Matter Manipulator"},
                     {"Lightning Bolt", "Swamp", "Mishra's Bauble"}),
           true);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Dark Matter Manipulator"));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Solitude"));
    CHECK(in_zone(d.st.s, kUw, Zone::Exile, "Ephemerate"));  // rimbalzo
}

TEST_CASE("Agente UW: Quantum Riddler con il warp salvato da Ephemerate resta in campo", "[uwagent][warp][ephemerate]") {
    Duel d(staged_uw(kUw, {kRiddler, "Ephemerate"}, {"Hallowed Fountain", "Plains", "Plains"}));
    d.st.s.turn.turn_number = 5;  // terzo turno di UW: il warp come ricarica (D-8)
    d.run_until([](const GameState& s) { return s.turn.turn_number >= 6; });
    REQUIRE(d.first(ActionType::CastSpell, kRiddler) != nullptr);
    CHECK(d.first(ActionType::CastSpell, kRiddler)->action.value == static_cast<std::uint8_t>(CastMethod::Warp));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, kRiddler));
}

TEST_CASE("Agente UW: Starfield Shepherd con il warp salvato da Phelia che attacca", "[uwagent][warp][phelia]") {
    Duel d(staged_uw(kUw, {"Starfield Shepherd"}, {kPhelia, "Plains", "Plains"}));
    d.run_to_turn(2);
    REQUIRE(d.first(ActionType::CastSpell, "Starfield Shepherd") != nullptr);
    CHECK(d.first(ActionType::CastSpell, "Starfield Shepherd")->action.value ==
          static_cast<std::uint8_t>(CastMethod::Warp));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));
    CHECK(d.st.s.objects[find_on_battlefield(d.st.s, kPhelia).card].counters.plus_one == 1);
}

TEST_CASE("Agente UW: Thraben Charm in risposta al trigger di FOMO, niente combattimento aggiuntivo",
          "[uwagent][thraben][fomo]") {
    Duel d(staged_uw(kRakdos, {"Thraben Charm"}, {"Plains", "Plains"}, {"Fear of Missing Out"},
                     {"Lightning Bolt", "Swamp", "Stitcher's Supplier", "Mishra's Bauble"}),
           true);
    d.run_to_turn(2);
    CHECK(d.st.s.players[kRakdos].graveyard.empty());
    CHECK(d.st.s.players[kUw].life == 18);  // un solo attacco di FOMO (2)
}

TEST_CASE("Agente UW: Consign to Memory sul trigger di FOMO con il mana disponibile", "[uwagent][consign][fomo]") {
    Duel d(staged_uw(kRakdos, {"Consign to Memory"}, {"Hallowed Fountain"}, {"Fear of Missing Out"},
                     {"Lightning Bolt", "Swamp", "Stitcher's Supplier", "Mishra's Bauble"}),
           true);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Consign to Memory"));
    CHECK(d.st.s.players[kUw].life == 18);
}

// ---------------------------------------------------------------------------
// Regole decise con Daniele (sezione 26)
// ---------------------------------------------------------------------------

TEST_CASE("Agente UW: mai Quantum Riddler con il warp al secondo turno", "[uwagent][warp]") {
    Duel d(staged_uw(kUw, {kRiddler}, {"Hallowed Fountain", "Plains"}));
    d.st.s.turn.turn_number = 3;  // secondo turno di UW
    d.run_until([](const GameState& s) { return s.turn.turn_number >= 4; });
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, kRiddler));
}

TEST_CASE("Agente UW: Quantum Riddler con il warp come ricarica, a mano vuota pesca due carte", "[uwagent][warp]") {
    Duel d(staged_uw(kUw, {kRiddler}, {"Hallowed Fountain", "Plains"}));
    d.st.s.turn.turn_number = 5;
    d.run_until([](const GameState& s) { return s.turn.turn_number >= 6; });
    const Choice* riddler = d.first(ActionType::CastSpell, kRiddler);
    REQUIRE(riddler != nullptr);
    CHECK(riddler->action.value == static_cast<std::uint8_t>(CastMethod::Warp));
    CHECK(riddler->step == Step::Main2);  // senza un piano per tenerlo: dopo il combattimento
    CHECK(riddler->rule == "Quantum Riddler: warp come ricarica");
}

TEST_CASE("Agente UW: l'energia di Guide of Souls sempre, sulla creatura che arriva a costituzione 4",
          "[uwagent][guide]") {
    Duel d(staged_uw(kUw, {}, {"Guide of Souls", "Ocelot Pride"}));
    d.st.s.players[kUw].energy = 3;
    d.run_to_turn(2);
    CHECK(d.st.s.objects[find_on_battlefield(d.st.s, "Guide of Souls").card].counters.plus_one == 2);
    CHECK(d.st.s.players[kUw].energy == 1);  // tre spese; una nuova dal gatto di Ocelot alla sottofase finale
}

TEST_CASE("Agente UW: i token gatto restano a bloccare", "[uwagent][combat]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Ocelot Pride"}, Zone::Battlefield);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    require_invariants(st.s);
    Duel d(std::move(st));
    d.run_to_turn(2);
    bool cat_stayed = false;
    bool ocelot_attacked = false;
    for (const Choice& c : d.choices) {
        if (c.kind != DecisionKind::DeclareAttacker) continue;
        if (c.subject == "Cat Token") cat_stayed = c.action.type == ActionType::ChooseOption;
        if (c.subject == "Ocelot Pride") ocelot_attacked = c.action.type == ActionType::TargetPlayer;
    }
    CHECK(cat_stayed);
    CHECK(ocelot_attacked);
}

TEST_CASE("Agente UW: Hallowed Fountain tappata se nessuna giocata del turno ne ha bisogno", "[uwagent][lands]") {
    // Con una terra sola nulla della mano si lancia nel turno: Consign to Memory non si tiene apposta.
    Duel d(staged_uw(kUw, {"Hallowed Fountain", "Consign to Memory", kRiddler, "Recruiter of the Guard"}, {}));
    d.run_to_turn(2);
    CHECK(d.st.s.players[kUw].life == 20);
    CHECK(d.rule_used("terra che costa vita: tappata"));
}

TEST_CASE("Agente UW: Hallowed Fountain pagata per una giocata del turno", "[uwagent][lands]") {
    Duel d(staged_uw(kUw, {"Hallowed Fountain", "Guide of Souls"}, {}));
    d.run_to_turn(2);
    CHECK(d.st.s.players[kUw].life == 18);
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Guide of Souls"));
}

TEST_CASE("Agente UW: in un attacco letale attaccano anche i gatti", "[uwagent][combat]") {
    // Rakdos a 3 vita con un bloccante: Ocelot Pride e tre gatti (4 di forza) passano comunque per 3.
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Ocelot Pride"}, Zone::Battlefield);
    for (int i = 0; i < 3; ++i) create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    put(st.s, kRakdos, {"Stitcher's Supplier"}, Zone::Battlefield);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    st.s.players[kRakdos].life = 3;
    require_invariants(st.s);
    Duel d(std::move(st));
    d.run_until([](const GameState& s) { return s.pending.kind == DecisionKind::GameOver; });
    CHECK(d.st.s.turn.winner == kUw);
    CHECK(d.rule_used("attacco: attacco letale"));
}

TEST_CASE("Agente UW: niente rimozione su Stitcher's Supplier", "[uwagent][seam_rip]") {
    Duel d(staged_uw(kUw, {"Seam Rip", "Solitude", "Ocelot Pride"}, {"Plains"}, {"Stitcher's Supplier"}));
    d.run_to_turn(3);
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Seam Rip"));
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Battlefield, "Stitcher's Supplier"));
}

TEST_CASE("Agente UW: Teferi -3 su Moonshadow da far ripartire", "[uwagent][teferi]") {
    Duel d(staged_uw(kUw, {}, {kTeferi}, {"Moonshadow"}));
    d.st.s.objects[find_on_battlefield(d.st.s, "Moonshadow").card].counters.minus_one = 3;
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Moonshadow"));
}

TEST_CASE("Agente UW: Force of Negation contrasta Flare of Malice esiliando Consign to Memory", "[uwagent][fon]") {
    Duel d(staged_uw(kRakdos, {"Force of Negation", "Consign to Memory", kRiddler}, {"Solitude"},
                     {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"}, {}, {"Flare of Malice"}));
    d.run_until([](const GameState& s) {
        return s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority && s.pending.player == kRakdos;
    });
    act(d.st, find_action(d.st.s, ActionType::CastSpell, "Flare of Malice"));
    while (d.st.s.pending.kind == DecisionKind::PayMana) act(d.st, option(0));
    d.run_until([](const GameState& s) { return s.stack_items.empty(); });
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Flare of Malice"));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Solitude"));
    CHECK(in_zone(d.st.s, kUw, Zone::Exile, "Consign to Memory"));
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, kRiddler));
}

TEST_CASE("Agente UW: Consign to Memory sul sacrificio di Solitude evocata", "[uwagent][consign][solitude]") {
    Duel d(staged_uw(kRakdos, {"Solitude", "Consign to Memory", "Ocelot Pride"},
                     {"Hallowed Fountain", "Plains", "Plains", "Plains"}, {"Nethergoyf"}, {"Lightning Bolt", "Swamp"}),
           true);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, "Solitude"));
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Consign to Memory"));
}

TEST_CASE("Agente UW: rimbalzo rifiutato senza un'entrata rilevante", "[uwagent][ephemerate]") {
    Duel d(staged_uw(kUw, {"Ephemerate"}, {"Plains", "Ocelot Pride", "Arid Mesa"}));
    act(d.st, find_action(d.st.s, ActionType::CastSpell, "Ephemerate"));
    for (const Action& a : legal(d.st.s)) {
        if (a.type == ActionType::TargetObject && name_of(d.st.s, a.card) == "Ocelot Pride") {
            act(d.st, a);
            break;
        }
    }
    while (d.st.s.pending.kind == DecisionKind::PayMana) act(d.st, option(0));
    d.run_until([](const GameState& s) { return s.pending.kind == DecisionKind::CastFromExile; });
    d.step();
    CHECK(d.choices.back().action.value == kDeclineCast);
    CHECK(in_zone(d.st.s, kUw, Zone::Exile, "Ephemerate"));
}

TEST_CASE("Agente UW: regola delle leggende, si tiene la Phelia con il segnalino", "[uwagent][legend]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {kPhelia}, Zone::Battlefield);
    st.s.objects[find_on_battlefield(st.s, kPhelia).card].counters.plus_one = 1;
    const CardId with_counter = find_on_battlefield(st.s, kPhelia).card;
    put(st.s, kUw, {kPhelia, "Arid Mesa"}, Zone::Battlefield);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    Duel d(std::move(st));
    REQUIRE(is_decision(d.st.s, DecisionKind::LegendRule, kUw));
    d.step();
    CHECK(d.st.s.objects[with_counter].zone == Zone::Battlefield);
}

TEST_CASE("Agente UW: Flare of Malice, si sacrifica la creatura meno preziosa tra le pari", "[uwagent][flare]") {
    Duel d(staged_uw(kRakdos, {}, {"Solitude", kRiddler}, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"}, {},
                     {"Flare of Malice"}));
    d.run_until([](const GameState& s) {
        return s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority && s.pending.player == kRakdos;
    });
    act(d.st, find_action(d.st.s, ActionType::CastSpell, "Flare of Malice"));
    while (d.st.s.pending.kind == DecisionKind::PayMana) act(d.st, option(0));
    d.run_until([](const GameState& s) { return s.stack_items.empty() && s.pending.kind == DecisionKind::Priority; });
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Solitude"));
    CHECK(in_zone(d.st.s, kUw, Zone::Battlefield, kRiddler));
}

// ---------------------------------------------------------------------------
// Sezione 1: nessuna decisione "predefinito"
// ---------------------------------------------------------------------------

TEST_CASE("Agente UW: nessuna decisione predefinito contro l'agente di Rakdos", "[uwagent][default]") {
    const CardDatabase& db = db_with_tokens();
    std::uint64_t defaults = 0;
    std::uint64_t decisions = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        GameState s = create_game(db, uw_sim_deck(), rakdos_deck(), seed);
        start_game(s, db, StartOptions{static_cast<PlayerId>(seed % 2), true});
        UwAgent uw(db, seed);
        RakdosAgent rakdos(db, seed);
        for (int guard = 0; guard < 5000 && s.pending.kind != DecisionKind::GameOver; ++guard) {
            const auto actions = legal_actions(s, db);
            if (s.pending.player == kUw) {
                const Action a = uw.choose(s, actions);
                ++decisions;
                if (uw.last_rule() == UwAgent::kDefaultRule) {
                    ++defaults;
                    UNSCOPED_INFO("seed " << seed << ", decisione " << static_cast<int>(s.pending.kind));
                }
                apply(s, db, a);
            } else {
                apply(s, db, rakdos.choose(s, actions));
            }
        }
        CHECK(s.pending.kind == DecisionKind::GameOver);
    }
    CHECK(decisions > 1000);
    CHECK(defaults == 0);
}
