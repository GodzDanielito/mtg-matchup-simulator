// Test del record compatto per le metriche (tools/metriche.hpp, docs/metriche.md, sezioni 2, 11 e 12).
//
// Due livelli:
//  1. registri sintetici (voci LogEntry scritte a mano): ogni codice, le mani del mulligan, le terre a fine
//     turno, il delirium con la convenzione del goldfish, il formato JSON, i controlli di coerenza;
//  2. partite vere del matchup: il record confrontato con lo stato visto da un osservatore (mani di ogni
//     mulligan, mano tenuta, mano finale, terre, delirium), con le carte morte, e i codici che il motore
//     produce davvero. In piu' "il record non cambia la partita" e "la prima mano da 7 dipende solo dal seed".

#include "carte_morte.hpp"
#include "goldfish.hpp"
#include "matchup.hpp"
#include "metriche.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"
#include "varianti.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kU = 0;  // UW
constexpr PlayerId kR = 1;  // Rakdos

CardDefId def(const char* name) { return db_with_tokens().id_of(name); }

// Registro sintetico: le voci nell'ordine in cui le scriverebbe il motore.
struct LogBuilder {
    GameLog log;
    std::uint16_t turn = 0;

    void move(PlayerId owner, CardId card, const char* name, Zone from, Zone to, Cause cause) {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = owner;
        e.turn = turn;
        e.action.card = card;
        e.def = def(name);
        e.from = from;
        e.to = to;
        e.cause = cause;
        log.entries.push_back(e);
    }
    void act(PlayerId p, DecisionKind d, ActionType t, CardId card = kNoCard, std::uint8_t value = 0) {
        LogEntry e;
        e.kind = LogEntry::Kind::Action;
        e.player = p;
        e.turn = turn;
        e.decision = d;
        e.action = Action{t, card, value};
        log.entries.push_back(e);
    }
    void life(PlayerId p, int amount) {
        LogEntry e;
        e.kind = LogEntry::Kind::LifeChange;
        e.player = p;
        e.turn = turn;
        e.amount = static_cast<std::int16_t>(amount);
        e.cause = Cause::Cost;
        log.entries.push_back(e);
    }
    // Inizio di un nuovo turno della partita (stap), con il giocatore attivo.
    void new_turn(PlayerId active) {
        ++turn;
        LogEntry e;
        e.kind = LogEntry::Kind::StepStart;
        e.player = active;
        e.turn = turn;
        e.step = Step::Untap;
        log.entries.push_back(e);
    }
    void draw(PlayerId p, CardId card, const char* name) { move(p, card, name, Zone::Library, Zone::Hand, Cause::Draw); }
};

MatchupGame game_info(bool uw_on_play, int turns = 0) {
    MatchupGame g;
    g.seed = uw_on_play ? 1 : 2;
    g.uw_on_play = uw_on_play;
    g.turns = turns;
    return g;
}

std::vector<CopyEvent> ev(std::initializer_list<CopyEvent> events) { return events; }

// La copia con quell'id di carta: il record non tiene il CardId, quindi i test cercano per definizione e
// posizione (l'n-esima copia di quella carta nel record).
const CopyRecord& copy_of(const PlayerRecord& p, const char* name, std::size_t nth = 0) {
    const CardDefId d = def(name);
    for (const CopyRecord& c : p.cards) {
        if (c.def != d) continue;
        if (nth == 0) return c;
        --nth;
    }
    FAIL("copia non trovata nel record: " << name);
    return p.cards.front();
}

MatchupConfig v1_config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;  // euristici v1: UW base_v2, Rakdos base_r3 (le opzioni predefinite)
}

}  // namespace

// --- 1. Registri sintetici ------------------------------------------------------------------------------

