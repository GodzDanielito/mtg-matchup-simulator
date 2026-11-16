# Metriche stile 17lands (fase 3)

**Stato: approvata da Daniele il 16/11/2026** (proposta del 15/11/2026), con le undici decisioni e le quattro aggiunte A-D della sezione 13. **Passi 1, 2, 2b e 4 della sezione 14 fatti** (record compatto: sezione 15; Parquet e metriche per carta: sezione 16; ablazione: sezione 17; mulligan: sezione 18); il passo 5 non serve; il passo 3 (campo `source`) si fa **dopo gli agenti forti** (decisione di Daniele del 16/11/2026; proposta della ricerca in [ricerca.md](ricerca.md)). Ogni scelta che non viene dalla specifica (`docs/progetto.md`, "Metriche stile 17lands") è segnata come **scelta di Claude**.

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

Il registro completo (`replay.jsonl`) pesa circa 60 KB a partita (66 KB per i primi 20 seed, 59 KB per i primi 10): 100.000 partite sarebbero circa 6 GB. Per le metriche serve molto meno: **una riga JSON per partita**, scritta da `metriche.exe` (sezione 10) in `record.jsonl`, con tutto quello che le sezioni 3-9 usano e niente altro.

Il record si costruisce **solo dal registro in memoria** (`GameLog`) e dall'esito (`MatchupGame`), con una funzione pura (`build_compact_record`, `tools/metriche.hpp`): niente osservatori, niente stato, niente generatore casuale.

### Campi della riga

| Campo | Contenuto |
| --- | --- |
| `seed`, `uw_in_play` | Come in `partite.jsonl` |
| `vincitore`, `fine`, `turni`, `vita_uw`, `vita_rakdos` | Esito, come in `partite.jsonl` |
| `uw`, `rakdos` | Un oggetto per giocatore (sotto) |
| `delirium` | `turno`: turno della partita in cui il cimitero di Rakdos arriva per la prima volta a quattro tipi (in qualsiasi momento, anche nel turno di UW); `turno_rakdos`: il turno di Rakdos da cui vale, **con la convenzione del goldfish** (aggiunta D, sezione 6); `perso`: quante volte scende sotto i quattro tipi (Thraben Charm, fuga di Nethergoyf, conferimento di Phoenix); `fine`: delirium a fine partita. `null` se mai |
| `minacce` | Una voce per minaccia grande di Rakdos (sezione 7). **Dal passo 3**, insieme al campo `source` del motore: nel record del passo 1 non c'è |

**Per giocatore:**

| Campo | Contenuto |
| --- | --- |
| `mulligan` | Mulligan fatti (0, 1, 2...) |
| `mani` | Ogni mano da 7 vista, in ordine (la prima è quella del mulligan contestuale, sezione 9): la mano a ogni decisione di mulligan del giocatore, nell'ordine di arrivo delle carte |
| `fondo` | Le carte messe in fondo (mulligan London), nell'ordine scelto |
| `carte` | **Una voce per copia fisica** che arriva in mano o viene usata da un'altra zona: nome Oracle e la lista dei suoi eventi `[turno, codice]`. Prima le copie della mano iniziale tenuta, nell'ordine della mano, poi le altre nell'ordine del loro primo evento. I token non ci sono |
| `terre` | Terre in campo alla fine di ogni proprio turno (`[1, 2, 3, 3, ...]`); per l'ultimo turno, alla fine della partita. Una voce per ogni proprio turno iniziato. Witch Enchanter conta come terra se è entrata giocata come terra (Witch-Blessed Meadow) |
| `mano_fine` | Carte in mano a fine partita |

**Codici degli eventi di una copia** (turno = turno della partita; il turno del giocatore si ricava in Python da `uw_in_play`; prima del turno 1 il turno è 0):

