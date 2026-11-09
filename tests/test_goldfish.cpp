// Test del goldfish (docs/agente_rakdos.md, sezione 16) e del registro JSON Lines.

#include "goldfish.hpp"
#include "goldfish_agent.hpp"
#include "rakdos_agent.hpp"
#include "replay.hpp"
#include "test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>

using namespace mtg;
using namespace test;

namespace {

const Decklist& goldfish_deck() {
    static const Decklist d = load_decklist(std::string(DECKS_DIR) + "/goldfish_plains.json");
    return d;
}

GoldfishConfig config(int turn_limit = 15) {
    GoldfishConfig c;
    c.db = &db_with_tokens();
    c.rakdos = rakdos_deck();
    c.goldfish = goldfish_deck();
    c.turn_limit = turn_limit;
    return c;
}

std::vector<nlohmann::json> parse_lines(const std::string& text) {
    std::vector<nlohmann::json> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        REQUIRE_FALSE(line.empty());
        out.push_back(nlohmann::json::parse(line));  // lancia se la riga non è JSON valido
    }
    return out;
}

}  // namespace

TEST_CASE("Goldfish: la lista di 60 Plains e' legale", "[goldfish][config]") {
    CHECK(validate_decklist(goldfish_deck()).empty());
    CHECK(goldfish_deck().main_size() == 60);
}

TEST_CASE("Goldfish: l'avversario non lancia, non gioca terre e non attacca mai", "[goldfish]") {
    const GoldfishConfig c = config();
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        GameLog log;
        (void)play_goldfish(c, seed, &log);
        for (const LogEntry& e : log.entries) {
            if (e.kind != LogEntry::Kind::Action || e.player != kGoldfishOpponent) continue;
            INFO("seed " << seed);
            CHECK(e.action.type != ActionType::PlayLand);
            CHECK(e.action.type != ActionType::CastSpell);
            CHECK(e.action.type != ActionType::ActivateAbility);
            CHECK(e.action.type != ActionType::Mulligan);
            CHECK_FALSE((e.decision == DecisionKind::DeclareAttacker && e.action.type == ActionType::TargetPlayer));
        }
        for (const LogEntry& e : log.entries) {  // nessuna carta dell'avversario arriva in campo o in pila
            if (e.kind == LogEntry::Kind::ZoneChange && e.player == kGoldfishOpponent) {
                CHECK(e.to != Zone::Battlefield);
                CHECK(e.to != Zone::Stack);
            }
        }
    }
}

TEST_CASE("Goldfish: meta' partite in play e meta' in draw, secondo la parita' del seed", "[goldfish]") {
    const GoldfishConfig c = config();
    CHECK(play_goldfish(c, 7).on_play);
    CHECK_FALSE(play_goldfish(c, 8).on_play);
    CHECK(rakdos_turn(1, true) == 1);
    CHECK(rakdos_turn(2, true) == 1);
    CHECK(rakdos_turn(3, true) == 2);
    CHECK(rakdos_turn(1, false) == 0);
    CHECK(rakdos_turn(2, false) == 1);
    CHECK(rakdos_turn(4, false) == 2);
    CHECK(rakdos_turn_reached(1, true) == 1);   // turno di Rakdos
    CHECK(rakdos_turn_reached(2, true) == 2);   // turno avversario: vale dal turno dopo
    CHECK(rakdos_turn_reached(1, false) == 1);
    CHECK(rakdos_turn_reached(2, false) == 1);
    CHECK(rakdos_turn_reached(3, false) == 2);
}

TEST_CASE("Goldfish: il limite di turni viene rispettato", "[goldfish]") {
    for (const int limit : {2, 4, 15}) {
        const GoldfishConfig c = config(limit);
        for (std::uint64_t seed = 1; seed <= 40; ++seed) {
            const GoldfishGame g = play_goldfish(c, seed);
            INFO("limite " << limit << ", seed " << seed);
            CHECK(g.rakdos_turns <= limit);
            if (g.result == GoldfishResult::Won) CHECK((g.win_turn >= 1 && g.win_turn <= limit));
            if (g.result == GoldfishResult::TurnLimit) CHECK(g.rakdos_turns == limit);
        }
    }
}

TEST_CASE("Goldfish: stesso seed, stesso risultato e stesso registro", "[goldfish]") {
    const GoldfishConfig c = config();
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameLog a, b;
        const GoldfishGame x = play_goldfish(c, seed, &a);
        const GoldfishGame y = play_goldfish(c, seed, &b);
        INFO("seed " << seed);
        CHECK(a.entries == b.entries);
        CHECK(goldfish_game_json(x) == goldfish_game_json(y));
    }
}

