# Ultimo resoconto: fase 3, passo 1 delle metriche, il record compatto (16/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). **Nessun agente e nessuna regola del motore cambiano**: i registri del matchup e del goldfish sono identici byte per byte ai riferimenti degli euristici v1.

## In breve

- **[docs/metriche.md](metriche.md) aggiornato con le tue decisioni:** le 11 della sezione 13 e le quattro aggiunte A-D (ablazione, OH sulla prima mano da 7, avvertenza sull'IWD a turno fisso, convenzione del delirium). La sezione 13 ora è la tabella delle decisioni; la nuova sezione 15 ha i risultati del passo 1.
- **Passo 1 fatto:** `metriche.exe` scrive una riga JSON per partita con mani, mulligan, carte in fondo, ogni evento di ogni copia, terre a fine turno, carte in mano alla fine e delirium. 100.000 partite in 90 secondi su un core, 14 in 8 processi.
- **Tutti i controlli della sezione 11 passano:** registri identici, 0 record incoerenti su 100.000, la versione Python del record coincide con quella C++ sui primi 20 seed, risultati identici in parallelo.
- **Dimensione: 2,5 KB a partita** (253 MB per 100.000), al limite alto della stima. Il formato resta questo; Parquet al passo 2.
- **Delirium (aggiunta D):** adottata la convenzione del goldfish. Le due convenzioni differiscono solo quando il delirium arriva nel turno di UW: nel 10,95% dei casi.
- **Un errore mio, trovato dal test:** avevo scritto a memoria che solo Riddler ed Ephemerate si lanciano dall'esilio; anche Starfield Shepherd ha il warp.

## 1. Il documento: decisioni e aggiunte

| # | Decisione | Dove è scritta |
| --- | --- | --- |
| 1 | IWD a turno fisso principale tra le osservazionali, IWD classico accanto; per i side vale l'ablazione | Sezione 3 |
| 2 | "Solo cercata" e "vista solo dal cimitero" fuori da GIH e GNS | Sezione 3 |
| 3 | Terre fuori dalla tabella principale, Witch dentro | Sezione 3 |
| 4 | Indicativo sotto 1.000 partite, nascosto sotto 200 | Sezione 5 |
| 5 | Cinque coppie in più (Consign + Phelia, Ephemerate + Riddler, Thraben Charm + Sanctifier; Flare + Supplier, Street Wraith + Nethergoyf) | Sezione 4 |
| 6 | Minaccia grande = forza 4 o più, con la forza massima nel record | Sezione 7 |
| 7 | Campo `source` nel motore al passo 3, registri identici | Sezione 7 |
| 8 | Starfield Shepherd e Thraben Charm tra le carte della "settima carta" | Sezione 9 |
| 9 | Sezione 9: prima 100.000 seed, poi 1.000.000 in parallelo | Sezione 9.2 |
| 10 | Un generatore per giocatore: non ora, prima della fase 4 con un ricalcolo unico | Sezione 9.2 |
| 11 | Varianti di Witch solo se la 9.3 non basta | Sezione 9.5 |
| A | Ablazione: una copia sostituita da Plains o Swamp, differenza appaiata, play e draw | Sezione 3, passo 2b |
| B | OH7 sulla prima mano da 7 | Sezione 3 |
| C | L'IWD a turno fisso è descrittivo, non causale | Sezione 3 |
| D | Turno del delirium con la convenzione del goldfish, differenza misurata | Sezioni 6 e 15 |

**Ablazione, tempi stimati:** 28 carte × 100.000 partite, circa 35 minuti su un core senza record (basta l'esito), circa 70 con il record; 5-10 minuti in 8 processi. Nel documento ci sono anche i limiti: misura "la carta contro una terra in più", le carte da una copia spariscono dalla lista, gli agenti sono tarati sulle liste attuali. **Non l'ho implementata**: è un passo a sé (2b).

## 2. Il record compatto

Si costruisce **solo dal registro in memoria della partita** (`GameLog`) e dall'esito, con una funzione pura: non tocca lo stato né il generatore casuale, quindi non può cambiare la partita. Rispetto alla proposta:

- **Due codici in più:** `F` (terra messa in campo da una fetch) e `V` (carta sorvegliata nel cimitero). Erano spostamenti dal grimorio che la proposta non elencava.
- **`?`** segna uno spostamento non previsto: non deve comparire mai, e su 100.000 partite non compare.
- **Il ciclo** (Street Wraith, Troll) il motore lo registra come uno scarto: si riconosce dall'attivazione della carta in mano, anche con una decisione di pagamento in mezzo.
- **Le minacce grandi** entrano al passo 3, con il campo `source`.
- **Delirium:** turno della partita, turno di Rakdos (convenzione del goldfish), volte perso, delirium a fine partita.

**Codici prodotti su 100.000 partite:**

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

## 3. I controlli della sezione 11

| Controllo | Esito |
| --- | --- |
| `partite.jsonl` di `metriche.exe`, 10.000 e 100.000 partite | Identici ai riferimenti (`2302d021…`, `0ad6744d…`) |
| `matchup.exe` (anche `--rakdos-r1`, `--rakdos-r2`) e `goldfish.exe` dopo le modifiche | Tutti gli MD5 identici |
| Riepilogo | UW 37,26% (42,72% in play, 31,79% in draw); mani tenute come `matchup.exe` |
| Determinismo | Due esecuzioni da 100.000: stesso `record.jsonl` (`6fc9e9fc…`) |
| Parallelo | 8 processi da 12.500 seed, concatenati: stessi MD5 dell'esecuzione unica |
| Coerenza di ogni record | 0 problemi su 100.000 (copie ≤ lista, mano iniziale = 7 − mulligan, carte in fondo = mulligan, arrivi − uscite = mano finale, una voce di terre per turno) |
| Record contro stato (60 partite, test) | Mani di ogni mulligan, mano tenuta, mano finale carta per carta, terre, delirium: uguali |
| Carte morte (stesse 60 partite) | Carte in mano a fine partita uguali all'osservatore di `carte_morte.exe` |
| Due implementazioni (C++ e Python) | Primi 20 seed: 0 differenze |
| Il record non cambia la partita (test) | Stesso esito e stesso registro con e senza |
| La prima mano da 7 dipende solo dal seed (test, 20 seed) | Con la prima decisione di mulligan di un giocatore forzata a "tieni" o "mulligan", le prime mani dei due non cambiano |

**Dimensione e tempi:**

| | 1.000 partite | 100.000 partite |
| --- | --- | --- |
| `record.jsonl` | 2.540 byte a partita | 253 MB, 2.528 byte a partita |
| Tempo, un core | 1,0 s | 90 s |
| Tempo, 8 processi | — | 14 s |

## 4. Il delirium con le due convenzioni (aggiunta D)

Raggiunto in 93,45% delle partite; nel turno di UW nel 10,95% di queste. Perso almeno una volta in 30.848 partite (le carte che esiliano il cimitero sono la fuga di Nethergoyf, il conferimento di Phoenix e Thraben Charm; quale pesa di più non è ancora misurato: serve il campo `source` del passo 3).

| Turno di Rakdos | Goldfish (adottata) | "Il turno appena giocato" |
| --- | --- | --- |
| 1 | 25,62% | 31,62% |
| 2 | 36,29% | 32,24% |
| 3 | 17,34% | 16,53% |
| 4 | 7,96% | 7,35% |
| 5 | 3,35% | 3,17% |
| 6 o più | 2,89% | 2,54% |
| Mai | 6,55% | 6,55% |

Con la convenzione del goldfish il turno è il primo in cui Rakdos può giocare con il delirium; il record tiene anche il turno della partita, quindi l'altra convenzione si ricalcola quando serve.

## 5. Test

- **C++:** 10 test case nuovi in `tests/test_metriche.cpp`. Con uno stub vuoto: 9 rossi su 10 (il decimo, "il record non cambia la partita", è una guardia e passava già). Poi il codice. Mutazioni: ciclo mai riconosciuto → 2 test case falliti; convenzione del delirium sbagliata → 1; Witch come terra mai contata → 2. Suite completa in Release: **529 test passati** (erano 519); dopo le modifiche tutti i riferimenti MD5 di `matchup.exe` (anche `--rakdos-r1` e `--rakdos-r2`) e di `goldfish.exe` sono identici.
- **Python:** 6 test nuovi in `python/tests/test_verifica_record.py`, rossi con lo stub e poi verdi; 24 test Python in tutto.

## 6. Errori e correzioni

- **Starfield Shepherd ha il warp** (Oracle: "Warp {1}{W}"): il test sui codici "sulle carte giuste" era sbagliato, non il motore. Corretto il test.
- **Una frase a memoria nel documento** (il delirium come "forza di Nethergoyf": la forza dipende dai tipi nel cimitero): riscritta prima del commit.
- **Un comando della shell rimasto in attesa** di ingresso (un `cat` senza file): fermato, nessun file del progetto toccato.

## 7. Prossimo passo

Passo 2: conversione in Parquet e metriche per carta (GIH, OH, OH7, GD, GNS, IWD classico e a turno fisso, con Wilson e bootstrap), con i test Python. Poi il 2b, l'ablazione.

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
