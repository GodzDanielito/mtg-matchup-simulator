// Regole dell'agente euristico di UW Blink: ogni funzione cita la sezione di
// docs/agente_uw.md che implementa.

#include "uw_agent.hpp"

#include "combat.hpp"
#include "mana.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <vector>

namespace mtg {

namespace {

constexpr std::uint64_t kUwAgentStream = 0x7F4A7C159E3779B9ULL;
constexpr std::uint8_t kAnyValue = 0xFF;
constexpr int kLifeFloor = 5;  // sezione 1: pavimento fisso della soglia di vita
constexpr int kBoltDamage = 3;

PlayerId other(PlayerId p) { return static_cast<PlayerId>(1 - p); }
bool is_main(Step st) { return st == Step::Main1 || st == Step::Main2; }

CardDefId lookup(const CardDatabase& db, std::string_view name) {
    const CardDef* d = db.find(name);
    return d == nullptr ? kNoDef : d->id;
}

ManaCost operator+(ManaCost a, const ManaCost& b) {
    a.generic = static_cast<std::uint8_t>(a.generic + b.generic);
    a.colorless = static_cast<std::uint8_t>(a.colorless + b.colorless);
    for (std::size_t c = 0; c < kNumColors; ++c) a.colored[c] = static_cast<std::uint8_t>(a.colored[c] + b.colored[c]);
    return a;
}

ManaCost mana(int generic, int white = 0, int blue = 0) {
    ManaCost c{};
    c.generic = static_cast<std::uint8_t>(generic);
    c.colored[static_cast<std::size_t>(Color::White)] = static_cast<std::uint8_t>(white);
    c.colored[static_cast<std::size_t>(Color::Blue)] = static_cast<std::uint8_t>(blue);
    return c;
}

// Si può pagare `cost` con queste terre (una per mana, ognuna con i colori che produce)?
// Ogni simbolo colorato va abbinato a una terra diversa (cammini aumentanti).
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

struct UwRules {
    using Choice = UwAgent::Choice;
    using Plan = UwAgent::Plan;

    UwAgent& agent;
    const GameState& s;
    const CardDatabase& db;
    std::span<const Action> legal;
    const UwAgent::Ids& id;
    PlayerId me;
    PlayerId opp;

    UwRules(UwAgent& a, const GameState& state, std::span<const Action> actions)
        : agent(a), s(state), db(*a.db_), legal(actions), id(a.ids_), me(state.pending.player), opp(other(me)) {}

    // --- Informazioni di base -------------------------------------------------------------

    const CardDef& def(CardId c) const { return effective_def(s, db, c); }
    CardDefId def_id(CardId c) const { return s.objects[c].def; }
    const PlayerState& mine() const { return s.players[me]; }
    const PlayerState& theirs() const { return s.players[opp]; }
    int life() const { return mine().life; }
    int opp_life() const { return theirs().life; }
    bool my_turn() const { return s.turn.active_player == me; }
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
    std::optional<CardId> on_field(PlayerId p, CardDefId d) const {
        for (const CardId c : s.battlefield) {
            if (s.objects[c].controller == p && def_id(c) == d) return c;
        }
        return std::nullopt;
    }
    int count_in_hand(CardDefId d) const {
        return static_cast<int>(std::count_if(mine().hand.begin(), mine().hand.end(), [&](CardId c) { return def_id(c) == d; }));
    }
    bool in_hand(CardDefId d) const { return count_in_hand(d) > 0; }
    int pw(CardId c) const { return power(s, db, c); }
    int tough(CardId c) const { return toughness(s, db, c); }
    bool keyword(CardId c, Keyword k) const { return has_keyword(s, db, c, k); }
    bool attacking(CardId c) const {
        return std::any_of(s.combat.attackers.begin(), s.combat.attackers.end(),
                           [&](const Attacker& a) { return a.creature.card == c; });
    }
    bool blocking(CardId c) const {
        return std::any_of(s.combat.blocks.begin(), s.combat.blocks.end(), [&](const Block& b) { return b.blocker.card == c; });
    }
    bool is_token(CardId c) const { return s.objects[c].has_flag(ObjectFlag::Token); }

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
    // Terre stappate che producono mana; `except`: la terra che sta entrando (non è ancora "in più").
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
    bool can_afford(const ManaCost& cost) const { return affordable(untapped_mana(), cost); }

    // Sezione 14: l'avversario ha mostrato una carta nera o rossa (campo, cimitero, esilio, pila).
    bool shown_black_red() const {
        const ColorMask br = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
        for (const CardId c : s.battlefield) {
            if (s.objects[c].controller != opp) continue;
            if ((def(c).colors & br) != 0 || (is_land(c) && (mana_colors(def(c)) & br) != 0)) return true;
        }
        for (const CardId c : theirs().graveyard) {
            if ((db.get(def_id(c)).colors & br) != 0) return true;
        }
        for (const CardId c : s.exile) {
            if (s.objects[c].owner == opp && (db.get(def_id(c)).colors & br) != 0) return true;
        }
        return std::any_of(s.stack_items.begin(), s.stack_items.end(), [&](const StackItem& i) {
            return i.controller == opp && (db.get(i.source_def).colors & br) != 0;
        });
    }

    // Una nostra creatura lanciata con il warp, che andrà in esilio alla prossima sottofase finale.
    bool warped(CardId c) const {
        return std::any_of(s.delayed.begin(), s.delayed.end(), [&](const DelayedTrigger& d) {
            return d.effect == StackEffect::WarpExile && s.is_valid(d.object) && d.object.card == c;
        });
    }

