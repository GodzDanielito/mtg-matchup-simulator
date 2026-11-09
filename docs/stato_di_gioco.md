# Proposta: stato di gioco del motore

Stato: **rivista l'8/11/2026, correzioni integrate**. Nessuna riga del motore è ancora scritta: questo documento fissa la forma dei dati prima del codice. In fondo c'è l'elenco delle decisioni prese in revisione.

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
    SubtypeMask subtypes;         // solo quelli che servono nel matchup (Cat, Nightmare, Aura, ...)
    ColorMask colors;
    int8_t base_power, base_toughness;
    std::span<const Ability> abilities;  // dichiarate dal modulo della carta
    const CardDef* back_face;     // MDFC (Witch Enchanter); nullptr se non bifronte
};
```

### Istanza (`CardObject`): mutabile, dentro lo stato

Un record per ogni carta fisica della partita (60 + 60) più 32 slot per i token (sezione 3).

```cpp
struct CardObject {
    CardDefId def;
    PlayerId owner, controller;
    Zone zone;
    uint16_t incarnation;      // aumenta a ogni cambio di zona e a ogni riuso dello slot
    uint16_t timestamp;        // per l'ordine degli effetti continui
    uint16_t flags;            // tappata, debolezza da evocazione, token, conferita,
                               // ha già attaccato in questo turno (Fear of Missing Out), ...
    uint8_t damage;
    Counters counters;         // segnalini, sotto
    ObjRef attached_to;        // Aura (Detective's Phoenix conferito)
    uint8_t known_to;          // bit per giocatore: chi conosce l'identità della carta
};

struct Counters {
    uint8_t plus_one;          // +1/+1 (Guide of Souls, Phelia)
    uint8_t minus_one;         // -1/-1 (Moonshadow entra con sei)
    KeywordCounters keywords;  // segnalini abilità: un contatore per parola chiave
};

enum class KeywordCounter : uint8_t { Flying, /* altri se servono */ Count };
using KeywordCounters = std::array<uint8_t, size_t(KeywordCounter::Count)>;
```

**Due contatori separati per +1/+1 e -1/-1**, invece di un unico intero con segno. Le regole li trattano come segnalini diversi, e l'azione di stato 704.5q li annulla a vicenda: se un permanente ha N segnalini +1/+1 e M segnalini -1/-1, se ne rimuovono min(N, M) di ciascun tipo. La distinzione conta per Moonshadow: la sua abilità guarda se ha "un segnalino -1/-1", e con un intero unico non si saprebbe.

**I segnalini abilità** (Guide of Souls dà un segnalino volare) sono contatori, non semplici bit. Due segnalini volare danno volare una volta sola, ma se ne viene rimosso uno l'altro resta.

La forza e la costituzione **non si salvano**: si calcolano quando servono, a partire da definizione, segnalini e stato (Nethergoyf legge il cimitero). Così non c'è una cache da tenere sincronizzata.

### Identità: carta fisica e oggetto di regole

La regola 400.7 dice che una carta che cambia zona diventa un **oggetto nuovo**, senza memoria del precedente. Molte interazioni del matchup dipendono da questo:

- Solitude in pila perde il bersaglio se Ephemerate fa sparire e ritornare la creatura.
- Phelia esilia un permanente che torna come oggetto nuovo, senza segnalini e senza danni.
- Detective's Phoenix conferito resta "attaccato" a un oggetto preciso: se la creatura incantata lascia il campo, `attached_to` non è più valido e la Phoenix smette di essere un'Aura (sezione 5).

Distinguo quindi due cose:

| Concetto | Rappresentazione | Stabile? | Serve a |
| --- | --- | --- | --- |
| Carta fisica | `CardId` (indice nella tabella) | Sempre (per i token: finché lo slot non viene riusato) | Registro, metriche GIH/OH/GD, tracciare la carta per tutta la partita |
| Oggetto di regole | `ObjRef = { CardId, incarnation }` | Fino al prossimo cambio di zona | Bersagli, collegamenti, trigger ritardati |

Un `ObjRef` è valido solo se `objects[card].incarnation == ref.incarnation`. Il controllo "il bersaglio esiste ancora?" diventa un confronto tra due interi, senza liste di oggetti morti.

**Alternative scartate**

- *Una classe per carta con metodi virtuali, istanze allocate con `new`.* È naturale da scrivere, ma per copiare lo stato bisogna clonare ogni oggetto e ricollegare tutti i puntatori. Per l'MCTS è troppo lento e porta facilmente a errori.
- *Tutte le informazioni della carta copiate nell'istanza.* Si risparmia un passaggio nella lettura, ma ogni copia dello stato si porta dietro nomi, costi e abilità che non cambiano mai.
- *Un nuovo ID per ogni cambio di zona, ricavato da un contatore globale.* È corretto ma la tabella cresce senza limite durante la partita e il legame con la carta fisica, che serve alle metriche, va ricostruito a parte.

## 3. Memoria: tabelle piatte e indici

```cpp
inline constexpr size_t kMaxCards  = 120;  // 60 + 60
inline constexpr size_t kMaxTokens = 32;
inline constexpr size_t kMaxObjects = kMaxCards + kMaxTokens;

