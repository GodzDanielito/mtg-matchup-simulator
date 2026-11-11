#include "replay_leggibile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mtg {

namespace {

using json = nlohmann::json;

std::string join(const std::vector<std::string>& v, const std::string& sep = ", ") {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i == 0 ? "" : sep) + v[i];
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

// L'indice dopo "opzione ": il valore di una ChooseOption (tools/replay.cpp: option_name).
int option_value(const std::string& action) {
    return starts_with(action, "opzione ") ? std::stoi(action.substr(8)) : -1;
}

class Narrator {
public:
    explicit Narrator(std::vector<json> events) : events_(std::move(events)) {}

    ReadableGame run() {
        ReadableGame g;
        const json& head = events_.front();
        g.seed = head.value("seed", std::uint64_t{0});
        for (const json& p : head["giocatori"]) g.players.push_back(p.get<std::string>());
        players_ = g.players;
        std::string result;
        for (const json& e : events_) {
            const std::string kind = e.value("evento", "");
            if (kind == "turno" && g.first_player.empty()) g.first_player = e.value("attivo", "");
            if (kind == "fine") {
                g.winner = e["vincitore"].is_null() ? "" : e["vincitore"].get<std::string>();
                result = e.value("esito", "");
            }
        }
        first_player_ = g.first_player;

        out_ << "Partita " << g.seed << " - " << label(0) << " contro " << label(1) << "\n";
        out_ << "Esito: " << result << "\n";
        out_ << "(Omesso il dettaglio tecnico: priorita', pagamenti del mana e cambi di zona.)\n";
        for (const json& e : events_) handle(e);
        finish_turn();
        flush();
        out_ << "\nFine della partita. Vincitore: " << (g.winner.empty() ? "nessuno" : g.winner) << ".\n";
        g.text = out_.str();
        return g;
    }

private:
    // --- Nomi --------------------------------------------------------------------------------

    static std::string short_name(const std::string& full) { return full.substr(0, full.find(' ')); }
    int index(const std::string& full) const { return full == players_[0] ? 0 : 1; }
    std::string who(const std::string& full) const { return short_name(full); }
    std::string label(int p) const {
        std::string out = players_[static_cast<std::size_t>(p)];
        if (!first_player_.empty()) out += players_[static_cast<std::size_t>(p)] == first_player_ ? " (in play)" : " (in draw)";
        return out;
    }

    // --- Uscita ------------------------------------------------------------------------------

    void line(int level, const std::string& text, const std::string& rule = "") {
        out_ << std::string(static_cast<std::size_t>(level) * 2, ' ') << text;
        if (!rule.empty()) out_ << "   [regola: " << rule << "]";
        out_ << '\n';
    }

    static std::string phase_label(const std::string& fase, int combats) {
        if (fase == "prima fase principale") return "Prima fase principale";
        if (fase == "seconda fase principale") return "Seconda fase principale";
        if (fase == "inizio combattimento" || fase == "attaccanti" || fase == "bloccanti" ||
            fase == "danno da attacco improvviso" || fase == "danno" || fase == "fine combattimento") {
            return combats > 1 ? "Combattimento aggiuntivo" : "Combattimento";
        }
        if (fase == "sottofase finale" || fase == "cancellazione") return "Fine del turno";
        return "Inizio del turno";
    }
    void phase(const std::string& fase) {
        if (fase.empty() || fase == "pre-partita") return;
        const std::string label = phase_label(fase, combats_);
        if (label == phase_) return;
        phase_ = label;
        line(1, "- " + label);
    }

    // --- Eventi aggregati: macinate ed esili dal cimitero ----------------------------------

    enum class Pending { None, Mill, GraveyardExile };

    void flush() {
        if (pending_ == Pending::None) return;
        const std::string name = who(players_[static_cast<std::size_t>(pending_owner_)]);
        const std::string n = std::to_string(pending_cards_.size());
        if (pending_ == Pending::Mill) line(2, name + " macina " + n + ": " + join(pending_cards_));
        if (pending_ == Pending::GraveyardExile) {
            line(2, "Carte esiliate dal cimitero di " + name + " (" + n + "): " + join(pending_cards_));
        }
        pending_ = Pending::None;
        pending_cards_.clear();
    }
    void add_pending(Pending kind, int owner, const std::string& card) {
        if (pending_ != kind || pending_owner_ != owner) flush();
        pending_ = kind;
        pending_owner_ = owner;
        pending_cards_.push_back(card);
    }