    // --- Azioni legali ----------------------------------------------------------------------

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
    const Action* cast(CardDefId d, CastMethod m = CastMethod::Normal, Zone zone = Zone::Hand) const {
        return find(ActionType::CastSpell, d, static_cast<std::uint8_t>(m), zone);
    }
    Choice pass(std::string_view rule) const {
        if (const Action* a = find_type(ActionType::Pass)) return {*a, rule};
        return fallback();
    }
    Choice fallback() const {
        if (const Action* a = find_type(ActionType::Pass)) return {*a, UwAgent::kDefaultRule};
        return {legal.front(), UwAgent::kDefaultRule};
    }
    // Lancia con un piano: le scelte successive (bersaglio, modo, X, carta da esiliare) lo seguono.
    std::optional<Choice> cast_with(const Action* a, Plan plan, std::string_view rule) {
        if (a == nullptr) return std::nullopt;
        plan.def = def_id(a->card);
        agent.plan_ = plan;
        return Choice{*a, rule};
    }
    Plan plan_object(CardId c) const {
        Plan p;
        p.target = Target{TargetKindTag::Object, kNoPlayer, s.ref(c)};
        return p;
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

    // --- Sezione 1: soglia di vita ----------------------------------------------------------

    int their_power() const {
        int n = 0;
        for (const CardId c : creatures(opp)) n += std::max(0, pw(c));
        return n;
    }
    // Si può pagare `n` vita per scelta? Nei primi tre turni sì (la curva); dopo, la soglia (D-16).
    bool can_pay_life(int n) const {
        const int after = life() - n;
        if (after <= 0) return false;
        if (my_turn_index() <= 3) return true;
        return after >= kLifeFloor && after > their_power() + kBoltDamage;
    }

    // --- Sezione 6: quale minaccia togliere ---------------------------------------------

    bool phoenix_on(CardId c) const { return bestowed_aura_count(s, c) > 0; }
    // Posizione nella classifica (1 = la più importante), 0 = non è una minaccia.
    int threat_rank(CardId c) const {
        if (s.objects[c].controller != opp || !is_creature(c)) return 0;
        const CardDefId d = def_id(c);
        if (d == id.supplier) return 0;  // D-6: si tratta come una magia
        if (d == id.fomo && theirs().gy.delirium()) return 1;
        if (phoenix_on(c)) return 2;
        if (d == id.nethergoyf || d == id.moonshadow || d == id.manipulator) return 3;
        if (d == id.troll || d == id.wraith) return 4;
        return 5;
    }
    // Più alto = più importante: prima la classifica, poi la forza, poi quanto può crescere.
    int threat_score(CardId c) const {
        const int r = threat_rank(c);
        if (r == 0) return 0;
        int growth = 0;
        if (def_id(c) == id.moonshadow) growth = s.objects[c].counters.minus_one;
        if (def_id(c) == id.nethergoyf) growth = std::max(0, 6 - theirs().gy.distinct_types());
        return (6 - r) * 10000 + std::max(0, pw(c)) * 100 + growth;
    }
    template <typename Pred>
    std::optional<CardId> best_threat(Pred ok) const {
        std::optional<CardId> best;
        for (const CardId c : creatures(opp)) {
            if (threat_rank(c) == 0 || !ok(c)) continue;
            if (!best || threat_score(c) > threat_score(*best)) best = c;
        }
        return best;
    }
    std::optional<CardId> best_threat() const {
        return best_threat([](CardId) { return true; });
    }

    // --- Sezione 22: valore delle carte -------------------------------------------------------

    int hand_value(CardId c) const {
        const CardDefId d = def_id(c);
        if (is_land(c) || d == id.witch) {
            const int lands = lands_in_play() + lands_in_hand();
            if (is_land(c) && lands <= 3) return 200;
            if (d == id.witch) return lands < 4 ? 150 : (in_hand(id.solitude) ? 45 : 10);
            return lands <= 5 ? 30 : 0;
        }
        if (d == id.solitude) return 160;
        if (d == id.riddler) return 150;
        if (d == id.sanctifier) return 140;
        if (d == id.march) return 130;
        if (d == id.thraben) return 120;
        if (d == id.seam_rip) return 110;
        if (d == id.guide) return 100;
        if (d == id.ocelot) return 95;
        if (d == id.ephemerate) return in_hand(id.solitude) ? 92 : 85;  // D-14: sopra Phelia con Solitude
        if (d == id.phelia) return 90;
        if (d == id.teferi) return 80;
        if (d == id.recruiter) return 70;
        if (d == id.shepherd) return 65;
        if (d == id.consign) return 60;
        if (d == id.force) return 55;
        return 50;
    }
    // In campo: più alto = più prezioso (sezione 22).
    int field_value(CardId c) const {
        const CardDefId d = def_id(c);
        if (warped(c)) return 1;
        if (is_token(c)) return 2;
        if (d == id.shepherd || d == id.recruiter) return 3;
        if (d == id.witch) return 4;
        if (d == id.solitude) return 5;
        if (d == id.riddler) return 6;
        if (d == id.teferi) return 50;
        return 10 + std::max(0, pw(c)) + tough(c);
    }

    // --- Sezione 2: mulligan ----------------------------------------------------------------

    bool counts_as_land(CardId c) const { return is_land(c) || def_id(c) == id.witch; }
    bool is_answer(CardId c) const {
        const CardDefId d = def_id(c);
        return d == id.solitude || d == id.march || d == id.sanctifier || d == id.seam_rip || d == id.teferi;
    }
    bool is_one_drop(CardId c) const { return def(c).has_type(CardType::Creature) && def(c).mana_value == 1; }
    bool is_early_play(CardId c) const {
        const CardDefId d = def_id(c);
        if (d == id.guide || d == id.ocelot || d == id.phelia || d == id.sanctifier || d == id.seam_rip) return true;
        return d == id.shepherd;  // il warp di Starfield Shepherd; mai quello di Quantum Riddler (D-8)
    }

    Choice mulligan() {
        const Action* keep = find_type(ActionType::Keep);
        const Action* mull = find_type(ActionType::Mulligan);
        const int keep_size = 7 - mine().mulligans;
        if (keep_size <= 5 || mull == nullptr) return {*keep, "mulligan: 5 carte o meno, tiene"};
        std::vector<CardId> hand(mine().hand.begin(), mine().hand.end());
        hand = after_bottom(hand, mine().mulligans);
        const int lands = static_cast<int>(std::count_if(hand.begin(), hand.end(), [&](CardId c) { return counts_as_land(c); }));
        const bool pair = std::any_of(hand.begin(), hand.end(), [&](CardId c) { return def_id(c) == id.solitude; }) &&
                          std::any_of(hand.begin(), hand.end(), [&](CardId c) { return def_id(c) == id.ephemerate; });
        // Tutte le terre della lista danno {W} (le fetch cercano Plains): basta la coppia.
        const bool one_land_pair = lands == 1 && pair;
        if (lands == 0) return {*mull, "mulligan: nessuna terra"};
        if (lands > 4) return {*mull, "mulligan: piu' di quattro terre"};
        if (lands == 1 && !one_land_pair) return {*mull, "mulligan: una terra senza Solitude ed Ephemerate"};
        const bool play = std::any_of(hand.begin(), hand.end(), [&](CardId c) { return is_early_play(c); }) || pair;
        const bool answer = std::any_of(hand.begin(), hand.end(), [&](CardId c) { return is_answer(c); });
        const int one_drops = static_cast<int>(std::count_if(hand.begin(), hand.end(), [&](CardId c) { return is_one_drop(c); }));
        const bool on_play = s.turn.starting_player == me;
        if (keep_size == 7) {
            if (!play) return {*mull, "mulligan: nessuna giocata entro il secondo turno"};
            if (!answer && !(on_play && one_drops >= 3)) return {*mull, "mulligan: nessuna risposta"};
        } else if (!play && !answer) {
            return {*mull, "mulligan: 6 carte senza giocate ne' risposte"};
        }
        return {*keep, lands == 1 ? "mulligan: una terra con Solitude ed Ephemerate, tiene" : "mulligan: tiene"};
    }

    // Quanto conviene mettere `c` in fondo, tra le carte `remaining` (più alto = prima in fondo).
    int bottom_score(CardId c, const std::vector<CardId>& remaining) const {
        const auto lands = std::count_if(remaining.begin(), remaining.end(), [&](CardId x) { return counts_as_land(x); });
        if (lands > 3 && counts_as_land(c)) {
            const CardDefId d = def_id(c);
            if (d == id.witch) return 1;  // si tiene per prima: carta bianca per Solitude
            if (d == id.fountain) return 2;
            if (def(c).rules.land.fetch_types != 0) return 3;
            if (d == id.plains) return 4;
            return 5;  // Meticulous Archive per ultima
        }
        return -hand_value(c);
    }
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
    Choice bottom() {
        std::vector<CardId> remaining;
        for (const CardId c : mine().hand) {
            if (!s.pending.chosen.contains(c)) remaining.push_back(c);
        }
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i =
            best_index(options.size(), [&](std::size_t k) { return bottom_score(options[k]->card, remaining); });
        return {*options[i], "carte in fondo: terre oltre la terza, poi valore piu' basso"};
    }

    // --- Sezione 3: terre ------------------------------------------------------------------

    // Una giocata in mano che diventa pagabile con una terra in più (in questo turno o nel prossimo).
    bool land_needed_for_curve() const {
        const int lands = lands_in_play();
        return std::any_of(mine().hand.begin(), mine().hand.end(), [&](CardId c) {
            if (is_land(c) || def_id(c) == id.witch) return false;
            const int mv = def(c).mana_value;
            return mv > lands && mv <= lands + 2;
        });
    }
    // Costi delle giocate previste nella nostra fase principale (sezione 4): le istantanee e le
    // creature con lampo, che si giocano nel turno avversario, non contano.
    std::vector<ManaCost> planned_costs() const {
        std::vector<ManaCost> out;
        if (!my_turn() || !is_main(s.turn.step)) return out;
        for (const CardId c : mine().hand) {
            const CardDefId d = def_id(c);
            if (d == id.sanctifier && !shown_black_red()) continue;
            if (d == id.seam_rip && !seam_rip_target()) continue;
            if (d == id.witch && !witch_target()) continue;
            if (d == id.sanctifier || d == id.teferi || d == id.seam_rip || d == id.guide || d == id.ocelot ||
                d == id.recruiter || d == id.witch || d == id.shepherd || d == id.riddler) {
                out.push_back(def(c).cost);
            }
            if (d == id.shepherd) out.push_back(def(c).rules.warp.cost);
            if (d == id.riddler && my_turn_index() != 2 && mine().hand.size() <= 2) out.push_back(def(c).rules.warp.cost);
        }
        return out;
    }
    // Una giocata prevista pagabile solo con il mana di questa terra in più (`entering`: la terra stessa).
    bool extra_land_helps(ColorMask colors, CardId entering = kNoCard) const {
        const std::vector<ColorMask> base = untapped_mana(entering);
        std::vector<ColorMask> more = base;
        more.push_back(colors);
        const auto costs = planned_costs();
        return std::any_of(costs.begin(), costs.end(), [&](const ManaCost& cost) {
            return !affordable(base, cost) && affordable(more, cost);
        });
    }
    // Una giocata prevista chiede il blu e le terre stappate non lo danno.
    bool blue_needed_now() const {
        bool has_blue = false;
        for (const ColorMask m : untapped_mana()) has_blue = has_blue || (m & color_bit(Color::Blue)) != 0;
        if (has_blue) return false;
        const auto costs = planned_costs();
        return std::any_of(costs.begin(), costs.end(),
                           [](const ManaCost& c) { return c.colored[static_cast<std::size_t>(Color::Blue)] > 0; });
    }

    std::optional<Choice> play_land() {
        std::vector<const Action*> fronts;
        const Action* meadow = nullptr;
        for (const Action& a : legal) {
            if (a.type != ActionType::PlayLand) continue;
            if (a.value == 0) fronts.push_back(&a);
            if (a.value == 1) meadow = &a;
        }
        const ColorMask wu = static_cast<ColorMask>(color_bit(Color::White) | color_bit(Color::Blue));
        for (const Action* a : fronts) {
            if (def_id(a->card) == id.archive && !extra_land_helps(wu)) return Choice{*a, "terra: Meticulous Archive (mana non necessario)"};
        }
        for (const Action* a : fronts) {
            if (def(a->card).rules.land.fetch_types != 0) return Choice{*a, "terra: fetch"};
        }
        for (const CardDefId d : {id.plains, id.fountain, id.archive}) {
            for (const Action* a : fronts) {
                if (def_id(a->card) == d) return Choice{*a, d == id.plains ? "terra: Plains" : "terra: Hallowed Fountain o Archive"};
            }
        }
        if (!fronts.empty()) return Choice{*fronts.front(), "terra: l'unica disponibile"};
        if (meadow != nullptr && land_needed_for_curve()) return Choice{*meadow, "terra: Witch-Blessed Meadow (serve alla curva)"};
        return std::nullopt;
    }
    std::optional<Choice> crack_fetch() {
        for (const Action& a : legal) {
            if (a.type != ActionType::ActivateAbility || s.objects[a.card].zone != Zone::Battlefield) continue;
            const LandRules& r = def(a.card).rules.land;
            if (r.fetch_types != 0 && can_pay_life(r.fetch_life)) return Choice{a, "fetch: rotta subito"};
        }
        return std::nullopt;
    }

    Choice pay_life() {
        const Action* pay = find_type(ActionType::ChooseOption, kPayLife);
        const Action* tapped = find_type(ActionType::ChooseOption, kEnterTapped);
        const CardId land = s.pending.subject.card;
        if (pay == nullptr || !can_pay_life(s.pending.amount)) return {*tapped, "terra che costa vita: tappata (soglia di vita)"};
        if (my_turn() && extra_land_helps(mana_colors(def(land)), land)) {
            return {*pay, "terra che costa vita: paga (mana necessario ora)"};
        }
        return {*tapped, "terra che costa vita: tappata (mana non necessario)"};
    }

    // --- Ricerche (sezioni 3 e 15) -----------------------------------------------------------

    Choice search() {
        std::vector<const Action*> cards;
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) cards.push_back(&a);
        }
        auto pick_def = [&](CardDefId d) -> const Action* {
            for (const Action* a : cards) {
                if (def_id(a->card) == d) return a;
            }
            return nullptr;
        };
        const Action* decline = find_type(ActionType::ChooseOption, kDeclineSearch);
        if (cards.empty()) return {*decline, "ricerca: niente da trovare"};
        if (s.pending.destination == Zone::Battlefield) {  // fetch
            if (blue_needed_now()) {
                if (const Action* a = pick_def(id.fountain)) return {*a, "fetch: Hallowed Fountain (serve il blu)"};
            }
            for (const CardDefId d : {id.plains, id.fountain, id.archive}) {
                if (const Action* a = pick_def(d)) return {*a, "fetch: Plains, poi Hallowed Fountain, poi Archive"};
            }
            return {*cards.front(), "fetch: l'unica terra"};
        }
        const bool recruiter = s.pending.search.max_toughness > 0;
        const int lands = lands_in_play() + lands_in_hand();
        if (recruiter) {
            if (!on_field(me, id.sanctifier) && !in_hand(id.sanctifier) && shown_black_red()) {
                if (const Action* a = pick_def(id.sanctifier)) return {*a, "Recruiter: Sanctifier en-Vec"};
            }
            if (!in_hand(id.solitude) && best_threat()) {
                if (const Action* a = pick_def(id.solitude)) return {*a, "Recruiter: Solitude"};
            }
            if (lands < 4) {
                if (const Action* a = pick_def(id.witch)) return {*a, "Recruiter: Witch Enchanter (serve una terra)"};
            }
            for (const CardDefId d : {id.guide, id.ocelot}) {
                if (!on_field(me, d)) {
                    if (const Action* a = pick_def(d)) return {*a, "Recruiter: Guide of Souls o Ocelot Pride"};
                }
            }
            for (const CardDefId d : {id.phelia, id.guide, id.ocelot, id.solitude, id.sanctifier, id.shepherd, id.witch}) {
                if (const Action* a = pick_def(d)) return {*a, "Recruiter: la prima della lista"};
            }
            return {*cards.front(), "Recruiter: l'unica creatura"};
        }
        if (lands < 4) {
            if (const Action* a = pick_def(id.plains)) return {*a, "Starfield Shepherd: Plains (servono terre)"};
        }
        if (!on_field(me, id.guide)) {
            if (const Action* a = pick_def(id.guide)) return {*a, "Starfield Shepherd: Guide of Souls"};
        }
        for (const CardDefId d : {id.ocelot, id.guide, id.plains}) {
            if (const Action* a = pick_def(d)) return {*a, "Starfield Shepherd: Ocelot Pride, poi il resto"};
        }
        return {*cards.front(), "Starfield Shepherd: l'unica carta"};
    }

