#include "mana.hpp"

#include "game.hpp"

#include <algorithm>
#include <array>

namespace mtg {

namespace {

// Indici dei requisiti: W U B R G come Color, poi l'incolore {C}.
constexpr std::size_t kColorless = kNumColors;
constexpr std::size_t kRequirementSlots = kNumColors + 1;
using Requirements = std::array<int, kRequirementSlots>;

// Una classe di fonti: terre stappate che producono lo stesso insieme di colori.
struct SourceClass {
    ColorMask colors = 0;
    std::vector<CardId> lands;  // in ordine di campo
};

std::vector<SourceClass> source_classes(const GameState& s, const CardDatabase& db, PlayerId p) {
    std::vector<SourceClass> classes;
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller != p || o.has_flag(ObjectFlag::Tapped)) continue;
        const ColorMask colors = mana_colors(effective_def(s, db, id));
        if (colors == 0) continue;  // non è una fonte di mana (fetch, creature, Bauble)
        auto it = std::find_if(classes.begin(), classes.end(), [&](const SourceClass& c) { return c.colors == colors; });
        if (it == classes.end()) {
            classes.push_back(SourceClass{colors, {}});
            it = classes.end() - 1;
        }
        it->lands.push_back(id);
    }
    // Ordine deterministico, indipendente dall'ordine in cui le terre sono entrate.
    std::sort(classes.begin(), classes.end(),
              [](const SourceClass& a, const SourceClass& b) { return a.colors < b.colors; });
    return classes;
}

Requirements requirements(const ManaCost& cost) {
    Requirements r{};
    for (std::size_t c = 0; c < kNumColors; ++c) r[c] = cost.colored[c];
    r[kColorless] = cost.colorless;
    return r;
}

int total_mana(const ManaCost& cost) {
    const Requirements r = requirements(cost);
    int total = cost.generic;  // {X} vale 0: nessuna carta con X è implementata
    for (const int n : r) total += n;
    return total;
}

// Teorema di Hall: i simboli colorati si possono assegnare alle terre tappate se,
// per ogni sottoinsieme S dei colori richiesti, i simboli di S non superano le
// terre tappate che producono almeno un colore di S. Il generico è coperto dalle
// terre restanti, perché il totale tappato coincide con il costo.
bool feasible(const std::vector<SourceClass>& classes, const std::vector<int>& counts, const Requirements& req) {
    unsigned required = 0;
    for (std::size_t c = 0; c < kRequirementSlots; ++c) {
        if (req[c] > 0) required |= 1u << c;
    }
    for (unsigned subset = required; subset != 0; subset = (subset - 1) & required) {
        int need = 0;
        for (std::size_t c = 0; c < kRequirementSlots; ++c) {
            if ((subset >> c) & 1u) need += req[c];
        }
        int have = 0;  // le terre non producono incolore: il bit di {C} non è mai coperto
        for (std::size_t i = 0; i < classes.size(); ++i) {
            if ((classes[i].colors & subset) != 0) have += counts[i];
        }
        if (need > have) return false;
    }
    return true;
}

// Enumera i vettori di conteggi (ordine lessicografico) con somma `total` e
// chiama `visit` su quelli fattibili; si ferma se `visit` restituisce false.
template <typename Visit>
void enumerate(const std::vector<SourceClass>& classes, const Requirements& req, int total, Visit&& visit) {
    std::vector<int> counts(classes.size(), 0);
    bool stop = false;
    auto rec = [&](auto&& self, std::size_t i, int remaining) -> void {
        if (stop) return;
        if (i == classes.size()) {
            if (remaining == 0 && feasible(classes, counts, req) && !visit(counts)) stop = true;
            return;
        }
        const int max = std::min<int>(remaining, static_cast<int>(classes[i].lands.size()));
        for (int k = 0; k <= max && !stop; ++k) {
            counts[i] = k;
            self(self, i + 1, remaining - k);
        }
        counts[i] = 0;
    };
    rec(rec, 0, total);
}

