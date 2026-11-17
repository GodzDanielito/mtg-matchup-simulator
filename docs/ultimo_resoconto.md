# Resoconto: fase 4, passo 2 (livello A minimo, UW forte)

Per Daniele, 17/11/2026. Tutti i numeri e le sezioni sono in [docs/ricerca.md](ricerca.md) (sezione 10).

## In breve

| Punto | Esito |
| --- | --- |
| Forza reale (seed 1-2.000, R = 256) | UW forte **62,30%** contro 35,75% degli euristici: **+26,55 [+24,42; +28,68] punti** (play +27,30, draw +25,80) |
| Canarino (stato vero, seed 1-1.000) | 73,90% contro 62,90% del forte: **+11,00 [+8,47; +13,53]**, controllo passato |
| Costo | 45,3 decisioni cercate a partita (stima 42), 55.013 rollout a partita, 4,8 s a partita (20 thread): 10.000 partite = 13 ore, non 2 |
| Ripresa | kill a 40 s: 6 blocchi rifatti, MD5 identici; la notte reale non ha avuto scadenze |
| Euristici | 23 riferimenti MD5 della sezione 1 identici; test C++ 612/612, Python 90/90 |

## Un errore trovato dalla ricerca (decisione per te)

La prima notte si è fermata: una mossa che gli euristici non fanno (March su un proprio token) porta a una catena di sacrifici con due Flare of Malice e la pila supera i 16 elementi. Ho corretto con un test prima: nei rollout il limite vale 0 e viene contato (4 su 110 milioni). **Non ho alzato il limite** (cambierebbe lo stato). Resta il rischio che la mossa *scelta* porti davvero a quello stato: lo alziamo?

## 10.6 Il canarino (diagnostica, mai nei dati)

Il canarino fa la stessa ricerca sullo stato **vero** (mondi con le carte nascoste vere), con R = 256 e lo stesso 99%. Seed 1-1.000 (2 blocchi), appaiato con il giocatore forte onesto sugli stessi seed (`data/ricerca/canarino_contro_forte.txt`):

| Giocatore | Tutte (1.000) | UW in play (500) | UW in draw (500) |
| --- | --- | --- | --- |
| Euristico v1 | 36,10% | 41,00% | 31,20% |
| UW forte (onesto) | 62,90% | 68,40% | 57,40% |
| Canarino (stato vero) | **73,90%** | 77,60% | 70,20% |
| Canarino − forte | **+11,00 [+8,47; +13,53]** | +9,20 [+5,78; +12,62] | +12,80 [+9,08; +16,52] |

**Il canarino vince almeno quanto il forte: il controllo è passato** (e di molto: +11 punti, IC95 lontano da zero). Lettura: sapere la mano di Rakdos e l'ordine del suo mazzo vale 11 punti sopra la ricerca onesta, quindi **l'informazione nascosta pesa** (e un'inferenza sulla mano dell'avversario, sezione 3.4, ha margine); il forte onesto non è al tetto del livello A. Il canarino scosta dall'euristico meno spesso (3,2% contro 4,6% delle decisioni cercate). Costo: 68.622 rollout a partita (5.868 s con 20 thread per 1.000 partite), 1 rollout interrotto dal limite di capacità del motore.

## 10.7 Esecuzioni riprendibili

Ogni esecuzione scrive un file per blocco di 500 seed (nome temporaneo, poi rinominato) e `manifest.json` con la configurazione, MD5 e secondi di ogni blocco; rilanciando lo stesso comando si rifanno solo i blocchi mancanti o corrotti, con una configurazione diversa si ferma (`--nuova` ricomincia). Il numero di thread non cambia i file. Prove:
- **Test unitari** (`tests/test_esecuzioni.cpp`): MD5 contro i vettori RFC 1321, ripresa dopo un blocco mancante, blocco corrotto rifatto, configurazione diversa rifiutata, stessi file con 1 e con 20 thread.
- **Prova vera con una fermata brutale** (200 seed, R = 8, blocchi da 20): vedi la tabella sotto (`data/ricerca/prova_ripresa/ripresa.txt`).
- **La notte del 17/11/2026** ha ripreso da sola tra le invocazioni (un blocco per invocazione, con la scadenza delle 06:50 controllata prima di ogni blocco: non è scattata).

