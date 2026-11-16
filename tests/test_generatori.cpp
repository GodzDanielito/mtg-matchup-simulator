// Passo 0 della ricerca (docs/ricerca.md, sezione 5.3; decisione 10 di docs/metriche.md): un generatore casuale
// per giocatore. Ogni mescolamento del grimorio di un giocatore usa il suo generatore (mano iniziale, mulligan,
// fetch, ricerche, ciclo del Troll); i semi dei due generatori vengono dal seed della partita con SplitMix64. Cosi'
// le pescate di un giocatore non dipendono dalle scelte dell'altro, e i confronti appaiati restano appaiati piu' a
// lungo.

#include "goldfish_agent.hpp"
#include "matchup.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <type_traits>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

std::vector<CardId> cards_of(const PlayerZone& zone) { return {zone.begin(), zone.end()}; }

MatchupConfig matchup_config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

// Le mani da 7 del giocatore `target` a ogni sua decisione di mulligan: fa mulligan `target_mulligans` volte e poi
// tiene; l'altro giocatore fa mulligan `other_mulligans` volte e poi tiene. Le carte in fondo: la prima opzione.
std::vector<std::vector<CardId>> mulligan_hands(std::uint64_t seed, PlayerId starting, PlayerId target,
                                                int target_mulligans, int other_mulligans) {
    GameState s = new_game(seed);
    start_game(s, db_with_tokens(), StartOptions{starting, true});
    std::vector<std::vector<CardId>> hands;
    int done[kNumPlayers] = {0, 0};
    while (s.pending.kind == DecisionKind::Mulligan || s.pending.kind == DecisionKind::BottomCards) {
        const PlayerId p = s.pending.player;
        if (s.pending.kind == DecisionKind::BottomCards) {
            apply(s, db_with_tokens(), legal(s).front());
            continue;
        }
        if (p == target) hands.push_back(cards_of(s.players[p].hand));
        const int wanted = p == target ? target_mulligans : other_mulligans;
        apply(s, db_with_tokens(), Action{done[p]++ < wanted ? ActionType::Mulligan : ActionType::Keep});
    }
    return hands;
}

// Una partita con il giocatore 0 che non fa nulla (goldfish), con o senza un mulligan forzato alla prima decisione,
// contro l'agente di Rakdos. A ogni decisione di Rakdos: il suo grimorio, la sua mano, il suo generatore e la scelta.
struct RakdosView {
    std::vector<std::vector<CardId>> library, hand;
    std::vector<Pcg32> rng;
    std::vector<Action> chosen;
};

RakdosView rakdos_view(std::uint64_t seed, bool goldfish_mulligans) {
    GameState s = new_game(seed);
    start_game(s, db_with_tokens(), StartOptions{static_cast<PlayerId>(seed % 2), true});
    GoldfishAgent goldfish;
    RakdosAgent rakdos(db_with_tokens(), seed);
    RakdosView v;
    bool forced = !goldfish_mulligans;
    for (int i = 0; i < 3000 && s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= 30; ++i) {
        const auto actions = legal(s);
        if (s.pending.player == 0) {
            Action a = goldfish.choose(s, actions);
            if (!forced && s.pending.kind == DecisionKind::Mulligan) {
                a = Action{ActionType::Mulligan};
                forced = true;
            }
            apply(s, db_with_tokens(), a);
            continue;
        }
        v.library.push_back(cards_of(s.players[1].library));
        v.hand.push_back(cards_of(s.players[1].hand));
        v.rng.push_back(s.players[1].rng);
        v.chosen.push_back(rakdos.choose(s, actions));
        apply(s, db_with_tokens(), v.chosen.back());
    }
    return v;
}

}  // namespace

TEST_CASE("SplitMix64: valori di riferimento", "[rng][generatori]") {
    // Sequenza di riferimento di SplitMix64 (Steele, Lea, Flood 2014; splitmix64.c di Vigna) con stato iniziale 0.
    std::uint64_t state = 0;
    CHECK(splitmix64(state) == 0xe220a8397b1dcdafULL);
    CHECK(splitmix64(state) == 0x6e789e6aa1b965f4ULL);
    CHECK(splitmix64(state) == 0x06c45d188009454fULL);
}