// Abbinamento simboli colorati -> terre (cammini aumentanti di Kuhn).
bool augment(std::size_t slot, const std::vector<std::size_t>& slot_color, const std::vector<ColorMask>& land_colors,
             std::vector<int>& land_slot, std::vector<bool>& visited) {
    for (std::size_t l = 0; l < land_colors.size(); ++l) {
        if (visited[l] || ((land_colors[l] >> slot_color[slot]) & 1u) == 0) continue;
        visited[l] = true;
        if (land_slot[l] < 0 ||
            augment(static_cast<std::size_t>(land_slot[l]), slot_color, land_colors, land_slot, visited)) {
            land_slot[l] = static_cast<int>(slot);
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<PaymentOption> payment_options(const GameState& s, const CardDatabase& db, PlayerId p,
                                           const ManaCost& cost) {
    const std::vector<SourceClass> classes = source_classes(s, db, p);
    std::vector<PaymentOption> options;
    enumerate(classes, requirements(cost), total_mana(cost), [&](const std::vector<int>& counts) {
        PaymentOption option;
        for (std::size_t i = 0; i < classes.size(); ++i) {
            for (int k = 0; k < counts[i]; ++k) option.lands.push_back(classes[i].lands[static_cast<std::size_t>(k)]);
        }
        options.push_back(std::move(option));
        return true;
    });
    return options;
}

bool can_pay(const GameState& s, const CardDatabase& db, PlayerId p, const ManaCost& cost) {
    bool found = false;
    enumerate(source_classes(s, db, p), requirements(cost), total_mana(cost), [&](const std::vector<int>&) {
        found = true;
        return false;  // basta il primo
    });
    return found;
}

void pay_mana(GameState& s, const CardDatabase& db, PlayerId p, const ManaCost& cost, const PaymentOption& option) {
    if (static_cast<int>(option.lands.size()) != total_mana(cost)) {
        throw EngineError("pay_mana: il numero di terre non corrisponde al costo");
    }
    std::vector<ColorMask> land_colors;
    for (const CardId id : option.lands) {
        const CardObject& o = s.objects[id];
        if (o.zone != Zone::Battlefield || o.controller != p || o.has_flag(ObjectFlag::Tapped)) {
            throw EngineError("pay_mana: terra non disponibile");
        }
        land_colors.push_back(mana_colors(effective_def(s, db, id)));
    }

    // Quale colore produce ogni terra: prima i simboli colorati (abbinamento), poi il generico.
    const Requirements req = requirements(cost);
    std::vector<std::size_t> slot_color;
    for (std::size_t c = 0; c < kRequirementSlots; ++c) {
        for (int k = 0; k < req[c]; ++k) slot_color.push_back(c);
    }
    std::vector<int> land_slot(land_colors.size(), -1);
    for (std::size_t slot = 0; slot < slot_color.size(); ++slot) {
        std::vector<bool> visited(land_colors.size(), false);
        if (!augment(slot, slot_color, land_colors, land_slot, visited)) {
            throw EngineError("pay_mana: i colori delle terre non coprono il costo");
        }
    }

    // 601.2g: abilità di mana. Ogni terra si tappa e mette un mana nella riserva.
    ManaPool& pool = s.players[p].pool;
    for (std::size_t l = 0; l < option.lands.size(); ++l) {
        std::size_t color = 0;
        if (land_slot[l] >= 0) {
            color = slot_color[static_cast<std::size_t>(land_slot[l])];
        } else {
            while (((land_colors[l] >> color) & 1u) == 0) ++color;  // un colore qualsiasi per il generico
        }
        s.objects[option.lands[l]].set_flag(ObjectFlag::Tapped);
        ++pool.amount[color];
    }

    // 601.2h: si paga dalla riserva, prima i simboli colorati, poi il generico.
    for (std::size_t c = 0; c < kRequirementSlots; ++c) {
        pool.amount[c] = static_cast<std::uint8_t>(pool.amount[c] - req[c]);
    }
    int generic = cost.generic;
    for (std::size_t c = 0; c < kRequirementSlots && generic > 0; ++c) {
        const int used = std::min<int>(generic, pool.amount[c]);
        pool.amount[c] = static_cast<std::uint8_t>(pool.amount[c] - used);
        generic -= used;
    }
    if (generic != 0) throw EngineError("pay_mana: mana insufficiente per il generico");
}

}  // namespace mtg
