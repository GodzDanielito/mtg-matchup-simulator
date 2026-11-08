#pragma once

// Caricatore delle liste dei mazzi da config/decks/*.json.
//
// Formato atteso:
//   { "name": "...", "format": "Modern", "source": "...",
//     "main": [ { "card": "Lightning Bolt", "count": 4 }, ... ] }
//
// La lettura (parse) e la verifica di legalità (validate) sono separate:
// parse_decklist rifiuta solo un JSON malformato, validate_decklist elenca
// tutte le violazioni delle regole di costruzione, così un errore nella lista
// le mostra tutte insieme invece di una alla volta.

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

inline constexpr int kMainDeckSize = 60;
inline constexpr int kMaxCopies = 4;

struct DeckEntry {
    std::string card;  // nome inglese Oracle (per le bifronti, il fronte)
    int count = 0;
};

struct Decklist {
    std::string name;
    std::string format;
    std::string source;
    std::vector<DeckEntry> main;

    // Numero totale di carte nel main.
    [[nodiscard]] int main_size() const;
};

// JSON malformato, campi mancanti o di tipo sbagliato, file illeggibile.
class DeckError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Nomi esatti delle terre base (comprese le versioni Snow-Covered e Wastes).
[[nodiscard]] bool is_basic_land(std::string_view card_name);

// Legge una lista da testo JSON. Lancia DeckError se il formato non è valido.
[[nodiscard]] Decklist parse_decklist(std::string_view json_text);

// Legge una lista da file. Lancia DeckError se il file manca o non è valido.
[[nodiscard]] Decklist load_decklist(const std::filesystem::path& path);

// Restituisce un messaggio per ogni violazione delle regole di costruzione
// (60 carte esatte, massimo 4 copie per nome tranne le terre base).
// Lista vuota = mazzo legale.
[[nodiscard]] std::vector<std::string> validate_decklist(const Decklist& deck);

}  // namespace mtg