    // --- Turni -------------------------------------------------------------------------------

    void finish_turn() {
        flush();
        if (turn_ == 0) return;
        std::string graveyards = "Cimiteri: ";
        for (int p = 0; p < 2; ++p) {
            const Graveyard& g = graveyard_[static_cast<std::size_t>(p)];
            graveyards += (p == 0 ? "" : "; ") + who(players_[static_cast<std::size_t>(p)]) + " ";
            graveyards += g.cards == 0 ? "vuoto"
                                       : std::to_string(g.cards) + (g.cards == 1 ? " carta (" : " carte (") + join(g.types) + ")";
        }
        line(1, "Fine del turno " + std::to_string(turn_) + ": vita " + who(players_[0]) + " " + std::to_string(life_[0]) +
                    ", " + who(players_[1]) + " " + std::to_string(life_[1]) + ". " + graveyards);
        turn_ = 0;
    }

    void start_turn(const json& e) {
        finish_turn();
        turn_ = e.value("turno", 0);
        combats_ = 0;
        phase_.clear();
        const json& life = e["vita"];
        for (int p = 0; p < 2; ++p) life_[static_cast<std::size_t>(p)] = life.value(players_[static_cast<std::size_t>(p)], 20);
        const std::string active = e.value("attivo", "");
        out_ << "\n=== Turno " << turn_ << " - " << who(active) << (active == first_player_ ? " (in play)" : " (in draw)")
             << " - vita " << who(players_[0]) << " " << life_[0] << ", " << who(players_[1]) << " " << life_[1] << " ===\n";
    }

    // --- Dispatch ----------------------------------------------------------------------------

    void handle(const json& e) {
        const std::string kind = e.value("evento", "");
        if (kind == "partita" || kind == "fine") return;
        if (kind == "spostamento") {
            move(e);
            return;
        }
        flush();
        if (kind == "turno") start_turn(e);
        else if (kind == "mano") hand(e);
        else if (kind == "pescata") {
            phase(e.value("fase", ""));
            line(2, who(e.value("giocatore", "")) + " pesca " + e.value("carta", "?"));
        } else if (kind == "combattimento") {
            ++combats_;
            if (combats_ > 1) phase(e.value("fase", ""));
        } else if (kind == "cimitero") graveyard_event(e);
        else if (kind == "vita") life_event(e);
        else if (kind == "decisione") decision(e);
    }

    void hand(const json& e) {
        if (!pregame_printed_) {
            out_ << "\nMani iniziali e mulligan\n";
            pregame_printed_ = true;
        }
        std::vector<std::string> cards;
        for (const json& c : e["carte"]) cards.push_back(c.get<std::string>());
        const int mulligans = e.value("mulligan_fatti", 0);
        std::string text = who(e.value("giocatore", "")) + " mano (" + std::to_string(cards.size()) + " carte";
        if (mulligans > 0) text += ", dopo " + std::to_string(mulligans) + " mulligan";
        line(1, text + "): " + join(cards));
    }

    void graveyard_event(const json& e) {
        const int p = index(e.value("giocatore", ""));
        Graveyard& g = graveyard_[static_cast<std::size_t>(p)];
        g.cards = e.value("carte", 0);
        g.types.clear();
        for (const json& t : e["tipi"]) g.types.push_back(t.get<std::string>());
        const bool delirium = e.value("delirium", false);
        const std::string name = who(players_[static_cast<std::size_t>(p)]);
        if (delirium && !delirium_[static_cast<std::size_t>(p)]) {
            line(2, "Delirium per " + name + ": il cimitero ha " + std::to_string(g.types.size()) + " tipi (" + join(g.types) + ")");
        } else if (!delirium && delirium_[static_cast<std::size_t>(p)]) {
            line(2, name + " perde il delirium");
        }
        delirium_[static_cast<std::size_t>(p)] = delirium;
    }