    // --- Sezione 5: mana aperto ------------------------------------------------------------

    // Thraben Charm ha un uso previsto (sezione 11, modi 1-4).
    bool thraben_useful() const {
        const int types = theirs().gy.distinct_types();
        const bool fomo = on_field(opp, id.fomo).has_value();
        const bool goyf = on_field(opp, id.nethergoyf).has_value();
        if (fomo && types >= 3) return true;
        if (goyf && types >= 3) return true;
        return thraben_damage_target().has_value();
    }
    std::optional<CardId> thraben_damage_target() const {
        const int damage = 2 * static_cast<int>(creatures(me).size());
        return best_threat([&](CardId c) { return tough(c) - s.objects[c].damage <= damage; });
    }
    std::optional<CardId> march_target(int max_mv) const {
        return best_threat([&](CardId c) { return static_cast<int>(def(c).mana_value) <= max_mv; });
    }
    // Il mana da tenere aperto per l'istantanea con la condizione vera (una sola, D-3).
    ManaCost reserve() const {
        if (in_hand(id.thraben) && thraben_useful()) return mana(1, 1);
        if (in_hand(id.ephemerate)) {
            const bool warp_on_field = std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) { return warped(c); });
            if (warp_on_field || (in_hand(id.solitude) && best_threat())) return mana(0, 1);
        }
        if (in_hand(id.march)) {
            if (const auto t = march_target(lands_in_play() - 1)) return mana(def(*t).mana_value, 1);
        }
        return ManaCost{};
    }
    // Una giocata della fase principale che non toglie il mana tenuto aperto.
    bool fits(const ManaCost& cost) const { return can_afford(cost + reserve()); }

    // --- Sezione 4: ordine delle giocate nel nostro turno ----------------------------------

    std::optional<Choice> main_phase(bool main1) {
        if (auto c = play_land()) return c;
        if (auto c = crack_fetch()) return c;
        if (shown_black_red() && !on_field(me, id.sanctifier)) {
            if (const Action* a = cast(id.sanctifier); a && fits(def(a->card).cost)) {
                return Choice{*a, "Sanctifier en-Vec: l'avversario ha mostrato il nero o il rosso"};
            }
        }
        if (main1) {
            if (const Action* a = cast(id.teferi); a && fits(def(a->card).cost)) {
                return Choice{*a, "Teferi: prima del combattimento"};
            }
        }
        if (auto c = teferi_ability()) return c;
        if (const Action* a = cast(id.seam_rip); a && fits(def(a->card).cost) && seam_rip_target()) {
            return Choice{*a, "Seam Rip: su una minaccia"};
        }
        // Ephemerate su una creatura con il warp, prima della sottofase finale (sezione 8).
        for (const CardId c : creatures(me)) {
            if (!warped(c)) continue;
            if (auto ch = cast_with(cast(id.ephemerate), plan_object(c), "Ephemerate: salva la creatura con il warp")) return ch;
        }
        for (const CardDefId d : {id.guide, id.ocelot}) {
            if (const Action* a = cast(d); a && fits(def(a->card).cost)) {
                return Choice{*a, d == id.guide ? "creatura da un mana: Guide of Souls" : "creatura da un mana: Ocelot Pride"};
            }
        }
        if (auto c = warp_plays(main1)) return c;
        if (const Action* a = cast(id.recruiter); a && fits(def(a->card).cost)) return Choice{*a, "Recruiter of the Guard"};
        if (const Action* a = cast(id.witch); a && fits(def(a->card).cost) && witch_target()) {
            return Choice{*a, "Witch Enchanter: con un bersaglio che non da' il delirium"};
        }
        if (const Action* a = cast(id.shepherd); a && fits(def(a->card).cost)) return Choice{*a, "Starfield Shepherd: costo pieno"};
        if (const Action* a = cast(id.riddler); a && fits(def(a->card).cost)) return Choice{*a, "Quantum Riddler: costo pieno"};
        for (const CardDefId d : {id.riddler, id.shepherd}) {  // dall'esilio, a costo pieno
            if (const Action* a = cast(d, CastMethod::Normal, Zone::Exile); a && fits(def(a->card).cost)) {
                return Choice{*a, "warp: rilancio dall'esilio a costo pieno"};
            }
        }
        return std::nullopt;
    }

    // Sezione 16: Starfield Shepherd con il warp; Quantum Riddler con il warp solo come ricarica.
    std::optional<Choice> warp_plays(bool main1) {
        const bool keep_plan = (in_hand(id.ephemerate) && can_afford(mana(1, 2))) ||
                               (on_field(me, id.phelia) && !s.objects[*on_field(me, id.phelia)].has_flag(ObjectFlag::SummoningSick));
        if (main1 && !keep_plan) return std::nullopt;  // senza un piano per tenerla: seconda fase principale
        if (const Action* a = cast(id.shepherd, CastMethod::Warp); a && fits(mana(1, 1))) {
            return Choice{*a, keep_plan ? "Starfield Shepherd: warp con un piano per tenerla" : "Starfield Shepherd: warp"};
        }
        const bool refill = my_turn_index() != 2 && mine().hand.size() <= 2;  // dopo il lancio: una carta o nessuna
        if (const Action* a = cast(id.riddler, CastMethod::Warp); a && refill && fits(mana(1, 0, 1))) {
            return Choice{*a, "Quantum Riddler: warp come ricarica"};
        }
        return std::nullopt;
    }

    // Witch Enchanter (sezione 13): un bersaglio che vale la carta e non dà il delirium.
    std::optional<CardId> witch_target() const {
        const bool sanctifier = on_field(me, id.sanctifier).has_value();
        const PlayerState& o = theirs();
        auto safe = [&](CardId c) {
            if (sanctifier || o.gy.delirium()) return true;
            const TypeMask after = static_cast<TypeMask>(o.gy.present | db.get(def_id(c)).types);
            return std::popcount(static_cast<unsigned>(after)) < 4;
        };
        std::optional<CardId> best;
        int best_score = 0;
        for (const CardId c : s.battlefield) {
            if (s.objects[c].controller != opp || !safe(c)) continue;
            const CardDefId d = def_id(c);
            int score = 0;
            if (d == id.fomo) score = 3;
            if (d == id.phoenix && s.objects[c].has_flag(ObjectFlag::Bestowed)) score = 2;
            if (d == id.phoenix && !s.objects[c].has_flag(ObjectFlag::Bestowed)) score = 1;
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        return best;
    }
    std::optional<CardId> seam_rip_target() const {
        return best_threat([&](CardId c) { return def(c).mana_value <= 2 && (threat_rank(c) <= 3 || def_id(c) == id.fomo); });
    }

    // --- Sezione 17: Teferi ----------------------------------------------------------------

    std::optional<Choice> teferi_ability() {
        const auto teferi = on_field(me, id.teferi);
        if (!teferi) return std::nullopt;
        const Action* minus = find_card(ActionType::ActivateAbility, *teferi);
        const Action* plus = nullptr;
        minus = nullptr;
        for (const Action& a : legal) {
            if (a.type != ActionType::ActivateAbility || a.card != *teferi) continue;
            if (a.value == 0) plus = &a;
            if (a.value == 1) minus = &a;
        }
        if (minus != nullptr) {
            if (const auto t = teferi_target()) return cast_with(minus, plan_object(*t), "Teferi: -3 su un bersaglio ragionevole");
        }
        if (plus != nullptr) return Choice{*plus, "Teferi: +1"};
        return std::nullopt;
    }
    std::optional<CardId> teferi_target() const {
        for (const CardId c : creatures(opp)) {
            if (def_id(c) == id.moonshadow && s.objects[c].counters.minus_one <= 3) return c;
        }
        for (const CardId c : creatures(opp)) {
            if (phoenix_on(c)) return c;
        }
        if (const auto sol = on_field(me, id.solitude); sol && best_threat()) return *sol;
        return std::nullopt;
    }

    // --- Risposte in pila (sezioni 8, 11, 18, 19) ----------------------------------------------

    int stack_index_of(std::uint16_t stack_id) const {
        for (std::size_t i = 0; i < s.stack_items.size(); ++i) {
            if (s.stack_items[i].id == stack_id) return static_cast<int>(i);
        }
        return -1;
    }
    Plan plan_stack(const StackItem& item) const {
        Plan p;
        p.stack_id = item.id;
        return p;
    }
    Plan plan_graveyard() const {
        Plan p;
        p.mode = 2;
        p.target = Target{TargetKindTag::Player, opp, ObjRef{}};
        return p;
    }
    std::optional<Choice> thraben_graveyard(std::string_view rule) {
        if (theirs().graveyard.empty()) return std::nullopt;
        return cast_with(cast(id.thraben), plan_graveyard(), rule);
    }
    std::optional<Choice> consign(const StackItem& item, std::string_view rule) {
        return cast_with(cast(id.consign), plan_stack(item), rule);
    }
    // Force of Negation: gratis nel turno avversario, altrimenti pagata (sezione 18).
    std::optional<Choice> force(const StackItem& item, std::string_view rule) {
        if (const Action* a = cast(id.force, CastMethod::Pitch)) return cast_with(a, plan_stack(item), rule);
        return cast_with(cast(id.force), plan_stack(item), rule);
    }
    CardId best_own_creature() const {
        CardId best = kNoCard;
        for (const CardId c : creatures(me)) {
            if (best == kNoCard || field_value(c) > field_value(best)) best = c;
        }
        return best;
    }

    std::optional<Choice> respond() {
        const StackItem& top = s.stack_items.back();
        if (top.copy) return std::nullopt;
        if (top.controller == opp && top.kind == StackKind::Spell) {
            if (top.source_def == id.flare && (!creatures(me).empty() || on_field(me, id.teferi))) {
                if (auto c = force(top, "Force of Negation: contrasta Flare of Malice")) return c;
            }
            if (top.source_def == id.bolt) {
                if (top.target.tag == TargetKindTag::Object && s.is_valid(top.target.object)) {
                    const CardId t = top.target.object.card;
                    if (s.objects[t].controller == me && is_creature(t) && tough(t) - s.objects[t].damage <= kBoltDamage) {
                        if (auto c = cast_with(cast(id.ephemerate), plan_object(t), "Ephemerate: salva una creatura da Lightning Bolt")) {
                            return c;
                        }
                        if (t == best_own_creature()) {
                            if (auto c = force(top, "Force of Negation: Bolt sulla nostra creatura migliore")) return c;
                        }
                    }
                }
                if (top.target.tag == TargetKindTag::Player && top.target.player == me && life() <= kBoltDamage) {
                    if (auto c = force(top, "Force of Negation: Bolt letale")) return c;
                }
            }
            return std::nullopt;
        }
        if (top.controller == opp && top.kind == StackKind::TriggeredAbility) {
            if (top.effect == StackEffect::UntapAndExtraCombat && theirs().gy.delirium()) {
                if (auto c = thraben_graveyard("Thraben Charm: esilia il cimitero in risposta al trigger di FOMO")) return c;
                if (auto c = consign(top, "Consign to Memory: il trigger di FOMO con il delirium")) return c;
            }
            return std::nullopt;
        }
        if (top.controller == me && top.kind == StackKind::TriggeredAbility) {
            if (top.effect == StackEffect::EvokeSacrifice && s.is_valid(top.source)) {
                if (auto c = cast_with(cast(id.ephemerate), plan_object(top.source.card), "Ephemerate: Solitude evocata, in risposta al sacrificio")) {
                    return c;
                }
                if (auto c = consign(top, "Consign to Memory: il sacrificio di Solitude evocata")) return c;
            }
            if (top.effect == StackEffect::WarpExile && s.is_valid(top.source) &&
                s.objects[top.source.card].zone == Zone::Battlefield) {
                if (auto c = cast_with(cast(id.ephemerate), plan_object(top.source.card), "Ephemerate: salva la creatura con il warp")) {
                    return c;
                }
                if (auto c = consign(top, "Consign to Memory: l'esilio del warp")) return c;
            }
            if (top.effect == StackEffect::PheliaReturn && s.is_valid(top.target.object) &&
                s.objects[top.target.object.card].owner == opp) {
                if (auto c = consign(top, "Consign to Memory: il ritorno di Phelia, esilio per sempre")) return c;
            }
        }
        return std::nullopt;
    }

    // --- Rimozione nel turno avversario (sezioni 5, 7, 10) ---------------------------------

    // Mano bianca per l'evocazione: c'è una carta da esiliare che non sia Solitude stessa?
    bool can_evoke() const { return cast(id.solitude, CastMethod::Evoke) != nullptr; }

    // La risposta che costa meno carte: March con il mana, poi Solitude a costo pieno, poi evocata.
    std::optional<Choice> remove(std::optional<CardId> target, std::string_view rule_march, std::string_view rule_solitude) {
        if (!target) return std::nullopt;
        const int mv = def(*target).mana_value;
        if (const Action* a = cast(id.march); a && can_afford(mana(mv, 1))) {
            Plan p = plan_object(*target);
            p.x = static_cast<std::uint8_t>(mv);
            return cast_with(a, p, rule_march);
        }
        if (const Action* a = cast(id.solitude)) return cast_with(a, plan_object(*target), rule_solitude);
        if (const Action* a = cast(id.solitude, CastMethod::Evoke)) return cast_with(a, plan_object(*target), rule_solitude);
        return std::nullopt;
    }

    std::optional<Choice> opponent_turn() {
        const Step st = s.turn.step;
        if (st == Step::DeclareAttackers && !s.combat.attackers.empty()) {
            if (auto c = remove(best_threat([&](CardId c) { return attacking(c); }), "March: la minaccia che attacca",
                                "Solitude: la minaccia che attacca")) {
                return c;
            }
        }
        if (auto c = thraben_combat()) return c;
        if (st == Step::End) {
            if (auto c = remove(best_threat(), "March: sottofase finale avversaria", "Solitude: sottofase finale avversaria")) {
                return c;
            }
            if (in_hand(id.thraben) && theirs().gy.distinct_types() >= 3 &&
                (on_field(opp, id.fomo) || on_field(opp, id.nethergoyf) || graveyard_has(id.phoenix) || graveyard_has(id.nethergoyf))) {
                if (auto c = thraben_graveyard("Thraben Charm: esilia il cimitero alla fine del turno avversario")) return c;
            }
            if (!on_field(me, id.phelia)) {
                if (const Action* a = cast(id.phelia)) return Choice{*a, "Phelia: lampo alla fine del turno avversario"};
            }
            if (const auto c = ephemerate_value_target()) {
                if (auto ch = cast_with(cast(id.ephemerate), plan_object(*c), "Ephemerate: un'entrata utile alla fine del turno avversario")) {
                    return ch;
                }
            }
        }
        return std::nullopt;
    }
    bool graveyard_has(CardDefId d) const {
        return std::any_of(theirs().graveyard.begin(), theirs().graveyard.end(), [&](CardId c) { return def_id(c) == d; });
    }
    // Thraben Charm con Nethergoyf in combattimento (sezione 11, modo 2), in qualsiasi turno.
    std::optional<Choice> thraben_combat() {
        if (s.turn.step != Step::DeclareBlockers) return std::nullopt;
        for (const CardId c : creatures(opp)) {
            if (def_id(c) == id.nethergoyf && pw(c) >= 2 && (attacking(c) || blocking(c))) {
                return thraben_graveyard("Thraben Charm: Nethergoyf in combattimento");
            }
        }
        return std::nullopt;
    }

    // Sezione 8, punto 4: la nostra creatura con l'entrata più utile adesso (0 = nessuna).
    int etb_value(CardId c) const {
        const CardDefId d = def_id(c);
        if (d == id.solitude && best_threat()) return 6;
        if (d == id.riddler) return 5;
        if (d == id.recruiter) return 4;
        if (d == id.shepherd) return 4;
        if (d == id.witch && witch_target()) return 3;
        if (d == id.sanctifier) {
            const bool br = std::any_of(s.players[0].graveyard.begin(), s.players[0].graveyard.end(),
                                        [&](CardId x) { return (db.get(def_id(x)).colors & 0x0C) != 0; }) ||
                            std::any_of(s.players[1].graveyard.begin(), s.players[1].graveyard.end(),
                                        [&](CardId x) { return (db.get(def_id(x)).colors & 0x0C) != 0; });
            return br ? 2 : 0;
        }
        return 0;
    }
    // A parità di entrata, la creatura che dopo resta con costituzione 4 o più (sezione 1, D-17).
    std::optional<CardId> ephemerate_value_target(bool avoid_attackers = false) const {
        std::optional<CardId> best;
        int best_score = 0;
        for (const CardId c : creatures(me)) {
            const int v = etb_value(c);
            if (v == 0 || (avoid_attackers && would_attack(c))) continue;
            const int score = v * 10 + (db.get(def_id(c)).base_toughness >= 4 ? 1 : 0);
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        return best;
    }

    Choice priority() {
        if (!s.stack_items.empty()) {
            if (auto c = respond()) return *c;
            return pass("priorita': nessuna risposta");
        }
        if (my_turn() && is_main(s.turn.step)) {
            if (auto c = main_phase(s.turn.step == Step::Main1)) return *c;
            return pass("priorita': fine delle giocate della fase");
        }
        if (my_turn()) {
            if (auto c = thraben_combat()) return *c;
            return pass("priorita': niente da fare ora");
        }
        if (auto c = opponent_turn()) return *c;
        return pass("priorita': niente da fare ora");
    }

    // --- Bersagli -----------------------------------------------------------------------------

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
        for (const Action& a : legal) {  // creature identiche sono offerte una volta sola
            if (a.type == ActionType::TargetObject && def_id(a.card) == def_id(c) &&
                s.objects[a.card].controller == s.objects[c].controller) {
                return &a;
            }
        }
        return nullptr;
    }
    const Action* stack_target_action(std::uint16_t stack_id) const {
        const int i = stack_index_of(stack_id);
        if (i < 0) return nullptr;
        for (const Action& a : legal) {
            if (a.type == ActionType::TargetStackItem && a.value == i) return &a;
        }
        return nullptr;
    }
    Choice no_target(std::string_view rule) const {
        if (const Action* a = find_type(ActionType::ChooseOption, kNoTarget)) return {*a, rule};
        return first_legal_target(rule);
    }
    Choice first_legal_target(std::string_view rule) const { return {legal.front(), rule}; }

    Choice target() {
        if (s.pending.stack_index != 0xFF) {  // copia di Consign to Memory: si tiene il bersaglio
            if (const Action* a = find_type(ActionType::ChooseOption, kKeepTarget)) return {*a, "copia di Consign: tiene il bersaglio"};
            return {legal.front(), "copia di Consign: l'unico bersaglio"};
        }
        const StackItem& top = s.stack_items.back();
        const Plan& plan = agent.plan_;
        if (plan.def == top.source_def) {
            if (plan.stack_id != 0) {
                if (const Action* a = stack_target_action(plan.stack_id)) return {*a, "bersaglio: quello deciso al lancio"};
            } else if (plan.target.tag == TargetKindTag::Player && top.source_def == id.thraben) {
                for (const Action& a : legal) {
                    if (a.type == ActionType::ChooseOption && a.value == (1u << opp)) return {a, "bersaglio: quello deciso al lancio"};
                }
            } else if (plan.target.tag != TargetKindTag::None) {
                if (const Action* a = target_action(plan.target)) return {*a, "bersaglio: quello deciso al lancio"};
            }
        }
        switch (top.effect) {
            case StackEffect::ExileOtherCreature:
                if (const auto c = best_threat()) {
                    if (const Action* a = target_action(Target{TargetKindTag::Object, kNoPlayer, s.ref(*c)})) {
                        return {*a, "Solitude: la prima minaccia della classifica"};
                    }
                }
                return no_target("Solitude: nessuna minaccia da esiliare");
            case StackEffect::DestroyTarget:
                if (const auto c = witch_target()) {
                    if (const Action* a = target_action(Target{TargetKindTag::Object, kNoPlayer, s.ref(*c)})) {
                        return {*a, "Witch Enchanter: FOMO, poi la Phoenix"};
                    }
                }
                return first_legal_target("Witch Enchanter: l'unico bersaglio");
            case StackEffect::PheliaExile: return phelia_target();
            case StackEffect::ExileUntilLeaves:
                if (const auto c = seam_rip_target()) {
                    if (const Action* a = target_action(Target{TargetKindTag::Object, kNoPlayer, s.ref(*c)})) {
                        return {*a, "Seam Rip: la prima minaccia con valore di mana 2 o meno"};
                    }
                }
                return first_legal_target("Seam Rip: l'unico bersaglio");
            case StackEffect::AttackCounters: return guide_target();
            default: break;
        }
        if (top.source_def == id.ephemerate) {  // rimbalzo: la creatura con l'entrata più utile
            if (const auto c = ephemerate_value_target(racing())) {
                if (const Action* a = target_action(Target{TargetKindTag::Object, kNoPlayer, s.ref(*c)})) {
                    return {*a, "Ephemerate: la creatura con l'entrata piu' utile"};
                }
            }
            return first_legal_target("Ephemerate: l'unica creatura");
        }
        if (top.source_def == id.teferi) return no_target("Teferi -3: nessun bersaglio");
        return first_legal_target("bersaglio: l'unico possibile");
    }

    // Sezione 9: bersaglio dell'esilio di Phelia.
    Choice phelia_target() {
        const CardId phelia = s.is_valid(s.stack_items.back().source) ? s.stack_items.back().source.card : kNoCard;
        auto offer = [&](CardId c) { return target_action(Target{TargetKindTag::Object, kNoPlayer, s.ref(c)}); };
        for (const CardId c : creatures(me)) {
            if (c != phelia && warped(c)) {
                if (const Action* a = offer(c)) return {*a, "Phelia: la nostra creatura con il warp"};
            }
        }
        std::optional<CardId> best;
        int best_score = 0;
        for (const CardId c : creatures(me)) {
            if (c == phelia) continue;
            const int score = etb_value(c) * 10 + (db.get(def_id(c)).base_toughness >= 4 ? 1 : 0);
            if (etb_value(c) > 0 && score > best_score) {
                best_score = score;
                best = c;
            }
        }
        if (best) {
            if (const Action* a = offer(*best)) return {*a, "Phelia: la nostra creatura con l'entrata piu' utile"};
        }
        if (const auto t = on_field(me, id.teferi); t && s.objects[*t].counters.loyalty < 4) {
            if (const Action* a = offer(*t)) return {*a, "Phelia: Teferi con fedelta' sotto 4"};
        }
        for (const CardId c : creatures(opp)) {
            if (def_id(c) == id.moonshadow && s.objects[c].counters.minus_one <= 2) {
                if (const Action* a = offer(c)) return {*a, "Phelia: Moonshadow da far ripartire"};
            }
        }
        return no_target("Phelia: nessun bersaglio utile");
    }

    // Sezione 20: i segnalini di Guide of Souls.
    Choice guide_target() {
        std::vector<const Action*> options;
        for (const Action& a : legal) {
            if (a.type == ActionType::TargetObject) options.push_back(&a);
        }
        if (options.empty()) return first_legal_target("Guide of Souls: l'unico bersaglio");
        const auto blockers_fly = std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
            return s.objects[c].controller == opp && is_creature(c) && !s.objects[c].has_flag(ObjectFlag::Tapped) &&
                   (keyword(c, Keyword::Flying) || keyword(c, Keyword::Reach));
        });
        const std::size_t i = best_index(options.size(), [&](std::size_t k) {
            const CardId c = options[k]->card;
            const bool tough4 = tough(c) + 2 >= 4;
            const bool flies_better = !keyword(c, Keyword::Flying) && !blockers_fly && !potential_blockers(c).empty();
            return (tough4 ? 1000 : 0) + (flies_better ? 100 : 0) + std::max(0, pw(c));
        });
        return {*options[i], "Guide of Souls: costituzione 4, poi chi vola meglio"};
    }

    // --- Scelte del lancio: modo, X, carte da esiliare, replicare ---------------------------------

    Choice choose_mode() {
        const Plan& plan = agent.plan_;
        if (const Action* a = find_type(ActionType::ChooseOption, plan.mode)) return {*a, "Thraben Charm: il modo deciso al lancio"};
        return {legal.front(), "Thraben Charm: l'unico modo possibile"};
    }
    Choice choose_x() {
        const Plan& plan = agent.plan_;
        if (const Action* a = find_type(ActionType::ChooseOption, plan.x)) return {*a, "March: X pari al valore di mana del bersaglio"};
        // Il valore più piccolo offerto che arriva al bersaglio.
        std::size_t best = 0;
        for (std::size_t i = 0; i < legal.size(); ++i) {
            if (legal[i].value >= plan.x && (legal[best].value < plan.x || legal[i].value < legal[best].value)) best = i;
        }
        return {legal[best], "March: X pari al valore di mana del bersaglio"};
    }
    // Sezione 7: la carta bianca da esiliare per l'evocazione (anche Force of Negation: la carta blu).
    Choice exile_from_hand() {
        const Action* stop = find_type(ActionType::ChooseOption, kStopExiling);
        const StackItem& top = s.stack_items.back();
        if (stop != nullptr && top.source_def == id.march) return {*stop, "March: nessuna carta da esiliare (basta il mana)"};
        std::vector<const Action*> cards;
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) cards.push_back(&a);
        }
        if (cards.empty()) return stop != nullptr ? Choice{*stop, "esilio dalla mano: nessuna carta"} : Choice{legal.front(), "esilio dalla mano: l'unica scelta"};
        if (top.source_def == id.force) {  // D-13: Consign, poi Teferi, quasi mai Quantum Riddler
            const std::size_t i = best_index(cards.size(), [&](std::size_t k) {
                const CardDefId d = def_id(cards[k]->card);
                return d == id.consign ? 4 : d == id.teferi ? 3 : d == id.force ? 2 : d == id.riddler ? 0 : 1;
            });
            return {*cards[i], "Force of Negation: esilia Consign, poi Teferi, quasi mai Riddler"};
        }
        const bool trick = in_hand(id.ephemerate) && can_afford(mana(0, 1));
        const int other_lands = lands_in_play() + lands_in_hand();
        const int solitudes = count_in_hand(id.solitude) + static_cast<int>(std::count_if(
                                  s.battlefield.begin(), s.battlefield.end(), [&](CardId c) { return def_id(c) == id.solitude; }));
        const std::size_t i = best_index(cards.size(), [&](std::size_t k) {
            const CardDefId d = def_id(cards[k]->card);
            if (d == id.witch) return other_lands >= 4 ? 1000 : 0;  // D-1: salvo che serva come terra
            if (d == id.solitude) return solitudes >= 3 ? 900 : 1;
            if (d == id.ephemerate && trick) return -1000;
            if (d == id.recruiter) return 800;
            if (d == id.shepherd) return 790;
            return 500 - hand_value(cards[k]->card);
        });
        return {*cards[i], "Solitude evocata: Witch Enchanter, poi la carta bianca meno preziosa"};
    }
    Choice replicate() {
        const Action* none = find_type(ActionType::ChooseOption, 0);
        return {none != nullptr ? *none : legal.front(), "Consign: nessuna copia (un solo bersaglio di valore)"};
    }

    // --- Sezione 8: rimbalzo ------------------------------------------------------------

    // Il nostro orologio (sezione 8): turni per portare l'avversario a 0 con gli attaccanti del piano.
    bool racing() const {
        int damage = 0;
        for (const CardId c : creatures(me)) {
            if (would_attack(c)) damage += std::max(0, pw(c));
        }
        if (damage <= 0) return false;
        return (opp_life() + damage - 1) / damage <= agent.options_.race_clock;
    }
    Choice cast_from_exile() {
        const Action* yes = find_type(ActionType::ChooseOption, kCastIt);
        const Action* no = find_type(ActionType::ChooseOption, kDeclineCast);
        if (yes != nullptr && ephemerate_value_target(racing())) return {*yes, "rimbalzo: su una creatura con un'entrata rilevante"};
        return {*no, "rimbalzo: nessuna entrata utile, rifiutato"};
    }

    // --- Sezione 21: attacchi e blocchi --------------------------------------------------------

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
        const int n = keyword(attacker, Keyword::Menace) ? 2 : 1;
        return std::max(n, static_cast<int>(def(attacker).rules.combat.min_blockers));
    }
    std::pair<bool, bool> fight(CardId attacker, CardId blocker) const {
        const int to_blocker = combat::damage_after_prevention(s, db, blocker, def(attacker).colors, std::max(0, pw(attacker)));
        const int to_attacker = combat::damage_after_prevention(s, db, attacker, def(blocker).colors, std::max(0, pw(blocker)));
        bool blocker_dies = to_blocker > 0 && s.objects[blocker].damage + to_blocker >= tough(blocker);
        bool attacker_dies = to_attacker > 0 && s.objects[attacker].damage + to_attacker >= tough(attacker);
        const bool att_fs = keyword(attacker, Keyword::FirstStrike);
        const bool blk_fs = keyword(blocker, Keyword::FirstStrike);
        if (att_fs && !blk_fs && blocker_dies) attacker_dies = false;
        if (blk_fs && !att_fs && attacker_dies) blocker_dies = false;
        return {attacker_dies, blocker_dies};
    }
    std::vector<CardId> potential_blockers(CardId attacker) const {
        std::vector<CardId> out;
        const PlayerId defender = other(s.objects[attacker].controller);
        for (const CardId c : creatures(defender)) {
            if (!s.objects[c].has_flag(ObjectFlag::Tapped) && can_block(c, attacker)) out.push_back(c);
        }
        return out;
    }
    // Le regole d'attacco della sezione 21, su una nostra creatura.
    bool would_attack(CardId att) const {
        if (is_token(att) && def_id(att) == id.cat) return false;  // D-18: i gatti restano a bloccare
        const auto blockers = potential_blockers(att);
        if (static_cast<int>(blockers.size()) < blockers_needed(att)) return true;
        const bool good_block = blockers_needed(att) == 1 && std::any_of(blockers.begin(), blockers.end(), [&](CardId b) {
                                    const auto [att_dies, blk_dies] = fight(att, b);
                                    return att_dies && !blk_dies;
                                });
        return !good_block;
    }
    Choice attack() {
        const CardId att = s.pending.subject.card;
        const Action* yes = find_type(ActionType::TargetPlayer);
        const Action* no = find_type(ActionType::ChooseOption, kNoAttack);
        auto attack_player = [&](std::string_view rule) { return yes != nullptr ? Choice{*yes, rule} : Choice{legal.front(), rule}; };
        auto stay = [&](std::string_view rule) { return no != nullptr ? Choice{*no, rule} : Choice{legal.front(), rule}; };
        if (lethal_attack()) return attack_player("attacco: attacco letale");
        if (is_token(att) && def_id(att) == id.cat) return stay("attacco: i gatti restano a bloccare");
        // Soglia di vita: resta la creatura che blocca meglio, se il prossimo attacco avversario ci porta sotto.
        if (their_power() + kBoltDamage >= life()) {
            const auto ours = creatures(me);
            const auto best = std::max_element(ours.begin(), ours.end(), [&](CardId a, CardId b) {
                return std::pair{tough(a), pw(a)} < std::pair{tough(b), pw(b)};
            });
            if (best != ours.end() && *best == att) return stay("attacco: resta a bloccare (soglia di vita)");
        }
        if (s.objects[att].counters.keywords[static_cast<std::size_t>(KeywordCounter::Flying)] > 0) {
            return attack_player("attacco: volante potenziata da Guide of Souls");
        }
        const auto blockers = potential_blockers(att);
        if (static_cast<int>(blockers.size()) < blockers_needed(att)) return attack_player("attacco: non puo' essere bloccata");
        if (def_id(att) == id.phelia && phelia_has_target()) {
            const bool dies = std::any_of(blockers.begin(), blockers.end(), [&](CardId b) { return fight(att, b).first; });
            if (!dies) return attack_player("attacco: Phelia con un bersaglio utile");
        }
        if (would_attack(att)) return attack_player("attacco: nessun blocco conviene all'avversario");
        return stay("attacco: non conviene");
    }
    // Sezione 21, regola 3: la forza che passa anche se ogni bloccante stappato ferma un attaccante
    // (i più forti) basta a portare l'avversario a 0. Contano anche i gatti.
    bool lethal_attack() const {
        std::vector<int> powers;
        for (const CardId c : creatures(me)) {
            const CardObject& o = s.objects[c];
            if (!o.has_flag(ObjectFlag::Tapped) && (!o.has_flag(ObjectFlag::SummoningSick) || keyword(c, Keyword::Haste))) {
                powers.push_back(std::max(0, pw(c)));
            }
        }
        std::sort(powers.begin(), powers.end(), std::greater<>());
        std::size_t blockers = 0;
        for (const CardId c : creatures(opp)) blockers += s.objects[c].has_flag(ObjectFlag::Tapped) ? 0 : 1;
        int through = 0;
        for (std::size_t i = std::min(powers.size(), blockers); i < powers.size(); ++i) through += powers[i];
        return through >= opp_life();
    }
    bool phelia_has_target() const {
        for (const CardId c : creatures(me)) {
            if (def_id(c) != id.phelia && (warped(c) || etb_value(c) > 0)) return true;
        }
        if (const auto t = on_field(me, id.teferi); t && s.objects[*t].counters.loyalty < 4) return true;
        return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
            return s.objects[c].controller == opp && def_id(c) == id.moonshadow && s.objects[c].counters.minus_one <= 2;
        });
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
            return {*least_valuable(adds), "blocco: bloccante in piu' obbligato"};
        }
        if (adds.empty()) return {*done, "blocco: nessun bloccante"};
        if (blockers_needed(att) == 1) {
            std::vector<const Action*> wins, trades;
            for (const Action* a : adds) {
                const auto [att_dies, blk_dies] = fight(att, a->card);
                if (att_dies && !blk_dies) wins.push_back(a);
                if (att_dies && blk_dies && threat_rank(att) != 0 && threat_rank(att) <= 3 && field_value(a->card) < 50) {
                    trades.push_back(a);
                }
            }
            if (!wins.empty()) return {*least_valuable(wins), "blocco: uccide e sopravvive"};
            if (!trades.empty()) return {*least_valuable(trades), "blocco: scambio con una minaccia che cresce"};
        }
        if (threat_rank(att) != 0 && threat_rank(att) <= 3) {
            for (const Action* a : adds) {
                for (const Action* b : adds) {
                    if (a == b) continue;
                    const int damage = std::max(0, pw(a->card)) + std::max(0, pw(b->card));
                    if (damage < tough(att) - s.objects[att].damage) continue;
                    const CardId cheap = field_value(a->card) <= field_value(b->card) ? a->card : b->card;
                    const CardId dear = cheap == a->card ? b->card : a->card;
                    if (fight(att, dear).second) continue;
                    agent.block_attacker_ = att;
                    agent.block_second_ = b->card;
                    return {*a, "blocco: in due su una minaccia che cresce"};
                }
            }
        }
        int incoming = 0;
        for (const Attacker& at : s.combat.attackers) {
            if (at.defender.tag == TargetKindTag::Player) incoming += std::max(0, pw(at.creature.card));
        }
        if (life() - incoming < kLifeFloor && blockers_needed(att) == 1) return {*least_valuable(adds), "blocco: di sacrificio (soglia di vita)"};
        if (done != nullptr) return {*done, "blocco: nessuno"};
        return {*least_valuable(adds), "blocco: obbligato"};
    }

    Choice divide() {
        const combat::DamageDivision division = combat::damage_division(s, db);
        if (division.splits.size() != legal.size() || division.splits.empty()) return {legal.front(), "divisione del danno: l'unico esito"};
        const CardId att = s.pending.subject.card;
        const std::size_t i = best_index(division.splits.size(), [&](std::size_t k) {
            int score = 0;
            for (std::size_t j = 0; j < division.blockers.size(); ++j) {
                const CardId b = division.blockers[j];
                const int dealt = combat::damage_after_prevention(s, db, b, def(att).colors, division.splits[k][j]);
                if (s.objects[b].damage + dealt >= tough(b)) score += 100 + threat_score(b) / 100;
            }
            return score;
        });
        return {legal[i], "divisione del danno: prima le minacce della classifica"};
    }

    // --- Decisioni minori (sezione 23) -------------------------------------------------------

    Choice surveil() {
        const Action* yard = find_type(ActionType::ChooseOption, kSurveilToGraveyard);
        const Action* top = find_type(ActionType::ChooseOption, kSurveilKeepOnTop);
        if (mine().library.empty() || yard == nullptr || top == nullptr) return {legal.front(), "sorveglianza: l'unica scelta"};
        const CardId card = mine().library.back();
        if (is_land(card) && lands_in_play() + lands_in_hand() >= 5) return {*yard, "sorveglianza: terra in piu'"};
        return {*top, "sorveglianza: resta in cima"};
    }
    Choice discard() {
        std::vector<const Action*> options;
        for (const Action& a : legal) {
            if (a.type == ActionType::ChooseCard) options.push_back(&a);
        }
        if (options.empty()) return {legal.front(), "scarto: l'unica scelta"};
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return -hand_value(options[k]->card); });
        return {*options[i], "scarto: la carta di valore piu' basso"};
    }
    Choice legend_rule() {
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) {
            const CardObject& o = s.objects[options[k]->card];
            return (o.counters.plus_one + o.counters.loyalty) * 10 + (o.has_flag(ObjectFlag::SummoningSick) ? 0 : 1);
        });
        return {*options[i], "regola delle leggende: la copia con piu' segnalini"};
    }
    Choice order_triggers() {
        // La prima scelta va in pila per prima e si risolve per ultima.
        const std::size_t i = best_index(legal.size(), [&](std::size_t k) {
            const StackEffect e = s.pending_triggers[legal[k].value].effect;
            if (e == StackEffect::EvokeSacrifice) return 3;
            if (e == StackEffect::WarpExile) return 2;
            if (e == StackEffect::GainLifeAndEnergy) return 0;
            return 1;
        });
        return {legal[i], "ordine dei trigger: sacrificio di Solitude sotto, la vita sopra"};
    }
    Choice sacrifice_effect() {
        std::vector<const Action*> options;
        for (const Action& a : legal) options.push_back(&a);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) { return -field_value(options[k]->card); });
        return {*options[i], "Flare of Malice: il permanente meno prezioso"};
    }
    Choice pay_optional() {
        if (const Action* a = find_type(ActionType::ChooseOption, kPayCost)) return {*a, "Guide of Souls: l'energia si spende sempre"};
        return {legal.front(), "costo facoltativo: l'unica scelta"};
    }

    // Costo di mana della magia in cima alla pila (per confrontare i modi di pagare).
    ManaCost pending_cost() const {
        const StackItem& item = s.stack_items.back();
        const CardDef& d = db.get(item.source_def);
        if (item.kind != StackKind::Spell) return d.rules.landcycling.cost;
        if (item.method == CastMethod::Warp) return d.rules.warp.cost;
        ManaCost c = d.cost;
        int generic = c.generic + c.x * item.x + item.replicate * d.rules.spell.replicate_generic;
        generic -= std::min(generic, item.exiled_for_cost * d.rules.spell.reduction_per_white_card);
        c.generic = static_cast<std::uint8_t>(generic);
        c.x = 0;
        return c;
    }
    Choice pay_mana() {
        const std::vector<PaymentOption> options = payment_options(s, db, me, pending_cost());
        if (options.size() != legal.size()) return {legal.front(), "pagamento: il primo modo"};
        const bool need_w = in_hand(id.ephemerate) || in_hand(id.thraben) || in_hand(id.march) || in_hand(id.phelia);
        const bool need_u = in_hand(id.consign);
        const std::size_t i = best_index(options.size(), [&](std::size_t k) {
            int w = 0, u = 0, duals = 0;
            for (const CardId c : s.battlefield) {
                const CardObject& o = s.objects[c];
                if (o.controller != me || o.has_flag(ObjectFlag::Tapped) || !is_land(c)) continue;
                if (std::find(options[k].lands.begin(), options[k].lands.end(), c) != options[k].lands.end()) continue;
                const ColorMask m = mana_colors(def(c));
                w += (m & color_bit(Color::White)) != 0 ? 1 : 0;
                u += (m & color_bit(Color::Blue)) != 0 ? 1 : 0;
                duals += std::popcount(static_cast<unsigned>(m)) >= 2 ? 1 : 0;
            }
            return ((need_w && w > 0) ? 100 : 0) + ((need_u && u > 0) ? 100 : 0) + duals;
        });
        return {legal[i], "pagamento: restano i colori delle istantanee, poi le bicolori"};
    }

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
            case DecisionKind::DeclareAttacker: return attack();
            case DecisionKind::DeclareBlockers: return block();
            case DecisionKind::DivideDamage: return divide();
            case DecisionKind::OrderTriggers: return order_triggers();
            case DecisionKind::SacrificeForEffect: return sacrifice_effect();
            case DecisionKind::LegendRule: return legend_rule();
            case DecisionKind::PayOptionalCost: return pay_optional();
            case DecisionKind::ChooseMode: return choose_mode();
            case DecisionKind::ChooseX: return choose_x();
            case DecisionKind::ExileFromHandForCost: return exile_from_hand();
            case DecisionKind::CastFromExile: return cast_from_exile();
            case DecisionKind::ChooseReplicate: return replicate();
            case DecisionKind::ChooseFromHand:
            case DecisionKind::ExileFromGraveyard:
            case DecisionKind::SacrificeForCost:
            case DecisionKind::None:
            case DecisionKind::GameOver: break;
        }
        return fallback();
    }
};

