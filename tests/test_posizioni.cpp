// Le posizioni test della ricerca (docs/ricerca.md, sezione 5.1; tools/posizioni.hpp).
//
// Sono linee guida di Daniele (pilota di UW Blink), da misurare con l'accordo e non da usare come verdetti. Per ognuna:
//  (i)   la giocata indicata e' tra le azioni legali del motore (la linea si gioca fino in fondo);
//  (ii)  cosa fa oggi l'euristico di UW (base_v2), con la regola: alla decisione e nella linea del turno;
//  (iii) una contro-posizione in cui la condizione non vale.
// I test passano senza la ricerca: registrano lo stato di oggi. Quando l'euristico cambiera', cambieranno i (ii).
//
// Oracle (Scryfall, dati del 9/11/2026) delle carte delle linee:
//  - March of Otherworldly Light {X}{W}: "As an additional cost to cast this spell, you may exile any number of white
//    cards from your hand. This spell costs {2} less to cast for each card exiled this way. Exile target artifact,
//    creature, or enchantment with mana value X or less."
//  - Thraben Charm {1}{W}: "Choose one — • Thraben Charm deals damage equal to twice the number of creatures you
//    control to target creature. • Destroy target enchantment. • Exile any number of target players' graveyards."
//  - Quantum Riddler {3}{U}{U} 4/6: "Flying / When this creature enters, draw a card. / As long as you have one or
//    fewer cards in hand, if you would draw one or more cards, you draw that many cards plus one instead. / Warp {1}{U}"
//  - Consign to Memory {U}: "Replicate {1} ... Counter target triggered ability or colorless spell."
//  - Flare of Malice {2}{B}{B}: "You may sacrifice a nontoken black creature rather than pay this spell's mana cost.
//    Each opponent sacrifices a creature or planeswalker with the greatest mana value among creatures and
//    planeswalkers they control."
//  - Phelia, Exuberant Shepherd: "Whenever Phelia attacks, exile up to one other target nonland permanent. At the
//    beginning of the next end step, return that card to the battlefield under its owner's control. ..."
//  - Fear of Missing Out 2/3: "Delirium — Whenever this creature attacks for the first time each turn, if there are
//    four or more card types among cards in your graveyard, untap target creature. After this phase, there is an
//    additional combat phase."
//  - Solitude: "When this creature enters, exile up to one other target creature. That creature's controller gains
//    life equal to its power. Evoke—Exile a white card from your hand."
//  - Ephemerate {W}: "Exile target creature you control, then return it to the battlefield under its owner's
//    control. Rebound"
//  - Recruiter of the Guard: "When this creature enters, you may search your library for a creature card with
//    toughness 2 or less, reveal it, put it into your hand, then shuffle."

#include "goldfish_agent.hpp"
#include "player_view.hpp"
#include "posizioni.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kRakdosGraveyard = 1u << kRakdos;  // Thraben Charm: l'insieme di giocatori bersaglio

const CardDatabase& db() { return db_with_tokens(); }
std::string name_of(const GameState& s, CardId id) { return db().get(s.objects[id].def).name; }

GameState position(const std::string& id) { return test_position(db(), uw_sim_deck(), rakdos_deck(), id).state; }

std::optional<Action> find(const GameState& s, ActionType type, std::string_view card = {}, int value = -1) {
    for (const Action& a : legal(s)) {
        if (a.type != type || (value >= 0 && a.value != value)) continue;
        if (!card.empty() && (a.card == kNoCard || name_of(s, a.card) != card)) continue;
        return a;
    }
    return std::nullopt;
}

bool in_zone(const GameState& s, PlayerId owner, Zone z, std::string_view card) {
    for (std::size_t id = 0; id < kMaxCards; ++id) {
        const CardObject& o = s.objects[id];
        if (o.owner == owner && o.zone == z && db().get(o.def).name == card) return true;
    }
    return false;
}

bool top_is(const GameState& s, StackEffect e) { return !s.stack_items.empty() && s.stack_items.back().effect == e; }

using Chooser = std::function<std::optional<Action>(const GameState&)>;

// Avanza dalla posizione: `choose` decide (nullopt: l'avversario del goldfish, che passa, non attacca e non blocca),
// finche' `done` non e' vero. Invarianti dopo ogni azione.
void run(GameState& s, const std::function<bool(const GameState&)>& done, const Chooser& choose) {
    const GoldfishAgent idle;
    for (int i = 0; i < 600 && !done(s); ++i) {
        REQUIRE(s.pending.kind != DecisionKind::GameOver);
        const auto actions = legal(s);
        const std::optional<Action> a = choose(s);
        const Action chosen = a ? *a : idle.choose(s, actions);
        apply(s, db(), chosen);
        require_invariants(s);
    }
    REQUIRE(done(s));
}

bool turn_reached(const GameState& s, int t) { return s.turn.turn_number >= t; }