    void life_event(const json& e) {
        phase(e.value("fase", ""));
        const std::string player = e.value("giocatore", "");
        const int change = e.value("variazione", 0);
        const int now = e.value("vita", 0);
        life_[static_cast<std::size_t>(index(player))] = now;
        const std::string source = e.value("fonte", "");
        const std::string cause = e.value("causa", "");
        const std::string name = who(player);
        const std::string amount = std::to_string(change < 0 ? -change : change);
        const std::string tail = ", vita " + std::to_string(now);
        if (cause == "danno" && change < 0) {
            line(2, name + " subisce " + amount + (amount == "1" ? " danno da " : " danni da ") + source + tail);
            return;
        }
        else if (cause == "pagamento") line(2, name + " paga " + amount + " vita (" + source + ")" + tail);
        else if (change > 0) line(2, name + " guadagna " + amount + " vita (" + source + ")" + tail);
        else line(2, name + " perde " + amount + " vita (" + source + ")" + tail);
    }

    // I cambi di zona che fanno notizia; gli altri sono rumore tecnico.
    void move(const json& e) {
        const std::string from = e.value("da", ""), to = e.value("a", ""), cause = e.value("causa", "");
        const std::string card = e.value("carta", "?");
        const int owner = index(e.value("giocatore", ""));
        if (cause == "cercata" && search_.pending) {
            flush();
            phase(e.value("fase", ""));
            line(2, search_.who + " cerca " + card, search_.rule);
            search_ = Search{};
            return;
        }
        if (from == "grimorio" && to == "cimitero") return add_pending(Pending::Mill, owner, card);
        if (from == "grimorio" && to == "esilio") return add_pending(Pending::Mill, owner, card + " (esiliata)");  // Sanctifier
        if (from == "cimitero" && to == "esilio") return add_pending(Pending::GraveyardExile, owner, card);
        flush();
        phase(e.value("fase", ""));
        if (from == "campo" && to == "cimitero") {
            if (cause == "costo") {
                // Una fetch rotta o un Bauble sacrificato sono gia' nella decisione di attivarli.
                if (card == last_activated_) return;
                return line(2, card + " viene sacrificata come costo");
            }
            if (cause == "sacrificata") return line(2, card + " viene sacrificata");
            if (cause == "distrutta") return line(2, card + " viene distrutta");
            return line(2, card + " muore");
        }
        if (from == "campo" && to == "esilio") return line(2, card + " viene esiliata");
        if (from == "campo" && to == "mano") return line(2, card + " torna in mano");
        if (from == "esilio" && to == "campo") return line(2, card + " torna in campo");
        if (from == "cimitero" && to == "campo") return line(2, card + " torna in campo dal cimitero");
        if (from == "nessuna" && to == "campo") return line(2, "nasce un " + card);
        if (from == "pila" && to == "cimitero" && cause == "neutralizzata") return line(2, card + " viene neutralizzata");
        if (from == "pila" && to == "esilio") return line(2, card + " va in esilio");
    }

    // --- Decisioni -----------------------------------------------------------------------------

    std::string target_text(const json& e, const std::string& source) const {
        if (e.contains("bersaglio_in_pila")) {
            const json& t = e["bersaglio_in_pila"];
            if (t.value("tipo", "") == "magia") return "la magia " + t.value("carta", "?");
            return "l'abilita' di " + t.value("carta", "?") + " (" + t.value("effetto", "") + ")";
        }
        if (e.contains("carta")) return e["carta"].get<std::string>();
        if (e.contains("giocatore_bersaglio")) return "il giocatore " + who(e["giocatore_bersaglio"].get<std::string>());
        const int v = option_value(e.value("azione", ""));
        if (v == 0) return "nessun bersaglio";
        if (v == 254) return "tiene il bersaglio";
        if (starts_with(source, "Thraben Charm") && v >= 1 && v <= 3) {  // un bit per giocatore
            std::vector<std::string> names;
            for (int p = 0; p < 2; ++p) {
                if ((v & (1 << p)) != 0) names.push_back(who(players_[static_cast<std::size_t>(p)]));
            }
            return names.size() == 1 ? "il cimitero di " + names[0] : "i cimiteri di " + join(names, " e ");
        }
        return e.value("azione", "?");
    }

