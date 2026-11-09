#pragma once

// Agente euristico di Rakdos Delirium: le regole di docs/agente_rakdos.md.
//
// Vede solo ciò che vedrebbe il giocatore: la propria mano, le zone pubbliche, i
// punti vita, quante carte ha in mano l'avversario e le carte rivelate (le opzioni
// di Thoughtseize). Non legge mai la mano avversaria né l'ordine dei grimori; del
// proprio grimorio usa solo la composizione (la lista è nota) e la carta in cima
// mentre sorveglia.
//
// Ogni decisione registra la regola che l'ha presa, oppure "predefinito" quando
// nessuna regola si applica (sezione 14: passa se può, altrimenti la prima opzione).

#include "card_db.hpp"
#include "game.hpp"
#include "rng.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

// Mulligan con una sola terra (docs/agente_rakdos.md, sezione 2).
//  A: una terra, sempre mulligan (solo come confronto).
//  B: si tiene con 7 o 6 carte se ci sono almeno due carte tra Street Wraith, Mishra's Bauble e
//     Troll of Khazad-dûm e una creatura da un mana lanciabile con quella terra (predefinita).
//  C: come B, ma basta una di quelle carte.
enum class MulliganVariant : std::uint8_t { A, B, C };

struct RakdosAgentOptions {
    // Variante di confronto del goldfish (sezione 16): Lightning Bolt in faccia appena
    // possibile quando non c'è una creatura da colpire, invece di aspettare i 6 punti vita.
    bool bolt_face_asap = false;
    MulliganVariant mulligan = MulliganVariant::B;
};

class RakdosAgent {
public:
    static constexpr std::string_view kDefaultRule = "predefinito";

    RakdosAgent(const CardDatabase& db, std::uint64_t seed, RakdosAgentOptions options = {});

    // Sceglie una delle azioni legali per la decisione in sospeso (del giocatore che decide).
    Action choose(const GameState& s, std::span<const Action> legal);

    // La regola dell'ultima scelta e il conteggio di tutte le scelte per regola.
    [[nodiscard]] std::string_view last_rule() const { return last_rule_; }
    [[nodiscard]] const std::map<std::string, std::uint64_t, std::less<>>& rule_counts() const { return counts_; }

    // Identificativi delle carte che le regole nominano (kNoDef se assenti dal database).
    struct Ids {
        CardDefId nethergoyf = kNoDef, moonshadow = kNoDef, manipulator = kNoDef, supplier = kNoDef,
                  fomo = kNoDef, phoenix = kNoDef, wraith = kNoDef, troll = kNoDef, flare = kNoDef,
                  bolt = kNoDef, thoughtseize = kNoDef, bauble = kNoDef;
        CardDefId swamp = kNoDef, blood_crypt = kNoDef, blackcleave = kNoDef, theater = kNoDef;
        CardDefId sanctifier = kNoDef, solitude = kNoDef, thraben_charm = kNoDef, march = kNoDef, teferi = kNoDef,
                  ocelot = kNoDef, guide = kNoDef, phelia = kNoDef, seam_rip = kNoDef, riddler = kNoDef,
                  ephemerate = kNoDef, force = kNoDef, witch = kNoDef, shepherd = kNoDef, consign = kNoDef,
                  recruiter = kNoDef, cat = kNoDef;
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
    RakdosAgentOptions options_;
    Pcg32 rng_;
    std::string_view last_rule_ = kDefaultRule;
    std::map<std::string, std::uint64_t, std::less<>> counts_;

    // Piano del lancio in corso: il bersaglio deciso quando si è scelto di lanciare.
    struct Plan {
        CardDefId def = kNoDef;
        Target target;
    };
    Plan plan_;

    // Blocco in due in corso (sezione 8): il secondo bloccante da aggiungere a quell'attaccante.
    CardId block_attacker_ = kNoCard;
    CardId block_second_ = kNoCard;

    friend struct RakdosRules;  // le regole, in rakdos_agent.cpp
};

}  // namespace mtg