struct GameState {
    std::array<CardObject, kMaxObjects> objects;
    std::array<PlayerState, 2> players;
    Stack stack;
    TurnState turn;
    PendingDecision pending;     // la domanda che il motore sta facendo (sezione 7)
    DelayedTriggers delayed;     // Phelia, warp
    Rng rng;                     // sezione 8
};
```

Dentro lo stato non c'è nessun puntatore, solo indici. Quindi `GameState` è *trivially copyable* e una copia è un semplice `memcpy`. A spanne: 152 oggetti × ~20 byte più le zone e il resto fa circa 5-7 KB, cioè meno di un microsecondo per copia.

Le zone sono contenitori a capacità fissa, per esempio `StaticVector<CardId, 60>`, con lunghezza e dati inline, così anche loro si copiano in blocco.

### Token: 32 slot riciclabili

- I token occupano gli slot da 120 a 151. Quando un token smette di esistere (cimitero, esilio, mano: azione di stato 704.5d), lo slot torna libero.
- Riusando uno slot, `incarnation` **sale** invece di ripartire da zero. Così un vecchio `ObjRef` al token morto non può mai puntare per sbaglio al token nuovo nello stesso slot.
- Se i 32 slot finiscono, il motore lancia un **errore esplicito** (`EngineLimitError`), mai una sovrascrittura silenziosa. Nei test è un fallimento, nelle simulazioni la partita viene scartata e registrata come anomalia.
- Un `uint16_t` di incarnation basta: ci vorrebbero 65.536 riusi dello stesso slot in una sola partita per tornare a un valore già visto.

**Alternativa scartata.** `std::vector` dentro lo stato: ogni copia diventerebbe una serie di allocazioni. I limiti massimi sono noti, quindi la capacità fissa non costa nulla.

## 4. Zone e cambi di zona

| Zona | Per giocatore? | Ordinata? | Note |
| --- | --- | --- | --- |
| Grimorio | Sì | Sì (cima = fondo dell'array, pescata in O(1)) | Nascosto; per l'ISMCTS l'ordine non noto si rimescola |
| Mano | Sì | No | Nascosta, tranne le carte con `known_to` (Thoughtseize) |
| Campo | No, unico con `controller` per oggetto | No | Il controllo può differire dal proprietario |
| Cimitero | Sì | **Sì** (deciso) | Pubblico; ha il riepilogo incrementale (sezione 6). L'ordine rende il registro leggibile come un replay |
| Esilio | No, unico | No | Ogni carta ha un "motivo" per le carte collegate (warp, Phelia, Consign) |
| Pila | No, unica | Sì | Contiene sia le carte lanciate sia le abilità (sezione 5) |

### Un solo punto di ingresso, a gruppi

Tutte le carte si muovono con **una** funzione che accetta un *gruppo* di oggetti:

```cpp
// Muove insieme tutti gli oggetti del gruppo e genera UN evento di gruppo.
ZoneChangeBatch move_objects(GameState&, std::span<const ObjRef>, Zone to, Cause cause);

