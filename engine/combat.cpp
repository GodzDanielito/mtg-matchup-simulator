#include "combat.hpp"

#include <algorithm>
#include <compare>
#include <set>
#include <utility>

namespace mtg {

namespace {

PlayerId other(PlayerId p) { return static_cast<PlayerId>(1 - p); }

PendingDecision decision(DecisionKind kind, PlayerId player, ObjRef subject) {
    PendingDecision d;
    d.kind = kind;
    d.player = player;
    d.subject = subject;
    return d;
}

bool is_creature(const GameState& s, const CardDatabase& db, CardId id) {
    // Detective's Phoenix conferita è un'Aura, non una creatura.
    return !s.objects[id].has_flag(ObjectFlag::Bestowed) && effective_def(s, db, id).has_type(CardType::Creature);
}

// --- Classi di creature identiche ---------------------------------------------------

// Tutto ciò che distingue due creature ai fini del combattimento. Le creature con
// la stessa chiave sono intercambiabili: si sceglie quante, non quali.
struct ClassKey {
    CardDefId def = kNoDef;
    PlayerId controller = kNoPlayer;
    std::uint8_t damage = 0;
    std::uint8_t plus_one = 0;
    std::uint8_t minus_one = 0;
    KeywordCounters keyword_counters{};
    std::uint16_t flags = 0;

    auto operator<=>(const ClassKey&) const = default;
};

// Flag che contano: per attaccare anche "ha già attaccato" (Fear of Missing Out);
// la debolezza da evocazione no, perché i candidati possono già tutti attaccare.
constexpr std::uint16_t kAttackFlags = static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(ObjectFlag::Token) | static_cast<std::uint16_t>(ObjectFlag::Bestowed) |
    static_cast<std::uint16_t>(ObjectFlag::BackFace) | static_cast<std::uint16_t>(ObjectFlag::AttackedThisTurn));
constexpr std::uint16_t kBlockFlags = static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(ObjectFlag::Token) | static_cast<std::uint16_t>(ObjectFlag::Bestowed) |
    static_cast<std::uint16_t>(ObjectFlag::BackFace));

ClassKey class_key(const GameState& s, CardId id, std::uint16_t flag_mask) {
    const CardObject& o = s.objects[id];
    return ClassKey{o.def,
                    o.controller,
                    o.damage,
                    o.counters.plus_one,
                    o.counters.minus_one,
                    o.counters.keywords,
                    static_cast<std::uint16_t>(o.flags & flag_mask)};
}

// Ordine canonico: per classe, poi per CardId. Riempie candidati e classi.
void set_candidates(GameState& s, std::vector<std::pair<ClassKey, CardId>> list) {
    std::sort(list.begin(), list.end());
    CombatState& c = s.combat;
    c.candidates.clear();
    c.candidate_class.clear();
    c.choice.clear();
    std::uint8_t cls = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i > 0 && list[i].first != list[i - 1].first) ++cls;
        c.candidates.push_back(list[i].second);
        c.candidate_class.push_back(cls);
    }
}

// --- Evasione e restrizioni (702.9, 702.14, 702.16, 702.111) ---------------------------

int min_blockers(const GameState& s, const CardDatabase& db, CardId attacker) {
    int m = has_keyword(s, db, attacker, Keyword::Menace) ? 2 : 1;
    return std::max<int>(m, effective_def(s, db, attacker).rules.combat.min_blockers);
}

// Passaterra: imbloccabile se il difensore controlla una terra del tipo indicato.
bool unblockable(const GameState& s, const CardDatabase& db, CardId attacker, PlayerId defender) {
    const LandTypeMask walk = effective_def(s, db, attacker).rules.combat.landwalk;
    if (walk == 0) return false;
    for (const CardId id : s.battlefield) {
        const CardDef& d = effective_def(s, db, id);
        if (s.objects[id].controller == defender && d.has_type(CardType::Land) && (d.land_types & walk) != 0) {
            return true;
        }
    }
    return false;
}