TEST_CASE("Metriche: mani del mulligan, carte in fondo e mano iniziale tenuta", "[metriche]") {
    const std::array<const char*, 14> uw = {"Plains",         "Solitude",          "Ephemerate",       "Guide of Souls",
                                            "Ocelot Pride",   "Witch Enchanter",   "Hallowed Fountain", "Arid Mesa",
                                            "Quantum Riddler", "Teferi, Time Raveler", "Seam Rip",     "Flooded Strand",
                                            "Consign to Memory", "Recruiter of the Guard"};
    const std::array<const char*, 7> rk = {"Swamp", "Nethergoyf", "Thoughtseize", "Lightning Bolt",
                                           "Marsh Flats", "Street Wraith", "Moonshadow"};
    LogBuilder b;
    for (CardId c = 0; c < 7; ++c) b.draw(kU, c, uw[c]);
    for (CardId c = 0; c < 7; ++c) b.draw(kR, static_cast<CardId>(60 + c), rk[c]);
    b.act(kU, DecisionKind::Mulligan, ActionType::Mulligan);
    b.act(kR, DecisionKind::Mulligan, ActionType::Keep);
    for (CardId c = 0; c < 7; ++c) b.move(kU, c, uw[c], Zone::Hand, Zone::Library, Cause::Mulligan);
    for (CardId c = 7; c < 14; ++c) b.draw(kU, c, uw[c]);
    b.act(kU, DecisionKind::Mulligan, ActionType::Keep);
    b.act(kU, DecisionKind::BottomCards, ActionType::ChooseCard, 9);
    b.move(kU, 9, uw[9], Zone::Hand, Zone::Library, Cause::Mulligan);
    b.new_turn(kU);

    const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 1), b.log);
    const PlayerRecord& u = r.players[kU];
    CHECK(u.mulligans == 1);
    REQUIRE(u.hands.size() == 2);
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK(u.hands[0][i] == def(uw[i]));
        CHECK(u.hands[1][i] == def(uw[7 + i]));
    }
    CHECK(u.bottom == std::vector<CardDefId>{def("Teferi, Time Raveler")});
    // La mano tenuta, nell'ordine della mano: le carte rimescolate nel grimorio e quella in fondo non ci sono.
    REQUIRE(u.cards.size() == 6);
    const std::array<CardId, 6> kept = {7, 8, 10, 11, 12, 13};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(u.cards[i].def == def(uw[kept[i]]));
        CHECK(u.cards[i].events == ev({{0, 'I'}}));
    }
    CHECK(u.hand_end == 6);

    const PlayerRecord& k = r.players[kR];
    CHECK(k.mulligans == 0);
    REQUIRE(k.hands.size() == 1);
    CHECK(k.hands[0].size() == 7);
    CHECK(k.bottom.empty());
    CHECK(k.cards.size() == 7);
    CHECK(k.hand_end == 7);
}

