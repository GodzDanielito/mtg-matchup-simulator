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

Un record per ogni carta fisica della partita (60 + 60) più 64 slot per i token (sezione 3).

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

### Token: 64 slot riciclabili

- I token occupano gli slot da 120 a 183. Quando un token smette di esistere (cimitero, esilio, mano: azione di stato 704.5d), lo slot torna libero.
- Riusando uno slot, `incarnation` **sale** invece di ripartire da zero. Così un vecchio `ObjRef` al token morto non può mai puntare per sbaglio al token nuovo nello stesso slot.
- Se i 64 slot finiscono, il motore lancia un **errore esplicito** (`EngineLimitError`), mai una sovrascrittura silenziosa. Nei test è un fallimento, nelle simulazioni la partita viene scartata e registrata come anomalia.
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
- **Step dei danni da attacco improvviso: serve.** *Correzione del 9/11/2026:* la prima versione diceva che nel pool non c'era attacco improvviso, ma **Ocelot Pride ha "First strike, lifelink"** (Oracle verificato). Lo step aggiuntivo della 510.4 è descritto nella sezione 13.

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
- **Mana e pagamento dei costi.** Deciso nella sezione 10 per il costo di mana stampato. Restano da fare i costi alternativi (evocare Solitude, Force of Negation gratis, Flare of Malice) e i costi aggiuntivi.

### Requisiti delle carte future

**Teferi, Time Raveler** è il primo planeswalker del pool. Quando verrà implementato, il motore dovrà cambiare in più punti, non solo nella carta:

- **Lightning Bolt e ogni "any target":** i planeswalker diventano bersagli legali. Oggi `TargetKind::AnyTarget` offre solo giocatori e creature, perché nessun planeswalker può essere in campo; il test della carta dovrà verificare che Bolt possa bersagliare Teferi.
- **Danno da combattimento:** un planeswalker può essere **attaccato** (la dichiarazione degli attaccanti deve offrire anche i planeswalker avversari come difensori) e riceve il danno delle creature che lo attaccano senza essere bloccate.
- **Segnalini fedeltà:** il danno a un planeswalker rimuove segnalini fedeltà (120.3c); serve il contatore in `Counters`. Va aggiunta l'azione di stato "planeswalker con fedeltà 0 va al cimitero" (704.5i).
- **Statica e abilità di fedeltà:** la statica sulla velocità di lancio degli avversari e il +1 (stregonerie come se avessero lampo, `sorceries_as_flash`) sono già previsti nella sezione 8; le abilità di fedeltà si attivano una volta per turno, a velocità stregoneria.
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
- **Slot dei token:** riusare uno slot fa salire `incarnation`, e un `ObjRef` al vecchio token non è più valido. Con tutti gli slot occupati, crearne un altro produce `EngineLimitError`.
- **Phoenix conferita:** se la creatura incantata viene esiliata, dopo le azioni di stato la Phoenix è una creatura in campo senza `Bestowed`.
- **Phelia:** un token esiliato non torna. Un permanente dell'avversario torna sotto il controllo dell'avversario e Phelia *non* prende il segnalino. Un permanente proprio torna e Phelia prende il segnalino.
- **Fear of Missing Out:** due FOMO che attaccano insieme portano `extra_combats` a 2. Nel combattimento aggiuntivo la stessa FOMO non riscatta. Se la creatura bersaglio lascia il campo prima della risoluzione, nessun combattimento aggiuntivo. Una FOMO uscita dal campo e rientrata nello stesso turno ha il flag azzerato.
- **Raccolta delle prove:** con un cimitero da 6 o più di valore di mana totale, qualunque sequenza di scelte chiude la decisione; le carte scelte finiscono in esilio con un solo evento di gruppo.

## 13. Combattimento (approvato il 9/11/2026)

Fonti: Comprehensive Rules in vigore dal **6 novembre 2026** (sezioni 506-511 e 702), testo Oracle da Scryfall.

### Cosa dicono le regole, in breve