struct ZoneChange { CardId card; Zone from, to; ObjRef old_ref, new_ref; };
struct ZoneChangeBatch { StaticVector<ZoneChange, 60> moves; Cause cause; };
```

La funzione è l'unica che:

- toglie le carte dalle zone di partenza e le mette in quella di arrivo;
- aumenta `incarnation`;
- aggiorna il riepilogo del cimitero;
- genera **un evento di gruppo** per i trigger;
- scrive la riga nel registro JSON Lines.

**Perché a gruppi.** Moonshadow dice: "ogni volta che una o più carte permanente finiscono nel tuo cimitero da qualsiasi zona, mentre ha un segnalino -1/-1, rimuovine uno". Macinare 3 carte è *un* evento: anche se sono 3 terre, Moonshadow perde un solo segnalino. Se la funzione spostasse una carta alla volta generando tre eventi, Moonshadow perderebbe tre segnalini e crescerebbe molto più in fretta del dovuto. Questo falserebbe proprio la velocità di Rakdos che vogliamo misurare.

I trigger leggono l'evento di gruppo in due modi:

- **"Una o più"** (Moonshadow): scattano **una volta** se almeno un elemento del gruppo soddisfa la condizione.
- **Per oggetto** (morte di una creatura, entrata in campo): scattano **una volta per elemento** del gruppo.

Sono simultanei, e quindi un unico gruppo:

- le carte macinate da un'unica istruzione (Stitcher's Supplier, Dark Matter Manipulator);
- le creature che muoiono insieme per azione di stato dopo i danni da combattimento;
- le carte scartate insieme.

Sono invece eventi separati il sacrificio di una fetch per pagarne l'abilità e, più tardi, la carta cercata che finisce in mano.

**Carta permanente** = carta con almeno un tipo tra artefatto, creatura, incantesimo, terra, planeswalker e battaglia. Due conseguenze per Moonshadow:

- un Lightning Bolt da solo che finisce nel cimitero non conta;
- un token che muore non conta, perché non è una carta.

## 5. Pila, trigger e Aure

```cpp
struct StackItem {
    StackKind kind;          // Spell, ActivatedAbility, TriggeredAbility
    ObjRef source;           // la carta lanciata o la fonte dell'abilità
    PlayerId controller;
    uint8_t ability_index;   // quale abilità della definizione
    Targets targets;         // array fisso di ObjRef / PlayerId
    uint8_t x_value;         // March of Otherworldly Light
    uint8_t mode;            // Thraben Charm
    CastMethod method;       // normale, evocazione, gratis (FoN), warp, conferimento, costo alternativo (Flare)
    Zone cast_from;          // mano, cimitero (Phoenix), esilio (warp)
};
```

- Una magia in pila è anche un oggetto in zona `Stack`; un'abilità non è una carta ed esiste solo come `StackItem`.
- Alla risoluzione si ricontrollano i bersagli con `ObjRef`; se nessuno è più legale, la magia o l'abilità viene neutralizzata.
- Il **metodo di lancio** resta sull'oggetto, perché cambia cosa succede dopo: Solitude evocata si sacrifica, Quantum Riddler lanciato col warp va esiliato alla fine del turno, Detective's Phoenix conferita entra come Aura.

### Detective's Phoenix e il conferimento

- **Costo del conferimento:** {R} più *raccogliere prove 6*, cioè esiliare dal proprio cimitero carte con valore di mana totale ≥ 6. Si può lanciare **anche dal cimitero** con il conferimento (`cast_from = Graveyard`).
- **Lanciata dal cimitero:** la Phoenix va in pila *prima* che si paghino i costi (601.2a), quindi non può essere esiliata come prova di se stessa. Il controllo di fattibilità usa `total_mana_value` del cimitero **meno il valore di mana della Phoenix stessa**.
- **In campo come Aura:** la carta ha il flag `Bestowed` e `attached_to` valido. È un'Aura incantesimo, non una creatura.
- **Quando l'Aura si stacca:** se la creatura incantata lascia il campo, `attached_to` non è più valido. All'azione di stato successiva la Phoenix perde `Bestowed` e **resta in campo come creatura incantesimo**. Non è un trigger e non c'è nessun "ritorno": è un cambio di caratteristiche.
- **Nel cimitero** conta come incantesimo e creatura: due tipi per il delirium.

### Trigger

Gli eventi di gruppo (sezione 4) e il combattimento creano trigger *in attesa*. Prima di dare la priorità, il motore li mette in pila nell'ordine APNAP: prima quelli del giocatore attivo, poi quelli dell'altro. Se un giocatore ha più trigger insieme, l'ordine diventa una **decisione dell'agente**: con Phelia, Ephemerate e i trigger di entrata l'ordine conta.

**Alternativa scartata.** Ordinare i trigger in automatico è più semplice, ma su Phelia ed Ephemerate darebbe risultati sbagliati proprio nelle linee che vogliamo misurare.

### Trigger ritardati

Stanno in una lista a parte, con la condizione di attivazione e gli oggetti collegati. Esiste solo la condizione `NextEndStep`:

| Fonte | Cosa succede all'inizio della prossima sottofase finale |
| --- | --- |
| Phelia | La carta esiliata torna in campo sotto il controllo del suo **proprietario**. Se è entrata sotto il controllo di chi controlla Phelia, Phelia prende un segnalino +1/+1 |
| Warp (Quantum Riddler, Starfield Shepherd) | Il permanente lanciato col warp viene esiliato; dall'esilio si potrà lanciare di nuovo più avanti |

**Gli oggetti collegati di Phelia** sono due `ObjRef`:

- **la carta in esilio**, con l'incarnation che ha *dopo* essere stata esiliata;
- **Phelia stessa.**

Alla risoluzione si possono presentare tre casi:

- **Token esiliato:** smette di esistere (704.5d), il suo slot viene riciclato e il riferimento non è più valido. Il trigger non fa nulla.
- **Phelia non più in campo:** la carta torna comunque, ma il segnalino non va da nessuna parte, perché il riferimento a Phelia non è più valido.
- **Permanente di proprietà di chi controlla Phelia:** torna sotto il suo controllo, quindi Phelia prende il segnalino.

## 6. Il cimitero

È la struttura più importante contro Rakdos Delirium. Ogni cimitero ha, oltre alla lista ordinata delle carte, un **riepilogo aggiornato a ogni ingresso e uscita**:

```cpp
struct GraveyardSummary {
    uint8_t card_count;
    uint16_t total_mana_value;                    // prove (Detective's Phoenix)
    std::array<uint8_t, kNumCardTypes> per_type;  // quante carte per tipo
    TypeMask present;                             // bit acceso se per_type[t] > 0

