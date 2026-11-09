// Regole dell'agente euristico di Rakdos: ogni funzione cita la sezione di
// docs/agente_rakdos.md che implementa.

#include "rakdos_agent.hpp"

#include "combat.hpp"
#include "mana.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <optional>

namespace mtg {

namespace {

constexpr int kLifeFloor = 5;  // sezione 1: dopo un pagamento di vita restano almeno 5 punti
constexpr std::uint64_t kRakdosAgentStream = 0x2545F4914F6CDD1DULL;
constexpr std::uint8_t kAnyValue = 0xFF;

PlayerId other(PlayerId p) { return static_cast<PlayerId>(1 - p); }
bool is_main(Step st) { return st == Step::Main1 || st == Step::Main2; }

ManaCost operator+(ManaCost a, const ManaCost& b) {
    a.generic = static_cast<std::uint8_t>(a.generic + b.generic);
    a.colorless = static_cast<std::uint8_t>(a.colorless + b.colorless);
    for (std::size_t c = 0; c < kNumColors; ++c) a.colored[c] = static_cast<std::uint8_t>(a.colored[c] + b.colored[c]);
    return a;
}

// Si può pagare `cost` con queste terre (una per mana, ognuna con i colori che produce)?
// Ogni simbolo colorato va abbinato a una terra diversa che produce quel colore
// (cammini aumentanti); le terre rimaste pagano il generico.
bool affordable(const std::vector<ColorMask>& lands, const ManaCost& cost) {
    std::vector<int> pips;
    for (std::size_t c = 0; c < kNumColors; ++c) {
        for (int i = 0; i < cost.colored[c]; ++i) pips.push_back(static_cast<int>(c));
    }
    if (pips.size() + cost.generic + cost.colorless > lands.size()) return false;
    std::vector<int> owner(lands.size(), -1);
    std::vector<char> seen;
    auto augment = [&](auto&& self, int pip) -> bool {
        for (std::size_t l = 0; l < lands.size(); ++l) {
            if (seen[l] != 0 || (lands[l] & color_bit(static_cast<Color>(pips[static_cast<std::size_t>(pip)]))) == 0) {
                continue;
            }
            seen[l] = 1;
            if (owner[l] < 0 || self(self, owner[l])) {
                owner[l] = pip;
                return true;
            }
        }
        return false;
    };
    for (std::size_t p = 0; p < pips.size(); ++p) {
        seen.assign(lands.size(), 0);
        if (!augment(augment, static_cast<int>(p))) return false;
    }
    return true;
}

}  // namespace

// Valutazione di un insieme di esilio (sezione 13): confronto lessicografico, più è meglio.
struct ExileEval {
    int types = 0;              // tipi rimasti nel cimitero
    int spared = 0;             // meno Phoenix e Nethergoyf esiliati (valore negato)
    int mana_value = 0;         // valore di mana rimasto (prove future)
    int cards = 0;              // carte rimaste, se conta (Dark Matter Manipulator in campo)

    friend auto operator<=>(const ExileEval&, const ExileEval&) = default;
};

struct RakdosRules {
    using Choice = RakdosAgent::Choice;

    RakdosAgent& agent;
    const GameState& s;
    const CardDatabase& db;
    std::span<const Action> legal;
    const RakdosAgent::Ids& id;
    PlayerId me;
    PlayerId opp;

    RakdosRules(RakdosAgent& a, const GameState& state, std::span<const Action> actions)
        : agent(a), s(state), db(*a.db_), legal(actions), id(a.ids_), me(state.pending.player), opp(other(me)) {}

    // --- Informazioni di base ---------------------------------------------------------

    const CardDef& def(CardId c) const { return effective_def(s, db, c); }
    CardDefId def_id(CardId c) const { return s.objects[c].def; }
    const PlayerState& mine() const { return s.players[me]; }
    int life() const { return mine().life; }
    int opp_life() const { return s.players[opp].life; }
    bool can_pay_life(int n) const { return life() - n >= kLifeFloor; }
    bool my_turn() const { return s.turn.active_player == me; }

    // Turni di questo giocatore iniziati (1 = il suo primo turno).
    int my_turn_index() const {
        const int t = s.turn.turn_number;
        return s.turn.starting_player == me ? (t + 1) / 2 : t / 2;
    }

