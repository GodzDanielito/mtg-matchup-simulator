# Proposta: stato di gioco del motore

Stato: **bozza da rivedere**. Nessuna riga del motore è ancora scritta: questo documento serve a decidere la forma dei dati prima del codice.

## 1. Vincoli che guidano le scelte

Tre requisiti della specifica ([progetto.md](progetto.md)) pesano su ogni scelta:

1. **Copia velocissima.** L'ISMCTS copia lo stato migliaia di volte per decisione. Lo stato deve copiarsi come un blocco di memoria contiguo, senza puntatori da ricollegare e senza allocazioni sparse.
2. **Riproducibilità.** Stesso seed e stesse scelte devono dare la stessa partita, su qualsiasi compilatore. Tutta la casualità vive dentro lo stato.
3. **Cimitero esatto e interrogabile subito.** Delirium, forza di Nethergoyf e prove per Detective's Phoenix vengono letti continuamente: nelle azioni legali, nel combattimento e nelle azioni di stato.

A questi se ne aggiunge uno implicito: **gli agenti non vedono lo stato completo**. Lo stato è una "vista divina"; l'agente riceve una vista filtrata (sezione 9).

## 2. Definizione della carta e istanza in partita

Separo nettamente **ciò che una carta è** da **ciò che le succede in questa partita**.

### Definizione (`CardDef`): immutabile, condivisa

Una per nome di carta, creata all'avvio dal modulo in `cards/` e mai modificata. Non fa parte dello stato e quindi non si copia.

```cpp
struct CardDef {
    CardDefId id;                 // indice nella tabella globale (uint16)
    std::string_view name;        // nome Oracle
    ManaCost cost;                // simboli di mana del costo
    uint8_t mana_value;
    TypeMask types;               // bit: Artifact, Creature, Enchantment, Instant, Land, ...
    SubtypeMask subtypes;         // solo quelli che servono nel matchup (Cat, Nightmare, ...)
    ColorMask colors;
    int8_t base_power, base_toughness;
    std::span<const Ability> abilities;  // dichiarate dal modulo della carta
    const CardDef* back_face;     // MDFC (Witch Enchanter); nullptr se non bifronte
};
```

### Istanza (`CardObject`): mutabile, dentro lo stato

Un record per ogni carta fisica della partita (60 + 60) più gli slot per i token (i gatti di Ocelot Pride).

```cpp
struct CardObject {
    CardDefId def;
    PlayerId owner, controller;
    Zone zone;
    uint16_t incarnation;   // aumenta a ogni cambio di zona (vedi sotto)
    uint16_t timestamp;     // per l'ordine degli effetti continui
    uint8_t flags;          // tappata, debolezza da evocazione, faccia girata, token...
    uint8_t damage;
    int8_t plus_counters;   // segnalini +1/+1 (Moonshadow, Guide of Souls)
    ObjRef attached_to;     // Detective's Phoenix conferito
    uint8_t known_to;       // bit per giocatore: chi conosce l'identità di questa carta
};
```

La forza e la costituzione **non si salvano**: si calcolano quando servono, a partire da definizione, segnalini e stato (Nethergoyf legge il cimitero). Così non c'è una cache da tenere sincronizzata.

### Identità: carta fisica e oggetto di regole

La regola 400.7 dice che una carta che cambia zona diventa un **oggetto nuovo**, senza memoria del precedente. Ci si appoggiano metà delle interazioni del matchup:

- Solitude in pila perde il bersaglio se Ephemerate fa sparire e ritornare la creatura.
- Phelia fa tornare "quella carta", ma è un oggetto nuovo, senza segnalini e senza danni.
- Detective's Phoenix torna quando muore *la creatura incantata*, non un oggetto successivo.

Distinguo quindi due cose:

| Concetto | Rappresentazione | Stabile? | Serve a |
| --- | --- | --- | --- |
| Carta fisica | `CardId` (indice 0..N nella tabella) | Sempre | Registro, metriche GIH/OH/GD, tracciare la carta per tutta la partita |
| Oggetto di regole | `ObjRef = { CardId, incarnation }` | Fino al prossimo cambio di zona | Bersagli, collegamenti, trigger ritardati |

Un `ObjRef` è valido solo se `objects[card].incarnation == ref.incarnation`. Il controllo "il bersaglio esiste ancora?" diventa un confronto tra due interi, senza liste di oggetti morti.

**Alternative scartate**

