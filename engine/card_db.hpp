#pragma once

// Definizioni delle carte (CardDef): immutabili, condivise, fuori da GameState.
// Si caricano da config/cards.json, generato da python/build_card_data.py.

#include "card_rules.hpp"
#include "types.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

// cards.json mancante o malformato, nome di carta sconosciuto, simbolo di mana non gestito.
class CardDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


// Analizza un costo in formato Scryfall ("{2}{B}{B}"). Accetta solo i simboli
// presenti nel pool (numeri, W U B R G C, X): gli altri sono un errore esplicito.
[[nodiscard]] ManaCost parse_mana_cost(std::string_view text);

struct CardDef {
    CardDefId id = kNoDef;
    std::string name;       // nome usato nelle liste (per le bifronti, il fronte)
    std::string full_name;  // nome Scryfall completo ("Fronte // Retro")
    std::string layout;     // normal, modal_dfc, transform, token...
    std::string mana_cost_text;
    ManaCost cost;
    std::uint8_t mana_value = 0;
    SupertypeMask supertypes = 0;
    TypeMask types = 0;
    std::vector<std::string> subtypes;
    ColorMask colors = 0;

    bool has_pt = false;
    std::int8_t base_power = 0;      // parte numerica ("1+*" -> 1, "*" -> 0)
    std::int8_t base_toughness = 0;
    bool power_star = false;         // definita da un'abilità (Nethergoyf)
    bool toughness_star = false;
    std::int8_t loyalty = -1;        // -1 = nessuna

    CardDefId back_face = kNoDef;    // retro delle bifronti
    bool is_token = false;
    LandTypeMask land_types = 0;     // tipi di terra base tra i sottotipi
    CardRules rules;                 // regole implementate in cards/ (vuote = non implementata)
    std::vector<std::string> scryfall_keywords;  // solo per il controllo incrociato nei test

    [[nodiscard]] bool has_type(CardType t) const { return (types & type_bit(t)) != 0; }
    [[nodiscard]] bool has_supertype(Supertype s) const { return (supertypes & supertype_bit(s)) != 0; }
    // Carta permanente: artefatto, battaglia, creatura, incantesimo, terra o planeswalker.
    [[nodiscard]] bool is_permanent_card() const { return (types & kPermanentTypes) != 0; }
};

// Colori che la carta può produrre in campo: quelli dei tipi di terra base
// (abilità intrinseca, regola 305.6) più le abilità di mana scritte.
[[nodiscard]] ColorMask mana_colors(const CardDef& def);

class CardDatabase {
public:
    [[nodiscard]] static CardDatabase from_json(std::string_view json_text);
    [[nodiscard]] static CardDatabase load(const std::filesystem::path& path);

    [[nodiscard]] const CardDef& get(CardDefId id) const { return defs_.at(id); }

    // Cerca per nome di lista (i retri delle bifronti non sono indicizzati). nullptr se assente.
    [[nodiscard]] const CardDef* find(std::string_view name) const;

    // Come find, ma lancia CardDataError se il nome non esiste.
    [[nodiscard]] CardDefId id_of(std::string_view name) const;

    // Aggiunge una definizione (token, carte di test). Assegna l'id. Nome duplicato = errore.
    CardDefId add(CardDef def);

    // Aggancia le regole implementate a una carta (o al retro di una bifronte).
    // Lancia CardDataError se la carta non esiste o non ha un retro.
    void set_rules(std::string_view name, const CardRules& rules);
    void set_back_face_rules(std::string_view front_name, const CardRules& rules);

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

private:
    CardDefId add_unindexed(CardDef def);

    std::vector<CardDef> defs_;
    std::map<std::string, CardDefId, std::less<>> by_name_;
};

}  // namespace mtg