| Codice | Evento | Fonte nel registro in memoria (`GameLog`) |
| --- | --- | --- |
| `I` | Nella mano iniziale tenuta (dopo le carte in fondo) | Mano al primo inizio di step (turno 1, stap), turno 0 |
| `P` | Pescata (pescata del turno o di un effetto: Riddler, Bauble, FOMO, Teferi, ciclo di Wraith) | Grimorio → mano, causa "pescata" |
| `C` | Cercata in mano (Recruiter, Starfield Shepherd, ciclo del Troll) | Grimorio → mano, causa "cercata" |
| `F` | Cercata e messa in campo (la terra di una fetch) | Grimorio → campo, causa "cercata" |
| `R` | Ritornata in mano (Teferi −3) | Campo → mano |
| `L` | Lanciata dalla mano (anche con il warp o l'evocazione) | Mano → pila, causa "lanciata" |
| `T` | Giocata come terra (anche Witch-Blessed Meadow) | Mano → campo, causa "giocata" |
| `Y` | Lanciata dal cimitero (Nethergoyf, Detective's Phoenix) | Cimitero → pila |
| `X` | Lanciata dall'esilio (warp, rimbalzo di Ephemerate) | Esilio → pila |
| `E` | Esiliata come costo (evocazione di Solitude o Force of Negation) | Mano → esilio, causa "costo" |
| `S` | Scartata (Thoughtseize dell'avversario, FOMO, scarto a fine turno) | Mano → cimitero, causa "scartata", senza un ciclo appena attivato |
| `K` | Ciclata (Street Wraith, Troll) | Mano → cimitero, causa "scartata", subito dopo l'attivazione del ciclo di quella carta (il motore registra il ciclo come uno scarto) |
| `M` | Macinata (anche in esilio per Sanctifier en-Vec) | Grimorio → cimitero o esilio, causa "macinata" |
| `V` | Messa nel cimitero sorvegliando (Meticulous Archive, Raucous Theater) | Grimorio → cimitero o esilio, causa "sorveglianza" |
| `?` | Spostamento da mano o grimorio, o verso la mano, non previsto | Qualsiasi altro: **non deve comparire mai** (test e controllo di `metriche.exe`) |

`F` e `V` sono aggiunte del passo 1 (*scelta di Claude*): sono spostamenti dal grimorio che la proposta non elencava. Uno scarto con Sanctifier en-Vec in campo va in esilio invece che nel cimitero: il codice dipende dalla causa, non dalla zona d'arrivo.

Esempio (accorciato):

```json
{"seed":1,"uw_in_play":true,"vincitore":"UW","fine":"vita a zero","turni":15,"vita_uw":10,"vita_rakdos":-5,
 "delirium":{"turno":6,"turno_rakdos":3,"perso":0,"fine":true},
 "uw":{"mulligan":0,"mani":[["Witch Enchanter","Recruiter of the Guard","Witch Enchanter","Quantum Riddler","Hallowed Fountain","Sanctifier en-Vec","Consign to Memory"]],
       "fondo":[],"terre":[1,2,3,4,4,5,5,6],"mano_fine":2,
       "carte":[{"c":"Sanctifier en-Vec","e":[[0,"I"],[3,"L"]]},{"c":"Solitude","e":[[7,"P"],[9,"L"]]}]},
 "rakdos":{"mulligan":1,"mani":[[...],[...]],"fondo":["Troll of Khazad-dûm"],"terre":[1,2,2,3],"mano_fine":0,"carte":[...]},
 "minacce":[{"c":"Moonshadow","entra":4,"grande":6,"forza_max":7,"esce":9,"come":"esiliata","da":"Solitude","danni":7}]}
```

(`minacce` dal passo 3.)

**Dimensione stimata:** 1,5-2,5 KB a partita, 150-250 MB per 100.000 partite in JSON Lines; 15-30 MB in Parquet (sezione 10). **Misurata al passo 1** sulle prime 1.000 partite: sezione 15.

**Scelta di Claude: i nomi Oracle, non codici numerici.** I codici (indice in `config/cards.json`) ridurrebbero il file di circa tre volte, ma la riga non si leggerebbe più da sola e un cambio delle liste cambierebbe il significato dei numeri. Parquet comprime comunque i nomi ripetuti (codifica a dizionario).

---

## 3. GIH, OH, GD, GNS e IWD nel constructed

### Unità e definizioni

L'unità è la coppia **(partita, carta)** per il giocatore che ha la carta nel mazzo, con la carta identificata dal **nome Oracle**: le copie si sommano. Win rate sempre del giocatore che possiede la carta.

| Metrica | Partite contate (per la carta *c*) |
| --- | --- |
| **OH** (opening hand) | Almeno una copia di *c* nella **mano iniziale tenuta** (dopo le carte in fondo) |
| **OH7** (aggiunta B) | Almeno una copia di *c* nella **prima mano da 7**, tenuta o no (`mani[0]`). Toglie la selezione dei mulligan: la prima mano dipende solo dal seed (test del passo 1). Si riporta accanto all'OH classico; non entra nella partizione sotto |
| **GD** (game drawn) | **Nessuna** copia nella mano iniziale, almeno una **pescata** dopo (`P`) |
| **GIH** (game in hand) | OH ∪ GD |
| **GNS** (game not seen) | Nessuna copia mai vista: mai in mano per nessuna via (`I`, `P`, `C`, `R`) e mai lanciata da un'altra zona (`Y`, `X`) |
| **Solo cercata** | Nessuna copia iniziale o pescata, almeno una cercata (`C`): **fuori da GIH e da GNS** |
| **IWD** | GIH WR − GNS WR |

OH e GD sono disgiunte e la loro unione è GIH, come in 17lands. Ogni partita cade in esattamente una tra OH, GD, "solo cercata", "vista solo dal cimitero" (sotto) e GNS: è il primo controllo di coerenza (sezione 11).

**Decisioni (16/11/2026).** Tra le metriche osservazionali la **principale è l'IWD a turno fisso**, con l'IWD classico accanto per il confronto con 17lands; **per decidere i side vale l'ablazione** (sotto, aggiunta A). Le partite "solo cercata" e "vista solo dal cimitero" stanno **fuori da GIH e da GNS**. Le terre stanno **fuori dalla tabella principale**; Witch Enchanter ci sta, come carta.

### IWD causale per ablazione (aggiunta A)

Per ogni carta non terra (16 di UW, 12 di Rakdos), stessi seed con **una copia sostituita da una terra base**: Plains per UW, Swamp per Rakdos. La differenza appaiata di win rate (lista vera − lista con la copia tolta), con l'IC al 95%, separata in play e in draw, è **il valore della carta rispetto a una terra**: non ha il difetto della lunghezza della partita né quello della sopravvivenza, perché si confrontano due partite intere dallo stesso seed, scelte prima di giocare.

- **Strumento:** opzione `--sostituisci "Carta:Terra"` di `metriche.exe` (si cambia la lista prima di giocare; gli agenti non cambiano). **Con l'opzione spenta, registri identici** a quelli della sezione 1.
- **Limiti:** cambia anche il numero di terre (una in più), quindi misura "questa carta contro una terra in più", non "contro una carta media"; gli agenti sono tarati sulle liste attuali, e con una copia in meno alcune regole (per esempio le soglie del mulligan) si comportano diversamente. Le carte da una copia spariscono del tutto dalla lista.
- **Appaiamento:** la copia sostituita è l'ultima della sua voce e la terra prende **la sua posizione** nella lista (`replace_one_copy`), quindi con lo stesso seed il grimorio si mescola uguale (stessi oggetti nello stesso ordine) e cambia solo la definizione di quella copia. Le due partite coincidono finché la copia non conta: quando arriva in mano (la prima differenza del registro è proprio quella voce), quando viene macinata o sorvegliata, o quando una decisione del suo proprietario guarda il grimorio (una ricerca, o Rakdos che sceglie una fetch o il ciclo del Troll perché nel grimorio c'è una Swamp in più). Test (c) del passo 2b, sezione 17.
- **Precisione:** con 100.000 seed per carta, una differenza appaiata di win rate tra due partite poco correlate ha IC di circa ±0,5-0,6 punti (in play e in draw separati circa ±0,8). Misurata: sezione 17.
- **Tempi stimati:** 28 carte × 100.000 partite, senza record (basta l'esito) a 74 s ogni 100.000: circa **35 minuti** su un core; con il record compatto (per scomporre la differenza per turno o per uso) circa 2,5 minuti per carta, **circa 70 minuti**. In 8 processi paralleli per intervalli di seed, 5-10 minuti. Misurati: sezione 17.
- **Ordine:** passo 2b della sezione 14, dopo le metriche per carta. **Fatto il 16/11/2026** (sezione 17).

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
   - **IWD a turno fisso** (analisi "landmark"), **la metrica principale tra le osservazionali** (decisione 1): al turno *T* del giocatore (3, 5, 7), tra le partite **ancora in corso** a quel turno, WR di chi ha visto la carta entro *T* meno WR di chi non l'ha vista. Il turno è fissato prima di guardare l'esito, quindi la lunghezza della partita non entra nel confronto.
   - **Attenzione (aggiunta C): condizionare sulle partite ancora in corso toglie quelle già decise dalla carta.** Una Solitude che al turno 3 ha già vinto la partita (o una carta che non è servita a evitare una sconfitta entro il turno 3) esce dal confronto prima di essere contata: l'IWD a turno fisso misura il valore della carta *da quel turno in poi, tra le partite arrivate fin lì*. È una **metrica descrittiva, non causale**. Il valore causale lo dà l'ablazione (aggiunta A).
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
| UW | Consign to Memory + Phelia | Decisione 5 (Daniele) |
| UW | Ephemerate + Quantum Riddler | Decisione 5 (Daniele) |
| UW | Thraben Charm + Sanctifier en-Vec | Decisione 5 (Daniele): le due carte contro il cimitero |
| Rakdos | Flare of Malice + Stitcher's Supplier | Decisione 5 (Daniele) |
| Rakdos | Street Wraith + Nethergoyf | Decisione 5 (Daniele) |

**Recruiter + Solitude per qualsiasi via (nota di Daniele, 16/11/2026).** Lo "0,0 al turno 5" di Recruiter + Solitude **non vuol dire "nessuna sinergia"**: una Solitude cercata da Recruiter è "solo cercata", quindi fuori da GIH per definizione, e la coppia normale non misura D-2c. Per questa coppia c'è una **variante con Solitude "in mano per qualsiasi via"** (I, P o C; il gruppo "non vista" resta GNS), classica e a turno fisso, accanto alla coppia normale (sezione 17). La misura causale è l'ablazione di coppia.

**Matrice esplorativa** di tutte le coppie di carte non terra di un mazzo (120 coppie per le 16 carte non terra di UW, 66 per le 12 di Rakdos): solo come mappa, con la correzione per confronti multipli (Benjamini-Hochberg al 5%) e senza conclusioni sulle coppie che non erano nella lista. **Dal passo 2 la mappa usa la sinergia al turno 5** (approvata da Daniele il 16/11/2026): con la sinergia classica 101 coppie di UW su 110 risultano oltre la soglia, tutte positive, e 30 delle 35 di Rakdos negative, cioè il difetto della lunghezza della partita (sezione 16).

**Le sinergie negative al turno 5 (25 su 38 per UW) possono essere un artefatto di selezione** (nota di Daniele, 16/11/2026): si condiziona sulle partite ancora in corso al turno 5 e sull'aver visto (o no) entrambe le carte entro quel turno, due selezioni che possono creare una correlazione negativa tra le celle anche senza un'interazione vera. **Per quelle coppie vale solo l'ablazione di coppia** (sezione 17).

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
- **Sotto le 1.000 partite** il valore si mostra in corsivo come "indicativo"; **sotto le 200** non si mostra (decisione 4).
- **Confronti multipli:** 28 carte non terra per 5 metriche sono 140 intervalli; circa 7 escluderanno lo zero per caso anche senza nessun effetto vero. Le conclusioni si prendono sulle carte e sulle coppie decise prima (sezione 4), non sui valori più estremi della tabella.

---

## 6. Win rate per turno del delirium

**Turno del delirium** = il turno di Rakdos (1 = il primo turno di Rakdos) **da cui** Rakdos ha il delirium: il cimitero di Rakdos arriva per la prima volta a quattro tipi di carta, in qualsiasi momento; se succede nel turno di Rakdos conta quel turno, **se succede nel turno di UW conta il turno successivo di Rakdos**. Categorie: 1, 2, 3, 4, 5, 6 o più, mai.

**Convenzione (aggiunta D): allineata al goldfish** (`rakdos_turn_reached`, `tools/goldfish.hpp`): il delirium raggiunto nel turno avversario si usa dal turno dopo, quindi i turni del delirium del goldfish e del matchup si confrontano direttamente. La proposta del 15/11 contava invece "il turno di Rakdos appena giocato": il record tiene anche il turno della partita (`delirium.turno`), da cui quella convenzione si ricava. **La differenza è misurata nella sezione 15:** riguarda solo le partite in cui il delirium arriva nel turno di UW, e lì vale esattamente un turno.

**Tabella:** per categoria, in play e in draw, partite, WR di UW con l'IC di Wilson. In più: quante volte UW toglie il delirium dopo che è arrivato (per esempio con Thraben Charm) e il WR in quei casi.

**Come si legge.** È una descrizione, non un effetto: un delirium presto è anche il segno di una buona mano di Rakdos. "Mai" mescola le partite finite presto (vinte da chiunque) con quelle in cui UW ha tenuto il cimitero vuoto. Per la domanda della specifica ("quanto conta rallentare il loro cimitero") si usano:

- **il confronto a turno fisso:** all'inizio del turno *T* di UW, tra le partite ancora in corso, WR di UW con e senza delirium di Rakdos (per *T* = 2, 3, 4, 5);
- **le carte che rallentano il cimitero:** IWD a turno fisso di Sanctifier en-Vec e di Thraben Charm, e il turno del delirium nelle partite con Sanctifier in campo entro il turno 2 di UW contro le altre.

---

## 7. Risposta alla minaccia grande

**Minacce grandi** (dalla specifica): Moonshadow, Nethergoyf, Detective's Phoenix conferito (e la creatura che incanta); in più Troll of Khazad-dûm se lanciato. **"Grande"** dal momento in cui la forza arriva a 4 o più (decisione 6: Moonshadow entra con sei segnalini −1/−1 e cresce; Nethergoyf dipende dal cimitero).

**Per ogni minaccia** il record (campo `minacce`, dal passo 3) dà: la carta, il turno in cui entra, il turno in cui diventa grande, **la forza massima raggiunta** (decisione 6), il turno in cui esce dal campo, **come esce** (esiliata, distrutta, rimbalzata, sacrificata, morta in combattimento, ancora in campo a fine partita) e **con quale carta di UW** (Solitude lanciata o evocata, March, Thraben Charm, Teferi, Phelia, il bloccante), i danni inflitti a UW mentre era in campo.

**Tabelle:**

1. Per minaccia × carta che la risolve: casi, WR di UW con IC, turni in campo (mediana), danni inflitti (media).
2. Per minaccia: quota "mai risolta" e WR di UW in quei casi.
3. Turno di arrivo della prima minaccia grande e WR di UW.

**Cosa serve nel motore.** Oggi lo spostamento nel registro in memoria (`LogEntry` di tipo `ZoneChange`) dice la carta, le zone e la causa, ma **non la fonte** (quale carta ha esiliato Moonshadow). Dedurla dalla pila è fragile (trigger, azioni di stato dopo il combattimento). **Approvato (decisione 7), al passo 3:** un campo `source` nella voce dello spostamento, scritto dal motore quando l'effetto ha una fonte. È una modifica del motore: test prima, e i registri della sezione 1 identici byte per byte (lo scrittore JSON non scrive il campo nuovo). Il danno per fonte c'è già (`LifeChange` ha la fonte).

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
| Carte singole | Recruiter, Witch, Phelia, Ephemerate, Riddler, Consign, Force of Negation, Teferi, **Starfield Shepherd, Thraben Charm** (decisione 8): presenza e copie |
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
- **Una sola fonte di casualità.** Lo stato ha un generatore solo: dopo un mulligan cambiano anche le mescolate successive (le fetch) e quindi le pescate di entrambi. Le due partite sono comunque partite valide dallo stesso seed, quindi il confronto non è distorto; è solo meno appaiato di quanto sarebbe con un generatore per giocatore. **Un generatore per giocatore: non ora** (decisione 10); si riprende **prima della fase 4**, con un ricalcolo unico di tutti i riferimenti MD5.
- **Costo:** 2 partite per seed e per giocatore. 100.000 seed = 400.000 partite in più (UW e Rakdos separati).
- **Quanti seed (decisione 9):** prima 100.000; poi 1.000.000 in parallelo per intervalli di seed, per i tipi di mano rari.

### 9.3 Valore per tipo di mano

**Tipi di mano decisi prima** (*scelta di Claude*): terre (1, 2, 3, 4, 5+) × risposta diretta (sì, no) × giocata entro il turno 2 (sì, no) × play o draw. Per ogni tipo: mani, WR con K, WR con M, **Δ medio con IC al 95%** (bootstrap sulle coppie), e cosa fa D-2 in quel tipo.

**Precisione attesa:** Δ ha deviazione standard di circa 0,7 per seed. Per un tipo con 1.000 mani l'IC è circa ±4,3 punti, con 5.000 ±1,9, con 20.000 ±1,0. I tipi rari (una terra con Recruiter) chiedono **1.000.000 di seed** (sezione 10).

**La settima carta (D-2b).** Per le mani che stanno al confine (Δ vicino a zero o D-2 che le rifiuta) si guarda come cambia Δ con una carta: tra mani dello stesso tipo, Δ con e senza Recruiter, Witch, Phelia, Ephemerate, Riddler, Consign, Force of Negation, Teferi, Starfield Shepherd, Thraben Charm (decisione 8; i tipi di mano sono approvati). Se la settima carta sposta Δ da negativo a positivo, D-2b ha ragione per quella carta.

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

Le varianti diagnostiche sono opzioni dell'agente spente, solo per misurare: prima del codice entrano nel documento dell'agente e nel registro (come M1-R e M3). **Si fanno solo se la 9.3 non basta** a separare le tre spiegazioni (decisione 11).

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

1. **`metriche.exe` (C++, `tools/`, passo 1):** gioca le partite con gli agenti euristici v1 e scrive il record compatto (sezione 2) in `data/metriche/v1/record.jsonl`, più lo stesso `partite.jsonl` di `matchup.exe` per il controllo byte per byte, e un riepilogo (`riepilogo.txt`) con le stesse win rate e mani tenute di `matchup.exe` e i controlli della sezione 11. Opzioni: `--games` (predefinito 100.000), `--first-seed`, `--out`. Dopo: `--mulligan-forzato uw|rakdos` per le partite K e M della sezione 9 (passo 4) e `--sostituisci "Carta:Terra"` per l'ablazione (passo 2b). Il record si costruisce dal registro in memoria (`GameLog`, già usato da `varianti_uw`): nessuna dipendenza nuova.
2. **`python/metriche/converti.py`:** JSON Lines → tre tabelle Parquet: `partite` (una riga per partita), `carte` (una riga per partita × giocatore × carta: copie viste, copie in OH, prima arrivata e come, primo lancio, usi), `mani` (una riga per prima mano da 7 con le caratteristiche della sezione 9.1 e, per le partite K e M, l'esito di entrambe).
3. **`python/metriche/*.py`:** le metriche delle sezioni 3-9; uscite in `data/metriche/v1/` (CSV e un riepilogo di testo); le tabelle finali vanno nel resoconto.

**Dipendenze Python:** pandas, pyarrow, numpy, statsmodels, con le versioni fissate nei metadati dello script (PEP 723, letti da `uv run`): nessun progetto Python nuovo, stesso uso di `uv` di oggi. I test Python restano `unittest`, nella CI.

**Tempi stimati** (un core, Release; misurati 74 s per 100.000 partite senza registro, circa 130 s con il registro in memoria in `varianti_uw`):

| Passo | 100.000 seed | 1.000.000 di seed |
| --- | --- | --- |
| Partite con il record (`metriche.exe`) | circa 2-2,5 min | circa 20-25 min |
| Partite K e M per UW e per Rakdos (400.000 per 100.000 seed) | circa 9 min | circa 90 min |
| Ablazione, 28 carte (aggiunta A) | 35 min senza record, circa 70 con | — |
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
- **La prima mano da 7 dipende solo dal seed:** test che la prima mano di ciascun giocatore è la stessa con e senza mulligan dell'altro (anticipato al passo 1: serve già all'OH7 dell'aggiunta B).
- **Determinismo:** due esecuzioni danno gli stessi MD5; l'esecuzione in parallelo concatenata dà lo stesso MD5 di quella unica.
- **Bootstrap contro formula:** per le differenze semplici, IC del bootstrap uguale (entro 0,05 punti) a quello della formula normale appaiata.

---

## 12. Test prima del codice

- **Passo 1, scritti** (`tests/test_metriche.cpp`, sezione 15): registri sintetici (voci del `GameLog` scritte a mano) per ogni codice, le mani del mulligan, le terre, il delirium e il formato JSON; partite vere del matchup confrontate con lo stato visto da un osservatore e con le carte morte; i codici che il motore produce davvero su 200 partite; "il record non cambia la partita"; "la prima mano da 7 dipende solo dal seed". Python: `python/tests/test_verifica_record.py`. Le partite preparate qui sotto restano per i passi 3 e 4 (minacce con la fonte, aggancio del mulligan).
- **C++ (Catch2), proposta:** partite preparate per ogni codice della sezione 2: pescata del turno e di Riddler, Recruiter che cerca Solitude, ciclo del Troll, Teferi che rimbalza, Nethergoyf lanciata dal cimitero, warp lanciato dall'esilio, Solitude evocata esiliando Witch, Witch giocata come terra, Thoughtseize che scarta, mulligan con carte in fondo; il turno del delirium e la sua perdita; una minaccia grande esiliata da Solitude con la fonte; "il record non cambia la partita"; l'aggancio del mulligan.
- **Passo 2, scritti** (sezione 16): `python/tests/test_statistica.py`, `test_converti.py`, `test_metriche_carte.py`, `test_dipendenze.py`.
- **Python (`unittest`):** Wilson su valori noti; la partizione OH, GD, GNS su partite costruite a mano; IWD, sinergia e IWD a turno fisso su dati sintetici con il risultato noto; il bootstrap con il seed fisso ripetibile; la probabilità ipergeometrica delle 3 terre su casi calcolati a mano.

---

## 13. Decisioni di Daniele (16/11/2026)

Proposte di Claude del 15/11/2026, **approvate da Daniele**, con quattro aggiunte (scelte di Claude, approvate insieme).

| # | Decisione | Dove |
| --- | --- | --- |
| 1 | L'**IWD a turno fisso** è la metrica principale tra le osservazionali, con l'IWD classico accanto; **per decidere i side vale l'ablazione** (A) | Sezione 3 |
| 2 | "Solo cercata" e "vista solo dal cimitero" **fuori da GIH e GNS** | Sezione 3 |
| 3 | Terre **fuori dalla tabella principale**; Witch Enchanter dentro, come carta | Sezione 3 |
| 4 | Soglie: **indicativo sotto 1.000 partite, nascosto sotto 200** | Sezione 5 |
| 5 | Coppie in più: UW Consign + Phelia, Ephemerate + Quantum Riddler, Thraben Charm + Sanctifier en-Vec; Rakdos Flare of Malice + Stitcher's Supplier, Street Wraith + Nethergoyf | Sezione 4 |
| 6 | Minaccia grande = **forza 4 o più**; il record tiene anche la **forza massima** | Sezione 7 |
| 7 | Campo **`source` nel motore: sì, al passo 3**, con i registri identici | Sezione 7 |
| 8 | Tipi di mano approvati; **Starfield Shepherd e Thraben Charm** tra le carte singole della "settima carta" | Sezione 9 |
| 9 | Sezione 9: **prima 100.000 seed, poi 1.000.000 in parallelo** | Sezione 9.2 |
| 10 | **Un generatore per giocatore: non ora**; da riprendere prima della fase 4, con un ricalcolo unico di tutti i riferimenti | Sezione 9.2 |
| 11 | **Varianti diagnostiche di Witch solo se la 9.3 non basta** | Sezione 9.5 |
| A | **IWD causale per ablazione**: una copia sostituita da una terra base, differenza appaiata con IC, play e draw separati; `--sostituisci "Carta:Terra"` | Sezione 3 |
| B | **OH sulla prima mano da 7** (OH7), tenuta o no, accanto all'OH classico | Sezione 3 |
| C | L'IWD a turno fisso toglie le partite già decise dalla carta: **descrittivo, non causale** | Sezione 3 |
| D | **Turno del delirium con la convenzione del goldfish**, con la differenza misurata | Sezioni 6 e 15 |

---

## 14. Ordine dei passi

1. **Fatto (16/11/2026).** Record compatto: test, strumento, controlli della sezione 11, misura della dimensione su 1.000 partite; in più il test "la prima mano da 7 di ogni giocatore dipende solo dal seed". Risultati nella sezione 15.
2. **Fatto (16/11/2026).** Conversione in Parquet e metriche per carta (sezioni 3-5, con OH7), con i test Python. Risultati nella sezione 16 e in [risultati_v1.md](risultati_v1.md).
   - 2b. **Fatto (16/11/2026).** Ablazione (aggiunta A): `--sostituisci`, 28 carte, 11 coppie, registri identici con l'opzione spenta. Risultati nella sezione 17 e in [risultati_v1.md](risultati_v1.md).
3. Delirium e minacce grandi (sezioni 6-7), con il campo `source` nel motore e la forza massima. **Dopo gli agenti forti** (decisione di Daniele del 16/11/2026): prima la ricerca ([ricerca.md](ricerca.md)), così le minacce si misurano anche sulle partite dei giocatori forti.
4. **Fatto (16/11/2026), prima del passo 3 per scelta di Daniele.** Mulligan descrittivo e contestuale (sezioni 8-9), con l'aggancio e la riproduzione di M1, su **100.000 seed** (non 1.000.000, decisione di Daniele del 16/11/2026). Risultati nella sezione 18 e in [risultati_v1_mulligan.md](risultati_v1_mulligan.md).
5. Le varianti diagnostiche di Witch, solo se la 9.3 non basta: **non servono** (sezione 18).

---

## 15. Passo 1: il record compatto (16/11/2026)

**Codice.** `tools/metriche.hpp` e `tools/metriche.cpp` (record dal `GameLog`, riga JSON, controlli di coerenza), `tools/metriche_main.cpp` (`metriche.exe`), `tests/test_metriche.cpp` (10 test case), `python/metriche/verifica_record.py` con `python/tests/test_verifica_record.py` (6 test). Nessuna modifica al motore né agli agenti.

### Controlli della sezione 11

| Controllo | Esito |
| --- | --- |
| `partite.jsonl` di `metriche.exe`, 10.000 partite | `2302d021603ed245978dc22d9cce88ae`: identico al riferimento della sezione 1 |
| `partite.jsonl` di `metriche.exe`, 100.000 partite | `0ad6744d85b251264f85ab4fd6155b60`: identico |
| `matchup.exe` (con `--rakdos-r1` e `--rakdos-r2`) e `goldfish.exe` dopo le modifiche | Tutti gli MD5 della sezione 1 e quelli di base_r1 e base_r2 identici |
| Riepilogo del matchup | UW 37,26% (42,72% in play, 31,79% in draw); mani da 7, 6, 5: UW 53,49%, 38,68%, 7,83%, Rakdos 78,03%, 18,56%, 3,41%: come `matchup.exe` |
| Determinismo | Due esecuzioni da 100.000: `record.jsonl` `6fc9e9fc6bcd4957ef2d857e0873ee94` entrambe |
| Esecuzione in parallelo | 8 processi da 12.500 seed, file concatenati nell'ordine dei seed: stessi MD5 di `record.jsonl` e `partite.jsonl` dell'esecuzione unica |
| Conti delle carte (`check_compact_record`, su ogni record) | **0 record con problemi su 100.000**: nessun codice `?`, copie viste ≤ copie nella lista, mani del mulligan da 7, mano iniziale = 7 − mulligan, carte in fondo = mulligan, arrivi − uscite = `mano_fine`, una voce di terre per ogni proprio turno |
| Record contro stato (test, 60 partite) | Mani di ogni mulligan, mano tenuta (contro `KeptHandTracker`), mano finale carta per carta, terre del giocatore attivo a fine partita, delirium a fine partita e prima del primo stato osservato con il delirium: tutto uguale |
| Carte morte (test, stesse 60 partite) | Partite con la carta in mano a fine partita, per carta e mazzo: uguali a `DeadCardTracker` (lo stesso osservatore di `carte_morte.exe`) |
| Due implementazioni | Primi 20 seed: record ricostruito in Python dal `replay.jsonl` di `matchup.exe --replays 20` contro `record.jsonl`: **0 differenze** (eventi per carta, mani, fondo, mulligan, delirium) |
| "Il record non cambia la partita" (test) | Stesso esito e stesso registro con e senza record (6 seed); il record è una funzione pura del registro |
| La prima mano da 7 dipende solo dal seed (test) | 20 seed, prima decisione di mulligan di ciascun giocatore forzata a "tieni" e a "mulligan": le prime mani dei due giocatori non cambiano e sono quelle pescate da `start_game` |

**Riferimenti del passo 1** (per i passi successivi): `record.jsonl` da 10.000 partite `472f0196517767e31b13be4f765b8809`, da 100.000 `6fc9e9fc6bcd4957ef2d857e0873ee94`.

### Dimensione e tempi

| | 1.000 partite | 100.000 partite |
| --- | --- | --- |
| `record.jsonl` | 2.540.343 byte, **2.540 byte a partita** | 252.829.715 byte (253 MB), 2.528 byte a partita |
| Tempo, un core (Release) | 1,0 s | **90 s** (1.109 partite al secondo) |
| Tempo, 8 processi | — | 14 s |

La dimensione è al limite alto della stima (1,5-2,5 KB): **il formato resta questo**. 253 MB per 100.000 partite stanno su disco senza problemi e sono esclusi da Git; per 1.000.000 di seed (2,5 GB) la conversione in Parquet del passo 2 li ridurrà. Il tempo è sotto la stima (2-2,5 minuti).

### Codici prodotti (100.000 partite)

| Codice | UW | Rakdos |
| --- | --- | --- |
| `I` mano iniziale | 645.666 | 674.623 |
| `P` pescata | 743.459 | 897.223 |
| `C` cercata in mano | 82.505 | 23.315 |
| `F` terra di una fetch | 204.411 | 255.032 |
| `R` ritornata in mano | 716 | 16.668 |
| `L` lanciata dalla mano | 623.746 | 901.626 |
| `T` giocata come terra | 467.013 | 438.761 |
| `Y` dal cimitero | — | 70.785 |
| `X` dall'esilio | 19.159 | — |
| `E` esiliata come costo | 75.796 | — |
| `S` scartata | 94.982 | 73.455 |
| `K` ciclata | — | 120.796 |
| `M` macinata | — | 659.454 |
| `V` sorvegliata nel cimitero | 3.732 | 20.479 |
| `?` | 0 | 0 |

`R` di Rakdos sono le creature di Rakdos rimbalzate da Teferi; `X` di UW comprende Quantum Riddler, Starfield Shepherd (anche lei ha il warp) ed Ephemerate (rimbalzo); `S` di UW sono soprattutto le carte prese da Thoughtseize.

### Il delirium con le due convenzioni (aggiunta D)

Delirium raggiunto in 93.454 partite (93,45%); **in 10.235 (10,95%) arriva nel turno di UW**, e solo lì le due convenzioni differiscono, sempre di un turno. Perso almeno una volta in 30.848 partite.

| Turno di Rakdos | Goldfish (adottata) | "Il turno appena giocato" (proposta del 15/11) |
| --- | --- | --- |
| 1 | 25.619 (25,62%) | 31.620 (31,62%) |
| 2 | 36.287 (36,29%) | 32.244 (32,24%) |
| 3 | 17.341 (17,34%) | 16.526 (16,53%) |
| 4 | 7.964 (7,96%) | 7.351 (7,35%) |
| 5 | 3.349 (3,35%) | 3.169 (3,17%) |
| 6 o più | 2.894 (2,89%) | 2.544 (2,54%) |
| Mai | 6.546 (6,55%) | 6.546 (6,55%) |

Con la convenzione del goldfish il turno è il primo turno di Rakdos in cui il delirium c'è già all'inizio o arriva durante il turno, cioè quando Rakdos può giocarci; con l'altra, il turno di Rakdos appena concluso quando il cimitero lo raggiunge nel turno di UW. Le tabelle della sezione 6 usano la prima; il record permette di ricalcolare la seconda (`delirium.turno`).

---

## 16. Passo 2: Parquet e metriche per carta (16/11/2026)

**Codice.** `python/metriche/statistica.py` (Wilson, formule normali, bootstrap), `python/metriche/converti.py` (record → Parquet), `python/metriche/metriche_carte.py` (metriche, CSV, `docs/risultati_v1.md`), `python/esegui_test.py` (esegue i test con le dipendenze fissate). Test: `python/tests/test_statistica.py`, `test_converti.py`, `test_metriche_carte.py`, `test_dipendenze.py`. Nessuna modifica al C++: motore, agenti e strumenti sono quelli del passo 1. **Le tabelle complete sono in [risultati_v1.md](risultati_v1.md)**, generato dallo script: non si modifica a mano.

**Dipendenze** (PEP 723, identiche in ogni script, controllate da un test): numpy 2.5.5, pandas 3.1.1, pyarrow 26.0.1; Python ≥ 3.12 (uv usa il 3.15.1).

### Le tre tabelle Parquet

| Tabella | Righe (100.000 partite) | Byte | Contenuto |
| --- | --- | --- | --- |
| `partite.parquet` | 100.000 | 1.668.731 | Esito, play o draw, turni, vite, delirium; per giocatore mulligan, carte, terre e (UW) Witch nella mano tenuta, mano finale, terre a fine turno, carte in fondo |
| `carte.parquet` | 4.100.000 (41 carte per partita) | 15.364.566 | Una riga per partita × giocatore × carta della lista, **anche se mai vista**: classe della partizione, copie con ogni codice (`k_I`...`k_V`), copie viste, lanciate, in mano alla fine, nella prima mano da 7, turno della prima vista in mano o pescata (`turno_ip`), turno della prima vista per qualsiasi via (`turno_vista`), turni propri del primo lancio di ogni copia |
| `mani.parquet` | una per mano da 7 vista | 2.418.941 | Seed, giocatore, play o draw, indice del mulligan, tenuta, carte, terre, Witch, esito. Le caratteristiche della sezione 9.1 arrivano al passo 4 |

In tutto **19,5 MB contro i 253 MB** del JSON Lines (13 volte meno; la stima della sezione 10 era 15-30 MB).

### Definizioni fissate nel codice

- **Classe** di una (partita, carta), sull'insieme dei codici di tutte le copie: OH se c'è `I`; altrimenti GD se c'è `P`; altrimenti "solo cercata" se c'è `C`; altrimenti "vista solo dal cimitero" se c'è `Y`, `X` o `R`; altrimenti GNS (anche una copia macinata e mai usata). Nei dati veri le 15.144 righe "solo cimitero" hanno tutte un lancio dal cimitero (nessuna con solo `X` o `R`).
- **Turno fisso T** del giocatore = turno della partita 2T − 1 in play, 2T in draw. **In corso**: la partita arriva a quel turno (`turni` ≥ quel turno). **Vista entro T**: una copia nella mano iniziale o pescata in un turno della partita ≤ quello, **tutto il turno T compreso** (la pescata del turno e quelle di Riddler nello stesso turno: il record non le separa). **Non vista entro T**: nessun evento di vista (`I`, `P`, `C`, `R`, `Y`, `X`) fino a quel turno. Le partite in corso con la carta vista entro T solo per ricerca o dal cimitero stanno fuori dal confronto, come nel classico.
- **IWD relativo**: IWD della carta meno la media degli IWD delle carte non terra dello stesso mazzo, **pesata per le partite del gruppo "vista"** (GIH per il classico, "vista entro T" per il turno fisso). Calcolato anche per T5.
- **Copie viste** (curva delle copie): copie con almeno un evento di vista, per qualsiasi via.
- **Sinergia**: celle tra le partite in cui ognuna delle due carte è GIH o GNS (per il turno fisso: "vista entro T" o "non vista entro T", tra le partite in corso).
- **Bootstrap**: una sola matrice di pesi (2.000 ricampionamenti delle 100.000 partite, PCG64 con seed 20261116) per tutte le carte, le coppie e i due mazzi: le statistiche che combinano più carte (IWD relativo, media per mazzo) restano coerenti dentro ogni ricampionamento. Ogni statistica è funzione di somme pesate, quindi il bootstrap è un prodotto di matrici (`statistica.somme_bootstrap`), esatto in float32 per gli interi fino a 2^24.
- **p della matrice**: bilaterale dalla deviazione standard del bootstrap (approssimazione normale), perché con B = 2.000 il p dei percentili non scende sotto 0,0005 e Benjamini-Hochberg su 110 coppie ne chiede di più piccoli.
- **Soglie**: un valore si mostra in corsivo sotto le 1.000 partite e non si mostra sotto le 200; per una differenza conta il gruppo più piccolo.

### Controlli della sezione 11

| Controllo | Esito |
| --- | --- |
| `metriche.exe` in 8 processi da 12.500 seed, file concatenati nell'ordine dei seed | `record.jsonl` `6fc9e9fc...` e `partite.jsonl` `0ad6744d...`: i riferimenti del passo 1 e della sezione 1; 0 record con problemi |
| Conversione | 0 problemi (righe per carta, copie viste ≤ copie nella lista, classe coerente con le copie iniziali e pescate, seed in ordine) |
| Partizione | 41 carte, ognuna con una riga per ognuna delle 100.000 partite: OH + GD + solo cercata + solo cimitero + GNS = 100.000; OH senza copia iniziale 0, GD con copia iniziale 0 |
| Copie viste ≤ copie nella lista | 0 righe oltre |
| Riepilogo del matchup | UW 37,26% (42,72% in play, 31,79% in draw); mani tenute come `matchup.exe`. In più, **partita per partita**: seed, play o draw, vincitore, turni, mulligan e vite finali uguali al `partite.jsonl` di `matchup.exe --games 100000` (MD5 `0ad6744d...`) |
| Carte in mano a fine partita | Uguali a `carte_morte.exe` sugli stessi seed: 123 valori (carta × tutte/play/draw), 0 diversi |
| Bootstrap contro formula normale | IWD classico delle 28 carte: scarto massimo degli estremi dell'intervallo **0,049 punti**, medio 0,023 (soglia 0,05). Nei test sintetici da 100.000 partite, sotto 0,05 sia per la differenza appaiata sia per quella tra gruppi |

Lo scarto massimo di 0,049 è vicino alla soglia: è l'errore Monte Carlo del percentile con B = 2.000 (circa 0,06 deviazioni standard della stima, cioè 0,02-0,05 punti con questi gruppi). Con B più grande scenderebbe; la soglia resta 0,05 e oggi è rispettata.

### Il difetto della lunghezza della partita

Media degli IWD delle carte non terra di ogni mazzo (pesata come l'IWD relativo), in punti:

| Metrica | Media UW | Media Rakdos | UW − Rakdos | Distorsione tolta |
| --- | --- | --- | --- | --- |
| IWD classico | +15,12 | +0,68 | **+14,44** [+14,15; +14,74] | — |
| IWD T3 | +2,00 | +2,63 | −0,63 [−0,81; −0,45] | 96% |
| IWD T5 | +4,39 | +4,18 | **+0,21** [−0,00; +0,44] | 98,5% |
| IWD T7 | +6,16 | +3,74 | +2,42 [+2,07; +2,79] | 83% |
| IWD relativo | 0 | 0 | 0 | per costruzione |

"Distorsione tolta" = 1 − |differenza| / 14,44. L'IWD a turno fisso toglie quasi tutta la distanza tra i due mazzi al turno 5; al turno 7 ne resta un sesto, perché le partite ancora in corso al turno 7 sono già una selezione (le lunghe, che vince UW) e il gruppo "non vista" è piccolo. **Le medie a turno fisso restano positive per entrambi i mazzi (da +2 a +6).** La spiegazione probabile, **non ancora misurata**: a parità di turno, chi non ha visto una carta non terra ha visto più terre (ingolfamento), un effetto comune a tutte le carte; lo toglie l'IWD relativo a turno fisso. L'IWD relativo classico toglie la media per costruzione ma non il difetto carta per carta (una carta da una copia e una da quattro ne soffrono in modo diverso). La stessa distorsione è nella **sinergia classica**: nella matrice esplorativa 101 coppie di UW su 110 oltre la soglia, tutte positive; al turno 5 ne restano 38 (13 positive, 25 negative). Per Rakdos 35 su 60 (30 negative); al turno 5 25 (20 negative).

### Cosa dicono (per questi agenti)

- **UW, IWD T5:** Ocelot Pride +18,2, Sanctifier en-Vec +17,3, Guide of Souls +15,5, Recruiter +9,2, Starfield Shepherd +9,0, Quantum Riddler +7,2; in fondo Force of Negation −5,4, Consign to Memory −5,3, Witch Enchanter −4,0, Solitude −3,7. Le carte in fondo sono anche quelle che gli agenti **tengono in mano**: Force of Negation resta in mano a fine partita nel 74,8% delle copie viste, Consign to Memory nel 56,9%, Witch Enchanter ed Ephemerate nel 31-32%.
- **Rakdos, IWD T5:** Dark Matter Manipulator +11,5, Fear of Missing Out +10,4, Nethergoyf +8,6; in fondo Flare of Malice −2,8 (resta in mano nel 28,0% delle copie viste), Troll −0,6, Thoughtseize −0,4. L'IWD classico di Thoughtseize (−7,9) era quasi tutto lunghezza della partita.
- **Coppie decise, sinergia T5:** Guide + Ocelot +13,1 [+11,7; +14,6], Riddler + Consign +9,5 [+8,2; +10,8], Solitude + Ephemerate +7,5 [+5,9; +9,1], Phelia + Solitude +4,3; Recruiter + Solitude **0,0** [−2,0; +2,1] (la classica, +11,1, era lunghezza della partita); Thraben Charm + Sanctifier −6,2 [−8,3; −4,1]. Rakdos: Flare of Malice + Stitcher's Supplier **+11,0** [+9,7; +12,4]; Nethergoyf + Dark Matter Manipulator −5,2, Nethergoyf + Supplier −3,5.
- **Terre:** UW vince di più con 2 terre nella mano tenuta (39,0%); con Witch contata come terra, con 2 (41,3%) più che con 3 (35,0%); Rakdos con 2 (65,6%) più che con 1 (62,9%) e molto più che con 4 (53,9%). Sono descrizioni condizionate dalle mani che l'agente tiene (sezione 9).

Sono **descrizioni del comportamento degli euristici v1**, non il valore delle carte: un IWD basso può voler dire che l'agente usa male la carta (Force of Negation, Consign to Memory). Il valore causale lo dà l'ablazione (passo 2b); la sezione 9 separa la mano dalla decisione di mulligan.

### Tempi

| Passo | Tempo |
| --- | --- |
| `metriche.exe`, 100.000 partite in 8 processi (il più lento) | 18,6 s |
| Concatenazione | 0,3 s |
| Conversione in Parquet | 43,9 s |
| Metriche con il bootstrap (lettura 0,6 s) | circa 27 s (calcolo 20 s) |
| Test Python (58) | 1,5 s |
| Controlli: `matchup.exe --games 100000` | 71,8 s |
| Controlli: `carte_morte.exe` | 73,0 s |
| Suite C++ in Release (529 test, invariata) | 19 s |

La prima esecuzione di `carte_morte.exe` ha riportato 6.536 s: tra l'inizio e la fine del file sono passate quasi due ore di orologio con il processo fermo (macchina probabilmente sospesa). Rifatta: 73,0 s, `carte_morte.csv` con lo stesso MD5 (`adc8dade...`).

**Registri della sezione 1 dopo il passo** (nessun C++ cambiato, Release ricompilato da `fbed81e`): matchup `--replays 20` `2302d021...` e `a9f2908d...`, matchup da 100.000 `0ad6744d...` e `9c13ada9...`, goldfish `--games 10000 --replays 20` `63648b4c...` e `eb4afb40...`, goldfish da 100.000 `3fc4b26d...`, base_r1 (`--rakdos-r1`) `81c39eba...` e `2b4dded8...`: tutti identici.

---

## 17. Passo 2b: ablazione (16/11/2026)

**Codice.** C++: `parse_substitution`, `replaced_copy_position`, `replace_one_copy`, `apply_substitution` in `tools/metriche.hpp`; opzioni `--sostituisci "Carta:Terra"` (ripetibile) e `--senza-record` di `metriche.exe`; test in `tests/test_ablazione.cpp` (8 test case). Python: `python/metriche/ablazione.py` (`gioca`: 8 processi per esecuzione, file concatenati nell'ordine dei seed; l'analisi la chiama `metriche_carte.py`), la colonna `turno_ipc` in `carte.parquet` (prima copia in mano per I, P o C) e la variante "B per qualsiasi via" delle coppie in `metriche_carte.py`; test in `python/tests/test_ablazione.py`, `test_converti.py`, `test_metriche_carte.py`. Motore e agenti non cambiano. Tabelle complete in [risultati_v1.md](risultati_v1.md), sezione "Ablazione".

**Approvato da Daniele (16/11/2026)**, con le quattro scelte proposte: le 11 coppie scelte tra tutte le coppie dei due mazzi, lo scarto di rango "grande" pari a un terzo del mazzo, il confronto dei segni "relativo" accanto a quello semplice, la copia sostituita è l'ultima della sua voce.

**Lista di simulazione, non lista vera (nota di Daniele, 16/11/2026).** L'ablazione usa `uw_blink_sim.json`: **4 Witch Enchanter e 3 Ephemerate** al posto di Aang, Swift Savior e Mockingbird, che il simulatore non ha (la lista vera, `uw_blink.json`, ha 3 Witch, 2 Ephemerate, 1 Aang, 1 Mockingbird). L'effetto di una copia dipende da quante altre copie restano: il valore di Witch Enchanter (−0,54) è quello della **quarta** Witch, e per la lista vera (3 Witch, quindi la terza copia) va letto con cautela; lo stesso per la terza Ephemerate (+0,33).

**Come si sostituisce.** L'ultima copia della prima voce con quel nome diventa una voce `{terra, 1}` subito dopo (con una copia sola la voce diventa la terra): la lista espansa cambia in una posizione sola, quindi `create_game` dà alla terra lo stesso `CardId` della copia e lo stesso seed mescola gli stessi oggetti nello stesso ordine. La carta si indica con il nome Oracle o con un prefisso unico tra i due mazzi (sulla riga di comando di Windows "Troll of Khazad-dûm" arriva storpiata: `ablazione.py` passa "Troll of Khazad-d").

### Test e controlli

| Controllo | Esito |
| --- | --- |
| (a) Opzione spenta | `metriche.exe` da 10.000: `partite.jsonl` `2302d021...`, `record.jsonl` `472f0196...` (i riferimenti); con `--senza-record` lo stesso `partite.jsonl`. Test: senza sostituzioni stesse liste e stessi registri |
| (b) Terra al posto di se stessa | Test: "Plains:Plains" e "Swamp:Swamp", 30 seed, registri identici voce per voce. Su 100.000 seed: `partite.jsonl` `0ad6744d...`, **lo stesso MD5 della base: differenza esattamente zero su tutti i seed** |
| (c) Prima divergenza | Test su Solitude, Ocelot Pride, Thoughtseize, Nethergoyf, 40 seed ciascuna: quando la copia arriva in mano, **la prima voce diversa dei due registri è quell'arrivo**, e differisce solo nella definizione della carta; se la copia non lascia mai il grimorio, partite identiche; se viene macinata o sorvegliata, la prima differenza è quello spostamento. L'unica divergenza prima è una decisione del proprietario che guarda il grimorio: trovata solo per Rakdos (3 partite su 80), sempre una giocata con una fetch al posto di un'altra (o di passare), perché nel grimorio c'è una Swamp in più da cercare. Il test accetta queste decisioni (ricerca, sorveglianza, priorità con una fetch o il ciclo del Troll) e nessun'altra |
| Grimorio | Test: con lo stesso seed, stessi `CardId` nello stesso ordine, definizioni uguali tranne quella della copia sostituita |
| Esecuzioni | 28 carte, 11 coppie e il controllo: 100.000 seed ciascuna, gli stessi della base (controllato seed per seed) |
| Bootstrap contro formula normale appaiata | 84 intervalli (28 carte × tutte, play, draw): scarto massimo 0,030 punti, medio 0,008 (soglia 0,05) |
| Registri della sezione 1 dopo il passo | Matchup `--replays 20` `2302d021...`/`a9f2908d...`, da 100.000 `0ad6744d...`/`9c13ada9...`, goldfish `--games 10000 --replays 20` `63648b4c...`/`eb4afb40...`, da 100.000 `3fc4b26d...`, base_r1 `81c39eba...`/`2b4dded8...`: identici |
| Suite C++ in Release | 537 test (erano 529); Python 75 (erano 58) |

**Un errore trovato dai test:** `check_compact_record` confrontava le copie viste con il conto dell'**ultima** voce con quel nome, non con la somma (`=` invece di `+=`): con le liste vere ogni nome ha una voce sola e non si vedeva; con "Plains:Plains" la lista ha due voci Plains e i record veri risultavano sbagliati. Test prima (rosso su ogni seed con almeno due Plains viste), poi la correzione. I risultati del passo 1 non cambiano: con una voce per nome la somma è il conto della voce.

### Risultati (per questi agenti)

Effetto = WR del proprietario con la lista vera meno con una copia sostituita, in punti (dettagli e play/draw in [risultati_v1.md](risultati_v1.md)):

- **UW:** Sanctifier en-Vec **+6,25** [+6,02; +6,48], Ocelot Pride +2,58, Guide of Souls +2,05, Seam Rip +1,99, Recruiter +1,69, Starfield Shepherd +1,52, Teferi +1,02, Quantum Riddler +0,96, Solitude +0,65, March +0,47, Ephemerate +0,33, Thraben Charm +0,18 (zero nell'IC), Phelia 0,00; **sotto una Plains:** Witch Enchanter −0,54, Consign to Memory −1,13, Force of Negation −1,36.
- **Rakdos:** Dark Matter Manipulator **+4,11**, Nethergoyf +3,81, Fear of Missing Out +3,62, Moonshadow +2,43, Mishra's Bauble +2,35, Lightning Bolt +2,10, Detective's Phoenix +2,08, Street Wraith +1,81, Troll +1,45, Stitcher's Supplier +1,33, Thoughtseize +0,98, Flare of Malice +0,80: **tutte sopra una Swamp**.
- **Media per mazzo:** UW +1,04 [+0,92; +1,16], Rakdos +2,24 [+2,09; +2,39]. Tre carte di UW valgono meno di una terra: le stesse che l'agente tiene in mano (Force of Negation nel 74,8% delle copie viste, Consign nel 56,9%, Witch nel 31,6%; sezione 16).
- **Esiti diversi** tra le due partite dello stesso seed: dal 7,7% (Witch) al 14,8% (Dark Matter Manipulator) dei seed; nel resto la copia non cambia il vincitore.

**Coppie: interazioni piccole, nessuna come la sinergia osservazionale.** Tutte tra −0,65 e +0,49 punti, contro sinergie al turno 5 da −7,6 a +13,1:

| Coppia | Sinergia T5 (osservazionale) | Interazione (ablazione) |
| --- | --- | --- |
| Guide + Ocelot | +13,1 | −0,24 [−0,42; −0,05] |
| Flare of Malice + Stitcher's Supplier | +11,0 | −0,65 [−0,87; −0,43] |
| Quantum Riddler + Consign to Memory | +9,5 | −0,25 [−0,41; −0,08] |
| Guide + Starfield Shepherd | +7,9 | −0,06 [−0,24; +0,13] |
| Solitude + Ephemerate | +7,5 | −0,23 [−0,40; −0,05] |
| Detective's Phoenix + Nethergoyf | −7,6 | −0,16 [−0,39; +0,05] |
| Thraben Charm + Sanctifier en-Vec | −6,2 | **+0,49** [+0,31; +0,68] |
| Nethergoyf + Dark Matter Manipulator | −5,2 | −0,16 [−0,38; +0,06] |
| Solitude + Starfield Shepherd | −4,9 | +0,03 [−0,15; +0,21] |
| Dark Matter Manipulator + Fear of Missing Out | −4,8 | −0,21 [−0,44; +0,00] |
| Recruiter + Solitude | 0,0 | +0,01 [−0,15; +0,19] |

Le sinergie osservazionali al turno 5, positive o negative, **non prevedono l'interazione causale**: le cinque più forti hanno interazioni negative o nulle (togliere due carte che fanno cose simili costa un po' meno della somma: rendimenti decrescenti), e la più negativa di UW (Thraben Charm + Sanctifier) ha l'unica interazione positiva chiara (le due carte contro il cimitero: senza Sanctifier, Thraben Charm conta di più). Conferma la nota di Daniele: per le coppie vale l'ablazione. Le coppie sono 11 e non 12 perché Solitude + Ephemerate è anche tra le cinque più forti.

**Recruiter + Solitude e D-2c.** Con Solitude "in mano per qualsiasi via" (I, P o C) la cella "entrambe" passa da 15.429 a 17.079 partite (le 1.650 "solo cercata"), e la sinergia resta nulla: classica +11,8 (era +11,1), **T5 +0,8 [−1,2; +2,9]** (era 0,0), T3 −1,6, T7 +5,1. L'ablazione di coppia dà +0,01. **Attenzione:** l'ablazione toglie Recruiter e **una** Solitude su quattro; con tre Solitude nel grimorio Recruiter ne trova ancora una, quindi neanche l'ablazione di coppia misura "Recruiter senza Solitude da cercare". Quella domanda (D-2c) chiede di togliere tutte le Solitude, o di misurare dove Recruiter cerca Solitude: passo 4 (sezione 9.6).

### Ablazione contro IWD

Spearman tra l'effetto e l'IWD (intervallo dal bootstrap congiunto: stessi ricampionamenti dei seed per le due misure):

| Metrica | UW (16) | Rakdos (12) | Tutte (28) |
| --- | --- | --- | --- |
| IWD classico | +0,92 [+0,89; +0,95] | +0,90 [+0,89; +0,95] | **+0,11** [+0,03; +0,21] |
| IWD T3 | +0,98 [+0,94; +0,99] | +0,94 [+0,90; +0,97] | +0,82 [+0,78; +0,87] |
| IWD T5 | +0,92 [+0,89; +0,94] | +0,95 [+0,92; +0,98] | **+0,83** [+0,78; +0,87] |
| IWD T7 | +0,74 [+0,68; +0,76] | +0,82 [+0,67; +0,89] | +0,62 [+0,54; +0,66] |

- **Dentro un mazzo l'ordine delle carte è quasi lo stesso** con tutte le metriche (0,90-0,98, meno al turno 7, dove i gruppi sono piccoli e selezionati). **Tra i due mazzi** l'IWD classico non dice niente (+0,11: è il difetto della lunghezza della partita, sezione 16), quello a turno fisso sì (+0,83).
- **Nessuna carta ha uno scarto di rango grande** (almeno 5 posizioni per UW, 4 per Rakdos: *scelta di Claude*, un terzo delle carte del mazzo). Il più grande è Solitude, 9ª per l'ablazione e 13ª per l'IWD T5.
- **Segno:** uguale per 21 carte su 28. Le 7 diverse hanno tutte un'ablazione positiva piccola (da 0,00 a +1,45) e un IWD T5 tra −3,7 e +1,2. **Tolta a tutte e due la media del mazzo** (ablazione meno la media, IWD T5 relativo) il segno è uguale per **25 su 28**, e le tre restanti (Quantum Riddler, Detective's Phoenix, Mishra's Bauble) sono a meno di 0,2 punti dalla media del mazzo con l'ablazione. Le due misure hanno scale diverse: l'IWD T5 vale circa 3,7-4 volte l'effetto di una copia (pendenza della retta tra le due, UW 3,73, Rakdos 3,95: l'IWD confronta "almeno una copia vista" con "nessuna", l'ablazione toglie una copia sola) più uno scarto positivo comune a tutte le carte (le medie per mazzo dell'IWD T5 sono +4,4 e +4,2).

**Le 7 carte con il segno diverso e la spiegazione più probabile** (indizi nella tabella di [risultati_v1.md](risultati_v1.md)):

| Carta | Ablazione | IWD T5 | Spiegazione più probabile |
| --- | --- | --- | --- |
| Solitude | +0,65 | −3,7 | **Selezione del mulligan e partite già decise.** D-2 tiene ogni mano con Solitude, quindi "Solitude vista presto" contiene anche mani deboli per il resto; Solitude si lancia presto (turno mediano 2) e le partite che decide prima del turno 5 escono dal confronto (aggiunta C). In più l'ablazione toglie la quarta copia, non tutte: con tre Solitude il valore marginale è piccolo. Le 1.650 partite "solo cercata" sono fuori dall'IWD |
| Flare of Malice | +0,80 | −2,8 | **Agente passivo e selezione:** resta in mano nel 28,0% delle copie viste (senza un bersaglio o senza una creatura da sacrificare), e chi la vede entro il turno 5 senza poterla lanciare ha una mano più lenta. Quando si lancia serve: l'ablazione è positiva |
| Thraben Charm | +0,18 | −1,9 | **Scala e una copia sola:** tutte e due sotto la media del mazzo (relativi concordi); l'effetto è nell'IC dello zero |
| March | +0,47 | −0,4 | **Scala:** IWD T5 nell'IC dello zero, tutte e due sotto la media del mazzo |
| Phelia | 0,00 | +1,2 | **Agente passivo e scala:** resta in mano nel 27,6% delle copie viste; la quarta Phelia vale una Plains. Tutte e due sotto la media del mazzo |
| Troll of Khazad-dûm | +1,45 | −0,6 | **Terra aggiunta e valore indiretto:** il Troll si cicla per una Swamp (quindi la Swamp che lo sostituisce non dà una terra in più) e mette una creatura nel cimitero (delirium, forza di Nethergoyf): il valore va ad altre carte, che l'IWD del Troll non vede; il costo (il mana del ciclo, presto) sì. Una copia sola |
| Thoughtseize | +0,98 | −0,4 | **Partite già decise e lunghezza residua:** il valore arriva al turno 1-2 (prende la risposta), e le partite che Rakdos vince grazie a quello prima del turno 5 escono dal confronto; l'IWD classico (−7,9) era quasi tutto lunghezza della partita |

Nessuna delle 7 è spiegata dalla lunghezza della partita da sola (l'IWD T5 la toglie già); "carta cercata" conta solo per Solitude.

### Limiti

- **L'ablazione aggiunge una terra al mazzo**: misura "questa carta contro una terra base in più", non "contro una carta media" né "contro niente". Con una terra in più cambiano anche le mani (più terre in media, quindi meno mani senza terre e più mani ingolfate), e la terra stessa ha un valore diverso nei due mazzi: per Rakdos una Swamp in più è anche un bersaglio delle fetch e del Troll (test c).
- **Gli agenti sono tarati sulle liste attuali**: le soglie del mulligan, D-2, le regole che contano le copie (per esempio quante risposte ci sono) restano le stesse con una carta in meno. Un pilota adatterebbe il gioco alla lista nuova.
- **Una copia, non la carta:** per le carte in più copie l'effetto è quello dell'ultima copia (rendimenti decrescenti); per quelle da una copia la carta sparisce dalla lista. Il confronto è **a livello di carta, non di lista ottimale**: un effetto negativo (Force of Negation, Consign, Witch) dice che per questi agenti quella copia vale meno di una terra base, non quale carta metterci.
- **Gli effetti sono piccoli** (da −1,4 a +6,3 punti per copia) e l'IC è circa ±0,2 punti (±0,3 in play o in draw), più stretto della stima della sezione 3 (±0,5-0,6): le due partite dello stesso seed coincidono finché la copia non conta e cambiano vincitore solo nel 7,7-14,8% dei seed.

### Medie degli IWD per mazzo (richiesta 6)

| Metrica | UW | Rakdos | UW − Rakdos |
| --- | --- | --- | --- |
| IWD classico | +15,12 [+14,94; +15,31] | +0,68 [+0,49; +0,88] | +14,44 [+14,15; +14,74] |
| IWD relativo | 0 per costruzione | 0 per costruzione | 0 |
| IWD T3 | +2,00 [+1,87; +2,12] | +2,63 [+2,49; +2,77] | −0,63 [−0,81; −0,45] |
| IWD T5 | +4,39 [+4,24; +4,54] | +4,18 [+4,01; +4,34] | +0,21 [−0,00; +0,44] |
| IWD T7 | +6,16 [+5,94; +6,41] | +3,74 [+3,47; +4,02] | +2,42 [+2,07; +2,79] |
| Ablazione (media semplice) | +1,04 [+0,92; +1,16] | +2,24 [+2,09; +2,39] | — |

L'ablazione ordina i mazzi al contrario dell'IWD classico: per questi agenti una carta media di Rakdos vale più di una Swamp di quanto una carta media di UW valga più di una Plains.

### Tempi

| Passo | Tempo |
| --- | --- |
| Ablazione delle 28 carte e controllo: 29 esecuzioni da 100.000 seed in 8 processi | 8,1 minuti (13-19 s per esecuzione) |
| Ablazione delle 11 coppie | 3,1 minuti |
| In tutto, 4.000.000 di partite | 672 s, circa 6.000 partite al secondo (la stima della sezione 3 era 35 minuti su un core, 5-10 in 8 processi) |
| Analisi in `metriche_carte.py` (lettura di 41 `partite.jsonl`, bootstrap) | 17 s |
| Conversione in Parquet rifatta (per `turno_ipc`), con le partite in parallelo | 84 s |

La prima esecuzione di Recruiter + Solitude ha riportato 2.299 s di orologio (le altre 13-19 s): come al passo 2, la macchina probabilmente sospesa. Rifatta: 13,0 s, stesso MD5 (`8ecd6bb1...`).

---

## 18. Passo 4: mulligan descrittivo e contestuale (16/11/2026)

**Approvato da Daniele (16/11/2026)**, con tutte e sette le scelte proposte: l'agente sceglie anche quando la decisione è forzata; le caratteristiche della regressione (le terre come probabilità di 3 entro il turno 3, più il numero e i casi 0 e 5+; le carte per gruppo più le singole); le mani al confine per la settima carta (senza risposta, senza giocata, una terra); la politica dal modello dei seed 1-50.000; un documento generato separato; statsmodels 0.15.0; nessuna variante diagnostica di Witch. M-LOG resta una variante candidata non adottata: gli agenti forti trattano il mulligan come decisione cercata e M-LOG è il riferimento da battere ([ricerca.md](ricerca.md), sezione 4.4).

**Richiesta di Daniele:** il passo 4 prima del 3, con 100.000 seed (non 1.000.000). Nessun agente cambia. Tabelle complete in [risultati_v1_mulligan.md](risultati_v1_mulligan.md), generato da `python/metriche/mulligan.py analizza` (non si modifica a mano).

**Codice.** C++: `ForcedMulligan` in `MatchupConfig` (l'aggancio: forza la decisione di mulligan numero *n* di un giocatore; l'agente sceglie comunque e la scelta viene sostituita, così il suo stato non cambia), `play_mulligan_pair` e `mulligan_pair_json` in `tools/metriche.hpp`, `metriche.exe --mulligan-forzato uw|rakdos`; test in `tests/test_mulligan_forzato.cpp` (5 test case). Python: `python/metriche/mulligan.py` (`gioca`, `analizza`), test in `python/tests/test_mulligan.py` (13 test). Nuova dipendenza **statsmodels 0.15.0** (regressione), fissata in tutti i blocchi PEP 723.

**Come si gioca.** Per ogni seed due partite: **K** con la prima decisione di mulligan del giocatore forzata a "tieni", **M** a "mulligan"; tutto il resto lo decidono gli euristici v1 (anche la decisione a 6 carte nella partita M). Una delle due è la partita normale. Per UW, in più, le **sonde** M1, M1-R e M3: agenti con quelle opzioni che, nello stesso stato, dicono cosa sceglierebbero a 7 carte e a 6 (nella partita M); dove una variante decide diversamente si gioca la sua partita. Seed 1-100.000 per UW (con il record di K e M) e per Rakdos; seed 100.001-200.000 per UW, solo per misurare la politica della regressione.

### Controlli

| Controllo | Esito |
| --- | --- |
| Aggancio spento (o che non scatta) | Test: registri identici; tutti gli MD5 della sezione 1 e di base_r1 identici dopo la modifica di `play_matchup`; `metriche.exe` da 10.000 `2302d021...` e `472f0196...` |
| Aggancio che forza la scelta dell'agente | Test (30 seed, i due giocatori, prima e seconda decisione): registri identici voce per voce |
| Scelta opposta | Test: registro identico fino a quella decisione, che ha la scelta forzata |
| K o M è la partita normale | Test (12 seed per giocatore): esito e record identici a quelli della partita normale |
| Le varianti a 7 carte giocano K o M | **11.349 + 2.942 + 1.436 partite di M1, M1-R e M3 identiche voce per voce a K o M**: le varianti cambiano solo il mulligan (`metriche.exe` esce con 1 altrimenti) |
| **Riproduzione di M1, M1-R, M3** (sezione 29 di `agente_uw.md`) | M1: **11.792 partite cambiate** (11.349 a 7 carte, 443 a 6), **+9,96** lì, **+1,18** sul totale; "solo Witch" **8.699, +7,14**; M1-R 3.122, +17,78, +0,56; M3 1.536, +24,54, +0,38: **tutti uguali ai numeri pubblicati**. I gruppi "entrambe" e "solo Recruiter" differiscono di una partita (1.363 contro 1.364, 1.730 contro 1.729): qui il gruppo è dato dalle carte nella mano tenuta, lì dalla carta che fa scattare il cambio |

### Valore di tenere (9.2, 9.3)

Delta = vittoria(K) − vittoria(M), in punti: **UW +10,5 [+10,1; +10,9]** (in play +11,3, in draw +9,6), **Rakdos +8,6 [+8,2; +9,0]**. In media tenere la prima mano da 7 vale 10 punti di win rate più del mulligan, per questi agenti.

**D-2 contro il segno di Delta:** D-2 concorda in **21 tipi su 34** (61.709 mani su 99.167). Rifiuta tipi con Delta positivo e grande: **due terre senza risposta, con una giocata: +13,9 in draw, +22,1 in play** (D-2 ne tiene lo 0-13%); tre terre senza risposta +8,8 e +17,2; quattro +3,9 e +9,6; **una terra con risposta e giocata +14,4 e +9,7** (D-2 ne tiene il 24%, quelle con Solitude ed Ephemerate); una terra senza risposta +5,4 e +8,6. Dove D-2 tiene (2-4 terre, risposta e giocata) Delta è positivo ovunque. Le mani con 0 o 5+ terre e quelle senza giocata hanno Delta per lo più negativo o nullo: lì D-2 ha ragione (eccezione indicativa: due terre con risposta e senza giocata in draw, +8,3 su 252 mani). **Punti persi da D-2:** la politica "tieni se il Delta del tipo è positivo" (segno stimato sui seed 1-50.000, misurata sui 50.001-100.000) vince **+3,76 [+3,43; +4,09]** punti più di D-2.

**Rakdos:** l'agente concorda in 37 tipi su 50 (86.543 mani su 97.959) e i punti persi sono **+0,06 [−0,14; +0,26]**: il mulligan di Rakdos (R-MUL con X1-C) è già quasi quello che i dati suggeriscono.

**Attenzione:** M è "mulligan, poi decide D-2" anche a 6 carte. Delta confronta tenere con il mulligan *come lo gioca questo agente*: se D-2 è troppo severo anche a 6 carte, M vale meno e Delta di più. Il confronto vale per questi agenti, non in assoluto.

### La settima carta (D-2b)

Nelle mani **senza risposta (2-4 terre, con giocata)**, 22.107, D-2 ne tiene il 3% e Delta è **+12,5**: tenerle conviene qualunque sia la settima carta, e **nessuna carta porta Delta da negativo a positivo** (è già positivo senza). Le carte che lo alzano di più: **Recruiter +7,5 [+5,1; +10,0]** (Delta +19,0 con Recruiter: +26,0 in play, +12,2 in draw), Thraben Charm +1,6; lo abbassano Phelia −8,6, Force of Negation −6,7, Witch −5,0, Consign −4,7. Nelle mani **senza giocata** (3.254, Delta −0,7) l'unica carta che sposta Delta in modo chiaro è **Quantum Riddler +8,3 [+4,0; +12,5]** (da −5,6 senza a +2,6 con). Nelle mani **con una terra** (13.969, Delta +11,1) nessuna carta lo rende negativo; Consign (−9,9), Force of Negation (−7,0) e Teferi (−5,5) lo abbassano di più.

**Per D-2b la risposta è "sì, ma non come confine":** la settima carta cambia il valore di tenere di parecchi punti (fino a ±9), ma per questi agenti quasi mai ne cambia il segno: il confine vero è altrove (il numero di terre e la giocata entro il turno 2).

### Regressione logistica (9.4)

Stima sui seed 1-50.000 (100.000 partite K e M), errori raggruppati per seed. I coefficienti di "tieni" più rilevanti (logit, |z| più grande): **creature da un mana +1,00** (z = 18,7), **nessuna terra −1,65**, **risposte dirette +0,62**, **Recruiter +0,77**, **Starfield Shepherd +0,66**, **Quantum Riddler +0,60**, probabilità di 3 terre entro il turno 3 +1,24, creature da due mana +0,46, Thraben Charm +0,55, Ephemerate +0,48, in play +0,18, **Witch −0,26** (l'unica carta con il segno negativo tra quelle con |z| ≥ 2), Consign +0,36, Force of Negation +0,29.

**Validazione sui seed 50.001-100.000:** Delta medio previsto +10,39, osservato +10,63; per decile del previsto, l'osservato segue il previsto da −10,4 (decile 1, previsto −11,3) a +35,3 (decile 10, previsto +33,7), sempre entro l'IC o vicino.

**Politica "tieni se il Delta previsto è positivo"**, misurata sui seed nuovi 100.001-200.000 contro D-2: **+4,09 [+3,84; +4,33]** punti (WR di UW 41,34% contro 37,25%). Tiene il 77,5% delle prime mani (D-2 il 53,4%), decide diversamente in 35.657 seed (29.883 tenute in più, 5.774 rifiutate in più), dove vale +11,48. **È solo una variante candidata, registrata (M-LOG nel registro), non adottata.**

### Witch Enchanter (9.5)

Le mani "solo Witch" a 7 carte (M1 le tiene, D-2 no): 8.407, K − M **+7,6 [+6,3; +9,0]**.

- **(a) Il mulligan evitato: è la spiegazione principale.** Nelle mani dello stesso tipo (giocata, nessuna risposta diretta), Delta **senza Witch** è più grande che con Witch: con 2 terre +20,0 contro +13,7, con 3 +15,6 contro +9,9, con 4 +5,7 contro +7,5. Tenere una mano senza risposta conviene già senza Witch; Witch, contata come terra, al posto di una terra vera vale meno (a 2-3 terre la mano ha una terra vera in meno), e nella regressione è l'unica carta con il coefficiente negativo (tra quelle con |z| ≥ 2).
- **(b) Witch come carta bianca per Solitude:** nelle partite K di quelle mani Witch viene esiliata per l'evocazione di Solitude nel **13,4%** (WR 53,0%); Solitude arriva dopo (pescata o cercata) nel 40,6%. Un contributo, non la spiegazione.
- **(c) Witch come terra o creatura:** giocata come terra nel **41,8%** (WR 46,5%), lanciata nel 23,6% (54,2%), ancora in mano alla fine nel **45,4%** (21,3%).

**Le varianti diagnostiche non servono** (decisione 11): la 9.3 separa già le spiegazioni, e (a) basta a dare conto del +7,14.

### D-2c: Recruiter e Riddler (9.6)

- **Recruiter of the Guard** (14.473 partite in cui viene lanciato): si lancia in media al **turno proprio 4,9-5,7**, non al 3; cerca **Sanctifier en-Vec nel 76,3%** dei casi (11.042, usato nel 74,8%) e **Solitude nel 18,6%** (2.687): Solitude cercata si usa nello stesso turno nel 48,2%, al turno dopo nel 9,8%, mai nel 38,0%. WR di UW 56,3% con Sanctifier cercato, 61,1% con Solitude.
- **Quantum Riddler** (37.905 partite in cui entra, al turno proprio 4,8 in media): nel turno in cui entra UW pesca 3,01 carte e 0,355 risposte dirette, contro 1,07 carte e 0,167 risposte nei turni senza Riddler: **Riddler aggiunge circa 1,9 carte e 0,19 risposte** per ingresso; almeno una risposta pescata nel suo turno nel 31,7% delle partite.
- **D-2c, per questi agenti:** Recruiter e Riddler sono risposte indirette nel senso che **alzano il valore di tenere** una mano senza risposta (Recruiter +7,5 punti, Riddler +8,3 nelle mani senza giocata) e hanno coefficienti positivi grandi nella regressione. Ma Recruiter trova soprattutto Sanctifier, non Solitude, e arriva più tardi del turno 3.

### Mulligan descrittivo (sezione 8)

WR per carte nella mano tenuta: UW 50,1% / 36,6% / 21,1% in play con 7, 6, 5 carte, 38,0% / 26,5% / 16,9% in draw; Rakdos 71,8% / 58,2% / 41,1% e 59,7% / 50,5% / 37,1%. Nessun giocatore tiene meno di 5 carte (con 5 si tiene sempre). Le tabelle per tipo di mano tenuta sono in [risultati_v1_mulligan.md](risultati_v1_mulligan.md).

### Tempi

| Passo | Tempo |
| --- | --- |
| `metriche.exe --mulligan-forzato uw`, seed 1-100.000, 8 processi, con il record di K e M | 36,9 s |
| `--mulligan-forzato rakdos`, seed 1-100.000 | 30,4 s |
| `--mulligan-forzato uw`, seed 100.001-200.000 | 32,4 s |
| Analisi (`mulligan.py analizza`: lettura, bootstrap, regressione, record) | 30 s |
| Spazio: `mulligan.jsonl` 90 MB (UW) e 52 MB (Rakdos); `record_K.jsonl` e `record_M.jsonl` 246 e 255 MB | — |

La stima della sezione 10 era circa 9 minuti per 400.000 partite su un core; in 8 processi le 600.000 partite (più le sonde e le partite delle varianti) richiedono 100 s.
