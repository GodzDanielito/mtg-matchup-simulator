#pragma once

// Confronto di varianti delle regole di un agente sugli stessi seed (docs/agente_uw.md, sezione 28).
//
// Con gli stessi seed le due versioni giocano gli stessi grimori e le stesse mani: la differenza di
// win rate si stima partita per partita (differenza appaiata), con un intervallo molto piu' stretto
// del confronto tra due campioni indipendenti.

#include "card_db.hpp"
#include "game.hpp"
#include "matchup.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mtg {

// Differenza appaiata: media di (variante - base) su esiti 0/1 dello stesso seed, con l'intervallo di
// confidenza al 95% (approssimazione normale: media +- 1,96 * deviazione standard campionaria / sqrt(n)).
struct PairedDiff {
    long long n = 0;
    double mean = 0.0;
    double low = 0.0;
    double high = 0.0;
};
[[nodiscard]] PairedDiff paired_difference(const std::vector<int>& base, const std::vector<int>& variant);

// Le ricariche di Quantum Riddler in una partita: i lanci con il warp presi dalla regola "Quantum
// Riddler: warp come ricarica" del giocatore 0 (UW) e le carte che il giocatore 0 pesca dopo, fino
// alla sua giocata successiva (lancio, terra, attivazione) o all'inizio del turno dopo. In pratica e'
// la pescata del trigger di entrata di Riddler, con il +1 a mano quasi vuota.
struct RefillStats {
    long long casts = 0;
    long long cards = 0;
};
[[nodiscard]] RefillStats count_refill_warps(const GameLog& log, const std::map<std::size_t, std::string>& rules);

// La variante W1 in una partita, fino all'inizio del turno dopo ogni lancio (o alla fine della partita):
// - casts: i lanci di Quantum Riddler con la regola di W1 del giocatore 0 (UW);
// - consign_cast: Consign to Memory lanciato con la regola "l'esilio del warp";
// - saved: Consign ha davvero neutralizzato l'esilio: lanciato, e Riddler mai uscito dal campo;
// - stayed: Riddler in campo al turno dopo con qualsiasi mezzo (Consign, Ephemerate, Phelia).
struct W1Stats {
    long long casts = 0;
    long long consign_cast = 0;
    long long saved = 0;
    long long stayed = 0;

    W1Stats& operator+=(const W1Stats& o);
};
[[nodiscard]] W1Stats count_w1(const GameLog& log, const std::map<std::size_t, std::string>& rules);

// Teferi in una partita (variante X3 di Rakdos, docs/agente_rakdos.md, sezione 21):
// - cast: i lanci di Teferi del giocatore 0 (UW);
// - race_turns, long_turns: i turni di Rakdos (giocatore 1 attivo) in cui la prima decisione di Rakdos
//   con una modalita' e' " [corsa]" (lo ignora) o " [partita lunga]" (lo tratta da togliere);
// - removed: Teferi dal campo al cimitero (succede solo per mano di Rakdos: Bolt, attacco, Flare).
struct TeferiStats {
    long long cast = 0;
    long long race_turns = 0;
    long long long_turns = 0;
    long long removed = 0;

    TeferiStats& operator+=(const TeferiStats& o);
};
[[nodiscard]] TeferiStats count_teferi(const GameLog& log, const std::map<std::size_t, std::string>& rules,
                                       CardDefId teferi);

// Thoughtseize nel primo turno di Rakdos (giocatore 1; variante X4): lanciata o no, e la carta presa
// dalla mano di UW (kNoDef se non ha preso nulla).
struct FirstTurnSeize {
    bool cast = false;
    CardDefId taken = kNoDef;
};
[[nodiscard]] FirstTurnSeize first_turn_thoughtseize(const GameLog& log, CardDefId thoughtseize);

// Turni iniziati del giocatore `p` (1 = il suo primo turno; nel turno avversario che segue resta lo stesso).
[[nodiscard]] int turns_started(const GameState& s, PlayerId p);

// --- Osservatori del seguito dei test A/B di Rakdos (docs/agente_rakdos.md, sezione 21) ----------
// Un osservatore per partita: si crea nuovo prima di ogni partita. Ricevono lo stato in sola lettura.

