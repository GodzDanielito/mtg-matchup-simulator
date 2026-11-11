#pragma once

// Agente euristico di UW Blink: le regole di docs/agente_uw.md.
//
// Vede solo ciò che vedrebbe il giocatore: la propria mano, le zone pubbliche, i punti
// vita, l'energia, quante carte ha in mano l'avversario. Non legge mai la mano avversaria
// né l'ordine dei grimori; del proprio grimorio usa solo la composizione (la lista è nota)
// e la carta in cima mentre sorveglia.
//
// Ogni decisione registra la regola che l'ha presa. "predefinito" è solo una rete di
// sicurezza: l'obiettivo (sezione 1) è che non succeda mai.

#include "card_db.hpp"
#include "game.hpp"
#include "rng.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace mtg {

struct UwAgentOptions {
    // Sezione 8: siamo in corsa quando il nostro orologio è entro questi turni (rimbalzo di
    // Ephemerate: in corsa non si rimbalza un attaccante).
    int race_clock = 2;

    // Varianti delle regole (docs/agente_uw.md, sezione 28). Decisione di Daniele, 11/11/2026: T1, W1 e R1
    // sono il comportamento predefinito (base_v2); M1 resta spenta. Restano opzioni, per i test A/B:
    // tutte spente e' la base_v1.
    // R1: nella "mano quasi vuota" del warp di Quantum Riddler non contano Force of Negation e
    // Consign to Memory (nella fase principale non c'è un trigger da contrastare).
    bool riddler_refill_ignores_counters = true;
    // T1: Teferi usa il -3 su qualsiasi bersaglio utile: dopo l'elenco della sezione 17, la prima
    // minaccia avversaria della classifica e la nostra creatura con l'entrata più utile.
    bool teferi_any_target = true;
    // M1: nel mulligan contano come risposta anche Recruiter of the Guard e Witch Enchanter.
    bool mulligan_more_answers = false;
    // W1: Riddler con il warp e Consign sul trigger di esilio (viene prima della ricarica di R1).
    bool riddler_warp_with_consign = true;
};

class UwAgent {
public:
    static constexpr std::string_view kDefaultRule = "predefinito";

    UwAgent(const CardDatabase& db, std::uint64_t seed, UwAgentOptions options = {});

    // Sceglie una delle azioni legali per la decisione in sospeso (del giocatore che decide).
    Action choose(const GameState& s, std::span<const Action> legal);

    // La regola dell'ultima scelta e il conteggio di tutte le scelte per regola.
    [[nodiscard]] std::string_view last_rule() const { return last_rule_; }
    [[nodiscard]] const std::map<std::string, std::uint64_t, std::less<>>& rule_counts() const { return counts_; }

    // Identificativi delle carte che le regole nominano (kNoDef se assenti dal database).
    struct Ids {
        // UW Blink
        CardDefId solitude = kNoDef, ephemerate = kNoDef, phelia = kNoDef, march = kNoDef, thraben = kNoDef,
                  seam_rip = kNoDef, witch = kNoDef, sanctifier = kNoDef, recruiter = kNoDef, shepherd = kNoDef,
                  riddler = kNoDef, teferi = kNoDef, force = kNoDef, consign = kNoDef, guide = kNoDef, ocelot = kNoDef,
                  cat = kNoDef;
        CardDefId plains = kNoDef, fountain = kNoDef, archive = kNoDef;
        // Rakdos Delirium (le carte avversarie che le regole nominano)
        CardDefId nethergoyf = kNoDef, moonshadow = kNoDef, manipulator = kNoDef, supplier = kNoDef, fomo = kNoDef,
                  phoenix = kNoDef, wraith = kNoDef, troll = kNoDef, bolt = kNoDef, flare = kNoDef,
                  thoughtseize = kNoDef, bauble = kNoDef;
    };

private:
    struct Choice {
        Action action;
        std::string_view rule;
    };
    Choice decide(const GameState& s, std::span<const Action> legal);
    std::size_t pick(std::size_t n);  // pareggio: indice casuale con il generatore dell'agente

    const CardDatabase* db_;
    Ids ids_;
    UwAgentOptions options_;
    Pcg32 rng_;
    std::string_view last_rule_ = kDefaultRule;
    std::map<std::string, std::uint64_t, std::less<>> counts_;

    // Piano del lancio in corso: le scelte decise quando si è scelto di lanciare.
    struct Plan {
        CardDefId def = kNoDef;
        Target target;
        std::uint8_t mode = 0;
        std::uint8_t x = 0;
        CardId pitch = kNoCard;     // carta da esiliare dalla mano come costo (evocazione, March)
        std::uint16_t stack_id = 0;  // bersaglio in pila (Force of Negation, Consign to Memory): il suo id
    };
    Plan plan_;
    // Blocco in due in corso (sezione 21): il secondo bloccante da aggiungere a quell'attaccante.
    CardId block_attacker_ = kNoCard;
    CardId block_second_ = kNoCard;

    friend struct UwRules;  // le regole, in uw_agent.cpp
};

}  // namespace mtg