UwAgent::UwAgent(const CardDatabase& db, std::uint64_t seed, UwAgentOptions options)
    : db_(&db), options_(options), rng_(seed, kUwAgentStream) {
    Ids& i = ids_;
    i.solitude = lookup(db, "Solitude");
    i.ephemerate = lookup(db, "Ephemerate");
    i.phelia = lookup(db, "Phelia, Exuberant Shepherd");
    i.march = lookup(db, "March of Otherworldly Light");
    i.thraben = lookup(db, "Thraben Charm");
    i.seam_rip = lookup(db, "Seam Rip");
    i.witch = lookup(db, "Witch Enchanter");
    i.sanctifier = lookup(db, "Sanctifier en-Vec");
    i.recruiter = lookup(db, "Recruiter of the Guard");
    i.shepherd = lookup(db, "Starfield Shepherd");
    i.riddler = lookup(db, "Quantum Riddler");
    i.teferi = lookup(db, "Teferi, Time Raveler");
    i.force = lookup(db, "Force of Negation");
    i.consign = lookup(db, "Consign to Memory");
    i.guide = lookup(db, "Guide of Souls");
    i.ocelot = lookup(db, "Ocelot Pride");
    i.cat = lookup(db, "Cat Token");
    i.plains = lookup(db, "Plains");
    i.fountain = lookup(db, "Hallowed Fountain");
    i.archive = lookup(db, "Meticulous Archive");
    i.nethergoyf = lookup(db, "Nethergoyf");
    i.moonshadow = lookup(db, "Moonshadow");
    i.manipulator = lookup(db, "Dark Matter Manipulator");
    i.supplier = lookup(db, "Stitcher's Supplier");
    i.fomo = lookup(db, "Fear of Missing Out");
    i.phoenix = lookup(db, "Detective's Phoenix");
    i.wraith = lookup(db, "Street Wraith");
    i.troll = lookup(db, "Troll of Khazad-dûm");
    i.bolt = lookup(db, "Lightning Bolt");
    i.flare = lookup(db, "Flare of Malice");
    i.thoughtseize = lookup(db, "Thoughtseize");
    i.bauble = lookup(db, "Mishra's Bauble");
}

Action UwAgent::choose(const GameState& s, std::span<const Action> legal) {
    if (legal.empty()) throw EngineError("UwAgent: nessuna azione legale");
    const Choice c = decide(s, legal);
    if (std::find(legal.begin(), legal.end(), c.action) == legal.end()) {
        throw EngineError("UwAgent: la regola \"" + std::string(c.rule) + "\" ha scelto un'azione non legale");
    }
    last_rule_ = c.rule;
    ++counts_[std::string(c.rule)];
    return c.action;
}

UwAgent::Choice UwAgent::decide(const GameState& s, std::span<const Action> legal) {
    UwRules rules(*this, s, legal);
    return rules.decide();
}

std::size_t UwAgent::pick(std::size_t n) { return rng_.bounded(static_cast<std::uint32_t>(n)); }

}  // namespace mtg