// Il bloccante può bloccare quell'attaccante? (restrizioni della singola coppia)
bool can_block_pair(const GameState& s, const CardDatabase& db, CardId blocker, CardId attacker) {
    if (has_keyword(s, db, attacker, Keyword::Flying) && !has_keyword(s, db, blocker, Keyword::Flying) &&
        !has_keyword(s, db, blocker, Keyword::Reach)) {
        return false;
    }
    if ((protection_of(s, db, attacker) & effective_def(s, db, blocker).colors) != 0) return false;  // 702.16f
    return true;
}

// --- Attaccanti (508.1) ------------------------------------------------------------

constexpr std::uint8_t kAttackPlayer = 1;  // opzione "attacca il giocatore avversario"

bool can_attack(const GameState& s, const CardDatabase& db, CardId id) {
    const CardObject& o = s.objects[id];
    return o.controller == s.turn.active_player && is_creature(s, db, id) && !o.has_flag(ObjectFlag::Tapped) &&
           (!o.has_flag(ObjectFlag::SummoningSick) || has_keyword(s, db, id, Keyword::Haste));
}

// Opzioni per il candidato `i`: dentro una classe le scelte non possono scendere.
std::vector<std::uint8_t> attack_options(const GameState& s, std::size_t i) {
    const CombatState& c = s.combat;
    const std::uint8_t minimum =
        (i > 0 && c.candidate_class[i] == c.candidate_class[i - 1]) ? c.choice[i - 1] : std::uint8_t{0};
    std::vector<std::uint8_t> out;
    for (std::uint8_t opt = minimum; opt <= kAttackPlayer; ++opt) out.push_back(opt);
    return out;
}

void finish_attack_declaration(GameState& s) {
    CombatState& c = s.combat;
    const PlayerId defender = other(s.turn.active_player);
    for (std::size_t i = 0; i < c.candidates.size(); ++i) {
        if (c.choice[i] == 0) continue;
        CardObject& o = s.objects[c.candidates[i]];
        o.set_flag(ObjectFlag::Tapped);  // 508.1f: attaccare tappa (nessuno ha cautela nel pool)
        o.set_flag(ObjectFlag::AttackedThisTurn);
        c.attackers.push_back(Attacker{s.ref(c.candidates[i]), Target{TargetKindTag::Player, defender, ObjRef{}}, false});
    }
    c.candidates.clear();
    c.candidate_class.clear();
    c.choice.clear();
    // 508.1m: qui scatteranno i trigger di attacco (Phelia, Guide of Souls, Fear of
    // Missing Out) leggendo c.attackers; nessuna di queste carte è ancora implementata.
}

void continue_attack_declaration(GameState& s) {
    CombatState& c = s.combat;
    while (c.cursor < c.candidates.size()) {
        const auto options = attack_options(s, c.cursor);
        if (options.size() == 1) {  // scelta obbligata: nessuna decisione
            c.choice.push_back(options.front());
            ++c.cursor;
            continue;
        }
        s.pending = decision(DecisionKind::DeclareAttacker, s.turn.active_player, s.ref(c.candidates[c.cursor]));
        return;
    }
    finish_attack_declaration(s);
}

// --- Bloccanti (509.1) -------------------------------------------------------------

// Vista sulla dichiarazione dei bloccanti per l'attaccante corrente: posizioni dei
// candidati (ordine canonico), loro classi, chi è già usato, profili.
struct BlockSearch {
    std::vector<int> cls;         // classe di ogni candidato
    std::vector<bool> eligible;   // può bloccare l'attaccante corrente
    std::vector<bool> used;       // sta già bloccando
    int min = 1;
    std::vector<int> prev;        // profilo dell'attaccante identico precedente (vuoto se non c'è)
    std::vector<int> profile;     // bloccanti per classe assegnati all'attaccante corrente
    int count = 0;
    int last_pos = -1;
};

bool identical_attackers(const GameState& s, std::size_t a, std::size_t b) {
    const Attacker& x = s.combat.attackers[a];
    const Attacker& y = s.combat.attackers[b];
    return s.is_valid(x.creature) && s.is_valid(y.creature) && x.defender == y.defender &&
           class_key(s, x.creature.card, kAttackFlags) == class_key(s, y.creature.card, kAttackFlags);
}

