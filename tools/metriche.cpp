#include "metriche.hpp"

#include "goldfish.hpp"

#include <algorithm>
#include <map>

#include <nlohmann/json.hpp>

namespace mtg {

namespace {

bool arrives_in_hand(char code) { return code == 'I' || code == 'P' || code == 'C' || code == 'R'; }
bool leaves_hand(char code) { return code == 'L' || code == 'T' || code == 'E' || code == 'S' || code == 'K'; }

// Il codice di uno spostamento dopo l'inizio della partita (docs/metriche.md, sezione 2); 0 se lo spostamento
// non e' un evento della copia (pila -> campo, campo -> cimitero, ...). `cycling`: la carta in mano e' stata
// appena attivata (il ciclo, che il motore registra come uno scarto).
char move_code(const LogEntry& e, bool cycling) {
    const Zone from = e.from;
    const Zone to = e.to;
    const Cause cause = e.cause;
    if (from == Zone::Library) {
        if (to == Zone::Hand) return cause == Cause::Draw ? 'P' : cause == Cause::Search ? 'C' : '?';
        if (to == Zone::Graveyard || to == Zone::Exile) {
            return cause == Cause::Mill ? 'M' : cause == Cause::Surveil ? 'V' : '?';
        }
        if (to == Zone::Battlefield) return cause == Cause::Search ? 'F' : '?';
        return '?';
    }
    if (from == Zone::Hand) {
        if (to == Zone::Stack && cause == Cause::Cast) return 'L';
        if (to == Zone::Battlefield && cause == Cause::Play) return 'T';
        if (to == Zone::Exile && cause == Cause::Cost) return 'E';
        if ((to == Zone::Graveyard || to == Zone::Exile) && cause == Cause::Discard) return cycling ? 'K' : 'S';
        return '?';
    }
    if (to == Zone::Hand) return from == Zone::Battlefield ? 'R' : '?';
    if (to == Zone::Stack && cause == Cause::Cast) {
        if (from == Zone::Graveyard) return 'Y';
        if (from == Zone::Exile) return 'X';
    }
    return 0;
}

}  // namespace

CompactRecord build_compact_record(const CardDatabase& db, const MatchupGame& game, const GameLog& log) {
    CompactRecord r;
    r.game = game;

    std::array<int, kMaxCards> copy_index{};  // indice della copia in PlayerRecord::cards, -1 se nessuna
    copy_index.fill(-1);
    std::array<CardDefId, kMaxCards> def_of{};
    def_of.fill(kNoDef);
    std::array<bool, kMaxCards> cycling{};  // attivata dalla mano (ciclo): il prossimo scarto e' un ciclo
    std::array<bool, kMaxCards> land{};     // in campo come terra
    std::array<std::vector<CardId>, kNumPlayers> hand;  // in ordine di arrivo, come PlayerState::hand
    std::array<int, kNumPlayers> lands{};
    std::array<int, kNumCardTypes> rakdos_types{};  // carte per tipo nel cimitero di Rakdos
    bool delirium = false;
    bool started = false;                       // la partita e' iniziata (primo inizio di step)
    PlayerId active = kNoPlayer;
    DecisionKind last_decision = DecisionKind::None;  // dell'ultima decisione nel registro

    auto add_event = [&](PlayerId owner, CardId c, std::uint16_t turn, char code) {
        PlayerRecord& p = r.players[owner];
        int& index = copy_index[c];
        if (index < 0) {
            index = static_cast<int>(p.cards.size());
            p.cards.push_back(CopyRecord{def_of[c], {}});
        }
        p.cards[static_cast<std::size_t>(index)].events.push_back(CopyEvent{turn, code});
    };
    auto rakdos_graveyard = [&](CardDefId d, int sign, const LogEntry& e) {
        const CardDef& def = db.get(d);
        for (std::size_t t = 0; t < kNumCardTypes; ++t) {
            if (def.has_type(static_cast<CardType>(t))) rakdos_types[t] += sign;
        }
        const auto types = std::count_if(rakdos_types.begin(), rakdos_types.end(), [](int n) { return n > 0; });
        const bool now = types >= 4;
        if (now && !delirium && !r.delirium.reached) {
            r.delirium.reached = true;
            r.delirium.turn = e.turn;
            r.delirium.rakdos_turn = rakdos_turn_reached(e.turn, !game.uw_on_play);
        }
        if (!now && delirium) ++r.delirium.lost;
        delirium = now;
    };

    for (const LogEntry& e : log.entries) {
        switch (e.kind) {
            case LogEntry::Kind::StepStart:
                if (!started) {  // la mano iniziale tenuta: dopo i mulligan e le carte in fondo
                    started = true;
                    for (PlayerId p = 0; p < kNumPlayers; ++p) {
                        for (const CardId c : hand[p]) add_event(p, c, 0, 'I');
                    }
                }
                if (e.step == Step::Untap) {
                    if (active != kNoPlayer) r.players[active].lands.push_back(lands[active]);
                    active = e.player;
                }
                break;

            case LogEntry::Kind::Action: {
                last_decision = e.decision;
                const PlayerId p = e.player;
                if (e.decision == DecisionKind::Mulligan && p < kNumPlayers) {
                    std::vector<CardDefId> cards;
                    for (const CardId c : hand[p]) cards.push_back(def_of[c]);
                    r.players[p].hands.push_back(std::move(cards));
                    if (e.action.type == ActionType::Mulligan) ++r.players[p].mulligans;
                }
                const CardId c = e.action.card;
                if (e.action.type == ActionType::ActivateAbility && c < kMaxCards && p < kNumPlayers &&
                    std::find(hand[p].begin(), hand[p].end(), c) != hand[p].end()) {
                    cycling[c] = true;
                }
                break;
            }

            case LogEntry::Kind::ZoneChange: {
                const CardId c = e.action.card;
                if (c >= kMaxCards || e.player >= kNumPlayers) break;  // i token non sono carte
                const PlayerId owner = e.player;
                def_of[c] = e.def;
                if (started) {
                    if (const char code = move_code(e, cycling[c]); code != 0) add_event(owner, c, e.turn, code);
                } else if (e.from == Zone::Hand && e.to == Zone::Library && last_decision == DecisionKind::BottomCards) {
                    r.players[owner].bottom.push_back(e.def);
                }
                if (e.from == Zone::Hand) {
                    std::erase(hand[owner], c);
                    cycling[c] = false;
                }
                if (e.to == Zone::Hand) hand[owner].push_back(c);
                if (e.from == Zone::Battlefield && land[c]) {
                    land[c] = false;
                    --lands[owner];
                }
                if (e.to == Zone::Battlefield && (e.cause == Cause::Play || db.get(e.def).has_type(CardType::Land))) {
                    land[c] = true;  // Witch Enchanter giocata come terra: Witch-Blessed Meadow
                    ++lands[owner];
                }
                if (owner == kMatchupRakdos) {
                    if (e.from == Zone::Graveyard) rakdos_graveyard(e.def, -1, e);
                    if (e.to == Zone::Graveyard) rakdos_graveyard(e.def, +1, e);
                }
                break;
            }

            case LogEntry::Kind::AutoPass:
            case LogEntry::Kind::AutoPay:
            case LogEntry::Kind::LifeChange: break;
        }
    }
    if (active != kNoPlayer) r.players[active].lands.push_back(lands[active]);  // l'ultimo turno: a fine partita
    for (PlayerId p = 0; p < kNumPlayers; ++p) r.players[p].hand_end = static_cast<int>(hand[p].size());
    r.delirium.at_end = delirium;
    return r;
}

std::string compact_record_json(const CardDatabase& db, const CompactRecord& r) {
    using json = nlohmann::ordered_json;
    auto names = [&](const std::vector<CardDefId>& defs) {
        json a = json::array();
        for (const CardDefId d : defs) a.push_back(db.get(d).name);
        return a;
    };
    auto player = [&](const PlayerRecord& p) {
        json j;
        j["mulligan"] = p.mulligans;
        json hands = json::array();
        for (const auto& h : p.hands) hands.push_back(names(h));
        j["mani"] = hands;
        j["fondo"] = names(p.bottom);
        j["terre"] = p.lands;
        j["mano_fine"] = p.hand_end;
        json cards = json::array();
        for (const CopyRecord& c : p.cards) {
            json events = json::array();
            for (const CopyEvent& e : c.events) events.push_back(json::array({e.turn, std::string(1, e.code)}));
            json copy;
            copy["c"] = db.get(c.def).name;
            copy["e"] = events;
            cards.push_back(copy);
        }
        j["carte"] = cards;
        return j;
    };
    const MatchupGame& g = r.game;
    json j;
    j["seed"] = g.seed;
    j["uw_in_play"] = g.uw_on_play;
    j["vincitore"] = g.winner == kMatchupUw ? "UW" : g.winner == kMatchupRakdos ? "Rakdos" : "nessuno";
    j["fine"] = g.ending;
    j["turni"] = g.turns;
    j["vita_uw"] = g.uw_life;
    j["vita_rakdos"] = g.rakdos_life;
    if (r.delirium.reached) {
        json d;
        d["turno"] = r.delirium.turn;
        d["turno_rakdos"] = r.delirium.rakdos_turn;
        d["perso"] = r.delirium.lost;
        d["fine"] = r.delirium.at_end;
        j["delirium"] = d;
    } else {
        j["delirium"] = nullptr;
    }
    j["uw"] = player(r.players[kMatchupUw]);
    j["rakdos"] = player(r.players[kMatchupRakdos]);
    return j.dump();
}

std::vector<CardDefId> hand_at_end(const PlayerRecord& player) {
    std::vector<CardDefId> out;
    for (const CopyRecord& c : player.cards) {
        if (!c.events.empty() && arrives_in_hand(c.events.back().code)) out.push_back(c.def);
    }
    return out;
}

std::vector<std::string> check_compact_record(const CardDatabase& db, const CompactRecord& r, const Decklist& uw,
                                              const Decklist& rakdos) {
    std::vector<std::string> problems;
    const std::array<const Decklist*, kNumPlayers> decks = {&uw, &rakdos};
    const std::array<const char*, kNumPlayers> who = {"UW", "Rakdos"};
    const PlayerId starting = r.game.uw_on_play ? kMatchupUw : kMatchupRakdos;
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        const PlayerRecord& pr = r.players[p];
        const std::string tag = std::string(who[p]) + ", seed " + std::to_string(r.game.seed) + ": ";
        std::map<std::string, int> copies;
        int opening = 0, arrivals = 0, departures = 0;
        for (const CopyRecord& c : pr.cards) {
            ++copies[db.get(c.def).name];
            for (const CopyEvent& e : c.events) {
                if (e.code == '?') problems.push_back(tag + "spostamento non previsto di " + db.get(c.def).name);
                if (e.code == 'I') ++opening;
                if (arrives_in_hand(e.code)) ++arrivals;
                if (leaves_hand(e.code)) ++departures;
            }
        }
        for (const auto& [name, n] : copies) {
            int in_list = 0;
            for (const DeckEntry& entry : decks[p]->main) {
                if (entry.card == name) in_list = entry.count;
            }
            if (n > in_list) {
                problems.push_back(tag + name + ": " + std::to_string(n) + " copie viste, " + std::to_string(in_list) +
                                   " nella lista");
            }
        }
        const int mulligans = p == kMatchupUw ? r.game.uw_mulligans : r.game.rakdos_mulligans;
        if (pr.mulligans != mulligans) problems.push_back(tag + "mulligan diversi dall'esito della partita");
        if (static_cast<int>(pr.hands.size()) != pr.mulligans + 1) problems.push_back(tag + "mani diverse da mulligan + 1");
        for (const auto& h : pr.hands) {
            if (h.size() != kOpeningHandSize) problems.push_back(tag + "una mano del mulligan non e' da 7");
        }
        if (opening != static_cast<int>(kOpeningHandSize) - pr.mulligans) {
            problems.push_back(tag + "mano iniziale di " + std::to_string(opening) + " carte con " +
                               std::to_string(pr.mulligans) + " mulligan");
        }
        if (static_cast<int>(pr.bottom.size()) != pr.mulligans) problems.push_back(tag + "carte in fondo diverse dai mulligan");
        if (arrivals - departures != pr.hand_end) {
            problems.push_back(tag + "arrivi in mano meno uscite (" + std::to_string(arrivals - departures) +
                               ") diverso da mano_fine (" + std::to_string(pr.hand_end) + ")");
        }
        if (static_cast<int>(hand_at_end(pr).size()) != pr.hand_end) {
            problems.push_back(tag + "carte in mano dagli eventi diverse da mano_fine");
        }
        const int own_turns = p == starting ? (r.game.turns + 1) / 2 : r.game.turns / 2;
        if (static_cast<int>(pr.lands.size()) != own_turns) {
            problems.push_back(tag + std::to_string(pr.lands.size()) + " voci di terre per " + std::to_string(own_turns) +
                               " turni");
        }
    }
    return problems;
}

}  // namespace mtg