TEST_CASE("Un generatore per giocatore, con i semi dal seed della partita (SplitMix64)", "[rng][generatori]") {
    for (const std::uint64_t seed : {1ULL, 2ULL, 12345ULL, 0xffffffffffffffffULL}) {
        std::uint64_t sm = seed;
        const std::uint64_t seed0 = splitmix64(sm), stream0 = splitmix64(sm);
        const std::uint64_t seed1 = splitmix64(sm), stream1 = splitmix64(sm);
        CHECK(player_generator(seed, 0) == Pcg32(seed0, stream0));
        CHECK(player_generator(seed, 1) == Pcg32(seed1, stream1));
        CHECK_FALSE(player_generator(seed, 0) == player_generator(seed, 1));

        // create_game mescola il grimorio di ogni giocatore con il suo generatore, partendo dall'ordine della lista.
        const GameState s = new_game(seed);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            std::vector<CardId> expected;
            for (std::size_t i = 0; i < kDeckSize; ++i) expected.push_back(static_cast<CardId>(p * kDeckSize + i));
            Pcg32 rng = player_generator(seed, p);
            shuffle(std::span<CardId>(expected), rng);
            CHECK(cards_of(s.players[p].library) == expected);
            CHECK(s.players[p].rng == rng);
        }
    }
}

TEST_CASE("Generatori: stesso seed, stessa partita voce per voce", "[generatori]") {
    const MatchupConfig config = matchup_config();
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        GameLog a, b;
        const MatchupGame ga = play_matchup(config, seed, &a);
        const MatchupGame gb = play_matchup(config, seed, &b);
        INFO("seed " << seed);
        CHECK(a.entries == b.entries);
        CHECK(ga.winner == gb.winner);
        CHECK(ga.turns == gb.turns);
    }
    GameLog one, two;
    (void)play_matchup(config, 1, &one);
    (void)play_matchup(config, 3, &two);
    CHECK_FALSE(one.entries == two.entries);
}

TEST_CASE("Generatori: le mani da 7 di un giocatore dipendono solo dal seed, anche dopo il mulligan dell'altro",
          "[generatori][mulligan]") {
    int compared = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        for (const PlayerId starting : {PlayerId{0}, PlayerId{1}}) {
            for (const PlayerId target : {PlayerId{0}, PlayerId{1}}) {
                // Il giocatore `target` fa due mulligan (tre mani da 7); l'altro tiene, oppure fa un mulligan.
                const auto keeps = mulligan_hands(seed, starting, target, 2, 0);
                const auto mulls = mulligan_hands(seed, starting, target, 2, 1);
                INFO("seed " << seed << ", primo " << int(starting) << ", giocatore " << int(target));
                REQUIRE(keeps.size() == 3);
                REQUIRE(mulls.size() == 3);
                CHECK(keeps == mulls);
                ++compared;
            }
        }
    }
    CHECK(compared == 80);
}

TEST_CASE("Generatori: grimorio e pescate di un giocatore non dipendono dalle scelte dell'altro", "[generatori]") {
    // Stesso seed, due giocatori 0 diversi: uno tiene, l'altro fa un mulligan (un mescolamento in piu' del SUO
    // grimorio). Finche' Rakdos sceglie le stesse azioni, il suo grimorio e la sua mano devono essere identici, anche
    // dopo i suoi mescolamenti (mulligan, fetch, ciclo del Troll): li fa con il suo generatore.
    int seeds_with_shuffle = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        const RakdosView a = rakdos_view(seed, false);
        const RakdosView b = rakdos_view(seed, true);
        const std::size_t n = std::min(a.chosen.size(), b.chosen.size());
        REQUIRE(n > 0);
        bool shuffled = false;
        for (std::size_t i = 0; i < n; ++i) {
            INFO("seed " << seed << ", decisione di Rakdos " << i);
            REQUIRE(a.library[i] == b.library[i]);
            REQUIRE(a.hand[i] == b.hand[i]);
            REQUIRE(a.rng[i] == b.rng[i]);
            if (!(a.rng[i] == a.rng[0])) shuffled = true;
            if (!(a.chosen[i] == b.chosen[i])) break;
        }
        if (shuffled) ++seeds_with_shuffle;
    }
    // Il confronto deve coprire davvero dei mescolamenti di Rakdos, non solo le prime decisioni.
    CHECK(seeds_with_shuffle >= 20);
}

TEST_CASE("Generatori: lo stato resta sotto il tetto di 16 KB", "[generatori][state]") {
    INFO("sizeof(GameState) = " << sizeof(GameState) << " byte");
    CHECK(sizeof(GameState) <= 16 * 1024);
    static_assert(std::is_trivially_copyable_v<GameState>);
}