| Prova | Risultato |
| --- | --- |
| Esecuzione intera (200 seed, R = 8, blocchi da 20) | 10 blocchi, 81 s |
| Fermata brutale (kill) dopo 40 s | 4 blocchi finiti, nessun file a metà (solo nomi definitivi) |
| Ripresa | 10 blocchi: fatti ora 6, già fatti 4, in 46 s |
| MD5 `partite.jsonl` intera / interrotta | d1cad2a7a0754af931e6b3bc4274e2b8 / d1cad2a7a0754af931e6b3bc4274e2b8 (uguali) |
| MD5 `ricerca.jsonl` intera / interrotta | adcf49d97653a65a85d8a95f17c783b2 / adcf49d97653a65a85d8a95f17c783b2 (uguali) |

La prima versione della prova (kill a 25 s) cadeva prima del primo blocco finito e dimostrava poco: rifatta con blocchi più piccoli.

## 10.8 Forza reale: UW forte contro Rakdos base_r3, seed 1-2.000, R = 256

R = 256 dalla curva (10.4). 20 thread di notte, 4 blocchi da 500 seed, uscita 0 (`data/ricerca/forte_R256/`; MD5 `partite.jsonl` 302f9e96b6bc6ae07e01e40e1059f85d, `ricerca.jsonl` 3814a0e28b1d33df8504207648f9d946).

| | UW euristico v1 | UW forte | Differenza appaiata (IC 95%) |
| --- | --- | --- | --- |
| Tutte (2.000) | 35,75% | **62,30%** | **+26,55 [+24,42; +28,68]** |
| UW in play (1.000) | 41,60% | 68,90% | +27,30 [+24,28; +30,32] |
| UW in draw (1.000) | 29,90% | 55,70% | +25,80 [+22,79; +28,81] |

**La ricerca onesta, con soli rollout euristici e 256 mondi, porta UW da 36 a 62%: +26,6 punti.** Il guadagno è uguale in play e in draw. È molto più grande di quanto la curva lasciasse pensare (3,1 punti a decisione, che non si sommano ma si sommano abbastanza): le regole dell'euristico di UW sbagliano in posti che contano.

| Costo | Misurato | Stima (sezione 7) |
| --- | --- | --- |
| Decisioni di UW a partita | 101,3 | |
| Decisioni **cercate** a partita | **45,3** (fase principale 18,6; risposta 14,0; turno avversario 12,8) | 42 |
| Mosse composte per decisione cercata | 4,74 | 6,9 (pilota) |
| Rollout a partita | **55.013** | 10.800 (budget adattivo) |
| Scostamenti dall'euristico | 4.207 su 90.617 decisioni cercate (4,64%), 2,10 a partita | |
| Rollout interrotti dal limite della pila (valgono 0) | 4 su 110.026.752 | |
| Tempo | 9.584 s per 2.000 partite con 20 thread: **4,8 s a partita**, 0,106 s di orologio e **2,1 s di thread** per decisione cercata | |
| Throughput sostenuto | 11.480 rollout/s con 20 thread | 15.000 / 8.800 |

La stima di 2,0 ore per 10.000 partite valeva per un budget adattivo (circa 10.800 rollout a partita); con **R = 256 fisso** il costo è 5 volte maggiore: **10.000 partite = 13,3 ore, 100.000 = 5,5 giorni.** Per questo i passi successivi (tipi di decisione che si cercano davvero, potature, budget adattivo) contano più di aggiungere decisioni.

