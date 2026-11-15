# Metriche stile 17lands (fase 3): proposta

**Stato: proposta del 15/11/2026, nessun codice.** Da rivedere con Daniele prima di scrivere lo strumento. Ogni scelta che non viene dalla specifica (`docs/progetto.md`, "Metriche stile 17lands") è segnata come **scelta di Claude**; le domande aperte sono nella sezione 13.

Le metriche misurano **come giocano gli agenti euristici v1**, non come gioca un pilota: una carta che l'agente usa male ha un IWD basso anche se è forte. Le conclusioni valgono per questa coppia di agenti finché la ricerca ad albero (fase 4) non le rimisura.

---

## 1. Agenti congelati: "euristici v1"

Dal 15/11/2026 le metriche si calcolano su una coppia fissa di agenti:

| | Agente | Opzioni |
| --- | --- | --- |
| UW Blink | **base_v2** (`UwAgentOptions{}`) | T1, W1, R1 accese; M1, M1-R, M3 **spente** |
| Rakdos Delirium | **base_r3** (`RakdosAgentOptions{}`) | mulligan C, X5, correzione del turno 1, X6 accese; X2, X3, X4 spente |

Liste: `config/decks/uw_blink_sim.json` e `config/decks/rakdos_delirium.json`; dati Oracle di `config/cards.json` (Scryfall del 9/11/2026). Seed da 1 a N, UW in play con il seed dispari.

**Riferimenti** (Release ricompilato da HEAD `828ba9c`, 15/11/2026). Qualsiasi modifica al codice, compresi lo strumento delle metriche e gli agganci della sezione 9, deve lasciare identici byte per byte questi registri:

| Registro | Comando | MD5 |
| --- | --- | --- |
| Matchup, 10.000 partite | `matchup.exe --replays 20` | `partite.jsonl` `2302d021603ed245978dc22d9cce88ae` |
| Matchup, primi 20 registri completi | come sopra | `replay.jsonl` `a9f2908d0209afc4a1621c9bcc23c094` |
| Matchup, 100.000 partite | `matchup.exe --games 100000` | `partite.jsonl` `0ad6744d85b251264f85ab4fd6155b60` |
| Matchup, 100.000 partite: i primi 10 registri completi | come sopra | `replay.jsonl` `9c13ada970161de30affee8ec24c83d7` |
| Goldfish, 10.000 partite | `goldfish.exe --games 10000 --replays 20` | `partite_C.jsonl` `63648b4c3600def504af46db50205d0c` |
| Goldfish, primi 20 registri completi | come sopra | `replay_C.jsonl` `eb4afb405eda3d5b84ff6c71cd323969` |
| Goldfish, 100.000 partite | `goldfish.exe` | `partite_C.jsonl` `3fc4b26daf526857f3ee70ab9f33d871` |

I due MD5 del matchup da 10.000 partite e quello del goldfish da 100.000 sono quelli di base_r3 nella sezione 24 di `docs/agente_rakdos.md`: il comportamento non è cambiato. Gli altri sono nuovi. Restano validi anche i riferimenti delle versioni precedenti (base_v1, base_r1, base_r2: sezione 28 di `docs/agente_uw.md`, sezioni 22 e 24 di `docs/agente_rakdos.md`).

Matchup da 100.000 partite con gli euristici v1: **UW 37,26%** (42,72% in play, 31,79% in draw), 74 s, 1.352 partite al secondo su un core. Mani tenute: UW 53,5% da 7, 38,7% da 6, 7,8% da 5; Rakdos 78,0%, 18,6%, 3,4%.

---

## 2. Il record compatto di ogni partita

Il registro completo (`replay.jsonl`) pesa circa 60 KB a partita (66 KB per i primi 20 seed, 59 KB per i primi 10): 100.000 partite sarebbero circa 6 GB. Per le metriche serve molto meno: **una riga JSON per partita**, scritta da uno strumento C++ nuovo (sezione 10), con tutto quello che le sezioni 3-9 usano e niente altro.

### Campi della riga

| Campo | Contenuto |
| --- | --- |
| `seed`, `uw_in_play` | Come in `partite.jsonl` |
| `vincitore`, `fine`, `turni`, `vita_uw`, `vita_rakdos` | Esito, come in `partite.jsonl` |
| `uw`, `rakdos` | Un oggetto per giocatore (sotto) |
| `delirium` | Turno della partita e turno di Rakdos in cui Rakdos raggiunge il delirium la prima volta (in qualsiasi momento, anche nel turno di UW); quante volte lo perde (per esempio con Thraben Charm che esilia il cimitero); `null` se mai |
| `minacce` | Una voce per minaccia grande di Rakdos (sezione 7) |

**Per giocatore:**

| Campo | Contenuto |
| --- | --- |
| `mulligan` | Mulligan fatti (0, 1, 2...) |
| `mani` | Ogni mano da 7 vista, in ordine (la prima è quella del mulligan contestuale, sezione 9) |
| `fondo` | Le carte messe in fondo (mulligan London) |
| `carte` | **Una voce per copia fisica** che arriva in mano o viene usata da un'altra zona: nome Oracle e la lista dei suoi eventi `[turno, codice]` |
| `terre` | Terre in campo alla fine di ogni proprio turno (`[1, 2, 3, 3, ...]`) |
| `mano_fine` | Carte in mano a fine partita |