BlockSearch block_search(const GameState& s, const CardDatabase& db) {
    const CombatState& c = s.combat;
    const std::size_t n = c.candidates.size();
    const std::size_t cur = c.cursor;
    const Attacker& attacker = c.attackers[cur];
    const int classes = n == 0 ? 0 : c.candidate_class[n - 1] + 1;

    BlockSearch b;
    b.cls.resize(n);
    b.eligible.assign(n, false);
    b.used.assign(n, false);
    b.profile.assign(static_cast<std::size_t>(classes), 0);
    const bool attacker_ok = in_combat(s, db, attacker.creature) &&
                             !unblockable(s, db, attacker.creature.card, other(s.turn.active_player));
    for (std::size_t p = 0; p < n; ++p) {
        b.cls[p] = c.candidate_class[p];
        b.used[p] = c.choice[p] != kNoChoice;
        b.eligible[p] = attacker_ok && !b.used[p] && can_block_pair(s, db, c.candidates[p], attacker.creature.card);
        if (c.choice[p] == cur) {
            ++b.profile[static_cast<std::size_t>(b.cls[p])];
            ++b.count;
        }
    }
    if (attacker_ok) b.min = min_blockers(s, db, attacker.creature.card);
    b.last_pos = c.last_pos;
    if (cur > 0 && identical_attackers(s, cur, cur - 1)) {
        b.prev.assign(static_cast<std::size_t>(classes), 0);
        for (std::size_t p = 0; p < n; ++p) {
            if (c.choice[p] == cur - 1) ++b.prev[static_cast<std::size_t>(b.cls[p])];
        }
    }
    return b;
}

// Posizioni aggiungibili (regole 1 e 2 e vincolo degli attaccanti identici).
std::vector<int> raw_adds(const BlockSearch& b) {
    std::vector<int> out;
    for (int p = b.last_pos + 1; p < static_cast<int>(b.cls.size()); ++p) {
        const auto up = static_cast<std::size_t>(p);
        if (!b.eligible[up] || b.used[up]) continue;
        bool first_free = true;  // il primo libero della sua classe
        for (std::size_t q = 0; q < up; ++q) {
            if (b.cls[q] == b.cls[up] && !b.used[q]) first_free = false;
        }
        if (!first_free) continue;
        if (!b.prev.empty()) {
            std::vector<int> next = b.profile;
            ++next[static_cast<std::size_t>(b.cls[up])];
            if (next > b.prev) continue;  // ordine lessicografico: non oltre il profilo precedente
        }
        out.push_back(p);
    }
    return out;
}

BlockSearch with_added(BlockSearch b, int p) {
    const auto up = static_cast<std::size_t>(p);
    b.used[up] = true;
    ++b.profile[static_cast<std::size_t>(b.cls[up])];
    ++b.count;
    b.last_pos = p;
    return b;
}

bool fine_allowed(const BlockSearch& b) { return b.count == 0 || b.count >= b.min; }

// Regola 3: esiste un completamento legale per questo attaccante?
bool completable(const BlockSearch& b) {
    if (fine_allowed(b)) return true;
    for (const int p : raw_adds(b)) {
        if (completable(with_added(b, p))) return true;
    }
    return false;
}

std::vector<int> valid_adds(const BlockSearch& b) {
    std::vector<int> out;
    for (const int p : raw_adds(b)) {
        if (completable(with_added(b, p))) out.push_back(p);
    }
    return out;
}

void add_blocker(GameState& s, int p) {
    CombatState& c = s.combat;
    const auto up = static_cast<std::size_t>(p);
    c.choice[up] = c.cursor;
    c.blocks.push_back(Block{s.ref(c.candidates[up]), c.cursor});
    c.last_pos = static_cast<std::int16_t>(p);
}

void next_attacker(GameState& s) {
    ++s.combat.cursor;
    s.combat.last_pos = -1;
}

void finish_block_declaration(GameState& s) {
    CombatState& c = s.combat;
    for (const Block& b : c.blocks) c.attackers[b.attacker].blocked = true;  // 509.1h
    c.candidates.clear();
    c.candidate_class.clear();
    c.choice.clear();
    // 509.1i: qui scatteranno i trigger di blocco (nessuno nel pool implementato).
}