TEST_CASE("Metriche: un codice per ogni spostamento da mano e grimorio", "[metriche]") {
    LogBuilder b;
    b.new_turn(kU);  // turno 1, UW
    b.draw(kU, 20, "Ephemerate");                                                       // P
    b.move(kU, 21, "Solitude", Zone::Library, Zone::Hand, Cause::Search);               // C (Recruiter)
    b.move(kU, 22, "Plains", Zone::Library, Zone::Battlefield, Cause::Search);          // F (fetch)
    b.move(kU, 20, "Ephemerate", Zone::Hand, Zone::Stack, Cause::Cast);                 // L
    b.draw(kU, 23, "Guide of Souls");
    b.move(kU, 23, "Guide of Souls", Zone::Hand, Zone::Stack, Cause::Cast);
    b.move(kU, 23, "Guide of Souls", Zone::Stack, Zone::Battlefield, Cause::Resolve);   // nessun codice
    b.move(kU, 23, "Guide of Souls", Zone::Battlefield, Zone::Hand, Cause::Resolve);    // R (Teferi -3)
    b.draw(kU, 24, "Hallowed Fountain");
    b.move(kU, 24, "Hallowed Fountain", Zone::Hand, Zone::Battlefield, Cause::Play);    // T
    b.draw(kU, 25, "Witch Enchanter");
    b.move(kU, 25, "Witch Enchanter", Zone::Hand, Zone::Exile, Cause::Cost);            // E (evocazione)
    b.draw(kU, 27, "Quantum Riddler");
    b.move(kU, 27, "Quantum Riddler", Zone::Hand, Zone::Stack, Cause::Cast);            // L (warp)
    b.move(kU, 27, "Quantum Riddler", Zone::Stack, Zone::Battlefield, Cause::Resolve);
    b.move(kU, 27, "Quantum Riddler", Zone::Battlefield, Zone::Exile, Cause::Resolve);  // esilio del warp
    b.move(kU, 28, "Seam Rip", Zone::Library, Zone::Exile, Cause::Exile);               // ? (non previsto)
    b.move(kU, 130, "Plains", Zone::Battlefield, Zone::Graveyard, Cause::StateBased);   // un token: niente

    b.new_turn(kR);  // turno 2, Rakdos
    b.draw(kU, 26, "Ocelot Pride");
    b.act(kR, DecisionKind::ChooseFromHand, ActionType::ChooseCard, 26);                // Thoughtseize
    b.move(kU, 26, "Ocelot Pride", Zone::Hand, Zone::Graveyard, Cause::Discard);        // S
    b.draw(kR, 70, "Street Wraith");
    b.act(kR, DecisionKind::Priority, ActionType::ActivateAbility, 70);                 // ciclo
    b.life(kR, -2);
    b.move(kR, 70, "Street Wraith", Zone::Hand, Zone::Graveyard, Cause::Discard);       // K
    b.draw(kR, 71, "Troll of Khazad-dûm");
    b.act(kR, DecisionKind::Priority, ActionType::ActivateAbility, 71);                 // ciclo della terra
    b.act(kR, DecisionKind::PayMana, ActionType::ChooseOption, kNoCard, 1);             // una decisione in mezzo
    b.move(kR, 71, "Troll of Khazad-dûm", Zone::Hand, Zone::Exile, Cause::Discard);     // K (Sanctifier: in esilio)
    b.move(kR, 72, "Swamp", Zone::Library, Zone::Hand, Cause::Search);                  // C (ciclo del Troll)
    b.draw(kR, 73, "Mishra's Bauble");
    b.act(kR, DecisionKind::DiscardCard, ActionType::ChooseCard, 73);                   // Fear of Missing Out
    b.move(kR, 73, "Mishra's Bauble", Zone::Hand, Zone::Exile, Cause::Discard);         // S (in esilio)
    b.move(kR, 74, "Nethergoyf", Zone::Library, Zone::Graveyard, Cause::Mill);          // M
    b.move(kR, 74, "Nethergoyf", Zone::Graveyard, Zone::Stack, Cause::Cast);            // Y
    b.move(kR, 75, "Thoughtseize", Zone::Library, Zone::Graveyard, Cause::Surveil);     // V
    b.move(kR, 76, "Lightning Bolt", Zone::Library, Zone::Exile, Cause::Mill);          // M (Sanctifier)

    b.new_turn(kU);  // turno 3, UW
    b.move(kU, 27, "Quantum Riddler", Zone::Exile, Zone::Stack, Cause::Cast);           // X

    const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 3), b.log);
    const PlayerRecord& u = r.players[kU];
    CHECK(copy_of(u, "Ephemerate").events == ev({{1, 'P'}, {1, 'L'}}));
    CHECK(copy_of(u, "Solitude").events == ev({{1, 'C'}}));
    CHECK(copy_of(u, "Plains").events == ev({{1, 'F'}}));
    CHECK(copy_of(u, "Guide of Souls").events == ev({{1, 'P'}, {1, 'L'}, {1, 'R'}}));
    CHECK(copy_of(u, "Hallowed Fountain").events == ev({{1, 'P'}, {1, 'T'}}));
    CHECK(copy_of(u, "Witch Enchanter").events == ev({{1, 'P'}, {1, 'E'}}));
    CHECK(copy_of(u, "Quantum Riddler").events == ev({{1, 'P'}, {1, 'L'}, {3, 'X'}}));
    CHECK(copy_of(u, "Seam Rip").events == ev({{1, '?'}}));
    CHECK(copy_of(u, "Ocelot Pride").events == ev({{2, 'P'}, {2, 'S'}}));
    CHECK(u.cards.size() == 9);  // il token non c'e'
    // Le copie in ordine di primo evento.
    CHECK(u.cards[0].def == def("Ephemerate"));
    CHECK(u.cards[1].def == def("Solitude"));
    CHECK(u.cards[8].def == def("Ocelot Pride"));

    const PlayerRecord& k = r.players[kR];
    CHECK(copy_of(k, "Street Wraith").events == ev({{2, 'P'}, {2, 'K'}}));
    CHECK(copy_of(k, "Troll of Khazad-dûm").events == ev({{2, 'P'}, {2, 'K'}}));
    CHECK(copy_of(k, "Swamp").events == ev({{2, 'C'}}));
    CHECK(copy_of(k, "Mishra's Bauble").events == ev({{2, 'P'}, {2, 'S'}}));
    CHECK(copy_of(k, "Nethergoyf").events == ev({{2, 'M'}, {2, 'Y'}}));
    CHECK(copy_of(k, "Thoughtseize").events == ev({{2, 'V'}}));
    CHECK(copy_of(k, "Lightning Bolt").events == ev({{2, 'M'}}));

    // In mano alla fine: Solitude (cercata), Guide of Souls (ritornata) per UW; lo Swamp cercato per Rakdos.
    CHECK(hand_at_end(u) == std::vector<CardDefId>{def("Solitude"), def("Guide of Souls")});
    CHECK(u.hand_end == 2);
    CHECK(hand_at_end(k) == std::vector<CardDefId>{def("Swamp")});
    CHECK(k.hand_end == 1);
}

