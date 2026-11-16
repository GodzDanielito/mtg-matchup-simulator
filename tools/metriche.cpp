#include "metriche.hpp"

#include "goldfish.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

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
                if (entry.card == name) in_list += entry.count;  // anche piu' voci (--sostituisci)
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

// --- Ablazione ---------------------------------------------------------------------------------------------

Substitution parse_substitution(std::string_view spec) {
    const std::size_t colon = spec.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == spec.size()) {
        throw std::invalid_argument("sostituzione non valida (serve \"Carta:Terra\"): " + std::string(spec));
    }
    return Substitution{std::string(spec.substr(0, colon)), std::string(spec.substr(colon + 1))};
}

int replaced_copy_position(const Decklist& deck, std::string_view card) {
    int position = 0;
    for (const DeckEntry& e : deck.main) {
        if (e.card == card && e.count > 0) return position + e.count - 1;
        position += e.count;
    }
    return -1;
}

Decklist replace_one_copy(const Decklist& deck, std::string_view card, std::string_view land) {
    if (!is_basic_land(land)) throw DeckError("la carta che sostituisce non e' una terra base: " + std::string(land));
    Decklist out = deck;
    for (auto it = out.main.begin(); it != out.main.end(); ++it) {
        if (it->card != card || it->count == 0) continue;
        if (it->count == 1) {
            it->card = std::string(land);
        } else {
            --it->count;
            out.main.insert(std::next(it), DeckEntry{std::string(land), 1});
        }
        return out;
    }
    throw DeckError("carta non nella lista \"" + deck.name + "\": " + std::string(card));
}

AppliedSubstitution apply_substitution(MatchupConfig& config, const Substitution& substitution) {
    std::array<Decklist*, kNumPlayers> decks = {&config.uw, &config.rakdos};
    // Prima il nome esatto, poi un prefisso che individui una sola carta tra i due mazzi.
    std::vector<std::pair<PlayerId, std::string>> found;
    for (const bool exact : {true, false}) {
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            for (const DeckEntry& e : decks[p]->main) {
                const bool match = exact ? e.card == substitution.card : e.card.starts_with(substitution.card);
                const auto same = [&](const auto& f) { return f.first == p && f.second == e.card; };
                if (match && std::none_of(found.begin(), found.end(), same)) found.emplace_back(p, e.card);
            }
        }
        if (!found.empty()) break;
    }
    if (found.empty()) throw std::invalid_argument("carta non trovata nei due mazzi: " + substitution.card);
    if (found.size() > 1) {
        std::string names;
        for (const auto& f : found) names += (names.empty() ? "" : ", ") + f.second;
        throw std::invalid_argument("\"" + substitution.card + "\" indica piu' carte: " + names);
    }
    const auto& [player, card] = found.front();
    AppliedSubstitution applied{player, card, substitution.land, replaced_copy_position(*decks[player], card)};
    *decks[player] = replace_one_copy(*decks[player], card, substitution.land);
    return applied;
}

// --- Mulligan contestuale ----------------------------------------------------------------------------------

std::vector<MulliganProbe> uw_mulligan_probes(const UwAgentOptions& base) {
    MulliganProbe m1{"m1", base}, m1r{"m1r", base}, m3{"m3", base};
    m1.options.mulligan_more_answers = true;
    m1r.options.mulligan_recruiter_answer = true;
    m3.options.mulligan_recruiter_answer_on_play = true;
    return {m1, m1r, m3};
}

namespace {

// Le sonde: agenti di UW con le opzioni delle varianti che, a ogni decisione di mulligan di UW (al massimo due),
// dicono cosa sceglierebbero nello stesso stato. Non cambiano la partita: l'osservatore riceve lo stato in sola
// lettura.
class ProbeObserver final : public MatchupObserver {
public:
    ProbeObserver(const CardDatabase& db, std::uint64_t seed, const std::vector<MulliganProbe>& probes)
        : choices(probes.size()) {
        for (const MulliganProbe& p : probes) agents_.push_back(std::make_unique<UwAgent>(db, seed, p.options));
    }
    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action&) override {
        if (s.pending.kind != DecisionKind::Mulligan || s.pending.player != kMatchupUw || count_ >= 2) return;
        for (std::size_t i = 0; i < agents_.size(); ++i) {
            const Action a = agents_[i]->choose(s, legal);
            choices[i][static_cast<std::size_t>(count_)] =
                MulliganChoice{true, a.type == ActionType::Keep, std::string(agents_[i]->last_rule())};
        }
        ++count_;
    }
    void on_end(const GameState&) override {}

    std::vector<std::array<MulliganChoice, 2>> choices;

private:
    std::vector<std::unique_ptr<UwAgent>> agents_;
    int count_ = 0;
};

// La n-esima decisione di mulligan (1 = la prima) di `p` nel registro: indice della voce, o entries.size().
std::size_t mulligan_entry(const GameLog& log, PlayerId p, int n) {
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind == LogEntry::Kind::Action && e.player == p && e.decision == DecisionKind::Mulligan && --n == 0) {
            return i;
        }
    }
    return log.entries.size();
}

// La mano tenuta: l'ultima mano del mulligan senza le carte messe in fondo.
std::vector<CardDefId> kept_hand(const PlayerRecord& r) {
    std::vector<CardDefId> hand = r.hands.empty() ? std::vector<CardDefId>{} : r.hands.back();
    for (const CardDefId b : r.bottom) {
        const auto it = std::find(hand.begin(), hand.end(), b);
        if (it != hand.end()) hand.erase(it);
    }
    return hand;
}