**Dove la ricerca si scosta (accordo per regola dell'ombra).** Le regole con più disaccordo sono quelle di "passa": **Phelia, Exuberant Shepherd** al posto di passare (439), **Solitude** in evocazione, soprattutto esiliando Witch Enchanter (206+126+...), **Witch Enchanter come terra** (108), **Quantum Riddler con il warp** (108), **Thraben Charm sul cimitero avversario** (69). Accordo più basso: Solitude nella sottofase finale avversaria 74,6%, Teferi −3 su un bersaglio ragionevole 78,0%, le scelte di terra 84-89%. Le regole di Daniele con accordo alto (96-100%): Ephemerate con Solitude evocata, Phelia a fine turno, Thraben Charm con Nethergoyf. Le tabelle complete sono in `data/ricerca/forte_R256/riepilogo.txt`.

## 10.9 Errori trovati e difetti noti

- **La pila a 16 elementi ha fermato la prima notte.** Una mossa che gli euristici non fanno (March su un proprio token) porta a una catena di sacrifici con due Flare of Malice e i trigger oltre il limite del motore (`EngineLimitError`). Corretto con un test prima: nei rollout un limite superato vale 0 (come la patta) e viene contato (`interrotti`); 4 rollout su 110 milioni. **Non** si è alzato il limite (cambierebbe la dimensione dello stato). Rischio residuo: se la mossa **scelta** portasse davvero a quello stato, la partita si fermerebbe.
- **Rakdos: `block()` conta ancora attaccanti non token già morti** (difetto noto, non toccato: gli euristici non cambiano); l'agente di UW ha lo stesso schema (non toccato).
- **Il token sparito:** un attaccante token non c'è più al momento dei bloccanti; corretto in `block()` con un test (10.2).
- **La stima dei costi era per un budget adattivo:** il R fisso costa 5 volte (10.8).


## Curva di convergenza e posizioni (da ricerca.md 10.4 e 10.5)

### 10.4 Curva di convergenza

**Come è fatta** (`ricerca.exe --curva`, `data/ricerca/curva/`). 1.000 decisioni **vere**: per i seed 1.000.001-1.001.000 si gioca la partita degli euristici v1 e se ne prende **una decisione cercata di UW a caso** (con un generatore fisso), con lo stato vero e l'agente di UW di quel momento (l'ombra). Per tipo: 404 di fase principale, 278 di risposta, 318 nel turno avversario; **4,70 mosse composte** in media (330 decisioni con 2 mosse, 1 con 106). Su ogni decisione, una ricerca con 2.048 mondi per mossa (seed della ricerca come in partita): la scelta a budget R usa i primi R mondi, così i budget sono annidati. Il **riferimento** è una seconda ricerca, **indipendente** (altro seed), con 16.384 mondi per mossa (8 volte il massimo). Le stime di valore del riferimento sono quindi senza la distorsione della selezione per le scelte ai budget R. 86,6 milioni di rollout in 9.162 s (2,5 ore) con 20 thread: **9.457 rollout al secondo**.

| R | Scelte uguali al riferimento | Migliore stimata uguale | Valore perso rispetto al riferimento (punti) | **Guadagno sull'euristico** (punti, IC95) | Quota del guadagno del riferimento | Scostamenti dall'euristico |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 69,4% | 75,0% | 1,88 | 1,17 ± 0,49 | 38% | 2,8% |
| 32 | 71,0% | 79,5% | 1,51 | 1,54 ± 0,52 | 50% | 4,5% |
| 64 | 74,0% | 83,6% | 1,06 | 2,00 ± 0,54 | 65% | 7,9% |
| 128 | 77,4% | 86,4% | 0,70 | 2,36 ± 0,55 | 77% | 11,3% |
| **256** | **82,5%** | **88,3%** | **0,36** | **2,70 ± 0,56** | **88%** | **16,6%** |
| 512 | 85,5% | 91,8% | 0,24 | 2,81 ± 0,56 | 92% | 19,5% |
| 1.024 | 89,4% | 93,5% | 0,11 | 2,94 ± 0,56 | 96% | 23,4% |
| 2.048 | 91,7% | 93,9% | 0,07 | 2,99 ± 0,56 | 98% | 26,1% |
| rif. 16.384 | — | — | — | 3,06 (con la selezione) | 100% | 33,2% |