TEST_CASE("Metriche: terre in campo alla fine di ogni proprio turno, Witch come terra solo se giocata", "[metriche]") {
    LogBuilder b;
    b.new_turn(kU);  // 1
    b.draw(kU, 0, "Plains");
    b.move(kU, 0, "Plains", Zone::Hand, Zone::Battlefield, Cause::Play);
    b.new_turn(kR);  // 2
    b.draw(kR, 60, "Swamp");
    b.move(kR, 60, "Swamp", Zone::Hand, Zone::Battlefield, Cause::Play);
    b.draw(kR, 61, "Bloodstained Mire");
    b.move(kR, 61, "Bloodstained Mire", Zone::Hand, Zone::Battlefield, Cause::Play);
    b.move(kR, 61, "Bloodstained Mire", Zone::Battlefield, Zone::Graveyard, Cause::Cost);  // la fetch si rompe
    b.move(kR, 62, "Swamp", Zone::Library, Zone::Battlefield, Cause::Search);
    b.new_turn(kU);  // 3
    b.draw(kU, 1, "Witch Enchanter");
    b.move(kU, 1, "Witch Enchanter", Zone::Hand, Zone::Battlefield, Cause::Play);  // Witch-Blessed Meadow
    b.draw(kU, 2, "Witch Enchanter");
    b.move(kU, 2, "Witch Enchanter", Zone::Hand, Zone::Stack, Cause::Cast);      // la creatura
    b.move(kU, 2, "Witch Enchanter", Zone::Stack, Zone::Battlefield, Cause::Resolve);
    b.new_turn(kR);  // 4: la partita finisce qui
    b.move(kU, 2, "Witch Enchanter", Zone::Battlefield, Zone::Hand, Cause::Resolve);
    b.move(kU, 0, "Plains", Zone::Battlefield, Zone::Graveyard, Cause::Destroy);  // nel turno di Rakdos

    const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 4), b.log);
    CHECK(r.players[kU].lands == std::vector<int>{1, 2});
    CHECK(r.players[kR].lands == std::vector<int>{2, 2});
    CHECK(copy_of(r.players[kU], "Witch Enchanter", 0).events == ev({{3, 'P'}, {3, 'T'}}));
    CHECK(copy_of(r.players[kU], "Witch Enchanter", 1).events == ev({{3, 'P'}, {3, 'L'}, {4, 'R'}}));
}