// Le prime giocate di Rakdos (giocatore 1), per la robustezza di X1:
// - lands_by_turn2, lands_by_turn3: il massimo di terre di Rakdos in campo fino all'inizio del suo
//   terzo (quarto) turno, cioe' entro la fine del suo turno 2 (3) e del turno di UW che segue;
// - spells_before_turn3: le magie lanciate da Rakdos (in qualsiasi turno) prima dell'inizio del suo
//   terzo turno. Terre, fetch e cicli non sono magie lanciate.
struct EarlyStats {
    int lands_by_turn2 = 0;
    int lands_by_turn3 = 0;
    int spells_before_turn3 = 0;
};

class EarlyRakdosTracker final : public MatchupObserver {
public:
    explicit EarlyRakdosTracker(const CardDatabase& db) : db_(&db) {}
    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override;
    void on_end(const GameState& s) override;
    [[nodiscard]] const EarlyStats& stats() const { return stats_; }

private:
    void see(const GameState& s);
    const CardDatabase* db_;
    EarlyStats stats_;
};

// Verifica della regola del turno 1 di Rakdos (sezione 6): Thoughtseize in mano a una priorita' di
// Rakdos nelle fasi principali del suo primo turno, e perche' non e' stata lanciata.
enum class SeizeT1 : std::uint8_t {
    NotInHand,         // mai in mano a quelle priorita'
    Cast,              // lanciata nel primo turno
    CreatureCastable,  // non lanciata, ma una creatura da un mana era lanciabile (viene prima)
    NoBlackSource,     // il lancio non e' mai legale e non c'e' mai una fonte nera stappata
    LifeFloor,         // lanciabile, ma ogni volta i 2 punti vita porterebbero sotto i 5
    TrollNoSecondLand, // ciclo del Troll al suo posto, senza un'altra terra in mano (sezione 22)
    BoltOnSight,       // Bolt al suo posto, con Guide of Souls o Ocelot Pride di UW in campo (sezione 22)
    NoValidReason,     // lanciabile sopra la soglia di vita, o una fonte nera stappata senza lancio legale
};
[[nodiscard]] const char* seize_t1_name(SeizeT1 r);

class FirstTurnSeizeTracker final : public MatchupObserver {
public:
    explicit FirstTurnSeizeTracker(const CardDatabase& db);
    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override;
    void on_end(const GameState& s) override { see_hand(s); }
    [[nodiscard]] SeizeT1 result() const;
    // Per NoValidReason: l'azione scelta all'ultima priorita' in cui Thoughtseize era lanciabile ("passa",
    // "lancia X", "attiva X", "terra X").
    [[nodiscard]] const std::string& detail() const { return detail_; }

private:
    // Thoughtseize in mano dal primo turno di Rakdos fino al turno di UW che segue, mulligan esclusi. Senza
    // mana il motore non da' la priorita' a Rakdos (i passaggi automatici non sono decisioni): la mano si
    // guarda a ogni stato osservato, di chiunque sia la decisione, e alla fine.
    void see_hand(const GameState& s);
    const CardDatabase* db_;
    CardDefId seize_, troll_, bolt_, guide_, ocelot_;
    bool in_hand_ = false, cast_ = false, one_drop_ = false, legal_ = false, legal_above_floor_ = false,
         black_ = false;
    SeizeT1 reason_ = SeizeT1::NoValidReason;  // dell'ultima priorita' lanciabile sopra la soglia di vita
    std::string detail_;
};

// Le carte prese da Thoughtseize di Rakdos in una partita, e se almeno una volta la scelta e' diversa
// da quella che l'agente di base_r1 (X5 spenta) farebbe nello stesso stato (variante X5).
class SeizePickTracker final : public MatchupObserver {
public:
    explicit SeizePickTracker(const CardDatabase& db) : db_(&db) {}
    void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) override;
    void on_end(const GameState&) override {}
    [[nodiscard]] const std::map<CardDefId, long long>& picks() const { return picks_; }
    [[nodiscard]] bool changed() const { return changed_; }

private:
    const CardDatabase* db_;
    std::map<CardDefId, long long> picks_;
    bool changed_ = false;
};

}  // namespace mtg
