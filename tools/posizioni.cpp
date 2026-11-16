#include "posizioni.hpp"

#include "game.hpp"
#include "game_setup.hpp"
#include "goldfish_agent.hpp"
#include "zones.hpp"

#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace mtg {

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kEveryone = (1u << kNumPlayers) - 1u;

using Chooser = std::function<std::optional<Action>(const GameState&, const std::vector<Action>&)>;

// Una partita preparata senza mulligan: le carte nominate si prendono dal grimorio (la prima copia), poi start_game.
struct Prep {
    const CardDatabase& db;
    GameState s;

    Prep(const CardDatabase& d, const Decklist& uw, const Decklist& rakdos) : db(d), s(create_game(d, uw, rakdos, 1)) {}

    [[nodiscard]] std::string_view name(CardId id) const { return db.get(s.objects[id].def).name; }

    CardId put(PlayerId p, const char* card, Zone to) {
        for (const CardId id : s.players[p].library) {
            if (name(id) == card) {
                move_object(s, db, s.ref(id), to, Cause::Test);
                return id;
            }
        }
        throw EngineError(std::string("posizione: carta non trovata nel grimorio: ") + card);
    }
    void put(PlayerId p, std::initializer_list<const char*> cards, Zone to) {
        for (const char* c : cards) put(p, c, to);
    }
    CardId on_field(std::string_view card, std::size_t skip = 0) const {
        for (const CardId id : s.battlefield) {
            if (name(id) == card && skip-- == 0) return id;
        }
        throw EngineError(std::string("posizione: carta non in campo: ") + std::string(card));
    }
    // Detective's Phoenix conferita sulla creatura `host` (come se fosse stata lanciata per conferimento).
    void bestow(std::string_view host) {
        CardObject& phoenix = s.objects[on_field("Detective's Phoenix")];
        phoenix.set_flag(ObjectFlag::Bestowed);
        phoenix.attached_to = s.ref(on_field(host));
    }
    void start(PlayerId starting) { start_game(s, db, StartOptions{starting, false}); }

    // Avanza: a ogni decisione `choose` sceglie, o (nullopt) l'avversario del goldfish (passa, tiene, non attacca, non
    // blocca, la prima opzione altrove), finche' `done` non e' vero.
    void drive(const std::function<bool(const GameState&)>& done, const Chooser& choose = {}) {
        const GoldfishAgent idle;
        for (int i = 0; i < 500; ++i) {
            if (done(s)) return;
            if (s.pending.kind == DecisionKind::GameOver) break;
            const std::vector<Action> legal = legal_actions(s, db);
            std::optional<Action> a = choose ? choose(s, legal) : std::nullopt;
            apply(s, db, a ? *a : idle.choose(s, legal));
        }
        throw EngineError("posizione: decisione non raggiunta");
    }

    // L'azione legale di quel tipo sulla carta con quel nome (vuoto: qualsiasi carta) e quel valore (-1: qualsiasi).
    std::optional<Action> pick(const std::vector<Action>& legal, ActionType type, std::string_view card = {},
                               int value = -1) const {
        for (const Action& a : legal) {
            if (a.type != type || (value >= 0 && a.value != value)) continue;
            if (!card.empty() && (a.card == kNoCard || name(a.card) != card)) continue;
            return a;
        }
        return std::nullopt;
    }
};

bool uw_priority(const GameState& s) { return s.pending.kind == DecisionKind::Priority && s.pending.player == kUw; }

// La prima priorita' di UW nel turno di Rakdos, con la pila vuota.
bool uw_priority_in_rakdos_turn(const GameState& s) {
    return uw_priority(s) && s.turn.active_player == kRakdos && s.stack_items.empty();
}

// La priorita' di UW nella sua prima fase principale, con la pila vuota.
bool uw_main_phase(const GameState& s) {
    return uw_priority(s) && s.turn.active_player == kUw && s.turn.step == Step::Main1 && s.stack_items.empty();
}

bool top_of_stack_is(const GameState& s, StackEffect e) {
    return !s.stack_items.empty() && s.stack_items.back().effect == e;
}

// --- Le posizioni ---------------------------------------------------------------------------------------------

