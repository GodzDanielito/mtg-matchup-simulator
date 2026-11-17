// La vista del giocatore e la determinizzazione (docs/ricerca.md, sezioni 3.3 e 3.5; passo 1 della ricerca).
//
// Test della sezione 3.5: (a) gemello per la determinizzazione, (c) carte note, (d) distribuzione ipergeometrica,
// (e) il proprio grimorio, (f) invarianti. Il test (b), gemello per la scelta, arriva con la ricerca (passo 2).

#include "gemello.hpp"
#include "matchup.hpp"
#include "player_view.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kAllPlayers = (1u << kNumPlayers) - 1u;  // come nel motore: nota a tutti

const CardDatabase& db() { return db_with_tokens(); }
std::string name_of(CardDefId d) { return db().get(d).name; }

// Il gemello (twin_of) e' in tests/gemello.hpp: lo usa anche il test (b) del giocatore forte (test_ricerca.cpp).

// Il primo campo diverso tra due stati, per leggere un fallimento.
std::string first_difference(const GameState& a, const GameState& b) {
    for (std::size_t i = 0; i < kMaxObjects; ++i) {
        const CardObject& x = a.objects[i];
        const CardObject& y = b.objects[i];
        if (x == y) continue;
        std::ostringstream o;
        o << "oggetto " << i << ": def " << x.def << "/" << y.def << ", zona " << static_cast<int>(x.zone) << "/"
          << static_cast<int>(y.zone) << ", incarnation " << x.incarnation << "/" << y.incarnation << ", zone_turn "
          << x.zone_turn << "/" << y.zone_turn << ", known_to " << static_cast<int>(x.known_to) << "/"
          << static_cast<int>(y.known_to) << ", timestamp " << x.timestamp << "/" << y.timestamp;
        return o.str();
    }
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        const PlayerState& x = a.players[p];
        const PlayerState& y = b.players[p];
        if (x == y) continue;
        if (!(x.library == y.library)) return "grimorio del giocatore " + std::to_string(p);
        if (!(x.hand == y.hand)) return "mano del giocatore " + std::to_string(p);
        if (!(x.rng == y.rng)) return "generatore del giocatore " + std::to_string(p);
        return "giocatore " + std::to_string(p) + " (altro campo)";
    }
    if (!(a.pending == b.pending)) return "decisione in sospeso";
    if (!(a.turn == b.turn)) return "turno";
    if (!(a.combat == b.combat)) return "combattimento";
    if (!(a == b)) return "altro campo dello stato";
    return "";
}

// Le definizioni che `viewer` non vede tra le carte di `owner`, calcolate in modo indipendente dalla vista: la lista
// del mazzo (60 carte) meno le carte che `viewer` vede (zone pubbliche, mano propria, carte note in mano o nel
// grimorio). Ordinate.
std::vector<CardDefId> expected_hidden(const GameState& s, PlayerId owner, PlayerId viewer, const Decklist& deck) {
    std::map<CardDefId, int> count;
    for (const DeckEntry& e : deck.main) count[db().id_of(e.card)] += e.count;
    const auto bit = static_cast<std::uint8_t>(1u << viewer);
    for (std::size_t i = 0; i < kMaxCards; ++i) {
        const CardObject& o = s.objects[i];
        if (o.owner != owner) continue;
        const bool visible = is_public_zone(o.zone) || (o.zone == Zone::Hand && owner == viewer) ||
                             ((o.zone == Zone::Hand || o.zone == Zone::Library) && (o.known_to & bit) != 0);
        if (visible) --count[o.def];
    }
    std::vector<CardDefId> out;
    for (const auto& [d, n] : count) {
        REQUIRE(n >= 0);
        for (int k = 0; k < n; ++k) out.push_back(d);
    }
    return out;
}