- *Una classe per carta con metodi virtuali, istanze allocate con `new`.* È naturale da scrivere, ma per copiare lo stato bisogna clonare ogni oggetto e ricollegare tutti i puntatori. Per l'MCTS è troppo lento e porta facilmente a errori.
- *Tutte le informazioni della carta copiate nell'istanza.* Si risparmia un passaggio nella lettura, ma ogni copia dello stato si porta dietro nomi, costi e abilità che non cambiano mai.
- *Un nuovo ID per ogni cambio di zona, ricavato da un contatore globale.* È corretto ma la tabella cresce senza limite durante la partita e il legame con la carta fisica, che serve alle metriche, va ricostruito a parte.

## 3. Memoria: tabelle piatte e indici

```cpp
struct GameState {
    std::array<CardObject, kMaxObjects> objects;  // 120 carte + ~16 slot token
    std::array<PlayerState, 2> players;
    Stack stack;
    TurnState turn;
    PendingDecision pending;     // la domanda che il motore sta facendo (sezione 7)
    DelayedTriggers delayed;     // Phelia, warp, ...
    Rng rng;                     // sezione 8
};
```

Dentro lo stato non c'è nessun puntatore, solo indici. Quindi `GameState` è *trivially copyable* e una copia è un semplice `memcpy`. A spanne: 136 oggetti × ~16 byte più le zone e il resto fa circa 4-6 KB, cioè meno di un microsecondo per copia.

Le zone sono contenitori a capacità fissa, per esempio `StaticVector<CardId, 60>`, con lunghezza e dati inline, così anche loro si copiano in blocco.

**Alternativa scartata.** `std::vector` dentro lo stato: ogni copia diventerebbe una serie di allocazioni. I limiti massimi sono noti (60 carte per mazzo), quindi la capacità fissa non costa nulla.

## 4. Zone