// 1: Moonshadow appena lanciato (Rakdos ha appena risolto il lancio), UW con due mana, March e Solitude in mano.
GameState moonshadow(Prep& p, bool two_mana) {
    p.put(kUw, {"March of Otherworldly Light", "Solitude"}, Zone::Hand);
    p.put(kUw, two_mana ? std::initializer_list<const char*>{"Plains", "Plains"} : std::initializer_list<const char*>{"Plains"},
          Zone::Battlefield);
    p.put(kRakdos, "Moonshadow", Zone::Hand);
    p.put(kRakdos, "Swamp", Zone::Battlefield);
    p.start(kRakdos);
    p.drive(
        [&](const GameState& s) {
            return uw_priority_in_rakdos_turn(s) && s.turn.step == Step::Main1 && s.players[kRakdos].hand.empty();
        },
        [&](const GameState& s, const std::vector<Action>& legal) -> std::optional<Action> {
            if (s.pending.player == kRakdos && s.pending.kind == DecisionKind::Priority) {
                return p.pick(legal, ActionType::CastSpell, "Moonshadow");
            }
            return std::nullopt;
        });
    return p.s;
}

// 2: Sanctifier en-Vec in mano al secondo turno di UW, Rakdos ha mostrato il nero.
GameState sanctifier(Prep& p, std::initializer_list<const char*> rakdos_graveyard) {
    p.put(kUw, {"Sanctifier en-Vec", "Guide of Souls", "Ocelot Pride"}, Zone::Hand);
    p.put(kUw, {"Plains", "Plains"}, Zone::Battlefield);
    p.put(kRakdos, "Swamp", Zone::Battlefield);
    p.put(kRakdos, rakdos_graveyard, Zone::Graveyard);
    p.start(kUw);
    p.drive(uw_main_phase);
    p.s.turn.turn_number = 3;  // secondo turno di UW (cambiato nella fase principale: nessuna pescata in piu')
    return p.s;
}

// 3: Rakdos a un tipo dal delirium (o lontano), Nethergoyf in campo, Thraben Charm in mano.
GameState thraben_delirium(Prep& p, std::initializer_list<const char*> rakdos_graveyard) {
    p.put(kUw, "Thraben Charm", Zone::Hand);
    p.put(kUw, {"Plains", "Plains"}, Zone::Battlefield);
    p.put(kRakdos, "Nethergoyf", Zone::Battlefield);
    p.put(kRakdos, rakdos_graveyard, Zone::Graveyard);
    p.start(kRakdos);
    p.drive(uw_priority_in_rakdos_turn);
    return p.s;
}

// 4: Teferi in mano con tre mana e Guide of Souls pronta ad attaccare; Rakdos con il rosso aperto (o no).
GameState teferi(Prep& p, const char* rakdos_land) {
    p.put(kUw, "Teferi, Time Raveler", Zone::Hand);
    p.put(kUw, {"Hallowed Fountain", "Plains", "Plains", "Guide of Souls"}, Zone::Battlefield);
    p.put(kRakdos, rakdos_land, Zone::Battlefield);
    p.start(kUw);
    p.drive(uw_main_phase);
    return p.s;
}