"Valore" e "guadagno" sono **punti di probabilità di vittoria di UW secondo i rollout del riferimento** (cioè se dopo la mossa giocano gli euristici), in media su tutte le 1.000 decisioni, comprese quelle in cui non cambia niente. Il guadagno a budget R è il valore della scelta a R meno il valore della scelta dell'euristico, entrambi stimati dal riferimento indipendente.

Per tipo (guadagno sull'euristico e quota del guadagno del riferimento):

| R | Fase principale (404) | Risposta (278) | Turno avversario (318) |
| ---: | ---: | ---: | ---: |
| 16 | 2,15 (51%) | 0,75 (33%) | 0,30 (13%) |
| 64 | 3,12 (74%) | 1,36 (60%) | 1,12 (48%) |
| 128 | 3,53 (84%) | 1,74 (77%) | 1,41 (61%) |
| **256** | **3,84 (92%)** | **1,92 (85%)** | **1,92 (83%)** |
| 512 | 3,95 (94%) | 2,02 (89%) | 2,06 (89%) |
| 2.048 | 4,12 (98%) | 2,20 (97%) | 2,24 (97%) |

**Cosa dice.**
- **Il ginocchio è a R = 256.** Fino a 256 ogni raddoppio del budget aggiunge 0,34-0,45 punti di guadagno per decisione; da 256 a 512 solo 0,12, poi 0,13 e 0,05. Con 256 la ricerca prende l'88% del guadagno del riferimento (83-92% per tipo); il turno avversario è il più lento a convergere (61% a 128). **Budget scelto: R = 256** (*scelta di Claude*, dalla curva come chiesto).
- **Nessuno scostamento sbagliato.** Fino a R = 512 **ogni** scostamento della ricerca è confermato dal riferimento indipendente: anche lui si scosta dall'euristico e dà alla mossa scelta un valore più alto di quello dell'euristico (166 scostamenti su 166 a R = 256, 157 con la stessa mossa del riferimento). A 1.024 e 2.048 uno solo non è confermato. Il rischio della selezione tra K mosse (10.1) in pratica non si vede: gli scostamenti trovati al 99% sono differenze grandi (16 punti in media a R = 256, 42 a R = 16).
- **Il valore in gioco è grande.** Secondo il riferimento l'euristico perde **3,1 punti a decisione cercata** in media e si sbaglia (al 99% con 16.384 mondi) in un terzo delle decisioni. I punti non si sommano lungo la partita (le decisioni non sono indipendenti e la vittoria vale al massimo 100), e sono **guadagni di un passo** con gli euristici dopo: la forza vera la dice la partita (10.8).
- **Dove sbaglia l'euristico** (scostamenti del riferimento, mossa dell'euristico → mossa del riferimento): passare invece di **evocare Solitude** (75 volte su 1.000), invece di lanciare **Phelia** (59), **March** (41), il warp di **Quantum Riddler** (17), **Ephemerate** (14), **Thraben Charm** (12), **Consign** (9), la terra **Witch Enchanter** (7). Quasi sempre è l'euristico che **passa** dove una giocata vale di più: tre quarti degli scostamenti del riferimento (251 su 332) vengono dalla regola "priorità" (che si scosta in 251 delle sue 696 decisioni), uno su sette dalla scelta della terra (48 su 117).

### 10.5 Le 22 posizioni (accordo con la linea del turno)

`ricerca.exe --posizioni`, `data/ricerca/posizioni/`: per ogni posizione e budget 100 ricerche con seed diversi, si gioca il turno e si conta la quota in cui la **linea del turno** coincide con quella indicata (il momento fa parte della linea in 6, 7 e 8a). Accanto, l'euristico (base_v2) sugli stessi seed. 11.000 turni in 1.892 s con 20 thread. Tabella completa e giocate più frequenti in `data/ricerca/posizioni/riepilogo.txt`.

