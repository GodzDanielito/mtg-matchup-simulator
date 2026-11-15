# Ultimo resoconto: base_r3, M1 non adottata, M1-R e M3, Witch Enchanter come terra (15/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). Dettagli e regole: [agente_rakdos.md](agente_rakdos.md), sezione 24; [agente_uw.md](agente_uw.md), sezione 29.

## In breve

- **Rakdos base_r3** (X6 predefinita: al turno 1 la creatura da un mana prima del ciclo del Troll): **62,74%** contro UW base_v2, +0,38 [+0,27; +0,50] rispetto a base_r2. Robusta contro tre versioni di UW. Goldfish entro le soglie, un po' più veloce.
- **M1 non adottata.** La divergenza con la misura è registrata: M1 vale +1,18 [+1,04; +1,31] contro base_r3.
- **M1-R** (solo Recruiter) **+0,56 [+0,48; +0,63]**; **M3** (Recruiter solo in play) **+0,38 [+0,32; +0,43]**. Nessuna adottata.
- **Scomposizione di M1:** circa metà del guadagno viene da Recruiter e metà da Witch Enchanter.
- **Witch come terra:** giocata nel 28,7% delle partite, 72% tappata. Il 19,6% delle giocate non soddisfa D-1 come scritta nel documento: sono tutte giocate per una carta che non è una giocata prevista.
- Tutti i registri di riferimento con le opzioni nuove spente sono identici byte per byte. Suite Release 517 su 517, CI verde.

## 0. Commit del passo X6

`e8506ab`, push fatto, CI verde (Release su Windows).

## 1. base_r3

Il riferimento di base_r2 è stato salvato prima di toccare l'agente. Dopo il cambio, con `--rakdos-r2` (nuovo in `matchup.exe` e `goldfish.exe`) e con `--rakdos-r1`, i registri sono identici byte per byte:

| Registro | base_r2 (`--rakdos-r2`) | base_r1 (`--rakdos-r1`) |
| --- | --- | --- |
| Matchup `partite.jsonl` | `b25af1f5...` ✔ | `81c39eba...` ✔ |
| Matchup `replay.jsonl` | `32a56483...` ✔ | `2b4dded8...` ✔ |
| Goldfish 10.000 `partite` | `4433a3a0...` ✔ | `29a9db59...` ✔ |
| Goldfish 10.000 `replay` | `4c317586...` ✔ | `43d53744...` ✔ |

I nuovi riferimenti di base_r3 sono il matchup `2302d021...` / `a9f2908d...` e il goldfish da 100.000 `3fc4b26d...`.

**Goldfish da 100.000 partite, base_r3 contro base_r2** (stessi seed; 9.970 partite diverse)

| | Play base_r2 | Play base_r3 | Draw base_r2 | Draw base_r3 |
| --- | --- | --- | --- | --- |
| Turno della vittoria: media, mediana | 4,856, 5 | 4,832, 5 | 4,660, 5 | 4,635, 5 |
| Vinte entro T4 / T5 | 35,90 / 83,33% | 37,38 / 84,13% | 46,32 / 89,92% | 48,12 / 90,56% |
| Turno del delirium: mediana | 2 | 2 | 2 | 2 |
| Delirium entro T2 / T3 / T4 | 53,86 / 72,38 / 83,06% | 53,95 / 72,26 / 83,03% | 60,93 / 78,94 / 88,33% | 60,87 / 78,79 / 88,25% |
| Delirium mai | 4.804 | 4.870 | 3.513 | 3.617 |

- Il turno della vittoria scende di −0,024 turni sia in play sia in draw (intervalli stretti, significativo).
- Le partite vinte entro il turno 4 salgono di +1,48 punti in play e +1,80 in draw.
- **Soglie rispettate:** la mediana della vittoria è 5 (allarme sotto 4 o sopra 7), quella del delirium 2 (atteso tra 2 e 4).

## 2. Mutazione di X6

| Mutazione | `TEST_CASE` falliti |
| --- | --- |
| **X6 non scatta mai** (quella chiesta) | **4**: Supplier, Manipulator con Thoughtseize, secondo turno, seed 13 e 30. Senza la guardia dei seed sono 3 |
| X6 a ogni turno (confine) | 1 (secondo turno) |
| X6 con qualsiasi numero di terre (confine) | 1 (due terre in campo) |

Il codice dell'agente è stato ripristinato byte per byte e Release ricompilato. Nelle partite vere, al turno 1 la condizione "una sola terra in campo" è sempre vera: solo una partita preparata la distingue.

## 3. Robustezza di X6

Win rate di Rakdos e differenza appaiata base_r3 − base_r2; 100.000 partite per riga.

| UW | base_r2 | base_r3 | Diff. in play | Diff. in draw | **Diff. totale** | Partite in cui scatta | Diff. lì |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base_v1 | 66,23% | 66,64% | +0,30 [+0,15; +0,46] | +0,52 [+0,36; +0,68] | **+0,41 [+0,30; +0,52]** | 9.758 | +4,21 [+3,07; +5,35] |
| base_v2 | 62,36% | 62,74% | +0,32 [+0,16; +0,48] | +0,45 [+0,29; +0,61] | **+0,38 [+0,27; +0,50]** | 9.758 | +3,92 [+2,76; +5,09] |
| base_v2 + M1 | 61,13% | 61,57% | +0,41 [+0,25; +0,57] | +0,46 [+0,30; +0,63] | **+0,44 [+0,32; +0,55]** | 9.742 | +4,50 [+3,32; +5,67] |

X6 ha lo stesso segno e la stessa grandezza contro tutte e tre le versioni di UW.

## 4. M1-R e M3

