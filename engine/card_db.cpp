#include "card_db.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <fstream>
#include <sstream>

namespace mtg {

namespace {

using nlohmann::json;

int color_index(char symbol) {
    switch (symbol) {
        case 'W': return static_cast<int>(Color::White);
        case 'U': return static_cast<int>(Color::Blue);
        case 'B': return static_cast<int>(Color::Black);
        case 'R': return static_cast<int>(Color::Red);
        case 'G': return static_cast<int>(Color::Green);
        default: return -1;
    }
}

std::uint8_t to_u8(int value, std::string_view what) {
    if (value < 0 || value > 255) throw CardDataError(std::string(what) + ": valore fuori intervallo");
    return static_cast<std::uint8_t>(value);
}

// "7" -> (7, false), "*" -> (0, true), "1+*" -> (1, true).
std::pair<std::int8_t, bool> parse_pt(const std::string& text, const std::string& card) {
    bool star = false;
    std::string digits = text;
    if (text == "*") {
        return {std::int8_t{0}, true};
    }
    if (text.size() > 2 && text.ends_with("+*")) {
        star = true;
        digits = text.substr(0, text.size() - 2);
    }
    int value = 0;
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || ptr != digits.data() + digits.size() || value < -128 || value > 127) {
        throw CardDataError(card + ": forza/costituzione non gestita: \"" + text + "\"");
    }
    return {static_cast<std::int8_t>(value), star};
}

SupertypeMask parse_supertypes(const json& list, const std::string& card) {
    SupertypeMask mask = 0;
    for (const auto& item : list) {
        const auto s = item.get<std::string>();
        if (s == "Basic") mask |= supertype_bit(Supertype::Basic);
        else if (s == "Legendary") mask |= supertype_bit(Supertype::Legendary);
        else if (s == "Snow") mask |= supertype_bit(Supertype::Snow);
        else if (s == "World") mask |= supertype_bit(Supertype::World);
        else if (s == "Ongoing") mask |= supertype_bit(Supertype::Ongoing);
        else throw CardDataError(card + ": supertipo sconosciuto " + s);
    }
    return mask;
}

TypeMask parse_types(const json& list, const std::string& card) {
    static constexpr std::pair<std::string_view, CardType> kNames[] = {
        {"Artifact", CardType::Artifact},       {"Battle", CardType::Battle},
        {"Creature", CardType::Creature},       {"Enchantment", CardType::Enchantment},
        {"Instant", CardType::Instant},         {"Kindred", CardType::Kindred},
        {"Land", CardType::Land},               {"Planeswalker", CardType::Planeswalker},
        {"Sorcery", CardType::Sorcery},
    };
    TypeMask mask = 0;
    for (const auto& item : list) {
        const auto t = item.get<std::string>();
        bool found = false;
        for (const auto& [name, type] : kNames) {
            if (name == t) {
                mask = static_cast<TypeMask>(mask | type_bit(type));
                found = true;
            }
        }
        if (!found) throw CardDataError(card + ": tipo sconosciuto " + t);
    }
    return mask;
}

ColorMask parse_colors(const json& list, const std::string& card) {
    ColorMask mask = 0;
    for (const auto& item : list) {
        const auto c = item.get<std::string>();
        const int idx = c.size() == 1 ? color_index(c[0]) : -1;
        if (idx < 0) throw CardDataError(card + ": colore sconosciuto " + c);
        mask = static_cast<ColorMask>(mask | (1u << static_cast<unsigned>(idx)));
    }
    return mask;
}

LandTypeMask land_types_of(const std::vector<std::string>& subtypes) {
    static constexpr std::pair<std::string_view, LandType> kBasics[] = {
        {"Plains", LandType::Plains},     {"Island", LandType::Island}, {"Swamp", LandType::Swamp},
        {"Mountain", LandType::Mountain}, {"Forest", LandType::Forest},
    };
    LandTypeMask mask = 0;
    for (const auto& sub : subtypes) {
        for (const auto& [name, type] : kBasics) {
            if (sub == name) mask = static_cast<LandTypeMask>(mask | land_type_bit(type));
        }
    }
    return mask;
}

// Una faccia di cards.json (o una carta a faccia singola).
CardDef parse_face(const json& j) {
    CardDef d;
    d.name = j.at("name").get<std::string>();
    d.mana_cost_text = j.at("mana_cost").get<std::string>();
    d.cost = parse_mana_cost(d.mana_cost_text);
    d.mana_value = to_u8(j.at("mana_value").get<int>(), d.name);
    d.supertypes = parse_supertypes(j.at("supertypes"), d.name);
    d.types = parse_types(j.at("types"), d.name);
    d.subtypes = j.at("subtypes").get<std::vector<std::string>>();
    d.colors = parse_colors(j.at("colors"), d.name);
    d.land_types = land_types_of(d.subtypes);

    if (!j.at("power").is_null()) {
        d.has_pt = true;
        std::tie(d.base_power, d.power_star) = parse_pt(j.at("power").get<std::string>(), d.name);
        std::tie(d.base_toughness, d.toughness_star) = parse_pt(j.at("toughness").get<std::string>(), d.name);
    }
    if (!j.at("loyalty").is_null()) {
        d.loyalty = parse_pt(j.at("loyalty").get<std::string>(), d.name).first;
    }
    return d;
}

}  // namespace