// Le definizioni nelle posizioni nascoste di `owner` in un mondo, ordinate.
std::vector<CardDefId> hidden_in_world(const GameState& w, PlayerId owner, PlayerId viewer) {
    std::vector<CardDefId> out;
    for (const CardId id : w.players[owner].hand) {
        if (hidden_from(w, id, viewer)) out.push_back(w.objects[id].def);
    }
    for (const CardId id : w.players[owner].library) {
        if (hidden_from(w, id, viewer)) out.push_back(w.objects[id].def);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const Decklist& deck_of(PlayerId p) { return p == kUw ? uw_sim_deck() : rakdos_deck(); }

// Gioca una partita degli euristici v1 e chiama `at` a ogni decisione, prima della scelta.
void play_heuristic_game(std::uint64_t seed, const std::function<void(const GameState&)>& at) {
    GameState s = create_game(db(), uw_sim_deck(), rakdos_deck(), seed);
    start_game(s, db(), StartOptions{seed % 2 == 1 ? kUw : kRakdos, true});
    UwAgent uw(db(), seed);
    RakdosAgent rakdos(db(), seed);
    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= 60) {
        at(s);
        const auto actions = legal_actions(s, db());
        const Action a = s.pending.player == kUw ? uw.choose(s, actions) : rakdos.choose(s, actions);
        apply(s, db(), a);
    }
}

// Una partita vera alla prima priorita' di UW nella sua prima fase principale: entrambi tengono 7 carte. UW gioca
// per primo: la mano di Rakdos (7) e i due grimori sono tutti nascosti a UW.
// Avanza passando (o con la prima azione legale, se passare non si puo') fino alla priorita' di `p` nella sua fase
// principale, con la pila vuota.
void pass_to_main_phase(GameState& s, PlayerId p) {
    for (int i = 0; i < 200; ++i) {
        if (is_decision(s, DecisionKind::Priority, p) && s.turn.active_player == p && s.turn.step == Step::Main1 &&
            s.stack_items.empty()) {
            return;
        }
        const auto actions = legal(s);
        const auto it = std::find(actions.begin(), actions.end(), pass());
        apply(s, db(), it != actions.end() ? *it : actions.front());
    }
    FAIL("fase principale non raggiunta");
}

GameState first_main_phase(std::uint64_t seed) {
    GameState s = new_game(seed);
    start_game(s, db(), StartOptions{kUw, true});
    while (s.pending.kind == DecisionKind::Mulligan) apply(s, db(), Action{ActionType::Keep});
    pass_to_main_phase(s, kUw);
    REQUIRE(s.turn.turn_number == 1);
    return s;
}

// Guida una partita preparata: a ogni decisione `choose` sceglie (o restituisce nullopt: la prima azione legale,
// "passa" se c'e'), finche' `done` non e' vero.
void drive(Staged& st, const std::function<bool(const GameState&)>& done,
           const std::function<std::optional<Action>(const GameState&, const std::vector<Action>&)>& choose,
           int max_steps = 300) {
    for (int i = 0; i < max_steps && !done(st.s); ++i) {
        REQUIRE(st.s.pending.kind != DecisionKind::GameOver);
        const auto actions = legal(st.s);
        std::optional<Action> a = choose(st.s, actions);
        if (!a) {
            const auto pass_it = std::find(actions.begin(), actions.end(), pass());
            a = pass_it != actions.end() ? *pass_it : actions.front();
        }
        act(st, *a);
    }
    REQUIRE(done(st.s));
}

const Action* find_type(const std::vector<Action>& actions, ActionType type, const GameState& s, std::string_view card = {}) {
    for (const Action& a : actions) {
        if (a.type != type) continue;
        if (!card.empty() && (a.card == kNoCard || db().get(s.objects[a.card].def).name != card)) continue;
        return &a;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// La vista
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Vista: si costruisce solo alla decisione del giocatore", "[vista]") {
    const GameState s = first_main_phase(1);
    CHECK_NOTHROW(PlayerView(s, db(), kUw));
    CHECK_THROWS_AS(PlayerView(s, db(), kRakdos), EngineError);
}

TEST_CASE("Vista: cosa e' nascosto a chi", "[vista]") {
    const GameState s = first_main_phase(2);
    const PlayerView v(s, db(), kUw);
    // UW vede la propria mano, non quella di Rakdos; nessuno vede i grimori.
    for (const CardId id : s.players[kUw].hand) CHECK_FALSE(hidden_from(s, id, kUw));
    for (const CardId id : s.players[kRakdos].hand) CHECK(hidden_from(s, id, kUw));
    for (const CardId id : s.players[kUw].library) CHECK(hidden_from(s, id, kUw));
    for (const CardId id : s.players[kRakdos].library) CHECK(hidden_from(s, id, kUw));
    CHECK(v.hidden_in_hand(kUw) == 0);
    CHECK(v.hidden_in_hand(kRakdos) == 7);
    CHECK(v.hidden_in_library(kUw) == 53);
    CHECK(v.hidden_in_library(kRakdos) == 53);
    CHECK(v.hidden_defs(kRakdos) == expected_hidden(s, kRakdos, kUw, rakdos_deck()));
    CHECK(v.hidden_defs(kUw) == expected_hidden(s, kUw, kUw, uw_sim_deck()));
    CHECK(v.decision() == DecisionKind::Priority);
    CHECK(v.viewer() == kUw);
}

TEST_CASE("Vista: forma canonica, CardId e incarnation che non rivelano niente", "[vista]") {
    GameState s = first_main_phase(3);
    // Una traccia vera: le carte di una mano rimescolata hanno l'incarnation piu' alta. Qui la si crea a mano su un
    // oggetto del grimorio di Rakdos.
    s.objects[s.players[kRakdos].library[10]].incarnation = 9;
    for (const GameState& c : {canonical_form(s, kUw), PlayerView(s, db(), kUw).sample_world(77)}) {
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            std::vector<CardId> ids;
            std::set<std::uint16_t> incarnations;
            if (p != kUw) {
                for (const CardId id : c.players[p].hand) ids.push_back(id);
            }
            for (const CardId id : c.players[p].library) ids.push_back(id);
            for (const CardId id : ids) incarnations.insert(c.objects[id].incarnation);
            // I CardId delle posizioni nascoste sono in ordine crescente (mano, poi grimorio dal fondo alla cima) e
            // l'incarnation e' la stessa per tutti: la piu' alta tra quelle nascoste (nessun riferimento vecchio torna
            // valido).
            CHECK(std::is_sorted(ids.begin(), ids.end()));
            CHECK(incarnations.size() == 1);
            CHECK(*incarnations.begin() == (p == kRakdos ? 9 : *incarnations.begin()));
            // Il grimorio non ha turno d'arrivo da leggere.
            for (const CardId id : c.players[p].library) CHECK(c.objects[id].zone_turn == 0);
        }
        require_invariants(c);
    }
    // Nella forma canonica le definizioni nascoste sono in ordine; i generatori sono vuoti.
    const GameState c = canonical_form(s, kUw);
    std::vector<CardDefId> defs;
    for (const CardId id : c.players[kRakdos].hand) defs.push_back(c.objects[id].def);
    for (const CardId id : c.players[kRakdos].library) defs.push_back(c.objects[id].def);
    CHECK(std::is_sorted(defs.begin(), defs.end()));
    CHECK(c.players[kUw].rng == Pcg32{});
    CHECK(c.players[kRakdos].rng == Pcg32{});
    // La propria mano non cambia.
    CHECK(c.players[kUw].hand == s.players[kUw].hand);
    for (const CardId id : s.players[kUw].hand) CHECK(c.objects[id] == s.objects[id]);
}

TEST_CASE("Vista: stesso seed del mondo, stesso mondo; seed diversi, mondi diversi", "[vista]") {
    const GameState s = first_main_phase(4);
    const PlayerView v(s, db(), kUw);
    CHECK(v.sample_world(5) == v.sample_world(5));
    CHECK_FALSE(v.sample_world(5) == v.sample_world(6));
    // I generatori dei giocatori sono nuovi, diversi da quelli veri e tra loro.
    const GameState w = v.sample_world(5);
    CHECK_FALSE(w.players[kUw].rng == s.players[kUw].rng);
    CHECK_FALSE(w.players[kRakdos].rng == s.players[kRakdos].rng);
    CHECK_FALSE(w.players[kUw].rng == w.players[kRakdos].rng);
}

// ---------------------------------------------------------------------------------------------------------------
// (a) Gemello per la determinizzazione, con (f) invarianti
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Determinizzazione (a): lo stato e il suo gemello danno lo stesso mondo, a ogni decisione",
          "[vista][determinizzazione]") {
    Pcg32 rng(20261116, 77);
    long long compared = 0, twin_differs = 0;
    std::array<long long, kNumPlayers> by_viewer{};
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        play_heuristic_game(seed, [&](const GameState& s) {
            const PlayerId viewer = s.pending.player;
            const GameState twin = twin_of(s, viewer, rng);
            if (!(twin == s)) ++twin_differs;
            const GameState cs = canonical_form(s, viewer);
            const GameState ct = canonical_form(twin, viewer);
            INFO("seed " << seed << ", turno " << s.turn.turn_number << ", decisione " << static_cast<int>(s.pending.kind)
                         << ", giocatore " << static_cast<int>(viewer));
            INFO("forma canonica: " << first_difference(cs, ct));
            REQUIRE(cs == ct);
            const PlayerView vs(s, db(), viewer);
            const PlayerView vt(twin, db(), viewer);
            const std::uint64_t world_seed = rng.next();
            const GameState ws = vs.sample_world(world_seed);
            const GameState wt = vt.sample_world(world_seed);
            INFO("mondo: " << first_difference(ws, wt));
            REQUIRE(ws == wt);
            // (f) invarianti del mondo.
            const auto problems = check_invariants(ws, db());
            for (const auto& p : problems) UNSCOPED_INFO(p);
            REQUIRE(problems.empty());
            ++compared;
            ++by_viewer[viewer];
        });
    }
    WARN("determinizzazione (a): " << compared << " decisioni confrontate (UW " << by_viewer[kUw] << ", Rakdos "
                                   << by_viewer[kRakdos] << ")");
    CHECK(compared > 20000);
    CHECK(by_viewer[kUw] > 5000);
    CHECK(by_viewer[kRakdos] > 5000);
    CHECK(twin_differs > compared * 9 / 10);  // il gemello cambia davvero lo stato quasi sempre
}