TEST_CASE("Goldfish: il turno del delirium coincide con il ricalcolo del cimitero", "[goldfish]") {
    // Ricalcolo indipendente: la stessa partita giocata passo per passo, con il riepilogo
    // del cimitero ricalcolato da zero (compute_graveyard_summary) dopo ogni decisione.
    // Una decisione può far avanzare la partita fino al turno successivo (una magia si
    // risolve, poi il turno finisce da solo): il ricalcolo sa solo che il delirium è
    // arrivato tra il turno prima e il turno dopo quella decisione. Il goldfish lo legge
    // dal registro, spostamento per spostamento, e deve cadere in quell'intervallo; quando
    // la decisione non cambia turno, i due valori coincidono.
    const GoldfishConfig c = config();
    const CardDatabase& db = *c.db;
    int with_delirium = 0, exact = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        const GoldfishGame g = play_goldfish(c, seed);
        const bool on_play = seed % 2 == 1;
        GameState s = create_game(db, c.rakdos, c.goldfish, seed);
        start_game(s, db, StartOptions{on_play ? kGoldfishRakdos : kGoldfishOpponent, true});
        RakdosAgent agent(db, seed, c.agent);
        GoldfishAgent fish;
        int earliest = 0, latest = 0;
        while (s.pending.kind != DecisionKind::GameOver && rakdos_turn(s.turn.turn_number, on_play) <= c.turn_limit) {
            const auto actions = legal_actions(s, db);
            const Action a = s.pending.player == kGoldfishRakdos ? agent.choose(s, actions) : fish.choose(s, actions);
            const std::uint16_t before = s.turn.turn_number;
            apply(s, db, a);
            if (latest == 0 && compute_graveyard_summary(s, db, kGoldfishRakdos).delirium()) {
                earliest = rakdos_turn_reached(before, on_play);
                latest = rakdos_turn_reached(s.turn.turn_number, on_play);
            }
        }
        INFO("seed " << seed);
        if (latest == 0) {
            CHECK(g.delirium_turn == 0);
            continue;
        }
        CHECK(g.delirium_turn >= earliest);
        CHECK(g.delirium_turn <= latest);
        ++with_delirium;
        exact += earliest == latest ? 1 : 0;
    }
    CHECK(with_delirium > 50);
    CHECK(exact > 40);  // il confronto è esatto nella maggior parte delle partite
}

TEST_CASE("Goldfish: Rakdos vince e le misure sono coerenti", "[goldfish]") {
    const GoldfishConfig c = config();
    int wins = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        const GoldfishGame g = play_goldfish(c, seed);
        INFO("seed " << seed);
        CHECK(g.mulligans >= 0);
        CHECK(g.decisions > 0);
        CHECK(g.default_decisions <= g.decisions);
        CHECK(g.types_end_of_turn.size() <= static_cast<std::size_t>(g.rakdos_turns));
        if (g.result != GoldfishResult::Won) continue;
        ++wins;
        int damage = 0;
        for (const auto& [card, n] : g.damage) damage += n;
        CHECK(damage >= 20);  // l'avversario parte da 20 e non guadagna vita
        const auto json = nlohmann::json::parse(goldfish_game_json(g));
        CHECK(json["seed"] == seed);
        CHECK(json["turno_vittoria"] == g.win_turn);
    }
    CHECK(wins > 90);
}

TEST_CASE("Registro JSON Lines: ogni riga e' JSON e la partita si ricostruisce", "[goldfish][replay]") {
    const GoldfishConfig c = config();
    GameLog log;
    std::map<std::size_t, std::string> rules;
    const GoldfishGame g = play_goldfish(c, 3, &log, &rules);
    ReplayInfo info;
    info.seed = 3;
    info.players = {"Rakdos", "Goldfish"};
    info.rules = rules;
    std::ostringstream out;
    write_replay(out, *c.db, log, info);
    const auto lines = parse_lines(out.str());
    REQUIRE(lines.size() > 30);

    std::set<std::string> events;
    for (const auto& l : lines) events.insert(l.at("evento").get<std::string>());
    for (const char* e : {"partita", "turno", "mano", "decisione", "pescata", "cimitero", "vita", "fine"}) {
        INFO(e);
        CHECK(events.contains(e));
    }
    CHECK(lines.front()["evento"] == "partita");
    CHECK(lines.back()["evento"] == "fine");

    // Le decisioni di Rakdos portano la regola dell'agente; le pescate in partita il turno.
    int decisions = 0;
    for (const auto& l : lines) {
        if (l["evento"] == "decisione" && l["giocatore"] == "Rakdos") {
            CHECK(l.contains("regola"));
            ++decisions;
        }
        if (l["evento"] == "pescata") CHECK(l.contains("turno"));
    }
    CHECK(decisions == static_cast<int>(g.decisions));

    // Ogni riepilogo del cimitero segue lo spostamento che lo ha cambiato, con lo stesso turno.
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i]["evento"] != "cimitero") continue;
        std::size_t j = i - 1;
        while (j > 0 && lines[j]["evento"] == "cimitero") --j;
        INFO("riga " << i + 1);
        CHECK((lines[j]["evento"] == "spostamento" || lines[j]["evento"] == "pescata"));
        CHECK(lines[j]["turno"] == lines[i]["turno"]);
    }

    // L'ultimo riepilogo del cimitero di Rakdos è il cimitero vero alla fine.
    nlohmann::json last;
    for (const auto& l : lines) {
        if (l["evento"] == "cimitero" && l["giocatore"] == "Rakdos") last = l;
    }
    REQUIRE_FALSE(last.is_null());
    int cards = 0;
    std::set<CardId> gy;
    for (const LogEntry& e : log.entries) {
        if (e.kind != LogEntry::Kind::ZoneChange || e.player != kGoldfishRakdos) continue;
        if (e.to == Zone::Graveyard) gy.insert(e.action.card);
        if (e.from == Zone::Graveyard) gy.erase(e.action.card);
    }
    cards = static_cast<int>(gy.size());
    CHECK(last["carte"] == cards);
    CHECK(last["tipi"].size() <= 6);
    CHECK(last["delirium"] == (last["tipi"].size() >= 4));
}