| Posizione | Euristico | R = 16 | 32 | 64 | 128 | **256** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1, 2, 2c, 3, 4, 4c, 5, 6, 7a, 7c, 8b, 9a, 10b | 100% | 100% | 100% | 100% | 100% | **100%** |
| 1c, 3c, 5c, 10a, 10c | 0% | 0% | 0% | 0% | 0% | 0% (1c: 3%) |
| **6c** (Flare nota) | 100% | 58% | 13% | 1% | 0% | **0%** |
| 8a (Charm sui danni) | 0% | 0% | 0% | 0% | 1% | **9%** |
| 8c (Charm senza delirium) | 0% | 0% | 0% | 0% | 0% | 6% |
| 9b (Bolt in mano) | 100% | 100% | 100% | 100% | 97% | 98% |

**Come si legge** (un accordo, non un voto): la ricerca gioca la linea indicata dove l'euristico la gioca e non la gioca dove l'euristico non la gioca; i due casi che cambiano con il budget sono quelli utili.
- **6c: la ricerca smentisce la linea con Flare nota.** Con 256 mondi gioca sempre Teferi, −3 su Nethergoyf, poi la terra (100 su 100), non Riddler con il warp: Flare of Malice fa sacrificare Riddler e Consign non la contrasta. La linea dell'euristico (W1, Riddler con il warp) vale 55,5% di vittorie contro 86,3% di Teferi (differenza +30,9 punti [+22,2; +39,5] al 99%). Coerente con la tua decisione 3 (Flare nota in 6c): la linea indicata è giusta per 6, non per 6c.
- **8a: 9% di Charm sui danni.** Quasi sempre (83%) la ricerca esilia i cimiteri come l'euristico; in 8 ricerche su 100 gioca i danni su FOMO prima dell'attacco, la tua linea. Con questi rollout la linea di 8b (aspettare il trigger) e quella di 8a valgono quasi lo stesso; la tua linea non è smentita, ma non è nemmeno preferita.
- **1c, 8c**: 3% e 6% di giocate diverse dall'euristico, da leggere nei replay (`replay/posizione_1c_*`, `posizione_8c_*`).
- **9b**: a 128 e 256 mondi la ricerca gioca la linea in 97-98 casi su 100 ma talvolta in un'altra sottofase (mantenimento o acquisizione invece che nell'attacco): la linea è la stessa, il momento no.
- **Limite del passo 2:** in 5, 5c, 9 e 10 il bersaglio di Solitude (trigger) e la carta cercata da Recruiter non sono decisioni della ricerca (10.1), quindi l'accordo è per costruzione quello dell'euristico: **10a e 10c restano a 0%** (Sanctifier invece di Solitude) e 5c gioca Nethergoyf invece di Supplier. Non dicono niente su Daniele D-2c: la ricerca delle carte e dei bersagli dei trigger è la proposta del passo 3.
- **Replay** di tre casi in cui la ricerca sceglie diverso, con le stime: `data/ricerca/posizioni/replay/posizione_6c_R256_ricerca_1.txt` (Teferi −3 invece di Riddler), `posizione_8a_R256_ricerca_*.txt` (danni invece dell'esilio dei cimiteri), `posizione_1c_R256_ricerca_*.txt` (Solitude invece di March). I file sono in `data/` (esclusi da Git).


## Tre replay dove la ricerca sceglie diverso

In `data/ricerca/posizioni/replay/` (esclusi da Git): `posizione_6c_R256_ricerca_1.txt` (Teferi −3 invece di Riddler), `posizione_8a_R256_ricerca_*.txt` (danni invece dell'esilio dei cimiteri), `posizione_1c_R256_ricerca_*.txt` (Solitude invece di March).

## Cosa resta da dirti

- Il rischio della pila a 16 (sopra).
- Rakdos `block()` conta ancora attaccanti non token già morti; l'agente di UW ha lo stesso schema: non toccati (gli euristici non cambiano).
- Costo: con R = 256 fisso, 100.000 partite sono 5,5 giorni. Il passo 3 (cercare carte e bersagli dei trigger, potature, budget adattivo) deve ridurre il costo e coprire 5, 9, 10.

URL: https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