    bool is_land(CardId c) const { return def(c).has_type(CardType::Land); }
    bool is_creature(CardId c) const {
        const CardObject& o = s.objects[c];
        return o.zone == Zone::Battlefield && def(c).has_type(CardType::Creature) && !o.has_flag(ObjectFlag::Bestowed);
    }
    std::vector<CardId> creatures(PlayerId p) const {
        std::vector<CardId> out;
        for (const CardId c : s.battlefield) {
            if (s.objects[c].controller == p && is_creature(c)) out.push_back(c);
        }
        return out;
    }
    bool controls(PlayerId p, CardDefId d) const {
        return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
            return s.objects[c].controller == p && def_id(c) == d;
        });
    }
    int pw(CardId c) const { return power(s, db, c); }
    bool attacking(CardId c) const {
        return std::any_of(s.combat.attackers.begin(), s.combat.attackers.end(),
                           [&](const Attacker& a) { return a.creature.card == c; });
    }
    int tough(CardId c) const { return toughness(s, db, c); }
    bool keyword(CardId c, Keyword k) const { return has_keyword(s, db, c, k); }

    int lands_in_play() const {
        int n = 0;
        for (const CardId c : s.battlefield) n += (s.objects[c].controller == me && is_land(c)) ? 1 : 0;
        return n;
    }
    int lands_in_hand() const {
        int n = 0;
        for (const CardId c : mine().hand) n += is_land(c) ? 1 : 0;
        return n;
    }
    // Terre stappate che producono mana (le fetch non ne producono).
    std::vector<ColorMask> untapped_mana(CardId except = kNoCard) const {
        std::vector<ColorMask> out;
        for (const CardId c : s.battlefield) {
            const CardObject& o = s.objects[c];
            if (c == except || o.controller != me || o.has_flag(ObjectFlag::Tapped) || !is_land(c)) continue;
            const ColorMask colors = mana_colors(def(c));
            if (colors != 0) out.push_back(colors);
        }
        return out;
    }
    bool red_source_in_play() const {
        return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
            return s.objects[c].controller == me && is_land(c) && (mana_colors(def(c)) & color_bit(Color::Red)) != 0;
        });
    }
    // Il proprio grimorio contiene una terra con uno di questi tipi? (la composizione è nota)
    bool library_has(LandTypeMask types) const {
        return std::any_of(mine().library.begin(), mine().library.end(), [&](CardId c) {
            return is_land(c) && (db.get(def_id(c)).land_types & types) != 0;
        });
    }

    // --- Azioni legali --------------------------------------------------------------------

    const Action* find(ActionType type, CardDefId d, std::uint8_t value = kAnyValue, Zone zone = Zone::None) const {
        for (const Action& a : legal) {
            if (a.type != type || a.card == kNoCard || def_id(a.card) != d) continue;
            if (value != kAnyValue && a.value != value) continue;
            if (zone != Zone::None && s.objects[a.card].zone != zone) continue;
            return &a;
        }
        return nullptr;
    }
    const Action* find_type(ActionType type, std::uint8_t value = kAnyValue) const {
        for (const Action& a : legal) {
            if (a.type == type && (value == kAnyValue || a.value == value)) return &a;
        }
        return nullptr;
    }
    const Action* find_card(ActionType type, CardId c) const {
        for (const Action& a : legal) {
            if (a.type == type && a.card == c) return &a;
        }
        return nullptr;
    }
    const Action* cast(CardDefId d, CastMethod m = CastMethod::Normal, Zone zone = Zone::None) const {
        return find(ActionType::CastSpell, d, static_cast<std::uint8_t>(m), zone);
    }
    Choice pass(std::string_view rule) const {
        if (const Action* a = find_type(ActionType::Pass)) return {*a, rule};
        return fallback();
    }
    Choice fallback() const {
        if (const Action* a = find_type(ActionType::Pass)) return {*a, RakdosAgent::kDefaultRule};
        return {legal.front(), RakdosAgent::kDefaultRule};
    }

    // Tra più indici equivalenti per le regole, uno a caso (sezione 1, pareggi).
    template <typename Score>
    std::size_t best_index(std::size_t n, Score score) {
        std::vector<std::size_t> ties;
        auto best = score(0);
        for (std::size_t i = 0; i < n; ++i) {
            const auto v = score(i);
            if (i == 0 || v > best) {
                best = v;
                ties.assign(1, i);
            } else if (v == best) {
                ties.push_back(i);
            }
        }
        return ties.size() == 1 ? ties.front() : ties[agent.pick(ties.size())];
    }

    // --- Valore delle carte (sezione 12) ------------------------------------------------

    // In mano: più alto = più prezioso. Le terre dipendono da quante ne abbiamo.
    int hand_value(CardId c) const {
        const CardDefId d = def_id(c);
        const bool early = my_turn_index() <= 3;
        if (is_land(c)) {
            const int lands = lands_in_play() + lands_in_hand();
            if (lands <= 3) return 200;  // le prime tre terre valgono più di tutto
            if (lands == 4) return 55;
            return 0;                    // oltre la quarta, meno di tutto
        }
        if (d == id.fomo) return 120;
        if (d == id.nethergoyf) return 110;
        if (d == id.thoughtseize) return early ? 100 : 5;
        if (d == id.moonshadow) return early ? 90 : 65;
        if (d == id.manipulator) return 80;
        if (d == id.phoenix) return 75;
        if (d == id.flare) return 70;
        if (d == id.bolt) return 60;
        if (d == id.supplier) return 40;
        if (d == id.bauble) return 30;
        if (d == id.wraith) return 20;
        if (d == id.troll) return 10;
        return 50 + db.get(d).mana_value;  // carte di altri mazzi (Thoughtseize nel mirror)
    }
    bool low_value(CardId c) const { return hand_value(c) <= 60; }  // Lightning Bolt e sotto
    // In campo: più alto = più prezioso.
    int field_value(CardId c) const {
        const CardDefId d = def_id(c);
        if (d == id.supplier) return 1;
        if (d == id.manipulator) return 2;
        if (d == id.moonshadow && s.objects[c].counters.minus_one >= 4) return 3;
        if (d == id.nethergoyf && pw(c) <= 2) return 4;
        if (d == id.fomo) return 5;
        if (d == id.phoenix) return 6;
        return 10 + pw(c) + db.get(d).mana_value;
    }
    bool adds_missing_type(CardId c) const { return (db.get(def_id(c)).types & ~mine().gy.present) != 0; }

    // --- Piano di mana del turno (sezione 3) ----------------------------------------------

    // Costi delle giocate previste in questo turno, nell'ordine della sezione 4.
    std::vector<ManaCost> planned_costs() const {
        std::vector<ManaCost> out;
        if (!my_turn() || !is_main(s.turn.step)) return out;
        auto add_all = [&](CardDefId d, const ManaCost& cost) {
            for (const CardId c : mine().hand) {
                if (def_id(c) == d) out.push_back(cost);
            }
        };
        if (can_pay_life(2) && id.thoughtseize != kNoDef) add_all(id.thoughtseize, db.get(id.thoughtseize).cost);
        for (const CardDefId d : {id.supplier, id.manipulator, id.moonshadow, id.nethergoyf, id.fomo, id.phoenix}) {
            if (d != kNoDef) add_all(d, db.get(d).cost);
        }
        if (id.bolt != kNoDef && (agent.options_.bolt_face_asap || best_bolt_creature(6).has_value())) {
            add_all(id.bolt, db.get(id.bolt).cost);
        }
        return out;
    }
    // Quante giocate previste si possono pagare con queste terre.
    int planned_count(const std::vector<ColorMask>& lands) const {
        ManaCost total{};
        int n = 0;
        for (const ManaCost& c : planned_costs()) {
            const ManaCost next = total + c;
            if (affordable(lands, next)) {
                total = next;
                ++n;
            }
        }
        return n;
    }
    bool extra_land_helps(ColorMask colors, CardId except = kNoCard) const {
        std::vector<ColorMask> base = untapped_mana(except);
        const int without = planned_count(base);
        base.push_back(colors);
        return planned_count(base) > without;
    }

    // --- Sezione 2: mulligan ----------------------------------------------------------------

    // Colori che le terre di questa mano possono dare (le fetch: quelli delle terre cercabili).
    ColorMask hand_land_colors(const std::vector<CardId>& hand) const {
        ColorMask colors = 0;
        for (const CardId c : hand) {
            if (!is_land(c)) continue;
            const CardDef& d = def(c);
            colors = static_cast<ColorMask>(colors | mana_colors(d));
            if (d.rules.land.fetch_types != 0) {
                for (const CardId l : mine().library) {
                    if (is_land(l) && (db.get(def_id(l)).land_types & d.rules.land.fetch_types) != 0) {
                        colors = static_cast<ColorMask>(colors | mana_colors(db.get(def_id(l))));
                    }
                }
            }
        }
        return colors;
    }

    Choice mulligan() {
        const int keep_size = 7 - mine().mulligans;
        if (keep_size <= 5) return {*find_type(ActionType::Keep), "mulligan: 5 carte o meno, tiene"};
        const int drawn_lands = lands_in_hand();
        const int bottomed_lands = std::min(mine().mulligans + 0, std::max(0, drawn_lands - 3));
        const int lands = drawn_lands - bottomed_lands;
        const Action* mull = find_type(ActionType::Mulligan);
        const std::vector<CardId> hand(mine().hand.begin(), mine().hand.end());
        if (lands == 1 && agent.options_.mulligan != MulliganVariant::A) {
            // Varianti B e C: una sola terra, se le carte che cercano carte e la creatura da un mana
            // permettono di arrivare alle terre (docs/agente_rakdos.md, sezione 2).
            if (one_land_hand_is_keepable(after_bottom(hand, mine().mulligans))) {
                return {*find_type(ActionType::Keep), "mulligan: una terra con carte che cercano e creatura da un mana"};
            }
            if (mull != nullptr) return {*mull, "mulligan: una terra senza abbastanza carte che cercano"};
        }
        if ((lands < 2 || lands > 4) && mull != nullptr) return {*mull, "mulligan: terre fuori da 2-4"};
        if (keep_size == 7) {
            const ColorMask colors = hand_land_colors(hand);
            bool cheap_creature = false;
            for (const CardId c : mine().hand) {
                const CardDef& d = def(c);
                if (!d.has_type(CardType::Creature) || d.mana_value > 2) continue;
                bool castable = true;
                for (std::size_t k = 0; k < kNumColors; ++k) {
                    if (d.cost.colored[k] > 0 && (colors & color_bit(static_cast<Color>(k))) == 0) castable = false;
                }
                cheap_creature = cheap_creature || castable;
            }
            if (!cheap_creature && mull != nullptr) return {*mull, "mulligan: nessuna creatura economica lanciabile"};
        }
        return {*find_type(ActionType::Keep), "mulligan: tiene"};
    }

    bool is_cycler(CardId c) const {
        const CardDefId d = def_id(c);
        return d == id.wraith || d == id.bauble || d == id.troll;
    }
    bool is_one_drop(CardId c) const { return def(c).has_type(CardType::Creature) && def(c).mana_value == 1; }

    // Varianti B e C: la mano finale (dopo le carte in fondo) ha una sola terra. Si tiene se ha
    // abbastanza carte che cercano carte (B: due, C: una) e una creatura da un mana lanciabile con i
    // colori che quella terra dà; una fetch conta come terra che cerca la Swamp.
    bool one_land_hand_is_keepable(const std::vector<CardId>& hand) const {
        const int needed = agent.options_.mulligan == MulliganVariant::B ? 2 : 1;
        const int cyclers = static_cast<int>(std::count_if(hand.begin(), hand.end(), [&](CardId c) { return is_cycler(c); }));
        if (cyclers < needed) return false;
        const ColorMask colors = hand_land_colors(hand);
        return std::any_of(hand.begin(), hand.end(), [&](CardId c) {
            if (!is_one_drop(c)) return false;
            const CardDef& d = def(c);
            for (std::size_t k = 0; k < kNumColors; ++k) {
                if (d.cost.colored[k] > 0 && (colors & color_bit(static_cast<Color>(k))) == 0) return false;
            }
            return true;
        });
    }

    // Una carta da non mettere in fondo, in una mano con una terra sola (varianti B e C): le carte che
    // cercano carte, finché non ne restano più di due, e la creatura da un mana, se è l'unica.
    bool protected_from_bottom(CardId c, const std::vector<CardId>& remaining) const {
        const auto cyclers = std::count_if(remaining.begin(), remaining.end(), [&](CardId x) { return is_cycler(x); });
        const auto one_drops = std::count_if(remaining.begin(), remaining.end(), [&](CardId x) { return is_one_drop(x); });
        return (is_cycler(c) && cyclers <= 2) || (is_one_drop(c) && one_drops <= 1);
    }

    // Quanto conviene mettere `c` in fondo, tra le carte `remaining` (più alto = prima in fondo).
    int bottom_score(CardId c, const std::vector<CardId>& remaining) const {
        const auto lands = std::count_if(remaining.begin(), remaining.end(), [&](CardId x) { return is_land(x); });
        if (lands > 3) return is_land(c) ? 10 - land_keep_rank(c) : -1;
        const bool protect = agent.options_.mulligan != MulliganVariant::A && lands <= 1 && protected_from_bottom(c, remaining);
        return -hand_value(c) - (protect ? 10000 : 0);
    }

    // La mano dopo aver messo in fondo `n` carte con le stesse regole di bottom() (a parità, la prima).
    std::vector<CardId> after_bottom(std::vector<CardId> hand, int n) const {
        for (int i = 0; i < n && !hand.empty(); ++i) {
            std::size_t best = 0;
            for (std::size_t k = 1; k < hand.size(); ++k) {
                if (bottom_score(hand[k], hand) > bottom_score(hand[best], hand)) best = k;
            }
            hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(best));
        }
        return hand;
    }

    // Terre da mettere in fondo per prime: Swamp, Raucous Theater, Blood Crypt e Blackcleave, fetch.
    int land_keep_rank(CardId c) const {
        const CardDefId d = def_id(c);
        if (def(c).rules.land.fetch_types != 0) return 4;
        if (d == id.blood_crypt || d == id.blackcleave) return 3;
        if (d == id.theater) return 2;
        return 1;
    }

    Choice bottom() {
        std::vector<CardId> remaining;
        for (const CardId c : mine().hand) {
            if (!s.pending.chosen.contains(c)) remaining.push_back(c);
        }
        const auto lands = std::count_if(remaining.begin(), remaining.end(), [&](CardId c) { return is_land(c); });
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return bottom_score(options[k]->card, remaining); });
        if (lands > 3) return {*options[i], "carte in fondo: terre oltre la terza"};
        if (agent.options_.mulligan != MulliganVariant::A && lands <= 1) {
            return {*options[i], "carte in fondo: una terra, restano le carte che cercano"};
        }
        return {*options[i], "carte in fondo: valore piu' basso"};
    }

    // --- Sezione 3: terre, fetch, shockland -----------------------------------------------

    bool fetch_crackable(CardId c) const {
        const LandRules& r = def(c).rules.land;
        return r.fetch_types != 0 && can_pay_life(r.fetch_life) && library_has(r.fetch_types);
    }

    std::optional<Choice> play_land() {
        std::vector<const Action*> lands;
        for (const Action& a : legal) {
            if (a.type == ActionType::PlayLand && a.value == 0) lands.push_back(&a);
        }
        if (lands.empty()) return std::nullopt;
        for (const Action* a : lands) {
            if (fetch_crackable(a->card)) return Choice{*a, "terra: fetch"};
        }
        const ColorMask br = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
        for (const Action* a : lands) {
            if (def_id(a->card) == id.theater && !extra_land_helps(br)) return Choice{*a, "terra: Raucous Theater"};
        }
        for (const Action* a : lands) {
            if (def_id(a->card) == id.blackcleave && lands_in_play() <= 2) return Choice{*a, "terra: Blackcleave Cliffs"};
        }
        for (const CardDefId d : {id.blood_crypt, id.swamp}) {
            for (const Action* a : lands) {
                if (def_id(a->card) == d) return Choice{*a, d == id.swamp ? "terra: Swamp" : "terra: Blood Crypt"};
            }
        }
        return Choice{*lands.front(), "terra: l'unica disponibile"};
    }

    Choice search() {
        bool any_land = false;
        for (const Action& a : legal) any_land = any_land || a.type == ActionType::ChooseCard;
        if (!any_land) return {legal.front(), "ricerca: nessuna terra da cercare"};
        const Action* swamp = find(ActionType::ChooseCard, id.swamp);
        const Action* crypt = find(ActionType::ChooseCard, id.blood_crypt);
        const Action* theater = find(ActionType::ChooseCard, id.theater);
        if (s.pending.destination == Zone::Hand) {  // ciclo del Troll: la terra va in mano
            const bool red_known = red_source_in_play() || std::any_of(mine().hand.begin(), mine().hand.end(), [&](CardId c) {
                                       return is_land(c) && (mana_colors(def(c)) & color_bit(Color::Red)) != 0;
                                   });
            const std::array<const Action*, 3> order = red_known ? std::array{swamp, crypt, theater}
                                                                 : std::array{crypt, theater, swamp};
            for (const Action* a : order) {
                if (a != nullptr) return {*a, "ciclo del Troll: terra cercata"};
            }
        }
        const ColorMask b = color_bit(Color::Black);
        const ColorMask br = static_cast<ColorMask>(b | color_bit(Color::Red));
        std::vector<ColorMask> lands = untapped_mana();
        const int n0 = planned_count(lands);
        lands.push_back(b);
        const int nb = planned_count(lands);
        lands.back() = br;
        const int nbr = planned_count(lands);
        if (nbr > nb && crypt != nullptr) return {*crypt, "fetch: Blood Crypt (serve il rosso ora)"};
        if (nb > n0) {
            if (swamp != nullptr) return {*swamp, "fetch: Swamp (serve il nero ora)"};
            if (crypt != nullptr) return {*crypt, "fetch: Blood Crypt (serve il nero ora, Swamp finite)"};
        }
        if (theater != nullptr) return {*theater, "fetch: Raucous Theater (mana non necessario)"};
        if (crypt != nullptr) return {*crypt, "fetch: Blood Crypt (mana non necessario)"};
        if (swamp != nullptr) return {*swamp, "fetch: Swamp (mana non necessario)"};
        return fallback_choice();
    }
    Choice fallback_choice() const {
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) return {a, RakdosAgent::kDefaultRule};
        }
        return fallback();
    }

    Choice pay_life() {
        const Action* pay = find_type(ActionType::ChooseOption, kPayLife);
        const Action* tapped = find_type(ActionType::ChooseOption, kEnterTapped);
        const CardId land = s.pending.subject.card;
        if (pay == nullptr || !can_pay_life(s.pending.amount)) return {*tapped, "shockland: tappata (soglia di vita)"};
        if (extra_land_helps(mana_colors(def(land)), land)) return {*pay, "shockland: paga 2 vita (mana necessario ora)"};
        return {*tapped, "shockland: tappata (mana non necessario)"};
    }

    bool need_land() const {
        const int in_hand = lands_in_hand();
        const int needed = s.turn.lands_played == 0 ? 2 : 1;  // per questo turno e il prossimo
        return in_hand < needed || lands_in_play() + in_hand < 4;
    }

    // --- Sezione 7: Lightning Bolt ----------------------------------------------------------

    // Posizione nella lista dei bersagli (1 = il più importante), 0 = mai.
    int bolt_rank(CardId c) const {
        const CardDefId d = def_id(c);
        if (d == id.ocelot) return 1;
        if (d == id.guide) return 2;
        if (d == id.phelia) return 3;
        if (d == id.shepherd || d == id.solitude) return 5;
        if (d == id.witch || d == id.recruiter || d == id.cat) return 6;
        if (d == id.sanctifier || d == id.riddler) return 0;
        return 7;  // creature fuori dalla lista (mirror): dopo tutte le altre
    }
    bool bolt_kills(CardId c) const {
        if ((protection_of(s, db, c) & color_bit(Color::Red)) != 0) return false;
        return tough(c) - s.objects[c].damage <= 3;
    }
    // Il bersaglio migliore tra le creature avversarie di rango fino a `max_rank`.
    std::optional<CardId> best_bolt_creature(int max_rank) const {
        std::optional<CardId> best;
        int best_rank = 100;
        const auto theirs = creatures(opp);
        for (const CardId c : theirs) {
            const int r = bolt_rank(c);
            if (r == 0 || r > max_rank || !bolt_kills(c)) continue;
            // Starfield Shepherd e Solitude: solo se attaccano o se l'avversario non ha altro.
            if (r == 5 && !(theirs.size() == 1 || attacking(c))) continue;
            if (r < best_rank) {
                best_rank = r;
                best = c;
            }
        }
        return best;
    }
    std::optional<Choice> bolt_at(Target t, std::string_view rule) {
        const Action* a = cast(id.bolt, CastMethod::Normal, Zone::Hand);
        if (a == nullptr) return std::nullopt;
        agent.plan_ = RakdosAgent::Plan{id.bolt, t};
        return Choice{*a, rule};
    }
    Target creature_target(CardId c) const { return Target{TargetKindTag::Object, kNoPlayer, s.ref(c)}; }
    Target face() const { return Target{TargetKindTag::Player, opp, ObjRef{}}; }

    // Bolt che valgono in qualsiasi momento: letale, Ocelot Pride e Guide of Souls a vista.
    std::optional<Choice> bolt_anytime() {
        if (cast(id.bolt, CastMethod::Normal, Zone::Hand) == nullptr) return std::nullopt;
        if (opp_life() <= 3) return bolt_at(face(), "Bolt: in faccia, letale");
        if (const auto c = best_bolt_creature(2)) return bolt_at(creature_target(*c), "Bolt: Ocelot Pride o Guide of Souls a vista");
        return std::nullopt;
    }

    // Bolt nella sottofase finale avversaria (o nella nostra seconda fase principale per la faccia).
    std::optional<Choice> bolt_late(bool creatures_allowed) {
        if (cast(id.bolt, CastMethod::Normal, Zone::Hand) == nullptr) return std::nullopt;
        if (opp_life() <= 6) {
            if (const auto c = best_bolt_creature(3)) return bolt_at(creature_target(*c), "Bolt: creatura tra le prime tre");
            return bolt_at(face(), "Bolt: in faccia (6 vita o meno)");
        }
        if (creatures_allowed) {
            if (const auto c = best_bolt_creature(7)) return bolt_at(creature_target(*c), "Bolt: creatura nella sottofase finale avversaria");
        }
        return std::nullopt;
    }

    // --- Sezione 11: Flare of Malice ------------------------------------------------------

    std::vector<CardId> flare_targets() const {
        std::vector<CardId> out;
        for (const CardId c : s.battlefield) {
            if (s.objects[c].controller != opp) continue;
            if (is_creature(c) || def(c).has_type(CardType::Planeswalker)) out.push_back(c);
        }
        return out;
    }
    bool flare_worth_it() const {
        const auto targets = flare_targets();
        if (targets.empty()) return false;  // mai senza creature o planeswalker avversari
        if (targets.size() == 1) return true;
        int best = -1;
        for (const CardId c : targets) best = std::max(best, static_cast<int>(def(c).mana_value));
        for (const CardId c : targets) {
            if (def(c).mana_value != best) continue;
            const CardDefId d = def_id(c);
            const bool threat = d == id.riddler || d == id.teferi || d == id.solitude || d == id.shepherd || d == id.sanctifier;
            if (!threat) return false;
        }
        return true;
    }
    // Creatura nera non token che conviene sacrificare, nell'ordine della sezione 11.
    std::optional<CardId> flare_sacrifice() const {
        const auto mine_c = creatures(me);
        auto first = [&](auto pred) -> std::optional<CardId> {
            for (const CardId c : mine_c) {
                if (!s.objects[c].has_flag(ObjectFlag::Token) && pred(c)) return c;
            }
            return std::nullopt;
        };
        if (auto c = first([&](CardId c) { return def_id(c) == id.supplier; })) return c;
        if (auto c = first([&](CardId c) { return def_id(c) == id.manipulator && mine().gy.card_count < 7; })) return c;
        if (auto c = first([&](CardId c) { return def_id(c) == id.moonshadow && s.objects[c].counters.minus_one >= 4; })) return c;
        if (auto c = first([&](CardId c) { return def_id(c) == id.nethergoyf && pw(c) <= 2; })) return c;
        return std::nullopt;
    }
    std::optional<Choice> flare(std::string_view rule) {
        if (!flare_worth_it()) return std::nullopt;
        if (!my_turn()) {
            if (const Action* a = cast(id.flare, CastMethod::Normal, Zone::Hand)) return Choice{*a, rule};
        }
        if (const Action* a = cast(id.flare, CastMethod::SacrificeAlternative, Zone::Hand); a != nullptr && flare_sacrifice()) {
            return Choice{*a, rule};
        }
        return std::nullopt;
    }

    // --- Sezioni 10 e 13: esilio dal cimitero ------------------------------------------------

    // Valuta il cimitero dopo l'uscita della carta lanciata (se era nel cimitero) e l'esilio di `set`.
    ExileEval evaluate(const std::vector<CardId>& set, CardId leaving) const {
        TypeMask present = 0;
        ExileEval e;
        for (const CardId c : mine().graveyard) {
            if (c == leaving || std::find(set.begin(), set.end(), c) != set.end()) continue;
            const CardDef& d = db.get(def_id(c));
            present = static_cast<TypeMask>(present | d.types);
            e.mana_value += d.mana_value;
            ++e.cards;
        }
        e.types = std::popcount(present);
        for (const CardId c : set) {
            if (def_id(c) == id.phoenix || def_id(c) == id.nethergoyf) --e.spared;
        }
        if (!controls(me, id.manipulator)) e.cards = 0;
        return e;
    }
    // Il miglior insieme di esilio per lanciare `card` con `method`, tra quelli coerenti con `prefix`.
    std::optional<std::pair<std::vector<CardId>, ExileEval>> best_exile(CardId card, CastMethod method,
                                                                       const PlayerZone* prefix = nullptr) {
        std::vector<std::vector<CardId>> sets = graveyard_exile_sets(s, db, me, card, method);
        if (prefix != nullptr) {
            std::erase_if(sets, [&](const std::vector<CardId>& set) {
                return set.size() <= prefix->size() || !std::equal(prefix->begin(), prefix->end(), set.begin());
            });
        }
        if (sets.empty()) return std::nullopt;
        const CardId leaving = s.objects[card].zone == Zone::Graveyard ? card : kNoCard;
        const std::size_t i = best_index(sets.size(), [&](std::size_t k) { return evaluate(sets[k], leaving); });
        return std::pair{sets[i], evaluate(sets[i], leaving)};
    }

    Choice exile() {
        const StackItem& item = s.stack_items.back();
        const auto best = best_exile(item.source.card, item.method, &s.pending.chosen);
        if (best) {
            const CardId next = best->first[s.pending.chosen.size()];
            if (const Action* a = find_card(ActionType::ChooseCard, next)) return {*a, "esilio: conserva i tipi e le carte da rilanciare"};
        }
        return fallback_choice();
    }

    // --- Sezione 10: Detective's Phoenix ------------------------------------------------

    // La nostra creatura migliore da incantare: più forza (contando che con la rapidità attacca).
    std::optional<CardId> bestow_target() const {
        std::optional<CardId> best;
        for (const CardId c : creatures(me)) {
            if ((protection_of(s, db, c) & color_bit(Color::Red)) != 0) continue;
            if (!best || pw(c) > pw(*best)) best = c;
        }
        return best;
    }
    int expected_attack_damage(std::optional<CardId> hasted) const {
        int total = 0;
        for (const CardId c : creatures(me)) {
            const CardObject& o = s.objects[c];
            const bool ready = !o.has_flag(ObjectFlag::Tapped) &&
                               (!o.has_flag(ObjectFlag::SummoningSick) || keyword(c, Keyword::Haste) || hasted == c);
            if (ready) total += std::max(0, pw(c));
        }
        return total;
    }
    bool opp_has_untapped_creature() const {
        const auto theirs = creatures(opp);
        return std::any_of(theirs.begin(), theirs.end(), [&](CardId c) { return !s.objects[c].has_flag(ObjectFlag::Tapped); });
    }

    std::optional<Choice> phoenix() {
        if (const auto target = bestow_target()) {
            for (const Zone zone : {Zone::Graveyard, Zone::Hand}) {
                const Action* a = cast(id.phoenix, CastMethod::Bestow, zone);
                if (a == nullptr) continue;
                const auto best = best_exile(a->card, CastMethod::Bestow);
                if (!best) continue;
                // Il delirium si controlla dopo le prove E dopo l'uscita della Phoenix dal cimitero.
                const bool keeps_delirium = !mine().gy.delirium() || best->second.types >= 4;
                const bool lethal = s.turn.step == Step::Main1 && !opp_has_untapped_creature() &&
                                    expected_attack_damage(target) + 2 >= opp_life();
                if (keeps_delirium || lethal) {
                    agent.plan_ = RakdosAgent::Plan{id.phoenix, creature_target(*target)};
                    return Choice{*a, zone == Zone::Graveyard ? "Phoenix: conferimento dal cimitero"
                                                              : "Phoenix: conferimento dalla mano"};
                }
            }
        }
        if (const Action* a = cast(id.phoenix, CastMethod::Normal, Zone::Hand)) return Choice{*a, "Phoenix: lancio normale"};
        return std::nullopt;
    }

    // --- Sezione 13: fuga di Nethergoyf ---------------------------------------------------

    std::optional<Choice> escape() {
        const Action* a = cast(id.nethergoyf, CastMethod::Escape, Zone::Graveyard);
        if (a == nullptr) return std::nullopt;
        const auto best = best_exile(a->card, CastMethod::Escape);
        if (!best) return std::nullopt;
        // I tipi rimasti si contano senza il Nethergoyf che esce dal cimitero.
        const int needed = controls(me, id.fomo) && mine().gy.delirium() ? 4 : 3;
        int their_power = 0;
        for (const CardId c : creatures(opp)) their_power += std::max(0, pw(c));
        const bool must_block = their_power >= life();
        if (best->second.types >= needed) return Choice{*a, "Nethergoyf: fuga (restano abbastanza tipi)"};
        if (must_block) return Choice{*a, "Nethergoyf: fuga (serve un bloccante)"};
        return std::nullopt;
    }

    // --- Sezione 4: priorità ----------------------------------------------------------------

    bool one_drop_castable() const {
        for (const CardDefId d : {id.supplier, id.manipulator, id.moonshadow, id.nethergoyf}) {
            if (cast(d, CastMethod::Normal, Zone::Hand) != nullptr) return true;
        }
        return false;
    }

    // Un solo bloccante possibile davanti a un attacco importante (forza totale 4 o più).
    std::optional<CardId> sole_blocker() const {
        std::vector<CardId> blockers;
        for (const CardId c : creatures(opp)) {
            if (!s.objects[c].has_flag(ObjectFlag::Tapped)) blockers.push_back(c);
        }
        if (blockers.size() != 1) return std::nullopt;
        return expected_attack_damage(std::nullopt) >= 4 ? std::optional<CardId>(blockers.front()) : std::nullopt;
    }

    std::optional<Choice> main_phase(bool main1) {
        // Mishra's Bauble in campo: si sacrifica subito.
        if (const Action* a = find(ActionType::ActivateAbility, id.bauble, kAnyValue, Zone::Battlefield)) {
            return Choice{*a, "Mishra's Bauble: sacrificato subito"};
        }
        // Fetch in campo: si rompe subito.
        for (const Action& a : legal) {
            if (a.type == ActionType::ActivateAbility && s.objects[a.card].zone == Zone::Battlefield &&
                fetch_crackable(a.card)) {
                return Choice{a, "fetch: rotta subito"};
            }
        }
        // 1-2. Cicli: Street Wraith (2 vita), Troll se serve una terra.
        if (const Action* a = find(ActionType::ActivateAbility, id.wraith, kAnyValue, Zone::Hand); a && can_pay_life(2)) {
            return Choice{*a, "ciclo di Street Wraith"};
        }
        if (const Action* a = find(ActionType::ActivateAbility, id.troll, kAnyValue, Zone::Hand);
            a && need_land() && library_has(db.get(id.troll).rules.landcycling.types)) {
            return Choice{*a, "ciclo del Troll (serve una terra)"};
        }
        // 3. Mishra's Bauble.
        if (const Action* a = cast(id.bauble, CastMethod::Normal, Zone::Hand)) return Choice{*a, "Mishra's Bauble: lanciato"};
        // 4. Terra.
        if (auto c = play_land()) return c;
        // 5. Thoughtseize (al primo turno, dopo la creatura da un mana).
        if (const Action* a = cast(id.thoughtseize, CastMethod::Normal, Zone::Hand); a && can_pay_life(2)) {
            if (!(my_turn_index() == 1 && one_drop_castable())) return Choice{*a, "Thoughtseize: il prima possibile"};
        }
        // 6-7. Creature: prima quelle che macinano, poi Moonshadow e Nethergoyf.
        for (const CardDefId d : {id.supplier, id.manipulator, id.moonshadow, id.nethergoyf}) {
            if (const Action* a = cast(d, CastMethod::Normal, Zone::Hand)) {
                return Choice{*a, d == id.supplier       ? "creatura: Stitcher's Supplier"
                                  : d == id.manipulator  ? "creatura: Dark Matter Manipulator"
                                  : d == id.moonshadow   ? "creatura: Moonshadow"
                                                         : "creatura: Nethergoyf"};
            }
        }
        // 8. Fear of Missing Out.
        if (const Action* a = cast(id.fomo, CastMethod::Normal, Zone::Hand)) return Choice{*a, "Fear of Missing Out"};
        // 9. Detective's Phoenix.
        if (auto c = phoenix()) return c;
        if (main1) {
            // 10. Rimozione prima dell'attacco: il solo bloccante davanti a un attacco importante.
            if (const auto blocker = sole_blocker()) {
                if (bolt_rank(*blocker) != 0 && bolt_kills(*blocker)) {
                    if (auto c = bolt_at(creature_target(*blocker), "Bolt: il solo bloccante prima dell'attacco")) return c;
                }
                if (auto c = flare("Flare: il solo bloccante prima dell'attacco")) return c;
            }
            if (controls(opp, id.teferi)) {
                if (auto c = flare("Flare: Teferi in campo, a velocita' stregoneria")) return c;
            }
            // Sezione 5: il quarto tipo prima di attaccare con FOMO.
            if (fomo_ready() && mine().gy.distinct_types() == 3 &&
                (mine().gy.present & type_bit(CardType::Instant)) == 0 && !best_bolt_creature(7)) {
                if (auto c = bolt_at(face(), "Bolt: in faccia per il delirium di FOMO")) return c;
            }
            if (agent.options_.bolt_face_asap && !best_bolt_creature(7)) {
                if (auto c = bolt_at(face(), "Bolt: in faccia appena possibile (variante)")) return c;
            }
        } else {
            if (auto c = escape()) return c;
            if (const Action* a = cast(id.troll, CastMethod::Normal, Zone::Hand); a && lands_in_play() >= 6) {
                return Choice{*a, "Troll: lanciato per sei mana"};
            }
            if (auto c = bolt_late(false)) return c;
        }
        return std::nullopt;
    }

    bool fomo_ready() const {
        for (const CardId c : creatures(me)) {
            const CardObject& o = s.objects[c];
            if (def_id(c) == id.fomo && !o.has_flag(ObjectFlag::Tapped) && !o.has_flag(ObjectFlag::SummoningSick) &&
                !o.has_flag(ObjectFlag::AttackedThisTurn)) {
                return true;
            }
        }
        return false;
    }

    Choice priority() {
        if (auto c = bolt_anytime()) return *c;
        if (my_turn() && is_main(s.turn.step) && s.stack_items.empty()) {
            if (auto c = main_phase(s.turn.step == Step::Main1)) return *c;
            return pass("priorita': fine delle giocate della fase");
        }
        if (!my_turn() && s.turn.step == Step::End) {
            if (auto c = bolt_late(true)) return *c;
            if (auto c = flare("Flare: sottofase finale avversaria")) return *c;
        }
        return pass("priorita': niente da fare ora");
    }

    // --- Bersagli ---------------------------------------------------------------------

    const Action* target_action(const Target& t) const {
        if (t.tag == TargetKindTag::Player) {
            for (const Action& a : legal) {
                if (a.type == ActionType::TargetPlayer && a.value == t.player) return &a;
            }
            return nullptr;
        }
        if (!s.is_valid(t.object)) return nullptr;
        const CardId c = t.object.card;
        if (const Action* a = find_card(ActionType::TargetObject, c)) return a;
        // Creature identiche sono offerte una volta sola: si cerca una gemella.
        for (const Action& a : legal) {
            if (a.type == ActionType::TargetObject && def_id(a.card) == def_id(c) &&
                s.objects[a.card].controller == s.objects[c].controller) {
                return &a;
            }
        }
        return nullptr;
    }

    Choice target() {
        const StackItem& top = s.stack_items.back();
        if (top.kind == StackKind::Spell) {
            if (agent.plan_.def == top.source_def) {
                if (const Action* a = target_action(agent.plan_.target)) return {*a, "bersaglio: quello deciso al lancio"};
            }
            if (top.source_def == id.thoughtseize) {
                if (const Action* a = target_action(face())) return {*a, "Thoughtseize: l'avversario"};
            }
            if (top.source_def == id.bolt) {
                if (const auto c = best_bolt_creature(7)) {
                    if (const Action* a = target_action(creature_target(*c))) return {*a, "Bolt: la creatura migliore"};
                }
                if (const Action* a = target_action(face())) return {*a, "Bolt: in faccia"};
            }
            if (top.source_def == id.phoenix) {
                if (const auto c = bestow_target()) {
                    if (const Action* a = target_action(creature_target(*c))) return {*a, "Phoenix: la nostra creatura migliore"};
                }
            }
            return fallback();
        }
        if (top.effect == StackEffect::LookAtTop) {
            if (const Action* a = target_action(Target{TargetKindTag::Player, me, ObjRef{}})) return {*a, "Mishra's Bauble: se stesso"};
        }
        if (top.effect == StackEffect::UntapAndExtraCombat) return fomo_untap();
        return fallback();
    }

    // Sezione 9: la nostra creatura attaccante con più forza, prima quelle difficili da bloccare;
    // con due trigger, creature diverse se possibile.
    Choice fomo_untap() {
        std::vector<CardId> already;
        for (std::size_t i = 0; i + 1 < s.stack_items.size(); ++i) {
            const StackItem& it = s.stack_items[i];
            if (it.effect == StackEffect::UntapAndExtraCombat && it.target.tag == TargetKindTag::Object) {
                already.push_back(it.target.object.card);
            }
        }
        std::vector<const Action*> options;
        for (const Action& a : legal) {
            if (a.type == ActionType::TargetObject && s.objects[a.card].controller == me) options.push_back(&a);
        }
        if (options.empty()) return fallback();
        const std::size_t i = best_index(options.size(), [&](std::size_t k) {
            const CardId c = options[k]->card;
            const bool in_attack = attacking(c);
            const bool evasive = keyword(c, Keyword::Flying) || keyword(c, Keyword::Menace);
            const bool fresh = std::find(already.begin(), already.end(), c) == already.end();
            return std::array{fresh ? 1 : 0, in_attack ? 1 : 0, evasive ? 1 : 0, pw(c)};
        });
        return {*options[i], "FOMO: stappa l'attaccante con piu' forza"};
    }

    // --- Pagamento del mana (sezione 14) --------------------------------------------------

    const ManaCost& pending_cost() const {
        const StackItem& item = s.stack_items.back();
        const CardDef& d = db.get(item.source_def);
        if (item.kind == StackKind::Spell) {
            if (item.method == CastMethod::Escape) return d.rules.escape.cost;
            if (item.method == CastMethod::Bestow) return d.rules.bestow.cost;
            return d.cost;
        }
        return d.rules.landcycling.cost;
    }

    Choice pay_mana() {
        const std::vector<PaymentOption> options = payment_options(s, db, me, pending_cost());
        if (options.size() != legal.size()) return fallback();
        const std::size_t i = best_index(options.size(), [&](std::size_t k) {
            int duals_left = 0;
            for (const CardId c : s.battlefield) {
                const CardObject& o = s.objects[c];
                if (o.controller != me || o.has_flag(ObjectFlag::Tapped) || !is_land(c)) continue;
                if (std::find(options[k].lands.begin(), options[k].lands.end(), c) != options[k].lands.end()) continue;
                duals_left += std::popcount(static_cast<unsigned>(mana_colors(def(c)))) >= 2 ? 1 : 0;
            }
            return duals_left;
        });
        return {legal[i], "pagamento: prima le terre di un solo colore"};
    }

    // --- Sezione 6: cosa prende Thoughtseize ---------------------------------------------

    Choice thoughtseize_pick() {
        const std::array<CardDefId, 16> order = {id.sanctifier, id.solitude, id.thraben_charm, id.march,
                                                 id.teferi,     id.ocelot,   id.guide,         id.phelia,
                                                 id.seam_rip,   id.riddler,  id.ephemerate,    id.force,
                                                 id.witch,      id.shepherd, id.consign,       id.recruiter};
        for (const CardDefId d : order) {
            if (d == kNoDef) continue;
            if (const Action* a = find(ActionType::ChooseCard, d)) return {*a, "Thoughtseize: lista contro UW"};
        }
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        if (options.empty()) return fallback();
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return hand_value(options[k]->card); });
        return {*options[i], "Thoughtseize: la carta di valore piu' alto"};
    }

    // --- Scarti (sezione 9) e sorveglianza (sezione 14) ----------------------------------

    Choice discard() {
        std::vector<const Action*> options;
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) options.push_back(&a);
        }
        if (options.empty()) return fallback();
        for (const Action* a : options) {
            if (def_id(a->card) == id.phoenix && red_source_in_play()) return {*a, "scarto: Phoenix (si conferisce dal cimitero)"};
        }
        std::vector<const Action*> missing;
        for (const Action* a : options) {
            if (adds_missing_type(a->card) && low_value(a->card)) missing.push_back(a);
        }
        if (!missing.empty()) {
            const std::size_t i = best_index(missing.size(), [&](std::size_t k) { return -hand_value(missing[k]->card); });
            return {*missing[i], "scarto: tipo mancante nel cimitero"};
        }
        if (lands_in_play() + lands_in_hand() >= 4) {
            for (const Action* a : options) {
                if (is_land(a->card)) return {*a, "scarto: terra in piu'"};
            }
        }
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return -hand_value(options[k]->card); });
        return {*options[i], "scarto: valore piu' basso"};
    }

    Choice surveil() {
        const Action* yard = find_type(ActionType::ChooseOption, kSurveilToGraveyard);
        const Action* top = find_type(ActionType::ChooseOption, kSurveilKeepOnTop);
        if (mine().library.empty() || yard == nullptr || top == nullptr) return fallback();
        const CardId card = mine().library.back();  // la carta guardata sorvegliando
        if (def_id(card) == id.phoenix) return {*yard, "sorveglianza: Phoenix nel cimitero"};
        if (is_land(card) && lands_in_play() + lands_in_hand() >= 4) return {*yard, "sorveglianza: terra in piu'"};
        if (!is_land(card) && hand_value(card) <= 40 && adds_missing_type(card)) {
            return {*yard, "sorveglianza: tipo mancante, carta di poco valore"};
        }
        return {*top, "sorveglianza: resta in cima"};
    }

    // --- Sezione 8: attacchi e blocchi ------------------------------------------------------

    bool can_block(CardId blocker, CardId attacker) const {
        if (keyword(attacker, Keyword::Flying) && !keyword(blocker, Keyword::Flying) && !keyword(blocker, Keyword::Reach)) {
            return false;
        }
        if ((protection_of(s, db, attacker) & def(blocker).colors) != 0) return false;
        const LandTypeMask walk = def(attacker).rules.combat.landwalk;
        if (walk != 0) {
            const PlayerId defender = s.objects[blocker].controller;
            for (const CardId c : s.battlefield) {
                if (s.objects[c].controller == defender && is_land(c) && (def(c).land_types & walk) != 0) return false;
            }
        }
        return true;
    }
    int blockers_needed(CardId attacker) const {
        int n = keyword(attacker, Keyword::Menace) ? 2 : 1;
        return std::max(n, static_cast<int>(def(attacker).rules.combat.min_blockers));
    }
    // Esito di un blocco singolo: {l'attaccante muore, il bloccante muore}.
    std::pair<bool, bool> fight(CardId attacker, CardId blocker) const {
        const int to_blocker = combat::damage_after_prevention(s, db, blocker, def(attacker).colors, std::max(0, pw(attacker)));
        const int to_attacker = combat::damage_after_prevention(s, db, attacker, def(blocker).colors, std::max(0, pw(blocker)));
        bool blocker_dies = to_blocker > 0 && s.objects[blocker].damage + to_blocker >= tough(blocker);
        bool attacker_dies = to_attacker > 0 && s.objects[attacker].damage + to_attacker >= tough(attacker);
        const bool att_fs = keyword(attacker, Keyword::FirstStrike);
        const bool blk_fs = keyword(blocker, Keyword::FirstStrike);
        if (att_fs && !blk_fs && blocker_dies) attacker_dies = false;  // il bloccante muore prima di colpire
        if (blk_fs && !att_fs && attacker_dies) blocker_dies = false;
        return {attacker_dies, blocker_dies};
    }
    std::vector<CardId> potential_blockers(CardId attacker) const {
        std::vector<CardId> out;
        for (const CardId c : creatures(opp)) {
            if (!s.objects[c].has_flag(ObjectFlag::Tapped) && can_block(c, attacker)) out.push_back(c);
        }
        return out;
    }

    Choice attack() {
        const CardId att = s.pending.subject.card;
        const Action* yes = find_type(ActionType::TargetPlayer);
        const Action* no = find_type(ActionType::ChooseOption, kNoAttack);
        if (yes == nullptr || no == nullptr) return fallback();

        // Resta indietro la creatura che blocca meglio, se le creature avversarie possono ucciderci.
        int their_power = 0;
        for (const CardId c : creatures(opp)) their_power += std::max(0, pw(c));
        if (their_power >= life()) {
            const auto ours = creatures(me);
            const auto best = std::max_element(ours.begin(), ours.end(), [&](CardId a, CardId b) {
                return std::pair{tough(a), pw(a)} < std::pair{tough(b), pw(b)};
            });
            if (best != ours.end() && *best == att) return {*no, "attacco: resta a bloccare"};
        }
        if (def_id(att) == id.fomo && !s.objects[att].has_flag(ObjectFlag::AttackedThisTurn) && mine().gy.delirium()) {
            return {*yes, "attacco: FOMO con il delirium"};
        }
        const auto blockers = potential_blockers(att);
        if (static_cast<int>(blockers.size()) < blockers_needed(att)) return {*yes, "attacco: non puo' essere bloccata"};
        const bool good_block = blockers_needed(att) == 1 && std::any_of(blockers.begin(), blockers.end(), [&](CardId b) {
                                    const auto [att_dies, blk_dies] = fight(att, b);
                                    return att_dies && !blk_dies;
                                });
        if (!good_block) return {*yes, "attacco: nessun blocco conviene all'avversario"};
        // Attacco letale: la forza che passa anche se ogni bloccante ferma un attaccante.
        std::vector<int> powers;
        for (const CardId c : creatures(me)) {
            const CardObject& o = s.objects[c];
            if (!o.has_flag(ObjectFlag::Tapped) && (!o.has_flag(ObjectFlag::SummoningSick) || keyword(c, Keyword::Haste))) {
                powers.push_back(std::max(0, pw(c)));
            }
        }
        std::sort(powers.begin(), powers.end(), std::greater<>());
        const std::size_t stopped = std::min(powers.size(), potential_blockers(att).size());
        int through = 0;
        for (std::size_t i = stopped; i < powers.size(); ++i) through += powers[i];
        if (through >= opp_life()) return {*yes, "attacco: attacco letale"};
        return {*no, "attacco: non conviene"};
    }

    Choice block() {
        const CardId att = s.pending.subject.card;
        const std::uint8_t index = s.combat.cursor;
        std::vector<CardId> current;
        for (const Block& b : s.combat.blocks) {
            if (b.attacker == index) current.push_back(b.blocker.card);
        }
        const Action* done = find_type(ActionType::ChooseOption, kDoneBlocking);
        std::vector<const Action*> adds;
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) adds.push_back(&a);
        }
        auto least_valuable = [&](const std::vector<const Action*>& v) {
            const std::size_t i = best_index(v.size(), [&](std::size_t k) { return -field_value(v[k]->card); });
            return v[i];
        };
        if (!current.empty()) {
            if (agent.block_attacker_ == att && agent.block_second_ != kNoCard) {
                if (const Action* a = find_card(ActionType::ChooseCard, agent.block_second_)) {
                    agent.block_second_ = kNoCard;
                    return {*a, "blocco: in due"};
                }
            }
            if (done != nullptr) return {*done, "blocco: fine"};
            if (!adds.empty()) return {*least_valuable(adds), "blocco: secondo bloccante obbligato"};
            return fallback();
        }
        if (adds.empty()) return done != nullptr ? Choice{*done, "blocco: nessun bloccante"} : fallback();
        if (blockers_needed(att) == 1) {
            std::vector<const Action*> wins, trades;
            for (const Action* a : adds) {
                const auto [att_dies, blk_dies] = fight(att, a->card);
                if (att_dies && !blk_dies) wins.push_back(a);
                if (att_dies && blk_dies && field_value(att) > field_value(a->card)) trades.push_back(a);
            }
            if (!wins.empty()) return {*least_valuable(wins), "blocco: uccide e sopravvive"};
            if (!trades.empty()) return {*least_valuable(trades), "blocco: scambio favorevole"};
        }
        // In due: insieme lo uccidono, e l'attaccante può uccidere al massimo il meno prezioso.
        for (const Action* a : adds) {
            for (const Action* b : adds) {
                if (a == b) continue;
                const int damage = std::max(0, pw(a->card)) + std::max(0, pw(b->card));
                if (damage < tough(att) - s.objects[att].damage) continue;
                const CardId cheap = field_value(a->card) <= field_value(b->card) ? a->card : b->card;
                const CardId dear = cheap == a->card ? b->card : a->card;
                if (fight(att, dear).second) continue;  // morirebbe anche il più prezioso
                agent.block_attacker_ = att;
                agent.block_second_ = b->card;
                return {*a, "blocco: in due"};
            }
        }
        // Blocco di sacrificio solo se altrimenti moriremmo in questo turno.
        int incoming = 0;
        for (const Attacker& at : s.combat.attackers) incoming += std::max(0, pw(at.creature.card));
        if (incoming >= life() && blockers_needed(att) == 1) return {*least_valuable(adds), "blocco: di sacrificio"};
        if (done != nullptr) return {*done, "blocco: nessuno"};
        return {*least_valuable(adds), "blocco: obbligato"};
    }

    Choice divide() {
        const combat::DamageDivision division = combat::damage_division(s, db);
        if (division.splits.size() != legal.size() || division.splits.empty()) return fallback();
        const CardId att = s.pending.subject.card;
        const std::size_t i = best_index(division.splits.size(), [&](std::size_t k) {
            int score = 0;
            for (std::size_t j = 0; j < division.blockers.size(); ++j) {
                const CardId b = division.blockers[j];
                const int dealt = combat::damage_after_prevention(s, db, b, def(att).colors, division.splits[k][j]);
                if (s.objects[b].damage + dealt >= tough(b)) score += 100 - 10 * bolt_rank(b) + field_value(b);
            }
            return score;
        });
        return {legal[i], "divisione del danno: prima i bloccanti piu' pericolosi"};
    }

    // --- Trigger e sacrifici ----------------------------------------------------------------

    Choice order_triggers() {
        // Il trigger scelto va in pila per primo e si risolve per ultimo: prima i non-macinate.
        for (const Action& a : legal) {
            if (s.pending_triggers[a.value].effect != StackEffect::Mill) return {a, "ordine dei trigger: prima si risolvono le macinate"};
        }
        return {legal.front(), "ordine dei trigger: prima si risolvono le macinate"};
    }

    Choice sacrifice_cost() {
        if (const auto c = flare_sacrifice()) {
            if (const Action* a = find_card(ActionType::ChooseCard, *c)) return {*a, "Flare: sacrifica la creatura meno preziosa"};
        }
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return -field_value(options[k]->card); });
        return {*options[i], "Flare: sacrifica la creatura meno preziosa"};
    }

    Choice sacrifice_effect() {
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return -field_value(options[k]->card); });
        return {*options[i], "sacrificio per Flare avversario: il meno prezioso"};
    }

    // --- Smistamento ------------------------------------------------------------------------

    Choice decide() {
        switch (s.pending.kind) {
            case DecisionKind::Mulligan: return mulligan();
            case DecisionKind::BottomCards: return bottom();
            case DecisionKind::Priority: return priority();
            case DecisionKind::PayLifeOrTapped: return pay_life();
            case DecisionKind::SearchLibrary: return search();
            case DecisionKind::Surveil: return surveil();
            case DecisionKind::DiscardToHandSize:
            case DecisionKind::DiscardCard: return discard();
            case DecisionKind::ChooseTarget: return target();
            case DecisionKind::PayMana: return pay_mana();
            case DecisionKind::ChooseFromHand: return thoughtseize_pick();
            case DecisionKind::DeclareAttacker: return attack();
            case DecisionKind::DeclareBlockers: return block();
            case DecisionKind::DivideDamage: return divide();
            case DecisionKind::OrderTriggers: return order_triggers();
            case DecisionKind::ExileFromGraveyard: return exile();
            case DecisionKind::SacrificeForCost: return sacrifice_cost();
            case DecisionKind::SacrificeForEffect: return sacrifice_effect();
            case DecisionKind::LegendRule:
            case DecisionKind::None:
            case DecisionKind::GameOver: break;
        }
        return fallback();
    }
};