TEST_CASE("Determinizzazione (a), controllo: cambiare una carta nota cambia il mondo", "[vista][determinizzazione]") {
    // Il test del gemello ha senso solo se la forma canonica non cancella tutto: cio' che il giocatore sa deve restare.
    const GameState s = first_main_phase(5);
    const PlayerView v(s, db(), kUw);
    SECTION("una carta della propria mano") {
        GameState t = s;
        CardObject& c = t.objects[t.players[kUw].hand[0]];
        c.def = c.def == db().id_of("Solitude") ? db().id_of("Ephemerate") : db().id_of("Solitude");
        CHECK_FALSE(PlayerView(t, db(), kUw).sample_world(9) == v.sample_world(9));
    }
    SECTION("una carta della mano avversaria che UW conosce") {
        GameState t = s;
        t.objects[t.players[kRakdos].hand[0]].known_to = kAllPlayers;
        GameState u = t;
        CardObject& c = u.objects[u.players[kRakdos].hand[0]];
        c.def = c.def == db().id_of("Lightning Bolt") ? db().id_of("Thoughtseize") : db().id_of("Lightning Bolt");
        CHECK_FALSE(PlayerView(t, db(), kUw).sample_world(9) == PlayerView(u, db(), kUw).sample_world(9));
    }
    SECTION("la vita dell'avversario") {
        GameState t = s;
        t.players[kRakdos].life = 17;
        CHECK_FALSE(PlayerView(t, db(), kUw).sample_world(9) == v.sample_world(9));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// (c) Carte note
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Determinizzazione (c): dopo Thoughtseize la mano rivelata e' la stessa in ogni mondo",
          "[vista][determinizzazione]") {
    // Street Wraith (ciclo pagando 2 vita) da' a Rakdos una decisione dopo la risoluzione: la vista si costruisce li'.
    Staged st = staged(kRakdos, {"Solitude", "Ephemerate", "Plains", "Guide of Souls"}, {"Thoughtseize", "Street Wraith"},
                       {}, {"Swamp"});
    bool cast = false;
    drive(
        st, [&](const GameState& s) { return cast && s.stack_items.empty() && is_decision(s, DecisionKind::Priority, kRakdos); },
        [&](const GameState& s, const std::vector<Action>& actions) -> std::optional<Action> {
            if (s.pending.player != kRakdos) return std::nullopt;
            if (s.pending.kind == DecisionKind::Priority && !cast) {
                if (const Action* a = find_type(actions, ActionType::CastSpell, s, "Thoughtseize")) {
                    cast = true;
                    return *a;
                }
            }
            if (s.pending.kind == DecisionKind::ChooseTarget) return Action{ActionType::TargetPlayer, kNoCard, kUw};
            return std::nullopt;  // la carta da prendere: la prima
        });
    const GameState& s = st.s;
    REQUIRE(s.players[kUw].hand.size() == 3);
    const PlayerView v(s, db(), kRakdos);
    CHECK(v.hidden_in_hand(kUw) == 0);
    for (std::uint64_t w = 1; w <= 50; ++w) {
        const GameState world = v.sample_world(w);
        REQUIRE(world.players[kUw].hand.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(world.objects[world.players[kUw].hand[i]].def == s.objects[s.players[kUw].hand[i]].def);
        }
    }
}

TEST_CASE("Determinizzazione (c): la cima guardata con Mishra's Bauble resta al suo posto, fino al mescolamento",
          "[vista][determinizzazione]") {
    // Rakdos lancia Bauble e guarda la cima del grimorio di UW.
    Staged st = staged(kRakdos, {"Plains"}, {"Mishra's Bauble", "Street Wraith"}, {}, {"Swamp"});
    bool cast = false, activated = false;
    drive(
        st,
        [&](const GameState& s) {
            return activated && s.stack_items.empty() && is_decision(s, DecisionKind::Priority, kRakdos);
        },
        [&](const GameState& s, const std::vector<Action>& actions) -> std::optional<Action> {
            if (s.pending.player != kRakdos) return std::nullopt;
            if (s.pending.kind == DecisionKind::Priority && !cast) {
                if (const Action* a = find_type(actions, ActionType::CastSpell, s, "Mishra's Bauble")) {
                    cast = true;
                    return *a;
                }
            }
            if (s.pending.kind == DecisionKind::Priority && cast && !activated) {
                if (const Action* a = find_type(actions, ActionType::ActivateAbility, s, "Mishra's Bauble")) {
                    activated = true;
                    return *a;
                }
            }
            if (s.pending.kind == DecisionKind::ChooseTarget) return Action{ActionType::TargetPlayer, kNoCard, kUw};
            return std::nullopt;
        });
    GameState s = st.s;
    const CardId top = s.players[kUw].library.back();
    REQUIRE((s.objects[top].known_to & (1u << kRakdos)) != 0);
    {
        const PlayerView v(s, db(), kRakdos);
        CHECK(v.hidden_in_library(kUw) == static_cast<int>(s.players[kUw].library.size()) - 1);
        for (std::uint64_t w = 1; w <= 50; ++w) {
            const GameState world = v.sample_world(w);
            CHECK(world.players[kUw].library.back() == top);
            CHECK(world.objects[top].def == s.objects[top].def);
        }
    }
    // Un mescolamento (come una fetch di UW): nessuno sa piu' dov'e' la carta, la cima cambia da un mondo all'altro.
    shuffle_library(s, kUw);
    const PlayerView v(s, db(), kRakdos);
    CHECK(v.hidden_in_library(kUw) == static_cast<int>(s.players[kUw].library.size()));
    std::set<CardDefId> tops;
    for (std::uint64_t w = 1; w <= 50; ++w) {
        const GameState world = v.sample_world(w);
        tops.insert(world.objects[world.players[kUw].library.back()].def);
    }
    CHECK(tops.size() >= 5);
}

TEST_CASE("Determinizzazione (c): le proprie carte in fondo dopo il mulligan restano in fondo, nell'ordine scelto",
          "[vista][determinizzazione]") {
    GameState s = new_game(6);
    start_game(s, db(), StartOptions{kUw, true});
    // UW fa un mulligan e mette in fondo una carta; Rakdos fa due mulligan e ne mette in fondo due.
    int mulligans[kNumPlayers] = {0, 0};
    std::vector<CardId> uw_bottom;
    while (s.pending.kind == DecisionKind::Mulligan || s.pending.kind == DecisionKind::BottomCards) {
        const PlayerId p = s.pending.player;
        if (s.pending.kind == DecisionKind::BottomCards) {
            const Action a = legal(s).back();
            if (p == kUw) uw_bottom.push_back(a.card);
            apply(s, db(), a);
            continue;
        }
        apply(s, db(), Action{mulligans[p]++ < (p == kUw ? 1 : 2) ? ActionType::Mulligan : ActionType::Keep});
    }
    REQUIRE(uw_bottom.size() == 1);
    pass_to_main_phase(s, kUw);
    REQUIRE(s.players[kUw].library[0] == uw_bottom[0]);
    const std::vector<CardId> rakdos_bottom = {s.players[kRakdos].library[0], s.players[kRakdos].library[1]};

    // UW: la propria carta in fondo e' nota e resta li'; quelle di Rakdos sono carte sconosciute.
    const PlayerView v(s, db(), kUw);
    CHECK(v.hidden_in_library(kUw) == static_cast<int>(s.players[kUw].library.size()) - 1);
    std::set<CardDefId> rakdos_bottoms;
    for (std::uint64_t w = 1; w <= 60; ++w) {
        const GameState world = v.sample_world(w);
        CHECK(world.players[kUw].library[0] == uw_bottom[0]);
        CHECK(world.objects[uw_bottom[0]].def == s.objects[uw_bottom[0]].def);
        rakdos_bottoms.insert(world.objects[world.players[kRakdos].library[0]].def);
    }
    CHECK(rakdos_bottoms.size() >= 5);

    // Rakdos (al suo primo turno): le sue due carte in fondo restano, nell'ordine scelto; quella di UW no.
    pass_to_main_phase(s, kRakdos);
    REQUIRE(s.players[kRakdos].library[0] == rakdos_bottom[0]);
    const PlayerView r(s, db(), kRakdos);
    std::set<CardDefId> uw_bottoms;
    for (std::uint64_t w = 1; w <= 60; ++w) {
        const GameState world = r.sample_world(w);
        CHECK(world.players[kRakdos].library[0] == rakdos_bottom[0]);
        CHECK(world.players[kRakdos].library[1] == rakdos_bottom[1]);
        CHECK(world.objects[rakdos_bottom[0]].def == s.objects[rakdos_bottom[0]].def);
        CHECK(world.objects[rakdos_bottom[1]].def == s.objects[rakdos_bottom[1]].def);
        uw_bottoms.insert(world.objects[world.players[kUw].library[0]].def);
    }
    CHECK(uw_bottoms.size() >= 5);
}

// ---------------------------------------------------------------------------------------------------------------
// (d) Distribuzione e (e) il proprio grimorio
// ---------------------------------------------------------------------------------------------------------------

namespace {

double log_choose(int n, int k) { return std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0); }

// Probabilita' ipergeometrica di k copie in un campione di h carte, da N carte con m copie.
double hypergeometric(int N, int m, int h, int k) {
    if (k < 0 || k > m || k > h || h - k > N - m) return 0.0;
    return std::exp(log_choose(m, k) + log_choose(N - m, h - k) - log_choose(N, h));
}

}  // namespace