ManaCost parse_mana_cost(std::string_view text) {
    ManaCost cost;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t close = text.find('}', i);
        if (text[i] != '{' || close == std::string_view::npos) {
            throw CardDataError("costo di mana malformato: \"" + std::string(text) + "\"");
        }
        const std::string_view symbol = text.substr(i + 1, close - i - 1);
        int number = 0;
        const auto [ptr, ec] = std::from_chars(symbol.data(), symbol.data() + symbol.size(), number);
        if (ec == std::errc{} && ptr == symbol.data() + symbol.size()) {
            cost.generic = static_cast<std::uint8_t>(cost.generic + to_u8(number, "costo generico"));
        } else if (symbol == "X") {
            ++cost.x;
        } else if (symbol == "C") {
            ++cost.colorless;
        } else if (symbol.size() == 1 && color_index(symbol[0]) >= 0) {
            ++cost.colored[static_cast<std::size_t>(color_index(symbol[0]))];
        } else {
            throw CardDataError("simbolo di mana non gestito {" + std::string(symbol) + "} in \"" +
                                std::string(text) + "\"");
        }
        i = close + 1;
    }
    return cost;
}

CardDatabase CardDatabase::from_json(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        throw CardDataError(std::string("cards.json non valido: ") + e.what());
    }

    CardDatabase db;
    try {
        for (const auto& j : root.at("cards")) {
            CardDef front = parse_face(j);
            front.full_name = j.at("full_name").get<std::string>();
            front.layout = j.at("layout").get<std::string>();
            if (!j.at("back_face").is_null()) {
                CardDef back = parse_face(j.at("back_face"));
                back.full_name = front.full_name;
                back.layout = front.layout;
                front.back_face = db.add_unindexed(std::move(back));  // il retro non è un nome di lista
            }
            db.add(std::move(front));
        }
    } catch (const json::exception& e) {
        throw CardDataError(std::string("cards.json: campo mancante o di tipo errato: ") + e.what());
    }
    return db;
}

CardDatabase CardDatabase::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw CardDataError("impossibile aprire " + path.string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return from_json(buffer.str());
}

const CardDef* CardDatabase::find(std::string_view name) const {
    const auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &defs_[it->second];
}

CardDefId CardDatabase::id_of(std::string_view name) const {
    const CardDef* def = find(name);
    if (def == nullptr) throw CardDataError("carta sconosciuta: \"" + std::string(name) + "\"");
    return def->id;
}

CardDefId CardDatabase::add(CardDef def) {
    if (by_name_.contains(def.name)) throw CardDataError("carta duplicata: \"" + def.name + "\"");
    const std::string name = def.name;
    const CardDefId id = add_unindexed(std::move(def));
    by_name_.emplace(name, id);
    return id;
}

CardDefId CardDatabase::add_unindexed(CardDef def) {
    if (defs_.size() >= kNoDef) throw CardDataError("troppe definizioni di carta");
    def.id = static_cast<CardDefId>(defs_.size());
    defs_.push_back(std::move(def));
    return defs_.back().id;
}

void CardDatabase::set_rules(std::string_view name, const CardRules& rules) {
    defs_[id_of(name)].rules = rules;
}

void CardDatabase::set_back_face_rules(std::string_view front_name, const CardRules& rules) {
    const CardDef& front = get(id_of(front_name));
    if (front.back_face == kNoDef) throw CardDataError(std::string(front_name) + ": non ha un retro");
    defs_[front.back_face].rules = rules;
}

ColorMask mana_colors(const CardDef& def) {
    // Ordine di LandType e di Color coincidono per W U B R G.
    static_assert(static_cast<int>(LandType::Plains) == static_cast<int>(Color::White));
    static_assert(static_cast<int>(LandType::Forest) == static_cast<int>(Color::Green));
    return static_cast<ColorMask>(def.land_types | def.rules.land.mana);
}

}  // namespace mtg