namespace {

CardDefId lookup(const CardDatabase& db, std::string_view name) {
    const CardDef* d = db.find(name);
    return d == nullptr ? kNoDef : d->id;
}

}  // namespace

RakdosAgent::RakdosAgent(const CardDatabase& db, std::uint64_t seed, RakdosAgentOptions options)
    : db_(&db), options_(options), rng_(seed, kRakdosAgentStream) {
    Ids& i = ids_;
    i.nethergoyf = lookup(db, "Nethergoyf");
    i.moonshadow = lookup(db, "Moonshadow");
    i.manipulator = lookup(db, "Dark Matter Manipulator");
    i.supplier = lookup(db, "Stitcher's Supplier");
    i.fomo = lookup(db, "Fear of Missing Out");
    i.phoenix = lookup(db, "Detective's Phoenix");
    i.wraith = lookup(db, "Street Wraith");
    i.troll = lookup(db, "Troll of Khazad-dûm");
    i.flare = lookup(db, "Flare of Malice");
    i.bolt = lookup(db, "Lightning Bolt");
    i.thoughtseize = lookup(db, "Thoughtseize");
    i.bauble = lookup(db, "Mishra's Bauble");
    i.swamp = lookup(db, "Swamp");
    i.blood_crypt = lookup(db, "Blood Crypt");
    i.blackcleave = lookup(db, "Blackcleave Cliffs");
    i.theater = lookup(db, "Raucous Theater");
    i.sanctifier = lookup(db, "Sanctifier en-Vec");
    i.solitude = lookup(db, "Solitude");
    i.thraben_charm = lookup(db, "Thraben Charm");
    i.march = lookup(db, "March of Otherworldly Light");
    i.teferi = lookup(db, "Teferi, Time Raveler");
    i.ocelot = lookup(db, "Ocelot Pride");
    i.guide = lookup(db, "Guide of Souls");
    i.phelia = lookup(db, "Phelia, Exuberant Shepherd");
    i.seam_rip = lookup(db, "Seam Rip");
    i.riddler = lookup(db, "Quantum Riddler");
    i.ephemerate = lookup(db, "Ephemerate");
    i.force = lookup(db, "Force of Negation");
    i.witch = lookup(db, "Witch Enchanter");
    i.shepherd = lookup(db, "Starfield Shepherd");
    i.consign = lookup(db, "Consign to Memory");
    i.recruiter = lookup(db, "Recruiter of the Guard");
    i.cat = lookup(db, "Cat Token");
}

Action RakdosAgent::choose(const GameState& s, std::span<const Action> legal) {
    if (legal.empty()) throw EngineError("RakdosAgent: nessuna azione legale");
    const Choice c = decide(s, legal);
    if (std::find(legal.begin(), legal.end(), c.action) == legal.end()) {
        throw EngineError("RakdosAgent: la regola \"" + std::string(c.rule) + "\" ha scelto un'azione non legale");
    }
    last_rule_ = c.rule;
    ++counts_[std::string(c.rule)];
    return c.action;
}

RakdosAgent::Choice RakdosAgent::decide(const GameState& s, std::span<const Action> legal) {
    RakdosRules rules(*this, s, legal);
    return rules.decide();
}

std::size_t RakdosAgent::pick(std::size_t n) { return rng_.bounded(static_cast<std::uint32_t>(n)); }

}  // namespace mtg