// Priorita' di UW con la pila vuota e nessun trigger in attesa: la linea si e' risolta.
bool settled(const GameState& s) {
    return s.pending.player == kUw && s.pending.kind == DecisionKind::Priority && s.stack_items.empty() &&
           s.pending_triggers.empty();
}

// --- (ii): l'euristico ---------------------------------------------------------------------------------------

struct Pick {
    Action action;
    std::string rule;
};

// La scelta dell'euristico di UW (base_v2) alla decisione della posizione.
Pick heuristic_choice(const GameState& s, UwAgentOptions options = {}) {
    UwAgent agent(db(), 7, options);
    const Action a = agent.choose(s, legal(s));
    return {a, std::string(agent.last_rule())};
}

// La linea degli euristici (UW base_v2 contro Rakdos base_r3) fino alla fine del turno dopo: le scelte di UW che non
// sono "passa", con la regola, e lo stato finale.
struct Line {
    std::vector<std::string> rules;
    std::vector<std::string> cards;
    GameState end;

    [[nodiscard]] bool used(std::string_view rule) const {
        return std::find(rules.begin(), rules.end(), rule) != rules.end();
    }
    [[nodiscard]] bool cast(std::string_view card) const {
        return std::find(cards.begin(), cards.end(), card) != cards.end();
    }
};

Line heuristic_line(const GameState& start) {
    Line line;
    line.end = start;
    GameState& s = line.end;
    UwAgent uw(db(), 7);
    RakdosAgent rakdos(db(), 11);
    const int end = s.turn.turn_number + 2;
    for (int i = 0; i < 600 && s.pending.kind != DecisionKind::GameOver && s.turn.turn_number < end; ++i) {
        const auto actions = legal(s);
        if (s.pending.player == kUw) {
            const Action a = uw.choose(s, actions);
            if (a.type != ActionType::Pass) {
                line.rules.emplace_back(uw.last_rule());
                if (a.type == ActionType::CastSpell) line.cards.push_back(name_of(s, a.card));
            }
            apply(s, db(), a);
        } else {
            apply(s, db(), rakdos.choose(s, actions));
        }
    }
    return line;
}

