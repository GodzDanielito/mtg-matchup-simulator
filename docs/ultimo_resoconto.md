# Resoconto: ricerca approvata e passo 0, un generatore casuale per giocatore (16/11/2026)

## 1. Cosa è stato fatto

- **`docs/ricerca.md` approvato**, con le tue risposte nella sezione 9 (1, 2, 3, 4, 5, 7 sì; la 6 e le posizioni test aspettano) e le tre correzioni:
  - **posizione test 9** con **due** creature avversarie (con una sola, Ephemerate salva solo il corpo di Solitude);
  - **99% di confidenza** per scostarsi dalla scelta dell'euristico (sezione 4.2): circa 1,7 volte i mondi per la stessa differenza; l'arresto per equivalenza resta al 95% (*scelta mia*: lì fermarsi presto fa giocare la regola);
  - **esecuzioni lunghe riprendibili** (sezione 8): un file per blocco di seed scritto e poi rinominato, un manifest con la configurazione e l'MD5 di ogni blocco, ripresa dei soli blocchi mancanti, test "interrotta e ripresa = senza interruzioni".
- **Sistemazioni:** `tools/sonda_ricerca.cpp` nel repository, con una riga nel README e **due test di fumo** in `ctest`; nessun push forzato per il BOM; regola in `CLAUDE.md`: messaggi di commit con `git commit -m` o con un file UTF-8 senza BOM.
- **Passo 0:** un generatore PCG32 per giocatore, test prima del codice, riferimenti MD5 ricalcolati una volta sola, confronti con i vecchi, tabelle dei risultati marcate "euristici v1, generatore unico". Nessun agente cambiato.

## 2. Il generatore per giocatore

| | Prima | Ora |
| --- | --- | --- |
| Dove | `GameState::rng`, uno per la partita | `PlayerState::rng`, uno per giocatore (`GameState::rng` non esiste più) |
| Semi | `Pcg32(seed)` | Seme e stream di ogni giocatore da **SplitMix64** del seed della partita (`player_generator`) |
| Mescolamenti | Tutti con lo stesso generatore | Il grimorio di P con il generatore di P: grimorio iniziale, mulligan, fetch, ricerche, ciclo del Troll |
| Play o draw | Parità del seed (orchestratore) | Uguale |
| `sizeof(GameState)` | 11.584 byte | **11.608 byte** (sotto il tetto di 16 KB) |

## 3. Test (prima del codice)

`tests/test_generatori.cpp`, 6 test case; con gli stub **4 rossi**, poi verdi:

| Test | Cosa verifica |
| --- | --- |
| SplitMix64 | Valori di riferimento |
| Generatori dei giocatori | Semi da SplitMix64, diversi tra i giocatori, usati da `create_game` |
| (a) Determinismo | Stesso seed, stessa partita voce per voce (guardia: verde anche prima) |
| (b) Grimorio e pescate | Il giocatore 0 tiene o fa un mulligan: grimorio, mano e generatore di Rakdos identici finché Rakdos sceglie le stesse azioni, **anche dopo i suoi mescolamenti**; 40 seed. Rosso con il generatore unico |
| (c) Mani del mulligan | Le tre mani da 7 di un giocatore non cambiano se l'altro fa un mulligan; 80 casi. Rosso con il generatore unico |
| (d) Dimensione | Sotto 16 KB (guardia) |

**9 test vecchi dipendevano dal mescolamento** (nessun comportamento cambiato): seed nuovi con lo stesso criterio (correzione del turno 1: 149, 499, 824, 828 e 3; X6: 3 e 4; mulligan a 6 carte: 183), posizioni preparate rese indipendenti dal grimorio (W1, Recruiter, debolezza da evocazione), un controllo del replay leggibile reso preciso. **Suite C++ in Release: 548 test verdi**, più i 2 test di fumo della sonda; **Python 90**.

## 4. Riferimenti MD5 nuovi (sezione 1 di `metriche.md`)

| Registro | Comando | MD5 |
| --- | --- | --- |
| Matchup 10.000 | `matchup.exe --replays 20` | `partite.jsonl` `963849614217fe80325bba4383dc8cef`, `replay.jsonl` `01325de38fc145ac2afe84f27fac36ab` |
| Matchup 100.000 | `matchup.exe --games 100000` | `partite.jsonl` `6a580e5037b4795701a08c48e5b20376`, `replay.jsonl` `1cd0fff7dc62ccd975ae1c4549717c99` |
| Matchup base_r1 | `--replays 20 --rakdos-r1` | `bb968dee...`, `190f3547...` |
| Matchup base_r2 | `--replays 20 --rakdos-r2` | `ffc8f620...`, `18ee805e...` |
| Goldfish 10.000 / 100.000 | `goldfish.exe` | `9af276f7...`, `048fd4c3...` / `4a787970...` |
| Goldfish base_r1 10.000 / 100.000 | `--rakdos-r1` | `7234144c...`, `e2e0d67a...` / `1e91cca3...` |
| Goldfish base_r2 10.000 / 100.000 | `--rakdos-r2` | `d5cc5fd9...`, `dc263e56...` / `27f9189a...` |
| Record 10.000 / 100.000 | `metriche.exe` | `710432cc...` / `81831db0...` |