    void decision(const json& e) {
        const std::string type = e.value("tipo", "");
        if (type == "pagamento del mana") return;
        const std::string player = e.value("giocatore", "");
        const std::string name = who(player);
        std::string rule = e.value("regola", "");
        if (starts_with(rule, name + ": ")) rule = rule.substr(name.size() + 2);
        // Con Teferi avversario in campo l'agente di Rakdos aggiunge la modalita' alla regola (" [corsa]").
        for (const auto& [suffix, text] : {std::pair<std::string, std::string>{" [corsa]", " - modalita' corsa"},
                                           {" [partita lunga]", " - modalita' partita lunga"}}) {
            if (rule.size() > suffix.size() && rule.compare(rule.size() - suffix.size(), suffix.size(), suffix) == 0) {
                rule = rule.substr(0, rule.size() - suffix.size()) + text;
            }
        }
        const std::string action = e.value("azione", "");
        const std::string card = e.value("carta", "");
        const std::string subject = e.value("soggetto", "");
        const int option = option_value(action);
        const std::string fase = e.value("fase", "");
        if (type == "priorita'" && action == "passa") return;
        phase(fase);

        if (type == "mulligan") return line(1, name + (action == "tiene" ? " tiene la mano" : " fa mulligan"), rule);
        if (type == "carte in fondo") return line(1, name + " mette in fondo: " + card, rule);
        if (type == "priorita'") {
            if (action == "gioca la terra") return line(2, name + " gioca la terra " + card, rule);
            if (action == "lancia") {
                std::vector<std::string> extra;
                const std::string mode = e.value("modo", "normale");
                if (mode != "normale") extra.push_back(mode);
                const std::string from = e.value("da", "mano");
                if (from == "esilio") extra.push_back("dall'esilio");
                else if (from != "mano") extra.push_back("dal " + from);
                return line(2, name + " lancia " + card + (extra.empty() ? "" : " (" + join(extra) + ")"), rule);
            }
            if (action == "attiva") {
                last_activated_ = card;
                std::string text = name + " attiva " + card;
                if (card.find("Teferi") != std::string::npos) text += e.value("abilita", 0) == 0 ? ": +1" : ": -3";
                return line(2, text, rule);
            }
            if (action == "ciclo") return line(2, name + " cicla " + card, rule);
        }
        if (type == "vita o terra tappata") return line(2, subject + ": " + action, rule);
        if (type == "ricerca nel grimorio") {
            if (!card.empty()) return line(2, name + " cerca " + card, rule);
            if (action == "sceglie") {  // la carta e' nel grimorio, mai vista: il nome arriva dallo spostamento
                search_ = Search{true, name, rule};
                return;
            }
            return line(2, name + " non cerca nulla", rule);
        }
        if (type == "sorveglianza") {
            return line(2, name + " sorveglia: " + (action == "nel cimitero" ? "la carta va nel cimitero" : "la carta resta in cima"), rule);
        }
        if (type == "scarto a fine turno" || type == "scarto") return line(2, name + " scarta " + card, rule);
        if (type == "regola delle leggende") return line(2, name + " tiene " + card + " (regola delle leggende)", rule);
        if (type == "bersaglio") return line(3, "bersaglio di " + subject + ": " + target_text(e, subject), rule);
        if (type == "modo") {
            if (starts_with(subject, "Thraben Charm")) {
                const char* modes[] = {"danno alle creature", "distrugge un incantesimo", "esilia i cimiteri"};
                if (option >= 0 && option < 3) return line(3, subject + ": " + modes[option], rule);
            }
            return line(3, subject + ": " + action, rule);
        }
        if (type == "valore di X") return line(3, subject + ": X = " + std::to_string(option), rule);
        if (type == "carta dalla mano da esiliare come costo") {
            if (!card.empty()) return line(3, "esilia dalla mano come costo: " + card, rule);
            return line(3, "nessun'altra carta da esiliare dalla mano", rule);
        }
        if (type == "lancio dall'esilio") {
            return line(2, subject + ": " + (option == 1 ? "la rilancia dall'esilio senza pagare" : "non la rilancia dall'esilio"), rule);
        }
        if (type == "replicare") return line(3, subject + ": replica " + std::to_string(option) + " volte", rule);
        if (type == "costo facoltativo") return line(3, subject + (option == 1 ? ": paga il costo facoltativo" : ": non paga il costo facoltativo"), rule);
        if (type == "attacco") {
            if (action == "attacca") return line(2, subject + " attacca " + who(e.value("giocatore_bersaglio", "")), rule);
            if (action == "bersaglio") return line(2, subject + " attacca " + card, rule);
            return line(2, subject + " resta indietro", rule);
        }
        if (type == "blocco") {
            if (action == "fine dei bloccanti") {
                if (blocks_added_ == 0) line(2, "nessun blocco contro " + subject, rule);
                blocks_added_ = 0;
                return;
            }
            ++blocks_added_;
            return line(2, card + " blocca " + subject, rule);
        }
        if (type == "divisione del danno") return line(2, name + " divide il danno di " + subject, rule);
        if (type == "ordine dei trigger") return line(2, name + " sceglie l'ordine dei trigger", rule);
        if (type == "sacrificio come costo" || type == "sacrificio per effetto") return line(2, name + " sacrifica " + card, rule);
        if (type == "carta dalla mano rivelata") return line(2, name + " sceglie " + card + " dalla mano rivelata", rule);
        if (type == "esilio dal cimitero") return line(2, name + " esilia dal cimitero " + card, rule);
        line(2, name + ": " + type + " - " + action + (card.empty() ? "" : " " + card), rule);
    }

