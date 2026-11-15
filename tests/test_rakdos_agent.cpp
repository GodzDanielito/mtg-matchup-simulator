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

// L'agente com'era prima della decisione del 15/11/2026 (mulligan B, X5 e correzione del turno 1
// spente): i test di una variante partono da qui, per restare isolati dal comportamento predefinito.
RakdosAgentOptions r1() { return RakdosAgentOptions::base_r1(); }

// Una scelta dell'agente, con il nome della carta coinvolta al momento della scelta.
struct Choice {
    DecisionKind kind;
    Action action;
    std::string card;
    std::string rule;
    std::uint16_t turn;
    Step step;
    std::string subject;  // la carta della decisione (l'attaccante, per DeclareAttacker)
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
                                     std::string(agent.last_rule()), st.s.turn.turn_number, st.s.turn.step,
                                     st.s.pending.subject.card == kNoCard ? "" : name_of(st.s, st.s.pending.subject.card)});
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

ActionType mulligan_choice(const GameState& s, MulliganVariant variant = MulliganVariant::B) {
    RakdosAgent agent(db_with_tokens(), 1, RakdosAgentOptions{false, variant});
    return agent.choose(s, legal(s)).type;
}

// La carta messa in fondo per prima da una mano tenuta dopo `mulligans` mulligan.
std::string first_bottomed(std::initializer_list<const char*> seven, std::uint8_t mulligans, MulliganVariant variant) {
    GameState s = opening(seven, mulligans);
    RakdosAgent agent(db_with_tokens(), 1, RakdosAgentOptions{false, variant});
    apply(s, db_with_tokens(), Action{ActionType::Keep});  // Rakdos tiene (scelta forzata del test)
    for (int guard = 0; guard < 4 && !is_decision(s, DecisionKind::BottomCards, kRakdos); ++guard) {
        apply(s, db_with_tokens(), legal(s).front());  // UW tiene
    }
    REQUIRE(is_decision(s, DecisionKind::BottomCards, kRakdos));
    return name_of(s, agent.choose(s, legal(s)).card);
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

TEST_CASE("Agente Rakdos: variante A, una sola terra e' sempre mulligan, anche con Wraith, Bauble e Troll",
          "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Street Wraith", "Mishra's Bauble", "Troll of Khazad-dûm", "Nethergoyf",
                                 "Stitcher's Supplier", "Lightning Bolt"});
    CHECK(mulligan_choice(s, MulliganVariant::A) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: dal 15/11/2026 la variante predefinita e' la C (base_r2), la B e' base_r1",
          "[agent][mulligan][base_r2]") {
    CHECK(RakdosAgentOptions{}.mulligan == MulliganVariant::C);
    CHECK(r1().mulligan == MulliganVariant::B);
}

// X1-C predefinita (sezione 22): ipotesi di Daniele (pilota), misurata, da confermare con la ricerca
// ad albero. Con 7 o 6 carte una sola terra basta con UNA carta che cerca e una creatura da un mana.
TEST_CASE("Agente Rakdos: base_r2 tiene una terra con una sola carta che cerca, base_r1 no", "[agent][mulligan][base_r2]") {
    for (const std::uint8_t mulligans : {std::uint8_t{0}, std::uint8_t{1}}) {
        CAPTURE(mulligans);
        const GameState s = opening({"Swamp", "Street Wraith", "Nethergoyf", "Lightning Bolt", "Flare of Malice",
                                     "Detective's Phoenix", "Thoughtseize"},
                                    mulligans);
        RakdosAgent r2(db_with_tokens(), 1);
        CHECK(r2.choose(s, legal(s)).type == ActionType::Keep);
        CHECK(r2.last_rule() == "mulligan: una terra con carte che cercano e creatura da un mana");
        RakdosAgent base(db_with_tokens(), 1, r1());
        CHECK(base.choose(s, legal(s)).type == ActionType::Mulligan);
    }
}

// Varianti B e C: una sola terra. Servono carte che cercano carte (Street Wraith, Mishra's Bauble,
// Troll of Khazad-dûm: B due, C una) e una creatura da un mana lanciabile con quella terra.
TEST_CASE("Agente Rakdos: B tiene una terra con due carte che cercano e una creatura da un mana",
          "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Street Wraith", "Mishra's Bauble", "Nethergoyf", "Lightning Bolt",
                                 "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice(s, MulliganVariant::B) == ActionType::Keep);
    CHECK(mulligan_choice(s, MulliganVariant::C) == ActionType::Keep);
    CHECK(mulligan_choice(s, MulliganVariant::A) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: B con il Troll tra le due carte che cercano", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Troll of Khazad-dûm", "Mishra's Bauble", "Stitcher's Supplier",
                                 "Lightning Bolt", "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice(s, MulliganVariant::B) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: una fetch conta come terra che cerca la Swamp", "[agent][mulligan]") {
    const GameState s = opening({"Marsh Flats", "Street Wraith", "Mishra's Bauble", "Moonshadow", "Lightning Bolt",
                                 "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice(s, MulliganVariant::B) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: B vuole due carte che cercano, C ne basta una", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Street Wraith", "Nethergoyf", "Lightning Bolt", "Flare of Malice",
                                 "Detective's Phoenix", "Thoughtseize"});
    CHECK(mulligan_choice(s, MulliganVariant::B) == ActionType::Mulligan);
    CHECK(mulligan_choice(s, MulliganVariant::C) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: una terra senza carte che cercano e' mulligan in ogni variante", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Nethergoyf", "Moonshadow", "Lightning Bolt", "Flare of Malice",
                                 "Detective's Phoenix", "Thoughtseize"});
    for (const MulliganVariant v : {MulliganVariant::A, MulliganVariant::B, MulliganVariant::C}) {
        CHECK(mulligan_choice(s, v) == ActionType::Mulligan);
    }
}

