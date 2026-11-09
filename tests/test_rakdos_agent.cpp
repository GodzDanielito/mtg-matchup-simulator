// Test dell'agente euristico di Rakdos (docs/agente_rakdos.md). Ogni regola ha
// almeno un test su una posizione preparata. UW (giocatore 0) è passivo: decide
// l'agente del goldfish, che passa sempre.

#include "goldfish_agent.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kBestow = static_cast<std::uint8_t>(CastMethod::Bestow);
constexpr std::uint8_t kEscape = static_cast<std::uint8_t>(CastMethod::Escape);
constexpr std::uint8_t kSacrificeAlt = static_cast<std::uint8_t>(CastMethod::SacrificeAlternative);

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Una scelta dell'agente, con il nome della carta coinvolta al momento della scelta.
struct Choice {
    DecisionKind kind;
    Action action;
    std::string card;
    std::string rule;
    std::uint16_t turn;
    Step step;
};

struct Driver {
    Staged st;
    RakdosAgent agent;
    GoldfishAgent uw;
    std::vector<Choice> choices;

    explicit Driver(Staged staged, RakdosAgentOptions options = {})
        : st(std::move(staged)), agent(db_with_tokens(), 7, options) {}

    void step() {
        const auto actions = legal(st.s);
        REQUIRE_FALSE(actions.empty());
        if (st.s.pending.player == kRakdos) {
            const Action a = agent.choose(st.s, actions);
            choices.push_back(Choice{st.s.pending.kind, a, a.card == kNoCard ? "" : name_of(st.s, a.card),
                                     std::string(agent.last_rule()), st.s.turn.turn_number, st.s.turn.step});
            act(st, a);
        } else {
            act(st, uw.choose(st.s, actions));
        }
    }

    template <typename Pred>
    void run_until(Pred done, int max_steps = 300) {
        for (int i = 0; i < max_steps && !done(st.s) && st.s.pending.kind != DecisionKind::GameOver; ++i) step();
        REQUIRE(done(st.s));
    }

    // Gioca fino all'inizio del turno indicato (o alla fine della partita).
    void run_to_turn(std::uint16_t turn) {
        run_until([turn](const GameState& s) {
            return s.turn.turn_number >= turn || s.pending.kind == DecisionKind::GameOver;
        });
    }

    [[nodiscard]] std::vector<std::string> cast_names() const {
        std::vector<std::string> out;
        for (const Choice& c : choices) {
            if (c.kind == DecisionKind::Priority && c.action.type == ActionType::CastSpell) out.push_back(c.card);
        }
        return out;
    }

    [[nodiscard]] bool chose(DecisionKind kind, ActionType type, std::string_view card) const {
        return std::any_of(choices.begin(), choices.end(), [&](const Choice& c) {
            return c.kind == kind && c.action.type == type && c.card == card;
        });
    }
};

// Partita preparata con il cimitero di Rakdos riempito PRIMA dell'avvio.
Staged staged_rakdos(std::initializer_list<const char*> hand1, std::initializer_list<const char*> field1,
                     std::initializer_list<const char*> graveyard1 = {}, std::initializer_list<const char*> hand0 = {"Plains"},
                     std::initializer_list<const char*> field0 = {}, PlayerId starting = kRakdos) {
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

// Porta in cima al grimorio di `p` le carte nominate (l'ultima nominata in cima): la mano
// iniziale sarà esattamente quella (sette carte).
void stack_library(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const CardId id = find_cards(s, p, Zone::Library, name)[0].card;
        auto& lib = s.players[p].library;
        lib.erase_at(lib.find(id));
        lib.push_back(id);
    }
}

// Mano iniziale preparata: Rakdos vede esattamente queste sette carte, dopo `mulligans` mulligan.
GameState opening(std::initializer_list<const char*> seven, std::uint8_t mulligans = 0) {
    GameState s = new_game(3);
    stack_library(s, kRakdos, seven);
    s.players[kRakdos].mulligans = mulligans;
    start_game(s, db_with_tokens(), StartOptions{kRakdos, true});
    REQUIRE(is_decision(s, DecisionKind::Mulligan, kRakdos));
    return s;
}

