#include "deck_loader.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <map>
#include <sstream>

namespace mtg {

namespace {

using nlohmann::json;

// Legge un campo stringa obbligatorio; `where` serve solo al messaggio d'errore.
std::string require_string(const json& obj, const char* key, const std::string& where) {
    if (!obj.contains(key) || !obj[key].is_string()) {
        throw DeckError(where + ": manca il campo stringa \"" + key + "\"");
    }
    return obj[key].get<std::string>();
}

DeckEntry parse_entry(const json& item, std::size_t index) {
    const std::string where = "main[" + std::to_string(index) + "]";
    if (!item.is_object()) {
        throw DeckError(where + ": ogni voce deve essere un oggetto { card, count }");
    }

    DeckEntry entry;
    entry.card = require_string(item, "card", where);
    if (entry.card.empty()) {
        throw DeckError(where + ": il nome della carta è vuoto");
    }

    if (!item.contains("count") || !item["count"].is_number_integer()) {
        throw DeckError(where + " (" + entry.card + "): \"count\" deve essere un intero");
    }
    const auto count = item["count"].get<long long>();
    if (count < 1 || count > kMainDeckSize) {
        throw DeckError(where + " (" + entry.card + "): numero di copie fuori intervallo: " +
                        std::to_string(count));
    }
    entry.count = static_cast<int>(count);
    return entry;
}

}  // namespace

int Decklist::main_size() const {
    int total = 0;
    for (const auto& entry : main) total += entry.count;
    return total;
}

bool is_basic_land(std::string_view card_name) {
    static constexpr std::array<std::string_view, 11> kBasics{
        "Plains",
        "Island",
        "Swamp",
        "Mountain",
        "Forest",
        "Wastes",
        "Snow-Covered Plains",
        "Snow-Covered Island",
        "Snow-Covered Swamp",
        "Snow-Covered Mountain",
        "Snow-Covered Forest",
    };
    for (const auto basic : kBasics) {
        if (basic == card_name) return true;
    }
    return false;
}

Decklist parse_decklist(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        throw DeckError(std::string("JSON non valido: ") + e.what());
    }
    if (!root.is_object()) {
        throw DeckError("la lista deve essere un oggetto JSON");
    }

    Decklist deck;
    deck.name = require_string(root, "name", "lista");
    deck.format = require_string(root, "format", "lista");
    deck.source = root.value("source", "");

    if (!root.contains("main") || !root["main"].is_array()) {
        throw DeckError("lista \"" + deck.name + "\": manca l'array \"main\"");
    }
    const json& main = root["main"];
    deck.main.reserve(main.size());
    for (std::size_t i = 0; i < main.size(); ++i) {
        deck.main.push_back(parse_entry(main[i], i));
    }
    return deck;
}

Decklist load_decklist(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw DeckError("impossibile aprire " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    try {
        return parse_decklist(buffer.str());
    } catch (const DeckError& e) {
        throw DeckError(path.filename().string() + ": " + e.what());
    }
}

std::vector<std::string> validate_decklist(const Decklist& deck) {
    std::vector<std::string> errors;

    const int total = deck.main_size();
    if (total != kMainDeckSize) {
        errors.push_back("il main ha " + std::to_string(total) + " carte invece di " +
                         std::to_string(kMainDeckSize));
    }

    // Somma le copie per nome: la stessa carta può comparire su più righe.
    // std::map dà un ordine stabile ai messaggi d'errore.
    std::map<std::string, int, std::less<>> copies;
    for (const auto& entry : deck.main) copies[entry.card] += entry.count;

    for (const auto& [card, count] : copies) {
        if (count > kMaxCopies && !is_basic_land(card)) {
            errors.push_back(card + ": " + std::to_string(count) + " copie (massimo " +
                             std::to_string(kMaxCopies) + ")");
        }
    }
    return errors;
}

}  // namespace mtg