TEST_CASE("Agente Rakdos: una terra e carte che cercano ma nessuna creatura da un mana e' mulligan",
          "[agent][mulligan]") {
    // Fear of Missing Out costa {1}{R}: non e' una creatura da un mana.
    const GameState s = opening({"Swamp", "Street Wraith", "Mishra's Bauble", "Fear of Missing Out",
                                 "Lightning Bolt", "Flare of Malice", "Thoughtseize"});
    CHECK(mulligan_choice(s, MulliganVariant::B) == ActionType::Mulligan);
    CHECK(mulligan_choice(s, MulliganVariant::C) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: senza terre e' sempre mulligan, anche con tre carte che cercano", "[agent][mulligan]") {
    const GameState s = opening({"Street Wraith", "Mishra's Bauble", "Troll of Khazad-dûm", "Nethergoyf",
                                 "Stitcher's Supplier", "Lightning Bolt", "Thoughtseize"});
    for (const MulliganVariant v : {MulliganVariant::A, MulliganVariant::B, MulliganVariant::C}) {
        CHECK(mulligan_choice(s, v) == ActionType::Mulligan);
    }
}

TEST_CASE("Agente Rakdos: con 6 carte B tiene una terra solo se dopo la carta in fondo restano due carte che cercano",
          "[agent][mulligan]") {
    // Sette carte viste, una va in fondo: con due carte che cercano la mano da 6 le conserva entrambe.
    const GameState keep = opening({"Swamp", "Street Wraith", "Mishra's Bauble", "Nethergoyf", "Lightning Bolt",
                                    "Flare of Malice", "Detective's Phoenix"},
                                   1);
    CHECK(mulligan_choice(keep, MulliganVariant::B) == ActionType::Keep);
    // Una sola carta che cerca: B mulligan, C tiene.
    const GameState one = opening({"Swamp", "Street Wraith", "Nethergoyf", "Lightning Bolt", "Flare of Malice",
                                   "Detective's Phoenix", "Thoughtseize"},
                                  1);
    CHECK(mulligan_choice(one, MulliganVariant::B) == ActionType::Mulligan);
    CHECK(mulligan_choice(one, MulliganVariant::C) == ActionType::Keep);
    CHECK(mulligan_choice(one, MulliganVariant::A) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: con 5 carte tiene anche una terra sola, in ogni variante", "[agent][mulligan]") {
    const GameState s = opening({"Swamp", "Nethergoyf", "Moonshadow", "Lightning Bolt", "Flare of Malice",
                                 "Detective's Phoenix", "Thoughtseize"},
                                2);
    for (const MulliganVariant v : {MulliganVariant::A, MulliganVariant::B, MulliganVariant::C}) {
        CHECK(mulligan_choice(s, v) == ActionType::Keep);
    }
}

TEST_CASE("Agente Rakdos: le varianti non cambiano le mani da 2 a 4 terre", "[agent][mulligan]") {
    const GameState ok = opening({"Swamp", "Marsh Flats", "Nethergoyf", "Lightning Bolt", "Thoughtseize",
                                  "Flare of Malice", "Detective's Phoenix"});
    const GameState many = opening({"Swamp", "Swamp", "Blood Crypt", "Marsh Flats", "Polluted Delta", "Nethergoyf",
                                    "Lightning Bolt"});
    for (const MulliganVariant v : {MulliganVariant::A, MulliganVariant::B, MulliganVariant::C}) {
        CHECK(mulligan_choice(ok, v) == ActionType::Keep);
        CHECK(mulligan_choice(many, v) == ActionType::Mulligan);
    }
}

TEST_CASE("Agente Rakdos: in una mano da una terra le carte che cercano non vanno in fondo", "[agent][mulligan]") {
    // Mano da 6 con una terra, due carte che cercano e una creatura da un mana: va in fondo una
    // delle altre, non il Troll (la carta di valore piu' basso in assoluto).
    const std::string b = first_bottomed({"Swamp", "Troll of Khazad-dûm", "Mishra's Bauble", "Nethergoyf",
                                          "Lightning Bolt", "Flare of Malice", "Detective's Phoenix"},
                                         1, MulliganVariant::B);
    CHECK(b == "Lightning Bolt");
    // Variante A: la regola di sempre, il valore piu' basso, cioe' il Troll.
    const std::string a = first_bottomed({"Swamp", "Troll of Khazad-dûm", "Mishra's Bauble", "Nethergoyf",
                                          "Lightning Bolt", "Flare of Malice", "Detective's Phoenix"},
                                         1, MulliganVariant::A);
    CHECK(a == "Troll of Khazad-dûm");
}

TEST_CASE("Agente Rakdos: con piu' di due carte che cercano se ne puo' mettere una in fondo", "[agent][mulligan]") {
    const std::string b = first_bottomed({"Swamp", "Troll of Khazad-dûm", "Mishra's Bauble", "Street Wraith",
                                          "Nethergoyf", "Lightning Bolt", "Flare of Malice"},
                                         1, MulliganVariant::B);
    CHECK(b == "Troll of Khazad-dûm");  // tre carte che cercano: la meno preziosa puo' andare
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

// Correzione del turno 1 (sezioni 6 e 22, decisione di Daniele del 15/11/2026): senza creatura da un
// mana lanciabile, Thoughtseize prima del ciclo di Troll, tranne quando dopo la terra giocata non resta
// un'altra terra in mano per il turno 2. Bolt su Guide o Ocelot a vista resta prima di Thoughtseize.
//
// Dati Oracle (Scryfall oracle_cards del 9/11/2026, config/cards.json):
//   Troll of Khazad-dûm {5}{B}: "This creature can't be blocked except by three or more creatures.
//     Swampcycling {1} ({1}, Discard this card: Search your library for a Swamp card, reveal it, put it
//     into your hand, then shuffle.)"
//   Thoughtseize {B}: "Target player reveals their hand. You choose a nonland card from it. That player
//     discards that card. You lose 2 life."
namespace {

RakdosAgentOptions with_t1_fix() {
    RakdosAgentOptions o = r1();
    o.t1_seize_before_troll = true;
    return o;
}

// Primo turno di Rakdos (in play) con `hand`; UW tiene Solitude e una Plains e passa sempre.
Driver first_turn(std::initializer_list<const char*> hand, RakdosAgentOptions o,
                  std::initializer_list<const char*> uw_field = {}) {
    Driver d(staged_rakdos(hand, {}, {}, {"Plains", "Solitude"}, uw_field), o);
    d.run_to_turn(2);
    return d;
}

bool used_rule(const Driver& d, std::string_view rule) {
    return std::any_of(d.choices.begin(), d.choices.end(), [&](const Choice& c) { return c.rule == rule; });
}

}  // namespace

TEST_CASE("Agente Rakdos: correzione del turno 1, Thoughtseize prima del ciclo del Troll con un'altra terra in mano",
          "[agent][order][t1fix]") {
    const auto hand = {"Swamp", "Swamp", "Thoughtseize", "Troll of Khazad-dûm"};
    SECTION("spenta (base_r1): il ciclo del Troll prende l'unico mana, Thoughtseize resta in mano") {
        const Driver d = first_turn(hand, r1());
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
    }
    SECTION("accesa: Thoughtseize prende Solitude, il Troll resta in mano") {
        const Driver d = first_turn(hand, with_t1_fix());
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Troll of Khazad-dûm"));
        CHECK(used_rule(d, "Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
    SECTION("predefinita (base_r2): come accesa") {
        const Driver d = first_turn(hand, {});
        CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(used_rule(d, "Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
}

TEST_CASE("Agente Rakdos: correzione del turno 1, senza un'altra terra in mano si cicla il Troll", "[agent][order][t1fix]") {
    for (const RakdosAgentOptions& o : {r1(), with_t1_fix(), RakdosAgentOptions{}}) {
        const Driver d = first_turn({"Swamp", "Thoughtseize", "Troll of Khazad-dûm"}, o);
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
        CHECK(used_rule(d, "ciclo del Troll (serve una terra)"));
        CHECK_FALSE(used_rule(d, "Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
}

// Secondo scenario indipendente della condizione "un'altra terra in mano" (la mutazione che la toglie
// faceva fallire solo due casi di test): Rakdos in draw, una fetch invece della Swamp, Mishra's Bauble.
// La pescata del turno 2 e' una carta non terra, quindi dopo la fetch la mano resta senza terre.
//   Bloodstained Mire: "{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain
//     card, put it onto the battlefield, then shuffle."
//   Mishra's Bauble {0}: "{T}, Sacrifice this artifact: Look at the top card of target player's library.
//     Draw a card at the beginning of the next turn's upkeep." (Scryfall oracle_cards del 9/11/2026)
TEST_CASE("Agente Rakdos: correzione del turno 1 in draw, fetch e Bauble senza un'altra terra: si cicla il Troll",
          "[agent][order][t1fix]") {
    for (const RakdosAgentOptions& o : {with_t1_fix(), RakdosAgentOptions{}}) {
        Staged st = staged_rakdos({"Bloodstained Mire", "Mishra's Bauble", "Thoughtseize", "Troll of Khazad-dûm"}, {}, {},
                                  {"Plains", "Solitude"}, {}, kUw);
        stack_library(st.s, kRakdos, {"Lightning Bolt"});  // la pescata del primo turno di Rakdos
        Driver d(std::move(st), o);
        d.run_to_turn(3);
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Bloodstained Mire"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Mishra's Bauble"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
        CHECK(used_rule(d, "ciclo del Troll (serve una terra)"));
        CHECK_FALSE(used_rule(d, "Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
}

TEST_CASE("Agente Rakdos: correzione del turno 1, con una creatura da un mana non cambia nulla", "[agent][order][t1fix]") {
    // La creatura viene prima di Thoughtseize (sezione 6); la correzione vale solo senza creatura.
    for (const RakdosAgentOptions& o : {r1(), with_t1_fix()}) {
        const Driver d = first_turn({"Swamp", "Swamp", "Thoughtseize", "Troll of Khazad-dûm", "Stitcher's Supplier"}, o);
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK_FALSE(used_rule(d, "Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
}

// Variante X6 (sezione 23, spenta per default): al primo turno, con una sola terra in campo, Troll of
// Khazad-dûm in mano e il ciclo che l'agente farebbe, la creatura da un mana lanciabile invece del ciclo.
//
// Dati Oracle (Scryfall oracle_cards del 9/11/2026, config/cards.json):
//   Stitcher's Supplier {B} 1/1: "When this creature enters or dies, mill three cards."
//   Dark Matter Manipulator {B} 1/2: "When this creature enters, mill three cards. This creature gets +2/+0
//     for every seven cards in your graveyard."
//   Moonshadow {B} 7/7: "Menace. This creature enters with six -1/-1 counters on it. Whenever one or more
//     permanent cards are put into your graveyard from anywhere while this creature has a -1/-1 counter on
//     it, remove a -1/-1 counter from this creature."
//   Troll of Khazad-dûm: "Swampcycling {1}" (sopra, correzione del turno 1).
namespace {
constexpr std::string_view kX6Rule = "creatura: turno 1 prima del ciclo del Troll (variante X6)";

RakdosAgentOptions with_x6(RakdosAgentOptions o) {
    o.t1_creature_before_troll = true;
    return o;
}
}  // namespace

TEST_CASE("Agente Rakdos: variante X6, al turno 1 la creatura da un mana invece del ciclo del Troll", "[agent][order][x6]") {
    const auto hand = {"Swamp", "Troll of Khazad-dûm", "Stitcher's Supplier"};
    SECTION("spenta (base_r2 e base_r1): il ciclo del Troll prende l'unico mana, Supplier resta in mano") {
        for (const RakdosAgentOptions& o : {RakdosAgentOptions{}, r1()}) {
            const Driver d = first_turn(hand, o);
            CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
            CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Stitcher's Supplier"));
            CHECK(used_rule(d, "ciclo del Troll (serve una terra)"));
            CHECK_FALSE(used_rule(d, kX6Rule));
        }
    }
    SECTION("accesa (sopra base_r1 e sopra base_r2): Supplier in campo, il Troll resta in mano") {
        for (const RakdosAgentOptions& o : {with_x6(r1()), with_x6({})}) {
            const Driver d = first_turn(hand, o);
            CHECK(on_battlefield(d.st.s, "Stitcher's Supplier"));
            CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Troll of Khazad-dûm"));
            CHECK(used_rule(d, kX6Rule));
            CHECK_FALSE(used_rule(d, "ciclo del Troll (serve una terra)"));
        }
    }
}

TEST_CASE("Agente Rakdos: variante X6, anche con un'altra terra e Thoughtseize in mano", "[agent][order][x6]") {
    // Con Manipulator lanciabile la correzione del turno 1 non vale (c'e' una creatura da un mana):
    // spenta si cicla il Troll; accesa si lancia Manipulator. Thoughtseize resta in mano in entrambi i casi.
    const auto hand = {"Swamp", "Swamp", "Thoughtseize", "Troll of Khazad-dûm", "Dark Matter Manipulator"};
    const Driver off = first_turn(hand, RakdosAgentOptions{});
    CHECK(in_zone(off.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
    CHECK(in_zone(off.st.s, kRakdos, Zone::Hand, "Dark Matter Manipulator"));
    CHECK(in_zone(off.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
    const Driver on = first_turn(hand, with_x6({}));
    CHECK(on_battlefield(on.st.s, "Dark Matter Manipulator"));
    CHECK(in_zone(on.st.s, kRakdos, Zone::Hand, "Troll of Khazad-dûm"));
    CHECK(in_zone(on.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
    CHECK(used_rule(on, kX6Rule));
}

TEST_CASE("Agente Rakdos: variante X6, con due terre in campo o dal secondo turno non cambia nulla", "[agent][order][x6]") {
    SECTION("due terre in campo al turno 1: il ciclo e poi Supplier, come spenta") {
        for (const RakdosAgentOptions& o : {RakdosAgentOptions{}, with_x6({})}) {
            Driver d(staged_rakdos({"Troll of Khazad-dûm", "Stitcher's Supplier"}, {"Swamp", "Swamp"}, {},
                                   {"Plains", "Solitude"}),
                     o);
            d.run_to_turn(2);
            CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
            CHECK(used_rule(d, "ciclo del Troll (serve una terra)"));
            CHECK_FALSE(used_rule(d, kX6Rule));
        }
    }
    SECTION("secondo turno, una sola terra in campo: si cicla il Troll") {
        // Turno 1: X6 lancia Moonshadow (che non macina). Turno 3: la pescata e' una carta non terra, la
        // terra in campo e' ancora una, ma non e' piu' il primo turno: il ciclo, poi Nethergoyf.
        Staged st = staged_rakdos({"Swamp", "Troll of Khazad-dûm", "Moonshadow", "Nethergoyf"}, {}, {},
                                  {"Plains", "Solitude"});
        stack_library(st.s, kRakdos, {"Lightning Bolt"});
        Driver d(std::move(st), with_x6({}));
        d.run_to_turn(4);
        const auto x6 = std::count_if(d.choices.begin(), d.choices.end(), [](const Choice& c) { return c.rule == kX6Rule; });
        CHECK(x6 == 1);
        const bool cycled_turn3 = std::any_of(d.choices.begin(), d.choices.end(), [](const Choice& c) {
            return c.turn == 3 && c.rule == "ciclo del Troll (serve una terra)";
        });
        CHECK(cycled_turn3);
        CHECK(on_battlefield(d.st.s, "Moonshadow"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Troll of Khazad-dûm"));
    }
}

TEST_CASE("Agente Rakdos: correzione del turno 1, Bolt su Ocelot Pride a vista resta prima di Thoughtseize",
          "[agent][order][t1fix]") {
    for (const RakdosAgentOptions& o : {r1(), with_t1_fix(), RakdosAgentOptions{}}) {
        const Driver d = first_turn({"Blackcleave Cliffs", "Swamp", "Thoughtseize", "Lightning Bolt"}, o, {"Ocelot Pride"});
        CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Ocelot Pride"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK(used_rule(d, "Bolt: Ocelot Pride o Guide of Souls a vista"));
    }
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
// Teferi, Time Raveler (sezioni 4, 7, 8 e 11; audit 1: A-1, A-2, A-3)
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kTeferi = "Teferi, Time Raveler";

void set_loyalty(Driver& d, int loyalty) {
    const ObjRef t = find_on_battlefield(d.st.s, kTeferi);
    d.st.s.objects[t.card].counters.loyalty = static_cast<std::uint8_t>(loyalty);
}

// Chi ha attaccato la creatura nominata, nella prima dichiarazione d'attacco registrata:
// "Teferi", "giocatore", "nessuno" o "non dichiarata".
std::string defender_of(const Driver& d, std::string_view creature) {
    for (const Choice& c : d.choices) {
        if (c.kind != DecisionKind::DeclareAttacker || c.subject != creature) continue;
        if (c.action.type == ActionType::TargetPlayer) return "giocatore";
        if (c.action.type == ActionType::TargetObject) return c.card == kTeferi ? "Teferi" : "altro";
        return "nessuno";
    }
    return "non dichiarata";
}

}  // namespace

TEST_CASE("Agente Rakdos: senza attacco letale su Teferi la creatura piu' piccola che basta, il resto sul giocatore",
          "[agent][combat][teferi]") {
    // Nethergoyf 3/4 (istantaneo, terra, creatura nel cimitero) e Stitcher's Supplier 1/1; Teferi a 1.
    Driver d(staged_rakdos({}, {"Nethergoyf", "Stitcher's Supplier"}, {"Lightning Bolt", "Swamp", "Stitcher's Supplier"},
                           {"Plains"}, {kTeferi}));
    set_loyalty(d, 1);
    d.run_to_turn(2);
    CHECK(defender_of(d, "Stitcher's Supplier") == "Teferi");
    CHECK(defender_of(d, "Nethergoyf") == "giocatore");
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(d.st.s.players[kUw].life == 17);
}

TEST_CASE("Agente Rakdos: su Teferi prima le creature che non possono essere bloccate", "[agent][combat][teferi]") {
    // Phoenix 2/2 volante contro nessun volatore; Nethergoyf 1/2 attacca (Guide of Souls 1/2 non lo uccide)
    // e da solo basterebbe per Teferi a 1, ma il danno della Phoenix arriva di sicuro.
    Driver d(staged_rakdos({}, {"Detective's Phoenix", "Nethergoyf"}, {"Swamp"}, {"Plains"}, {kTeferi, "Guide of Souls"}));
    set_loyalty(d, 1);
    d.run_to_turn(2);
    CHECK(defender_of(d, "Detective's Phoenix") == "Teferi");
    CHECK(defender_of(d, "Nethergoyf") == "giocatore");
}

TEST_CASE("Agente Rakdos: con un attacco letale sul giocatore Teferi si ignora", "[agent][combat][teferi]") {
    Driver d(staged_rakdos({}, {"Detective's Phoenix", "Nethergoyf"}, {"Swamp"}, {"Plains"}, {kTeferi}));
    set_loyalty(d, 1);
    d.st.s.players[kUw].life = 3;  // Phoenix 2 + Nethergoyf 1
    d.run_until([](const GameState& s) { return s.pending.kind == DecisionKind::GameOver; });
    CHECK(defender_of(d, "Detective's Phoenix") == "giocatore");
    CHECK(defender_of(d, "Nethergoyf") == "giocatore");
    CHECK(d.st.s.players[kUw].life <= 0);
}

TEST_CASE("Agente Rakdos: se l'attacco non basta a togliere Teferi va tutto sul giocatore", "[agent][combat][teferi]") {
    Driver d(staged_rakdos({}, {"Stitcher's Supplier"}, {}, {"Plains"}, {kTeferi}));  // Teferi a 4
    d.run_to_turn(2);
    CHECK(defender_of(d, "Stitcher's Supplier") == "giocatore");
    CHECK(on_battlefield(d.st.s, kTeferi));
    CHECK(d.st.s.players[kUw].life == 19);
}

TEST_CASE("Agente Rakdos: Bolt su Teferi a fedelta' 3 prima di Ocelot Pride", "[agent][bolt][teferi]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {"Ocelot Pride", kTeferi}));
    set_loyalty(d, 3);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(on_battlefield(d.st.s, "Ocelot Pride"));
}

TEST_CASE("Agente Rakdos: Teferi a fedelta' 4 senza attaccanti, Bolt resta su Ocelot Pride", "[agent][bolt][teferi]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {"Ocelot Pride", kTeferi}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Ocelot Pride"));
    CHECK(on_battlefield(d.st.s, kTeferi));
}

TEST_CASE("Agente Rakdos: Bolt su Teferi prima del combattimento, l'attacco toglie il resto", "[agent][bolt][teferi]") {
    // Teferi a 5: Bolt lo porta a 2 nella prima fase principale, la Phoenix (non bloccabile) lo toglie.
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt", "Detective's Phoenix"}, {}, {"Plains"}, {kTeferi}));
    set_loyalty(d, 5);
    d.run_to_turn(2);
    CHECK(d.chose(DecisionKind::Priority, ActionType::CastSpell, "Lightning Bolt"));
    CHECK(defender_of(d, "Detective's Phoenix") == "Teferi");
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(d.st.s.players[kUw].life == 20);
}

TEST_CASE("Agente Rakdos: se l'attacco basta a togliere Teferi, Bolt resta in mano", "[agent][bolt][teferi]") {
    // Teferi a 3 e Nethergoyf 3/4 senza bloccanti: l'attacco lo toglie da solo.
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt", "Nethergoyf"}, {"Lightning Bolt", "Swamp", "Stitcher's Supplier"},
                           {"Plains"}, {kTeferi}));
    set_loyalty(d, 3);
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Lightning Bolt"));
}

TEST_CASE("Agente Rakdos: con Teferi in campo Bolt su Phelia nella prima fase principale", "[agent][bolt][teferi]") {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {"Phelia, Exuberant Shepherd", kTeferi}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Phelia, Exuberant Shepherd"));
    const auto bolt = std::find_if(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.action.type == ActionType::CastSpell && c.card == "Lightning Bolt";
    });
    REQUIRE(bolt != d.choices.end());
    CHECK(bolt->step == Step::Main1);
}

TEST_CASE("Agente Rakdos: con Teferi in campo Flare pagato con il mana nella prima fase principale", "[agent][flare][teferi]") {
    Driver d(staged_rakdos({"Flare of Malice"}, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"}, {}, {"Plains"}, {kTeferi}));
    d.run_to_turn(2);
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    const auto flare = std::find_if(d.choices.begin(), d.choices.end(), [](const Choice& c) {
        return c.action.type == ActionType::CastSpell && c.card == "Flare of Malice";
    });
    REQUIRE(flare != d.choices.end());
    CHECK(flare->action.value == static_cast<std::uint8_t>(CastMethod::Normal));
    CHECK(flare->step == Step::Main1);
}

// Teferi in campo: orologio, modalità corsa o partita lunga (sezione 8, revisione del 10/11/2026).
// Posizione comune: Nethergoyf 3/4 (istantaneo, terra, creatura nel cimitero) senza bloccanti,
// un Lightning Bolt in mano, Teferi a fedeltà 3. Orologio = (vita di UW - 3) / 3, per eccesso.
namespace {

Driver clock_position(std::int16_t uw_life, RakdosAgentOptions options = {}) {
    Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt", "Nethergoyf"}, {"Lightning Bolt", "Swamp", "Stitcher's Supplier"},
                           {"Plains"}, {kTeferi}),
             options);
    set_loyalty(d, 3);
    d.st.s.players[kUw].life = uw_life;
    return d;
}

// La regola registrata per la dichiarazione d'attacco della creatura nominata.
std::string attack_rule_of(const Driver& d, std::string_view creature) {
    for (const Choice& c : d.choices) {
        if (c.kind == DecisionKind::DeclareAttacker && c.subject == creature) return c.rule;
    }
    return "";
}

}  // namespace

TEST_CASE("Agente Rakdos: orologio di 2 turni, corsa: Teferi si ignora e Bolt va in faccia sopra i 6 punti vita",
          "[agent][teferi][clock]") {
    Driver d = clock_position(9);  // (9 - 3) / 3 = 2 turni
    d.run_to_turn(2);
    CHECK(defender_of(d, "Nethergoyf") == "giocatore");
    CHECK(on_battlefield(d.st.s, kTeferi));
    CHECK(d.st.s.players[kUw].life == 3);  // Bolt (a 9 vita) e Nethergoyf
    CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Lightning Bolt"));
    CHECK(attack_rule_of(d, "Nethergoyf").ends_with(" [corsa]"));
}

TEST_CASE("Agente Rakdos: orologio oltre 2 turni, partita lunga: le regole contro Teferi", "[agent][teferi][clock]") {
    Driver d = clock_position(12);  // la vita in più allunga l'orologio: (12 - 3) / 3 = 3 turni
    d.run_to_turn(2);
    CHECK(defender_of(d, "Nethergoyf") == "Teferi");
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(d.st.s.players[kUw].life == 12);
    CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Lightning Bolt"));  // l'attacco basta a togliere Teferi
    CHECK(attack_rule_of(d, "Nethergoyf") == "attacco: su Teferi, abbastanza danno da toglierlo [partita lunga]");
}

TEST_CASE("Agente Rakdos: variante togli sempre Teferi, anche con l'orologio a 2 turni", "[agent][teferi][clock]") {
    RakdosAgentOptions always;
    always.teferi_race_clock = 0;
    Driver d = clock_position(9, always);
    d.run_to_turn(2);
    CHECK(defender_of(d, "Nethergoyf") == "Teferi");
    CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, kTeferi));
    CHECK(attack_rule_of(d, "Nethergoyf").ends_with(" [partita lunga]"));
}

TEST_CASE("Agente Rakdos: senza Teferi i nomi delle regole non portano la modalita'", "[agent][teferi][clock]") {
    Driver d(staged_rakdos({}, {"Nethergoyf"}, {"Lightning Bolt", "Swamp", "Stitcher's Supplier"}));
    d.st.s.players[kUw].life = 9;
    d.run_to_turn(2);
    CHECK(attack_rule_of(d, "Nethergoyf") == "attacco: non puo' essere bloccata");
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

// ---------------------------------------------------------------------------
// Sezione 21: varianti dei test A/B nel matchup (X2, X3, X4). Ogni test gira con l'opzione spenta
// (il comportamento predefinito) e accesa.
// ---------------------------------------------------------------------------

namespace {

ActionType mulligan_choice_with(const GameState& s, RakdosAgentOptions options) {
    RakdosAgent agent(db_with_tokens(), 1, options);
    return agent.choose(s, legal(s)).type;
}

RakdosAgentOptions with_x2() {
    RakdosAgentOptions o = r1();
    o.mulligan_keep_disruption = true;
    return o;
}

RakdosAgentOptions with_x4() {
    RakdosAgentOptions o = r1();
    o.t1_thoughtseize_first = true;
    return o;
}

RakdosAgentOptions with_clock(int threshold) {
    RakdosAgentOptions o = r1();
    o.teferi_race_clock = threshold;
    return o;
}

}  // namespace

TEST_CASE("Agente Rakdos: variante X2, mano da 7 senza creatura economica con due carte di interazione",
          "[agent][mulligan][x2]") {
    // Due o tre terre, nessuna creatura da 1-2 mana (Phoenix 3, Street Wraith 5, Troll 6).
    const GameState ts_bolt = opening({"Swamp", "Blood Crypt", "Thoughtseize", "Lightning Bolt", "Detective's Phoenix",
                                       "Street Wraith", "Troll of Khazad-dûm"});
    const GameState flare_ts = opening({"Swamp", "Blood Crypt", "Marsh Flats", "Thoughtseize", "Flare of Malice",
                                        "Detective's Phoenix", "Street Wraith"});
    SECTION("spenta: mulligan") {
        CHECK(mulligan_choice_with(ts_bolt, r1()) == ActionType::Mulligan);
        CHECK(mulligan_choice_with(flare_ts, r1()) == ActionType::Mulligan);
    }
    SECTION("accesa: tiene") {
        CHECK(mulligan_choice_with(ts_bolt, with_x2()) == ActionType::Keep);
        CHECK(mulligan_choice_with(flare_ts, with_x2()) == ActionType::Keep);
        RakdosAgent agent(db_with_tokens(), 1, with_x2());
        agent.choose(ts_bolt, legal(ts_bolt));
        CHECK(agent.last_rule() == "mulligan: niente creatura economica, due tra Thoughtseize, Bolt e Flare (variante X2)");
    }
}

TEST_CASE("Agente Rakdos: variante X2, la creatura economica non lanciabile conta come assente", "[agent][mulligan][x2]") {
    // Due Swamp e Fear of Missing Out ({1}{R}): la base fa mulligan (manca il rosso).
    const GameState s = opening({"Swamp", "Swamp", "Fear of Missing Out", "Lightning Bolt", "Thoughtseize",
                                 "Flare of Malice", "Detective's Phoenix"});
    CHECK(mulligan_choice_with(s, r1()) == ActionType::Mulligan);
    CHECK(mulligan_choice_with(s, with_x2()) == ActionType::Keep);
}

TEST_CASE("Agente Rakdos: variante X2, una sola carta di interazione non basta", "[agent][mulligan][x2]") {
    const GameState s = opening({"Swamp", "Blood Crypt", "Marsh Flats", "Thoughtseize", "Detective's Phoenix",
                                 "Street Wraith", "Troll of Khazad-dûm"});
    CHECK(mulligan_choice_with(s, r1()) == ActionType::Mulligan);
    CHECK(mulligan_choice_with(s, with_x2()) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: variante X2, non cambia le mani con terre fuori da 2-4", "[agent][mulligan][x2]") {
    const GameState one_land = opening({"Swamp", "Thoughtseize", "Lightning Bolt", "Flare of Malice", "Detective's Phoenix",
                                        "Street Wraith", "Troll of Khazad-dûm"});
    const GameState five_lands = opening({"Swamp", "Swamp", "Blood Crypt", "Marsh Flats", "Polluted Delta", "Thoughtseize",
                                          "Lightning Bolt"});
    CHECK(mulligan_choice_with(one_land, with_x2()) == ActionType::Mulligan);
    CHECK(mulligan_choice_with(five_lands, with_x2()) == ActionType::Mulligan);
}

TEST_CASE("Agente Rakdos: variante X4, al turno 1 Thoughtseize prima della creatura da un mana", "[agent][order][x4]") {
    SECTION("spenta: la creatura, Thoughtseize resta in mano") {
        Driver d(staged_rakdos({"Swamp", "Thoughtseize", "Stitcher's Supplier"}, {}, {}, {"Plains", "Solitude"}));
        d.run_to_turn(2);
        CHECK(on_battlefield(d.st.s, "Stitcher's Supplier"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Hand, "Solitude"));
    }
    SECTION("accesa: Thoughtseize prende Solitude, la creatura resta in mano") {
        Driver d(staged_rakdos({"Swamp", "Thoughtseize", "Stitcher's Supplier"}, {}, {}, {"Plains", "Solitude"}), with_x4());
        d.run_to_turn(2);
        CHECK(in_zone(d.st.s, kRakdos, Zone::Graveyard, "Thoughtseize"));
        CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(in_zone(d.st.s, kRakdos, Zone::Hand, "Stitcher's Supplier"));
        CHECK(std::any_of(d.choices.begin(), d.choices.end(), [](const Choice& c) {
            return c.rule == "Thoughtseize: turno 1 prima della creatura (variante X4)";
        }));
    }
}

TEST_CASE("Agente Rakdos: variante X4, dal secondo turno non cambia nulla", "[agent][order][x4]") {
    // Un solo mana al secondo turno: Thoughtseize viene prima comunque (sezione 4), in entrambe.
    for (const RakdosAgentOptions& o : {r1(), with_x4()}) {
        Driver d(staged_rakdos({"Thoughtseize", "Stitcher's Supplier"}, {"Swamp"}, {}, {"Plains", "Solitude"}), o);
        d.st.s.turn.turn_number = 3;  // secondo turno di Rakdos
        d.run_to_turn(4);
        const auto casts = d.cast_names();
        REQUIRE_FALSE(casts.empty());
        CHECK(casts[0] == "Thoughtseize");
        CHECK(std::none_of(d.choices.begin(), d.choices.end(),
                           [](const Choice& c) { return c.rule.find("X4") != std::string::npos; }));
    }
}

TEST_CASE("Agente Rakdos: variante X3 ignora Teferi, corsa a ogni orologio", "[agent][teferi][clock][x3]") {
    const RakdosAgentOptions ignore = with_clock(RakdosAgentOptions::kTeferiAlwaysRace);
    SECTION("orologio di 3 turni: la base lo toglie, la variante attacca il giocatore") {
        Driver base = clock_position(12);
        base.run_to_turn(2);
        CHECK(defender_of(base, "Nethergoyf") == "Teferi");
        Driver d = clock_position(12, ignore);
        d.run_to_turn(2);
        CHECK(defender_of(d, "Nethergoyf") == "giocatore");
        CHECK(on_battlefield(d.st.s, kTeferi));
        CHECK(d.st.s.players[kUw].life == 6);  // Bolt in faccia e Nethergoyf
        CHECK(attack_rule_of(d, "Nethergoyf").ends_with(" [corsa]"));
    }
    SECTION("orologio infinito (nessuna creatura): la base usa Bolt su Teferi, la variante in faccia") {
        auto position = [](RakdosAgentOptions o) {
            Driver d(staged_rakdos({"Lightning Bolt"}, {"Blood Crypt"}, {}, {"Plains"}, {kTeferi}), o);
            set_loyalty(d, 3);
            d.run_to_turn(2);
            return d;
        };
        const Driver base = position(r1());
        CHECK(in_zone(base.st.s, kUw, Zone::Graveyard, kTeferi));
        CHECK(base.st.s.players[kUw].life == 20);
        const Driver d = position(ignore);
        CHECK(on_battlefield(d.st.s, kTeferi));
        CHECK(d.st.s.players[kUw].life == 17);
    }
}

TEST_CASE("Agente Rakdos: variante X3, l'orologio con la soglia a 1 e a 3 turni", "[agent][teferi][clock][x3]") {
    SECTION("soglia 3: orologio di 3 turni in corsa (la base, soglia 2, lo toglie)") {
        Driver d = clock_position(12, with_clock(3));
        d.run_to_turn(2);
        CHECK(defender_of(d, "Nethergoyf") == "giocatore");
        CHECK(d.st.s.players[kUw].life == 6);
    }
    SECTION("soglia 3: orologio di 4 turni in partita lunga") {
        Driver d = clock_position(15, with_clock(3));  // (15 - 3) / 3 = 4 turni
        d.run_to_turn(2);
        CHECK(defender_of(d, "Nethergoyf") == "Teferi");
    }
    SECTION("soglia 1: orologio di 2 turni in partita lunga (la base, soglia 2, corre)") {
        Driver d = clock_position(9, with_clock(1));
        d.run_to_turn(2);
        CHECK(defender_of(d, "Nethergoyf") == "Teferi");
        CHECK(attack_rule_of(d, "Nethergoyf").ends_with(" [partita lunga]"));
    }
}

TEST_CASE("Agente Rakdos: i predefiniti sono base_r2 (X1-C, X5, correzione del turno 1); X2, X3, X4 spente",
          "[agent][x2][x3][x4][base_r2]") {
    const RakdosAgentOptions o;
    CHECK(o.mulligan == MulliganVariant::C);
    CHECK(o.thoughtseize_answer_or_threat);
    CHECK(o.t1_seize_before_troll);
    CHECK(o.teferi_race_clock == 2);
    CHECK_FALSE(o.mulligan_keep_disruption);
    CHECK_FALSE(o.t1_thoughtseize_first);
    CHECK_FALSE(o.t1_creature_before_troll);  // X6 (sezione 23)
    CHECK_FALSE(o.bolt_face_asap);
}

TEST_CASE("Agente Rakdos: base_r1 e' l'agente prima del 15/11/2026, le altre opzioni come base_r2", "[agent][base_r2]") {
    const RakdosAgentOptions o = RakdosAgentOptions::base_r1();
    CHECK(o.mulligan == MulliganVariant::B);
    CHECK_FALSE(o.thoughtseize_answer_or_threat);
    CHECK_FALSE(o.t1_seize_before_troll);
    CHECK(o.teferi_race_clock == 2);
    CHECK_FALSE(o.mulligan_keep_disruption);
    CHECK_FALSE(o.t1_thoughtseize_first);
    CHECK_FALSE(o.t1_creature_before_troll);
    CHECK_FALSE(o.bolt_face_asap);
}

// ---------------------------------------------------------------------------
// Sezione 21, variante X5: cosa prende Thoughtseize. Linea guida di Daniele, da confermare o
// smentire con la ricerca ad albero: la risposta piu' forte; in mancanza di risposte la minaccia piu'
// pericolosa (quella che la nostra mano non puo' rimuovere); Teferi dopo tutte le minacce.
//
// Dati Oracle (Scryfall oracle_cards del 9/11/2026, config/cards.json) usati dalla regola:
//   Guide of Souls {W} 1/2, Ocelot Pride {W} 1/1, Phelia, Exuberant Shepherd {1}{W} 2/2,
//   Quantum Riddler {3}{U}{U} 4/6, Starfield Shepherd {3}{W}{W} 3/2, Teferi, Time Raveler {1}{W}{U}.
//   Lightning Bolt: 3 danni a un bersaglio. Flare of Malice: "Each opponent sacrifices a creature or
//   planeswalker with the greatest mana value among creatures and planeswalkers they control."
// ---------------------------------------------------------------------------

namespace {

RakdosAgentOptions with_x5() {
    RakdosAgentOptions o = r1();
    o.thoughtseize_answer_or_threat = true;
    return o;
}

// Secondo turno di Rakdos con una Swamp: Thoughtseize (in `ours`) sulla mano di UW. La carta presa.
std::string seize_pick(std::initializer_list<const char*> ours, std::initializer_list<const char*> uw_hand,
                       std::initializer_list<const char*> uw_field, RakdosAgentOptions o) {
    Driver d(staged_rakdos(ours, {"Swamp"}, {}, uw_hand, uw_field), o);
    d.st.s.turn.turn_number = 3;
    d.run_to_turn(4);
    for (const Choice& c : d.choices) {
        if (c.kind == DecisionKind::ChooseFromHand) {
            CHECK(in_zone(d.st.s, kUw, Zone::Graveyard, c.card));
            if (o.thoughtseize_answer_or_threat) CHECK(c.rule.find("(variante X5)") != std::string::npos);
            return c.card;
        }
    }
    FAIL("Thoughtseize non ha preso nulla");
    return "";
}

}  // namespace

TEST_CASE("Agente Rakdos: variante X5, Sanctifier se non abbiamo Flare", "[agent][thoughtseize][x5]") {
    const auto uw = {"Sanctifier en-Vec", "Solitude", "Plains"};
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, with_x5()) == "Sanctifier en-Vec");
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, r1()) == "Sanctifier en-Vec");  // come la base
}

TEST_CASE("Agente Rakdos: variante X5, con Flare in mano Solitude se UW ha Ephemerate o una rimozione",
          "[agent][thoughtseize][x5]") {
    const auto ours = {"Thoughtseize", "Flare of Malice"};
    SECTION("con Ephemerate (riga b): Solitude; la base prende Sanctifier") {
        const auto uw = {"Sanctifier en-Vec", "Solitude", "Ephemerate"};
        CHECK(seize_pick(ours, uw, {"Plains"}, with_x5()) == "Solitude");
        CHECK(seize_pick(ours, uw, {"Plains"}, r1()) == "Sanctifier en-Vec");
    }
    SECTION("con una rimozione singola (riga c, interpretazione provvisoria): Solitude") {
        for (const char* removal : {"March of Otherworldly Light", "Seam Rip", "Thraben Charm"}) {
            CAPTURE(removal);
            const std::string pick = seize_pick(ours, {"Sanctifier en-Vec", "Solitude", removal}, {"Plains"}, with_x5());
            CHECK(pick == "Solitude");
        }
    }
    SECTION("senza Ephemerate ne' rimozioni (riga d): Sanctifier, come la base") {
        const auto uw = {"Sanctifier en-Vec", "Solitude", "Plains"};
        CHECK(seize_pick(ours, uw, {"Plains"}, with_x5()) == "Sanctifier en-Vec");
        CHECK(seize_pick(ours, uw, {"Plains"}, r1()) == "Sanctifier en-Vec");
    }
}

TEST_CASE("Agente Rakdos: variante X5, Solitude con Ephemerate", "[agent][thoughtseize][x5]") {
    const auto uw = {"Solitude", "Ephemerate", "Plains"};
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, with_x5()) == "Solitude");
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, r1()) == "Solitude");  // come la base
}

TEST_CASE("Agente Rakdos: variante X5, senza risposte la minaccia che non possiamo rimuovere",
          "[agent][thoughtseize][x5]") {
    SECTION("Bolt in mano, UW con Guide e Riddler: Riddler (Guide muore a Bolt); la base prende Guide") {
        const auto uw = {"Guide of Souls", "Quantum Riddler", "Plains"};
        CHECK(seize_pick({"Thoughtseize", "Lightning Bolt"}, uw, {"Plains"}, with_x5()) == "Quantum Riddler");
        CHECK(seize_pick({"Thoughtseize", "Lightning Bolt"}, uw, {"Plains"}, r1()) == "Guide of Souls");
    }
    SECTION("niente rimozione in mano: la prima dell'ordine, Guide; la base prende Ocelot") {
        const auto uw = {"Ocelot Pride", "Guide of Souls", "Quantum Riddler"};
        CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, with_x5()) == "Guide of Souls");
        CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, r1()) == "Ocelot Pride");
    }
    SECTION("Bolt e Flare, campo di UW senza creature: tutte rimovibili, la prima dell'ordine") {
        const auto uw = {"Guide of Souls", "Quantum Riddler", "Plains"};
        CHECK(seize_pick({"Thoughtseize", "Lightning Bolt", "Flare of Malice"}, uw, {"Plains"}, with_x5()) ==
              "Guide of Souls");
    }
    SECTION("Bolt e Flare, Starfield Shepherd (5) in campo: Riddler (5) a pari valore non e' rimovibile") {
        const auto uw = {"Guide of Souls", "Quantum Riddler", "Plains"};
        CHECK(seize_pick({"Thoughtseize", "Lightning Bolt", "Flare of Malice"}, uw, {"Plains", "Starfield Shepherd"},
                         with_x5()) == "Quantum Riddler");
    }
}

TEST_CASE("Agente Rakdos: variante X5, Teferi dopo le minacce", "[agent][thoughtseize][x5]") {
    const auto uw = {"Teferi, Time Raveler", "Guide of Souls", "Plains"};
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, with_x5()) == "Guide of Souls");
    CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, r1()) == "Teferi, Time Raveler");
    SECTION("senza minacce Teferi viene prima del resto (Seam Rip, Ephemerate)") {
        CHECK(seize_pick({"Thoughtseize"}, {"Seam Rip", "Teferi, Time Raveler", "Ephemerate"}, {"Plains"}, with_x5()) ==
              "Teferi, Time Raveler");
    }
}

// X5 predefinita dal 15/11/2026 (sezione 22): linea guida di Daniele (pilota di UW Blink), da confermare
// o smentire con la ricerca ad albero. Flare "rimuove" solo un permanente di valore STRETTAMENTE piu' alto:
// "Each opponent sacrifices a creature or planeswalker with the greatest mana value among creatures and
// planeswalkers they control" (Oracle, Scryfall oracle_cards del 9/11/2026): a pari valore sceglie UW.
TEST_CASE("Agente Rakdos: base_r2 prende con la regola di X5, base_r1 con l'ordine della sezione 6",
          "[agent][thoughtseize][x5][base_r2]") {
    SECTION("Teferi e Guide: base_r2 Guide, base_r1 Teferi") {
        const auto uw = {"Teferi, Time Raveler", "Guide of Souls", "Plains"};
        CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, {}) == "Guide of Souls");
        CHECK(seize_pick({"Thoughtseize"}, uw, {"Plains"}, r1()) == "Teferi, Time Raveler");
    }
    SECTION("Riddler (5) a pari valore con Starfield Shepherd (5) in campo: Flare non lo rimuove, base_r2 lo prende") {
        const auto uw = {"Guide of Souls", "Quantum Riddler", "Plains"};
        const auto ours = {"Thoughtseize", "Lightning Bolt", "Flare of Malice"};
        CHECK(seize_pick(ours, uw, {"Plains", "Starfield Shepherd"}, {}) == "Quantum Riddler");
        CHECK(seize_pick(ours, uw, {"Plains", "Starfield Shepherd"}, r1()) == "Guide of Souls");
    }
}

// Secondo scenario indipendente di Flare "strettamente piu' alto" (la mutazione "maggiore o uguale" faceva
// fallire solo i due casi con Starfield Shepherd). Nell'ordine delle minacce il valore di mana cresce
// (Guide 1, Ocelot 1, Phelia 2, Riddler 5, Shepherd 5), quindi "maggiore" e "maggiore o uguale" scelgono
// diversamente solo se Bolt toglie una minaccia precedente e quella a pari valore resiste a Bolt: solo
// Quantum Riddler (costituzione 6). Qui il permanente di valore 5 in campo e' Solitude, non Shepherd, e
// la minaccia che Bolt toglie e' Ocelot Pride.
//   Solitude {3}{W}{W} 3/2, Ocelot Pride {W} 1/1 (Scryfall oracle_cards del 9/11/2026, config/cards.json).
TEST_CASE("Agente Rakdos: variante X5, Riddler a pari valore con Solitude in campo non e' rimovibile con Flare",
          "[agent][thoughtseize][x5]") {
    const auto ours = {"Thoughtseize", "Lightning Bolt", "Flare of Malice"};
    const auto uw = {"Ocelot Pride", "Quantum Riddler", "Plains"};
    for (const RakdosAgentOptions& o : {with_x5(), RakdosAgentOptions{}}) {
        CHECK(seize_pick(ours, uw, {"Plains", "Solitude"}, o) == "Quantum Riddler");
        // Controllo: senza Solitude in campo Flare toglie Riddler, tutte rimovibili, la prima: Ocelot.
        CHECK(seize_pick(ours, uw, {"Plains"}, o) == "Ocelot Pride");
    }
}