ActionType mulligan_choice(const GameState& s) {
    RakdosAgent agent(db_with_tokens(), 1);
    return agent.choose(s, legal(s)).type;
}

bool in_zone(const GameState& s, PlayerId p, Zone z, std::string_view name) {
    const auto& ps = s.players[p];
    const PlayerZone& list = z == Zone::Hand ? ps.hand : z == Zone::Graveyard ? ps.graveyard : ps.library;
    return std::any_of(list.begin(), list.end(), [&](CardId id) { return name_of(s, id) == name; });
}

bool on_battlefield(const GameState& s, std::string_view name) {
    return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) { return name_of(s, id) == name; });
}

}  // namespace

// ---------------------------------------------------------------------------
// Sezione 2: mulligan
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: mulligan con una sola terra, anche con Wraith, Bauble e Troll", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Street Wraith", "Mishra's Bauble", "Troll of Khazad-dûm", "Nethergoyf",
                                 "Stitcher's Supplier", "Lightning Bolt"});
    CHECK(mulligan_choice(s) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: tiene 7 carte con due terre e una creatura economica lanciabile", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Marsh Flats", "Nethergoyf", "Lightning Bolt", "Thoughtseize",
                                 "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice(s) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: mulligan se la sola creatura economica non e' lanciabile", "[agent][mulligan]") {
    // Due Swamp e Fear of Missing Out ({1}{R}): manca il rosso.
    const GameState s = opening({"Swamp", "Swamp", "Fear of Missing Out", "Lightning Bolt", "Thoughtseize",
                                 "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice(s) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: mulligan con cinque terre su sette", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Swamp", "Blood Crypt", "Marsh Flats", "Polluted Delta", "Nethergoyf",
                                 "Lightning Bolt"});
    CHECK(mulligan_choice(s) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: con 6 carte basta avere da 2 a 4 terre", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Blood Crypt", "Marsh Flats", "Lightning Bolt", "Thoughtseize",
                                 "Flare of Malice", "Detective's Phoenix"},
                                1);
    CHECK(mulligan_choice(s) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: con 5 carte tiene sempre", "[agent][mulligan]") {
    const GameState s = opening({"Lightning Bolt", "Thoughtseize", "Flare of Malice", "Detective's Phoenix",
                                 "Street Wraith", "Mishra's Bauble", "Nethergoyf"},
                                2);
    CHECK(mulligan_choice(s) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: in fondo prima le terre oltre la terza, Swamp per prima", "[agent][mulligan]") {
    GameState s = opening({"Swamp", "Swamp", "Blood Crypt", "Marsh Flats", "Blackcleave Cliffs", "Nethergoyf",
                           "Lightning Bolt"},
                          1);
    RakdosAgent agent(db_with_tokens(), 1);
    apply(s, db_with_tokens(), agent.choose(s, legal(s)));  // tiene: 4 terre su 6
    for (int guard = 0; guard < 4 && !is_decision(s, DecisionKind::BottomCards, kRakdos); ++guard) {
        apply(s, db_with_tokens(), legal(s).front());  // UW tiene
    }
    REQUIRE(is_decision(s, DecisionKind::BottomCards, kRakdos));
    const Action bottom = agent.choose(s, legal(s));
    CHECK(name_of(s, bottom.card) == "Swamp");
}

TEST_CASE("Agente Rakdos: in fondo la carta di valore piu' basso", "[agent][mulligan]") {
    GameState s = opening({"Swamp", "Blood Crypt", "Marsh Flats", "Troll of Khazad-dûm", "Fear of Missing Out",
                           "Nethergoyf", "Mishra's Bauble"},
                          1);
    RakdosAgent agent(db_with_tokens(), 1);
    apply(s, db_with_tokens(), agent.choose(s, legal(s)));
    for (int guard = 0; guard < 4 && !is_decision(s, DecisionKind::BottomCards, kRakdos); ++guard) {
        apply(s, db_with_tokens(), legal(s).front());
    }
    REQUIRE(is_decision(s, DecisionKind::BottomCards, kRakdos));
    CHECK(name_of(s, agent.choose(s, legal(s)).card) == "Troll of Khazad-dûm");
}

// ---------------------------------------------------------------------------
// Sezione 3: terre, fetch, shockland
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: gioca la fetch, la rompe e cerca la Swamp per la creatura nera", "[agent][lands]") {
    Driver d(staged_rakdos({"Marsh Flats", "Swamp", "Nethergoyf"}, {}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Marsh Flats"));
    CHECK(on_battlefield(d.st.s, "Swamp"));
    CHECK(on_battlefield(d.st.s, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Swamp"));  // la Swamp in mano resta per dopo
    CHECK(d.st.s.players[kRakdos].life == 19);
}

TEST_CASE("Agente Rakdos: serve il rosso subito, Blood Crypt pagando 2 vita", "[agent][lands]") {
    Driver d(staged_rakdos({"Marsh Flats", "Fear of Missing Out"}, {"Swamp"}));
    d.run_to_turn(2);
    CHECK(on_battlefield(d.st.s, "Blood Crypt"));
    CHECK(on_battlefield(d.st.s, "Fear of Missing Out"));
    CHECK(d.st.s.players[kRakdos].life == 17);  // fetch 1, Blood Crypt 2
}

TEST_CASE("Agente Rakdos: mana non necessario, la fetch cerca Raucous Theater", "[agent][lands]") {
    Driver d(staged_rakdos({"Marsh Flats"}, {"Swamp"}));
    d.run_to_turn(2);
    CHECK(on_battlefield(d.st.s, "Raucous Theater"));
    CHECK(d.st.s.players[kRakdos].life == 19);
}

TEST_CASE("Agente Rakdos: soglia di vita, niente ciclo di Wraith e niente fetch a 5 vita", "[agent][lands]") {
    Driver d(staged_rakdos({"Street Wraith", "Marsh Flats"}, {"Swamp"}));
    d.st.s.players[kRakdos].life = 5;
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Street Wraith"));
    CHECK(on_battlefield(d.st.s, "Marsh Flats"));  // giocata ma non rotta
    CHECK(d.st.s.players[kRakdos].life == 5);
}

TEST_CASE("Agente Rakdos: niente ciclo del Troll se nel grimorio non restano terre da cercare", "[agent][lands]") {
    // Tutte le terre con il tipo Swamp sono già fuori dal grimorio: il ciclo non troverebbe nulla.
    Driver d(staged_rakdos({"Troll of Khazad-dûm"},
                           {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Blood Crypt"}, {"Raucous Theater"}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Troll of Khazad-dûm"));
    CHECK_FALSE(d.agent.rule_counts().contains(std::string(RakdosAgent::kDefaultRule)));
}

// ---------------------------------------------------------------------------
// Sezione 4: ordine delle giocate
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: ciclo di Wraith prima della terra; Bauble lanciato e sacrificato subito", "[agent][order]") {
    Driver d(staged_rakdos({"Street Wraith", "Mishra's Bauble", "Swamp"}, {}));
    d.run_to_turn(2);
    // Solo le giocate (niente "passa"); la carta pescata con il ciclo può aggiungerne altre.
    std::vector<Choice> plays;
    for (const Choice& c : d.choices) {
        if (c.kind == DecisionKind::Priority && c.action.type != ActionType::Pass) plays.push_back(c);
        if (c.kind == DecisionKind::ChooseTarget && c.card.empty()) {
            CHECK(c.action == Action{ActionType::TargetPlayer, kNoCard, kRakdos});  // Bauble: se stesso
        }
    }
    auto first = [&](ActionType type, std::string_view card) {
        const auto it = std::find_if(plays.begin(), plays.end(),
                                     [&](const Choice& c) { return c.action.type == type && c.card == card; });
        REQUIRE(it != plays.end());
        return static_cast<std::size_t>(it - plays.begin());
    };
    const std::size_t cycle = first(ActionType::ActivateAbility, "Street Wraith");
    const std::size_t bauble = first(ActionType::CastSpell, "Mishra's Bauble");
    const auto land = std::find_if(plays.begin(), plays.end(),
                                   [](const Choice& c) { return c.action.type == ActionType::PlayLand; });
    REQUIRE(land != plays.end());
    CHECK(cycle == 0);
    CHECK(cycle < bauble);
    CHECK(bauble < static_cast<std::size_t>(land - plays.begin()));
    REQUIRE(bauble + 1 < plays.size());
    CHECK(plays[bauble + 1].action.type == ActionType::ActivateAbility);  // sacrificato subito
    CHECK(plays[bauble + 1].card == "Mishra's Bauble");
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Mishra's Bauble"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Street Wraith"));
}

TEST_CASE("Agente Rakdos: al turno 1 la creatura da un mana prima di Thoughtseize", "[agent][order]") {
    Driver d(staged_rakdos({"Swamp", "Thoughtseize", "Stitcher's Supplier"}, {}));
    d.run_to_turn(2);
    CHECK(on_battlefield(d.st.s, "Stitcher's Supplier"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
}

TEST_CASE("Agente Rakdos: dal secondo turno Thoughtseize prima delle creature", "[agent][order]") {
    Driver d(staged_rakdos({"Thoughtseize", "Stitcher's Supplier"}, {"Swamp", "Blood Crypt"}, {},
                           {"Plains", "Solitude"}));
    d.st.s.turn.turn_number = 3;  // secondo turno di Rakdos
    d.run_to_turn(4);
    const auto casts = d.cast_names();
    REQUIRE(casts.size() == 2);
    CHECK(casts[0] == "Thoughtseize");
    CHECK(casts[1] == "Stitcher's Supplier");
}

TEST_CASE("Agente Rakdos: creature nell'ordine Supplier, Manipulator, Moonshadow, Nethergoyf", "[agent][order]") {
    Driver d(staged_rakdos({"Nethergoyf", "Moonshadow", "Dark Matter Manipulator", "Stitcher's Supplier"},
                           {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"}));
    d.st.s.turn.turn_number = 3;
    d.run_to_turn(4);
    CHECK(d.cast_names() ==
          std::vector<std::string>{"Stitcher's Supplier", "Dark Matter Manipulator", "Moonshadow", "Nethergoyf"});
}

// ---------------------------------------------------------------------------
// Sezione 6: Thoughtseize
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: Thoughtseize prende Sanctifier prima di Solitude e Ocelot", "[agent][thoughtseize]") {
    Driver d(staged_rakdos({"Thoughtseize"}, {"Swamp"}, {}, {"Ocelot Pride", "Solitude", "Sanctifier en-Vec", "Plains"}));
    d.st.s.turn.turn_number = 3;
    d.run_to_turn(4);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Sanctifier en-Vec"));
    CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
    CHECK(d.chose(DecisionKind::ChooseTarget, ActionType::TargetPlayer, ""));
}

TEST_CASE("Agente Rakdos: Thoughtseize anche se non c'e' nulla da prendere", "[agent][thoughtseize]") {
    Driver d(staged_rakdos({"Thoughtseize"}, {"Swamp"}, {}, {"Plains", "Plains"}));
    d.st.s.turn.turn_number = 3;
    d.run_to_turn(4);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Thoughtseize"));
    CHECK(d.st.s.players[kRakdos].life == 18);
}

// ---------------------------------------------------------------------------
// Sezione 7: Lightning Bolt
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: Bolt su Ocelot Pride a vista", "[agent][bolt]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {"Ocelot Pride"}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Ocelot Pride"));
}

TEST_CASE("Agente Rakdos: niente Bolt in faccia sopra i 6 punti vita", "[agent][bolt]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}));
    d.run_to_turn(3);  // il turno di Rakdos e quello dell'avversario, con la sua sottofase finale
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Lightning Bolt"));
    CHECK(d.st.s.players[kUw].life == 20);
}

TEST_CASE("Agente Rakdos: Bolt in faccia con l'avversario a 6 punti vita", "[agent][bolt]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}));
    d.st.s.players[kUw].life = 6;
    d.run_to_turn(3);
    CHECK(d.st.s.players[kUw].life == 3);
}

TEST_CASE("Agente Rakdos: variante Bolt in faccia appena possibile", "[agent][bolt]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}), RakdosAgentOptions{true});
    d.run_to_turn(2);
    CHECK(d.st.s.players[kUw].life == 17);
}

TEST_CASE("Agente Rakdos: niente Bolt su Quantum Riddler (non basta a ucciderlo)", "[agent][bolt]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {"Quantum Riddler"}));
    d.run_to_turn(3);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Lightning Bolt"));
}

// ---------------------------------------------------------------------------
// Sezione 11: Flare of Malice
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: Flare non si lancia se l'avversario non ha creature", "[agent][flare]") {
    Driver d(staged_rakdos({"Flare of Malice"}, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Stitcher's Supplier"}));
    d.run_to_turn(3);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Flare of Malice"));
    CHECK(on_battlefield(d.st.s, "Stitcher's Supplier"));
}

TEST_CASE("Agente Rakdos: Flare nella sottofase finale avversaria sacrificando Stitcher's Supplier", "[agent][flare]") {
    Driver d(staged_rakdos({"Flare of Malice"}, {"Stitcher's Supplier", "Dark Matter Manipulator"}, {}, {"Plains"},
                           {"Solitude"}, kUw));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Solitude"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Stitcher's Supplier"));
    CHECK(on_battlefield(d.st.s, "Dark Matter Manipulator"));
    const auto flare = std::find_if(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.action.type == ActionType::CastSpell && c.card == "Flare of Malice";
    });
    REQUIRE(flare != d.choices.end());
    CHECK(flare->action.value == kSacrificeAlt);
    CHECK(flare->step == Step::End);
}

// ---------------------------------------------------------------------------
// Sezioni 10 e 13: la carta lanciata dal cimitero esce anche lei
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: niente conferimento dal cimitero se l'uscita della Phoenix costa il delirium",
          "[agent][phoenix]") {
    // Cimitero: Phoenix (creatura, incantesimo), istantanei, stregonerie, una terra: 5 tipi.
    // Le prove migliori lasciano Swamp, Thoughtseize e Lightning Bolt; uscendo, la Phoenix
    // porta via creatura e incantesimo: restano 3 tipi, il delirium si perde.
    Driver d(staged_rakdos({}, {"Blood Crypt", "Stitcher's Supplier"},
                           {"Detective's Phoenix", "Lightning Bolt", "Thoughtseize", "Swamp", "Flare of Malice",
                            "Thoughtseize", "Lightning Bolt"}));
    REQUIRE(d.st.s.players[kRakdos].gy.delirium());
    d.run_to_turn(2);
    CHECK_FALSE(d.chose(DecisionKind::Priority, ActionType::CastSpell, "Detective's Phoenix"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Detective's Phoenix"));
}

TEST_CASE("Agente Rakdos: conferimento dal cimitero se il delirium resta anche senza la Phoenix", "[agent][phoenix]") {
    Driver d(staged_rakdos({}, {"Blood Crypt", "Stitcher's Supplier"},
                           {"Detective's Phoenix", "Lightning Bolt", "Thoughtseize", "Swamp", "Flare of Malice",
                            "Thoughtseize", "Lightning Bolt", "Street Wraith", "Fear of Missing Out"}));
    d.run_to_turn(2);
    REQUIRE(d.chose(DecisionKind::Priority, ActionType::CastSpell, "Detective's Phoenix"));
    CHECK(d.st.s.players[kRakdos].gy.delirium());
    const ObjRef phoenix = find_on_battlefield(d.st.s, "Detective's Phoenix");
    CHECK(d.st.s.objects[phoenix.card].has_flag(ObjectFlag::Bestowed));
}

TEST_CASE("Agente Rakdos: niente fuga se senza Nethergoyf restano meno di 3 tipi", "[agent][escape]") {
    // Le altre carte hanno esattamente 4 tipi: la fuga ne esilia uno per tipo e restano una
    // terra e un Bolt (2 tipi). Contando anche Nethergoyf sarebbero 3.
    Driver d(staged_rakdos({}, {"Blood Crypt", "Swamp", "Blood Crypt"},
                           {"Nethergoyf", "Swamp", "Marsh Flats", "Lightning Bolt", "Lightning Bolt", "Thoughtseize",
                            "Mishra's Bauble"}));
    d.run_to_turn(2);
    CHECK_FALSE(d.chose(DecisionKind::Priority, ActionType::CastSpell, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Nethergoyf"));
}

TEST_CASE("Agente Rakdos: fuga nella seconda fase principale se restano almeno 3 tipi", "[agent][escape]") {
    Driver d(staged_rakdos({}, {"Blood Crypt", "Swamp", "Blood Crypt"},
                           {"Nethergoyf", "Swamp", "Marsh Flats", "Lightning Bolt", "Lightning Bolt", "Thoughtseize",
                            "Mishra's Bauble", "Street Wraith"}));
    d.run_to_turn(2);
    const auto escape = std::find_if(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.action.type == ActionType::CastSpell && c.action.value == kEscape;
    });
    REQUIRE(escape != d.choices.end());
    CHECK(escape->step == Step::Main2);
    CHECK(on_battlefield(d.st.s, "Nethergoyf"));
    CHECK(d.st.s.players[kRakdos].gy.distinct_types() >= 3);
}

TEST_CASE("Agente Rakdos: l'esilio della fuga conserva i tipi e risparmia la Phoenix", "[agent][escape]") {
    // Fear of Missing Out e Detective's Phoenix danno gli stessi tipi: a parità di tipi
    // rimasti si esilia FOMO, perché la Phoenix si può conferire dal cimitero.
    Driver d(staged_rakdos({}, {"Blood Crypt", "Swamp", "Blood Crypt"},
                           {"Nethergoyf", "Detective's Phoenix", "Fear of Missing Out", "Street Wraith", "Swamp",
                            "Lightning Bolt", "Thoughtseize", "Mishra's Bauble", "Marsh Flats", "Lightning Bolt"}));
    d.run_to_turn(2);
    REQUIRE(on_battlefield(d.st.s, "Nethergoyf"));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Detective's Phoenix"));
    CHECK_FALSE(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Fear of Missing Out"));
    // Esiliati FOMO, una terra e un Bolt (creatura, incantesimo, terra, istantaneo): restano
    // tutti e sei i tipi, perché ognuno ha un'altra copia nel cimitero.
    CHECK(d.st.s.players[kRakdos].gy.distinct_types() == 6);
}

// ---------------------------------------------------------------------------
// Sezione 9: Fear of Missing Out
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: FOMO scarta la Phoenix se c'e' una fonte rossa", "[agent][fomo]") {
    Driver d(staged_rakdos({"Fear of Missing Out", "Detective's Phoenix", "Nethergoyf"},
                           {"Swamp", "Blood Crypt", "Blood Crypt"}));
    d.st.s.turn.turn_number = 3;
    d.run_to_turn(4);
    CHECK(on_battlefield(d.st.s, "Fear of Missing Out"));
    CHECK(d.chose(DecisionKind::DiscardCard, ActionType::ChooseCard, "Detective's Phoenix"));
}

TEST_CASE("Agente Rakdos: il trigger di FOMO stappa l'attaccante con piu' forza", "[agent][fomo]") {
    Driver d(staged_rakdos({}, {"Fear of Missing Out", "Nethergoyf", "Stitcher's Supplier"},
                           {"Lightning Bolt", "Thoughtseize", "Swamp", "Mishra's Bauble"}));
    d.run_to_turn(2);
    const auto target = std::find_if(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.kind == DecisionKind::ChooseTarget && c.action.type == ActionType::TargetObject;
    });
    REQUIRE(target != d.choices.end());
    CHECK(target->card == "Nethergoyf");  // 4/5 con quattro tipi nel cimitero
    CHECK(d.st.s.players[kUw].life < 20 - 4 - 2 - 1);  // ha attaccato due volte
}

// ---------------------------------------------------------------------------
// Sezione 8: attacchi e blocchi
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: non attacca se un bloccante la uccide e sopravvive", "[agent][combat]") {
    Driver d(staged_rakdos({}, {"Stitcher's Supplier"}, {}, {"Plains"}, {"Guide of Souls"}));
    d.run_to_turn(2);
    REQUIRE(d.chose(DecisionKind::DeclareAttacker, ActionType::ChooseOption, ""));
    CHECK(d.st.s.players[kUw].life == 20);
}

TEST_CASE("Agente Rakdos: attacca senza bloccanti possibili", "[agent][combat]") {
    Driver d(staged_rakdos({}, {"Stitcher's Supplier"}));
    d.run_to_turn(2);
    CHECK(d.st.s.players[kUw].life == 19);
}

TEST_CASE("Agente Rakdos: blocca se il bloccante uccide l'attaccante e sopravvive", "[agent][combat]") {
    // Nethergoyf 2/3 (due tipi nel cimitero) contro Guide of Souls 1/2.
    Driver d(staged_rakdos({}, {"Nethergoyf"}, {"Lightning Bolt", "Swamp"}, {"Plains"}, {"Guide of Souls"}, kUw));
    d.run_until([](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
    act(d.st, Action{ActionType::TargetPlayer, kNoCard, kRakdos});  // UW attacca a mano
    d.run_until([](const GameState& s) { return s.turn.step >= Step::CombatDamage || s.turn.turn_number > 1; });
    CHECK(d.chose(DecisionKind::DeclareBlockers, ActionType::ChooseCard, "Nethergoyf"));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Guide of Souls"));
    CHECK(on_battlefield(d.st.s, "Nethergoyf"));
}

// ---------------------------------------------------------------------------
// Sezione 1: principi
// ---------------------------------------------------------------------------

TEST_CASE("Agente Rakdos: non legge la mano avversaria ne' l'ordine dei grimori", "[agent][hidden]") {
    // A ogni decisione di Rakdos si costruisce uno stato gemello con la mano di UW scambiata
    // con carte del suo grimorio e i grimori rimescolati: la scelta deve essere identica.
    const CardDatabase& db = db_with_tokens();
    int compared = 0;
    for (const std::uint64_t seed : {2u, 5u, 9u}) {
        GameState s = create_game(db, uw_sim_deck(), rakdos_deck(), seed);
        start_game(s, db, StartOptions{static_cast<PlayerId>(seed % 2), true});
        RakdosAgent agent(db, seed);
        GoldfishAgent uw;
        for (int i = 0; i < 400 && s.pending.kind != DecisionKind::GameOver; ++i) {
            const auto actions = legal_actions(s, db);
            if (s.pending.player != kRakdos) {
                apply(s, db, uw.choose(s, actions));
                continue;
            }
            const DecisionKind k = s.pending.kind;
            if (k != DecisionKind::ChooseFromHand && k != DecisionKind::Surveil && k != DecisionKind::SearchLibrary) {
                GameState twin = s;
                auto& uw_hand = twin.players[kUw].hand;
                auto& uw_lib = twin.players[kUw].library;
                for (std::size_t h = 0; h < uw_hand.size() && h < uw_lib.size(); ++h) {
                    std::swap(uw_hand[h], uw_lib[h]);
                    twin.objects[uw_hand[h]].zone = Zone::Hand;
                    twin.objects[uw_lib[h]].zone = Zone::Library;
                }
                shuffle_library(twin, kUw);
                shuffle_library(twin, kRakdos);
                REQUIRE(check_invariants(twin, db).empty());
                RakdosAgent copy = agent;
                const Action original = agent.choose(s, actions);
                const Action other = copy.choose(twin, legal_actions(twin, db));
                INFO("seed " << seed << ", decisione " << static_cast<int>(k));
                CHECK(original == other);
                ++compared;
                apply(s, db, original);
            } else {
                apply(s, db, agent.choose(s, actions));
            }
        }
    }
    CHECK(compared > 50);
}

TEST_CASE("Agente Rakdos: ogni scelta registra la sua regola", "[agent]") {
    Driver d(staged_rakdos({"Marsh Flats", "Nethergoyf", "Lightning Bolt"}, {"Swamp"}));
    d.run_to_turn(3);
    std::uint64_t total = 0;
    for (const auto& [rule, n] : d.agent.rule_counts()) {
        CHECK_FALSE(rule.empty());
        total += n;
    }
    CHECK(total == d.choices.size());
    CHECK(d.agent.rule_counts().contains("terra: fetch"));
}