TEST_CASE("Metriche: il delirium di Rakdos con la convenzione del goldfish, perso e a fine partita", "[metriche]") {
    // UW in play: i turni pari sono di Rakdos (turno 2 = primo turno di Rakdos).
    auto to_gy = [](LogBuilder& b, PlayerId p, CardId c, const char* name) {
        b.move(p, c, name, Zone::Library, Zone::Graveyard, Cause::Mill);
    };
    SECTION("nel turno di UW: conta dal turno successivo di Rakdos") {
        LogBuilder b;
        b.new_turn(kU);
        b.new_turn(kR);
        to_gy(b, kR, 60, "Lightning Bolt");
        to_gy(b, kR, 61, "Thoughtseize");
        to_gy(b, kR, 62, "Mishra's Bauble");
        b.move(kR, 130, "Nethergoyf", Zone::Battlefield, Zone::Graveyard, Cause::StateBased);  // un token non conta
        to_gy(b, kU, 0, "Plains");  // il cimitero di UW non conta
        to_gy(b, kU, 1, "Solitude");
        to_gy(b, kU, 2, "Ephemerate");
        to_gy(b, kU, 3, "Teferi, Time Raveler");
        b.new_turn(kU);  // 3
        to_gy(b, kR, 63, "Marsh Flats");
        b.new_turn(kR);  // 4
        b.move(kR, 62, "Mishra's Bauble", Zone::Graveyard, Zone::Exile, Cause::Exile);  // Thraben Charm
        to_gy(b, kR, 64, "Mishra's Bauble");
        const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 4), b.log);
        CHECK(r.delirium.reached);
        CHECK(r.delirium.turn == 3);
        CHECK(r.delirium.rakdos_turn == 2);  // rakdos_turn_reached: il turno dopo, non il 1 appena giocato
        CHECK(r.delirium.rakdos_turn == rakdos_turn_reached(3, false));
        CHECK(r.delirium.lost == 1);
        CHECK(r.delirium.at_end);
    }
    SECTION("nel turno di Rakdos: conta quel turno; perso e non ripreso") {
        LogBuilder b;
        b.new_turn(kU);
        b.new_turn(kR);
        to_gy(b, kR, 60, "Lightning Bolt");
        to_gy(b, kR, 61, "Thoughtseize");
        to_gy(b, kR, 62, "Mishra's Bauble");
        to_gy(b, kR, 63, "Marsh Flats");
        b.new_turn(kU);
        b.move(kR, 60, "Lightning Bolt", Zone::Graveyard, Zone::Exile, Cause::Exile);
        const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 3), b.log);
        CHECK(r.delirium.reached);
        CHECK(r.delirium.turn == 2);
        CHECK(r.delirium.rakdos_turn == 1);
        CHECK(r.delirium.lost == 1);
        CHECK_FALSE(r.delirium.at_end);
    }
    SECTION("mai") {
        LogBuilder b;
        b.new_turn(kU);
        to_gy(b, kR, 60, "Lightning Bolt");
        const CompactRecord r = build_compact_record(db_with_tokens(), game_info(true, 1), b.log);
        CHECK_FALSE(r.delirium.reached);
        CHECK(r.delirium.lost == 0);
        CHECK_FALSE(r.delirium.at_end);
    }
}

TEST_CASE("Metriche: la riga JSON del record", "[metriche]") {
    CompactRecord r;
    r.game.seed = 7;
    r.game.uw_on_play = true;
    r.game.winner = kMatchupRakdos;
    r.game.ending = "vita a zero";
    r.game.turns = 3;
    r.game.uw_life = -1;
    r.game.rakdos_life = 18;
    PlayerRecord& u = r.players[kU];
    u.mulligans = 1;
    u.hands = {{def("Plains"), def("Solitude")}, {def("Ephemerate")}};
    u.bottom = {def("Solitude")};
    u.lands = {1, 1};
    u.hand_end = 1;
    u.cards = {CopyRecord{def("Ephemerate"), {{0, 'I'}, {2, 'L'}}}, CopyRecord{def("Plains"), {{1, 'P'}}}};
    PlayerRecord& k = r.players[kR];
    k.hands = {{def("Swamp")}};
    k.lands = {1};
    k.cards = {CopyRecord{def("Troll of Khazad-dûm"), {{2, 'K'}}}};

    CHECK(compact_record_json(db_with_tokens(), r) ==
          R"({"seed":7,"uw_in_play":true,"vincitore":"Rakdos","fine":"vita a zero","turni":3,"vita_uw":-1,"vita_rakdos":18,)"
          R"("delirium":null,)"
          R"("uw":{"mulligan":1,"mani":[["Plains","Solitude"],["Ephemerate"]],"fondo":["Solitude"],"terre":[1,1],"mano_fine":1,)"
          R"("carte":[{"c":"Ephemerate","e":[[0,"I"],[2,"L"]]},{"c":"Plains","e":[[1,"P"]]}]},)"
          R"("rakdos":{"mulligan":0,"mani":[["Swamp"]],"fondo":[],"terre":[1],"mano_fine":0,)"
          R"("carte":[{"c":"Troll of Khazad-dûm","e":[[2,"K"]]}]}})");

    r.delirium = DeliriumRecord{true, 4, 2, 1, true};
    const std::string j = compact_record_json(db_with_tokens(), r);
    CHECK(j.find(R"("delirium":{"turno":4,"turno_rakdos":2,"perso":1,"fine":true},)") != std::string::npos);
}