void continue_block_declaration(GameState& s, const CardDatabase& db) {
    CombatState& c = s.combat;
    while (c.cursor < c.attackers.size()) {
        const BlockSearch b = block_search(s, db);
        const std::vector<int> adds = valid_adds(b);
        const bool fine = fine_allowed(b);
        const std::size_t options = adds.size() + (fine ? 1 : 0);
        if (options == 0) throw EngineError("dichiarazione dei bloccanti senza completamenti legali");
        if (options == 1) {  // scelta obbligata
            if (fine) {
                next_attacker(s);
            } else {
                add_blocker(s, adds.front());
            }
            continue;
        }
        s.pending = decision(DecisionKind::DeclareBlockers, other(s.turn.active_player), c.attackers[c.cursor].creature);
        return;
    }
    finish_block_declaration(s);
}

// --- Danno (510) -----------------------------------------------------------------

bool deals_damage_now(const GameState& s, const CardDatabase& db, CardId id) {
    const bool first_strike = has_keyword(s, db, id, Keyword::FirstStrike);
    if (s.turn.step == Step::FirstStrikeDamage) return first_strike;
    return !(s.combat.first_strike_step_done && first_strike);  // chi ha già colpito non colpisce di nuovo
}

std::vector<CardId> live_blockers(const GameState& s, const CardDatabase& db, std::size_t attacker) {
    std::vector<CardId> out;
    for (const Block& b : s.combat.blocks) {
        if (b.attacker == attacker && in_combat(s, db, b.blocker)) out.push_back(b.blocker.card);
    }
    return out;
}

// Esiti distinti della divisione del danno di un attaccante tra i suoi bloccanti
// (510.1c): per ogni esito, una ripartizione che lo produce.
std::vector<std::vector<int>> divide_options(const GameState& s, const CardDatabase& db, std::size_t attacker) {
    const CardId source = s.combat.attackers[attacker].creature.card;
    const int total = power(s, db, source);
    const ColorMask colors = effective_def(s, db, source).colors;
    const bool lifelink = has_keyword(s, db, source, Keyword::Lifelink);
    const std::vector<CardId> blockers = live_blockers(s, db, attacker);
    const std::size_t k = blockers.size();

    std::vector<std::vector<int>> options;
    std::set<std::vector<int>> seen;
    std::vector<int> split(k, 0);
    auto rec = [&](auto&& self, std::size_t i, int remaining) -> void {
        if (i + 1 == k) {
            split[i] = remaining;
            // Esito: per ogni bloccante "muore" (-1) o il danno che gli resta; tra
            // bloccanti identici l'ordine non conta.
            std::vector<std::pair<ClassKey, int>> outcome;
            int dealt_total = 0;
            for (std::size_t j = 0; j < k; ++j) {
                const int dealt = combat::damage_after_prevention(s, db, blockers[j], colors, split[j]);
                dealt_total += dealt;
                const int marked = s.objects[blockers[j]].damage + dealt;
                const int value = marked >= toughness(s, db, blockers[j]) ? -1 : marked;
                outcome.emplace_back(class_key(s, blockers[j], kBlockFlags), value);
            }
            std::sort(outcome.begin(), outcome.end());
            std::vector<int> signature;
            for (const auto& [key, value] : outcome) signature.push_back(value);
            if (lifelink) signature.push_back(dealt_total);  // la vita guadagnata distingue gli esiti
            if (seen.insert(signature).second) options.push_back(split);
            return;
        }
        for (int a = 0; a <= remaining; ++a) {
            split[i] = a;
            self(self, i + 1, remaining - a);
        }
    };
    if (k > 0) rec(rec, 0, total);
    return options;
}

void assign(GameState& s, CardId source, Target target, int amount) {
    if (amount > 0) s.combat.assignments.push_back(DamageAssignment{source, target, static_cast<std::uint8_t>(amount)});
}

void assign_split(GameState& s, const CardDatabase& db, std::size_t attacker, const std::vector<int>& split) {
    const CardId source = s.combat.attackers[attacker].creature.card;
    const std::vector<CardId> blockers = live_blockers(s, db, attacker);
    for (std::size_t j = 0; j < blockers.size(); ++j) {
        assign(s, source, Target{TargetKindTag::Object, kNoPlayer, s.ref(blockers[j])}, split[j]);
    }
}