**Codici degli eventi di una copia** (turno = turno della partita; il turno del giocatore si ricava in Python da `uw_in_play`):

| Codice | Evento | Fonte nel registro in memoria (`GameLog`) |
| --- | --- | --- |
| `I` | Nella mano iniziale tenuta (dopo le carte in fondo) | Mano alla prima decisione dopo il mulligan |
| `P` | Pescata (pescata del turno o di un effetto: Riddler, Bauble, FOMO, Teferi, ciclo di Wraith) | Spostamento grimorio → mano, causa pescata |
| `C` | Cercata (Recruiter, Starfield Shepherd, ciclo del Troll) | Grimorio → mano, causa "cercata" |
| `R` | Ritornata in mano (Teferi −3) | Campo → mano |
| `L` | Lanciata dalla mano | Mano → pila, causa "lanciata" |
| `T` | Giocata come terra (anche Witch-Blessed Meadow) | Mano → campo, causa "giocata" |
| `Y` | Lanciata dal cimitero (Nethergoyf, Detective's Phoenix) | Cimitero → pila |
| `X` | Lanciata dall'esilio (warp) | Esilio → pila |
| `E` | Esiliata come costo (evocazione di Solitude o Force of Negation) | Mano → esilio, causa "costo" |
| `S` | Scartata (Thoughtseize dell'avversario, FOMO) | Mano → cimitero, causa "scartata" |
| `K` | Ciclata (Street Wraith, Troll) | Mano → cimitero, causa "costo" del ciclo |
| `M` | Macinata | Grimorio → cimitero, causa "macinata" |

Esempio (accorciato):

```json
{"seed":1,"uw_in_play":true,"vincitore":"UW","fine":"vita a zero","turni":15,"vita_uw":10,"vita_rakdos":-5,
 "delirium":{"turno":6,"turno_rakdos":3,"perso":0},
 "uw":{"mulligan":0,"mani":[["Witch Enchanter","Recruiter of the Guard","Witch Enchanter","Quantum Riddler","Hallowed Fountain","Sanctifier en-Vec","Consign to Memory"]],
       "fondo":[],"terre":[1,2,3,4,4,5,5,6],"mano_fine":2,
       "carte":[{"c":"Sanctifier en-Vec","e":[[0,"I"],[3,"L"]]},{"c":"Solitude","e":[[7,"P"],[9,"L"]]}]},
 "rakdos":{"mulligan":1,"mani":[[...],[...]],"fondo":["Troll of Khazad-dûm"],"terre":[1,2,2,3],"mano_fine":0,"carte":[...]},
 "minacce":[{"c":"Moonshadow","entra":4,"grande":6,"esce":9,"come":"esiliata","da":"Solitude","danni":7}]}
```

**Dimensione stimata:** 1,5-2,5 KB a partita, 150-250 MB per 100.000 partite in JSON Lines; 15-30 MB in Parquet (sezione 10). Si misura sulle prime 1.000 partite prima di fissare il formato.

**Scelta di Claude: i nomi Oracle, non codici numerici.** I codici (indice in `config/cards.json`) ridurrebbero il file di circa tre volte, ma la riga non si leggerebbe più da sola e un cambio delle liste cambierebbe il significato dei numeri. Parquet comprime comunque i nomi ripetuti (codifica a dizionario).

---

## 3. GIH, OH, GD, GNS e IWD nel constructed

### Unità e definizioni

L'unità è la coppia **(partita, carta)** per il giocatore che ha la carta nel mazzo, con la carta identificata dal **nome Oracle**: le copie si sommano. Win rate sempre del giocatore che possiede la carta.

| Metrica | Partite contate (per la carta *c*) |
| --- | --- |
| **OH** (opening hand) | Almeno una copia di *c* nella **mano iniziale tenuta** (dopo le carte in fondo) |
| **GD** (game drawn) | **Nessuna** copia nella mano iniziale, almeno una **pescata** dopo (`P`) |
| **GIH** (game in hand) | OH ∪ GD |
| **GNS** (game not seen) | Nessuna copia mai vista: mai in mano per nessuna via (`I`, `P`, `C`, `R`) e mai lanciata da un'altra zona (`Y`, `X`) |
| **Solo cercata** | Nessuna copia iniziale o pescata, almeno una cercata (`C`): **fuori da GIH e da GNS** |
| **IWD** | GIH WR − GNS WR |

OH e GD sono disgiunte e la loro unione è GIH, come in 17lands. Ogni partita cade in esattamente una tra OH, GD, "solo cercata", "vista solo dal cimitero" (sotto) e GNS: è il primo controllo di coerenza (sezione 11).

### Carte in più copie

- **"Almeno una copia"** per tutte le metriche principali: è la definizione che permette il confronto con 17lands e con i dati di torneo.
- **Curva delle copie:** win rate per numero di copie viste nella partita (0, 1, 2, 3+), con IC. Dice quanto vale la seconda Solitude rispetto alla prima: l'IWD da solo non lo dice.
- **Quanto spesso una carta è vista** dipende dalle copie. A priori (circa 15 carte viste a partita su 60): una carta in 4 copie è vista in circa il 69% delle partite, in 3 copie il 58%, in 1 copia il 25%. Le carte da una copia (Teferi, Recruiter, Sanctifier, Seam Rip, Thraben Charm, Force of Negation, Troll) hanno quindi un GNS grande e un GIH piccolo; le carte da 4 il contrario. I numeri veri escono dai dati.

### Terre

- **Le terre si calcolano ma non entrano nella tabella principale.** L'IWD di una terra misura soprattutto "avere terre" (il contrario dell'ingolfamento o della mancanza di mana), non la carta. *Scelta di Claude*, come 17lands che tiene da parte le terre base.
- Per le terre: win rate per numero di terre nella mano iniziale e per turno della prima terra mancata (dal campo `terre`); a parte, le terre speciali (Meticulous Archive, Raucous Theater, Blackcleave Cliffs) contro le altre.
- **Witch Enchanter è una carta, non una terra**, anche se il mulligan la conta come terra (D-1): entra nella tabella principale, con la scomposizione degli usi (lanciata, giocata come terra, esiliata per Solitude, rimasta in mano).

### Carte che cercano o pescano

- **Le carte pescate da un effetto sono pescate** (`P`): Riddler, Bauble, FOMO, Teferi e il ciclo di Street Wraith pescano a caso dal grimorio, come la pescata del turno.
- **Le carte cercate no** (`C`): Recruiter sceglie Solitude quando c'è una minaccia, il Troll trova una Swamp quando serve una terra. La scelta dipende dalla partita, quindi una carta "solo cercata" non è un'estrazione casuale. **Scelta di Claude:** le partite "solo cercata" stanno fuori da GIH e da GNS, e si riportano a parte (WR quando la carta arriva solo per ricerca). Il loro valore resta alla carta che cerca: l'IWD di Recruiter comprende la Solitude che trova, senza contarla due volte.
- **Le carte che pescano allungano la partita e fanno vedere più carte** (Riddler soprattutto): gonfiano il GIH delle altre carte. Lo corregge l'IWD a turno fisso (sotto).
- **La carta che cerca o pesca** ha il suo IWD normale; in più si riporta cosa ha portato (per Recruiter: quale carta, in che turno, se usata e quando: D-2c).

### Carte che tornano in mano

Un ritorno (`R`, Teferi −3 sulle nostre creature o su quelle di Rakdos) non cambia la classe della partita: la copia era già stata vista. Si registra per contare i rilanci e per il turno in cui la carta torna disponibile. Se una copia arriva in mano per la prima volta con un ritorno (Nethergoyf lanciato dal cimitero e poi rimbalzato da Teferi), la partita è già "vista dal cimitero".

### Carte usate dal cimitero (Rakdos)

Nethergoyf e Detective's Phoenix si lanciano dal cimitero senza passare dalla mano: una Nethergoyf macinata e lanciata **conta come vista** (`Y`), ma non entra in OH o GD. **Scelta di Claude:** classe a parte "vista solo dal cimitero", fuori da GIH e da GNS come le cercate. Una carta macinata e mai usata resta GNS: il suo contributo al cimitero (delirium, forza di Nethergoyf) non è misurato dall'IWD, ma dalla sezione 6.

### I tre difetti noti di GIH e IWD, e come si leggono

1. **La lunghezza della partita.** Le partite lunghe fanno vedere più carte: GIH contiene più partite lunghe, GNS più partite corte. In questo matchup Rakdos vince le corte e UW le lunghe, quindi l'IWD **gonfia tutte le carte di UW e sgonfia tutte quelle di Rakdos**. Due correzioni:
   - **IWD relativo:** IWD della carta meno la media degli IWD delle carte non terra dello stesso mazzo (pesata per partite). Dice quali carte sono sopra la media, non quanto valgono in assoluto.
   - **IWD a turno fisso** (analisi "landmark"), **la metrica che propongo come principale per le decisioni**: al turno *T* del giocatore (3, 5, 7), tra le partite **ancora in corso** a quel turno, WR di chi ha visto la carta entro *T* meno WR di chi non l'ha vista. Il turno è fissato prima di guardare l'esito, quindi la lunghezza della partita non entra nel confronto.
2. **La selezione dei mulligan.** OH è condizionato dalle mani che l'agente tiene: Solitude in OH è anche "una mano tenuta perché aveva una risposta". L'OH WR va letto insieme all'analisi dei mulligan (sezioni 8 e 9).
3. **"Lanciata" non è una metrica di valore.** WR quando la carta è lanciata è fortemente distorto (Solitude si lancia quando c'è una minaccia; Teferi quando c'è il mana). Si riporta solo come descrizione (quota delle copie viste che vengono lanciate, turno medio del lancio, carte rimaste in mano a fine partita, come `carte_morte.exe`).

---

## 4. Correlazione tra carte

Nel constructed le carte si potenziano a vicenda (`docs/progetto.md`: l'IWD di Solitude va letto con quello di Ephemerate). Due misure.

**IWD condizionato.** IWD di *A* nelle partite in cui *B* è vista (GIH) e in quelle in cui *B* non è vista (GNS). Se l'IWD di Solitude è più alto con Ephemerate vista, le due si potenziano.

**Coppie: tabella 2×2 e interazione.** Per una coppia (*A*, *B*), il WR nelle quattro celle (entrambe viste, solo *A*, solo *B*, nessuna), con il numero di partite e l'IC di ognuna, e l'**interazione**:

> sinergia = (WR(*A*,*B*) − WR(*A*, non *B*)) − (WR(non *A*, *B*) − WR(non *A*, non *B*))

Positiva: insieme valgono più della somma. IC con il bootstrap (sezione 5). Si calcola anche a turno fisso (viste entro il turno *T*), per lo stesso difetto della lunghezza della partita.

**Coppie decise prima di guardare i dati** (*scelta di Claude*, da completare con Daniele):

| Mazzo | Coppia | Perché |
| --- | --- | --- |
| UW | Solitude + Ephemerate | La coppia della specifica; anche la mano da una terra di D-2 |
| UW | Solitude + Witch Enchanter | Witch come carta bianca da esiliare (sezione 9) |
| UW | Recruiter of the Guard + Solitude | D-2c: Recruiter trova Solitude |
| UW | Quantum Riddler + Consign to Memory | W1 |
| UW | Phelia + Solitude, Phelia + Ephemerate | Esilio e ritorno: trigger di entrata ripetuti |
| UW | Guide of Souls + Ocelot Pride | Le creature da un mana che crescono insieme |
| Rakdos | Nethergoyf + Stitcher's Supplier / Dark Matter Manipulator | Il cimitero che fa crescere Nethergoyf |
| Rakdos | Detective's Phoenix + Fear of Missing Out | Due delle minacce che vogliono il delirium |
| Rakdos | Thoughtseize + Lightning Bolt | Togliere la risposta, poi la creatura |

**Matrice esplorativa** di tutte le coppie di carte non terra di un mazzo (120 coppie per le 16 carte non terra di UW, 66 per le 12 di Rakdos): solo come mappa, con la correzione per confronti multipli (Benjamini-Hochberg al 5%) e senza conclusioni sulle coppie che non erano nella lista.

**Precisione attesa.** L'interazione somma l'incertezza di quattro celle: con 100.000 partite, per Solitude (4 copie) ed Ephemerate (3 copie) l'IC al 95% è circa **±1,3 punti**. Per vedere una sinergia di 1 punto servono circa 400.000 partite.

---

## 5. Intervalli di confidenza e numero di partite

**Ogni valore si mostra con il numero di partite e l'intervallo al 95%.** Nessun valore senza *n*.

**Proporzioni (GIH WR, OH WR, GNS WR, WR per categoria): intervallo di Wilson.** Con *p̂* = vittorie / *n* e *z* = 1,96:

> centro = (*p̂* + *z*²/2*n*) / (1 + *z*²/*n*); semiampiezza = *z* / (1 + *z*²/*n*) · √(*p̂*(1 − *p̂*)/*n* + *z*²/4*n*²)

Wilson e non l'approssimazione normale perché resta corretto con *n* piccolo e con *p̂* vicino a 0 o a 1 (categorie rare: Witch tenuta con 5 terre, delirium al turno 1).

**Differenze (IWD, sinergia, IWD condizionato, differenze a turno fisso): bootstrap sulle partite.** Si ricampionano le **partite** (i seed) con reimmissione, si ricalcola la statistica intera (anche le dimensioni dei gruppi cambiano), **B = 2.000** ricampionamenti, intervallo dei percentili 2,5 e 97,5. Il generatore del bootstrap ha un seed fisso, scritto nell'uscita (stessa regola di casualità del motore). Per le differenze **appaiate** (tenere contro mulligan sullo stesso seed, sezione 9) si ricampionano le coppie di partite. Controllo: per le differenze semplici il bootstrap deve dare lo stesso intervallo della formula normale appaiata già usata da `varianti_uw` e `varianti_rakdos` (con 100.000 partite le due coincidono entro pochi centesimi di punto).

**Precisione per numero di partite** (semiampiezza dell'IC al 95% per un win rate vicino al 40%):

| Partite | 500 | 1.000 | 5.000 | 10.000 | 30.000 | 100.000 |
| --- | --- | --- | --- | --- | --- | --- |
| Semiampiezza | ±4,3 | ±3,0 | ±1,4 | ±1,0 | ±0,55 | ±0,30 |

- Con 100.000 partite l'IWD di una carta ha un IC di circa **±0,7 punti**, sia per una carta da 4 copie (circa 69.000 GIH e 31.000 GNS) sia per una da una copia (circa 25.000 e 75.000).
- **Sotto le 1.000 partite** il valore si mostra in corsivo come "indicativo"; **sotto le 200** non si mostra (*scelta di Claude*, da fissare con Daniele).
- **Confronti multipli:** 28 carte non terra per 5 metriche sono 140 intervalli; circa 7 escluderanno lo zero per caso anche senza nessun effetto vero. Le conclusioni si prendono sulle carte e sulle coppie decise prima (sezione 4), non sui valori più estremi della tabella.

---

## 6. Win rate per turno del delirium

**Turno del delirium** = il turno di Rakdos (1 = il primo turno di Rakdos) in cui il cimitero di Rakdos arriva per la prima volta a quattro tipi di carta, in qualsiasi momento; se succede nel turno di UW, conta il turno di Rakdos appena giocato. Categorie: 1, 2, 3, 4, 5, 6 o più, mai.

**Tabella:** per categoria, in play e in draw, partite, WR di UW con l'IC di Wilson. In più: quante volte UW toglie il delirium dopo che è arrivato (per esempio con Thraben Charm) e il WR in quei casi.

**Come si legge.** È una descrizione, non un effetto: un delirium presto è anche il segno di una buona mano di Rakdos. "Mai" mescola le partite finite presto (vinte da chiunque) con quelle in cui UW ha tenuto il cimitero vuoto. Per la domanda della specifica ("quanto conta rallentare il loro cimitero") si usano:

- **il confronto a turno fisso:** all'inizio del turno *T* di UW, tra le partite ancora in corso, WR di UW con e senza delirium di Rakdos (per *T* = 2, 3, 4, 5);
- **le carte che rallentano il cimitero:** IWD a turno fisso di Sanctifier en-Vec e di Thraben Charm, e il turno del delirium nelle partite con Sanctifier in campo entro il turno 2 di UW contro le altre.

---

## 7. Risposta alla minaccia grande

**Minacce grandi** (dalla specifica): Moonshadow, Nethergoyf, Detective's Phoenix conferito (e la creatura che incanta); in più Troll of Khazad-dûm se lanciato. **"Grande"** dal momento in cui la forza arriva a 4 o più (*scelta di Claude*: Moonshadow entra 1/1 con sei segnalini −1/−1 e cresce; Nethergoyf dipende dal cimitero).

**Per ogni minaccia** il record (campo `minacce`) dà: la carta, il turno in cui entra, il turno in cui diventa grande, il turno in cui esce dal campo, **come esce** (esiliata, distrutta, rimbalzata, sacrificata, morta in combattimento, ancora in campo a fine partita) e **con quale carta di UW** (Solitude lanciata o evocata, March, Thraben Charm, Teferi, Phelia, il bloccante), i danni inflitti a UW mentre era in campo.

**Tabelle:**

1. Per minaccia × carta che la risolve: casi, WR di UW con IC, turni in campo (mediana), danni inflitti (media).
2. Per minaccia: quota "mai risolta" e WR di UW in quei casi.
3. Turno di arrivo della prima minaccia grande e WR di UW.

**Cosa serve nel motore.** Oggi lo spostamento nel registro in memoria (`LogEntry` di tipo `ZoneChange`) dice la carta, le zone e la causa, ma **non la fonte** (quale carta ha esiliato Moonshadow). Dedurla dalla pila è fragile (trigger, azioni di stato dopo il combattimento). **Proposta:** un campo `source` nella voce dello spostamento, scritto dal motore quando l'effetto ha una fonte. È una modifica del motore: test prima, e i registri della sezione 1 identici byte per byte (lo scrittore JSON non scrive il campo nuovo). Il danno per fonte c'è già (`LifeChange` ha la fonte).

---

## 8. Mulligan: mani da 7, 6 e 5 carte

Per giocatore, in play e in draw: partite e WR con l'IC per mano tenuta da 7, 6, 5 e 4 o meno carte. Poi per **tipo di mano tenuta**: terre (Witch compresa per UW), giocata entro il turno 2, risposta diretta; per Rakdos terre, creatura da un mana, carte che cercano. È il punto di partenza descrittivo: il confronto vero è nella sezione 9, perché il WR delle mani da 6 mescola "mano da 6" con "la mano da 7 era brutta".

---

## 9. Mulligan contestuale

L'ipotesi di Daniele **D-2b** (registro, 15/11/2026): tenere o no dipende dalla settima carta, non da una regola per categorie. E **D-2c**: Recruiter e Quantum Riddler sono risposte indirette. Qui si misura, prima dell'albero, quanto vale tenere ogni mano.

### 9.1 Caratteristiche di ogni mano da 7

Per **ogni prima mano da 7** (tenuta o no), calcolate in Python dalla mano e dal giocatore in play o in draw:

**UW:**

| Caratteristica | Definizione |
| --- | --- |
| Terre | Terre vere; Witch Enchanter a parte; totale come in D-2 |
| Creature da 1 mana, da 2 mana | Guide of Souls, Ocelot Pride; Phelia, Sanctifier en-Vec |
| Risposte dirette | Quante e quali tra Solitude, March, Sanctifier, Seam Rip, Teferi (le risposte di D-2) |
| Risposte indirette | Recruiter of the Guard, Quantum Riddler (D-2c) |
| Carte singole | Recruiter, Witch, Phelia, Ephemerate, Riddler, Consign, Force of Negation, Teferi: presenza e copie |
| **Carte bianche oltre a Solitude** | Quante carte bianche diverse da Solitude ci sono (per l'evocazione serve esiliarne una): Ephemerate, Guide, March, Ocelot, Phelia, Recruiter, Sanctifier, Seam Rip, Starfield Shepherd, Teferi, Thraben Charm, Witch (29 copie nella lista, colori da `config/cards.json`) |
| **Almeno 3 terre entro il turno 3** | Probabilità esatta (ipergeometrica) di avere almeno 3 terre, Witch compresa, entro il proprio turno 3: dalla mano e dalle 53 carte restanti, con 2 pescate in play e 3 in draw. Le ricerche (Recruiter, Shepherd) non contano |
| Play o draw | UW in play |
| Decisione di D-2 | Quello che l'agente ha fatto davvero, con la regola (dal record) |

*Perché "almeno 3 terre" come probabilità e non come esito:* l'esito (3 terre in campo al turno 3) dipende dalle pescate dopo la decisione, quindi non si può usare per decidere il mulligan; la probabilità sì. L'esito si riporta a parte, come descrizione.

**Rakdos** (stesso metodo, per R-MUL e X1-C): terre, creature da 1 mana (Supplier, Manipulator, Moonshadow, Nethergoyf), carte che cercano (Street Wraith, Bauble, Troll), Thoughtseize, Bolt, Flare, FOMO, Phoenix, play o draw.

### 9.2 Tenere contro mulligan a parità di seed

La prima mano da 7 di ogni giocatore dipende solo dal seed (da verificare con un test: sezione 11). Per ogni seed si giocano **due partite**:

- **K:** la prima decisione di mulligan del giocatore è forzata a "tieni";
- **M:** è forzata a "mulligan"; da lì in poi decide l'agente (anche se tenere la 6).

Tutto il resto è l'agente euristico v1. La differenza appaiata Δ = vittoria(K) − vittoria(M) (−1, 0 o +1 per seed) è **il valore di tenere quella mano**, per questo agente.

- **Come si forza.** Un aggancio facoltativo in `play_matchup` che intercetta la prima decisione di mulligan di un giocatore; l'agente non cambia. Senza aggancio la partita è identica (sezione 11).
- **Una sola fonte di casualità.** Lo stato ha un generatore solo: dopo un mulligan cambiano anche le mescolate successive (le fetch) e quindi le pescate di entrambi. Le due partite sono comunque partite valide dallo stesso seed, quindi il confronto non è distorto; è solo meno appaiato di quanto sarebbe con un generatore per giocatore. *Un generatore per giocatore* cambierebbe tutti i registri: **non lo propongo ora** (sezione 13).
- **Costo:** 2 partite per seed e per giocatore. 100.000 seed = 400.000 partite in più (UW e Rakdos separati).

### 9.3 Valore per tipo di mano

**Tipi di mano decisi prima** (*scelta di Claude*): terre (1, 2, 3, 4, 5+) × risposta diretta (sì, no) × giocata entro il turno 2 (sì, no) × play o draw. Per ogni tipo: mani, WR con K, WR con M, **Δ medio con IC al 95%** (bootstrap sulle coppie), e cosa fa D-2 in quel tipo.

**Precisione attesa:** Δ ha deviazione standard di circa 0,7 per seed. Per un tipo con 1.000 mani l'IC è circa ±4,3 punti, con 5.000 ±1,9, con 20.000 ±1,0. I tipi rari (una terra con Recruiter) chiedono **1.000.000 di seed** (sezione 10).

**La settima carta (D-2b).** Per le mani che stanno al confine (Δ vicino a zero o D-2 che le rifiuta) si guarda come cambia Δ con una carta: tra mani dello stesso tipo, Δ con e senza Recruiter, Witch, Phelia, Ephemerate, Riddler, Consign, Force of Negation, Teferi. Se la settima carta sposta Δ da negativo a positivo, D-2b ha ragione per quella carta.

### 9.4 Regressione logistica

Sulle 2*N* partite (K e M per ogni seed):

> logit P(vince) = α + β·x + tieni · (γ₀ + γ·x)

con *x* le caratteristiche della sezione 9.1 e play o draw. I coefficienti **γ** dicono **quanto ogni caratteristica cambia il valore di tenere**: è la forma esplicita di "dipende dalla settima carta". Errori standard raggruppati per seed (le due partite dello stesso seed non sono indipendenti); `statsmodels`.

- **Contro l'adattamento ai dati:** stima sui seed 1-50.000, verifica sui seed 50.001-100.000 (Δ previsto contro Δ osservato, per decili).
- **Politica di mulligan dalla regressione** ("tieni se il Δ previsto è positivo"): misurata su seed nuovi (100.001-200.000) con l'aggancio, contro D-2, differenza appaiata. Se migliora, diventa una **variante candidata** dell'agente, che entra nel registro delle ipotesi prima del codice (regola di `CLAUDE.md`) e decide Daniele.
- **Limite:** il valore di una mano è il valore **con il modo di giocare dell'agente v1**. Una mano che l'agente gioca male vale poco qui e forse molto per un pilota; l'albero la rimisura.

### 9.5 La domanda su Witch Enchanter (+7,14)

Nella scomposizione di M1 (sezione 29 di `docs/agente_uw.md`), le mani tenute grazie alla sola Witch vincono **+7,14 [+5,82; +8,46]** rispetto al mulligan. Da dove viene?

**Un fatto che restringe la domanda.** In quelle 8.699 mani **non c'è Solitude**, per costruzione: Solitude è una risposta di D-2, quindi una mano con Solitude non ha bisogno di M1 per essere tenuta. Witch come carta da esiliare per Solitude può contare **solo se Solitude arriva dopo** (pescata o cercata).

Tre spiegazioni, e come si separano:

| Spiegazione | Cosa si aspetta | Come si misura |
| --- | --- | --- |
| **(a) Il mulligan evitato:** quelle mani valgono più di una da 6 qualunque sia la settima carta | Δ simile nelle mani dello stesso tipo (terre, giocata, nessuna risposta) **senza** Witch | Sezione 9.3: Δ delle mani senza risposta, con e senza Witch, a parità di tipo |
| **(b) Witch come carta bianca per Solitude** pescata dopo | Δ più grande dove Solitude arriva dopo e Witch è ancora in mano; il guadagno sparisce se Witch non si può esiliare | Variante diagnostica "Witch non si esilia per Solitude" nelle partite K: la differenza appaiata con K è il valore dell'esilio. Più la descrizione: quante volte Witch viene davvero esiliata per Solitude in quelle partite |
| **(c) Witch come terra o come creatura** | Il guadagno resta anche senza l'esilio | Gli usi di Witch nelle partite K (terra, lanciata, esiliata, rimasta in mano); variante diagnostica "Witch mai come terra" |

Le varianti diagnostiche sono opzioni dell'agente spente, solo per misurare: prima del codice entrano nel documento dell'agente e nel registro (come M1-R e M3).

**Controllo che lega la sezione 9 alle misure già fatte:** le partite K dei seed in cui M1 cambia la decisione **da 7 carte** sono la stessa partita di M1 (M1 cambia solo il mulligan, poi l'agente gioca uguale). Su quei seed il confronto K − M deve **riprodurre esattamente** i numeri di M1 della sezione 29 per le decisioni a 7 carte (+9,96 su tutte le partite cambiate, +7,14 su "solo Witch", al netto delle partite in cui M1 cambia la decisione a 6 carte, che vanno contate a parte).

### 9.6 Confronto con D-2, M1, M1-R e M3

- **Accordo di D-2** con il segno di Δ per tipo di mano: in quanti tipi D-2 tiene quando Δ è positivo e rifiuta quando è negativo.
- **Punti persi da D-2:** la somma, sui tipi in cui D-2 sceglie il segno sbagliato, di |Δ| per le mani di quel tipo, divisa per i seed: quanto vincerebbe una politica che sceglie sempre il segno giusto (con la stima fatta sui seed di verifica, per non essere ottimisti).
- **M1, M1-R, M3** sono politiche che allargano D-2: il loro guadagno misurato (+1,18, +0,56, +0,38) si confronta con la somma dei Δ sulle mani che cambiano. Devono coincidere (stesso agente, stessi seed).
- **D-2c:** nelle partite con Recruiter: turno del lancio, carta cercata, turno in cui la Solitude cercata viene usata (lanciata o evocata), WR; per Riddler: carte pescate, quante risposte trovate e in che turno.

---

## 10. Formato, strumenti e tempi

**Formato: Parquet** (*scelta di Claude*; la specifica lascia aperto Parquet o SQLite).

| | Parquet | SQLite |
| --- | --- | --- |
| Lettura con pandas | Diretta e colonnare, veloce sui raggruppamenti | Riga per riga, più lenta su decine di milioni di righe |
| Liste annidate (le carte di una partita) | Supportate | Servono tabelle separate e join |
| Scrittura dal C++ | Richiede Apache Arrow: dipendenza pesante su MSVC, GCC e Clang | Un file sorgente (amalgamation), facile |
| Interrogazioni libere | Con pandas o DuckDB | SQL nativo |

**Pipeline proposta:**

1. **`metriche.exe` (C++, `tools/`):** gioca le partite con gli agenti euristici v1 e scrive il record compatto (sezione 2) in `data/metriche/v1/partite.jsonl`, più lo stesso `partite.jsonl` di `matchup.exe` per il controllo byte per byte. Opzioni come `matchup.exe` (`--games`, `--first-seed`, `--out`), più `--mulligan-forzato uw|rakdos` per le partite K e M della sezione 9. Il record si costruisce dal registro in memoria (`GameLog`, già usato da `varianti_uw`) e dallo stato alla fine: nessuna dipendenza nuova.
2. **`python/metriche/converti.py`:** JSON Lines → tre tabelle Parquet: `partite` (una riga per partita), `carte` (una riga per partita × giocatore × carta: copie viste, copie in OH, prima arrivata e come, primo lancio, usi), `mani` (una riga per prima mano da 7 con le caratteristiche della sezione 9.1 e, per le partite K e M, l'esito di entrambe).
3. **`python/metriche/*.py`:** le metriche delle sezioni 3-9; uscite in `data/metriche/v1/` (CSV e un riepilogo di testo); le tabelle finali vanno nel resoconto.

**Dipendenze Python:** pandas, pyarrow, numpy, statsmodels, con le versioni fissate nei metadati dello script (PEP 723, letti da `uv run`): nessun progetto Python nuovo, stesso uso di `uv` di oggi. I test Python restano `unittest`, nella CI.

**Tempi stimati** (un core, Release; misurati 74 s per 100.000 partite senza registro, circa 130 s con il registro in memoria in `varianti_uw`):

| Passo | 100.000 seed | 1.000.000 di seed |
| --- | --- | --- |
| Partite con il record (`metriche.exe`) | circa 2-2,5 min | circa 20-25 min |
| Partite K e M per UW e per Rakdos (400.000 per 100.000 seed) | circa 9 min | circa 90 min |
| Spazio su disco (JSON Lines, Parquet) | 150-250 MB, 15-30 MB | 1,5-2,5 GB, 150-300 MB |
| Conversione in Parquet | circa 1 min | circa 10 min |
| Metriche delle sezioni 3-8, con il bootstrap | 2-5 min | 20-40 min |
| Sezione 9 (tipi, regressione) | circa 1 min | circa 10 min |

**Parallelo per intervalli di seed** (`--first-seed`, `--games`, già nel matchup): 8 processi riducono le partite a circa un ottavo del tempo; i file per intervallo si concatenano nell'ordine dei seed e devono dare lo stesso MD5 dell'esecuzione unica.

---

## 11. Il gioco non cambia, e i controlli di coerenza

### Registrare non cambia la partita

1. **Solo lettura.** Il record si costruisce dal registro in memoria e dallo stato in sola lettura (come gli osservatori di `varianti_uw`): niente che consumi il generatore casuale. Le caratteristiche delle mani (sezione 9.1) si calcolano in Python, dopo.
2. **Registri identici byte per byte.** `metriche.exe` scrive anche `partite.jsonl`: deve avere gli MD5 della sezione 1 (10.000 e 100.000 partite). `matchup.exe` e `goldfish.exe` dopo le modifiche: tutti gli MD5 della sezione 1 e quelli di base_v1, base_r1, base_r2.
3. **Test Catch2 "il record non cambia la partita":** stesso seed con e senza record, stesso `GameLog` voce per voce e stesso esito.
4. **L'aggancio del mulligan:** senza aggancio, registri identici; con l'aggancio che **forza la scelta che l'agente farebbe comunque**, registri identici (prova che l'aggancio non tocca il generatore).
5. **Il campo `source`** (sezione 7), se si aggiunge al motore: i registri JSON identici, perché lo scrittore non lo scrive.

### Controlli di coerenza dei dati

- **Partizione:** per ogni carta, OH + GD + solo cercata + vista solo dal cimitero + GNS = tutte le partite; OH e GD disgiunte.
- **Conti delle carte:** per partita e giocatore, copie viste di una carta ≤ copie nella lista; carte nella mano iniziale = 7 − mulligan; arrivi in mano − uscite = `mano_fine`.
- **Riepilogo del matchup:** win rate totale, in play e in draw, distribuzione delle mani da 7, 6, 5 identici al riepilogo di `matchup.exe` sugli stessi seed (UW 37,26% su 100.000).
- **Due implementazioni dello stesso record:** per i primi 20 seed il record compatto ricalcolato in Python dal registro completo (`replay.jsonl`) deve coincidere con quello scritto dal C++.
- **Carte morte:** le carte in mano a fine partita coincidono con `carte_morte.exe` sugli stessi seed.
- **M1 riprodotta:** sezione 9.5.
- **La prima mano da 7 dipende solo dal seed:** test che la prima mano di ciascun giocatore è la stessa con e senza mulligan dell'altro.
- **Determinismo:** due esecuzioni danno gli stessi MD5; l'esecuzione in parallelo concatenata dà lo stesso MD5 di quella unica.
- **Bootstrap contro formula:** per le differenze semplici, IC del bootstrap uguale (entro 0,05 punti) a quello della formula normale appaiata.

---

## 12. Test prima del codice

- **C++ (Catch2):** partite preparate per ogni codice della sezione 2: pescata del turno e di Riddler, Recruiter che cerca Solitude, ciclo del Troll, Teferi che rimbalza, Nethergoyf lanciata dal cimitero, warp lanciato dall'esilio, Solitude evocata esiliando Witch, Witch giocata come terra, Thoughtseize che scarta, mulligan con carte in fondo; il turno del delirium e la sua perdita; una minaccia grande esiliata da Solitude con la fonte; "il record non cambia la partita"; l'aggancio del mulligan.
- **Python (`unittest`):** Wilson su valori noti; la partizione OH, GD, GNS su partite costruite a mano; IWD, sinergia e IWD a turno fisso su dati sintetici con il risultato noto; il bootstrap con il seed fisso ripetibile; la probabilità ipergeometrica delle 3 terre su casi calcolati a mano.

---

## 13. Decisioni aperte per Daniele

1. **IWD a turno fisso come metrica principale** per le decisioni, con l'IWD classico accanto per il confronto con 17lands?
2. **Le partite "solo cercata" e "vista solo dal cimitero" fuori da GIH e GNS?** L'alternativa è contarle in GIH (ma non sono estrazioni casuali).
3. **Le terre fuori dalla tabella principale?** Witch dentro, come carta.
4. **Soglie di visualizzazione:** indicativo sotto 1.000 partite, nascosto sotto 200?
5. **Le coppie della sezione 4:** quali aggiungere o togliere.
6. **"Minaccia grande" = forza 4 o più**, più Phoenix conferito e Troll?
7. **Il campo `source` nel motore** per sapere quale carta ha risposto alla minaccia.
8. **Tipi di mano della sezione 9.3** e lista delle caratteristiche: va bene come partenza? Quali carte contano per "la settima carta"?
9. **1.000.000 di seed per la sezione 9** (circa 2 ore su un core, circa 15-20 minuti in parallelo) o 100.000 prima?
10. **Un generatore casuale per giocatore** (le pescate di uno non cambiano con il mulligan dell'altro): migliora l'appaiamento, ma cambia tutti i registri e tutti i riferimenti. Non ora; da decidere prima della fase 4?
11. **Le varianti diagnostiche di Witch** (sezione 9.5): "Witch non si esilia per Solitude" e "Witch mai come terra".

---

## 14. Ordine dei passi proposto

1. Record compatto: test, strumento, controlli della sezione 11, misura della dimensione su 1.000 partite.
2. Conversione in Parquet e metriche per carta (sezioni 3-5), con i test Python.
3. Delirium e minacce grandi (sezioni 6-7), con il campo `source` se approvato.
4. Mulligan descrittivo e contestuale (sezioni 8-9), con l'aggancio e la riproduzione di M1.
5. Le varianti diagnostiche di Witch, se approvate.
