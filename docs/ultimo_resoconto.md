# Ultimo resoconto: fase 3, passo 4 delle metriche, mulligan descrittivo e contestuale (16/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). **Nessun agente cambiato**: i registri della sezione 1 di [metriche.md](metriche.md) sono identici byte per byte. Commento completo nella sezione 18 di [metriche.md](metriche.md); tabelle generate in [risultati_v1_mulligan.md](risultati_v1_mulligan.md).

## In breve

- **Passo 2b chiuso:** le quattro scelte approvate sono scritte nei documenti; la nota sulla lista di simulazione (4 Witch, 3 Ephemerate) è nella sezione 17 di `metriche.md` e in `risultati_v1.md`; il registro ha l'ablazione in D-1 (Witch −0,54, da leggere con cautela per la lista vera), D-10 (Sanctifier +6,25), D-13 (Force of Negation −1,36), D-15 (Consign −1,13).
- **Aggancio del mulligan e `metriche.exe --mulligan-forzato`:** K (tieni) e M (mulligan) per seed. Senza aggancio, o forzando la scelta dell'agente, registri identici; tutti gli MD5 di riferimento invariati.
- **M1, M1-R e M3 riprodotti esattamente:** 11.792 partite cambiate, +9,96 lì, +1,18 sul totale; "solo Witch" 8.699, +7,14; M1-R 3.122, +17,78, +0,56; M3 1.536, +0,38. Le partite in cui la variante cambia la decisione a 7 carte (11.349 per M1) sono **K o M voce per voce**; le 443 cambiate a 6 carte sono contate a parte.
- **Tenere la prima mano da 7 vale +10,5 punti per UW e +8,6 per Rakdos** rispetto al mulligan, per questi agenti.
- **D-2 rifiuta troppe mani:** concorda con il segno di Delta in 21 tipi su 34. Le mani senza risposta con 2-4 terre e una giocata (D-2 ne tiene il 3%) valgono **+12,5** punti tenute; quelle con una terra +11,1.
- **D-2b:** la settima carta sposta Delta fino a ±9 punti ma **quasi mai ne cambia il segno**. Recruiter alza il valore di tenere di +7,5, Phelia lo abbassa di 8,6.
- **Regressione:** ben calibrata sui seed di verifica (decile 1 previsto −11,3 e osservato −10,4; decile 10 +33,7 e +35,3). **La politica "tieni se il Delta previsto è positivo" vince +4,09 [+3,84; +4,33] su seed nuovi**: registrata come variante candidata M-LOG, non adottata.
- **Witch:** il +7,14 è soprattutto il mulligan evitato (a parità di tipo di mano Delta è più alto senza Witch). Le varianti diagnostiche non servono.
- **D-2c:** Recruiter cerca **Sanctifier nel 76%** dei casi e Solitude nel 19%, al turno proprio 5 circa (non al 3); Riddler aggiunge circa 1,9 carte e 0,19 risposte nel turno in cui entra.
- **Rakdos:** il suo mulligan è quasi allineato ai dati (punti persi +0,06, non significativo).

## 1. Cosa ho fatto