// 510.2: tutto il danno assegnato viene inflitto insieme, con prevenzione e legame vitale.
void deal_assigned_damage(GameState& s, const CardDatabase& db) {
    for (const DamageAssignment& a : s.combat.assignments) {
        const CardDef& source = effective_def(s, db, a.source);
        int dealt = a.amount;
        if (a.target.tag == TargetKindTag::Player) {
            lose_life(s, a.target.player, dealt);
        } else {
            CardObject& o = s.objects[a.target.object.card];
            dealt = combat::damage_after_prevention(s, db, a.target.object.card, source.colors, dealt);
            o.damage = static_cast<std::uint8_t>(std::min(255, o.damage + dealt));
        }
        if (dealt > 0 && has_keyword(s, db, a.source, Keyword::Lifelink)) {
            gain_life(s, s.objects[a.source].controller, dealt);  // 702.15b
        }
    }
    s.combat.assignments.clear();
}

void continue_damage(GameState& s, const CardDatabase& db) {
    CombatState& c = s.combat;
    while (c.cursor < c.attackers.size()) {
        const std::size_t i = c.cursor;
        const Attacker& a = c.attackers[i];
        const CardId source = a.creature.card;
        if (!in_combat(s, db, a.creature) || !deals_damage_now(s, db, source) || power(s, db, source) <= 0) {
            ++c.cursor;
            continue;
        }
        const int amount = power(s, db, source);
        if (!a.blocked) {
            assign(s, source, a.defender, amount);  // 510.1b
        } else {
            const std::vector<CardId> blockers = live_blockers(s, db, i);
            if (blockers.size() == 1) {
                assign(s, source, Target{TargetKindTag::Object, kNoPlayer, s.ref(blockers.front())}, amount);
            } else if (blockers.size() > 1) {
                const auto options = divide_options(s, db, i);
                if (options.size() > 1) {
                    s.pending = decision(DecisionKind::DivideDamage, s.objects[source].controller, a.creature);
                    return;  // l'assegnazione arriva con la decisione
                }
                assign_split(s, db, i, options.front());
            }
            // Bloccata ma senza bloccanti rimasti: nessun danno (510.1c).
        }
        ++c.cursor;
    }
    // 510.1d: ogni bloccante infligge il suo danno all'attaccante che blocca.
    for (const Block& b : c.blocks) {
        const Attacker& a = c.attackers[b.attacker];
        if (!in_combat(s, db, b.blocker) || !in_combat(s, db, a.creature)) continue;
        if (!deals_damage_now(s, db, b.blocker.card)) continue;
        assign(s, b.blocker.card, Target{TargetKindTag::Object, kNoPlayer, a.creature}, power(s, db, b.blocker.card));
    }
    deal_assigned_damage(s, db);
    if (s.turn.step == Step::FirstStrikeDamage) c.first_strike_step_done = true;
}

}  // namespace

// --- Interfaccia ------------------------------------------------------------------

bool has_keyword(const GameState& s, const CardDatabase& db, CardId id, Keyword k) {
    const CardObject& o = s.objects[id];
    if ((effective_def(s, db, id).rules.combat.keywords & keyword_bit(k)) != 0) return true;
    return k == Keyword::Flying && o.counters.keywords[static_cast<std::size_t>(KeywordCounter::Flying)] > 0;
}

ColorMask protection_of(const GameState& s, const CardDatabase& db, CardId id) {
    return effective_def(s, db, id).rules.combat.protection;
}

bool in_combat(const GameState& s, const CardDatabase& db, ObjRef ref) {
    return s.is_valid(ref) && s.objects[ref.card].zone == Zone::Battlefield && is_creature(s, db, ref.card);
}

