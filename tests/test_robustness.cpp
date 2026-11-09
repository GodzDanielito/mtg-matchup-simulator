// Test di robustezza: partite complete giocate dall'agente casuale.
//
// L'agente casuale sceglie tra tutte le azioni legali: terre, fetch, magie
// implementate (tutte quelle di Rakdos; di UW Ocelot Pride, Guide of Souls,
// Witch Enchanter, Recruiter of the Guard, Starfield Shepherd con e senza warp,
// Sanctifier en-Vec), mulligan, scarti, bersagli, pagamenti, attacchi, blocchi,
// divisioni del danno e "passa". Dopo ogni azione si controllano tutti gli
// invarianti; ogni partita deve finire senza eccezioni.
//
// Due serie da 10.000 partite:
//  - UW Blink contro Rakdos: il matchup vero, con le creature di UW che
//    attaccano e bloccano;
//  - Rakdos contro Rakdos: entrambi i lati con molte creature, così si coprono
//    anche blocchi multipli e divisioni del danno.
// I controlli di copertura verificano che ogni carta implementata e ogni tipo
// di decisione compaiano davvero.

#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

using namespace mtg;
using namespace test;

namespace {

constexpr int kGames = 10000;
constexpr std::size_t kMaxDecisions = 100000;  // rete di sicurezza contro i cicli infiniti

struct Outcome {
    PlayerId winner = kNoPlayer;
    bool decked[kNumPlayers] = {false, false};
    std::int16_t life[kNumPlayers] = {0, 0};
    std::size_t decisions = 0;
    std::string failure;  // vuoto = partita regolare
};

// Quante volte ogni carta viene lanciata o attivata, quante decisioni di ogni
// tipo, e quante scelte di combattimento fa l'agente.
struct Coverage {
    std::map<std::string, int> cast, activated;
    std::map<DecisionKind, int> decisions;
    int attacks = 0;  // "attacca" scelto in una decisione DeclareAttacker
    int blocks = 0;   // bloccante aggiunto in una decisione DeclareBlockers
    int fomo_targets = 0;          // bersagli scelti per il trigger di Fear of Missing Out
    int extra_combat_attacks = 0;  // creature che attaccano di nuovo nello stesso turno (combattimento aggiuntivo)
    std::size_t max_tokens = 0;  // massimo di token in campo insieme (gli slot sono kMaxTokens)
    int energy_paid = 0;         // Guide of Souls: tre energie pagate attaccando
    int destroy_targets = 0;     // Witch Enchanter: bersagli scelti per il trigger di entrata
    int creature_searches = 0;   // Recruiter of the Guard e Starfield Shepherd: ricerche risolte
    int blessings = 0;           // partite in cui un giocatore ha preso la benedizione della città