TEST_CASE("Determinizzazione (d): la mano avversaria segue l'ipergeometrica delle carte non viste",
          "[vista][determinizzazione]") {
    const GameState s = first_main_phase(7);
    const PlayerView v(s, db(), kUw);
    const std::vector<CardDefId> pool = expected_hidden(s, kRakdos, kUw, rakdos_deck());
    const int N = static_cast<int>(pool.size());
    const int h = static_cast<int>(s.players[kRakdos].hand.size());
    REQUIRE(N == 60);
    REQUIRE(h == 7);
    std::map<CardDefId, int> copies;
    for (const CardDefId d : pool) ++copies[d];

    constexpr long long kWorlds = 100000;
    std::map<CardDefId, long long> in_hand;
    const CardDefId bolt = db().id_of("Lightning Bolt");
    std::array<long long, 5> bolt_pmf{};
    for (long long w = 0; w < kWorlds; ++w) {
        const GameState world = v.sample_world(static_cast<std::uint64_t>(w) + 1);
        int bolts = 0;
        for (const CardId id : world.players[kRakdos].hand) {
            ++in_hand[world.objects[id].def];
            bolts += world.objects[id].def == bolt ? 1 : 0;
        }
        ++bolt_pmf[static_cast<std::size_t>(bolts)];
        if (w % 1000 == 0) {
            // Le carte nascoste di ogni mondo sono esattamente l'insieme atteso; (f) invarianti.
            CHECK(hidden_in_world(world, kRakdos, kUw) == pool);
            CHECK(hidden_in_world(world, kUw, kUw) == expected_hidden(s, kUw, kUw, uw_sim_deck()));
            require_invariants(world);
        }
    }
    // Ogni carta: copie in mano nei mondi contro l'atteso h*m/N, con la varianza ipergeometrica.
    for (const auto& [d, m] : copies) {
        const double p = static_cast<double>(m) / N;
        const double mean = kWorlds * h * p;
        const double var = kWorlds * h * p * (1.0 - p) * (N - h) / (N - 1.0);
        const double z = (static_cast<double>(in_hand[d]) - mean) / std::sqrt(var);
        INFO(name_of(d) << ": " << in_hand[d] << " contro " << mean << ", z = " << z);
        CHECK(std::abs(z) < 5.0);
    }
    // Lightning Bolt (4 copie): numero di copie in mano, chi quadro contro l'ipergeometrica (le ultime due classi
    // insieme: con 4 copie in mano l'atteso e' sotto 10).
    double chi2 = 0.0;
    for (int k = 0; k <= 3; ++k) {
        double expected = kWorlds * hypergeometric(N, copies[bolt], h, k);
        double observed = static_cast<double>(bolt_pmf[static_cast<std::size_t>(k)]);
        if (k == 3) {
            expected += kWorlds * hypergeometric(N, copies[bolt], h, 4);
            observed += static_cast<double>(bolt_pmf[4]);
        }
        chi2 += (observed - expected) * (observed - expected) / expected;
    }
    INFO("chi quadro (3 gradi di liberta') " << chi2);
    CHECK(chi2 < 30.0);  // p circa 1e-6
}