TEST_CASE("Metriche: i controlli di coerenza trovano un record sbagliato", "[metriche]") {
    // Un record coerente: una partita vera.
    const MatchupConfig c = v1_config();
    GameLog log;
    const MatchupGame g = play_matchup(c, 3, &log);
    const CompactRecord good = build_compact_record(db_with_tokens(), g, log);
    CHECK(check_compact_record(db_with_tokens(), good, c.uw, c.rakdos).empty());

    auto problems = [&](CompactRecord r) { return check_compact_record(db_with_tokens(), r, c.uw, c.rakdos).size(); };
    SECTION("un codice non previsto") {
        CompactRecord r = good;
        r.players[kU].cards.push_back(CopyRecord{def("Seam Rip"), {{3, '?'}}});
        CHECK(problems(r) >= 1);
    }
    SECTION("piu' copie di quante nella lista") {
        CompactRecord r = good;
        for (int i = 0; i < 2; ++i) r.players[kU].cards.push_back(CopyRecord{def("Teferi, Time Raveler"), {{3, 'M'}}});
        CHECK(problems(r) >= 1);
    }
    SECTION("mano iniziale diversa da 7 meno i mulligan") {
        CompactRecord r = good;
        r.players[kR].mulligans += 1;
        CHECK(problems(r) >= 1);
    }
    SECTION("mano_fine diversa da arrivi meno uscite") {
        CompactRecord r = good;
        r.players[kU].hand_end += 1;
        CHECK(problems(r) >= 1);
    }
    SECTION("una voce di terre in meno") {
        CompactRecord r = good;
        r.players[kR].lands.pop_back();
        CHECK(problems(r) >= 1);
    }
    SECTION("una mano del mulligan non da 7") {
        CompactRecord r = good;
        r.players[kU].hands[0].pop_back();
        CHECK(problems(r) >= 1);
    }
}

// --- 2. Partite vere -----------------------------------------------------------------------------------

namespace {

// Lo stato visto dall'osservatore, da confrontare con il record (che viene solo dal registro).
class StateProbe final : public MatchupObserver {
public:
    explicit StateProbe(DeadCardTracker& dead) : dead_(&dead), kept_{KeptHandTracker(kU), KeptHandTracker(kR)} {}

    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override {
        dead_->on_decision(s, legal, chosen);
        for (KeptHandTracker& k : kept_) k.on_decision(s, legal, chosen);
        if (s.pending.kind == DecisionKind::Mulligan) {
            std::vector<CardDefId> hand;
            for (const CardId c : s.players[s.pending.player].hand) hand.push_back(s.objects[c].def);
            hands[s.pending.player].push_back(hand);
        }
        if (first_delirium_turn == 0 && s.players[kR].gy.delirium()) first_delirium_turn = s.turn.turn_number;
    }
    void on_end(const GameState& s) override {
        dead_->on_end(s);
        for (KeptHandTracker& k : kept_) k.on_end(s);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            for (const CardId c : s.players[p].hand) final_hand[p].push_back(s.objects[c].def);
            for (const CardId c : s.battlefield) {
                if (s.objects[c].controller == p && effective_def(s, db_with_tokens(), c).has_type(CardType::Land)) {
                    ++final_lands[p];
                }
            }
        }
        delirium_at_end = s.players[kR].gy.delirium();
        active_at_end = s.turn.active_player;
    }
    [[nodiscard]] const std::vector<CardDefId>& kept(PlayerId p) const { return kept_[p].kept(); }

    std::array<std::vector<std::vector<CardDefId>>, 2> hands;
    std::array<std::vector<CardDefId>, 2> final_hand;
    std::array<int, 2> final_lands{};
    int first_delirium_turn = 0;
    bool delirium_at_end = false;
    PlayerId active_at_end = kNoPlayer;

private:
    DeadCardTracker* dead_;
    std::array<KeptHandTracker, 2> kept_;
};

