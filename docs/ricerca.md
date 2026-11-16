# Ricerca: un giocatore molto forte (fase 4)

**Stato: approvato da Daniele il 16/11/2026** (proposta dello stesso giorno), con le risposte e le tre correzioni della sezione 9: la posizione test 9 con due creature avversarie, il 99% di confidenza per scostarsi dall'euristico, le esecuzioni lunghe riprendibili. La domanda 6 ha la risposta di Daniele (le posizioni 6-10, sezione 5.1). **Passo 0 fatto** (16/11/2026): un generatore casuale per giocatore (sezione 5.3). **Passo 1 fatto** (16/11/2026): un agente per lato scelto dalla configurazione, la vista del giocatore e la determinizzazione con i loro test (sezione 3.7), le dieci posizioni test come stati preparati (sezione 5.1), la variante R-REC dell'euristico di UW (misurata, non adottata). La ricerca non è ancora implementata: il documento contiene le **misure di velocità**, fatte con il programma di prova `tools/sonda_ricerca.cpp` che usa il motore e gli agenti così come sono (sezione 1.2), e la proposta di come costruire il giocatore forte. Ogni scelta che non viene da Daniele o dalla specifica è segnata come **scelta di Claude**.

**Priorità di Daniele (16/11/2026):** un giocatore **molto forte**, per avere dati su giocate forti e non su regole euristiche. Gli agenti euristici v1 (UW base_v2, Rakdos base_r3: `docs/metriche.md`, sezione 1) restano il riferimento e la politica dei rollout; le loro regole sono le ipotesi da confermare o smentire (`docs/ipotesi_daniele.md`).

## In breve

| Domanda | Risposta (sezione) |
| --- | --- |
| Quanto costa un rollout? | Da metà partita, **0,53-0,58 ms su un P-core** (1.710-1.880 al secondo), 1,1-1,4 ms su un E-core; una partita intera 0,76-0,80 ms. La copia dello stato (11.584 byte) costa 0,1-0,2 µs: **trascurabile**. Due mosse valutate sugli stessi mondi hanno esiti correlati a **0,81**: la differenza appaiata ha **un terzo della varianza** (1) |
| Quanto fa la macchina, sostenuto? | Due regimi: dopo una partenza da fermo il portatile resta per 5-10 minuti in un **limite basso (8.000-8.900 rollout/s, con qualsiasi numero di processi)**, poi passa a un **regime stabile: 14.980 rollout/s con 20 processi** per 10 minuti (13.550 con 16, 12.000 con 12). Senza Game Shift (1.5) |
| Quanti processi? | **20** per le esecuzioni lunghe senza nessuno al PC; **16** se il PC serve nel frattempo (−10%). 16 MB di memoria per processo (1.5) |
| Che livelli? | **A** ricerca piatta con determinizzazione e rollout euristici; **B** ISMCTS; **C** rete di valore e politica allenata sulle partite di A e B. Si parte da A (2) |
| Informazione nascosta? | Si campiona solo ciò che il giocatore non sa, **anche nel proprio grimorio**, scambiando le identità delle carte nascoste; un test dello stato gemello verifica che la scelta non dipenda da nulla di nascosto (3) |
| Quali decisioni? | Mulligan, giocate di fase principale, risposte, bersagli, attacchi e blocchi; il resto all'euristica. Budget adattivo con numeri casuali comuni (4) |
| Come si valuta? | 10 posizioni test, curva di convergenza, partite appaiate contro gli euristici sugli stessi seed, forti contro forti, cinque versioni di avversario (5) |
| Quanto costano 10.000 e 100.000 partite? | Con UW forte al livello A e il budget proposto: **circa 2 ore e 20 ore** a 15.000 rollout/s (3,4 e 34 ore nel limite basso); con entrambi forti il doppio; il livello B circa 8 volte di più (7) |
| GPU? | Solo per il livello C (addestramento); A e B sono calcolo intero su CPU (8) |

---

## 1. Misure di velocità

### 1.1 Macchina

| | |
| --- | --- |
| Modello | Dell G15 5530 |
| Processore | Intel Core i7-13650HX: **14 core fisici** (6 P-core con hyper-threading, 8 E-core), **20 thread logici** (letti da Windows: `NumberOfCores` 14, `NumberOfLogicalProcessors` 20); frequenza nominale 2,6 GHz; L2 11,5 MB, L3 24 MB |
| Memoria | 16 GB |
| GPU | RTX 4060 Laptop, 8 GB (non usata nelle misure) |
| Alimentazione | Collegato alla corrente (batteria al 100%), piano energetico "Prestazioni elevate" |
| Game Shift (F9) | **Non disponibile**: Alienware Command Center dà un errore di installazione anche dopo la riparazione (Daniele, 16/11/2026). **Tutte le misure sono senza Game Shift**: con le ventole al massimo il throughput sostenuto potrebbe essere più alto |
| Compilazione | MSVC (Visual Studio 2026), Release, le stesse opzioni del repository |

### 1.2 Cosa si è misurato e come

**Programma di prova** `sonda_ricerca` (dal 16/11/2026 in `tools/sonda_ricerca.cpp`, con due test di fumo in `ctest`; le misure di questa sezione sono state fatte prima, con il generatore unico, quando il programma era ancora fuori dal repository). Usa le librerie `engine`, `agents` e `simulation` senza modificarle: motore e agenti sono quelli degli euristici v1. Il generatore per giocatore (passo 0) non cambia il costo di un rollout: cambiano solo le pescate.