    // Opzioni per tipo di decisione arrivata all'agente (le scelte obbligate avvengono da
    // sole): il fattore di ramificazione che vedrà la ricerca ad albero.
    struct Options {
        std::string label;
        std::size_t max = 0;
        std::size_t sum = 0;
        std::size_t count = 0;
    };
    std::map<int, Options> options;  // chiave: ordine di stampa (tipo di decisione, sottotipo)
};

// Nome leggibile della decisione; l'esilio dal cimitero si divide tra fuga e prove,
// i bersagli tra magie e abilità (attivate o innescate).
std::pair<int, std::string> decision_label(const GameState& s) {
    const DecisionKind k = s.pending.kind;
    const int base = static_cast<int>(k) * 2;
    switch (k) {
        case DecisionKind::Mulligan: return {base, "mulligan"};
        case DecisionKind::BottomCards: return {base, "carte in fondo (mulligan)"};
        case DecisionKind::Priority: return {base, "priorita'"};
        case DecisionKind::PayLifeOrTapped: return {base, "vita o terra tappata"};
        case DecisionKind::SearchLibrary: return {base, "ricerca della fetch"};
        case DecisionKind::Surveil: return {base, "sorveglianza"};
        case DecisionKind::DiscardToHandSize: return {base, "scarto a fine turno"};
        case DecisionKind::LegendRule: return {base, "regola delle leggende"};
        case DecisionKind::ChooseTarget:
            return s.stack_items.back().kind == StackKind::Spell ? std::pair{base, std::string("bersaglio di una magia")}
                                                                 : std::pair{base + 1, std::string("bersaglio di un'abilita'")};
        case DecisionKind::PayMana: return {base, "pagamento del mana"};
        case DecisionKind::ChooseFromHand: return {base, "carta dalla mano (Thoughtseize)"};
        case DecisionKind::DeclareAttacker: return {base, "attacco (per creatura)"};
        case DecisionKind::DeclareBlockers: return {base, "blocco (per attaccante)"};
        case DecisionKind::DivideDamage: return {base, "divisione del danno"};
        case DecisionKind::OrderTriggers: return {base, "ordine dei trigger"};
        case DecisionKind::ExileFromGraveyard:
            return s.stack_items.back().method == CastMethod::Bestow
                       ? std::pair{base + 1, std::string("esilio per le prove (Phoenix)")}
                       : std::pair{base, std::string("esilio per la fuga (Nethergoyf)")};
        case DecisionKind::DiscardCard: return {base, "scarto (Fear of Missing Out)"};
        case DecisionKind::SacrificeForCost: return {base, "sacrificio come costo (Flare)"};
        case DecisionKind::SacrificeForEffect: return {base, "sacrificio per effetto (Flare)"};
        case DecisionKind::PayOptionalCost: return {base, "costo facoltativo (Guide of Souls)"};
        case DecisionKind::None:
        case DecisionKind::GameOver: break;
    }
    return {base, "altro"};
}

std::size_t tokens_on_battlefield(const GameState& s) {
    std::size_t n = 0;
    for (std::size_t slot = kMaxCards; slot < kMaxObjects; ++slot) {
        n += s.objects[slot].zone == Zone::Battlefield ? 1 : 0;
    }
    return n;
}

Outcome play_random_game(std::uint64_t seed, const Decklist& deck0, const Decklist& deck1, GameLog* log,
                         GameState* final_state = nullptr, Coverage* coverage = nullptr) {
    const CardDatabase& db = db_with_tokens();
    Outcome out;
    GameState s = create_game(db, deck0, deck1, seed);
    start_game(s, db, StartOptions{static_cast<PlayerId>(seed % 2), true}, log);
    RandomAgent agent(seed);  // generatore proprio, su uno stream diverso da quello della partita

    while (s.pending.kind != DecisionKind::GameOver) {
        const std::vector<Action> actions = legal_actions(s, db);
        if (actions.empty()) {
            out.failure = "decisione senza azioni legali";
            break;
        }
        // Priorità e decisioni di combattimento con una sola opzione avvengono da sole
        // (passaggio automatico, scelte obbligate): non devono mai arrivare all'agente.
        const DecisionKind k = s.pending.kind;
        const bool must_be_automatic = k == DecisionKind::Priority || k == DecisionKind::DeclareAttacker ||
                                       k == DecisionKind::DeclareBlockers || k == DecisionKind::DivideDamage ||
                                       k == DecisionKind::OrderTriggers || k == DecisionKind::ExileFromGraveyard;
        if (must_be_automatic && actions.size() == 1) {
            out.failure = "decisione con una sola azione legale (doveva avvenire da sola)";
            break;
        }
        const Action chosen = agent.choose(s, actions);
        if (coverage != nullptr) {
            ++coverage->decisions[s.pending.kind];
            const auto [order, label] = decision_label(s);
            Coverage::Options& o = coverage->options[order];
            o.label = label;
            o.max = std::max(o.max, actions.size());
            o.sum += actions.size();
            ++o.count;
            if (chosen.type == ActionType::CastSpell) {
                std::string name = db.get(s.objects[chosen.card].def).name;
                switch (static_cast<CastMethod>(chosen.value)) {
                    case CastMethod::Normal:
                        if (s.objects[chosen.card].zone == Zone::Exile) name += " (dall'esilio)";
                        break;
                    case CastMethod::Escape: name += " (fuga)"; break;
                    case CastMethod::Bestow:
                        name += s.objects[chosen.card].zone == Zone::Graveyard ? " (conferimento dal cimitero)"
                                                                               : " (conferimento)";
                        break;
                    case CastMethod::SacrificeAlternative: name += " (costo alternativo)"; break;
                    case CastMethod::Warp: name += " (warp)"; break;
                }
                ++coverage->cast[name];
            }
            if (chosen.type == ActionType::ActivateAbility) {
                ++coverage->activated[db.get(s.objects[chosen.card].def).name];
            }
            if (s.pending.kind == DecisionKind::DeclareAttacker && chosen.type == ActionType::TargetPlayer) {
                ++coverage->attacks;
                if (s.objects[s.pending.subject.card].has_flag(ObjectFlag::AttackedThisTurn)) {
                    ++coverage->extra_combat_attacks;
                }
            }
            if (s.pending.kind == DecisionKind::ChooseTarget &&
                s.stack_items.back().effect == StackEffect::UntapAndExtraCombat) {
                ++coverage->fomo_targets;
            }
            if (s.pending.kind == DecisionKind::DeclareBlockers && chosen.type == ActionType::ChooseCard) {
                ++coverage->blocks;
            }
            if (s.pending.kind == DecisionKind::PayOptionalCost && chosen.value == kPayCost) ++coverage->energy_paid;
            if (s.pending.kind == DecisionKind::ChooseTarget &&
                s.stack_items.back().effect == StackEffect::DestroyTarget) {
                ++coverage->destroy_targets;
            }
            if (s.pending.kind == DecisionKind::SearchLibrary && s.pending.search.creature) ++coverage->creature_searches;
        }
        apply(s, db, chosen, log);
        if (coverage != nullptr) coverage->max_tokens = std::max(coverage->max_tokens, tokens_on_battlefield(s));
        if (const auto problems = check_invariants(s, db); !problems.empty()) {
            out.failure = "invariante violato: " + problems.front();
            break;
        }
        if (++out.decisions > kMaxDecisions) {
            out.failure = "la partita non finisce";
            break;
        }
    }
    if (coverage != nullptr && (s.players[0].city_blessing || s.players[1].city_blessing)) ++coverage->blessings;
    out.winner = s.turn.winner;
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        out.decked[p] = s.players[p].drew_from_empty_library;
        out.life[p] = s.players[p].life;
    }
    if (final_state != nullptr) *final_state = s;
    return out;
}