// 5: Detective's Phoenix conferita su una creatura di Rakdos, Solitude in mano (evocabile con Witch Enchanter).
GameState phoenix(Prep& p, bool on_nethergoyf) {
    p.put(kUw, {"Solitude", "Witch Enchanter", "Ocelot Pride"}, Zone::Hand);
    p.put(kUw, {"Plains", "Plains", "Plains", "Hallowed Fountain"}, Zone::Battlefield);
    p.put(kRakdos, {"Nethergoyf", "Detective's Phoenix"}, Zone::Battlefield);
    if (!on_nethergoyf) p.put(kRakdos, "Stitcher's Supplier", Zone::Battlefield);
    p.put(kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    p.bestow(on_nethergoyf ? "Nethergoyf" : "Stitcher's Supplier");
    p.start(kRakdos);
    p.drive(uw_priority_in_rakdos_turn);
    return p.s;
}

// 6: terzo turno di UW, Quantum Riddler e Consign to Memory in mano, tre terre con due fonti blu.
GameState riddler(Prep& p, bool flare) {
    p.put(kUw, {"Quantum Riddler", "Consign to Memory", "Teferi, Time Raveler"}, Zone::Hand);
    p.put(kUw, {"Meticulous Archive", "Hallowed Fountain", "Plains"}, Zone::Battlefield);
    if (flare) {
        p.put(kRakdos, {"Swamp", "Blood Crypt", "Blackcleave Cliffs", "Raucous Theater", "Nethergoyf"}, Zone::Battlefield);
        const CardId f = p.put(kRakdos, "Flare of Malice", Zone::Hand);
        p.s.objects[f].known_to = kEveryone;  // nota a UW, come se fosse stata rivelata
    } else {
        p.put(kRakdos, {"Swamp", "Nethergoyf"}, Zone::Battlefield);
    }
    p.put(kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    p.start(kUw);
    p.drive(uw_main_phase);
    p.s.turn.turn_number = 5;  // terzo turno di UW (cambiato nella fase principale: nessuna pescata in piu')
    return p.s;
}

// 7: Phelia ha attaccato ed esiliato un permanente di Rakdos; alla sottofase finale il ritorno e' in pila, UW ha
// Consign to Memory e {U} aperto.
GameState phelia(Prep& p, const char* exiled) {
    p.put(kUw, "Consign to Memory", Zone::Hand);
    p.put(kUw, {"Phelia, Exuberant Shepherd", "Hallowed Fountain", "Plains"}, Zone::Battlefield);
    p.put(kRakdos, {"Swamp", exiled}, Zone::Battlefield);
    p.put(kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    p.start(kUw);
    p.drive([](const GameState& s) { return uw_priority(s) && top_of_stack_is(s, StackEffect::PheliaReturn); },
            [&](const GameState& s, const std::vector<Action>& legal) -> std::optional<Action> {
                if (s.pending.player != kUw) return std::nullopt;
                if (s.pending.kind == DecisionKind::DeclareAttacker) return p.pick(legal, ActionType::TargetPlayer);
                if (s.pending.kind == DecisionKind::ChooseTarget) return p.pick(legal, ActionType::TargetObject, exiled);
                return std::nullopt;
            });
    return p.s;
}

// 8: Fear of Missing Out di Rakdos; UW con Guide of Souls, Ocelot Pride (Thraben Charm fa 4 danni) e Thraben Charm.
GameState fomo(Prep& p, bool delirium, bool trigger_on_stack) {
    p.put(kUw, "Thraben Charm", Zone::Hand);
    p.put(kUw, {"Plains", "Plains", "Guide of Souls", "Ocelot Pride"}, Zone::Battlefield);
    p.put(kRakdos, {"Fear of Missing Out", "Swamp"}, Zone::Battlefield);
    if (delirium) {
        p.put(kRakdos, {"Lightning Bolt", "Swamp", "Stitcher's Supplier", "Mishra's Bauble"}, Zone::Graveyard);
    } else {
        p.put(kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    }
    p.start(kRakdos);
    if (!trigger_on_stack) {
        p.drive(uw_priority_in_rakdos_turn);
        return p.s;
    }
    p.drive([](const GameState& s) { return uw_priority(s) && top_of_stack_is(s, StackEffect::UntapAndExtraCombat); },
            [&](const GameState& s, const std::vector<Action>& legal) -> std::optional<Action> {
                if (s.pending.player != kRakdos) return std::nullopt;
                if (s.pending.kind == DecisionKind::DeclareAttacker) return p.pick(legal, ActionType::TargetPlayer);
                if (s.pending.kind == DecisionKind::ChooseTarget) {
                    return p.pick(legal, ActionType::TargetObject, "Fear of Missing Out");
                }
                return std::nullopt;
            });
    return p.s;
}

// 9: due creature di Rakdos; UW con Solitude, Ephemerate, una carta bianca da esiliare e {W} aperto. Con `bolt`
// Rakdos ha Lightning Bolt in mano (nascosto) e il rosso aperto.
GameState solitude_ephemerate(Prep& p, bool bolt) {
    p.put(kUw, {"Solitude", "Ephemerate", "Ocelot Pride"}, Zone::Hand);
    p.put(kUw, "Plains", Zone::Battlefield);
    p.put(kRakdos, {"Nethergoyf", "Dark Matter Manipulator", "Swamp"}, Zone::Battlefield);
    if (bolt) {
        p.put(kRakdos, "Blood Crypt", Zone::Battlefield);
        p.put(kRakdos, "Lightning Bolt", Zone::Hand);
    }
    p.put(kRakdos, {"Lightning Bolt", "Swamp", "Mishra's Bauble"}, Zone::Graveyard);
    p.start(kRakdos);
    p.drive(uw_priority_in_rakdos_turn);
    return p.s;
}

// 10: terzo turno di UW, Recruiter of the Guard lanciato con tre terre: la decisione e' la ricerca.
GameState recruiter(Prep& p, bool threat, bool solitude_in_hand) {
    p.put(kUw, {"Recruiter of the Guard", "Ephemerate"}, Zone::Hand);
    if (solitude_in_hand) p.put(kUw, "Solitude", Zone::Hand);
    p.put(kUw, {"Plains", "Plains", "Hallowed Fountain"}, Zone::Battlefield);
    if (threat) {
        p.put(kRakdos, {"Swamp", "Nethergoyf"}, Zone::Battlefield);
        p.put(kRakdos, {"Lightning Bolt", "Swamp"}, Zone::Graveyard);
    } else {
        p.put(kRakdos, "Swamp", Zone::Battlefield);
        p.put(kRakdos, {"Lightning Bolt", "Swamp", "Thoughtseize", "Mishra's Bauble"}, Zone::Graveyard);
    }
    p.start(kUw);
    p.drive(uw_main_phase);
    p.s.turn.turn_number = 5;  // terzo turno di UW (cambiato nella fase principale: nessuna pescata in piu')
    p.drive([](const GameState& s) { return s.pending.kind == DecisionKind::SearchLibrary && s.pending.player == kUw; },
            [&](const GameState& s, const std::vector<Action>& legal) -> std::optional<Action> {
                if (s.pending.player == kUw && s.pending.kind == DecisionKind::Priority && s.stack_items.empty() &&
                    s.turn.step == Step::Main1) {
                    return p.pick(legal, ActionType::CastSpell, "Recruiter of the Guard");
                }
                return std::nullopt;
            });
    return p.s;
}

}  // namespace

std::vector<std::string> test_position_ids() {
    return {"1", "1c", "2", "2c", "3", "3c", "4", "4c", "5", "5c", "6", "6c", "7a", "7c",
            "8a", "8b", "8c", "9a", "9b", "10a", "10b", "10c"};
}

TestPosition test_position(const CardDatabase& db, const Decklist& uw, const Decklist& rakdos, const std::string& id) {
    Prep p(db, uw, rakdos);
    TestPosition t;
    t.id = id;
    if (id == "1" || id == "1c") {
        t.state = moonshadow(p, id == "1");
        t.situation = id == "1" ? "Moonshadow appena lanciato; UW con 2 mana, March e Solitude in mano"
                                : "Come 1, ma UW con 1 mana";
        t.indicated = id == "1" ? "March of Otherworldly Light con X=1 su Moonshadow, senza esiliare carte"
                                : "March con X=1 costa anche una carta: la linea non risparmia piu' le carte di Solitude";
    } else if (id == "2" || id == "2c") {
        t.state = id == "2" ? sanctifier(p, {"Mishra's Bauble"})
                            : sanctifier(p, {"Lightning Bolt", "Thoughtseize", "Swamp", "Mishra's Bauble",
                                             "Stitcher's Supplier"});
        t.situation = id == "2" ? "Secondo turno di UW, Sanctifier en-Vec in mano, cimitero di Rakdos quasi vuoto"
                                : "Come 2, ma il cimitero di Rakdos ha gia' il delirium";
        t.indicated = id == "2" ? "Lanciare Sanctifier en-Vec subito"
                                : "Sanctifier toglie le carte nere e rosse ma lascia terra e artefatto: non e' piu' "
                                  "'prima che il cimitero cresca'";
    } else if (id == "3" || id == "3c") {
        t.state = id == "3" ? thraben_delirium(p, {"Lightning Bolt", "Swamp", "Stitcher's Supplier"})
                            : thraben_delirium(p, {"Swamp"});
        t.situation = id == "3" ? "Rakdos a un tipo dal delirium, Nethergoyf in campo, Thraben Charm in mano"
                                : "Come 3, ma il cimitero di Rakdos ha un tipo solo";
        t.indicated = id == "3" ? "Thraben Charm: esiliare il cimitero di Rakdos"
                                : "Il delirium e' lontano: esiliare il cimitero adesso vale poco";
    } else if (id == "4" || id == "4c") {
        t.state = teferi(p, id == "4" ? "Blood Crypt" : "Swamp");
        t.situation = id == "4" ? "Teferi in mano con 3 mana, Guide of Souls in campo, Rakdos con il rosso aperto"
                                : "Come 4, ma Rakdos senza mana rosso";
        t.indicated = id == "4" ? "Teferi prima di attaccare" : "Senza rosso aperto l'ordine Teferi-attacco conta meno";
    } else if (id == "5" || id == "5c") {
        t.state = phoenix(p, id == "5");
        t.situation = id == "5" ? "Detective's Phoenix conferita su Nethergoyf, Solitude in mano"
                                : "Phoenix conferita su Stitcher's Supplier, Nethergoyf senza Phoenix";
        t.indicated = id == "5" ? "Solitude esilia Nethergoyf (la Phoenix resta come creatura piccola)"
                                : "Il bersaglio della linea guida non c'e': la creatura con la Phoenix e' piccola";
    } else if (id == "6" || id == "6c") {
        t.state = riddler(p, id == "6c");
        t.situation = id == "6" ? "Terzo turno di UW: Quantum Riddler e Consign in mano, tre terre con due fonti blu"
                                : "Come 6, ma Rakdos ha Flare of Malice in mano (nota a UW) e quattro mana";
        t.indicated = id == "6" ? "Riddler con il warp tenendo {U} aperto, Consign sul trigger di esilio: Riddler resta"
                                : "Flare e' colorata (Consign non la contrasta) e fa sacrificare Riddler";
    } else if (id == "7a" || id == "7c") {
        t.state = phelia(p, id == "7a" ? "Nethergoyf" : "Stitcher's Supplier");
        t.situation = id == "7a" ? "Phelia ha esiliato Nethergoyf; ritorno in pila, Consign in mano e {U} aperto"
                                 : "Come 7a, ma l'esiliato e' Stitcher's Supplier";
        t.indicated = id == "7a" ? "Consign to Memory sul ritorno: Nethergoyf resta in esilio"
                                 : "Un pezzo piccolo: non vale una Consign";
    } else if (id == "8a" || id == "8b" || id == "8c") {
        t.state = fomo(p, id != "8c", id == "8b");
        t.situation = id == "8a"   ? "Fear of Missing Out con il delirium, prima di attaccare; UW con due creature"
                      : id == "8b" ? "Trigger di FOMO in pila con il delirium"
                                   : "Fear of Missing Out senza delirium";
        t.indicated = id == "8a"   ? "Thraben Charm sui danni (4) su FOMO, subito"
                      : id == "8b" ? "Thraben Charm esilia i cimiteri: alla risoluzione il delirium non c'e' piu'"
                                   : "Senza delirium Thraben Charm non serve";
    } else if (id == "9a" || id == "9b") {
        t.state = solitude_ephemerate(p, id == "9b");
        t.situation = id == "9a" ? "Due creature di Rakdos, nessun rosso aperto; Solitude, Ephemerate e una carta "
                                   "bianca in mano, {W} aperto"
                                 : "Come 9a, ma Rakdos ha Lightning Bolt in mano (nascosto) e il rosso aperto";
        t.indicated = id == "9a" ? "Solitude evocata, Ephemerate su Solitude con il sacrificio in pila: due esili, "
                                   "Solitude resta"
                                 : "Bolt su Solitude in risposta a Ephemerate: la linea vale meno, secondo la "
                                   "probabilita' di Bolt";
    } else if (id == "10a" || id == "10b" || id == "10c") {
        t.state = recruiter(p, id != "10b", id == "10c");
        t.situation = id == "10a"   ? "Recruiter al terzo turno; minaccia di Rakdos in campo, Solitude non in mano"
                      : id == "10b" ? "Recruiter al terzo turno; nessuna minaccia, cimitero di Rakdos che cresce"
                                    : "Come 10a, ma Solitude gia' in mano";
        t.indicated = id == "10a"   ? "Recruiter cerca Solitude, e la si usa subito (evocata)"
                      : id == "10b" ? "Recruiter cerca Sanctifier en-Vec"
                                    : "Solitude c'e' gia': Recruiter cerca altro";
    } else {
        throw EngineError("posizione sconosciuta: " + id);
    }
    return t;
}

}  // namespace mtg