- **Attaccanti (508.1):** il giocatore attivo sceglie quali sue creature attaccano: stappate, e con rapidità oppure controllate con continuità dall'inizio del turno. Per ognuna sceglie chi attacca: il giocatore avversario, o un suo planeswalker (sezione 11, Teferi). Gli attaccanti si tappano. Poi scattano i trigger di attacco (508.1m).
- **Nessun attaccante (508.8):** si saltano bloccanti e danno.
- **Bloccanti (509.1):** il giocatore in difesa sceglie quali sue creature stappate bloccano, e **ogni bloccante blocca una sola creatura** (nel pool nessuna carta permette di bloccare più creature). Le restrizioni di evasione si sommano (509.1b).
- **Danno (510.1c, regola attuale):** una creatura bloccata da due o più creature **divide il suo danno tra i bloccanti come sceglie il suo controllore**. Non esistono più l'ordine di assegnazione e l'obbligo di assegnare danno letale al primo bloccante. Una creatura bloccata resta bloccata anche se i bloccanti spariscono (509.1h), e allora non infligge danno.
- **Attacco improvviso (510.4):** se all'inizio dello step del danno almeno un attaccante o un bloccante ha attacco improvviso, c'è un primo step in cui infliggono danno solo quelle creature, poi un secondo step per le altre.

### Parole chiave e restrizioni del pool

Verificate sul testo Oracle delle carte della lista di simulazione:

| Regola | Carte | Effetto sul combattimento |
| --- | --- | --- |
| Volare (702.9) | Starfield Shepherd, Detective's Phoenix, Quantum Riddler; segnalino di Guide of Souls | Bloccabile solo da creature con volare o raggiungere |
| Rapidità (702.10) | Detective's Phoenix | Attacca anche se è appena entrata |
| Legame vitale (702.15) | Ocelot Pride, Solitude | Il controllore guadagna vita pari al danno inflitto |
| Attacco improvviso (702.7) | Ocelot Pride | Step di danno aggiuntivo (510.4) |
| Minaccia (702.111) | Moonshadow | Bloccabile solo da due o più creature |
| "Can't be blocked except by three or more creatures" | Troll of Khazad-dûm | Come minaccia, con minimo 3 |
| Passaterra: swampwalk (702.14) | Street Wraith | Imbloccabile se il difensore controlla una Swamp (UW Blink non ne ha: il test lo verificherà con una Swamp messa in campo apposta) |
| Protezione dal nero e dal rosso (702.16) | Sanctifier en-Vec | Non bloccabile da creature nere o rosse (tutte quelle di Rakdos); il danno da fonti nere o rosse è prevenuto; niente bersagli neri o rossi (Lightning Bolt) |

**Raggiungere non serve:** nel pool ce l'ha solo Aang and La, il retro di Aang, escluso dalla lista di simulazione. Lo implementerò insieme ad Aang.

**Due parole chiave non erano nella richiesta, ma il pool le richiede:** attacco improvviso (Ocelot Pride) e protezione (Sanctifier en-Vec).

### Il problema: le dichiarazioni esplodono

Con n creature che possono attaccare ci sono 2ⁿ dichiarazioni di attacco. Con a attaccanti e b bloccanti, i modi di bloccare sono fino a (a+1)ᵇ: con 4 attaccanti e 4 bloccanti sono 625. Una decisione con centinaia di opzioni è inutilizzabile sia per l'agente casuale sia per la ricerca ad albero. In più:

- molte dichiarazioni sono **identiche** a meno di scambiare creature uguali: tre gatti di Ocelot Pride, due Plains, due Stitcher's Supplier;
- alcune combinazioni sono **illegali** solo nel loro insieme: un bloccante solo su Moonshadow (minaccia) o due bloccanti su Troll.

### La soluzione: decisioni successive, in ordine canonico

Una dichiarazione si costruisce **un pezzo alla volta**, con decisioni piccole. Tre regole garantiscono che ogni dichiarazione legale si raggiunga **esattamente in un modo** e che non si finisca mai in una dichiarazione illegale.