TEST_CASE("Determinizzazione (e): la prossima pescata del mondo e' quella vera solo con la frequenza attesa",
          "[vista][determinizzazione]") {
    const GameState s = first_main_phase(8);
    const PlayerView v(s, db(), kUw);
    const CardDefId true_top = s.objects[s.players[kUw].library.back()].def;
    const std::vector<CardDefId> pool = expected_hidden(s, kUw, kUw, uw_sim_deck());
    const int N = static_cast<int>(pool.size());
    REQUIRE(N == 53);
    std::map<CardDefId, int> copies;
    for (const CardDefId d : pool) ++copies[d];
    constexpr long long kWorlds = 40000;
    std::map<CardDefId, long long> tops;
    for (long long w = 0; w < kWorlds; ++w) {
        const GameState world = v.sample_world(static_cast<std::uint64_t>(w) + 1000001);
        ++tops[world.objects[world.players[kUw].library.back()].def];
    }
    double chi2 = 0.0;
    for (const auto& [d, m] : copies) {
        const double expected = static_cast<double>(kWorlds) * m / N;
        chi2 += (static_cast<double>(tops[d]) - expected) * (static_cast<double>(tops[d]) - expected) / expected;
    }
    const double p = static_cast<double>(copies[true_top]) / N;
    const double z = (static_cast<double>(tops[true_top]) - kWorlds * p) / std::sqrt(kWorlds * p * (1.0 - p));
    INFO("vera prossima pescata " << name_of(true_top) << ": " << tops[true_top] << " mondi su " << kWorlds
                                  << ", atteso " << kWorlds * p << ", z = " << z << "; chi quadro " << chi2 << " con "
                                  << copies.size() - 1 << " gradi di liberta'");
    CHECK(std::abs(z) < 5.0);
    CHECK(chi2 < 80.0);  // circa 25 gradi di liberta': p sotto 1e-6
}