namespace combat {

void begin_attack_declaration(GameState& s, const CardDatabase& db) {
    std::vector<std::pair<ClassKey, CardId>> list;
    for (const CardId id : s.battlefield) {
        if (can_attack(s, db, id)) list.emplace_back(class_key(s, id, kAttackFlags), id);
    }
    set_candidates(s, std::move(list));
    s.combat.cursor = 0;
    continue_attack_declaration(s);
}

void begin_block_declaration(GameState& s, const CardDatabase& db) {
    const PlayerId defender = other(s.turn.active_player);
    std::vector<std::pair<ClassKey, CardId>> list;
    for (const CardId id : s.battlefield) {
        const CardObject& o = s.objects[id];
        if (o.controller == defender && is_creature(s, db, id) && !o.has_flag(ObjectFlag::Tapped)) {
            list.emplace_back(class_key(s, id, kBlockFlags), id);
        }
    }
    set_candidates(s, std::move(list));
    for (std::size_t i = 0; i < s.combat.candidates.size(); ++i) s.combat.choice.push_back(kNoChoice);
    s.combat.cursor = 0;
    s.combat.last_pos = -1;
    continue_block_declaration(s, db);
}

void begin_damage_step(GameState& s, const CardDatabase& db) {
    s.combat.assignments.clear();
    s.combat.cursor = 0;
    continue_damage(s, db);
}

bool needs_first_strike_step(const GameState& s, const CardDatabase& db) {
    for (const Attacker& a : s.combat.attackers) {
        if (in_combat(s, db, a.creature) && has_keyword(s, db, a.creature.card, Keyword::FirstStrike)) return true;
    }
    for (const Block& b : s.combat.blocks) {
        if (in_combat(s, db, b.blocker) && has_keyword(s, db, b.blocker.card, Keyword::FirstStrike)) return true;
    }
    return false;
}

void end_combat(GameState& s) { s.combat = CombatState{}; }

void legal_actions(const GameState& s, const CardDatabase& db, std::vector<Action>& out) {
    const CombatState& c = s.combat;
    switch (s.pending.kind) {
        case DecisionKind::DeclareAttacker:
            for (const std::uint8_t opt : attack_options(s, c.cursor)) {
                out.push_back(opt == 0 ? Action{ActionType::ChooseOption, kNoCard, kNoAttack}
                                       : Action{ActionType::TargetPlayer, kNoCard, other(s.turn.active_player)});
            }
            return;
        case DecisionKind::DeclareBlockers: {
            const BlockSearch b = block_search(s, db);
            if (fine_allowed(b)) out.push_back(Action{ActionType::ChooseOption, kNoCard, kDoneBlocking});
            for (const int p : valid_adds(b)) {
                out.push_back(Action{ActionType::ChooseCard, c.candidates[static_cast<std::size_t>(p)], 0});
            }
            return;
        }
        case DecisionKind::DivideDamage: {
            const std::size_t n = divide_options(s, db, c.cursor).size();
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(Action{ActionType::ChooseOption, kNoCard, static_cast<std::uint8_t>(i)});
            }
            return;
        }
        default: return;
    }
}

void apply(GameState& s, const CardDatabase& db, const PendingDecision& d, const Action& action) {
    CombatState& c = s.combat;
    switch (d.kind) {
        case DecisionKind::DeclareAttacker:
            c.choice.push_back(action.type == ActionType::TargetPlayer ? kAttackPlayer : std::uint8_t{0});
            ++c.cursor;
            continue_attack_declaration(s);
            return;
        case DecisionKind::DeclareBlockers:
            if (action.type == ActionType::ChooseCard) {
                const auto pos = static_cast<int>(c.candidates.find(action.card));
                add_blocker(s, pos);
            } else {
                next_attacker(s);
            }
            continue_block_declaration(s, db);
            return;
        case DecisionKind::DivideDamage: {
            const auto options = divide_options(s, db, c.cursor);
            assign_split(s, db, c.cursor, options[action.value]);
            ++c.cursor;
            continue_damage(s, db);
            return;
        }
        default: throw EngineError("combat::apply: decisione non di combattimento");
    }
}

int damage_after_prevention(const GameState& s, const CardDatabase& db, CardId target, ColorMask source_colors,
                            int amount) {
    return (protection_of(s, db, target) & source_colors) != 0 ? 0 : amount;  // 702.16e
}

}  // namespace combat

}  // namespace mtg