**Regola 1, ordine canonico.** Le creature si esaminano sempre nello stesso ordine: raggruppate per *classe* (stessa carta, stesso controllore, stesso stato: segnalini, danno, flag), poi per `CardId`. Due creature della stessa classe sono indistinguibili per il combattimento.

**Regola 2, scelte monotone dentro una classe.** Tra creature della stessa classe le scelte devono seguire un ordine fisso: si sceglie *quante* creature della classe fanno una cosa, non *quali*.

**Regola 3, nessun vicolo cieco.** Un'opzione viene offerta solo se esiste almeno un modo legale di completare la dichiarazione dopo averla scelta. Così non serve mai tornare indietro (al contrario della 508.1, che per i giocatori umani prevede di annullare una dichiarazione illegale).

#### Attaccanti: una creatura alla volta

All'inizio dello step di dichiarazione degli attaccanti, per ogni creatura che può attaccare, in ordine canonico, il giocatore attivo prende **una decisione con le opzioni "non attacca" e "attacca <difensore>"** (oggi il difensore è solo il giocatore avversario; con Teferi anche i suoi planeswalker).

- Le creature che non possono attaccare (tappate, "malate" senza rapidità, non creature come la Phoenix conferita) non generano decisioni.
- **Regola 2:** le opzioni hanno un ordine fisso (non attacca, poi i difensori). Dentro una classe ogni creatura deve scegliere un'opzione **non precedente** a quella della creatura prima di lei. Esempio: tre gatti identici e un solo difensore danno 4 dichiarazioni (0, 1, 2 o 3 gatti attaccano) invece di 8 sequenze. Quando resta una sola opzione (il gatto prima ha attaccato), la decisione non viene posta, come per il pagamento automatico.
- **Regola 3:** nel pool non esistono restrizioni o requisiti di attacco ("non può attaccare da sola", "attacca se può"), quindi ogni scelta è completabile e la dichiarazione è sempre legale.
- **Nessun attaccante possibile = nessuna decisione**, e si saltano bloccanti e danno (508.8).

Alla fine gli attaccanti si tappano, ricevono il flag `AttackedThisTurn` e il motore produce l'evento **"attaccanti dichiarati"**, da cui scatteranno i trigger di Phelia ("whenever Phelia attacks"), Guide of Souls ("whenever you attack") e Fear of Missing Out ("attacks for the first time each turn": si controlla il flag prima di accenderlo).

*Alternativa considerata:* "scegli il prossimo attaccante oppure fine", come proponevi. Con l'ordine canonico è anch'essa senza duplicati. Ho preferito una decisione per creatura perché la scelta del difensore ci sta dentro naturalmente e la regola delle classi si applica in modo uniforme.

#### Bloccanti: un attaccante alla volta

All'inizio dello step di dichiarazione dei bloccanti, per ogni attaccante in ordine canonico, il giocatore in difesa aggiunge **un bloccante alla volta** ("blocca con <creatura>") e poi sceglie **"fine"** per passare all'attaccante successivo.

Si procede per attaccante, e non per bloccante, perché le restrizioni con un minimo di bloccanti (minaccia 2, Troll 3) diventano **controlli locali**: "fine" è legale se l'attaccante ha 0 bloccanti oppure almeno il minimo. Procedendo per bloccante, invece, "un bloccante su Moonshadow" sarebbe legale o no a seconda delle scelte future degli altri bloccanti, e servirebbe un calcolo di flusso globale.

Per l'attaccante A, il bloccante b si può aggiungere se:

1. **è idoneo:** stappato, non sta già bloccando, e A non ha evasione contro di lui (volare senza volare/raggiungere, protezione dal suo colore, swampwalk se il difensore controlla una Swamp);
2. **viene dopo** l'ultimo bloccante aggiunto ad A, in ordine canonico (Regola 1: ogni insieme di bloccanti si costruisce in un solo ordine);
3. **è il primo della sua classe** tra quelli ancora liberi (Regola 2: si sceglie quanti bloccanti di una classe, non quali);
4. **la dichiarazione resta completabile** (Regola 3): restano abbastanza bloccanti idonei per arrivare al minimo di A, rispettando anche il vincolo sugli attaccanti identici qui sotto.