    // La decisione di una ricerca in attesa del nome della carta trovata.
    struct Search {
        bool pending = false;
        std::string who;
        std::string rule;
    };

    struct Graveyard {
        int cards = 0;
        std::vector<std::string> types;
    };

    std::vector<json> events_;
    std::vector<std::string> players_;
    std::string first_player_;
    std::ostringstream out_;
    std::array<int, 2> life_{20, 20};
    std::array<Graveyard, 2> graveyard_;
    std::array<bool, 2> delirium_{false, false};
    int turn_ = 0;
    int combats_ = 0;
    int blocks_added_ = 0;
    std::string phase_;
    Search search_;
    std::string last_activated_;  // l'ultima carta attivata: se poi va nel cimitero come costo, e' la fetch o il Bauble
    bool pregame_printed_ = false;
    Pending pending_ = Pending::None;
    int pending_owner_ = 0;
    std::vector<std::string> pending_cards_;
};

}  // namespace

std::vector<ReadableGame> render_readable_replays(std::istream& jsonl) {
    std::vector<ReadableGame> games;
    std::vector<json> events;
    auto close = [&] {
        if (!events.empty()) games.push_back(Narrator(std::move(events)).run());
        events.clear();
    };
    for (std::string text; std::getline(jsonl, text);) {
        if (text.empty()) continue;
        json e = json::parse(text, nullptr, false);
        if (e.is_discarded()) continue;
        if (e.value("evento", "") == "partita") close();
        if (!events.empty() || e.value("evento", "") == "partita") events.push_back(std::move(e));
    }
    close();
    return games;
}

std::string readable_file_name(const ReadableGame& game) {
    char seed[16];
    std::snprintf(seed, sizeof seed, "%05llu", static_cast<unsigned long long>(game.seed));
    const bool uw_on_play = !game.players.empty() && game.first_player == game.players[0];
    const std::string winner = game.winner.empty() ? "nessun_vincitore" : "vince_" + game.winner.substr(0, game.winner.find(' '));
    return std::string("partita_") + seed + "_" + winner + (uw_on_play ? "_uw_in_play" : "_uw_in_draw") + ".txt";
}

std::vector<std::filesystem::path> write_readable_replays(std::istream& jsonl, const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    std::vector<std::filesystem::path> files;
    for (const ReadableGame& game : render_readable_replays(jsonl)) {
        files.push_back(dir / readable_file_name(game));
        std::ofstream(files.back(), std::ios::binary) << game.text;
    }
    return files;
}

}  // namespace mtg