// Passo 2: nelle ricerche nel grimorio le azioni legali citano carte del grimorio, nascoste nella forma canonica. Il
// giocatore le vede mentre cerca (701.23): la vista costruita con le azioni legali dice la definizione delle carte
// che la decisione mostra, e solo di quelle. I mondi non cambiano.
TEST_CASE("Vista: una ricerca nel grimorio mostra le definizioni delle carte offerte, e solo quelle",
          "[vista][determinizzazione]") {
    int searches = 0;
    for (std::uint64_t seed = 1; seed <= 30 && searches < 20; ++seed) {
        play_heuristic_game(seed, [&](const GameState& s) {
            if (s.pending.kind != DecisionKind::SearchLibrary || searches >= 20) return;
            const PlayerId viewer = s.pending.player;
            const std::vector<Action> actions = legal_actions(s, db());
            const PlayerView with(s, db(), viewer, actions);
            const PlayerView without(s, db(), viewer);
            int shown = 0;
            for (const Action& a : actions) {
                if (a.type != ActionType::ChooseCard) continue;
                CHECK(with.shown_def(a.card) == s.objects[a.card].def);
                CHECK(without.shown_def(a.card) == kNoDef);  // senza le azioni non mostra niente
                ++shown;
            }
            REQUIRE(shown > 0);
            // Una carta del grimorio che la ricerca non offre resta nascosta.
            for (const CardId id : s.players[viewer].library) {
                const bool offered = std::any_of(actions.begin(), actions.end(), [&](const Action& a) {
                    return a.type == ActionType::ChooseCard && a.card == id;
                });
                if (!offered) CHECK(with.shown_def(id) == kNoDef);
            }
            // Le azioni non cambiano i mondi.
            for (std::uint64_t w = 1; w <= 5; ++w) CHECK(with.sample_world(w) == without.sample_world(w));
            ++searches;
        });
    }
    CHECK(searches == 20);
}