// Gioca kGames partite e stampa tempi, esiti e copertura.
Coverage run_series(const char* title, const Decklist& deck0, const Decklist& deck1) {
    int by_decking = 0, by_life = 0, draws = 0;
    int life_losses[kNumPlayers] = {0, 0};  // chi ha perso per punti vita
    std::size_t total_decisions = 0;
    Coverage coverage;
    const auto start = std::chrono::steady_clock::now();

    for (int game = 0; game < kGames; ++game) {
        Outcome out;
        try {
            out = play_random_game(static_cast<std::uint64_t>(game), deck0, deck1, nullptr, nullptr, &coverage);
        } catch (const std::exception& e) {
            FAIL(title << ", seed " << game << ": eccezione: " << e.what());
        }
        if (!out.failure.empty()) FAIL(title << ", seed " << game << ": " << out.failure);

        total_decisions += out.decisions;
        if (out.winner == kNoPlayer) {
            ++draws;
        } else if (out.decked[1 - out.winner]) {
            ++by_decking;
        } else {
            REQUIRE(out.life[1 - out.winner] <= 0);  // l'unica altra fine possibile
            ++by_life;
            ++life_losses[1 - out.winner];
        }
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\n  [" << title << "] " << kGames << " partite in " << seconds << " s (" << kGames / seconds
              << " partite/s, " << total_decisions / kGames << " decisioni per partita in media)\n"
              << "  finite per grimorio vuoto: " << by_decking << ", per punti vita: " << by_life
              << " (perse dal giocatore 0 " << life_losses[0] << ", dal giocatore 1 " << life_losses[1]
              << "), patte: " << draws << "\n";
    std::cout << "  lanci:";
    for (const auto& [name, n] : coverage.cast) std::cout << " " << name << " " << n << ";";
    std::cout << '\n' << "  attivazioni:";
    for (const auto& [name, n] : coverage.activated) std::cout << " " << name << " " << n << ";";
    std::cout << '\n'
              << "  combattimento: " << coverage.attacks << " attacchi scelti, " << coverage.blocks
              << " bloccanti scelti, " << coverage.decisions[DecisionKind::DivideDamage] << " divisioni del danno\n"
              << "  Fear of Missing Out: " << coverage.fomo_targets << " trigger con bersaglio, "
              << coverage.extra_combat_attacks << " attacchi in combattimenti aggiuntivi\n"
              << "  decisioni: " << coverage.decisions[DecisionKind::OrderTriggers] << " ordini di trigger, "
              << coverage.decisions[DecisionKind::ExileFromGraveyard] << " scelte di esilio (fuga e prove)\n"
              << "  UW: " << coverage.energy_paid << " volte tre energie pagate (Guide of Souls), "
              << coverage.destroy_targets << " bersagli di Witch Enchanter, " << coverage.creature_searches
              << " ricerche di Recruiter e Starfield Shepherd\n"
              << "  benedizione della citta' presa in " << coverage.blessings << " partite su " << kGames << "\n"
              << "  token in campo insieme, massimo osservato: " << coverage.max_tokens << " su " << kMaxTokens
              << " slot\n";
    const std::streamsize old_precision = std::cout.precision();
    std::cout << "  opzioni per decisione (massimo, media, decisioni arrivate all'agente):\n";
    for (const auto& [order, o] : coverage.options) {
        std::cout << "    " << std::left << std::setw(36) << o.label << std::right << std::setw(5) << o.max
                  << std::setw(8) << std::fixed << std::setprecision(1)
                  << static_cast<double>(o.sum) / static_cast<double>(o.count) << std::setw(10) << o.count << '\n';
    }
    std::cout << std::defaultfloat << std::setprecision(static_cast<int>(old_precision));
    CHECK(coverage.max_tokens < kMaxTokens);  // se si avvicina al limite, gli slot vanno rivisti
    CHECK(by_decking + by_life + draws == kGames);
    return coverage;
}

}  // namespace

TEST_CASE("Robustezza: 10.000 partite casuali UW Blink contro Rakdos", "[robustness]") {
    Coverage coverage = run_series("UW Blink - Rakdos", uw_sim_deck(), rakdos_deck());

    // Copertura: ogni carta implementata è stata davvero usata, ogni decisione nuova è comparsa.
    for (const char* name : {"Lightning Bolt", "Thoughtseize", "Street Wraith", "Mishra's Bauble", "Stitcher's Supplier",
                             "Nethergoyf", "Nethergoyf (fuga)", "Moonshadow", "Dark Matter Manipulator",
                             "Troll of Khazad-dûm", "Fear of Missing Out", "Detective's Phoenix",
                             "Detective's Phoenix (conferimento)", "Detective's Phoenix (conferimento dal cimitero)",
                             "Flare of Malice", "Flare of Malice (costo alternativo)"}) {
        INFO(name);
        CHECK(coverage.cast[name] > 0);
    }
    // Le carte di UW (passo 13).
    for (const char* name : {"Ocelot Pride", "Guide of Souls", "Witch Enchanter", "Recruiter of the Guard",
                             "Starfield Shepherd", "Starfield Shepherd (warp)", "Starfield Shepherd (dall'esilio)",
                             "Sanctifier en-Vec"}) {
        INFO(name);
        CHECK(coverage.cast[name] > 0);
    }
    CHECK(coverage.energy_paid > 0);
    CHECK(coverage.destroy_targets > 0);
    CHECK(coverage.creature_searches > 0);
    CHECK(coverage.decisions[DecisionKind::PayOptionalCost] > 0);
    CHECK(coverage.decisions[DecisionKind::DeclareBlockers] > 0);  // ora UW ha creature che bloccano
    CHECK(coverage.decisions[DecisionKind::DiscardCard] > 0);  // Fear of Missing Out che entra
    CHECK(coverage.fomo_targets > 0);                         // ...che attacca con delirium
    CHECK(coverage.extra_combat_attacks > 0);                 // ...e dà davvero un combattimento in più
    for (const char* name : {"Street Wraith", "Mishra's Bauble", "Marsh Flats", "Arid Mesa", "Troll of Khazad-dûm"}) {
        INFO(name);
        CHECK(coverage.activated[name] > 0);
    }
    for (const DecisionKind kind : {DecisionKind::ChooseTarget, DecisionKind::PayMana, DecisionKind::ChooseFromHand,
                                    DecisionKind::DeclareAttacker}) {
        CHECK(coverage.decisions[kind] > 0);
    }
    CHECK(coverage.attacks > 0);
}

TEST_CASE("Robustezza: 10.000 partite casuali Rakdos contro Rakdos (blocchi)", "[robustness]") {
    Coverage coverage = run_series("Rakdos - Rakdos", rakdos_deck(), rakdos_deck());
    // Le scelte di Flare of Malice richiedono creature da entrambe le parti: solo nel mirror.
    for (const DecisionKind kind : {DecisionKind::DeclareAttacker, DecisionKind::DeclareBlockers,
                                    DecisionKind::DivideDamage, DecisionKind::OrderTriggers,
                                    DecisionKind::ExileFromGraveyard, DecisionKind::SacrificeForCost,
                                    DecisionKind::SacrificeForEffect}) {
        INFO(static_cast<int>(kind));
        CHECK(coverage.decisions[kind] > 0);
    }
    CHECK(coverage.attacks > 0);
    CHECK(coverage.blocks > 0);
}

TEST_CASE("Robustezza: stesso seed, stessa partita", "[robustness]") {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        // Metà dei seed con il mirror, per includere blocchi e divisioni del danno.
        const Decklist& deck0 = seed % 4 < 2 ? uw_sim_deck() : rakdos_deck();
        GameLog log_a, log_b;
        GameState a, b;
        const Outcome first = play_random_game(seed, deck0, rakdos_deck(), &log_a, &a);
        const Outcome second = play_random_game(seed, deck0, rakdos_deck(), &log_b, &b);
        INFO("seed " << seed);
        REQUIRE(first.failure.empty());
        CHECK(log_a.entries == log_b.entries);  // ogni decisione e ogni passaggio automatico
        CHECK(first.winner == second.winner);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            CHECK(a.players[p].life == b.players[p].life);
            CHECK(std::vector<CardId>(a.players[p].graveyard.begin(), a.players[p].graveyard.end()) ==
                  std::vector<CardId>(b.players[p].graveyard.begin(), b.players[p].graveyard.end()));
            CHECK(std::vector<CardId>(a.players[p].library.begin(), a.players[p].library.end()) ==
                  std::vector<CardId>(b.players[p].library.begin(), b.players[p].library.end()));
        }
        CHECK(std::vector<CardId>(a.battlefield.begin(), a.battlefield.end()) ==
              std::vector<CardId>(b.battlefield.begin(), b.battlefield.end()));
    }
}

TEST_CASE("Robustezza: seed diversi, partite diverse", "[robustness]") {
    GameLog log_a, log_b;
    (void)play_random_game(1, uw_sim_deck(), rakdos_deck(), &log_a);
    (void)play_random_game(3, uw_sim_deck(), rakdos_deck(), &log_b);  // stesso giocatore iniziale
    CHECK(log_a.entries != log_b.entries);
}