**Attaccanti identici** (stessa classe, stesso difensore): assegnare l'insieme S al primo e T al secondo equivale a T al primo e S al secondo. Per contare la combinazione una volta sola, il *profilo* dei bloccanti del secondo (quanti bloccanti di ogni classe) non deve superare, in ordine lessicografico, quello del primo.

Un attaccante senza bloccanti idonei (per esempio Street Wraith contro un difensore con una Swamp) non genera decisioni. Il controllo della Regola 3 è una piccola ricerca esaustiva sulle scelte rimaste: con le dimensioni del matchup (al più una decina di creature per parte) costa pochi microsecondi.

#### Danno: una decisione solo per gli attaccanti con più bloccanti

- **Non bloccato:** tutto il danno al difensore. **Un bloccante:** tutto a lui. **Bloccante:** tutto all'attaccante che blocca. Nessuna decisione.
- **Due o più bloccanti** (510.1c): il controllore dell'attaccante divide il danno. Le divisioni possibili sono le composizioni della forza tra i bloccanti, ma molte sono equivalenti: il danno in eccesso su un bloccante che muore comunque non cambia nulla. La decisione offre **un'opzione per ogni esito diverso**: quali bloccanti muoiono, quanto danno resta su quelli che sopravvivono e quanto danno viene davvero inflitto (conta per legame vitale e protezione). Se l'esito possibile è uno solo, nessuna decisione.
- **Simultaneità (510.2):** tutto il danno dello step viene inflitto insieme, con prevenzione (protezione) e legame vitale; poi azioni di stato (le creature che muoiono insieme sono **un** evento di gruppo), trigger e priorità.
- **Bloccanti spariti (509.1h, 510.1c):** un attaccante bloccato resta bloccato anche se tutti i suoi bloccanti lasciano il combattimento prima del danno, e allora non infligge danno a nessuno. Nel pool nessuna creatura ha travolgere, quindi non c'è eccezione.

#### Step del danno da attacco improvviso (510.4)

- **Esiste solo se serve:** quando lo step dei bloccanti finisce, si controlla se almeno un attaccante o un bloccante ancora in combattimento ha attacco improvviso. Se sì, si passa a `FirstStrikeDamage`, altrimenti direttamente a `CombatDamage`.
- **Nel primo step** infliggono danno solo le creature con attacco improvviso; poi azioni di stato, trigger e priorità, come in ogni step.
- **Nel secondo step** infliggono danno solo le creature rimaste in combattimento che **non** avevano attacco improvviso all'inizio del primo step: chi ha già colpito non colpisce di nuovo (non c'è doppio attacco nel pool).
- **Serve per Ocelot Pride:** un Ocelot Pride che blocca uno Stitcher's Supplier lo uccide nel primo step e non subisce danno nel secondo.

#### Protezione completa (702.16)

Sanctifier en-Vec ha protezione dal nero e dal rosso. Tutte e tre le parti si implementano in questo passo:

- **Blocchi (702.16f):** non può essere bloccato da creature nere o rosse.
- **Danno (702.16e):** il danno da fonti nere o rosse che gli verrebbe inflitto è prevenuto, in combattimento e fuori. Il danno prevenuto non è inflitto, quindi non dà vita con legame vitale.
- **Bersagli (702.16b):** non può essere bersaglio di magie nere o rosse né di abilità di fonti nere o rosse. Lightning Bolt (rosso) non lo può bersagliare; se lo diventasse dopo il lancio, alla risoluzione il bersaglio sarebbe illegale.

Test: Lightning Bolt non può bersagliare Sanctifier en-Vec; una creatura nera bloccata da Sanctifier non gli infligge danno.

#### Vita guadagnata in questo turno

Un flag per giocatore in `PlayerState`, `gained_life_this_turn`:

- **si accende** con qualsiasi guadagno di vita, compreso il legame vitale (anche in combattimento);
- **si azzera** all'inizio di ogni turno, per entrambi i giocatori;
- **servirà** al trigger di fine turno di Ocelot Pride ("if you gained life this turn").

