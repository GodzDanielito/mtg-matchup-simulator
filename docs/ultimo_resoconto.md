# Resoconto: passo 1 della ricerca, interfaccia degli agenti, vista e determinizzazione, posizioni test, R-REC (16/11/2026)

## 1. Cosa è stato fatto

- **Approvazioni registrate** in `docs/ricerca.md`: passo 0; il 99% solo per scostarsi dall'euristico, l'arresto per equivalenza al 95% (sezione 4.2); la tua risposta alla domanda 6 (le posizioni 6-10, sezione 5.1).
- **`matchup.exe --uw-v1`** (UW base_v1, `UwAgentOptions::base_v1()`) e il suo MD5 nella sezione 1 di `docs/metriche.md`: anche base_v1 è coperto byte per byte.
- **Passo 1, niente ricerca implementata:**
  1. un agente per lato scelto dalla configurazione; il giocatore forte riceverà solo la **vista** del giocatore, in un tipo separato;
  2. la **determinizzazione** con i test (a), (c), (d), (e), (f);
  3. le **dieci posizioni test** come stati preparati, con legalità, euristico di oggi e contro-posizione;
  4. la variante **R-REC** di Recruiter, spenta, misurata su 100.000 partite. **Non adottata.**
- **Nessun agente cambia di default.** In più (non chiesto, previsto da `ricerca.md` al passo 1): il test dello stato gemello per l'agente di UW ("non legge la mano avversaria né l'ordine dei grimori"), verde.
- **Rimandato al passo 2:** le esecuzioni riprendibili (blocchi di seed e manifest). `ricerca.md` le metteva nel passo 1, ma la tua richiesta elencava quattro punti senza di loro; servono prima delle esecuzioni lunghe.

## 2. Registri: identici byte per byte

Tutti i riferimenti della sezione 1 di `docs/metriche.md`, rigiocati con Release **prima** dell'interfaccia (con `--uw-v1` appena aggiunta), **dopo** l'interfaccia e **dopo** R-REC (spenta): 23 MD5, tutti uguali nei tre casi.

| Registro | MD5 |
| --- | --- |
| Matchup 10.000 (`partite.jsonl` / `replay.jsonl`) | `96384961...` / `01325de3...` |
| Matchup 100.000 (`partite.jsonl` / `replay.jsonl`) | `6a580e50...` / `1cd0fff7...` |
| Rakdos base_r1 / base_r2, 10.000 | `bb968dee...`, `190f3547...` / `ffc8f620...`, `18ee805e...` |
| **UW base_v1, 10.000 (nuovo)** | **`cd44fb62c24bd419d07d01dd55b988b7`**, **`bc436d599131e696410d827125e0c8d2`** |
| Goldfish (C, B_r1, C_r2; 10.000 e 100.000) | 8 MD5, uguali alla sezione 1 |
| Record compatto 10.000 / 100.000 | `710432cc...` / `81831db0...` |

In più un test confronta voce per voce, su 30 seed, `play_matchup` con il ciclo di prima (base_v2 contro base_r3 e base_v1 contro base_r1): registro, regole, conteggi, esito identici.

## 3. Interfaccia degli agenti e vista

| Pezzo | Dove | Cosa fa |
| --- | --- | --- |
| `Seat` | `tools/giocatori.hpp` | Un posto per lato in `play_matchup`, da `MatchupConfig::uw_seat` / `rakdos_seat` |
| `AgentKind::Heuristic` | idem | L'euristico v1 con le opzioni di `MatchupConfig` (il predefinito) |
| `AgentKind::View` | idem | Un `ViewAgent` da una fabbrica (il giocatore forte dal passo 2) |
| `ViewAgent` | `engine/player_view.hpp` | `choose(const PlayerView&, azioni legali)`: **non riceve mai lo stato vero** |
| `PlayerView` | idem | Costruita dal motore alla decisione del giocatore; dentro lo stato in **forma canonica**; espone solo `sample_world(seed)`, le carte nascoste, chi guarda |

**Forma canonica** (per ogni proprietario, nelle posizioni nascoste al giocatore: mano avversaria non nota, grimori nelle posizioni non note, il proprio compreso): CardId in ordine crescente, definizioni in ordine crescente, la stessa incarnation (la più alta tra le nascoste), turno d'arrivo in mano tenuto (è pubblico) e 0 nel grimorio, nessun generatore. **Un mondo:** le definizioni nascoste permutate in modo uniforme, generatori nuovi dal seed del mondo; nessuna carta cambia zona.