Win rate di UW contro Rakdos base_r3, differenza appaiata rispetto a base_v2.

| | In play | In draw | Totale | Diff. in play | Diff. in draw | **Diff. totale** | Mani 7 / 6 / 5 | Sconfitte con 3+ carte (play / draw) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base_v2 | 42,72% | 31,79% | 37,26% | | | | 53,49 / 38,68 / 7,83% | 41,17% (32,22 / 48,69%) |
| + M1 | 44,17% | 32,70% | 38,43% | +1,44 [+1,24; +1,64] | +0,91 [+0,72; +1,09] | **+1,18 [+1,04; +1,31]** | 64,84 / 29,64 / 5,52% | 44,48% (35,03 / 52,31%) |
| + M1-R | 43,48% | 32,15% | 37,81% | +0,75 [+0,65; +0,86] | +0,36 [+0,26; +0,45] | **+0,56 [+0,48; +0,63]** | 56,44 / 36,38 / 7,18% | 41,94% (32,67 / 49,67%) |
| + M3 | 43,48% | 31,79% | 37,63% | +0,75 [+0,65; +0,86] | +0,00 | **+0,38 [+0,32; +0,43]** | 54,93 / 37,56 / 7,51% | 41,43% (32,67 / 48,69%) |

Le partite in cui il mulligan cambia:
- **M1:** 11.792 partite, +9,96 lì.
- **M1-R:** 3.122 partite, +17,78 lì. In play 1.536 partite (+24,54), in draw 1.586 (+11,22).
- **M3:** solo le 1.536 partite in play.

## 5. Scomposizione di M1

Le partite in cui M1 tiene una mano che base_v2 rifiuta. Win rate di UW con M1 e con base_v2 sugli stessi seed.

| Carta | UW | Partite | Con M1 | Con base_v2 | Differenza |
| --- | --- | --- | --- | --- | --- |
| Solo Recruiter | in play | 833 | 60,26% | 34,45% | +25,81 [+21,43; +30,19] |
| | in draw | 896 | 37,61% | 24,11% | +13,50 [+9,59; +17,42] |
| Solo Witch | in play | 4.245 | 42,03% | 33,92% | +8,10 [+6,12; +10,09] |
| | in draw | 4.454 | 30,35% | 24,14% | +6,22 [+4,46; +7,98] |
| Entrambe | in play | 690 | 55,36% | 31,74% | +23,62 [+18,68; +28,57] |
| | in draw | 674 | 34,12% | 25,96% | +8,16 [+3,59; +12,73] |

In totale: solo Recruiter +19,43 su 1.729 partite, solo Witch +7,14 su 8.699, entrambe +15,98 su 1.364. In partite vinte in più sono circa 340, 620 e 220, cioè quasi metà Recruiter e metà Witch. Recruiter vale più in play, come dice il pilota, ma anche in draw la mano tenuta vince più del mulligan.

## 6. Witch Enchanter come terra (base_v2, l'agente non cambia)

| | UW in play | UW in draw | Totale |
| --- | --- | --- | --- |
| Partite con Witch-Blessed Meadow | 15.451 (30,90%) | 13.270 (26,54%) | 28.721 (28,72%) |
| Giocate | 17.830 | 15.264 | 33.094 |
| Pagate 3 vita | 5.143 (28,84%) | 4.004 (26,23%) | 9.147 (27,64%) |
| Tappata | 12.687 (71,16%) | 11.260 (73,77%) | 23.947 (72,36%) |
| Vita pagata per le terre, per partita | 3,28 | 3,26 | 3,27 |
| ... nelle partite con Witch-Blessed Meadow | 3,58 | 3,59 | 3,58 |
| Giocate che non soddisfano D-1 | 3.740 (20,98%) | 2.752 (18,03%) | 6.492 (19,62%) |
| Pagamenti che non soddisfano D-1 | 11 (0,21%) | 17 (0,42%) | 28 (0,31%) |
| Vita sotto 10 dopo il pagamento | 132 | 582 | 714 |

**Da dove viene il 19,6%.** Tutte queste giocate mancano la condizione (b): una giocata prevista con valore di mana tra L + 1 e L + 2. L'agente decide invece con "qualsiasi carta non terra" in quell'intervallo, quindi conta anche le istantanee e le creature con lampo.

**Replay leggibili** in `data/matchup/witch_terra/`: seed 38, 406 e 574, tutti con UW in draw e vinti da Rakdos. Nel seed 38 UW paga i 3 punti al suo terzo turno (da 12 a 9) e il turno dopo muore con esattamente 12 danni.

## 7 e 8. README e CLAUDE.md

- **README:** fase 2 completata e fase 3 prossima; 41 carte; tolta la frase "Non esistono ancora le altre carte del pool".
- **CLAUDE.md:** nuova regola sul resoconto completo in questo file a ogni passo; aggiornati i comandi (`--rakdos-r2`, `--x6-robustezza`, le tre modalità nuove di `varianti_uw`).

## Errori del passo

- **Un MD5 di base_r3 preso da un registro troncato:** `Select-Object -First` aveva fermato `matchup.exe` a 9.999 righe. Corretto prima del commit con due esecuzioni complete.
- **Una modifica di C++ fatta dalla shell**, contro la regola del diario. Completata con lo strumento di modifica.

Nessun errore di regole: i testi Oracle citati sono stati letti dal bulk Scryfall del 9/11/2026.

## Da decidere

Non è stato adottato niente. Restano aperti:
- **M1-R o M3**, contro la lettura del pilota su Recruiter;
- se correggere l'approssimazione di D-1 nell'agente (le istantanee contate come giocate previste).