| Zona | Per giocatore? | Ordinata? | Note |
| --- | --- | --- | --- |
| Grimorio | Sì | Sì (cima = fondo dell'array, pescata in O(1)) | Nascosto; per l'ISMCTS l'ordine non noto si rimescola |
| Mano | Sì | No | Nascosta, tranne le carte con `known_to` (Thoughtseize) |
| Campo | No, unico con `controller` per oggetto | No | Il controllo può cambiare (Phelia ritorna "sotto il tuo controllo") |
| Cimitero | Sì | Sì | Pubblico; ha il riepilogo incrementale (sezione 6) |
| Esilio | No, unico | No | Ogni carta ha un "motivo" per le carte collegate (warp, Phelia, Consign) |
| Pila | No, unica | Sì | Contiene sia le carte lanciate sia le abilità (sezione 5) |

**Un solo punto di ingresso per i cambi di zona.** Tutte le carte si muovono con `move_object(state, ref, to_zone, ...)`. Quella funzione è l'unica che:

- toglie la carta dalla zona di partenza e la mette in quella di arrivo;
- aumenta `incarnation`;
- aggiorna il riepilogo del cimitero;
- genera gli eventi per i trigger (entrata in campo, morte, "una carta finisce nel tuo cimitero" per Moonshadow);
- scrive la riga nel registro JSON Lines.

Il rischio principale indicato nella specifica, un cimitero tracciato male, si concentra così in una sola funzione invece di essere sparso in 43 carte.

## 5. Pila e trigger

```cpp
struct StackItem {
    StackKind kind;          // Spell, ActivatedAbility, TriggeredAbility
    ObjRef source;           // la carta lanciata o la fonte dell'abilità
    PlayerId controller;
    uint8_t ability_index;   // quale abilità della definizione
    Targets targets;         // array fisso di ObjRef / PlayerId
    uint8_t x_value;         // March of Otherworldly Light
    uint8_t mode;            // Thraben Charm
    CastMethod method;       // normale, evocazione, gratis (FoN), warp, costo alternativo (Flare)
};
```

- Una magia in pila è anche un oggetto in zona `Stack`; un'abilità non è una carta ed esiste solo come `StackItem`.
- Alla risoluzione si ricontrollano i bersagli con `ObjRef`; se nessuno è più legale, la magia o l'abilità viene neutralizzata.
- Il **metodo di lancio** resta sull'oggetto, perché cambia cosa succede dopo: Solitude evocata si sacrifica, Quantum Riddler lanciato col warp va esiliato alla fine del turno.

**Trigger.** Gli eventi generati da `move_object` e dal combattimento creano trigger *in attesa*. Prima di dare la priorità, il motore li mette in pila nell'ordine APNAP: prima quelli del giocatore attivo, poi quelli dell'altro. Se un giocatore ha più trigger insieme, l'ordine diventa una **decisione dell'agente**: con Phelia, Ephemerate e i trigger di entrata l'ordine conta.

**Trigger ritardati** (fine turno di Phelia, esilio del warp, ritorno di Detective's Phoenix): stanno in una lista a parte con la condizione di attivazione (`NextEndStep`, `WhenDies(ObjRef)`) e l'oggetto collegato.

**Alternativa scartata.** Ordinare i trigger in automatico è più semplice, ma su Phelia ed Ephemerate darebbe risultati sbagliati proprio nelle linee che vogliamo misurare.

## 6. Il cimitero

È la struttura più importante contro Rakdos Delirium. Ogni cimitero ha, oltre alla lista ordinata delle carte, un **riepilogo aggiornato a ogni ingresso e uscita**:

```cpp
struct GraveyardSummary {
    uint8_t card_count;
    uint16_t total_mana_value;                 // prove (Detective's Phoenix)
    std::array<uint8_t, kNumCardTypes> per_type;  // quante carte per tipo
    TypeMask present;                          // bit acceso se per_type[t] > 0

    int distinct_types() const { return std::popcount(present); }  // forza di Nethergoyf
    bool delirium() const { return distinct_types() >= 4; }
};
```

**Perché i contatori per tipo e non solo i bit.** Con i soli bit, quando esce un Lightning Bolt non sai se c'è ancora un altro istantaneo. Con un contatore per tipo l'uscita è un semplice decremento, e il bit si spegne solo quando il contatore arriva a zero. Una carta con più tipi incrementa ogni suo tipo: Fear of Missing Out conta come incantesimo e creatura, il che è decisivo per il delirium.

**Le domande poste al cimitero e il loro costo.**

| Domanda | Risposta | Costo |
| --- | --- | --- |
| Quante carte? | `card_count` | O(1) |
| Delirium? | `popcount(present) >= 4` | O(1) |
| Forza di Nethergoyf | `popcount(present)` | O(1) |
| Posso raccogliere prove 6? | `total_mana_value >= 6` | O(1) |
| *Quali* carte esilio per le prove? | Scelta dell'agente tra i sottoinsiemi validi | Si enumera solo quando la decisione serve |

**Bifronti nel cimitero.** Fuori da campo e pila, una MDFC ha solo le caratteristiche del fronte (Witch Enchanter conta come creatura). Il riepilogo usa sempre la definizione del fronte.

**Controllo di coerenza.** Nei test, e in Debug dopo ogni azione, si ricalcola il riepilogo da zero scorrendo il cimitero e lo si confronta con quello incrementale. È la mitigazione "controllo del cimitero dopo ogni azione nei test" chiesta dalla specifica.

**Alternative scartate**

- *Ricalcolare tutto a ogni domanda.* Il cimitero ha al massimo qualche decina di carte, quindi non sarebbe drammatico. Ma la forza di Nethergoyf si legge di continuo (azioni di stato, ogni blocco, ogni azione legale), e il ricalcolo da zero lo teniamo comunque come verifica nei test.
- *Cache con flag "da ricalcolare".* Aggiunge una condizione in ogni lettura e il rischio di dimenticare di invalidarla. L'aggiornamento incrementale in un solo punto (`move_object`) è più semplice e più verificabile.

## 7. Turno, fasi, step e priorità

```cpp
enum class Step : uint8_t {
    Untap, Upkeep, Draw,
    Main1,
    BeginCombat, DeclareAttackers, DeclareBlockers, CombatDamage, EndCombat,
    Main2,
    End, Cleanup
};

struct TurnState {
    uint16_t turn_number;
    PlayerId active_player;
    PlayerId priority_player;
    Step step;
    uint8_t passes_in_a_row;       // 2 = entrambi hanno passato
    uint8_t extra_combats;         // Fear of Missing Out con delirium
    uint8_t lands_played;
    bool game_over; PlayerId winner;
};
```

- **Priorità.** Prima di dare la priorità il motore controlla le azioni di stato e mette in pila i trigger. Quando entrambi passano di fila, si risolve la cima della pila; se la pila è vuota, si passa allo step successivo.
- **Combattimento aggiuntivo.** A `EndCombat`, se `extra_combats > 0`, si decrementa e si torna a `BeginCombat` invece di andare in `Main2`.
- **Niente step dei danni da attacco improvviso.** Nel pool non ci sono attacco improvviso né doppio attacco, quindi lo step si omette. Se una carta futura li porta, si aggiunge.

### Il motore fa domande, non chiama gli agenti

Questa è la scelta architetturale più importante. Il motore è una **macchina a stati che si ferma a ogni decisione**:

```cpp
std::vector<Action> legal_actions(const GameState&);
void apply(GameState&, const Action&);   // avanza fino alla decisione successiva
```

`PendingDecision` dice cosa si sta chiedendo e a chi: priorità, bersagli, attaccanti, bloccanti, mulligan, carte da mettere in fondo, scarti, ordine dei trigger, carte da esiliare per le prove. Tra una decisione e l'altra il motore avanza da solo.

**Alternativa scartata: il motore chiama l'agente** (`agent->choose(...)` dentro la risoluzione di una carta). È più naturale da scrivere, ma il "punto in cui siamo" della risoluzione resta sullo stack delle chiamate C++, che non si può copiare. L'MCTS deve poter fotografare lo stato a ogni decisione e ripartire da lì, quindi tutto ciò che serve per riprendere deve stare dentro `GameState`. Il costo è che le risoluzioni con scelte a metà (Fear of Missing Out: scarta, poi pesca) vanno spezzate in passi espliciti.

## 8. Giocatori e casualità

```cpp
struct PlayerState {
    int16_t life;
    uint8_t energy;              // Guide of Souls
    ManaPool pool;               // W U B R G C
    StaticVector<CardId, 60> library, hand, graveyard;
    GraveyardSummary gy;
    uint8_t mulligans;
    uint8_t spells_cast_this_turn;
    bool drew_from_empty_library;
};
```

**Generatore casuale.** Uso un generatore piccolo (PCG32 o xoshiro128++: 8-16 byte) scritto nel progetto, dentro `GameState`, al posto di `std::mt19937` più `std::uniform_int_distribution`, per due motivi:

- `std::mt19937` occupa circa 2,5 KB e raddoppierebbe la dimensione dello stato da copiare;
- le distribuzioni della libreria standard **non danno gli stessi numeri** con MSVC, GCC e Clang: la stessa partita darebbe risultati diversi su Windows e su Linux. Mescolamento e intervalli si implementano a mano (Fisher-Yates, metodo di Lemire).

## 9. Informazione nascosta

Lo stato contiene tutto. Gli agenti ricevono una vista (`Observation`) che contiene solo:

- le zone pubbliche;
- la propria mano;
- il numero di carte in mano e nel grimorio dell'avversario;
- le carte con il proprio bit acceso in `known_to`.

Thoughtseize, per esempio, accende il bit su tutta la mano dell'avversario. Per l'ISMCTS la "determinizzazione" parte dalla vista: si redistribuiscono le carte non note tra mano e grimorio dell'avversario, rispettando ciò che si sa.

## 10. Cosa resta fuori da questa proposta

- **Effetti continui e livelli (regola 613).** Nel pool sono pochi: Nethergoyf (forza definita dalla caratteristica), Teferi (lancio solo a velocità stregoneria), il conferimento di Phoenix. Propongo di calcolarli al volo in una funzione dedicata, senza un sistema di livelli generico, e di rivederla se le carte lo richiedono.
- **Mana e pagamento dei costi.** Riguarda le azioni legali più che lo stato. Va deciso a parte, compresi Street Wraith (ciclo pagando vita) e i costi alternativi.

## Domande per la revisione

1. **Token.** I 16 slot fissi per i token (li crea solo UW Blink) bastano per i gatti di Ocelot Pride, o servono più spazio o una gestione diversa?
2. **Ordine del cimitero.** Lo tengo ordinato anche se nel pool nessuna carta lo usa: costa poco e il registro diventa più leggibile. Va bene?
3. **Step con priorità saltati.** Per velocità propongo di passare in automatico la priorità quando l'unica azione legale è "passa", registrandolo comunque nel log. È accettabile per gli agenti livello 1-2, o vuoi ogni passaggio esplicito?
4. **Mockingbird e Aang.** La specifica suggerisce di sostituirli temporaneamente. Mockingbird (copia di una creatura) metterebbe alla prova la separazione tra definizione e istanza: la copia ha una "definizione efficace" diversa da quella stampata. Lo rimandiamo, come suggerito?