std::vector<CardDefId> sorted(std::vector<CardDefId> v) {
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

TEST_CASE("Metriche: il record non cambia la partita", "[metriche][matchup]") {
    const MatchupConfig c = v1_config();
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        GameLog log1, log2;
        const MatchupGame without = play_matchup(c, seed);
        const MatchupGame with1 = play_matchup(c, seed, &log1);
        const CompactRecord r1 = build_compact_record(db_with_tokens(), with1, log1);
        const MatchupGame with2 = play_matchup(c, seed, &log2);
        const CompactRecord r2 = build_compact_record(db_with_tokens(), with2, log2);
        CHECK(matchup_game_json(without) == matchup_game_json(with1));
        CHECK(matchup_game_json(without) == matchup_game_json(with2));
        CHECK(log1.entries == log2.entries);
        CHECK(compact_record_json(db_with_tokens(), r1) == compact_record_json(db_with_tokens(), r2));
        // L'esito nella riga del record e' quello di partite.jsonl.
        CHECK(r1.game.winner == without.winner);
        CHECK(r1.game.turns == without.turns);
    }
}

TEST_CASE("Metriche: in partite vere il record coincide con lo stato e con le carte morte", "[metriche][matchup]") {
    const MatchupConfig c = v1_config();
    DeadCardTracker dead(db_with_tokens());
    std::array<std::map<std::string, long long>, 2> in_hand_at_end;  // dal record
    constexpr std::uint64_t kGames = 60;
    for (std::uint64_t seed = 1; seed <= kGames; ++seed) {
        INFO("seed " << seed);
        GameLog log;
        StateProbe probe(dead);
        const MatchupGame g = play_matchup(c, seed, &log, nullptr, nullptr, nullptr, &probe);
        const CompactRecord r = build_compact_record(db_with_tokens(), g, log);
        const auto problems = check_compact_record(db_with_tokens(), r, c.uw, c.rakdos);
        for (const auto& p : problems) UNSCOPED_INFO(p);
        CHECK(problems.empty());
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            const PlayerRecord& pr = r.players[p];
            CHECK(pr.hands == probe.hands[p]);  // ogni mano del mulligan, nello stesso ordine
            CHECK(pr.mulligans == (p == kU ? g.uw_mulligans : g.rakdos_mulligans));
            std::vector<CardDefId> opening;
            for (const CopyRecord& copy : pr.cards) {
                if (!copy.events.empty() && copy.events.front().code == 'I') opening.push_back(copy.def);
            }
            CHECK(opening == probe.kept(p));  // la mano tenuta, nell'ordine della mano
            CHECK(pr.hand_end == static_cast<int>(probe.final_hand[p].size()));
            CHECK(sorted(hand_at_end(pr)) == sorted(probe.final_hand[p]));
            std::set<CardDefId> names(probe.final_hand[p].begin(), probe.final_hand[p].end());
            for (const CardDefId d : names) ++in_hand_at_end[p][db_with_tokens().get(d).name];
        }
        CHECK(r.players[probe.active_at_end].lands.back() == probe.final_lands[probe.active_at_end]);
        CHECK(r.delirium.at_end == probe.delirium_at_end);
        if (probe.first_delirium_turn != 0) {
            CHECK(r.delirium.reached);
            CHECK(r.delirium.turn <= probe.first_delirium_turn);
        }
    }
    // Carte morte: le partite con la carta in mano alla fine, per mazzo (play e draw sommati).
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        for (const auto& [name, counts] : dead.diagnostics()[p].cards) {
            INFO(name);
            const auto it = in_hand_at_end[p].find(name);
            CHECK(counts[kOnPlay].in_hand_at_end + counts[kOnDraw].in_hand_at_end ==
                  (it == in_hand_at_end[p].end() ? 0 : it->second));
        }
    }
}