nlohmann::ordered_json choice_json(const MulliganChoice& c) {
    if (!c.present) return nullptr;
    nlohmann::ordered_json j;
    j["tieni"] = c.keep;
    j["regola"] = c.rule;
    return j;
}

nlohmann::ordered_json names_json(const CardDatabase& db, const std::vector<CardDefId>& cards) {
    nlohmann::ordered_json j = nlohmann::ordered_json::array();
    for (const CardDefId d : cards) j.push_back(db.get(d).name);
    return j;
}

nlohmann::ordered_json outcome_json(const MatchupGame& g) {
    nlohmann::ordered_json j;
    j["vincitore"] = g.winner == kMatchupUw ? "UW" : g.winner == kMatchupRakdos ? "Rakdos" : "nessuno";
    j["fine"] = g.ending;
    j["turni"] = g.turns;
    j["mulligan_uw"] = g.uw_mulligans;
    j["mulligan_rakdos"] = g.rakdos_mulligans;
    return j;
}

}  // namespace

MulliganPair play_mulligan_pair(const MatchupConfig& config, std::uint64_t seed, PlayerId player, bool with_records) {
    const CardDatabase& db = *config.db;
    MulliganPair out;
    out.player = player;
    out.seed = seed;
    MatchupConfig keep = config, mull = config;
    keep.forced_mulligan = ForcedMulligan{player, 1, true};
    mull.forced_mulligan = ForcedMulligan{player, 1, false};
    const std::vector<MulliganProbe> probes =
        player == kMatchupUw ? uw_mulligan_probes(config.uw_agent) : std::vector<MulliganProbe>{};
    ProbeObserver observer(db, seed, probes);

    GameLog keep_log, mull_log;
    std::map<std::size_t, std::string> mull_rules;
    out.keep_game = play_matchup(keep, seed, &keep_log);
    out.mull_game = play_matchup(mull, seed, &mull_log, &mull_rules, nullptr, nullptr, &observer);
    out.agent7 = MulliganChoice{out.keep_game.forced_applied, out.keep_game.forced_agent_keep,
                                out.keep_game.forced_agent_rule};
    const CompactRecord keep_record = build_compact_record(db, out.keep_game, keep_log);
    out.first_hand = keep_record.players[player].hands.front();
    const std::size_t six = mulligan_entry(mull_log, player, 2);
    if (six < mull_log.entries.size()) {
        std::string rule = mull_rules[six];
        rule = rule.substr(rule.find(": ") + 2);
        out.agent6 = MulliganChoice{true, mull_log.entries[six].action.type == ActionType::Keep, rule};
    }
    if (with_records) {
        out.has_records = true;
        out.keep_record = keep_record;
        out.mull_record = build_compact_record(db, out.mull_game, mull_log);
    }

    for (std::size_t i = 0; i < probes.size(); ++i) {
        const auto& c = observer.choices[i];
        out.probes.emplace_back(probes[i].name, c);
        int changed_at = 0;
        if (c[0].present && c[0].keep != out.agent7.keep) {
            changed_at = 7;
        } else if (!out.agent7.keep && c[1].present && out.agent6.present && c[1].keep != out.agent6.keep) {
            changed_at = 6;
        }
        if (changed_at == 0) continue;
        MatchupConfig variant = config;
        variant.uw_agent = probes[i].options;
        GameLog log;
        MulliganVariantGame v;
        v.name = probes[i].name;
        v.changed_at = changed_at;
        v.game = play_matchup(variant, seed, &log);
        v.kept_hand = kept_hand(build_compact_record(db, v.game, log).players[kMatchupUw]);
        if (changed_at == 7) {
            const GameLog& same = c[0].keep ? keep_log : mull_log;
            const MatchupGame& same_game = c[0].keep ? out.keep_game : out.mull_game;
            v.equals_pair_game = log.entries == same.entries && matchup_game_json(v.game) == matchup_game_json(same_game);
        }
        out.variants.push_back(std::move(v));
    }
    return out;
}

std::string mulligan_pair_json(const CardDatabase& db, const MulliganPair& p) {
    nlohmann::ordered_json j;
    j["seed"] = p.seed;
    j["uw_in_play"] = p.keep_game.uw_on_play;
    j["giocatore"] = p.player == kMatchupUw ? "uw" : "rakdos";
    j["mano"] = names_json(db, p.first_hand);
    j["agente"]["7"] = choice_json(p.agent7);
    j["agente"]["6"] = choice_json(p.agent6);
    j["K"] = outcome_json(p.keep_game);
    j["M"] = outcome_json(p.mull_game);
    if (!p.probes.empty()) {
        for (const auto& [name, c] : p.probes) {
            j["sonde"][name]["7"] = choice_json(c[0]);
            j["sonde"][name]["6"] = choice_json(c[1]);
        }
    }
    nlohmann::ordered_json variants = nlohmann::ordered_json::array();
    for (const MulliganVariantGame& v : p.variants) {
        nlohmann::ordered_json x = outcome_json(v.game);
        x["nome"] = v.name;
        x["cambia_a"] = v.changed_at;
        x["mano_tenuta"] = names_json(db, v.kept_hand);
        x["uguale_a_K_o_M"] = v.equals_pair_game;
        variants.push_back(x);
    }
    j["varianti"] = variants;
    return j.dump();
}

}  // namespace mtg