| Passo | Cosa | Tempo |
| --- | --- | --- |
| Documenti del passo 2b | Approvazione, lista di simulazione, registro (D-1, D-10, D-13, D-15) | — |
| Test C++ prima del codice | `tests/test_mulligan_forzato.cpp`, 5 test case: senza aggancio; forzare la scelta dell'agente (prima e seconda decisione, due giocatori, 30 seed); scelta opposta; K o M è la partita normale; sonde e partite delle varianti (a 7 carte identiche a K, a 6 carte il seed 258). Con lo stub 4 rossi, poi verdi | — |
| Aggancio e `--mulligan-forzato` | `ForcedMulligan` in `MatchupConfig`; `play_mulligan_pair`; uscita `mulligan.jsonl` (prima mano, scelta dell'agente a 7 e a 6 carte con la regola, K, M, sonde, varianti) | — |
| `mulligan.py gioca` | UW e Rakdos sui seed 1-100.000, UW sui 100.001-200.000; 8 processi | 36,9 + 30,4 + 32,4 s |
| `mulligan.py analizza` | Tutte le tabelle, `docs/risultati_v1_mulligan.md` e i CSV | 30 s |
| Verifiche | Suite C++ in Release (542 test), Python (88), MD5 della sezione 1 e di base_r1 | 13 s + 3 s |

## 2. Controlli

| Controllo | Esito |
| --- | --- |
| Aggancio spento | Test: registri identici. MD5: `metriche.exe` 10.000 `2302d021...`/`472f0196...`; matchup `2302d021...`/`a9f2908d...`, `0ad6744d...`/`9c13ada9...`; base_r1 `81c39eba...`/`2b4dded8...`; goldfish `63648b4c...`/`eb4afb40...`, `3fc4b26d...`: tutti identici |
| Aggancio che forza la scelta dell'agente | Test: registri identici voce per voce (prima e seconda decisione, UW e Rakdos) |
| Varianti a 7 carte | 11.349 (M1), 2.942 (M1-R), 1.436 (M3) partite identiche voce per voce a K o M |
| Riproduzione della sezione 29 di `agente_uw.md` | Esatta (tabella nella sezione 7 qui sotto) |

## 3. Valore di tenere per tipo di mano (UW), con D-2 accanto

Tipo della prima mano da 7: terre (Witch compresa), risposta diretta, giocata entro il turno 2 (D-2), play o draw. Delta = vittoria(K) − vittoria(M) in punti, IC al 95% dal bootstrap sui seed. **D-2 tiene** = quota delle mani del tipo che l'agente tiene. *Corsivo*: meno di 1.000 mani.

| Terre | Risposta | Giocata | Lato | Mani | WR K | WR M | Delta | D-2 tiene | Accordo |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | sì | sì | draw | 1.136 | 19,5 | 24,0 | -4,5 [-7,6; -1,2] | 0% | sì |
| 0 | sì | sì | play | 1.213 | 13,1 | 34,2 | -21,1 [-24,3; -17,8] | 0% | sì |
| 1 | no | sì | draw | 1.105 | 33,2 | 27,8 | +5,4 [+1,8; +9,2] | 0% | **no** |
| 1 | no | sì | play | 1.108 | 42,1 | 33,5 | +8,6 [+4,4; +12,3] | 0% | **no** |
| 1 | sì | sì | draw | 5.872 | 38,3 | 23,9 | +14,4 [+12,8; +15,9] | 24% | **no** |
| 1 | sì | sì | play | 5.788 | 43,4 | 33,7 | +9,7 [+8,0; +11,5] | 24% | **no** |
| 2 | no | sì | draw | 3.195 | 38,9 | 24,9 | +13,9 [+11,9; +16,0] | 0% | **no** |
| 2 | no | sì | play | 3.163 | 55,3 | 33,2 | +22,1 [+19,8; +24,5] | 13% | **no** |
| 2 | sì | no | draw | 252 | 29,4 | 21,0 | *+8,3 [+1,2; +15,4]* | 0% | **no** |
| 2 | sì | no | play | 268 | 27,6 | 34,0 | *-6,3 [-14,2; +1,6]* | 0% | sì |
| 2 | sì | sì | draw | 10.836 | 40,3 | 25,1 | +15,3 [+14,0; +16,4] | 100% | sì |
| 2 | sì | sì | play | 10.853 | 52,6 | 34,0 | +18,6 [+17,4; +19,8] | 100% | sì |
| 3 | no | sì | draw | 4.441 | 33,8 | 25,0 | +8,8 [+7,0; +10,6] | 0% | **no** |
| 3 | no | sì | play | 4.462 | 50,9 | 33,7 | +17,2 [+15,3; +19,2] | 5% | **no** |
| 3 | sì | no | draw | 602 | 25,9 | 22,4 | *+3,5 [-1,1; +7,9]* | 0% | **no** |
| 3 | sì | no | play | 624 | 32,7 | 32,4 | *+0,3 [-4,9; +5,3]* | 0% | **no** |
| 3 | sì | sì | draw | 9.748 | 38,2 | 24,7 | +13,5 [+12,4; +14,7] | 100% | sì |
| 3 | sì | sì | play | 9.844 | 48,9 | 35,2 | +13,8 [+12,4; +15,1] | 100% | sì |
| 4 | no | no | draw | 347 | 19,3 | 26,5 | *-7,2 [-12,4; -1,8]* | 0% | sì |
| 4 | no | no | play | 325 | 30,5 | 35,1 | *-4,6 [-12,0; +2,5]* | 0% | sì |
| 4 | no | sì | draw | 3.422 | 29,4 | 25,5 | +3,9 [+1,9; +5,8] | 0% | **no** |
| 4 | no | sì | play | 3.424 | 44,5 | 35,0 | +9,6 [+7,3; +11,9] | 2% | **no** |
| 4 | sì | no | draw | 762 | 24,4 | 24,7 | *-0,3 [-4,5; +3,9]* | 0% | sì |
| 4 | sì | no | play | 746 | 30,6 | 37,1 | *-6,6 [-11,3; -2,0]* | 0% | sì |
| 4 | sì | sì | draw | 4.364 | 30,7 | 26,1 | +4,6 [+2,9; +6,4] | 100% | sì |
| 4 | sì | sì | play | 4.291 | 43,3 | 34,4 | +9,0 [+7,1; +10,9] | 100% | sì |
| 5+ | no | no | draw | 547 | 13,2 | 27,4 | *-14,3 [-18,9; -9,8]* | 0% | sì |
| 5+ | no | no | play | 557 | 19,4 | 32,7 | *-13,3 [-18,2; -8,3]* | 0% | sì |
| 5+ | no | sì | draw | 1.546 | 21,1 | 23,7 | -2,6 [-5,4; +0,1] | 0% | sì |
| 5+ | no | sì | play | 1.541 | 32,3 | 32,8 | -0,5 [-3,8; +2,7] | 0% | sì |
| 5+ | sì | no | draw | 543 | 20,3 | 27,3 | *-7,0 [-11,8; -2,0]* | 0% | sì |
| 5+ | sì | no | play | 532 | 23,3 | 36,8 | *-13,5 [-18,9; -8,3]* | 0% | sì |
| 5+ | sì | sì | draw | 867 | 23,9 | 26,9 | *-3,0 [-6,6; +0,7]* | 0% | sì |
| 5+ | sì | sì | play | 843 | 34,6 | 34,6 | *+0,0 [-4,2; +4,5]* | 0% | sì |

**Accordo di D-2:** 21 tipi su 34 (61.709 mani su 99.167). **Punti persi:** la politica "tieni se il Delta del tipo è positivo" (segno dai seed 1-50.000, misurata sui 50.001-100.000) vince **+3,76 [+3,43; +4,09]** più di D-2.

**Attenzione:** M è "mulligan, poi decide D-2" (anche a 6 carte). Delta confronta tenere con il mulligan giocato da questo agente, non con il mulligan migliore. Rakdos (tabella completa nel documento generato): accordo in 37 tipi su 50, punti persi +0,06 [−0,14; +0,26].

## 4. La settima carta (D-2b)

Delta con e senza ogni carta, nelle mani al confine; differenza = con − senza.

| Gruppo (mani, D-2 tiene, Delta) | Carta | Mani con | Delta con | Delta senza | Differenza |
| --- | --- | --- | --- | --- | --- |
| Senza risposta, 2-4 terre, con giocata (22.107, 3%, +12,5) | Recruiter | 2.988 | +19,0 [+16,8; +21,2] | +11,5 | **+7,5** [+5,1; +10,0] |
| | Thraben Charm | 2.967 | +13,9 | +12,3 | +1,6 [−0,9; +4,1] |
| | Ephemerate | 8.047 | +12,0 | +12,8 | −0,9 |
| | Starfield Shepherd | 8.582 | +12,2 | +12,7 | −0,6 |
| | Quantum Riddler | 10.154 | +11,6 | +13,3 | −1,7 |
| | Consign to Memory | 8.071 | +9,5 | +14,2 | −4,7 |
| | Witch Enchanter | 9.986 | +9,8 | +14,7 | −5,0 |
| | Force of Negation | 2.992 | +6,7 | +13,4 | −6,7 |
| | Phelia | 11.026 | +8,2 | +16,8 | **−8,6** |
| Senza giocata, 2-4 terre, con risposta (3.254, 0%, −0,7) | Quantum Riddler | 1.926 | +2,6 [−0,3; +5,5] | −5,6 | **+8,3** [+4,0; +12,5] |
| | Recruiter | 611 | *+1,6* | −1,3 | *+2,9* |
| | Witch Enchanter | 1.618 | −2,4 | +0,9 | −3,3 |
| Una terra (13.969, 20%, +11,1) | Ephemerate | 5.892 | +10,9 | +11,3 | −0,4 |
| | Recruiter | 2.236 | +9,4 | +11,4 | −2,0 |
| | Teferi | 2.271 | +6,5 | +12,0 | −5,5 |
| | Force of Negation | 2.263 | +5,2 | +12,2 | −7,0 |
| | Consign to Memory | 5.899 | +5,4 | +15,3 | **−9,9** |

Recruiter vale di più in play (+26,0 con Recruiter nelle mani senza risposta, contro +12,2 in draw). Tabelle complete, in play e in draw, nel documento generato.

## 5. Regressione logistica e politica

Stima sui seed 1-50.000 (K e M, 100.000 righe), errori raggruppati per seed. I coefficienti di "tieni" (logit) con |z| ≥ 2, i più rilevanti:

| Coefficiente | Stima | Errore standard | z |
| --- | --- | --- | --- |
| tieni:creature da un mana | +1,000 | 0,053 | +18,7 |
| tieni:nessuna terra | −1,646 | 0,107 | −15,4 |
| tieni:risposte dirette | +0,623 | 0,046 | +13,6 |
| tieni (costante) | −4,574 | 0,363 | −12,6 |
| tieni:Recruiter | +0,765 | 0,065 | +11,7 |
| tieni:Starfield Shepherd | +0,655 | 0,056 | +11,7 |
| tieni:Quantum Riddler | +0,604 | 0,056 | +10,9 |
| tieni:P(3 terre entro il turno 3) | +1,243 | 0,116 | +10,7 |
| tieni:creature da due mana | +0,460 | 0,047 | +9,9 |
| tieni:Thraben Charm | +0,550 | 0,066 | +8,4 |
| tieni:Ephemerate | +0,483 | 0,059 | +8,2 |
| tieni:in play | +0,179 | 0,028 | +6,4 |
| tieni:Witch Enchanter | −0,256 | 0,040 | −6,4 |
| tieni:Consign to Memory | +0,361 | 0,057 | +6,3 |
| tieni:terre (numero) | +0,345 | 0,060 | +5,7 |
| tieni:Solitude ed Ephemerate | +0,260 | 0,055 | +4,8 |
| tieni:Force of Negation | +0,289 | 0,066 | +4,4 |
| tieni:carte bianche oltre a Solitude | +0,087 | 0,033 | +2,6 |

**Validazione** (seed 50.001-100.000): Delta medio previsto +10,39, osservato +10,63.

| Decile del previsto | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Previsto | −11,3 | −3,3 | +1,1 | +4,8 | +8,2 | +11,7 | +15,3 | +19,3 | +24,6 | +33,7 |
| Osservato | −10,4 | −2,5 | +2,4 | +5,2 | +8,4 | +11,9 | +14,0 | +20,0 | +22,1 | +35,3 |

**Politica "tieni se il Delta previsto è positivo"** (seed nuovi 100.001-200.000, contro D-2): **+4,09 [+3,84; +4,33]** punti (WR 41,34% contro 37,25%). Tiene il 77,5% delle prime mani (D-2 il 53,4%); decide diversamente in 35.657 seed (29.883 mani tenute in più, 5.774 rifiutate in più), dove vale +11,48. **Registrata come M-LOG, variante candidata, non adottata.**

## 6. Witch Enchanter (9.5)

Le mani "solo Witch" a 7 carte (M1 le tiene, D-2 no): 8.407, K − M **+7,6 [+6,3; +9,0]**.

**(a) Il mulligan evitato.** Mani dello stesso tipo (una giocata, nessuna risposta diretta), con e senza Witch:

| Terre (con Witch) | Witch | Mani | Delta | D-2 tiene |
| --- | --- | --- | --- | --- |
| 2 | no | 4.303 | +20,0 [+18,1; +21,9] | 6% |
| 2 | sì | 2.055 | +13,7 [+11,0; +16,6] | 7% |
| 3 | no | 4.852 | +15,6 [+13,7; +17,3] | 3% |
| 3 | sì | 4.051 | +9,9 [+8,1; +11,9] | 3% |
| 4 | no | 2.966 | +5,7 [+3,3; +7,9] | 1% |
| 4 | sì | 3.880 | +7,5 [+5,5; +9,5] | 1% |

**(b) e (c)** Nelle partite K di quelle 8.407 mani:

| Uso di Witch | Partite | WR di UW |
| --- | --- | --- |
| Giocata come terra | 3.512 (41,8%) | 46,5 [44,9; 48,2] |
| Lanciata | 1.980 (23,6%) | 54,2 [52,0; 56,4] |
| Esiliata per l'evocazione di Solitude | 1.123 (13,4%) | 53,0 [50,1; 55,9] |
| In mano alla fine | 3.813 (45,4%) | 21,3 [20,1; 22,7] |
| Solitude arriva dopo (pescata o cercata) | 3.415 (40,6%) | 53,1 [51,5; 54,8] |

**Lettura:** tenere una mano senza risposta conviene già senza Witch, e di più (a 2-3 terre una Witch al posto di una terra vera abbassa Delta di circa 6 punti). Il +7,14 di M1 è soprattutto **(a) il mulligan evitato**; (b) e (c) sono contributi. **Proposta: nessuna variante diagnostica** (decisione 11: la 9.3 basta).

## 7. D-2 contro M1, M1-R e M3 (9.6)

| Variante | Cambiate (a 7 / a 6) | Identiche a K o M | Delta lì | Pubblicato | Guadagno sul totale | Pubblicato |
| --- | --- | --- | --- | --- | --- | --- |
| M1 | 11.792 (11.349 / 443) | 11.349 su 11.349 | +9,96 [+8,82; +11,10] | 11.792, +9,96 | +1,18 | +1,18 |
| M1-R | 3.122 (2.942 / 180) | 2.942 su 2.942 | +17,78 [+15,57; +19,98] | 3.122, +17,78 | +0,56 | +0,56 |
| M3 | 1.536 (1.436 / 100) | 1.436 su 1.436 | +24,54 [+21,28; +27,80] | 1.536, +24,54 | +0,38 | +0,38 |

| Gruppo di M1 | Cambiate | Delta | Pubblicato | A 7 carte: cambiate, K − M | A 6 carte |
| --- | --- | --- | --- | --- | --- |
| Entrambe | 1.363 | +15,92 | 1.364, +15,98 | 1.282, +16,15 | 81 |
| Solo Recruiter | 1.730 | +19,48 | 1.729, +19,43 | 1.660, +20,18 | 70 |
| Solo Witch | 8.699 | +7,14 | 8.699, +7,14 | 8.407, +7,65 | 292 |

Una partita su 11.792 cade in un gruppo diverso: qui il gruppo viene dalle carte nella mano tenuta, nella sezione 29 dalla carta che fa scattare il cambio. I totali coincidono.

**Accordo e punti persi:** D-2 sceglie il segno giusto di Delta in 21 tipi su 34; M1, M1-R e M3 recuperano 1,18, 0,56 e 0,38 punti; la politica per tipo 3,76, la regressione 4,09. Le varianti allargano D-2 nella direzione giusta ma ne prendono solo una parte.

## 8. D-2c: Recruiter e Riddler

Partite normali dei seed 1-100.000.

| Recruiter cerca | Partite | Turno proprio del lancio (media) | Usata | Turni dal lancio all'uso (media) | WR di UW |
| --- | --- | --- | --- | --- | --- |
| Sanctifier en-Vec | 11.042 (76,3%) | 4,92 | 74,8% | 0,75 | 56,3 [55,4; 57,3] |
| Solitude | 2.687 (18,6%) | 5,70 | 62,0% | 0,31 | 61,1 [59,3; 62,9] |
| Guide of Souls | 326 | 5,77 | 96,3% | 0,26 | *77,9* |
| Witch Enchanter | 240 | 4,03 | 28,3% | 1,91 | *65,4* |
| Ocelot Pride | 112 | 7,03 | 97,3% | 0,11 | — |
| Phelia | 66 | 8,58 | 25,8% | 0,47 | — |

Solitude cercata: usata nello stesso turno nel 48,2%, al turno dopo nel 9,8%, mai nel 38,0%.

**Quantum Riddler** (37.905 partite, entra al turno proprio 4,8 in media, WR 56,1%): nel turno in cui entra UW pesca **3,01 carte e 0,355 risposte dirette**, contro 1,07 e 0,167 nei turni senza Riddler; almeno una risposta pescata nel 31,7% delle partite.

**Lettura per D-2c:** Recruiter e Riddler sono risposte indirette nel senso che alzano il valore di tenere (Recruiter +7,5 nelle mani senza risposta, Riddler +8,3 in quelle senza giocata; coefficienti +0,77 e +0,60). Ma Recruiter arriva al turno 5 circa e cerca soprattutto Sanctifier: "Recruiter al turno 3 trova Solitude" non è quello che fa questo agente.

## 9. Mulligan descrittivo (sezione 8)

| | 7 carte | 6 carte | 5 carte |
| --- | --- | --- | --- |
| UW in play | 27.120: 50,1 [49,5; 50,7] | 19.053: 36,6 [35,9; 37,3] | 3.827: 21,1 [19,9; 22,5] |
| UW in draw | 26.374: 38,0 [37,4; 38,6] | 19.625: 26,5 [25,9; 27,1] | 4.001: 16,9 [15,8; 18,1] |
| Rakdos in play | 38.933: 71,8 [71,4; 72,2] | 9.378: 58,2 [57,2; 59,2] | 1.689: 41,1 [38,8; 43,5] |
| Rakdos in draw | 39.098: 59,7 [59,3; 60,2] | 9.183: 50,5 [49,5; 51,6] | 1.719: 37,1 [34,8; 39,4] |

Nessuno tiene meno di 5 carte. Tipi di mano tenuta nel documento generato.

## 10. Decisioni da approvare

| # | Proposta | Perché |
| --- | --- | --- |
| 1 | **L'agente sceglie anche quando la decisione è forzata** (poi la scelta si sostituisce) | Il suo stato resta quello della partita normale: "forzare la scelta dell'agente" dà la stessa partita voce per voce |
| 2 | **Caratteristiche della regressione**: le terre come probabilità di 3 entro il turno 3, più il numero e i casi 0 e 5+; le carte per gruppo più le singole | Con un indicatore per ogni numero di terre la probabilità sarebbe stata quasi collineare |
| 3 | **Mani al confine per la settima carta**: senza risposta, senza giocata, una terra | I gruppi in cui D-2 rifiuta per un solo motivo |
| 4 | **La politica usa il modello dei seed 1-50.000** (quello validato) | Stima, verifica e politica su tre insiemi di seed separati |
| 5 | **Un documento generato separato**, `risultati_v1_mulligan.md` | Un generatore per documento; `metriche_carte.py` non dipende dalle partite K e M |
| 6 | **statsmodels 0.15.0** fissato in tutti i blocchi PEP 723 | La regressione con gli errori raggruppati per seed |
| 7 | **Nessuna variante diagnostica di Witch** | La 9.3 separa già le tre spiegazioni |

## 11. Tempi

| Passo | Tempo |
| --- | --- |
| `--mulligan-forzato uw`, seed 1-100.000, 8 processi, con il record di K e M | 36,9 s |
| `--mulligan-forzato rakdos`, seed 1-100.000 | 30,4 s |
| `--mulligan-forzato uw`, seed 100.001-200.000 | 32,4 s |
| `mulligan.py analizza` | 30 s |
| `metriche_carte.py` (rigenerato per la nota della lista di simulazione) | 43 s |
| Suite C++ in Release (542 test) / Python (88) | 13 s / 3 s |

La stima della sezione 10 era circa 9 minuti per 400.000 partite su un core: in 8 processi le 600.000 partite (più le sonde e le partite delle varianti) richiedono 100 s.

---

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