Tutti i guadagni di vita passano da un'unica funzione `gain_life`, così il flag non si può dimenticare.

### Rappresentazione nello stato

```cpp
struct Attacker { ObjRef creature; Target defender; bool blocked; };
struct Block    { ObjRef blocker; std::uint8_t attacker; };   // indice in attackers
struct CombatState {
    StaticVector<Attacker, 32> attackers;
    StaticVector<Block, 32> blocks;
    std::uint8_t cursor;            // prossima creatura (attacco) o attaccante (blocco) da esaminare
    bool first_strike_step;         // c'è stato lo step dei danni da attacco improvviso
};
```

- **Uscita dal combattimento (506.4):** una creatura che lascia il campo ha un `ObjRef` non più valido, quindi è automaticamente fuori dal combattimento senza aggiornare liste.
- **Fine del combattimento (511.3):** lo stato si azzera; con il combattimento aggiuntivo di Fear of Missing Out ricomincia vuoto, mentre `AttackedThisTurn` resta acceso per tutto il turno.
- **Decisioni nuove:** `DeclareAttacker` (soggetto: la creatura; azioni `TargetPlayer`/`TargetObject` o "non attacca"), `DeclareBlockers` (soggetto: l'attaccante; azioni `ChooseCard` per aggiungere un bloccante, `ChooseOption` per "fine"), `DivideDamage` (soggetto: l'attaccante; un'opzione per esito).
- **Step:** si aggiunge `FirstStrikeDamage` prima di `CombatDamage`, usato solo quando serve.

### Come si verifica

- **Test esaustivo delle dichiarazioni:** su molte configurazioni piccole (2-4 attaccanti, 2-5 bloccanti, con copie identiche, volare, minaccia, il minimo di 3 di Troll, protezione, swampwalk) si generano **per forza bruta** tutte le dichiarazioni legali, si riducono a forma canonica (scambiando creature identiche) e si confrontano con le dichiarazioni raggiunte dal processo a decisioni. I due insiemi devono coincidere, e il numero di percorsi deve essere uguale al numero di dichiarazioni distinte: nessuna dichiarazione mancante, nessun duplicato, nessuna illegale.
- **Test approvati in revisione:** attacco improvviso (Ocelot Pride), protezione (Bolt non bersaglia Sanctifier, creatura nera bloccata da Sanctifier senza danno), vita guadagnata in questo turno, attaccante bloccato i cui bloccanti lasciano il combattimento senza danno.
- **Test di regole per ogni parola chiave**, con il testo Oracle della carta che la usa.
- **Robustezza:** 10.000 partite con attacchi e blocchi casuali, invarianti dopo ogni azione (anche sullo stato del combattimento), stesso seed stessa partita, copertura di attacchi, blocchi, blocchi multipli e divisioni del danno.

## 14. Fuga, ordine dei trigger, caratteristiche calcolate (9/11/2026)

### Fuga di Nethergoyf: solo insiemi minimali

"Escape—{2}{B}, Exile any number of other cards from your graveyard with four or more card types among them."

- **"Any number"** permette di esiliare più carte del necessario, ma per Rakdos è sempre peggio: un cimitero più piccolo indebolisce delirium, Nethergoyf, Detective's Phoenix e Dark Matter Manipulator. Si offrono quindi solo gli **insiemi minimali**: almeno 4 tipi, e nessuna carta superflua (togliendone una qualsiasi si scende sotto 4). Un insieme minimale ha al più 4 carte, perché ognuna deve portare un tipo che le altre non hanno.
- **Una carta alla volta, in ordine canonico** (definizione, poi `CardId`). Carte identiche sono un'unica candidata: un insieme minimale non ne contiene mai due. Si offre una carta solo se esiste un insieme minimale che comincia con le scelte già fatte e prosegue con lei. Così ogni insieme si raggiunge in un solo modo e non ci sono vicoli ciechi. Le scelte obbligate avvengono da sole.
- **Le carte scelte si esiliano insieme** (un evento di gruppo), poi si paga il mana.
- **Test:** confronto con la forza bruta su più cimiteri: stessi insiemi, un percorso ciascuno.
- **Raccolta delle prove:** lo stesso meccanismo servirà a Detective's Phoenix (insiemi minimali con valore di mana totale ≥ 6). La sezione 6 parla di "una carta alla volta finché non si arriva a 6": quella regola diventa "insiemi minimali in ordine canonico", per evitare duplicati.

### Ordine dei trigger (603.3b)

- **Trigger intercambiabili** (stesso effetto, stessa fonte, stesso bersaglio): vanno in pila senza chiedere l'ordine.
- **Trigger diversi dello stesso giocatore:** decisione `OrderTriggers`, un trigger alla volta. **Il primo scelto va in pila per primo, quindi si risolve per ultimo.** Prima il giocatore attivo, poi l'altro (APNAP).
- **Esempio del pool:** uno Stitcher's Supplier muore mentre Moonshadow ha segnalini. Scattano insieme la macinata del Supplier e il segnalino di Moonshadow, e l'ordine conta: se la macinata si risolve per prima, porta altre carte nel cimitero e può far togliere un altro segnalino a Moonshadow.

### Caratteristiche calcolate e "entra con"

- **Nethergoyf (604.3):** l'asterisco vale i tipi tra le carte nel cimitero del controllore, letti dal riepilogo incrementale. Un invariante verifica a ogni azione che coincida con il ricalcolo da zero.
- **Dark Matter Manipulator:** +2/+0 ogni sette carte (non token) nel cimitero del controllore, calcolato al volo.
- **Moonshadow (614.1c):** i sei segnalini -1/-1 si mettono mentre entra, prima dei trigger e delle azioni di stato. Il suo trigger scatta **una volta per evento di gruppo** e solo se in quel momento ha almeno un segnalino. Se Moonshadow lascia il campo nello stesso evento, il trigger non scatta: l'effetto non avrebbe nulla da fare.

## 15. Fear of Missing Out, conferimento, Flare of Malice (9/11/2026)

### Trigger con bersaglio (Fear of Missing Out)

- **Quando scatta:** alla fine della dichiarazione degli attaccanti, per ogni FOMO che attacca **prima** di accendere il suo flag `AttackedThisTurn`, e solo se il controllore ha delirium. L'"if" interposto (603.4) si ricontrolla alla risoluzione.
- **In pila uno alla volta:** se il trigger ha un bersaglio, il controllore lo sceglie subito (decisione `ChooseTarget`); se non ci sono bersagli legali, il trigger si toglie dalla pila (603.3d). Poi tocca al trigger successivo.
- **Alla risoluzione:** bersaglio illegale, oppure delirium perso: non succede nulla, quindi **nessun combattimento aggiuntivo** (608.2b). Altrimenti la creatura si stappa e `extra_combats` cresce di uno.
- **Due FOMO:** due trigger, due combattimenti aggiuntivi. Nei combattimenti aggiuntivi nessuna delle due FOMO fa scattare di nuovo il trigger, perché hanno già attaccato in questo turno.
- **Bersagli identici:** due creature identiche sono lo stesso bersaglio solo se le bersagliano già lo stesso numero di abilità in pila. Con due trigger, "la stessa creatura due volte" e "una creatura ciascuno" sono scelte diverse, e vanno offerte entrambe.

### Conferimento (702.103)

- **Modo di lancio `Bestow`**, dalla mano o dal cimitero (dal cimitero solo così). In pila la carta ha il flag `Bestowed` ed è un'Aura con bersaglio "creatura". Costo: {R} più le prove.
- **Mano e cimitero sono azioni distinte** anche con la stessa definizione: conferire la copia in mano lascia quella del cimitero come prova, conferire quella del cimitero tiene l'altra in mano.
- **Risoluzione:** con il bersaglio legale entra conferita e attaccata; con il bersaglio illegale entra come creatura (702.103e).
- **Azione di stato:** un'Aura conferita staccata, o attaccata a un oggetto che non può più incantare (non è più una creatura, oppure ha protezione dal rosso), smette di essere conferita e **resta in campo come creatura** (702.103f). È lo stesso oggetto: nessun cambio di zona, nessun trigger.
- **Bonus:** +2/+2, volare e rapidità per la creatura incantata, calcolati al volo da `power`, `toughness` e `has_keyword`. Una creatura con un'Aura conferita non è identica a una senza: il numero di Aure conferite entra nella chiave di classe del combattimento e nel confronto tra permanenti.
- **Invarianti:** quando un giocatore ha la priorità, ogni Phoenix conferita in campo è attaccata a una creatura in campo; in pila una carta è conferita solo se è stata lanciata con il conferimento.

### Prove: insiemi minimali generalizzati

- **Lo stesso meccanismo della fuga, con due misure:** il numero di tipi (Nethergoyf) o il valore di mana totale (Phoenix).
- **Per il valore di mana gli insiemi sono multinsiemi:** due Lightning Bolt possono servire entrambi. Un insieme è minimale se togliendo la carta più piccola si scende sotto 6. Le carte di valore di mana 0 non servono mai.
- **Le copie identiche si prendono come blocco delle prime k** in ordine canonico, così ogni insieme si raggiunge in un solo modo.
- **Supera la decisione di revisione** "una carta alla volta fino a 6": senza il vincolo di minimalità avrebbe prodotto percorsi duplicati e insiemi con carte superflue.
- **Prestazioni:** la ricerca non alloca nei nodi. La prima versione ricostruiva un vettore a ogni nodo e triplicava il tempo del test di robustezza.

### Flare of Malice

- **Costo alternativo** (`SacrificeAlternative`): sacrificare una creatura nera non token. Se le creature distinte sono più di una, decisione `SacrificeForCost`. Il sacrificio è una morte: la macinata di Stitcher's Supplier scatta e va in pila sopra Flare.
- **Effetto:** l'avversario sacrifica la creatura o il planeswalker di valore di mana più alto; tra pari sceglie lui (decisione `SacrificeForEffect`), tra copie identiche nessuna decisione. Un'Aura conferita non è una creatura e non conta.
- **Durante la scelta** Flare resta nella zona pila, come Thoughtseize mentre si sceglie la carta.

## Decisioni prese in revisione (8/11/2026)

| Tema | Decisione |
| --- | --- |
| Detective's Phoenix | Conferimento = {R} + raccogliere prove 6, lanciabile dal cimitero; staccata resta creatura incantesimo. Nessun trigger "quando muore" |
| Phelia | Ritorno all'inizio della prossima sottofase finale sotto il controllo del proprietario; segnalino +1/+1 se entra sotto il controllo di chi controlla Phelia; i token esiliati smettono di esistere |
| Moonshadow | Entra con sei segnalini -1/-1; eventi di cambio zona raggruppati; contatori +1/+1 e -1/-1 separati con annullamento per azione di stato |
| Guide of Souls | Segnalini abilità (volare) come contatori per parola chiave |
| Fear of Missing Out | Flag "ha già attaccato questo turno" sulla **creatura** (per oggetto, azzerato al cambio di zona): due FOMO danno due combattimenti aggiuntivi; bersaglio illegale = niente combattimento aggiuntivo |
| Raccolta delle prove | Una carta alla volta fino a 6, niente enumerazione dei sottoinsiemi. *Superata:* una carta alla volta verso un insieme minimale (sezioni 14 e 15) |
| Teferi | Il +1 permette di lanciare stregonerie come se avessero lampo |
| Token | 64 slot riciclabili (erano 32: Ocelot Pride con la benedizione della città può raddoppiare i gatti a ogni turno), incarnation che sale, errore esplicito se finiscono |
| Ordine del cimitero | Si tiene |
| Passaggio automatico della priorità | Solo quando "passa" è l'unica azione legale; non è un nodo della ricerca |
| Mockingbird e Aang | Rimandati; lista di simulazione `uw_blink_sim.json` |