- **Stati di metà partita:** per i seed 1-1.000 si gioca la partita degli euristici v1, se ne prende la lunghezza L (turni della partita) e la si rigioca fino al turno max(3, (L+1)/2), alla prima priorità del giocatore attivo nella **prima fase principale con la pila vuota**. 1.000 stati: UW è il giocatore attivo in 474; turno medio 6,81; **3,98 azioni legali in media** (massimo 8).
- **Rollout:** copia dello stato, **determinizzazione di prova** dal punto di vista del giocatore attivo (sezione 3: le carte nascoste a lui si scambiano l'identità tra loro), generatore della partita con un seed nuovo, due agenti euristici nuovi (seed del rollout), partita fino alla fine o al limite di 60 turni, **registro spento** (`GameLog* = nullptr`). In media un rollout da metà partita gioca **6,9 turni e 133 decisioni**.
- **Determinizzazione di prova:** 0 stati su 1.000 con gli invarianti violati dopo lo scambio (`check_invariants`). Non è la determinizzazione proposta: serve solo a misurarne il costo.
- **Un core:** il programma fissato a un processore logico (`ProcessorAffinity`), una volta su un P-core e una su un E-core. Senza affinità Windows lo aveva messo su un E-core: la prima misura era la metà di quella su un P-core.
- **Sostenuto:** N processi indipendenti (12, 16, 20), ognuno con i suoi 1.000 stati, che fanno rollout in ciclo per 10 minuti e scrivono quanti ne finiscono ogni 10 secondi; ogni 10 secondi uno script legge le prestazioni del processore (`Win32_PerfFormattedData_Counters_ProcessorInformation`) e la memoria dei processi.

### 1.3 Un core

| Misura | P-core | E-core |
| --- | --- | --- |
| **Rollout da metà partita** | **0,53-0,59 ms (1.700-1.880 al secondo)** | 1,13-1,42 ms (700-885 al secondo) |
| Partita intera da inizio, registro spento | 0,76-0,80 ms (1.250-1.320 al secondo) | 1,76-1,79 ms (560 al secondo) |
| Tempo per decisione (rollout) | 4,0-4,4 µs | 8,6-10,7 µs |
| **Copia dello stato** (11.584 byte), 1.000 stati diversi (11,6 MB, fuori dalla L2) | 0,22 µs | 0,69-0,88 µs |
| Copia dello stato, 100 stati (1,2 MB, nella cache) | 0,12 µs | — |
| Determinizzazione di prova, copia compresa | 0,57-0,68 µs | 1,4-1,7 µs |
| Costruzione dei due agenti euristici | 2,4 µs | 4,3-6,6 µs |

- **La copia dello stato non conta:** 0,2 µs contro 580 µs di un rollout (0,04%). Il requisito "`GameState` trivially copyable" del motore ripaga: nessuna ottimizzazione della copia serve.
- **Un E-core vale circa metà di un P-core** in questo codice (salti, tabelle piccole, poco calcolo vettoriale).
- **Dove va il tempo di un rollout** (cronometri su 400.000 decisioni): `apply` **65%**, `legal_actions` 18%, scelta degli agenti 17%. Margini noti, da usare solo se servono: `legal_actions` restituisce un `std::vector` (un'allocazione per decisione); gli agenti contano ogni regola in una `std::map` con chiave stringa; gli agenti vanno costruiti da capo a ogni rollout (sezione 3.6).

### 1.4 Decisioni per partita

Partite intere degli euristici v1 (10.000 seed): **202,6 decisioni a partita**, 201,1 con almeno due opzioni (le priorità con il solo "passa" il motore le passa da sé). Per giocatore, decisioni con almeno due opzioni:

| Decisione | UW | Rakdos | Opzioni medie |
| --- | --- | --- | --- |
| Mulligan | 1,55 | 1,25 | 2,00 |
| Carte in fondo | 0,55 | 0,25 | 6,11 |
| **Priorità** | **78,2** | **67,7** | 2,50 |
| Vita o terra tappata | 1,75 | 1,65 | 1,99 |
| Ricerca (fetch, Recruiter, Shepherd, Troll) | 2,86 | 2,78 | 3,74 |
| Sorveglianza | 0,45 | 0,68 | 2,00 |
| Bersaglio | 2,72 | 4,23 | 3,29 |
| Pagamento del mana | 2,08 | 2,67 | 2,06 |
| Carta di Thoughtseize | — | 0,79 | 2,72 |
| Attaccanti (una decisione per creatura) | 8,59 | 9,10 | 2,02 |
| Bloccanti | 3,03 | 1,54 | 2,50 |
| Ordine dei trigger | 2,31 | 0,06 | 2,08 |
| Esilio dal cimitero (fuga, prove) | — | 1,67 | 3,56 |
| Altre (scarti, sacrifici, energia, modo, X, carte esiliate per il costo, rimbalzo, replicare, danno diviso) | 2,00 | 0,66 | — |
| **Totale** | **106,1** | **95,0** | |

**Le priorità per contesto** (per partita intera, e tra parentesi per rollout da metà partita), con la quota in cui l'euristico passa:

| Priorità | UW: per partita | UW: opzioni, passa | Rakdos: per partita | Rakdos: opzioni, passa |
| --- | --- | --- | --- | --- |
| Propria fase principale, pila vuota | **18,4** (10,7) | 3,15; passa 38% | **22,5** (11,9) | 3,22; passa 20% |
| Pila non vuota (risposta) | 17,7 (11,9) | 2,31; passa 98% | 11,5 (8,1) | 2,16; passa 98% |
| Altro, pila vuota (turno avversario, altri step) | 42,2 (25,9) | 2,27; passa 96% | 33,6 (21,3) | 2,18; passa 98% |

Le scelte vere di un turno sono poche: **circa 18-23 priorità di fase principale per giocatore e per partita**, con 3,2 opzioni in media. Le priorità di risposta e degli altri momenti sono tre volte tante, ma l'euristico passa nel 96-98% dei casi: lì serve cercare solo quando c'è qualcosa da fare, e scoprirlo presto (sezione 4.2).

### 1.5 Throughput sostenuto (tutti i processori)

**Prima serie** (condizione normale, una dopo l'altra: 20 processi per 10 minuti, poi 16 e 12 per 5 minuti). Rollout al secondo della macchina intera, somma dei processi:

| Processi | Primi 20 s | 30-160 s | Dopo 160 s | Media degli ultimi 5 minuti |
| --- | --- | --- | --- | --- |
| 20 | 20.700 | 14.450-14.720 | 8.060-8.970 | **8.758** |
| 16 (subito dopo) | 10.470 | 8.550-8.900 | — | **8.863** |
| 12 (subito dopo) | 9.870 | 8.680-8.880 fino a 120 s, poi **11.730-12.330** | — | **10.778** (12.000 negli ultimi 3 minuti) |

- **Il portatile riduce la potenza in due gradini:** pieno turbo per circa 20 secondi, un limite intermedio fino a circa 160 secondi, poi un limite basso (con 20 processi è durato fino alla fine, 7 minuti; la seconda serie mostra che non è permanente). Le prestazioni dei P-core passano da circa il 117% della frequenza nominale (3,0 GHz) prima del gradino a circa l'85% (2,2 GHz) dopo.
- **Nel limite basso 12, 16 e 20 processi fanno quasi lo stesso** (8.700-8.900): conta la potenza, non i thread. Con 12 processi, dopo due minuti, la macchina è uscita dal limite basso (12.000).

**Seconda serie** (verifica, 10 minuti ciascuna: 12, poi 16, poi 20 processi; tra le due serie il portatile è rimasto fermo qualche minuto):

| Processi | Andamento | Media degli ultimi 5 minuti | Media dei 10 minuti |
| --- | --- | --- | --- |
| 12 | 16.860 nei primi 10 s, poi **limite basso** (7.300-8.700) per circa 5 minuti, poi 11.800-12.400 | **11.938** | 10.234 |
| 16 (subito dopo) | 13.300-14.000 per tutti i 10 minuti, senza cali | **13.534** | 13.571 |
| 20 (subito dopo) | 17.280 nei primi 10 s, poi 14.740-15.090 per tutti i 10 minuti | **14.983** | 15.009 |

**Lettura.**
- Il portatile ha **due regimi stabili**: un **limite basso** (8.000-8.900 rollout/s, uguale con 12, 16 e 20 processi: conta la potenza, non i thread; P-core a circa 2,2 GHz) e un **regime normale** (P-core a circa 3,0 GHz) in cui più processi rendono di più: **12.000 con 12, 13.550 con 16, 14.980 con 20**. Il limite basso è comparso **dopo le partenze da fermo** (prima serie, inizio della seconda) ed è durato 5-10 minuti; poi la macchina è passata al regime normale e ci è rimasta per 25 minuti di fila con 16 e 20 processi. Il passaggio lo decide il firmware: dall'esterno non si vede perché (la temperatura non è leggibile senza privilegi), e potrebbe tornare in esecuzioni di ore.
- **Il valore sostenuto per le stime: 15.000 rollout/s con 20 processi** (il regime di 10 minuti più recente), con accanto il **caso peggiore di 8.800** (limite basso per tutta l'esecuzione): i tempi della sezione 7 hanno le due colonne. Le esecuzioni lunghe misureranno il loro throughput e lo scriveranno nel resoconto.
- **Hyper-threading e E-core rendono:** da 12 a 20 processi +25% nel regime normale. Un processo rende circa 750 rollout/s con 20 processi, contro 1.880 da solo su un P-core.

**Memoria:** **16,4 MB per processo** al massimo (working set), di cui 11,6 MB sono i 1.000 stati campionati; 20 processi usano 330 MB su 16 GB. Un processo di ricerca vera (sezione 2) ne userà di più per l'albero, ma resta sotto i 100 MB (sezione 8).

**Quanti processi.** **20** per le esecuzioni lunghe senza nessuno al PC: nel regime normale fanno il 10% più di 16 e il 25% più di 12, e nel limite basso non sono peggio. **16** quando il PC serve nel frattempo: lasciano 4 thread liberi e perdono circa il 10%. 12 non conviene mai. In ogni caso **16 MB per processo**, quindi la memoria non limita.

**Senza Game Shift.** Il Game Shift (F9) non è disponibile su questo portatile (sezione 1.1): il throughput sostenuto misurato è **senza** le ventole al massimo, e può essere inferiore a quello possibile con il Game Shift; in particolare il limite basso potrebbe essere un effetto termico che le ventole al massimo eviterebbero.

### 1.6 Varianza appaiata (numeri casuali comuni)

Una prova di come si confrontano due mosse, **non una ricerca**: dai 1.000 stati di metà partita, a = la scelta dell'euristico, b = un'altra azione legale a caso; per 40 mondi per stato, l'esito di a e di b **negli stessi mondi** (stessa determinizzazione, stesso generatore, stessi seed degli agenti) e di b in mondi indipendenti. 120.000 rollout, 62 s su un P-core.

| Misura | Valore |
| --- | --- |
| Correlazione tra gli esiti di a e b negli stessi mondi | **0,81** |
| E[(a − b)²], mondi comuni contro indipendenti | 0,096 contro 0,290: **un terzo** |
| Stati in cui a e b danno lo stesso esito in tutti i 40 mondi | **454 su 1.000 (45%)** |
| Negli altri: a vince più mondi di b / b più di a | 298 / 205 (43 pari) |
| Vittorie del giocatore attivo: a / b (mondi comuni) / b (indipendenti) | 49,7% / 48,0% / 48,3% |

- **I numeri casuali comuni valgono un fattore 3:** a parità di precisione sulla differenza tra due mosse servono un terzo dei rollout. Con un generatore per giocatore (passo 0, sezione 5.3) la prova rifatta dà correlazione **0,80** e rapporto 0,35, 44% di stati con esiti identici: invariata, perché le due mosse condividono già tutto il mondo (determinizzazione e generatori).
- **Quasi metà delle scelte di metà partita non cambia l'esito in 40 mondi:** sono le decisioni in cui la ricerca può fermarsi subito (sezione 4.2). Quali mosse siano non è stato misurato.
- **L'errore della stima:** con la correlazione 0,81 e vittorie intorno al 50%, la deviazione standard della differenza tra due mosse su R mondi comuni è circa √(0,096/R): **3,1 punti con R = 100, 1,4 con R = 500, 0,7 con R = 2.000**. La ricerca piatta separa bene differenze di 5-10 punti con 100-200 mondi; differenze di 1-2 punti chiedono migliaia di mondi per decisione, e per quelle vale la regola "a parità, l'euristico" (sezione 4.2).

### 1.7 Valori usati per le stime

| Grandezza | Valore |
| --- | --- |
| Throughput della macchina (rollout da metà partita al secondo) | **15.000** con 20 processi (regime normale); **8.800** nel caso peggiore |
| Costo medio di un rollout di una decisione qualsiasi | come uno da metà partita (le decisioni di inizio partita costano di più, quelle di fine meno); il mulligan 1,4 volte (partita intera) |
| Decisioni cercate per partita e per giocatore (livello A, sezione 4.1) | circa **42**: 18-23 priorità di fase principale, 12-18 di risposta, 4-6 di combattimento (attacco e blocco come insiemi), più il mulligan |
| Rollout per decisione cercata (budget adattivo, K ≈ 3) | **circa 190** in media: il 45% si ferma al primo giro (16 mondi × 3 azioni = 48), il resto arriva a circa 300 |
| Mulligan | 2 azioni × 1.000 mondi, a 1,4 volte il costo: circa 2.800 rollout equivalenti |

Sono **stime di ordini di grandezza**: il budget vero lo fissa la curva di convergenza (sezione 5.2).

---

## 2. Tre livelli

### Livello A: ricerca piatta con determinizzazione e rollout euristici

**Cosa fa.** A una decisione da cercare il giocatore forte:

1. prende le K azioni legali (dopo le potature della sezione 4.3);
2. campiona R **mondi**: una determinizzazione (sezione 3), un seed per il generatore della partita, un seed per gli agenti dei rollout;
3. per ogni azione e per ogni mondo: copia lo stato determinizzato, applica l'azione, poi gioca la partita fino alla fine con gli **agenti euristici v1 per entrambi i giocatori** (registro spento);
4. sceglie l'azione con più vittorie. **Gli stessi R mondi per tutte le K azioni** (numeri casuali comuni): le differenze tra le azioni sono appaiate, con varianza molto più bassa (sezione 1.6), quindi a parità di precisione servono meno rollout. È la tecnica già usata per i test A/B degli agenti, applicata dentro la decisione.

Le decisioni che non si cercano le prende l'agente euristico (sezione 4.1). La ricerca guarda **un passo solo**: dopo l'azione, tutto lo gioca l'euristico. È la ricerca di Monte Carlo "piatta" (Ward e Cowling, 2009, proprio su Magic: The Gathering).

**Cosa costa.** K × R rollout per decisione cercata. Con K = 3 e R = 128: 384 rollout, **0,22 s su un P-core**; con la macchina intera, a 15.000 rollout al secondo, 39 decisioni cercate al secondo (23 nel limite basso). Il costo di una partita è nella sezione 7.

**Difetti noti.**
- **Distorsione dei rollout:** il valore di un'azione è il valore *se poi si gioca come l'euristico*. Una carta che l'euristico usa male vale poco nei rollout: se l'euristico non lancia mai Force of Negation al momento giusto, tenerla in mano sembra inutile anche alla ricerca. A migliora le singole scelte, non le linee che l'euristico non conosce.
- **Modello dell'avversario fisso:** l'avversario dei rollout è l'euristico. La ricerca impara a battere *quell'*avversario: contro un avversario diverso il guadagno può essere minore (per questo la robustezza, sezione 5.5).
- **Niente inferenza:** i mondi sono campionati in modo uniforme tra quelli coerenti con le carte viste (sezione 3.4).
- **Strategy fusion: quasi assente al livello A.** Il difetto classico della determinizzazione (Frank e Basin, 1998) è scegliere, in ogni mondo, la mossa migliore *sapendo* com'è fatto quel mondo, e poi fare la media: la ricerca si comporta come se dopo la prima mossa potesse vedere le carte nascoste. Qui dopo la prima mossa giocano gli euristici, che vedono solo la propria informazione (lo verificano i test dello stato gemello degli agenti: `tests/test_rakdos_agent.cpp` e, dal passo 1, `tests/test_uw_agent.cpp`), quindi nessuna scelta futura sfrutta il mondo campionato. Resta la **non-località**: il valore di una mossa dipende anche da come l'avversario sarebbe arrivato in quella posizione, e il campionamento uniforme lo ignora.

**Quando si passa a B.** Quando A batte gli euristici in modo robusto (sezione 5) **e** la curva di convergenza (sezione 5.2) è piatta: più rollout non cambiano più le scelte, quindi il limite è la profondità (un passo) e la politica dei rollout, non il campionamento. In pratica: le posizioni test che chiedono **due decisioni di fila che l'euristico non fa da solo** (sezione 5.1) restano sbagliate con qualsiasi budget.

### Livello B: ISMCTS ad albero

**Cosa fa.** Information Set Monte Carlo Tree Search (Cowling, Powley e Whitehouse, 2012; su Magic anche Cowling, Ward e Powley, 2012): a ogni iterazione si campiona un mondo, si scende in un albero i cui nodi sono **insiemi di informazione del giocatore che cerca** (non stati), scegliendo con UCB tra le azioni disponibili in quel mondo, si aggiunge un nodo, si finisce con un rollout euristico e si aggiornano le statistiche lungo il percorso. Le statistiche di un nodo sono **condivise tra tutti i mondi**: è questo che toglie la strategy fusion anche più in profondità.

Scelte proposte (*scelte di Claude*):
- **Una sola vista (SO-ISMCTS) e avversario euristico nell'albero** come primo passo: le decisioni dell'avversario dentro l'albero le prende l'agente euristico con la sua mano del mondo campionato. Con le decisioni dell'avversario nell'albero (con UCB) un albero solo le sceglierebbe senza distinguere le sue mani possibili, cioè come se l'avversario non conoscesse la propria mano; la versione corretta è un albero per giocatore (MO-ISMCTS), secondo passo.
- **Albero limitato al turno corrente e al turno avversario successivo**, poi rollout: la partita media ha 13 turni e 200 decisioni, un albero intero non converge.
- **Chiavi dei nodi indipendenti dal mondo:** un'azione dell'avversario si identifica dalla carta (definizione), non dal `CardId`, che cambia da un mondo all'altro (sezione 3.2).
- **Banditi con disponibilità** (le azioni legali cambiano da un mondo all'altro: un bersaglio c'è in un mondo e non in un altro): UCB con il conteggio delle volte in cui l'azione era disponibile.

**Cosa costa.** Un'iterazione costa circa un rollout (la discesa nell'albero e l'aggiornamento sono poche decine di decisioni), ma per far crescere un albero utile servono più iterazioni di quanti rollout chieda A: ordine di grandezza **1.000-5.000 per decisione**, contro 200-500 di A. La memoria è piccola: un nodo sono poche decine di byte, 10.000 iterazioni sono meno di 1 MB.

**Difetti noti.** Le statistiche condivise tolgono la strategy fusion del giocatore che cerca, ma restano: la distorsione dei rollout (meno pesante, perché l'albero copre il turno); il modello dell'avversario euristico (fino a MO-ISMCTS); la non-località e l'assenza di inferenza; la varianza alta per decisione, perché ogni iterazione vede un mondo diverso.

**Quando si passa a C.** Quando B migliora su A ma è limitato dal tempo (la curva di convergenza sale ancora al budget massimo che ci si può permettere) o dalla distorsione dei rollout, **e** le partite di A e B hanno prodotto abbastanza posizioni per allenare una rete (ordine di grandezza: un milione di posizioni cercate, sezione 7).

### Livello C: rete di valore e di politica sulla GPU

**Cosa fa.** Una rete prende la **vista del giocatore** (la propria mano, le zone pubbliche, le carte note dell'avversario, vite, energia, turno, step, numero di carte in mano e nel grimorio, mulligan, play o draw) e dà due cose: il **valore** (probabilità di vittoria) e la **politica** (una probabilità per ogni azione legale). Si allena con le posizioni delle partite di A e B: per il valore l'esito della partita, per la politica le scelte della ricerca (le visite di B, o le vittorie per azione di A). Poi guida la ricerca come in AlphaZero: la politica ordina e pota le azioni, il valore sostituisce o accorcia i rollout (rollout troncati dopo qualche turno, poi il valore).

**Cosa costa.** L'addestramento: ore sulla RTX 4060 per una rete piccola (qualche milione di parametri) e qualche milione di posizioni. Nella ricerca: una valutazione di una rete piccola (due strati da 256) è nell'ordine di **10 µs su un core** in C++, contro 580 µs di un rollout: **circa 50 volte più valutazioni** nello stesso tempo, o la stessa ricerca 50 volte più veloce. Servono prima: una codifica della vista che non usi informazione nascosta (con il suo test), un formato per le posizioni, il collegamento C++/Python.

**Difetti noti.** La rete impara le scelte di A e B, con i loro difetti; un valore sbagliato in una zona dello spazio di gioco poco vista (per esempio una partita lunga con Teferi) sbaglia senza dirlo. Il self-play (rete contro rete) corregge questo, ma costa milioni di partite (`docs/progetto.md`, "Quante partite servono"). La rete non va confusa con un risultato: è uno strumento per cercare meglio, e va sempre misurata contro A e B sulle stesse posizioni test.

---

## 3. Determinizzazione

### 3.1 Cosa sa il giocatore

Un mondo campionato deve essere **coerente con tutto quello che il giocatore sa e con niente di più**. Il motore tiene già l'informazione necessaria: ogni oggetto ha `known_to` (un bit per giocatore), acceso dai cambi di zona (zona pubblica: tutti; mano: il proprietario), da Thoughtseize (la mano rivelata), da Mishra's Bauble (la cima guardata), dalle ricerche che rivelano (Recruiter, Starfield Shepherd, ciclo del Troll), e spento da ogni mescolamento del grimorio.

| Informazione | Legittima? | Dove sta nel motore | Uso nella determinizzazione |
| --- | --- | --- | --- |
| La propria mano | Sì | `hand` del giocatore | Fissa |
| Le zone pubbliche (campo, cimiteri, esilio, pila), vite, energia, segnalini | Sì | Stato | Fisse |
| **La lista del proprio mazzo** e quindi la composizione del proprio grimorio | Sì | Lista + zone viste | Il proprio grimorio = lista meno le carte viste |
| **La lista del mazzo avversario** | **Ipotesi**: i giocatori conoscono la lista dell'archetipo, non le 60 carte esatte | Lista | Le carte nascoste avversarie = lista meno quelle pubbliche e note (domanda 1) |
| Carte avversarie note: mano rivelata da Thoughtseize, carte cercate e rivelate, carte tornate in mano da una zona pubblica (il −3 di Teferi) | Sì | `known_to` con il proprio bit | Fisse, nella loro zona |
| La cima del grimorio guardata con Bauble o con la sorveglianza lasciata in cima | Sì, finché non si mescola | `known_to` | Fissa nella sua posizione |
| **Le carte messe in fondo con il mulligan London** | Sì per il proprietario, no per l'avversario | `known_to` del proprietario (resta acceso andando nel grimorio) | Nel proprio grimorio: fisse in fondo, nell'ordine scelto. Nel grimorio avversario: carte sconosciute in fondo |
| Numero di carte in mano e nel grimorio avversari, numero di mulligan | Sì | Dimensioni delle zone, `mulligans` | Fissi |
| Da quando una carta è nella mano avversaria (pescata al turno 3, tenuta dalla mano iniziale) | Sì: un giocatore vede quando l'avversario pesca e cosa gioca | `zone_turn` | v1: ignorata (le identità si scambiano, l'età della carta resta allo slot); v2: inferenza (3.4) |
| Le scelte dell'avversario (non ha giocato una terra al turno 3, ha tenuto 7 carte) | Sì: è inferenza, la fa ogni pilota | Registro | v1: ignorate; v2: inferenza (3.4) |

### 3.2 Cosa non deve usare

- **L'identità delle carte nascoste** (mano avversaria non nota, grimori nelle posizioni non note), **il proprio grimorio compreso**: la ricerca non deve sapere cosa pescherà.
- **L'ordine dei grimori** e **lo stato del generatore della partita** (`GameState::rng`): dal generatore si prevedono le mescolate future. Ogni mondo ha un generatore nuovo.
- **Il seed dell'agente avversario** (i suoi pareggi).
- **Le tracce indirette** nello stato, che il motore non nasconde perché non serve agli euristici:
  - il **`CardId`**: le carte hanno `CardId` nell'ordine della lista (0-59 per il giocatore 0, 60-119 per l'1, `create_game`), quindi l'id di una carta nella mano avversaria dice quale carta è;
  - l'**`incarnation`** di una carta nascosta conta i suoi cambi di zona: una carta del grimorio avversario con l'incarnation più alta è stata in una mano rimescolata con il mulligan;
  - il **registro** della partita, che contiene le carte pescate da entrambi.

### 3.3 Come si campiona (proposta)

**Si scambiano le identità, non si spostano le carte** (*scelta di Claude*). Per ogni giocatore si prendono gli oggetti **nascosti al giocatore che cerca** (per l'avversario: la mano senza il proprio bit in `known_to` e il grimorio nelle posizioni non note; per sé: solo il grimorio nelle posizioni non note), si raccolgono le loro definizioni (`def`) e le si **permuta a caso** tra quegli oggetti. Nessuna carta cambia zona: le dimensioni delle zone, l'età delle carte in mano, le carte note e il cimitero restano quelli veri, e gli invarianti del motore non si toccano (lo conferma la prova della sezione 1.2: 0 violazioni su 1.000). Poi il generatore della partita riparte con un seed nuovo.

Perché così e non spostando i `CardId` tra mano e grimorio (come fa il test dello stato gemello di oggi): lo scambio delle definizioni toglie **insieme** le tre tracce della sezione 3.2. Dopo lo scambio il `CardId` e l'`incarnation` di uno slot non dicono più niente sulla carta che contiene, perché la carta è stata estratta a caso tra tutte quelle nascoste.

**Forma canonica prima di campionare.** Prima della permutazione le definizioni nascoste si ordinano (per esempio per `CardDefId`): così il mondo campionato dipende **solo dall'insieme** delle carte nascoste e dal seed del mondo, non da dove stavano davvero. È questa proprietà che il test 3.5 (a) verifica byte per byte.

**Probabilità uniforme.** La permutazione uniforme delle definizioni nascoste dà la distribuzione corretta *senza inferenza*: ogni assegnazione delle carte nascoste agli slot nascosti ha la stessa probabilità, quindi la mano avversaria ha la distribuzione ipergeometrica delle carte non viste.

**La regola nel codice.** La ricerca non legge mai lo stato vero: riceve lo stato vero solo la funzione di determinizzazione, che lo copia e restituisce un mondo; tutto il resto (generazione delle azioni, rollout, chiavi dell'albero) lavora sul mondo. Le azioni della radice (le proprie) restano valide in ogni mondo, perché la propria mano e le zone pubbliche non cambiano.

### 3.4 Inferenza (dopo)

L'inferenza usa le scelte osservate dell'avversario per pesare i mondi: se l'avversario non ha giocato una terra al turno 3, i mondi in cui ha una terra in mano sono meno probabili. Una forma semplice e corretta: **ripesare** (o scartare) i mondi in base a quanto il **modello dell'avversario** (l'euristico) avrebbe preso le decisioni osservate con quella mano. È informazione legittima (la usa ogni pilota), ma costa (si rigiocano le decisioni passate dell'avversario per ogni mondo) e lega la ricerca a un modello dell'avversario. **Proposta: v1 senza inferenza**, v2 con l'inferenza come variante misurata (domanda 2).

### 3.5 Test (prima del codice)

| Test | Cosa verifica |
| --- | --- |
| **(a) Gemello per la determinizzazione** | Da uno stato vero se ne costruisce un **gemello** che differisce solo in ciò che il giocatore non sa: identità delle carte nascoste permutate, grimori rimescolati nelle posizioni non note, generatore e seed dell'agente avversario diversi, `CardId` delle carte nascoste scambiati. Con lo stesso seed del mondo, `determinize(stato)` e `determinize(gemello)` devono dare **mondi identici** (campo per campo; non con `memcmp`, per i byte di riempimento). Su ogni decisione di qualche centinaio di partite, per entrambi i giocatori. È il test più forte: qualsiasi traccia di informazione nascosta nel mondo lo fa fallire |
| **(b) Gemello per la scelta** | Come il test di oggi dell'agente di Rakdos ("non legge la mano avversaria né l'ordine dei grimori"), ma per il giocatore forte: con lo stesso seed della ricerca, la scelta sullo stato e sul gemello è identica. Copre anche le parti della ricerca fuori dalla determinizzazione |
| **(c) Carte note** | Dopo Thoughtseize la mano avversaria rivelata è identica in ogni mondo; dopo Bauble la cima guardata è al suo posto; dopo un mulligan le proprie carte in fondo sono in fondo nell'ordine scelto; dopo un mescolamento non più |
| **(d) Distribuzione** | Su 100.000 mondi di posizioni preparate, la frequenza di ogni carta nella mano avversaria segue l'ipergeometrica delle carte non viste (chi quadro); le carte nascoste di ogni mondo sono esattamente l'insieme atteso |
| **(e) Il proprio grimorio** | Il proprio grimorio nei mondi non è mai quello vero più spesso del caso: la prossima pescata del mondo coincide con quella vera con la frequenza attesa (copie rimaste / carte nascoste) |
| **(f) Invarianti** | `check_invariants` su ogni mondo |

Un controllo in più, **diagnostico e mai per i dati**: un giocatore "che bara" (stessa ricerca sullo stato vero, senza determinizzazione) deve vincere di più di quello onesto. La differenza misura quanto vale l'informazione nascosta in questo matchup; se fosse zero, la determinizzazione non toglierebbe niente e qualcosa non va.

### 3.6 Gli agenti euristici dentro i rollout

Gli agenti hanno un piccolo stato: il **piano del lancio** in corso (bersaglio, modo, X, carta da esiliare decisi quando si sceglie di lanciare) e il blocco in due. Un rollout che parte in mezzo a un lancio (per esempio alla scelta del bersaglio) con un agente nuovo non ha il piano. Due soluzioni: copiare gli agenti con lo stato (la copia include la `std::map` dei conteggi delle regole: va tolta dai rollout) oppure far partire le ricerche solo da decisioni in cui il piano non serve, e cercare il lancio come **mossa composta** (carta, metodo, bersaglio insieme: sezione 4.3). **Proposta: mosse composte**, e agenti dei rollout senza conteggi (*scelta di Claude*). Nella prova i rollout partono da priorità con la pila vuota, dove il piano è vuoto.

### 3.7 Come è fatta (passo 1, 16/11/2026)

- **Tipi separati.** `play_matchup` costruisce un posto per lato (`Seat`, `tools/giocatori.hpp`) da `MatchupConfig::uw_seat` e `rakdos_seat`: `AgentKind::Heuristic` (l'euristico v1, il predefinito) o `AgentKind::View`. Un `ViewAgent` (`engine/player_view.hpp`) riceve solo `const PlayerView&` e le azioni legali: la vista la costruisce il posto, a ogni decisione, dallo stato vero; il `ViewAgent` non ha un tipo con cui leggere lo stato vero. Con gli euristici i registri sono identici byte per byte (tutti i riferimenti della sezione 1 di `metriche.md`, compreso quello nuovo di UW base_v1, e un test voce per voce contro il ciclo di prima).
- **La vista** si costruisce solo alla decisione del proprio giocatore (una decisione in corso dell'avversario, come le carte già scelte da mettere in fondo, sarebbe informazione nascosta). Contiene lo stato in **forma canonica**: per ogni proprietario, nelle posizioni nascoste al giocatore (mano avversaria senza il suo bit in `known_to`, grimori nelle posizioni senza il suo bit) i CardId in ordine crescente, le definizioni in ordine crescente, la stessa incarnation (la più alta tra le nascoste: nessun riferimento vecchio torna valido), il turno d'arrivo in mano (pubblico) e 0 nel grimorio, chi conosce la posizione; nessun generatore.
- **Un mondo** (`sample_world(seed)`): le definizioni nascoste di ogni proprietario permutate in modo uniforme (PCG32 con il seed del mondo), generatori dei giocatori nuovi dal seed del mondo. Nessuna carta cambia zona.
- **Test** (`tests/test_vista.cpp`): (a) a ogni decisione di 200 partite degli euristici (39.129 decisioni: 20.803 di UW, 18.326 di Rakdos) lo stato e un gemello (CardId e incarnation delle carte nascoste scambiati tra le posizioni, definizioni permutate, generatori diversi) danno la **stessa forma canonica e lo stesso mondo** campo per campo (`operator==` aggiunto a `GameState` e alle sue parti); controllo: cambiare una carta nota, una carta avversaria rivelata o la vita cambia il mondo; (c) la mano rivelata da Thoughtseize, la cima guardata con Mishra's Bauble (fino a un mescolamento), le proprie carte in fondo dopo il mulligan (non quelle dell'avversario) sono fisse in ogni mondo; (d) su 100.000 mondi la mano avversaria segue l'ipergeometrica delle carte non viste (z per carta sotto 5, chi quadro delle copie di Bolt), e le carte nascoste di ogni mondo sono esattamente la lista meno le carte viste; (e) la prossima pescata del mondo è quella vera solo con la frequenza attesa; (f) invarianti su ogni mondo. In più la posizione 9b (sezione 5.1): Bolt nella mano di Rakdos nei mondi in proporzione alle copie rimaste, mai se la mano è nota senza Bolt. (b) arriva con la ricerca (passo 2).
- **Esecuzioni riprendibili** (sezione 8): non fatte in questo passo (la richiesta del passo 1 elencava quattro punti senza di loro); servono prima delle esecuzioni lunghe, quindi **al passo 2**.

---

## 4. Quali decisioni cercare

### 4.1 Per tipo di decisione

| Decisione | Proposta | Perché |
| --- | --- | --- |
| **Mulligan** | **Cercata**, budget alto (4.4) | Una decisione per partita che vale 10 punti in media (`metriche.md`, sezione 18) |
| Carte in fondo | Euristica; poi cercata | 0,6 a partita, 6 opzioni: costosa, effetto probabilmente piccolo |
| **Priorità nella propria fase principale** (terra, magie, abilità, Teferi +1/−3, warp, evocazione) | **Cercata** | Le giocate del turno: il cuore del gioco |
| **Priorità con la pila non vuota** (risposte: Solitude evocata, Consign, Force of Negation, Ephemerate, Thraben Charm; Bolt, Flare) | **Cercata** | Le interazioni che le posizioni test di Daniele chiedono |
| Priorità negli altri momenti con la pila vuota (passare o lanciare un istantaneo nel turno avversario, fetch) | Cercata solo se c'è un'azione diversa da "passa" che non sia una fetch | Molte, quasi sempre "passa" |
| **Bersaglio** | **Cercata come parte della mossa composta** del lancio | Chi esiliare, chi colpire con Bolt |
| **Carta di Thoughtseize** | **Cercata** (Rakdos) | X5 e R-ORD da verificare |
| **Attaccanti e bloccanti** | **Cercata**, come insieme (4.3) | 12 decisioni a partita per UW; la classifica delle minacce, la corsa |
| Modo, X, carte esiliate per il costo | Parte della mossa composta | March con X=1, Solitude con quale carta bianca |
| Vita o terra tappata, ricerca della fetch | Euristica; poi cercata | Contano (3 vita contro tempo) ma l'euristica è ragionevole; D-16 |
| Sorveglianza, esilio dal cimitero (fuga, prove), scarti | Euristica; poi cercata | Contano per Rakdos (cimitero): secondo passo |
| Pagamento del mana, ordine dei trigger, danno diviso, regola delle leggende, rimbalzo, energie di Guide | Euristica | Poche opzioni, quasi sempre equivalenti |

Si parte con un **elenco di decisioni cercate** in configurazione (non nel codice) e lo si allarga una voce alla volta, misurando ogni allargamento (sezione 5.3).

### 4.2 Budget adattivo

- **Niente ricerca se l'azione è una sola** (il motore lo fa già per le priorità) **o se le azioni sono equivalenti** (stesso risultato in tutti i primi mondi: nella prova della sezione 1.6 succede nel 45% degli stati di metà partita, su 40 mondi).
- **Dimezzamento successivo** (sequential halving, Karnin, Koren e Somekh, 2013): R₀ mondi per tutte le K azioni, poi metà delle azioni con R raddoppiato, fino a una. Con i numeri casuali comuni le differenze si stimano sulle coppie.
- **Arresto anticipato:** ci si ferma quando l'intervallo della differenza tra la migliore e la seconda esclude lo zero, oppure quando dice che la differenza è sotto una soglia ε (per esempio 1 punto): azioni equivalenti.
- **A parità, la scelta dell'euristico** (*scelta di Claude*, approvata da Daniele il 16/11/2026): se la ricerca non trova una differenza oltre ε con il budget dato, gioca la regola. Così ogni scelta diversa dall'euristico è **significativa** (non rumore), e l'accordo della sezione 6 si legge: "diverso" vuol dire "la ricerca ha trovato di meglio". Il costo: un'azione migliore di meno di ε non si trova.
- **Per scostarsi dall'euristico serve il 99% di confidenza, non il 95%** (correzione di Daniele, 16/11/2026): l'azione della ricerca sostituisce quella dell'euristico solo se l'intervallo al **99%** della loro differenza appaiata esclude lo zero. Il motivo: l'arresto anticipato guarda la differenza più volte durante la decisione e la ricerca confronta più azioni contro l'euristico; ogni sguardo e ogni azione in più è un'occasione di falso positivo, e al 95% i falsi scostamenti si accumulerebbero su decine di decisioni per partita. Con la deviazione standard della sezione 1.6 servono circa **2,58 / 1,96 ≈ 1,3 volte** i punti di differenza a parità di mondi, cioè circa **1,7 volte i mondi** per la stessa differenza; i tempi della sezione 7 contano già un budget medio che lo comprende solo in parte, e vanno ricontrollati con la curva di convergenza. L'arresto per equivalenza (ε) resta al 95% (*scelta di Claude*, **approvata da Daniele il 16/11/2026**): fermarsi presto lì al massimo fa giocare la regola. Il 99% vale solo per scostarsi dalla scelta dell'euristico.
- **Budget per importanza:** più rollout dove le prime stime sono lontane tra loro e la partita è in bilico (vittorie tra 20% e 80%), meno dove è decisa.

### 4.3 Potature

- **Mosse composte:** il lancio di una magia con metodo (normale, evocazione, warp), bersaglio, modo, X e carte esiliate per il costo è **una** mossa; l'attacco è un **insieme** di attaccanti (le combinazioni sensate: tutti, nessuno, quelli che l'euristico manda, quelli che sopravvivono ai bloccanti possibili). Il motore chiede le parti una alla volta; la ricerca le genera insieme e le applica in sequenza.
- **Equivalenze:** il motore unisce già le copie identiche; si uniscono anche i pagamenti di mana equivalenti (si paga come l'euristico).
- **Le prime k dell'euristico:** se le azioni sono troppe (attacchi con molte creature), si cercano le k più promettenti secondo un ordine dell'euristico, più sempre la sua scelta.
- **Rollout troncati** (livello C): dopo N turni il valore della rete al posto del resto della partita.

### 4.4 Il mulligan come decisione cercata

Dal passo 4 delle metriche (`docs/metriche.md`, sezione 18): tenere la prima mano da 7 vale +10,5 punti per UW e +8,6 per Rakdos rispetto al mulligan giocato dagli stessi agenti; D-2 perde punti su certi tipi di mano; la politica della regressione **M-LOG** vince **+4,09 [+3,84; +4,33]** contro D-2 sui seed 100.001-200.000. **M-LOG è il riferimento da battere** (decisione di Daniele, 16/11/2026), non una regola da adottare.

- **La ricerca:** "tieni" e "mulligan" sono due azioni; R mondi comuni (la mano avversaria e i grimori campionati), per ognuno la partita con il resto giocato dall'agente (euristico, o il giocatore forte quando il suo costo lo permette). Budget alto: 1.000-2.000 mondi per azione, perché la decisione è una e vale molto. Dopo un mulligan, la decisione a 6 carte e le carte in fondo seguono lo stesso schema.
- **Il confronto:** sugli stessi seed 100.001-200.000 di M-LOG, UW con il mulligan cercato contro D-2 e contro M-LOG, il resto della partita uguale (euristico): la ricerca deve fare almeno +4,09 contro D-2. Se non ci arriva, il budget o i rollout non bastano per una decisione che si guarda a 60 decisioni di distanza.
- **Attenzione:** il valore del mulligan dipende da come si gioca dopo. Con il giocatore forte anche nel resto della partita, il confronto si rifà (le mani da tenere possono cambiare).

---

## 5. Valutazione

### 5.1 Posizioni test

Ognuna è una **posizione preparata** (`tools/posizioni.hpp`, `test_position(db, uw, rakdos, id)`) e un **insieme di informazione**: la mano avversaria non è data, si campiona; la giocata deve esserlo in tutti i mondi plausibili, non solo in quello preparato. **Sono tutte linee guida di Daniele** (pilota di UW Blink; le 1-5 sono quelle della specifica, scritte da lui), **da misurare con l'accordo e non da usare come verdetti**: fonte "Daniele", stato "da far decidere ai dati" (registro, righe P6-P10 e D-2c). Si misura **quante volte su 100 ricerche** (seed diversi) il giocatore forte sceglie la giocata indicata, a ogni budget della curva (5.2), accanto alla scelta dell'euristico. Il risultato è un **accordo da guardare nei replay, non un voto**: quando la ricerca sceglie diverso è un caso da leggere, non un errore.

Per ognuna i test (`tests/test_posizioni.cpp`, verdi senza la ricerca) verificano: (i) la giocata indicata è tra le azioni legali del motore, e la linea si gioca fino in fondo; (ii) cosa fa oggi l'euristico di UW (base_v2), con la regola, alla decisione e nella linea del turno (UW e Rakdos euristici); (iii) una contro-posizione in cui la condizione non vale. **Nessun buco del motore**: tutte le giocate indicate sono legali.

| # | Posizione (decisione di UW) | Giocata indicata | Contro-posizione | Euristico oggi (regola) |
| --- | --- | --- | --- | --- |
| 1 | Moonshadow appena lanciato e risolto (fase principale di Rakdos); UW con 2 Plains, March e Solitude | March con X=1 su Moonshadow, senza esiliare carte: Solitude resta | **1c**: un solo mana, X=1 costa anche Solitude | Passa alla decisione ("priorita': niente da fare ora"); March X=1 nella sottofase finale di Rakdos ("March: sottofase finale avversaria"). In 1c evoca Solitude nella sottofase finale |
| 2 | Secondo turno di UW, Sanctifier, Guide e Ocelot in mano; Rakdos con una Swamp e una Bauble nel cimitero | Sanctifier subito | **2c**: cimitero già con il delirium; Sanctifier lascia terra e artefatto (2 tipi) | Sanctifier in entrambe ("Sanctifier en-Vec: l'avversario ha mostrato il nero o il rosso") |
| 3 | Rakdos con Nethergoyf, tre tipi nel cimitero (prima priorità di UW nel turno di Rakdos); Thraben Charm | Esiliare il cimitero di Rakdos | **3c**: un tipo solo nel cimitero | Passa; Charm quando Nethergoyf attacca ("Thraben Charm: Nethergoyf in combattimento"). In 3c non lo lancia |
| 4 | Teferi, 3 terre e Guide of Souls; Rakdos con Blood Crypt (rosso aperto) | Teferi prima di attaccare | **4c**: Rakdos con una Swamp, niente rosso | Teferi prima del combattimento in entrambe ("Teferi: prima del combattimento") |
| 5 | Phoenix conferita su Nethergoyf; Solitude, Witch Enchanter (carta da esiliare), 4 terre | Solitude esilia Nethergoyf | **5c**: Phoenix su Stitcher's Supplier, Nethergoyf senza Phoenix | Passa; Solitude evocata quando Nethergoyf attacca ("Solitude: la minaccia che attacca"); in 5c esilia Nethergoyf (classifica D-6), non la creatura con la Phoenix |
| 6 | Terzo turno di UW: Quantum Riddler, Consign e Teferi; Meticulous Archive, Hallowed Fountain, Plains | Riddler con il warp tenendo {U}, Consign sul trigger di esilio: Riddler resta (bloccante 4/6 volante al turno di Rakdos) e ha pescato una carta | **6c**: Rakdos con Flare of Malice in mano (**nota a UW**) e quattro mana: Consign non la contrasta (colorata) e Riddler viene sacrificato | W1 in entrambe ("Quantum Riddler: warp con Consign per l'esilio (variante W1)"); in 6c Rakdos lancia Flare nella sottofase finale e Riddler va al cimitero |
| 7 | 7a: Phelia ha esiliato Nethergoyf; alla sottofase finale il ritorno è in pila, Consign e {U} aperto | Consign sul ritorno: Nethergoyf resta in esilio | **7c**: l'esiliato è Stitcher's Supplier, che non vale una Consign | Consign in entrambe ("Consign to Memory: il ritorno di Phelia, esilio per sempre", D-15): **in 7c diverge** dalla linea guida |
| 8 | 8a: FOMO con il delirium prima di attaccare; UW con Guide e Ocelot (Charm fa 4 danni) e Thraben Charm. 8b: trigger di FOMO in pila | 8a: Charm sui danni su FOMO, subito. 8b: Charm esilia i cimiteri (uccidere FOMO non ferma il trigger) | **8c**: Rakdos senza delirium, Charm non serve | 8a: passa, poi esilia il cimitero in risposta al trigger (la linea di 8b, non quella di 8a). 8b: "Thraben Charm: esilia il cimitero in risposta al trigger di FOMO". 8c: non lo lancia |
| 9 | 9a: Nethergoyf e Dark Matter Manipulator, nessun rosso aperto; Solitude, Ephemerate, Ocelot (carta bianca), {W} | Solitude evocata, Ephemerate su Solitude con il sacrificio in pila: torna, esilia la seconda creatura, resta | **9b**: Rakdos con Blood Crypt e Bolt in mano (nascosto): Bolt in risposta a Ephemerate fa fallire la linea; la scelta dipende dalla probabilità di Bolt | Passa; quando le creature attaccano gioca la linea ("Solitude: la minaccia che attacca", "Ephemerate: Solitude evocata, in risposta al sacrificio"), anche in 9b (non guarda Bolt; l'euristico di Rakdos non risponde a Ephemerate) |
| 10 | Terzo turno di UW, Recruiter lanciato con tre terre (la decisione è la ricerca), Ephemerate in mano. 10a: Nethergoyf in campo | 10a: Solitude, e la si evoca subito. 10b: nessuna minaccia, cimitero a 4 tipi: Sanctifier | **10c**: Solitude già in mano, Recruiter cerca altro | Sanctifier in tutte e tre ("Recruiter: Sanctifier en-Vec"); con la variante R-REC: Solitude, Sanctifier, Sanctifier |

**Note per la misura** (da decidere con Daniele):
- **Il momento.** In 1, 3, 5, 8a e 9a la posizione è la prima priorità di UW nel turno di Rakdos: la giocata indicata lì è legale, e l'euristico la fa più tardi nello stesso turno (quando la minaccia attacca, o nella sottofase finale). La linea guida dice *cosa*, non sempre *quando*. Proposta (*scelta di Claude*): l'accordo si conta sulla **linea del turno** (la giocata fatta prima che la condizione cambi), e il momento si legge nei replay.
- **Informazione nascosta nelle contro-posizioni.** In 6c Flare è resa **nota a UW** (come se fosse stata rivelata): con Flare nascosta, 6 e 6c sarebbero lo stesso insieme di informazione per UW (differirebbero solo per le terre di Rakdos). In 9b Bolt resta **nascosto**: 9a e 9b differiscono per il rosso aperto (pubblico) e per la mano (campionata), ed è proprio la probabilità di Bolt a decidere. Un test della determinizzazione lo verifica (sezione 3.7).
- La posizione 8a è anche la più utile per il livello B: l'euristico gioca la linea di 8b (aspettare il trigger), che al livello A è un'alternativa ragionevole; la 9 chiede due decisioni di fila (evocare, poi Ephemerate con il trigger in pila) e al livello A riesce solo se l'euristico, nel rollout, gioca la seconda (lo fa: D-5).

**La variante R-REC** (`docs/agente_uw.md`, sezione 30), la linea della posizione 10 in codice, spenta: **−0,72 [−0,80; −0,65]** punti di win rate di UW contro base_v2 (−0,96 in play, −0,48 in draw), **−8,65** nelle 8.339 partite in cui cambia la ricerca (sempre Solitude al posto di Sanctifier). Non adottata: decide Daniele.

### 5.2 Curva di convergenza

Un insieme fisso di **1.000 decisioni vere** (prese dalle partite degli euristici, divise per tipo) e le 10 posizioni test. Per budget R = 16, 32, 64, ..., 2.048 mondi per azione: la quota di decisioni in cui la scelta coincide con quella a budget **8 volte il massimo** (il riferimento), e la differenza di valore stimata tra la scelta e il riferimento. La curva dice dove il rendimento cala: il budget si fissa lì, non a priori. Si ripete per livello (A, poi B).

### 5.3 Forti contro euristici, appaiate

- **Stessi seed** (1-10.000, poi 1-100.000 se il costo lo permette, UW in play con il seed dispari) e stesse liste degli euristici v1: per ogni seed, la partita con UW forte contro Rakdos euristico e quella già giocata dagli euristici (`partite.jsonl`, MD5 `0ad6744d...`). Differenza appaiata della win rate di UW con l'intervallo al 95%, play e draw separati. Lo stesso per Rakdos forte contro UW euristico.
- **Il seed della ricerca** è una funzione del seed della partita e dell'indice della decisione: la partita del giocatore forte è riproducibile ed è la stessa con 1 o 20 processi.
- **Un generatore per giocatore** (decisione 10 di `metriche.md`: "da riprendere prima della fase 4"): oggi un solo generatore mescola i grimori di entrambi, quindi appena il giocatore forte sceglie diversamente (una fetch in più) cambiano anche le pescate dell'avversario, e l'appaiamento si perde presto. Con un generatore per giocatore le pescate di chi non ha cambiato niente restano le stesse. **Fatto come passo 0 il 16/11/2026** (domanda 3; `metriche.md`, sezione 19): ogni giocatore ha il suo PCG32 con i semi da SplitMix64 del seed della partita; tutti i riferimenti MD5 ricalcolati una volta sola (sezione 1 di `metriche.md`), le win rate statisticamente uguali a quelle di prima. **Il guadagno sull'appaiamento è piccolo:** intervalli più stretti del 5-9% per le varianti di Rakdos, nessuno per quelle di UW (W1, base_v2), perché dopo il primo scostamento entrambi i giocatori giocano diversamente. Resta il valore di principio: le scelte del giocatore forte non spostano più le pescate dell'avversario.

### 5.4 Forti contro forti

UW forte contro Rakdos forte sugli stessi seed: è la **stima del matchup** che la specifica chiede ("i dati diventano davvero affidabili solo dalla fase della ricerca ad albero in poi"). Accanto al 37,26% degli euristici v1. Il confronto tra livelli è anche il controllo di qualità della specifica: se la win rate cambia molto da euristici a forti, i dati degli euristici erano distorti.

### 5.5 Robustezza

Il giocatore forte usa gli euristici v1 nei rollout e come modello dell'avversario: va misurato **anche contro avversari diversi** da quello che ha in testa, come già fatto per X1-C e X6. UW forte contro Rakdos **base_r1, base_r2, base_r3**; Rakdos forte contro UW **base_v1 e base_v2**; poi contro il forte dell'altro lato. Un guadagno vero ha lo stesso segno contro tutte le versioni; un guadagno che c'è solo contro l'avversario dei rollout è adattamento a quell'avversario, e la riga lo dice.

### 5.6 Lettura dei replay

Come per gli euristici: `replay_leggibile` sulle partite dei forti, con in più, a ogni decisione cercata, le azioni considerate e le loro stime (vittorie su mondi). Daniele legge a campione le partite in cui il forte si discosta dall'euristico: è il controllo che nessuna statistica fa.

---

## 6. Le regole del registro: conferma o smentita

Il registro (`docs/ipotesi_daniele.md`, sezione 4) fissa già **cosa** si misura: accordo per decisione, differenza di win rate "albero libero − regola forzata" appaiata, la differenza dove la regola decide, contro più versioni di avversario. Qui **come** lo si ottiene dalla ricerca.

1. **L'euristico come ombra.** A ogni decisione cercata il giocatore forte chiede anche all'agente euristico cosa farebbe e con quale regola (come le sonde del passo 4 delle metriche: l'agente riceve lo stato in sola lettura). Il registro della partita forte tiene, per ogni decisione cercata: la regola, la scelta dell'euristico, la scelta della ricerca, le stime di entrambe.
2. **Accordo per regola** = quota di decisioni con la stessa scelta, dai registri delle partite forti: **gratis**.
3. **Rimpianto stimato per regola** = media, sulle decisioni della regola, della differenza stimata dalla ricerca tra la propria scelta e quella della regola (con i mondi comuni è appaiata). È una stima **dentro la decisione** (vale se poi si gioca come l'euristico), ma è gratis e ordina le regole: quelle con rimpianto alto e accordo basso vanno per prime al punto 4.
4. **Differenza di win rate per regola** (la misura causale del registro): due esecuzioni sugli stessi seed, giocatore forte libero e giocatore forte con **quella regola forzata** nelle sue posizioni (la decisione la prende l'euristico). Costa un'esecuzione completa per regola e per avversario: con circa 30 gruppi di regole e 5 versioni di avversario sarebbero 150 esecuzioni, troppe. **Proposta:** tutte le regole con il punto 3, poi il punto 4 solo per le 5-10 regole con il rimpianto più alto, prima contro l'avversario predefinito, poi contro le altre versioni (*scelta di Claude*).
5. **Una cautela:** al livello A i rollout giocano le regole stesse. Una regola giudicata "giusta" dalla ricerca piatta lo è *se poi si gioca come l'euristico*; il giudizio vale di più al livello B e C. Il registro dirà da quale livello viene ogni conferma.

Le soglie dell'accordo restano quelle del registro: si fissano dopo aver visto la distribuzione su tutte le regole.

---

## 7. Agenti forti nel record, nelle metriche e nell'ablazione

**Cosa serve nel codice** (nessuno di questi passi è fatto):
- **Un'interfaccia degli agenti in `play_matchup`**: oggi `play_matchup` costruisce `UwAgent` e `RakdosAgent`; serve un agente per lato scelto dalla configurazione. Con gli euristici i registri devono restare identici byte per byte (riferimenti della sezione 1 di `metriche.md`).
- **Il record compatto non cambia:** è una funzione pura del registro (`build_compact_record`), quindi vale per qualsiasi agente. Si aggiunge solo il nome dell'agente di ogni lato nella riga e nella cartella (`data/metriche/forti_v1/`).
- **Le metriche Python non cambiano:** `converti.py`, `metriche_carte.py`, `mulligan.py` leggono il record; si rigenerano in un documento generato per gli agenti forti (`risultati_forti_v1.md`).
- **L'ablazione** è la stessa opzione `--sostituisci`, con gli agenti forti: costa come tante esecuzioni complete quante sono le carte (sotto).
- **Il passo 3 delle metriche** (campo `source`, minacce grandi) dopo gli agenti forti (decisione di Daniele del 16/11/2026).

### Tempi stimati

Dai valori della sezione 1.7. "Rollout per partita" è per il lato forte; con entrambi i lati forti si sommano (Rakdos ha un numero simile di decisioni cercate: 22,5 priorità di fase principale, 11,5 di risposta).

| Configurazione | Rollout per partita | Partite al secondo (15.000 / 8.800) | 10.000 partite | 100.000 partite |
| --- | --- | --- | --- | --- |
| A minimo: UW forte solo nella fase principale e nel mulligan (circa 20 decisioni) | circa 6.600 | 2,3 / 1,3 | **1,2 h** / 2,1 h | **12 h** / 21 h |
| **A proposto: UW forte** (42 decisioni, budget adattivo, mulligan) | circa 10.800 | 1,39 / 0,81 | **2,0 h** / 3,4 h | **20 h** / 34 h |
| A proposto: **UW e Rakdos forti** | circa 21.600 | 0,69 / 0,41 | **4,0 h** / 6,8 h | **40 h** / 68 h |
| A pesante: UW forte, budget doppio (circa 400 rollout per decisione) e 52 decisioni | circa 23.600 | 0,64 / 0,37 | 4,4 h / 7,5 h | 44 h / 75 h |
| **B**: UW forte, 2.000 iterazioni per decisione | circa 87.000 | 0,17 / 0,10 | **16 h** / 27 h | **6,7 giorni** / 11 giorni |
| C: B con la rete al posto dei rollout (stima: un'iterazione 5-10 volte meno cara) | — | — | 2-3 h | 0,7-1,3 giorni |

- **Il record non costa niente in più:** si costruisce dal registro in memoria (sezione 15 di `metriche.md`: 1 s per 1.000 partite, trascurabile rispetto alla ricerca). Le metriche Python sono le stesse (1-2 minuti).
- **Proposta:** il livello A con UW forte su **10.000 seed** (2 ore) per la valutazione e per la prima tabella delle metriche; **100.000 seed** (20 ore, due o tre notti) solo dopo la curva di convergenza e se l'intervallo delle metriche a 10.000 non basta (gli IC delle metriche per carta a 10.000 partite sono circa 3 volte più larghi che a 100.000).
- **Ablazione con i forti:** costa come un'esecuzione completa per carta. Le 28 carte a 100.000 seed sarebbero 28 × 20 h: impossibile. **Proposta:** le carte in discussione (Force of Negation, Consign to Memory, Witch Enchanter, che per gli euristici valgono meno di una terra base; Sanctifier, la più forte) su **20.000 seed** ciascuna, più la base sugli stessi seed: 4 h per esecuzione, circa 20 ore in tutto. L'intervallo della differenza appaiata a 20.000 seed è circa ±0,5 punti (±0,2 a 100.000 con gli euristici).
- **I tempi sono senza Game Shift** (sezione 1.5) e da ricontrollare con la prima esecuzione vera: ogni esecuzione scrive il suo throughput.

---

## 8. GPU e parallelismo

- **Livelli A e B: solo CPU.** Un rollout è codice di regole: rami, tabelle piccole, nessun calcolo vettoriale. Su una GPU ogni partita prenderebbe un ramo diverso: inutile.
- **Livello C: la GPU per addestrare** (PyTorch, RTX 4060 da 8 GB: basta per reti da qualche milione di parametri e qualche milione di posizioni). **Per usare la rete nella ricerca, prima la CPU:** una rete piccola valutata in C++ costa circa 10 µs su un core, e i 12-20 processi di ricerca lavorano in modo indipendente. La GPU per l'inferenza conviene solo con reti grandi e con le richieste di molti processi raccolte in lotti da un server: è latenza in più (ordine di 0,1 ms per lotto) e un processo da scrivere. Si decide quando la rete esiste, misurando.
- **Parallelismo: per partite, non dentro la ricerca.** Come per `metriche.exe` e l'ablazione: N processi, ognuno con un intervallo di seed, file concatenati nell'ordine dei seed. Nessun thread nel motore, nessuna memoria condivisa; il risultato non dipende dal numero di processi, perché il seed della ricerca dipende solo dal seed della partita e dalla decisione. Il parallelismo dentro una decisione (più thread sulla stessa ricerca) serve solo per analizzare una posizione a mano, e si può fare dopo.
- **Memoria:** 16 MB per processo nella prova; un albero di ISMCTS da 10.000 iterazioni è sotto 1 MB, una rete piccola qualche MB. 20 processi restano sotto 2 GB su 16.
- **Processi:** 20 di notte, 16 di giorno (decisione di Daniele, 16/11/2026).

### Esecuzioni lunghe riprendibili (correzione di Daniele, 16/11/2026)

Un'esecuzione da 20 o 40 ore su un portatile si interrompe (sospensione, aggiornamenti, corrente: ai passi 2 e 2b delle metriche la macchina si è già fermata durante un'esecuzione, per fortuna corta). Per questo:

- **Un file per blocco di seed.** I seed si dividono in blocchi fissi (per esempio 500 seed); ogni blocco scrive i suoi file (`partite_000001-000500.jsonl`, il record, il registro della ricerca) in un nome temporaneo e lo **rinomina solo alla fine**: un blocco o c'è tutto o non c'è.
- **Un manifest** (`manifest.json` nella cartella dell'esecuzione) con tutto ciò che definisce l'esecuzione: agenti e opzioni, livello e budget della ricerca, liste, intervallo di seed, dimensione dei blocchi, commit del codice, e per ogni blocco finito l'MD5 dei suoi file e il tempo. Il manifest si aggiorna dopo ogni blocco (anche lui con scrittura e rinomina).
- **Ripresa:** rilanciando lo stesso comando, lo strumento legge il manifest, controlla che la configurazione sia la stessa (altrimenti si ferma con un errore: non si mescolano blocchi di due configurazioni) e rifà **solo i blocchi mancanti**. Il risultato non dipende da dove si è interrotto, perché ogni partita dipende solo dal suo seed (seed della ricerca compreso).
- **Fine:** i blocchi si concatenano nell'ordine dei seed, come per `metriche.exe` in 8 processi; l'MD5 del file concatenato va nel resoconto. Un test verifica che un'esecuzione interrotta e ripresa dia gli stessi file di una senza interruzioni.

---

## 9. Rischi, ordine dei passi, domande per Daniele

### Rischi

| Rischio | Effetto | Come si mitiga |
| --- | --- | --- |
| **Distorsione dei rollout** | Il forte eredita gli errori dell'euristico nella valutazione (Force of Negation, Consign) | Livello B (l'albero copre il turno), poi C; le posizioni test lo mostrano |
| **Adattamento all'avversario dei rollout** | La win rate contro l'euristico gonfia il guadagno | Robustezza contro cinque versioni (5.5), forti contro forti (5.4) |
| **Informazione nascosta che filtra** | Un giocatore "forte" perché bara | Test (a)-(f) della sezione 3.5, prima del codice; il giocatore che bara come canarino |
| **Calcolo** | Le stime della sezione 7 sono di ordini di grandezza; il portatile riduce la potenza dopo 160 s | Budget dalla curva di convergenza; esecuzioni lunghe di notte; misure dei tempi a ogni passo |
| **Approssimazioni del motore** (March offre solo gli X pari a un valore di mana bersaglio; Consign offre copie solo per altri bersagli; le carte da esiliare si scelgono dopo il bersaglio: `agente_uw.md`, sezione 24) | Il forte non può fare mosse che il motore non offre | Elenco noto; se una posizione test le richiede, si corregge il motore prima (con il suo test) |
| **Appaiamento debole con un generatore solo** | Intervalli più larghi nei confronti appaiati | Generatore per giocatore come passo 0 (5.3): **fatto** |
| **Esecuzioni lunghe interrotte** | Ore di calcolo perse | Blocchi di seed e manifest, ripresa dei soli blocchi mancanti (sezione 8) |
| **Determinismo** | Una partita del forte non riproducibile non si può rileggere né verificare | Seed della ricerca dal seed della partita; test "stesso seed, stessa partita" anche per il forte |
| **Lista avversaria nota** | Il forte sa le 60 carte esatte, un pilota no | Ipotesi dichiarata (domanda 1) |

### Ordine dei passi (proposta)

0. **Generatore per giocatore** (decisione 10 di `metriche.md`), con il ricalcolo unico dei riferimenti MD5. **Fatto il 16/11/2026** (sezione 5.3; riferimenti nella sezione 1 di `metriche.md`).
1. **Interfaccia degli agenti** in `play_matchup` (registri degli euristici identici), **determinizzazione** con i test (a), (c)-(f), prima del codice, le posizioni test come stati preparati. **Fatto il 16/11/2026** (sezioni 3.7 e 5.1). Le **esecuzioni riprendibili** (blocchi di seed e manifest, sezione 8) passano al passo 2.
2. **Livello A minimo:** decisioni della fase principale, risposte, bersagli (mosse composte), budget fisso, mondi comuni; l'accordo sulle 10 posizioni test; la curva di convergenza; il test (b) dello stato gemello per la scelta; le esecuzioni riprendibili.
3. **Budget adattivo e mulligan cercato**, confronto con M-LOG e D-2 sui seed 100.001-200.000.
4. **Valutazione:** forti contro euristici appaiati su 10.000 seed, robustezza, ombra dell'euristico e accordo per regola.
5. **Record e metriche con gli agenti forti** (10.000 seed, poi 100.000 se il costo lo permette), poi il passo 3 delle metriche.
6. **Livello B** quando A si satura (2); **livello C** quando B ha prodotto abbastanza posizioni.

Ogni passo con il suo resoconto, come nella fase 3.

### Domande per Daniele e risposte (16/11/2026)

| # | Domanda | Risposta di Daniele |
| --- | --- | --- |
| 1 | **Lista avversaria:** la determinizzazione può assumere che ogni giocatore conosca le 60 carte esatte dell'avversario (come oggi gli euristici)? L'alternativa è una lista di archetipo con le copie incerte, più complicata | **Sì**: ogni giocatore conosce le 60 carte esatte dell'avversario |
| 2 | **Inferenza:** nella prima versione i mondi sono uniformi (nessuna lettura delle scelte avversarie). Va bene, con l'inferenza come variante misurata dopo? | **Sì**: v1 senza inferenza |
| 3 | **Generatore per giocatore** come passo 0, con il ricalcolo di tutti i riferimenti MD5? | **Sì**, passo 0: fatto (sezione 5.3) |
| 4 | **Da che lato si parte:** proposta UW forte, poi Rakdos | **UW forte per primo** |
| 5 | **Tempo macchina:** esecuzioni di notte da 8-10 ore? Con quanti processi? | **Sì: 20 processi di notte, 16 di giorno** |
| 6 | **Posizioni test:** confermi le giocate corrette delle posizioni 6-10? Per Recruiter al turno 3 senza minacce in campo (10b), cosa cerchi: Sanctifier, Solitude da tenere, o altro? | **Sì, con le posizioni 6-10 riscritte** (16/11/2026): linee guida da misurare con l'accordo, non verdetti; 10b: Sanctifier en-Vec; ogni posizione con una contro-posizione (sezione 5.1). Variante R-REC per la 10 |
| 7 | **"A parità, la scelta dell'euristico"** (4.2): va bene? | **Sì**, con il **99% di confidenza** per scostarsi (4.2) |

**Correzioni di Daniele (16/11/2026):** la posizione 9 con **due** creature avversarie (5.1); il 99% di confidenza, non il 95%, per scostarsi dall'euristico (4.2); le esecuzioni lunghe riprendibili, un file per blocco di seed e un manifest (8).

---

## Fonti

- P. I. Cowling, E. J. Powley, D. Whitehouse, *Information Set Monte Carlo Tree Search*, IEEE Transactions on Computational Intelligence and AI in Games, 2012.
- P. I. Cowling, C. D. Ward, E. J. Powley, *Ensemble Determinization in Monte Carlo Tree Search for the Imperfect Information Card Game Magic: The Gathering*, IEEE TCIAIG, 2012.
- C. D. Ward, P. I. Cowling, *Monte Carlo Search Applied to Card Selection in Magic: The Gathering*, IEEE CIG, 2009.
- I. Frank, D. Basin, *Search in Games with Incomplete Information: a Case Study Using Bridge Card Play*, Artificial Intelligence, 1998 (strategy fusion e non-località).
- J. Long, N. Sturtevant, M. Buro, T. Furtak, *Understanding the Success of Perfect Information Monte Carlo Sampling in Game Tree Search*, AAAI, 2010.
- Z. Karnin, T. Koren, O. Somekh, *Almost Optimal Exploration in Multi-Armed Bandits*, ICML, 2013 (dimezzamento successivo).
- D. Silver et al., *A general reinforcement learning algorithm that masters chess, shogi, and Go through self-play*, Science, 2018 (AlphaZero).