TEST_CASE("Metriche: in partite vere il motore produce ogni codice, sulle carte giuste", "[metriche][matchup]") {
    const MatchupConfig c = v1_config();
    std::map<std::pair<std::string, char>, long long> seen;  // (carta, codice) -> volte
    std::array<std::map<char, long long>, 2> codes;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        GameLog log;
        const MatchupGame g = play_matchup(c, seed, &log);
        const CompactRecord r = build_compact_record(db_with_tokens(), g, log);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            for (const CopyRecord& copy : r.players[p].cards) {
                for (const CopyEvent& e : copy.events) {
                    ++seen[{db_with_tokens().get(copy.def).name, e.code}];
                    ++codes[p][e.code];
                }
            }
        }
    }
    auto only = [&](char code, std::set<std::string> allowed) {
        for (const auto& [key, n] : seen) {
            if (key.second == code) {
                INFO("codice " << code << " su " << key.first);
                CHECK(allowed.contains(key.first));
            }
        }
    };
    CHECK(codes[kU]['?'] + codes[kR]['?'] == 0);
    for (const char code : {'I', 'P', 'L', 'T'}) {
        CHECK(codes[kU][code] > 0);
        CHECK(codes[kR][code] > 0);
    }
    only('K', {"Street Wraith", "Troll of Khazad-dûm"});
    only('Y', {"Nethergoyf", "Detective's Phoenix"});
    // Warp: Quantum Riddler e Starfield Shepherd ("Warp {1}{W}", Oracle in cards/starfield_shepherd.cpp);
    // rimbalzo: Ephemerate.
    only('X', {"Quantum Riddler", "Starfield Shepherd", "Ephemerate"});
    CHECK(seen[{"Street Wraith", 'K'}] > 0);
    CHECK(seen[{"Troll of Khazad-dûm", 'K'}] > 0);
    CHECK(seen[{"Nethergoyf", 'Y'}] > 0);
    CHECK(seen[{"Quantum Riddler", 'X'}] > 0);
    CHECK(seen[{"Solitude", 'C'}] > 0);         // Recruiter of the Guard
    CHECK(seen[{"Witch Enchanter", 'E'}] > 0);  // evocazione di Solitude
    CHECK(seen[{"Witch Enchanter", 'T'}] > 0);  // Witch-Blessed Meadow
    CHECK(seen[{"Teferi, Time Raveler", 'L'}] > 0);
    CHECK(codes[kU]['R'] + codes[kR]['R'] > 0);  // Teferi -3
    CHECK(codes[kU]['S'] > 0);                   // Thoughtseize
    CHECK(codes[kR]['M'] > 0);                   // Stitcher's Supplier
    CHECK(codes[kU]['F'] + codes[kR]['F'] > 0);  // le fetch
    CHECK(codes[kR]['C'] > 0);                   // ciclo del Troll
    CHECK(codes[kU]['V'] + codes[kR]['V'] > 0);  // Meticulous Archive, Raucous Theater
}

TEST_CASE("Metriche: la prima mano da 7 di ogni giocatore dipende solo dal seed", "[metriche][matchup]") {
    // Le due prime mani sono pescate da start_game prima di qualsiasi decisione: non dipendono dal mulligan
    // dell'altro giocatore ne' da chi decide. Qui si gioca la stessa partita con la prima decisione di
    // mulligan di un giocatore forzata a "tieni" e a "mulligan", e si confrontano le prime mani dei due.
    const CardDatabase& db = db_with_tokens();
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        INFO("seed " << seed);
        const bool uw_on_play = seed % 2 == 1;
        std::array<std::vector<CardDefId>, 2> reference;
        {
            GameState s = create_game(db, uw_sim_deck(), rakdos_deck(), seed);
            start_game(s, db, StartOptions{uw_on_play ? kU : kR, true});
            for (PlayerId p = 0; p < kNumPlayers; ++p) {
                for (const CardId c : s.players[p].hand) reference[p].push_back(s.objects[c].def);
            }
        }
        for (PlayerId forced = 0; forced < kNumPlayers; ++forced) {
            for (const ActionType first : {ActionType::Keep, ActionType::Mulligan}) {
                GameState s = create_game(db, uw_sim_deck(), rakdos_deck(), seed);
                GameLog log;
                start_game(s, db, StartOptions{uw_on_play ? kU : kR, true}, &log);
                UwAgent uw(db, seed, UwAgentOptions{});
                RakdosAgent rakdos(db, seed, RakdosAgentOptions{});
                bool done = false;
                for (int guard = 0; guard < 200 && s.turn.turn_number < 2; ++guard) {
                    const std::vector<Action> actions = legal_actions(s, db);
                    Action a = s.pending.player == kU ? uw.choose(s, actions) : rakdos.choose(s, actions);
                    if (!done && s.pending.kind == DecisionKind::Mulligan && s.pending.player == forced) {
                        a = Action{first};
                        done = true;
                    }
                    apply(s, db, a, &log);
                }
                MatchupGame g;
                g.seed = seed;
                g.uw_on_play = uw_on_play;
                const CompactRecord r = build_compact_record(db, g, log);
                for (PlayerId p = 0; p < kNumPlayers; ++p) {
                    REQUIRE_FALSE(r.players[p].hands.empty());
                    CHECK(r.players[p].hands[0] == reference[p]);
                }
            }
        }
    }
}