## 4. Test della determinizzazione (`tests/test_vista.cpp`)

| Test | Cosa verifica | Esito |
| --- | --- | --- |
| **(a) Gemello** | A ogni decisione di 200 partite degli euristici: stato vero e gemello (CardId e incarnation delle carte nascoste scambiati tra le posizioni, definizioni permutate, generatori diversi) danno la **stessa forma canonica e lo stesso mondo**, campo per campo | **39.129 decisioni** (UW 20.803, Rakdos 18.326), tutte uguali |
| (a) Controllo | Cambiare una carta della propria mano, una carta avversaria rivelata o la vita cambia il mondo | Verde |
| (c) Carte note | Mano rivelata da Thoughtseize, cima guardata con Bauble (fino a un mescolamento), proprie carte in fondo dopo il mulligan (quelle dell'avversario no): fisse in ogni mondo | Verde |
| (d) Distribuzione | 100.000 mondi: copie di ogni carta nella mano di Rakdos contro l'ipergeometrica (z sotto 5 per ogni carta), copie di Bolt in mano (chi quadro); carte nascoste di ogni mondo = lista meno carte viste | Verde |
| (e) Proprio grimorio | 40.000 mondi: la prossima pescata è quella vera solo con la frequenza attesa | Verde |
| (f) Invarianti | Su ogni mondo dei test (a) e (d) | Verde |
| Posizione 9b | Bolt nella mano di Rakdos nei mondi in proporzione alle copie rimaste; **0** con la mano nota senza Bolt | Verde |

Test prima del codice: con gli stub 13 test case rossi su 14 (il quattordicesimo, "registri come prima", è una guardia).

## 5. Le dieci posizioni test

Tutte **linee guida tue, da misurare con l'accordo** (fonte "Daniele", stato "da far decidere ai dati"). **Nessun buco del motore: tutte le giocate indicate sono legali** e le linee si giocano fino in fondo.

| # | Giocata indicata | Contro-posizione | Euristico oggi (regola) |
| --- | --- | --- | --- |
| 1 | March X=1 su Moonshadow, Solitude resta | 1c: un solo mana, X=1 costa anche Solitude | Passa; March X=1 nella sottofase finale di Rakdos. In 1c evoca Solitude |
| 2 | Sanctifier subito | 2c: cimitero già con il delirium | Sanctifier in entrambe |
| 3 | Thraben Charm esilia il cimitero (a un tipo dal delirium) | 3c: un tipo solo | Passa; Charm quando Nethergoyf attacca. In 3c no |
| 4 | Teferi prima di attaccare (rosso aperto) | 4c: niente rosso | Teferi prima del combattimento in entrambe |
| 5 | Solitude su Nethergoyf con la Phoenix | 5c: Phoenix su Supplier | Passa; Solitude quando attacca, su Nethergoyf in entrambe |
| 6 | Riddler warp, {U} aperto, Consign sull'esilio: Riddler resta (bloccante 4/6, ha pescato) | 6c: Flare **nota** e 4 mana: Consign non la contrasta, Riddler sacrificato | **W1 in entrambe**: in 6c Riddler va al cimitero |
| 7 | 7a: Consign sul ritorno di Phelia (Nethergoyf) | 7c: Supplier, non vale una Consign | **Consign in entrambe** (D-15): in 7c diverge |
| 8 | 8a: Charm sui danni su FOMO, subito; 8b: con il trigger in pila, esilio dei cimiteri | 8c: senza delirium | 8a: **aspetta il trigger** ed esilia (la linea di 8b); 8b sì; 8c no |
| 9 | 9a: Solitude evocata + Ephemerate con il sacrificio in pila, due esili | 9b: Bolt in mano (nascosto) e rosso aperto: Bolt in risposta fa fallire la linea | Passa; la linea quando le creature attaccano, anche in 9b |
| 10 | 10a: Recruiter cerca Solitude e la si evoca subito; 10b: Sanctifier | 10c: Solitude già in mano | **Sanctifier in tutte e tre** |

Due note da decidere con te (sezione 5.1 di `ricerca.md`):
- **Il momento.** In 1, 3, 5, 8a, 9a la posizione è la prima priorità di UW nel turno di Rakdos; l'euristico fa la stessa giocata più tardi nel turno. Proposta: l'accordo si conta sulla **linea del turno**, il momento si legge nei replay.
- **Informazione nascosta.** In 6c ho reso Flare nota a UW (nascosta, 6 e 6c sarebbero lo stesso insieme di informazione); in 9b Bolt resta nascosto, ed è la probabilità di Bolt a decidere.

Registro: righe **P6-P10** (sezione 2b) e **D-2c** aggiornata.

## 6. Variante R-REC (non adottata)

**Regola** (sezione 30 di `docs/agente_uw.md`, opzione `recruiter_threat_or_graveyard`, spenta): Recruiter cerca Solitude se c'è una minaccia avversaria in campo e Solitude non è in mano; Sanctifier se non ci sono minacce e il cimitero avversario ha almeno due tipi; altrimenti la regola di oggi. **Definizioni provvisorie** (mie): "minaccia" = la classifica D-6, cioè ogni creatura di Rakdos tranne Stitcher's Supplier; "cimitero che cresce" = due tipi.

100.000 partite, seed 1-100.000, contro Rakdos base_r3, generatore per giocatore:

| | base_v2 | + R-REC | Differenza appaiata [IC 95%] |
| --- | --- | --- | --- |
| Win rate di UW, in play | 42,42% | 41,46% | −0,96 [−1,07; −0,85] |
| Win rate di UW, in draw | 32,20% | 31,72% | −0,48 [−0,58; −0,38] |
| **Win rate di UW, totale** | **37,31%** | **36,59%** | **−0,72 [−0,80; −0,65]** |
| Ricerche di Recruiter (14.369 partite) | 17.585 | 17.180 | |
| ... Solitude | 4.827 (27,5%) | 12.344 (71,9%) | |
| ... Sanctifier en-Vec | 10.914 (62,1%) | 3.184 (18,5%) | |
| ... altre | 1.844 (10,5%) | 1.652 (9,6%) | |
| Partite con Solitude cercata | 4.145, WR 68,42% | 11.483, WR 49,73% (base_v2 sugli stessi seed 56,01%) | −6,28 [−6,92; −5,64] |
| Partite con Sanctifier cercato | 10.914, WR 56,88% | 3.184, WR 72,58% (base_v2 71,77%) | +0,82 [+0,24; +1,39] |

**Partite in cui R-REC cambia la ricerca:** 8.339, **sempre Solitude al posto di Sanctifier**: UW 43,91% contro 52,56% → **−8,65 [−9,52; −7,77]** (−11,41 in play, −5,82 in draw).

**Lettura.** Con questi agenti Sanctifier cercato vale più di Solitude cercata anche con una minaccia in campo (coerente con l'ablazione: Sanctifier +6,25 punti per copia, una sola nella lista; le Solitude sono quattro). Due limiti, che la ricerca può togliere: l'euristico poi usa Solitude con le sue regole, non "subito"; "minaccia" comprende anche creature piccole (FOMO senza delirium, Street Wraith). Una definizione più stretta non è stata misurata. **Decidiamo insieme.**

## 7. Test e tempi

| | |
| --- | --- |
| Suite C++ in Release | **580 test case** (erano 548) + 2 di fumo, tutti verdi, 21-29 s |
| Nuovi (32) | vista e determinizzazione 11, posti 3, posizioni 12, R-REC 4, base_v1 e gemello di UW 2 |
| Suite in Debug (senza `[robustness]`) | verde, circa 40 s; i nuovi test in circa 10 s |
| Python | 90 test, invariati (nessun file Python cambiato) |
| Riferimenti MD5 (13 esecuzioni in parallelo) | 93 s, tre volte |
| `varianti_uw --r-rec` (2 × 100.000 partite) | 136 s + 94 s |

## 8. Errori trovati e corretti

- Il turno spostato prima della prima decisione faceva pescare UW in una posizione (6c): ora si sposta nella fase principale.
- Le preparazioni dei test delle carte note si fermavano troppo tardi (senza azioni Rakdos non riceve la priorità): Street Wraith in mano a Rakdos.
- "Linea risolta" = pila vuota non bastava (trigger di Solitude in attesa): ora anche nessun trigger in attesa.
- In 6c UW non riceve la priorità con Flare in pila (Consign senza bersagli): il test lo verifica così, invece di aspettare uno stato che non esiste.
- Una traccia temporanea con `getenv` (avviso trattato come errore in MSVC) e una stringa C++ spezzata da uno script: corrette.

## 9. Prossimo passo (proposta)

Passo 2: livello A minimo (fase principale, risposte, bersagli come mosse composte, budget fisso, mondi comuni), l'accordo sulle posizioni test, la curva di convergenza, il test (b) del gemello per la scelta, le esecuzioni riprendibili.

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