    int distinct_types() const { return std::popcount(present); }  // forza di Nethergoyf
    bool delirium() const { return distinct_types() >= 4; }
};
```

**Perché i contatori per tipo e non solo i bit.** Con i soli bit, quando esce un Lightning Bolt non sai se c'è ancora un altro istantaneo. Con un contatore per tipo l'uscita è un semplice decremento, e il bit si spegne solo quando il contatore arriva a zero. Una carta con più tipi incrementa ogni suo tipo: Fear of Missing Out e Detective's Phoenix contano come incantesimo e creatura, il che è decisivo per il delirium.

**Le domande poste al cimitero e il loro costo.**

| Domanda | Risposta | Costo |
| --- | --- | --- |
| Quante carte? | `card_count` | O(1) |
| Delirium? | `popcount(present) >= 4` | O(1) |
| Forza di Nethergoyf | `popcount(present)` | O(1) |
| Posso raccogliere prove 6? | `total_mana_value >= 6` (meno la Phoenix stessa se lanciata dal cimitero) | O(1) |
| *Quali* carte esilio per le prove? | L'agente le sceglie una alla volta (sotto) | Una decisione per carta |

### Raccolta delle prove: una carta alla volta

L'agente non sceglie un sottoinsieme in un colpo solo. Il motore apre una decisione `CollectEvidence { needed = 6, collected = 0 }`:

1. a ogni passo l'agente sceglie **una** carta tra quelle ancora nel cimitero (esclusa la Phoenix in pila);
2. la carta viene messa da parte e `collected` aumenta del suo valore di mana;
3. appena `collected >= 6` la decisione si chiude. Tutte le carte scelte vengono esiliate con **un solo** `move_objects`, perché il costo si paga in un'unica azione e quindi è un unico evento di gruppo.

**Non ci sono vicoli ciechi.** Si entra nella decisione solo se il totale disponibile è ≥ 6, quindi qualunque sequenza di scelte arriva prima o poi a 6. Il numero di azioni legali a ogni passo è al massimo il numero di carte nel cimitero, contro le possibili combinazioni (anche migliaia) dell'enumerazione dei sottoinsiemi.

### Altri dettagli

**Bifronti nel cimitero.** Fuori da campo e pila, una MDFC ha solo le caratteristiche del fronte (Witch Enchanter conta come creatura). Il riepilogo usa sempre la definizione del fronte.

**Controllo di coerenza.** Nei test, e in Debug dopo ogni azione, si ricalcola il riepilogo da zero scorrendo il cimitero e lo si confronta con quello incrementale. È la mitigazione "controllo del cimitero dopo ogni azione nei test" chiesta dalla specifica.

**Alternative scartate**

- *Ricalcolare tutto a ogni domanda.* Il cimitero ha al massimo qualche decina di carte, quindi non sarebbe drammatico. Ma la forza di Nethergoyf si legge di continuo (azioni di stato, ogni blocco, ogni azione legale), e il ricalcolo da zero lo teniamo comunque come verifica nei test.
- *Cache con flag "da ricalcolare".* Aggiunge una condizione in ogni lettura e il rischio di dimenticare di invalidarla. L'aggiornamento incrementale in un solo punto (`move_objects`) è più semplice e più verificabile.

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

### "Ha già attaccato questo turno" (Fear of Missing Out)

L'Oracle dice "Whenever this creature attacks for the first time each turn": il trigger riguarda **la creatura**, non il giocatore. Il flag `AttackedThisTurn` sta quindi nei `flags` di `CardObject`:

- **Si accende** quando la creatura viene dichiarata come attaccante.
- **Si azzera** all'inizio di ogni turno e quando la carta cambia zona: dopo un cambio di zona è un oggetto nuovo (400.7), che non ha mai attaccato.
- **Due Fear of Missing Out** che attaccano insieme fanno scattare **due** trigger e quindi **due** combattimenti aggiuntivi (`extra_combats` sale di 2).
- **Nel combattimento aggiuntivo** la stessa creatura non è al primo attacco del turno, quindi non riscatta e non si creano combattimenti infiniti. Una Fear of Missing Out *diversa*, che attacca per la prima volta solo nel combattimento aggiuntivo, scatta invece regolarmente.
- **Il combattimento aggiuntivo fa parte dell'effetto del trigger**, insieme allo stappare la creatura bersaglio. Se alla risoluzione il bersaglio non è più legale, il trigger viene neutralizzato: niente creatura stappata e **niente combattimento aggiuntivo**.

### Il motore fa domande, non chiama gli agenti

Questa è la scelta architetturale più importante. Il motore è una **macchina a stati che si ferma a ogni decisione**:

```cpp
std::vector<Action> legal_actions(const GameState&);
void apply(GameState&, const Action&);   // avanza fino alla decisione successiva
```

`PendingDecision` dice cosa si sta chiedendo e a chi: priorità, bersagli, attaccanti, bloccanti, mulligan, carte da mettere in fondo, scarti, ordine dei trigger, una carta per la raccolta delle prove. Tra una decisione e l'altra il motore avanza da solo.

**Passaggio automatico della priorità.** Quando l'**unica** azione legale è "passa", `apply` passa da solo e prosegue fino alla prossima decisione vera. Il passaggio si scrive nel registro ma **non è un nodo della ricerca**: l'MCTS non vede mai una decisione con una sola scelta. Basta che ci sia anche una sola alternativa (un istantaneo lanciabile, un'abilità attivabile) perché la decisione venga posta all'agente.

**Alternativa scartata: il motore chiama l'agente** (`agent->choose(...)` dentro la risoluzione di una carta). È più naturale da scrivere, ma il "punto in cui siamo" della risoluzione resta sullo stack delle chiamate C++, che non si può copiare. L'MCTS deve poter fotografare lo stato a ogni decisione e ripartire da lì, quindi tutto ciò che serve per riprendere deve stare dentro `GameState`. Il costo è che le risoluzioni con scelte a metà (Fear of Missing Out: scarta, poi pesca) vanno spezzate in passi espliciti.

## 8. Giocatori e casualità

```cpp
struct PlayerState {
    int16_t life;
    uint8_t energy;                       // Guide of Souls
    ManaPool pool;                        // W U B R G C
    StaticVector<CardId, 60> library, hand, graveyard;
    GraveyardSummary gy;
    uint8_t mulligans;
    uint8_t spells_cast_this_turn;
    bool sorceries_as_flash;              // +1 di Teferi, fino al prossimo turno del giocatore
    bool drew_from_empty_library;
};
```

**Teferi, Time Raveler** tocca la velocità di lancio in due modi:

- **statica:** gli avversari di chi controlla Teferi possono lanciare magie solo quando potrebbero lanciare una stregoneria. Non si salva nello stato: il generatore di azioni legali la ricava guardando se l'avversario ha Teferi in campo;
- **+1:** il controllore può lanciare stregonerie come se avessero lampo, fino al suo prossimo turno. È il flag `sorceries_as_flash`, che si azzera all'inizio del suo turno successivo.

**Generatore casuale.** Uso un generatore piccolo (PCG32 o xoshiro128++: 8-16 byte) scritto nel progetto, dentro `GameState`, al posto di `std::mt19937` più `std::uniform_int_distribution`, per due motivi:

- `std::mt19937` occupa circa 2,5 KB e raddoppierebbe la dimensione dello stato da copiare;
- le distribuzioni della libreria standard **non danno gli stessi numeri** con MSVC, GCC e Clang: la stessa partita darebbe risultati diversi su Windows e su Linux. Mescolamento e intervalli si implementano a mano (Fisher-Yates, metodo di Lemire).

### Generatore della partita e generatori degli agenti (deciso il 9/11/2026)

Ci sono **due tipi di generatore**, separati:

| Generatore | Dove sta | A cosa serve |
| --- | --- | --- |
| **Della partita** | `GameState::rng`, seed della partita | Solo casualità delle regole: mescolamenti iniziali, mulligan, fetchland, ogni "then shuffle" |
| **Dell'agente** | Dentro l'agente (`RandomAgent`), seed proprio, stream PCG diverso | Solo le scelte dell'agente |

`choose()` riceve lo stato **in sola lettura** (`const GameState&`): un agente non può consumare i numeri casuali della partita, e lo garantisce il compilatore.

**Perché: numeri casuali comuni.** Per confrontare due agenti (o due versioni di un mazzo) conviene farli giocare con la stessa sorte. Con lo stesso seed di partita:

- grimori iniziali e mani iniziali sono identici, qualunque agente giochi;
- a parità di azioni, anche i mescolamenti successivi sono identici: il k-esimo mescolamento usa gli stessi numeri.

Così la differenza tra i risultati dipende dalle scelte e non dalla fortuna delle pescate, e per vedere una differenza di win rate servono molte meno partite. È la tecnica standard di riduzione della varianza chiamata *common random numbers*.

**Alternativa scartata:** l'agente casuale che pesca dal generatore della partita (la prima versione). Ogni scelta dell'agente spostava in avanti la sequenza della partita, quindi due agenti diversi con lo stesso seed avrebbero visto mescolamenti diversi dal primo mulligan in poi: il confronto avrebbe mescolato l'effetto delle scelte con quello della sorte.

**Test:** `tests/test_agents.cpp` verifica che due agenti con seed diversi, sulla stessa partita, abbiano gli stessi grimori e le stesse mani iniziali, e le stesse mani anche dopo un mulligan di entrambi.

## 9. Informazione nascosta

Lo stato contiene tutto. Gli agenti ricevono una vista (`Observation`) che contiene solo:

- le zone pubbliche;
- la propria mano;
- il numero di carte in mano e nel grimorio dell'avversario;
- le carte con il proprio bit acceso in `known_to`.

Thoughtseize, per esempio, accende il bit su tutta la mano dell'avversario. Per l'ISMCTS la "determinizzazione" parte dalla vista: si redistribuiscono le carte non note tra mano e grimorio dell'avversario, rispettando ciò che si sa.

## 10. Lancio delle magie e pagamento del mana (deciso il 9/11/2026)

### Flusso del lancio (601.2)

Il motore offre l'azione `CastSpell` solo se il lancio si può **completare**: tempi giusti, almeno un bersaglio legale (se serve) e almeno un modo di pagare. Così non serve mai tornare indietro a metà lancio (601.2, "se un passo è impossibile si annulla tutto").

1. **La carta va in pila** (zona `Stack`, con un `StackItem` di tipo `Spell`). È un cambio di zona come gli altri: un evento di gruppo.
2. **Modalità e bersagli:** se la carta ha un bersaglio, decisione `ChooseTarget` (giocatori, creature in campo).
3. **Costo totale:** il costo di mana stampato (per ora nessun costo alternativo o aggiuntivo tra le carte implementate).
4. **Abilità di mana e pagamento:** decisione `PayMana` (sotto). Le terre scelte vengono tappate, il loro mana entra nella riserva e da lì si paga il costo: la riserva torna a zero.
5. **Lancio completato:** il giocatore che ha lanciato riceve la priorità (117.3c).

**Velocità:** istantanei e abilità attivate in qualunque momento si abbia la priorità; tutto il resto (stregonerie, creature, artefatti) solo nella propria fase principale, con la pila vuota.

Le abilità attivate seguono lo stesso schema (602.2): in pila, bersagli, poi i costi (tappare, sacrificare, pagare vita, scartare).

### Il problema: troppi modi di pagare

Un modo di pagare è un insieme di terre da tappare più il colore che ognuna produce. Contarli tutti esplode:

- con 8 terre e un costo di 3 ci sono C(8,3) = 56 sottoinsiemi di terre, e per ognuno più assegnazioni dei colori;
- quasi tutti sono **equivalenti**: tappare la prima o la seconda di due Swamp non cambia nulla, e nemmeno quale colore produce una Blood Crypt che paga mana generico.

Un nodo della ricerca con 56 mosse equivalenti spreca simulazioni, e un agente casuale sceglierebbe di fatto a caso tra copie della stessa mossa.

### La soluzione: contare le fonti per classe

Una scelta di pagamento conta solo per **le fonti di mana che restano disponibili dopo**. Per questo:

1. **Classi di fonti.** Le terre stappate del giocatore si raggruppano per *insieme di colori che possono produrre* (la `ColorMask` di `mana_colors`). Esempio Rakdos: {B} per le Swamp, {B,R} per Blood Crypt, Blackcleave Cliffs e Raucous Theater. Due terre della stessa classe sono intercambiabili come fonti di mana.
2. **Un pagamento è un vettore di conteggi:** quante terre tappare per ogni classe, con somma uguale al costo totale. Si enumerano i vettori, non i sottoinsiemi: con k classi da n₁…nₖ terre sono al più (n₁+1)·…·(nₖ+1), in pratica qualche decina al massimo, contro le migliaia di sottoinsiemi con i colori.
3. **Ogni vettore lascia un insieme diverso di fonti disponibili**, e due vettori diversi ne lasciano due diversi: l'agente vede esattamente un'opzione per ogni risultato distinto, come richiesto.
4. **Fattibilità con il teorema di Hall.** Un vettore è valido se i simboli colorati si possono assegnare alle terre tappate: per ogni sottoinsieme S dei colori richiesti, i simboli di S non devono superare le terre tappate che producono almeno un colore di S. Con 5 colori sono al più 31 controlli. Il mana generico è coperto dalle terre rimanenti, perché il totale coincide.
5. **I colori non sono una scelta.** Dato il vettore, il motore assegna i colori con un abbinamento (cammini aumentanti, su al massimo una decina di terre). Qualunque assegnazione valida porta allo stesso stato, perché la riserva viene consumata tutta.
6. **Quali terre di una classe:** le prime nell'ordine del campo. Sono intercambiabili per definizione di classe.
7. **Un solo modo di pagare = nessuna decisione.** Se c'è un solo vettore valido (anche il costo {0}), il pagamento avviene da solo e viene registrato nel log come `AutoPay`. Come per il passaggio automatico, non è un nodo della ricerca.

L'opzione scelta è l'indice del vettore nell'enumerazione, che è deterministica (ordine lessicografico delle classi, ordinate per `ColorMask`).

**Alternative scartate**

- *Tappare una terra alla volta*, con una decisione per terra: la stessa esplosione spalmata su più livelli dell'albero, con sequenze diverse che arrivano allo stesso stato.
- *Pagamento automatico "furbo"*, con un'euristica che sceglie le terre da tappare: toglie all'agente una scelta vera (tenere stappata Blood Crypt per un Lightning Bolt in risposta, o una Swamp), che è proprio il tipo di decisione che vogliamo misurare.
- *Classi per nome della carta* invece che per colori: Blood Crypt e Raucous Theater resterebbero distinte anche se, come fonti di mana, sono identiche.

**Limiti noti:** le classi guardano solo il mana. Se in futuro una carta distinguerà terre con gli stessi colori (per tipo, per nome), la classe dovrà includere anche quella proprietà.

## 11. Cosa resta fuori da questa proposta

- **Effetti continui e livelli (regola 613).** Nel pool sono pochi: Nethergoyf (forza definita dalla caratteristica), la statica di Teferi, la Phoenix conferita (+2/+2, volare e rapidità alla creatura incantata). Propongo di calcolarli al volo in una funzione dedicata, senza un sistema di livelli generico, e di rivederla se le carte lo richiedono.
- **Mana e pagamento dei costi.** Riguarda le azioni legali più che lo stato. Va deciso a parte, compresi Street Wraith (ciclo pagando vita) e i costi alternativi.
- **Mockingbird e Aang.** Sono rimandati. Le simulazioni usano [config/decks/uw_blink_sim.json](../config/decks/uw_blink_sim.json): una terza Ephemerate al posto di Aang e una quarta Witch Enchanter al posto di Mockingbird. La lista vera resta invariata in `uw_blink.json`, e un test controlla che le due liste differiscano esattamente per queste sostituzioni.

## 12. Test previsti per lo stato

Questi test si scrivono **prima** del codice corrispondente, quando inizierà il motore.

**Raggruppamento degli eventi (Moonshadow)**

| Caso | Atteso |
| --- | --- |
| Moonshadow con 6 segnalini -1/-1; si macinano 3 terre in un'unica istruzione | `move_objects` produce **un** evento di gruppo con 3 elementi; Moonshadow perde **1** segnalino (ne restano 5) |
| Come sopra, ma 1 terra e 2 istantanei | Un evento di gruppo; il gruppo contiene una carta permanente, quindi **1** segnalino rimosso |
| Si macinano 3 istantanei | Nessuna carta permanente: **0** segnalini rimossi |
| Due istruzioni separate, ognuna con 1 terra | **Due** eventi di gruppo, **2** segnalini rimossi |
| Un token creatura muore | Non è una carta: **0** segnalini rimossi |
| Moonshadow senza segnalini -1/-1 | La condizione "mentre ha un segnalino -1/-1" è falsa: il trigger non scatta |

**Altri test sullo stato**

- **Riepilogo del cimitero:** dopo ogni `move_objects`, il riepilogo incrementale coincide con quello ricalcolato da zero (si verifica in ogni test).
- **Segnalini:** un permanente con 2 segnalini +1/+1 e 3 -1/-1, dopo l'azione di stato, ha 0 segnalini +1/+1 e 1 -1/-1.
- **Slot dei token:** riusare uno slot fa salire `incarnation`, e un `ObjRef` al vecchio token non è più valido. Con 32 token già in campo, crearne un altro produce `EngineLimitError`.
- **Phoenix conferita:** se la creatura incantata viene esiliata, dopo le azioni di stato la Phoenix è una creatura in campo senza `Bestowed`.
- **Phelia:** un token esiliato non torna. Un permanente dell'avversario torna sotto il controllo dell'avversario e Phelia *non* prende il segnalino. Un permanente proprio torna e Phelia prende il segnalino.
- **Fear of Missing Out:** due FOMO che attaccano insieme portano `extra_combats` a 2. Nel combattimento aggiuntivo la stessa FOMO non riscatta. Se la creatura bersaglio lascia il campo prima della risoluzione, nessun combattimento aggiuntivo. Una FOMO uscita dal campo e rientrata nello stesso turno ha il flag azzerato.
- **Raccolta delle prove:** con un cimitero da 6 o più di valore di mana totale, qualunque sequenza di scelte chiude la decisione; le carte scelte finiscono in esilio con un solo evento di gruppo.

## Decisioni prese in revisione (8/11/2026)

| Tema | Decisione |
| --- | --- |
| Detective's Phoenix | Conferimento = {R} + raccogliere prove 6, lanciabile dal cimitero; staccata resta creatura incantesimo. Nessun trigger "quando muore" |
| Phelia | Ritorno all'inizio della prossima sottofase finale sotto il controllo del proprietario; segnalino +1/+1 se entra sotto il controllo di chi controlla Phelia; i token esiliati smettono di esistere |
| Moonshadow | Entra con sei segnalini -1/-1; eventi di cambio zona raggruppati; contatori +1/+1 e -1/-1 separati con annullamento per azione di stato |
| Guide of Souls | Segnalini abilità (volare) come contatori per parola chiave |
| Fear of Missing Out | Flag "ha già attaccato questo turno" sulla **creatura** (per oggetto, azzerato al cambio di zona): due FOMO danno due combattimenti aggiuntivi; bersaglio illegale = niente combattimento aggiuntivo |
| Raccolta delle prove | Una carta alla volta fino a 6, niente enumerazione dei sottoinsiemi |
| Teferi | Il +1 permette di lanciare stregonerie come se avessero lampo |
| Token | 32 slot riciclabili, incarnation che sale, errore esplicito se finiscono |
| Ordine del cimitero | Si tiene |
| Passaggio automatico della priorità | Solo quando "passa" è l'unica azione legale; non è un nodo della ricerca |
| Mockingbird e Aang | Rimandati; lista di simulazione `uw_blink_sim.json` |