I vecchi restano nella sezione 1 come "generatore unico, storici"; note nelle sezioni 22 e 24 di `agente_rakdos.md` e 28 di `agente_uw.md`. UW base_v1 non ha un registro di riferimento: `matchup.exe` non ha l'opzione (si misura con `varianti_uw.exe`).

## 5. Win rate: prima e dopo (punto 3)

Partite diverse: differenza tra proporzioni indipendenti, intervallo al 95%.

| Misura (100.000 partite) | Generatore unico | Per giocatore | Differenza |
| --- | --- | --- | --- |
| UW base_v2 contro Rakdos base_r3 | 37,26% | 37,31% | +0,05 [−0,37; +0,48] |
| ... in play / in draw | 42,72% / 31,79% | 42,42% / 32,20% | −0,30 / +0,41 (entro ±0,6) |
| Rakdos base_r1 / base_r2 / base_r3 contro UW base_v2 | 60,44 / 62,36 / 62,74% | 60,40 / 62,23 / 62,69% | −0,04 / −0,13 / −0,05 (entro ±0,43) |
| UW base_v1 / base_v2 contro Rakdos base_r1 | 35,77 / 39,56% | 35,59 / 39,60% | −0,18 / +0,04 (entro ±0,43) |
| Goldfish base_r3: mai il delirium (play / draw); vittoria media | 4.870 / 3.617; 4,8 / 4,6 | 4.825 / 3.610; 4,8 / 4,6 | uguali |

**Tutte le win rate restano statisticamente uguali.** Le differenze appaiate (X1-C, X5, base_r2, X6, M1) restano dentro l'intervallo della differenza tra le due stime; **due escono di poco**: base_v2 − base_v1 contro base_r1 +3,79 → +4,01 e W1 +1,83 → +2,00 (scarti 0,22 e 0,17 contro ±0,21 e ±0,14), entrambe sulle stesse partite di base_v1, che vince 0,18 punti meno (entro il suo intervallo). Su una ventina di confronti uno o due fuori dal 95% sono attesi; segno e grandezza non cambiano.

## 6. Quanto migliora l'appaiamento (punto 4)

| Confronto | Generatore unico | Per giocatore | Ampiezza dell'IC |
| --- | --- | --- | --- |
| **X6 contro base_r2** | +0,38 [+0,27; +0,50] | +0,46 [+0,35; +0,56] | 0,23 → 0,21, **−9%** |
| X6 dove scatta | +3,92 [+2,76; +5,09] | +4,69 [+3,61; +5,78] | −7% |
| X1-C, X5, base_r2 contro base_r1 | — | — | −6% / −7% |
| **W1 sul totale** (contro Rakdos base_r1) | +1,83 [+1,73; +1,92] | +2,00 [+1,90; +2,10] | 0,19 → 0,20, **+5%** |
| base_v2 contro base_v1 | — | — | 0% |
| M1 contro base_v2 | +1,18 [+1,04; +1,31] | +1,09 [+0,96; +1,22] | −4% |
| Sonda: correlazione di due mosse negli stessi mondi | 0,81 | 0,80 | — |

**Il guadagno è piccolo.** L'appaiamento dura finché le due partite non divergono: dopo il primo scostamento anche l'avversario gioca diversamente, e la variante cambia spesso i mescolamenti del proprio grimorio (X6 toglie un ciclo del Troll, X1-C cambia il mulligan). Per la ricerca il valore è soprattutto di principio: le scelte del giocatore forte non spostano più le pescate dell'avversario.

## 7. Controlli del punto 5

| Controllo | Esito |
| --- | --- |
| `metriche.exe` 10.000 e 100.000 | `partite.jsonl` uguale al matchup; 0 record con problemi |
| Record Python contro C++, primi 20 seed | **0 differenze** |
| Ablazione di prova Plains:Plains e Swamp:Swamp (10.000 seed) | Stesso MD5 della base: **differenza zero** |
| Mulligan K/M, 5.000 seed per lato | 0 partite di una variante diverse da K o M |

## 8. Tabelle dei risultati (punto 6)

Non rifatte. Marcate **"euristici v1, generatore unico"**: titoli delle sezioni 15-18 di `metriche.md`, nota in testa al documento e al registro; `risultati_v1.md` e `risultati_v1_mulligan.md` rigenerati dai dati esistenti con l'etichetta nel titolo (`ETICHETTA_DATI`, con un test), **prima** di rifare le partite: cambiano solo il titolo e i tempi.

## 9. Tempi

| Passo | Tempo |
| --- | --- |
| Riferimenti MD5 e controlli | 507 s |
| `varianti_uw`, `varianti_rakdos --base-r2` e `--x6` in parallelo | 19,6 minuti |
| UW base_v1, W1, base_v2 contro base_r1 (programma di prova) | 110 s |
| Rigenerazione dei due documenti marcati | 46 s + 31 s |

## 10. Da decidere

| # | Cosa | Proposta |
| --- | --- | --- |
| 1 | Domanda 6 e posizioni test 6-10 (`ricerca.md`, sezioni 5.1 e 9) | In attesa della tua risposta |
| 2 | L'arresto per equivalenza resta al 95% (solo lo scostamento chiede il 99%) | Sì: lì fermarsi presto fa solo giocare la regola |
| 3 | Prossimo passo: interfaccia degli agenti, determinizzazione con i test (a)-(f), esecuzioni riprendibili (`ricerca.md`, passo 1) | Sì |

---

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