constexpr const char* kNothingNow = "priorita': niente da fare ora";

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 1. Moonshadow appena lanciato, 2 mana, March e Solitude in mano: March con X=1 (specifica)
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 1: March con X=1 su Moonshadow", "[posizioni]") {
    SECTION("(i) March con X=1 senza esiliare carte e' legale; Solitude resta in mano") {
        GameState s = position("1");
        REQUIRE(find(s, ActionType::CastSpell, "March of Otherworldly Light"));
        bool x1 = true, stop = false;  // X: la decisione c'e' solo con piu' valori possibili (qui X=1 e' l'unico)
        apply(s, db(), *find(s, ActionType::CastSpell, "March of Otherworldly Light"));
        run(s, settled,
            [&](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseX) {
                    x1 = find(g, ActionType::ChooseOption, {}, 1).has_value();
                    return find(g, ActionType::ChooseOption, {}, 1);
                }
                if (g.pending.kind == DecisionKind::ChooseTarget) return find(g, ActionType::TargetObject, "Moonshadow");
                if (g.pending.kind == DecisionKind::ExileFromHandForCost) {
                    stop = find(g, ActionType::ChooseOption, {}, kStopExiling).has_value();
                    return find(g, ActionType::ChooseOption, {}, kStopExiling);
                }
                return std::nullopt;
            });
        CHECK(x1);
        CHECK(stop);
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Moonshadow"));
        CHECK(in_zone(s, kUw, Zone::Hand, "Solitude"));
    }
    SECTION("(ii) l'euristico passa ora e lancia March con X=1 nella sottofase finale di Rakdos") {
        const GameState s = position("1");
        const Pick p = heuristic_choice(s);
        CHECK(p.action.type == ActionType::Pass);
        CHECK(p.rule == kNothingNow);
        const Line line = heuristic_line(s);
        CHECK(line.used("March: sottofase finale avversaria"));
        CHECK(line.used("March: nessuna carta da esiliare (basta il mana)"));
        CHECK(in_zone(line.end, kRakdos, Zone::Exile, "Moonshadow"));
        CHECK(in_zone(line.end, kUw, Zone::Hand, "Solitude"));
    }
    SECTION("(iii) con un solo mana March con X=1 costa anche Solitude; l'euristico evoca Solitude") {
        GameState s = position("1c");
        apply(s, db(), *find(s, ActionType::CastSpell, "March of Otherworldly Light"));
        run(s, settled,
            [&](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseX) return find(g, ActionType::ChooseOption, {}, 1);
                if (g.pending.kind == DecisionKind::ChooseTarget) return find(g, ActionType::TargetObject, "Moonshadow");
                if (g.pending.kind == DecisionKind::ExileFromHandForCost) {
                    CHECK_FALSE(find(g, ActionType::ChooseOption, {}, kStopExiling));
                }
                return std::nullopt;
            });
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Moonshadow"));
        CHECK(in_zone(s, kUw, Zone::Exile, "Solitude"));  // il costo: la carta bianca
        const Line line = heuristic_line(position("1c"));
        CHECK(line.used("Solitude: sottofase finale avversaria"));
        CHECK_FALSE(line.cast("March of Otherworldly Light"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 2. Sanctifier en-Vec in mano al turno 2, cimitero avversario quasi vuoto: lanciarlo subito (specifica)
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 2: Sanctifier en-Vec subito", "[posizioni]") {
    for (const char* id : {"2", "2c"}) {
        DYNAMIC_SECTION("posizione " << id) {
            const GameState s = position(id);
            REQUIRE(find(s, ActionType::CastSpell, "Sanctifier en-Vec"));  // (i)
            const Pick p = heuristic_choice(s);                           // (ii) e (iii): la stessa scelta
            CHECK(p.action == *find(s, ActionType::CastSpell, "Sanctifier en-Vec"));
            CHECK(p.rule == "Sanctifier en-Vec: l'avversario ha mostrato il nero o il rosso");
        }
    }
    SECTION("(iii) con il delirium gia' fatto Sanctifier lascia terra e artefatto nel cimitero") {
        GameState s = position("2c");
        apply(s, db(), *find(s, ActionType::CastSpell, "Sanctifier en-Vec"));
        run(s, settled,
            [](const GameState&) { return std::nullopt; });
        CHECK(s.players[kRakdos].gy.distinct_types() == 2);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 3. Avversario a un tipo dal delirium, Thraben Charm in mano: esiliare il suo cimitero (specifica)
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 3: Thraben Charm esilia il cimitero a un tipo dal delirium", "[posizioni]") {
    SECTION("(i) Thraben Charm, modo 'esilia i cimiteri', sul cimitero di Rakdos") {
        GameState s = position("3");
        REQUIRE(find(s, ActionType::CastSpell, "Thraben Charm"));
        apply(s, db(), *find(s, ActionType::CastSpell, "Thraben Charm"));
        run(s, settled,
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseMode) return find(g, ActionType::ChooseOption, {}, 2);
                if (g.pending.kind == DecisionKind::ChooseTarget) {
                    return find(g, ActionType::ChooseOption, {}, kRakdosGraveyard);
                }
                return std::nullopt;
            });
        CHECK(s.players[kRakdos].graveyard.empty());
    }
    SECTION("(ii) l'euristico passa ora e lo lancia quando Nethergoyf attacca") {
        const GameState s = position("3");
        CHECK(heuristic_choice(s).rule == kNothingNow);
        const Line line = heuristic_line(s);
        CHECK(line.used("Thraben Charm: Nethergoyf in combattimento"));
        CHECK(line.end.players[kRakdos].graveyard.empty());
    }
    SECTION("(iii) con un tipo solo nel cimitero l'euristico non lo lancia") {
        const GameState s = position("3c");
        CHECK(find(s, ActionType::CastSpell, "Thraben Charm"));
        CHECK(heuristic_choice(s).rule == kNothingNow);
        CHECK_FALSE(heuristic_line(s).cast("Thraben Charm"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 4. Teferi in mano con 3 mana, avversario con il rosso aperto: Teferi prima di attaccare (specifica)
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 4: Teferi prima di attaccare", "[posizioni]") {
    for (const char* id : {"4", "4c"}) {
        DYNAMIC_SECTION("posizione " << id) {
            const GameState s = position(id);
            REQUIRE(s.turn.step == Step::Main1);
            REQUIRE(find(s, ActionType::CastSpell, "Teferi, Time Raveler"));  // (i): nella fase principale prima del combattimento
            const Pick p = heuristic_choice(s);  // (ii) e (iii): la stessa scelta, con o senza rosso aperto
            CHECK(p.action == *find(s, ActionType::CastSpell, "Teferi, Time Raveler"));
            CHECK(p.rule == "Teferi: prima del combattimento");
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 5. Phoenix conferita su un Nethergoyf grande, Solitude in mano: esiliare Nethergoyf (specifica)
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 5: Solitude esilia Nethergoyf con la Phoenix conferita", "[posizioni]") {
    auto evoke_on = [](GameState& s, const char* target) {
        apply(s, db(), *find(s, ActionType::CastSpell, "Solitude", static_cast<int>(CastMethod::Evoke)));
        run(s, settled,
            [&](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ExileFromHandForCost) return find(g, ActionType::ChooseCard, "Witch Enchanter");
                if (g.pending.kind == DecisionKind::ChooseTarget && top_is(g, StackEffect::ExileOtherCreature)) {
                    return find(g, ActionType::TargetObject, target);
                }
                return std::nullopt;
            });
    };
    SECTION("(i) Solitude evocata su Nethergoyf: la Phoenix resta come creatura") {
        GameState s = position("5");
        REQUIRE(find(s, ActionType::CastSpell, "Solitude", static_cast<int>(CastMethod::Evoke)));
        evoke_on(s, "Nethergoyf");
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Nethergoyf"));
        const ObjRef phoenix = find_on_battlefield(s, "Detective's Phoenix");
        CHECK_FALSE(s.objects[phoenix.card].has_flag(ObjectFlag::Bestowed));
    }
    SECTION("(ii) l'euristico passa ora e la evoca quando Nethergoyf attacca") {
        const GameState s = position("5");
        CHECK(heuristic_choice(s).rule == kNothingNow);
        const Line line = heuristic_line(s);
        CHECK(line.used("Solitude: la minaccia che attacca"));
        CHECK(in_zone(line.end, kRakdos, Zone::Exile, "Nethergoyf"));
    }
    SECTION("(iii) Phoenix su Stitcher's Supplier: entrambi bersagli legali, l'euristico esilia Nethergoyf") {
        GameState s = position("5c");
        evoke_on(s, "Stitcher's Supplier");
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
        const Line line = heuristic_line(position("5c"));
        CHECK(line.used("Solitude: la minaccia che attacca"));
        CHECK(in_zone(line.end, kRakdos, Zone::Exile, "Nethergoyf"));
        CHECK(in_zone(line.end, kRakdos, Zone::Battlefield, "Stitcher's Supplier"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 6. Riddler e Consign, tre terre con due fonti blu: Riddler con il warp, {U} aperto, Consign sul trigger di esilio
// ---------------------------------------------------------------------------------------------------------------

namespace {

bool untapped_blue_land(const GameState& s) {
    for (const CardId id : s.battlefield) {
        const std::string n = name_of(s, id);
        if (s.objects[id].controller == kUw && (n == "Hallowed Fountain" || n == "Meticulous Archive") &&
            !s.objects[id].has_flag(ObjectFlag::Tapped)) {
            return true;
        }
    }
    return false;
}

// Il pagamento del warp che lascia stappata una fonte blu.
std::optional<Action> pay_keeping_blue(const GameState& s) {
    for (const Action& a : legal(s)) {
        GameState t = s;
        apply(t, db(), a);
        if (untapped_blue_land(t)) return a;
    }
    return std::nullopt;
}

// Riddler con il warp, pagato tenendo il blu, fino alla risoluzione (UW con la priorita' e la pila vuota).
void warp_riddler(GameState& s) {
    apply(s, db(), *find(s, ActionType::CastSpell, "Quantum Riddler", static_cast<int>(CastMethod::Warp)));
    run(s, settled,
        [](const GameState& g) -> std::optional<Action> {
            if (g.pending.kind == DecisionKind::PayMana) return pay_keeping_blue(g);
            return std::nullopt;
        });
}

}  // namespace

TEST_CASE("Posizione 6: Riddler con il warp e Consign sul trigger di esilio", "[posizioni]") {
    SECTION("(i) la linea e' legale: Riddler resta, puo' bloccare al turno di Rakdos e ha pescato") {
        GameState s = position("6");
        REQUIRE(find(s, ActionType::CastSpell, "Quantum Riddler", static_cast<int>(CastMethod::Warp)));
        warp_riddler(s);
        CHECK(untapped_blue_land(s));
        CHECK(s.players[kUw].hand.size() == 3);  // Consign, Teferi e la carta pescata dall'entrata
        run(s, [](const GameState& g) { return g.pending.player == kUw && top_is(g, StackEffect::WarpExile); },
            [](const GameState&) { return std::nullopt; });
        apply(s, db(), *find(s, ActionType::CastSpell, "Consign to Memory"));
        run(s, [](const GameState& g) { return turn_reached(g, 6); },
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseTarget && g.pending.player == kUw) {
                    for (const Action& a : legal(g)) {
                        if (a.type == ActionType::TargetStackItem && g.stack_items[a.value].effect == StackEffect::WarpExile) return a;
                    }
                }
                return std::nullopt;
            });
        const ObjRef riddler = find_on_battlefield(s, "Quantum Riddler");
        CHECK_FALSE(s.objects[riddler.card].has_flag(ObjectFlag::Tapped));  // un bloccante volante 4/6
        CHECK(in_zone(s, kUw, Zone::Graveyard, "Consign to Memory"));
        CHECK(s.players[kUw].hand.size() == 2);
    }
    SECTION("(ii) l'euristico gioca la linea (W1)") {
        const GameState s = position("6");
        const Pick p = heuristic_choice(s);
        CHECK(p.action.value == static_cast<std::uint8_t>(CastMethod::Warp));
        CHECK(p.rule == "Quantum Riddler: warp con Consign per l'esilio (variante W1)");
        const Line line = heuristic_line(s);
        CHECK(line.used("Consign to Memory: l'esilio del warp"));
        CHECK(in_zone(line.end, kUw, Zone::Battlefield, "Quantum Riddler"));
    }
    SECTION("(iii) Flare of Malice nota e quattro mana: Consign non la contrasta e Riddler viene sacrificato") {
        GameState s = position("6c");
        warp_riddler(s);
        // Rakdos lancia Flare a costo pieno appena ha la priorita' (dopo che UW passa, con Riddler in campo).
        run(s, [](const GameState& g) {
                return is_decision(g, DecisionKind::Priority, kRakdos) &&
                       find(g, ActionType::CastSpell, "Flare of Malice", static_cast<int>(CastMethod::Normal));
            },
            [](const GameState&) { return std::nullopt; });
        apply(s, db(), *find(s, ActionType::CastSpell, "Flare of Malice", static_cast<int>(CastMethod::Normal)));
        // Consign in mano e {U} aperto, ma nessun bersaglio legale (Flare e' colorata): UW non riceve nemmeno la
        // priorita' (solo "passa"), Flare si risolve e il sacrificio (una sola creatura) e' automatico.
        CHECK(s.stack_items.empty());
        CHECK(in_zone(s, kUw, Zone::Hand, "Consign to Memory"));
        CHECK(untapped_blue_land(s));
        CHECK(in_zone(s, kRakdos, Zone::Graveyard, "Flare of Malice"));
        CHECK(in_zone(s, kUw, Zone::Graveyard, "Quantum Riddler"));
        // L'euristico non guarda Flare: gioca W1 anche qui, e Rakdos sacrifica Riddler nella sottofase finale.
        const GameState c = position("6c");
        CHECK(heuristic_choice(c).rule == "Quantum Riddler: warp con Consign per l'esilio (variante W1)");
        const Line line = heuristic_line(c);
        CHECK(line.used("Consign to Memory: l'esilio del warp"));
        CHECK_FALSE(in_zone(line.end, kUw, Zone::Battlefield, "Quantum Riddler"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 7. Phelia ha esiliato un permanente avversario; Consign e {U} aperto: Consign sul ritorno
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 7: Consign to Memory sul ritorno di Phelia", "[posizioni]") {
    for (const auto& [id, card] : {std::pair{"7a", "Nethergoyf"}, std::pair{"7c", "Stitcher's Supplier"}}) {
        DYNAMIC_SECTION("posizione " << id << ": " << card) {
            GameState s = position(id);
            REQUIRE(in_zone(s, kRakdos, Zone::Exile, card));
            // (i) Consign sul ritorno: il permanente resta in esilio.
            REQUIRE(find(s, ActionType::CastSpell, "Consign to Memory"));
            GameState t = s;
            apply(t, db(), *find(t, ActionType::CastSpell, "Consign to Memory"));
            run(t, [](const GameState& g) { return turn_reached(g, 2); },
                [](const GameState& g) -> std::optional<Action> {
                    if (g.pending.kind == DecisionKind::ChooseTarget && g.pending.player == kUw) {
                        for (const Action& a : legal(g)) {
                            if (a.type == ActionType::TargetStackItem && g.stack_items[a.value].effect == StackEffect::PheliaReturn) {
                                return a;
                            }
                        }
                    }
                    return std::nullopt;
                });
            CHECK(in_zone(t, kRakdos, Zone::Exile, card));
            // (ii) e (iii): l'euristico la lancia sempre, anche sul pezzo piccolo (D-15).
            const Pick p = heuristic_choice(s);
            CHECK(p.action == *find(s, ActionType::CastSpell, "Consign to Memory"));
            CHECK(p.rule == "Consign to Memory: il ritorno di Phelia, esilio per sempre");
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 8. Fear of Missing Out con il delirium e Thraben Charm
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 8: Thraben Charm contro Fear of Missing Out", "[posizioni]") {
    SECTION("8a (i): Thraben Charm sui danni (4: due creature di UW) uccide FOMO prima dell'attacco") {
        GameState s = position("8a");
        REQUIRE(find(s, ActionType::CastSpell, "Thraben Charm"));
        apply(s, db(), *find(s, ActionType::CastSpell, "Thraben Charm"));
        run(s, settled,
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseMode) return find(g, ActionType::ChooseOption, {}, 0);
                if (g.pending.kind == DecisionKind::ChooseTarget) return find(g, ActionType::TargetObject, "Fear of Missing Out");
                return std::nullopt;
            });
        CHECK(in_zone(s, kRakdos, Zone::Graveyard, "Fear of Missing Out"));
    }
    SECTION("8a (ii): l'euristico aspetta il trigger e poi esilia il cimitero") {
        const GameState s = position("8a");
        CHECK(heuristic_choice(s).rule == kNothingNow);
        const Line line = heuristic_line(s);
        CHECK(line.used("Thraben Charm: esilia il cimitero in risposta al trigger di FOMO"));
        CHECK(in_zone(line.end, kRakdos, Zone::Battlefield, "Fear of Missing Out"));
    }
    SECTION("8b (i) e (ii): con il trigger in pila, l'esilio dei cimiteri toglie il combattimento in piu'") {
        GameState s = position("8b");
        const Pick p = heuristic_choice(s);
        CHECK(p.action == *find(s, ActionType::CastSpell, "Thraben Charm"));
        CHECK(p.rule == "Thraben Charm: esilia il cimitero in risposta al trigger di FOMO");
        apply(s, db(), *find(s, ActionType::CastSpell, "Thraben Charm"));
        run(s, [](const GameState& g) { return turn_reached(g, 2); },
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.player != kUw) return std::nullopt;
                if (g.pending.kind == DecisionKind::ChooseMode) return find(g, ActionType::ChooseOption, {}, 2);
                if (g.pending.kind == DecisionKind::ChooseTarget) return find(g, ActionType::ChooseOption, {}, kRakdosGraveyard);
                return std::nullopt;
            });
        CHECK(s.players[kUw].life == 18);  // un solo attacco di FOMO (2)
    }
    SECTION("8c (iii): senza delirium l'attacco di FOMO non innesca niente e l'euristico non lancia il Charm") {
        GameState s = position("8c");
        bool trigger = false;
        run(s, [](const GameState& g) { return turn_reached(g, 2); },
            [&](const GameState& g) -> std::optional<Action> {
                trigger = trigger || top_is(g, StackEffect::UntapAndExtraCombat);
                if (g.pending.player == kRakdos && g.pending.kind == DecisionKind::DeclareAttacker) {
                    return find(g, ActionType::TargetPlayer);
                }
                return std::nullopt;
            });
        CHECK_FALSE(trigger);
        CHECK_FALSE(heuristic_line(position("8c")).cast("Thraben Charm"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 9. Solitude ed Ephemerate contro due creature
// ---------------------------------------------------------------------------------------------------------------

namespace {

// Solitude evocata (esiliando Ocelot Pride) su Nethergoyf, il sacrificio messo in pila per primo (si risolve per
// ultimo), fino a Ephemerate su Solitude con il sacrificio ancora in pila. Si ferma con Ephemerate in pila e la
// priorita' a Rakdos, oppure (se `finish`) alla fine del turno.
void solitude_line(GameState& s, bool finish) {
    apply(s, db(), *find(s, ActionType::CastSpell, "Solitude", static_cast<int>(CastMethod::Evoke)));
    bool ephemerate = false;
    run(s,
        [&](const GameState& g) {
            return finish ? turn_reached(g, 2)
                          : ephemerate && g.pending.player == kRakdos && g.pending.kind == DecisionKind::Priority;
        },
        [&](const GameState& g) -> std::optional<Action> {
            if (g.pending.player != kUw) return std::nullopt;
            switch (g.pending.kind) {
                case DecisionKind::ExileFromHandForCost: return find(g, ActionType::ChooseCard, "Ocelot Pride");
                case DecisionKind::OrderTriggers:
                    for (const Action& a : legal(g)) {
                        if (g.pending_triggers[a.value].effect == StackEffect::EvokeSacrifice) return a;
                    }
                    return std::nullopt;
                case DecisionKind::ChooseTarget:
                    if (top_is(g, StackEffect::ExileOtherCreature)) {
                        if (auto a = find(g, ActionType::TargetObject, "Nethergoyf")) return a;
                        return find(g, ActionType::TargetObject, "Dark Matter Manipulator");
                    }
                    return find(g, ActionType::TargetObject, "Solitude");  // Ephemerate
                case DecisionKind::Priority:
                    if (!ephemerate && top_is(g, StackEffect::EvokeSacrifice)) {
                        ephemerate = true;
                        return find(g, ActionType::CastSpell, "Ephemerate");
                    }
                    return std::nullopt;
                default: return std::nullopt;
            }
        });
}

}  // namespace

TEST_CASE("Posizione 9: Solitude evocata ed Ephemerate con il sacrificio in pila", "[posizioni]") {
    SECTION("9a (i): due esili, Solitude resta") {
        GameState s = position("9a");
        solitude_line(s, true);
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Nethergoyf"));
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Dark Matter Manipulator"));
        CHECK(in_zone(s, kUw, Zone::Battlefield, "Solitude"));
    }
    SECTION("9a (ii): l'euristico passa ora e gioca la linea quando le creature attaccano") {
        const GameState s = position("9a");
        CHECK(heuristic_choice(s).rule == kNothingNow);
        const Line line = heuristic_line(s);
        CHECK(line.used("Solitude: la minaccia che attacca"));
        CHECK(line.used("Ephemerate: Solitude evocata, in risposta al sacrificio"));
        CHECK(in_zone(line.end, kRakdos, Zone::Exile, "Dark Matter Manipulator"));
    }
    SECTION("9b (iii): con Bolt e il rosso aperto Rakdos puo' rispondere a Ephemerate e la linea fallisce") {
        GameState s = position("9b");
        solitude_line(s, false);
        REQUIRE(find(s, ActionType::CastSpell, "Lightning Bolt"));
        apply(s, db(), *find(s, ActionType::CastSpell, "Lightning Bolt"));
        run(s, [](const GameState& g) { return turn_reached(g, 2); },
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.player == kRakdos && g.pending.kind == DecisionKind::ChooseTarget) {
                    return find(g, ActionType::TargetObject, "Solitude");
                }
                return std::nullopt;
            });
        CHECK(in_zone(s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(in_zone(s, kRakdos, Zone::Battlefield, "Dark Matter Manipulator"));
        // L'euristico di UW non guarda Bolt: stessa linea; quello di Rakdos non risponde a Ephemerate (Bolt dopo).
        const Line line = heuristic_line(position("9b"));
        CHECK(line.used("Ephemerate: Solitude evocata, in risposta al sacrificio"));
    }
}

TEST_CASE("Posizione 9b: Bolt nei mondi in proporzione alle copie rimaste, mai se la mano e' nota senza Bolt",
          "[posizioni][vista][determinizzazione]") {
    const CardDefId bolt = db().id_of("Lightning Bolt");
    GameState s = position("9b");
    REQUIRE(s.players[kRakdos].hand.size() == 1);
    SECTION("mano nascosta: la frequenza di Bolt in mano e' copie rimaste / carte nascoste") {
        const PlayerView v(s, db(), kUw);
        const std::vector<CardDefId> pool = v.hidden_defs(kRakdos);
        const auto m = static_cast<double>(std::count(pool.begin(), pool.end(), bolt));
        const auto n = static_cast<double>(pool.size());
        REQUIRE(m == 2);  // tre copie, una nel cimitero
        constexpr int kWorlds = 20000;
        int with_bolt = 0;
        for (int w = 1; w <= kWorlds; ++w) {
            const GameState world = v.sample_world(static_cast<std::uint64_t>(w));
            with_bolt += world.objects[world.players[kRakdos].hand[0]].def == bolt ? 1 : 0;
        }
        const double p = m / n;
        const double z = (with_bolt - kWorlds * p) / std::sqrt(kWorlds * p * (1 - p));
        INFO(with_bolt << " mondi con Bolt su " << kWorlds << ", atteso " << kWorlds * p << ", z = " << z);
        CHECK(std::abs(z) < 5.0);
    }
    SECTION("mano rivelata senza Bolt (come dopo Thoughtseize): zero mondi con Bolt in mano") {
        // Lo stato vero diverso: la carta in mano e' un Thoughtseize del grimorio (scambio di identita'), e la mano
        // e' nota a UW come dopo una rivelazione.
        const CardId hand = s.players[kRakdos].hand[0];
        for (const CardId id : s.players[kRakdos].library) {
            if (s.objects[id].def == db().id_of("Thoughtseize")) {
                std::swap(s.objects[id].def, s.objects[hand].def);
                break;
            }
        }
        s.objects[hand].known_to = (1u << kNumPlayers) - 1u;
        const PlayerView v(s, db(), kUw);
        for (int w = 1; w <= 200; ++w) {
            const GameState world = v.sample_world(static_cast<std::uint64_t>(w));
            CHECK(world.objects[world.players[kRakdos].hand[0]].def == db().id_of("Thoughtseize"));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 10. Recruiter of the Guard al terzo turno
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Posizione 10: cosa cerca Recruiter of the Guard", "[posizioni]") {
    SECTION("10a (i): Recruiter trova Solitude e la si evoca subito su Nethergoyf") {
        GameState s = position("10a");
        REQUIRE(find(s, ActionType::ChooseCard, "Solitude"));
        apply(s, db(), *find(s, ActionType::ChooseCard, "Solitude"));
        run(s, settled,
            [](const GameState&) { return std::nullopt; });
        REQUIRE(find(s, ActionType::CastSpell, "Solitude", static_cast<int>(CastMethod::Evoke)));
        apply(s, db(), *find(s, ActionType::CastSpell, "Solitude", static_cast<int>(CastMethod::Evoke)));
        run(s, settled,
            [](const GameState& g) -> std::optional<Action> {
                if (g.pending.kind == DecisionKind::ChooseTarget) return find(g, ActionType::TargetObject, "Nethergoyf");
                return std::nullopt;
            });
        CHECK(in_zone(s, kRakdos, Zone::Exile, "Nethergoyf"));
        CHECK(in_zone(s, kUw, Zone::Exile, "Ephemerate"));  // la carta bianca dell'evocazione
    }
    SECTION("(i) le ricerche indicate sono tra le azioni legali") {
        CHECK(find(position("10b"), ActionType::ChooseCard, "Sanctifier en-Vec"));
        const GameState c = position("10c");
        CHECK(find(c, ActionType::ChooseCard, "Sanctifier en-Vec"));  // "altro" della contro-posizione
    }
    SECTION("(ii) e (iii): oggi l'euristico cerca Sanctifier in tutte e tre (D-2c)") {
        for (const char* id : {"10a", "10b", "10c"}) {
            const GameState s = position(id);
            const Pick p = heuristic_choice(s);
            INFO("posizione " << id);
            CHECK(p.action == *find(s, ActionType::ChooseCard, "Sanctifier en-Vec"));
            CHECK(p.rule == "Recruiter: Sanctifier en-Vec");
        }
    }
}

TEST_CASE("Posizioni: ogni posizione e' una decisione di UW con almeno due azioni legali", "[posizioni]") {
    for (const std::string& id : test_position_ids()) {
        INFO("posizione " << id);
        const TestPosition p = test_position(db(), uw_sim_deck(), rakdos_deck(), id);
        CHECK(p.state.pending.player == kUw);
        CHECK(legal(p.state).size() >= 2);
        CHECK_FALSE(p.situation.empty());
        CHECK_FALSE(p.indicated.empty());
        require_invariants(p.state);
        CHECK_NOTHROW(PlayerView(p.state, db(), kUw));
    }
    CHECK_THROWS_AS(test_position(db(), uw_sim_deck(), rakdos_deck(), "11"), EngineError);
}

// ---------------------------------------------------------------------------------------------------------------
// Variante R-REC dell'euristico di UW (docs/agente_uw.md, sezione 30): spenta per default
// ---------------------------------------------------------------------------------------------------------------

namespace {

UwAgentOptions with_r_rec() {
    UwAgentOptions o = UwAgentOptions::base_v1();
    o.recruiter_threat_or_graveyard = true;
    return o;
}

}  // namespace

TEST_CASE("Agente UW, variante R-REC: Recruiter cerca Solitude con una minaccia in campo", "[uwagent][varianti][rrec]") {
    const GameState s = position("10a");
    const Pick on = heuristic_choice(s, with_r_rec());
    CHECK(on.action == *find(s, ActionType::ChooseCard, "Solitude"));
    CHECK(on.rule == "Recruiter: Solitude contro una minaccia (variante R-REC)");
    const Pick off = heuristic_choice(s, UwAgentOptions::base_v1());
    CHECK(off.action == *find(s, ActionType::ChooseCard, "Sanctifier en-Vec"));
    CHECK(off.rule == "Recruiter: Sanctifier en-Vec");
}

TEST_CASE("Agente UW, variante R-REC: senza minacce e con due tipi nel cimitero cerca Sanctifier en-Vec",
          "[uwagent][varianti][rrec]") {
    const GameState s = position("10b");
    const Pick on = heuristic_choice(s, with_r_rec());
    CHECK(on.action == *find(s, ActionType::ChooseCard, "Sanctifier en-Vec"));
    CHECK(on.rule == "Recruiter: Sanctifier en-Vec, nessuna minaccia (variante R-REC)");
}

TEST_CASE("Agente UW, variante R-REC: negli altri casi la regola di oggi", "[uwagent][varianti][rrec]") {
    SECTION("Solitude gia' in mano, con una minaccia") {
        const GameState s = position("10c");
        const Pick on = heuristic_choice(s, with_r_rec());
        CHECK(on.action == *find(s, ActionType::ChooseCard, "Sanctifier en-Vec"));
        CHECK(on.rule == "Recruiter: Sanctifier en-Vec");
    }
    SECTION("nessuna minaccia e un tipo solo nel cimitero") {
        GameState s = position("10b");
        // Il cimitero di Rakdos si riduce alla Swamp: un tipo.
        for (const char* card : {"Lightning Bolt", "Thoughtseize", "Mishra's Bauble"}) {
            move_object(s, db(), find_cards(s, kRakdos, Zone::Graveyard, card)[0], Zone::Exile, Cause::Test);
        }
        require_invariants(s);
        REQUIRE(s.players[kRakdos].gy.distinct_types() == 1);
        const Pick on = heuristic_choice(s, with_r_rec());
        CHECK(on.rule == "Recruiter: Sanctifier en-Vec");  // la sezione 15: Rakdos ha mostrato il nero
    }
    SECTION("Stitcher's Supplier non e' una minaccia") {
        GameState s = position("10b");
        move_object(s, db(), find_cards(s, kRakdos, Zone::Library, "Stitcher's Supplier")[0], Zone::Battlefield, Cause::Test);
        require_invariants(s);
        const Pick on = heuristic_choice(s, with_r_rec());
        CHECK(on.rule == "Recruiter: Sanctifier en-Vec, nessuna minaccia (variante R-REC)");
    }
}

TEST_CASE("Agente UW, variante R-REC: spenta per default e in base_v1", "[uwagent][varianti][rrec]") {
    CHECK_FALSE(UwAgentOptions{}.recruiter_threat_or_graveyard);
    CHECK_FALSE(UwAgentOptions::base_v1().recruiter_threat_or_graveyard);
    // Spenta: le scelte delle posizioni 10 sono quelle della sezione 15.
    for (const char* id : {"10a", "10b", "10c"}) CHECK(heuristic_choice(position(id)).rule == "Recruiter: Sanctifier en-Vec");
}
