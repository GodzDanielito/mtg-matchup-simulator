# Mulligan degli euristici v1 (passo 4)

Generato da `python/metriche/mulligan.py analizza` (sezioni 8 e 9 di [metriche.md](metriche.md)). 100.000 seed (1-100.000) per UW e per Rakdos, due partite per seed: **K** con la prima decisione di mulligan del giocatore forzata a "tieni", **M** a "mulligan" (`metriche.exe --mulligan-forzato`); il resto lo decidono gli euristici v1 (UW base_v2, Rakdos base_r3). **Delta** = vittoria(K) − vittoria(M): il valore di tenere la prima mano da 7 **per questi agenti**, in punti. Intervalli al 95%: bootstrap sui seed (B = 2.000, seed 20261116) e Wilson per le proporzioni. *Corsivo* sotto 1.000 mani, "—" sotto 200. I seed 100.001-200.000 servono solo alla politica della regressione.

- **UW:** l'agente tiene la prima mano da 7 nel 53,49% dei seed; K vince il 40,08%, M il 29,61%; Delta medio +10,5 [+10,1; +10,9] (in play +11,3 [+10,7; +11,9], in draw +9,6 [+9,1; +10,2]).
- **Rakdos:** l'agente tiene la prima mano da 7 nel 78,03% dei seed; K vince il 60,92%, M il 52,34%; Delta medio +8,6 [+8,2; +9,0] (in play +9,5 [+8,9; +10,1], in draw +7,7 [+7,1; +8,2]).

## Mulligan descrittivo (sezione 8)

Le partite normali dei seed 1-100.000 (gli stessi registri della sezione 1), per carte nella mano tenuta: WR del giocatore con Wilson. È una descrizione: una mano da 6 è anche "la mano da 7 era brutta".

|  | 7 carte | 6 carte | 5 carte | 4 o meno |
| --- | --- | --- | --- | --- |
| UW in play | 27.120: 50,1 [49,5; 50,7] | 19.053: 36,6 [35,9; 37,3] | 3.827: 21,1 [19,9; 22,5] | — |
| UW in draw | 26.374: 38,0 [37,4; 38,6] | 19.625: 26,5 [25,9; 27,1] | 4.001: 16,9 [15,8; 18,1] | — |

|  | 7 carte | 6 carte | 5 carte | 4 o meno |
| --- | --- | --- | --- | --- |
| Rakdos in play | 38.933: 71,8 [71,4; 72,2] | 9.378: 58,2 [57,2; 59,2] | 1.689: 41,1 [38,8; 43,5] | — |
| Rakdos in draw | 39.098: 59,7 [59,3; 60,2] | 9.183: 50,5 [49,5; 51,6] | 1.719: 37,1 [34,8; 39,4] | — |

Per tipo di mano tenuta (dopo le carte in fondo). UW: terre (Witch compresa) / risposta diretta / giocata entro il turno 2; Rakdos: terre / creatura da un mana / carte che cercano. Solo i tipi con almeno 1.000 partite.

| Tipo | Partite | WR |
| --- | --- | --- |
| 3 / sì / sì | 33.096 | 38,0 [37,4; 38,5] |
| 2 / sì / sì | 32.868 | 42,1 [41,6; 42,7] |
| 4 / sì / sì | 9.416 | 35,8 [34,9; 36,8] |
| 3 / no / sì | 8.985 | 29,9 [29,0; 30,9] |
| 1 / sì / sì | 4.821 | 36,7 [35,4; 38,1] |
| 2 / no / sì | 4.349 | 39,9 [38,4; 41,4] |
| 3 / sì / no | 2.969 | 19,0 [17,6; 20,4] |
| 4 / no / sì | 1.351 | 26,6 [24,3; 29,0] |

| Tipo | Partite | WR |
| --- | --- | --- |
| 2 / sì / 0 | 15.575 | 64,5 [63,7; 65,2] |
| 2 / sì / 1 | 15.134 | 67,5 [66,8; 68,3] |
| 1 / sì / 1 | 13.229 | 62,5 [61,6; 63,3] |
| 3 / sì / 0 | 12.131 | 61,7 [60,8; 62,6] |
| 1 / sì / 2+ | 10.954 | 64,0 [63,1; 64,9] |
| 3 / sì / 1 | 8.906 | 63,9 [62,9; 64,9] |
| 2 / sì / 2+ | 7.716 | 67,8 [66,8; 68,9] |
| 4 / sì / 0 | 3.086 | 56,4 [54,6; 58,1] |
| 3 / sì / 2+ | 2.793 | 60,2 [58,3; 62,0] |
| 4 / sì / 1 | 2.229 | 54,2 [52,2; 56,3] |
| 3 / no / 0 | 1.434 | 44,6 [42,0; 47,1] |
| 3 / no / 1 | 1.313 | 52,9 [50,2; 55,5] |
| 2 / no / 1 | 1.110 | 59,6 [56,7; 62,5] |
| 2 / no / 2+ | 1.034 | 63,0 [60,0; 65,8] |


## Valore di tenere per tipo di mano (sezione 9.3)

Tipo della **prima mano da 7** (decisa prima: terre con Witch, risposta diretta, giocata entro il turno 2 di D-2, play o draw). **D-2 tiene**: quota delle mani di quel tipo che l'agente tiene. Accordo: D-2 tiene la maggioranza dove Delta > 0 e la rifiuta dove Delta < 0. La riga si mostra da 200 mani. **Attenzione:** M è "mulligan, poi decide D-2" (anche a 6 carte): Delta confronta tenere con il mulligan *giocato da questo agente*, non con il mulligan migliore.

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

**Accordo di D-2:** 21 tipi su 34 (61.709 mani su 99.167). **Punti persi da D-2** (politica "tieni se il Delta del tipo è positivo", segno stimato sui seed 1-50.000, misurata sui seed 50.001-100.000 contro D-2): +3,76 [+3,43; +4,09] punti, 17.439 decisioni diverse su 50.000.


### Rakdos

| Terre | Creatura da 1 | Cercano | Lato | Mani | WR K | WR M | Delta | Agente tiene | Accordo |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | sì | 0 | draw | 676 | 23,4 | 50,6 | *-27,2 [-31,8; -22,5]* | 0% | sì |
| 0 | sì | 0 | play | 720 | 16,4 | 55,8 | *-39,4 [-43,9; -35,0]* | 0% | sì |
| 0 | sì | 1 | draw | 1.535 | 33,2 | 46,9 | -13,7 [-16,8; -10,4] | 0% | sì |
| 0 | sì | 1 | play | 1.534 | 28,1 | 56,1 | -28,0 [-31,1; -24,6] | 0% | sì |
| 0 | sì | 2+ | draw | 1.820 | 38,4 | 48,6 | -10,2 [-13,2; -7,1] | 0% | sì |
| 0 | sì | 2+ | play | 1.756 | 40,9 | 55,4 | -14,5 [-17,8; -11,5] | 0% | sì |
| 1 | no | 1 | draw | 209 | 53,6 | 50,2 | *+3,3 [-5,6; +12,6]* | 0% | **no** |
| 1 | no | 1 | play | 215 | 49,8 | 57,7 | *-7,9 [-17,5; +1,5]* | 0% | sì |
| 1 | no | 2+ | draw | 511 | 49,5 | 49,9 | *-0,4 [-6,3; +5,8]* | 0% | sì |
| 1 | no | 2+ | play | 552 | 60,3 | 55,4 | *+4,9 [-0,9; +10,3]* | 0% | **no** |
| 1 | sì | 0 | draw | 2.813 | 53,1 | 48,4 | +4,7 [+2,3; +7,2] | 0% | **no** |
| 1 | sì | 0 | play | 2.941 | 58,9 | 54,7 | +4,1 [+1,7; +6,5] | 0% | **no** |
| 1 | sì | 1 | draw | 5.363 | 58,5 | 49,5 | +9,0 [+7,2; +10,8] | 100% | sì |
| 1 | sì | 1 | play | 5.193 | 69,9 | 56,5 | +13,3 [+11,7; +15,1] | 100% | sì |
| 1 | sì | 2+ | draw | 4.336 | 59,6 | 46,9 | +12,7 [+10,9; +14,6] | 100% | sì |
| 1 | sì | 2+ | play | 4.317 | 72,4 | 55,5 | +16,9 [+14,9; +18,8] | 100% | sì |
| 2 | no | 1 | draw | 614 | 56,0 | 49,2 | *+6,8 [+1,5; +12,1]* | 67% | sì |
| 2 | no | 1 | play | 605 | 62,3 | 56,2 | *+6,1 [+0,8; +11,2]* | 65% | sì |
| 2 | no | 2+ | draw | 964 | 54,5 | 50,3 | *+4,1 [+0,2; +8,3]* | 49% | **no** |
| 2 | no | 2+ | play | 917 | 61,1 | 55,6 | *+5,5 [+1,1; +9,7]* | 46% | **no** |
| 2 | sì | 0 | draw | 4.783 | 62,9 | 48,3 | +14,7 [+12,8; +16,4] | 100% | sì |
| 2 | sì | 0 | play | 4.829 | 75,2 | 57,2 | +18,0 [+16,3; +19,9] | 100% | sì |
| 2 | sì | 1 | draw | 6.826 | 62,1 | 47,5 | +14,6 [+13,1; +16,2] | 100% | sì |
| 2 | sì | 1 | play | 6.844 | 75,1 | 56,5 | +18,6 [+17,1; +20,1] | 100% | sì |
| 2 | sì | 2+ | draw | 3.687 | 61,3 | 47,6 | +13,7 [+11,6; +15,7] | 100% | sì |
| 2 | sì | 2+ | play | 3.771 | 75,0 | 56,9 | +18,1 [+16,1; +20,0] | 100% | sì |
| 3 | no | 0 | draw | 367 | 49,9 | 49,9 | *+0,0 [-6,5; +6,4]* | 65% | **no** |
| 3 | no | 0 | play | 357 | 57,1 | 57,4 | *-0,3 [-7,2; +6,6]* | 62% | **no** |
| 3 | no | 1 | draw | 782 | 49,7 | 48,8 | *+0,9 [-4,0; +5,8]* | 55% | sì |
| 3 | no | 1 | play | 767 | 59,6 | 55,9 | *+3,7 [-1,3; +8,4]* | 54% | sì |
| 3 | no | 2+ | draw | 679 | 47,9 | 46,2 | *+1,6 [-3,5; +6,4]* | 33% | **no** |
| 3 | no | 2+ | play | 744 | 56,9 | 55,6 | *+1,2 [-3,4; +6,1]* | 36% | **no** |
| 3 | sì | 0 | draw | 3.769 | 63,5 | 49,0 | +14,5 [+12,3; +16,5] | 100% | sì |
| 3 | sì | 0 | play | 3.778 | 74,1 | 56,7 | +17,3 [+15,3; +19,4] | 100% | sì |
| 3 | sì | 1 | draw | 3.959 | 59,9 | 47,9 | +11,9 [+10,0; +13,9] | 100% | sì |
| 3 | sì | 1 | play | 3.942 | 71,2 | 57,2 | +14,1 [+12,0; +16,0] | 100% | sì |
| 3 | sì | 2+ | draw | 1.322 | 54,8 | 47,4 | +7,5 [+4,1; +11,1] | 100% | sì |
| 3 | sì | 2+ | play | 1.336 | 66,9 | 58,4 | +8,5 [+5,0; +12,1] | 100% | sì |
| 4 | no | 0 | draw | 318 | 40,3 | 46,2 | *-6,0 [-13,1; +0,7]* | 54% | **no** |
| 4 | no | 0 | play | 309 | 45,0 | 53,1 | *-8,1 [-15,2; -0,6]* | 55% | **no** |
| 4 | no | 1 | draw | 464 | 39,0 | 48,1 | *-9,1 [-15,4; -3,0]* | 37% | sì |
| 4 | no | 1 | play | 479 | 46,3 | 56,4 | *-10,0 [-15,9; -4,2]* | 35% | sì |
| 4 | no | 2+ | draw | 246 | 42,7 | 42,3 | *+0,4 [-7,9; +8,9]* | 22% | **no** |
| 4 | no | 2+ | play | 210 | 52,4 | 55,7 | *-3,3 [-12,1; +5,9]* | 20% | sì |
| 4 | sì | 0 | draw | 1.490 | 52,1 | 51,1 | +1,0 [-2,4; +4,4] | 100% | sì |
| 4 | sì | 0 | play | 1.463 | 62,5 | 56,4 | +6,2 [+2,6; +9,5] | 100% | sì |
| 4 | sì | 1 | draw | 1.079 | 48,3 | 47,8 | +0,5 [-3,3; +4,3] | 100% | sì |
| 4 | sì | 1 | play | 1.100 | 60,5 | 57,2 | +3,4 [-0,4; +7,2] | 100% | sì |
| 5+ | sì | 0 | draw | 332 | 40,7 | 50,3 | *-9,6 [-16,6; -2,8]* | 0% | sì |
| 5+ | sì | 0 | play | 336 | 46,4 | 58,9 | *-12,5 [-19,8; -5,3]* | 0% | sì |

**Accordo dell'agente di Rakdos:** 37 tipi su 50 (86.543 mani su 97.959); punti persi +0,06 [-0,14; +0,26].


## La settima carta (D-2b)

Nelle mani al confine di D-2, Delta con e senza ogni carta singola (decisione 8). **Differenza** = Delta con − Delta senza (IC normale tra gruppi indipendenti). Una carta che porta Delta da negativo a positivo dà ragione a D-2b per quella carta.


**senza risposta (2-4 terre, con giocata)**: 22.107 mani, D-2 ne tiene il 3%, Delta del gruppo +12,5 punti.

| Carta | Mani con | Delta con | Delta senza | Differenza | Delta con, in play | Delta con, in draw |
| --- | --- | --- | --- | --- | --- | --- |
| recruiter | 2.988 | +19,0 [+16,8; +21,2] | +11,5 | +7,5 [+5,1; +10,0] | +26,0 | +12,2 |
| witch | 9.986 | +9,8 [+8,5; +11,1] | +14,7 | -5,0 [-6,6; -3,3] | +12,6 | +6,9 |
| phelia | 11.026 | +8,2 [+7,0; +9,5] | +16,8 | -8,6 [-10,3; -6,9] | +12,0 | +4,5 |
| ephemerate | 8.047 | +12,0 [+10,6; +13,4] | +12,8 | -0,9 [-2,6; +0,9] | +14,4 | +9,5 |
| riddler | 10.154 | +11,6 [+10,3; +12,8] | +13,3 | -1,7 [-3,4; -0,0] | +14,7 | +8,4 |
| consign | 8.071 | +9,5 [+8,1; +10,9] | +14,2 | -4,7 [-6,5; -3,0] | +12,1 | +6,9 |
| force | 2.992 | +6,7 [+4,3; +9,0] | +13,4 | -6,7 [-9,1; -4,2] | +8,1 | +5,3 |
| teferi | 0 | — | +12,5 | — | — | — |
| shepherd | 8.582 | +12,2 [+10,9; +13,5] | +12,7 | -0,6 [-2,3; +1,2] | +15,7 | +8,7 |
| thraben | 2.967 | +13,9 [+11,6; +16,1] | +12,3 | +1,6 [-0,9; +4,1] | +16,3 | +11,4 |

**senza giocata (2-4 terre, con risposta)**: 3.254 mani, D-2 ne tiene il 0%, Delta del gruppo -0,7 punti.

| Carta | Mani con | Delta con | Delta senza | Differenza | Delta con, in play | Delta con, in draw |
| --- | --- | --- | --- | --- | --- | --- |
| recruiter | 611 | *+1,6 [-3,1; +6,4]* | -1,3 | *+2,9 [-2,5; +8,3]* | *+2,6* | *+0,7* |
| witch | 1.618 | -2,4 [-5,3; +0,5] | +0,9 | -3,3 [-7,5; +0,9] | *-8,5* | *+3,5* |
| phelia | 0 | — | -0,7 | — | — | — |
| ephemerate | 666 | *-2,0 [-6,5; +2,6]* | -0,4 | *-1,5 [-6,6; +3,6]* | *-4,5* | *+1,0* |
| riddler | 1.926 | +2,6 [-0,3; +5,5] | -5,6 | +8,3 [+4,0; +12,5] | *+1,5* | *+3,8* |
| consign | 1.581 | -1,1 [-4,2; +1,9] | -0,4 | -0,7 [-4,9; +3,6] | *-6,5* | *+4,5* |
| force | 622 | *-2,4 [-7,1; +2,4]* | -0,3 | *-2,1 [-7,4; +3,3]* | *-7,4* | *+2,6* |
| teferi | 885 | *-0,1 [-4,2; +4,0]* | -1,0 | *+0,9 [-3,9; +5,6]* | *-2,6* | *+2,6* |
| shepherd | 0 | — | -0,7 | — | — | — |
| thraben | 642 | *+0,8 [-4,2; +5,6]* | -1,1 | *+1,9 [-3,4; +7,2]* | *-1,2* | *+2,8* |

**una terra**: 13.969 mani, D-2 ne tiene il 20%, Delta del gruppo +11,1 punti.

| Carta | Mani con | Delta con | Delta senza | Differenza | Delta con, in play | Delta con, in draw |
| --- | --- | --- | --- | --- | --- | --- |
| recruiter | 2.236 | +9,4 [+6,7; +12,1] | +11,4 | -2,0 [-4,9; +0,9] | +7,3 | +11,5 |
| witch | 2.384 | +8,6 [+5,9; +11,2] | +11,6 | -3,0 [-5,8; -0,1] | +9,1 | +8,2 |
| phelia | 7.331 | +9,8 [+8,4; +11,2] | +12,5 | -2,6 [-4,8; -0,5] | +7,5 | +12,2 |
| ephemerate | 5.892 | +10,9 [+9,2; +12,5] | +11,3 | -0,4 [-2,5; +1,8] | +8,8 | +12,9 |
| riddler | 7.337 | +8,8 [+7,4; +10,2] | +13,6 | -4,8 [-6,9; -2,6] | +8,0 | +9,6 |
| consign | 5.899 | +5,4 [+3,8; +7,0] | +15,3 | -9,9 [-12,0; -7,7] | +2,6 | +8,1 |
| force | 2.263 | +5,2 [+2,5; +7,9] | +12,2 | -7,0 [-9,9; -4,1] | +1,5 | +9,0 |
| teferi | 2.271 | +6,5 [+3,9; +9,1] | +12,0 | -5,5 [-8,3; -2,6] | +0,9 | +11,7 |
| shepherd | 5.861 | +10,2 [+8,5; +11,9] | +11,7 | -1,5 [-3,7; +0,7] | +7,9 | +12,6 |
| thraben | 2.323 | +7,7 [+5,1; +10,5] | +11,8 | -4,0 [-6,9; -1,1] | +5,0 | +10,4 |

## Regressione logistica (sezione 9.4)

logit P(vince) = a + b·x + tieni·(g0 + g·x) sulle partite K e M dei seed 1-50.000 (100.000 righe), errori raggruppati per seed. I coefficienti **g** ("tieni:...") dicono quanto ogni caratteristica cambia il valore di tenere (in logit). Qui quelli con |z| ≥ 2, per |z|:

| Coefficiente | Stima | Errore standard | z | p |
| --- | --- | --- | --- | --- |
| tieni:uno_mana | +1,000 | 0,053 | +18,7 | 2.0e-78 |
| tieni:t0 | -1,646 | 0,107 | -15,4 | 8.4e-54 |
| tieni:risposte_dirette | +0,623 | 0,046 | +13,6 | 3.4e-42 |
| tieni | -4,574 | 0,363 | -12,6 | 1.8e-36 |
| tieni:c_recruiter | +0,765 | 0,065 | +11,7 | 7.6e-32 |
| tieni:c_shepherd | +0,655 | 0,056 | +11,7 | 1.5e-31 |
| tieni:c_riddler | +0,604 | 0,056 | +10,9 | 1.7e-27 |
| tieni:p3 | +1,243 | 0,116 | +10,7 | 8.7e-27 |
| tieni:due_mana | +0,460 | 0,047 | +9,9 | 6.8e-23 |
| tieni:c_thraben | +0,550 | 0,066 | +8,4 | 5.9e-17 |
| tieni:c_ephemerate | +0,483 | 0,059 | +8,2 | 1.6e-16 |
| tieni:in_play | +0,179 | 0,028 | +6,4 | 1.2e-10 |
| tieni:witch | -0,256 | 0,040 | -6,4 | 1.4e-10 |
| tieni:c_consign | +0,361 | 0,057 | +6,3 | 2.6e-10 |
| tieni:terre_totali | +0,345 | 0,060 | +5,7 | 1.0e-08 |
| tieni:coppia | +0,260 | 0,055 | +4,8 | 1.8e-06 |
| tieni:c_force | +0,289 | 0,066 | +4,4 | 1.3e-05 |
| tieni:bianche | +0,087 | 0,033 | +2,6 | 0,008 |

**Validazione sui seed 50.001-100.000:** Delta medio previsto +10,39, osservato +10,63; correlazione tra previsto e osservato per seed 0,199 (l'osservato è −1, 0 o +1). Per decile del previsto:

| Decile | Mani | Delta previsto | Delta osservato |
| --- | --- | --- | --- |
| 1 | 5.000 | -11,3 | -10,4 [-12,1; -8,8] |
| 2 | 5.000 | -3,3 | -2,5 [-4,1; -0,8] |
| 3 | 5.000 | +1,1 | +2,4 [+0,6; +4,1] |
| 4 | 5.000 | +4,8 | +5,2 [+3,6; +7,0] |
| 5 | 5.000 | +8,2 | +8,4 [+6,6; +10,2] |
| 6 | 5.000 | +11,7 | +11,9 [+10,2; +13,6] |
| 7 | 5.000 | +15,3 | +14,0 [+12,3; +15,9] |
| 8 | 5.000 | +19,3 | +20,0 [+18,2; +21,8] |
| 9 | 5.000 | +24,6 | +22,1 [+20,2; +23,8] |
| 10 | 5.000 | +33,7 | +35,3 [+33,5; +37,0] |

**Politica "tieni se il Delta previsto è positivo"** (modello stimato sui seed 1-50.000), misurata sui seed nuovi 100.001-200.000 contro D-2: differenza appaiata **+4,09 [+3,84; +4,33]** punti (normale [+3,85; +4,33]); WR 41,34% contro 37,25%. La politica tiene il 77,5% delle prime mani, D-2 il 53,4%; decide diversamente in 35.657 seed (29.883 mani tenute in più, 5.774 rifiutate in più), dove vale +11,48 punti. **Solo una variante candidata**: non si adotta.


## D-2 contro M1, M1-R e M3 (sezione 9.6)

Per ogni variante, i seed in cui decide diversamente da D-2: a 7 carte la sua partita è K o M (controllato voce per voce: "identiche"), a 6 carte (nella partita M) è giocata a parte con la variante. "Pubblicato": sezione 29 di `agente_uw.md` (stessi seed, partite intere con la variante).

| Variante | Cambiate (a 7 / a 6) | Identiche a K o M | Delta lì | Pubblicato | Guadagno sul totale | Pubblicato |
| --- | --- | --- | --- | --- | --- | --- |
| M1 | 11.792 (11.349 / 443) | 11.349 su 11.349 | +9,96 [+8,82; +11,10] | 11.792, +9,96 | +1,18 | +1,18 |
| M1-R | 3.122 (2.942 / 180) | 2.942 su 2.942 | +17,78 [+15,57; +19,98] | 3.122, +17,78 | +0,56 | +0,56 |
| M3 | 1.536 (1.436 / 100) | 1.436 su 1.436 | +24,54 [+21,28; +27,80] | 1.536, +24,54 | +0,38 | +0,38 |

M1 per gruppo (la carta che fa scattare il cambio nella mano tenuta):

| Gruppo | Cambiate | Delta | Pubblicato | A 7 carte: cambiate, K − M | A 6 carte |
| --- | --- | --- | --- | --- | --- |
| entrambe | 1.363 | +15,92 | 1.364, +15,98 | 1.282, +16,15 | 81 |
| solo Recruiter | 1.730 | +19,48 | 1.729, +19,43 | 1.660, +20,18 | 70 |
| solo Witch | 8.699 | +7,14 | 8.699, +7,14 | 8.407, +7,65 | 292 |

Qui il gruppo è dato da quali delle due carte ci sono nella mano tenuta; la sezione 29 lo dava dalla carta che fa scattare il cambio. Una partita su 11.792 cade in un gruppo diverso ("entrambe" e "solo Recruiter"); i totali coincidono.


## Witch Enchanter (sezione 9.5)

**Le mani "solo Witch" a 7 carte** (M1 le tiene, D-2 no): 8.407 mani, K − M +7,6 [+6,3; +9,0].


**(a) Il mulligan evitato:** mani dello stesso tipo (2-4 terre con Witch, una giocata, nessuna risposta diretta), con e senza Witch, per numero di terre:

| Terre (con Witch) | Witch | Mani | WR K | WR M | Delta | D-2 tiene |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | no | 4.303 | 49,6 | 29,6 | +20,0 [+18,1; +21,9] | 6% |
| 2 | sì | 2.055 | 41,6 | 27,8 | +13,7 [+11,0; +16,6] | 7% |
| 3 | no | 4.852 | 44,7 | 29,1 | +15,6 [+13,7; +17,3] | 3% |
| 3 | sì | 4.051 | 39,6 | 29,6 | +9,9 [+8,1; +11,9] | 3% |
| 4 | no | 2.966 | 36,4 | 30,7 | +5,7 [+3,3; +7,9] | 1% |
| 4 | sì | 3.880 | 37,4 | 29,9 | +7,5 [+5,5; +9,5] | 1% |

**(b) e (c): cosa fa Witch nelle partite K delle mani "solo Witch" a 7 carte** (8.407 partite). Una partita può avere più usi (più copie):

| Uso | Partite | WR di UW (K) |
| --- | --- | --- |
| terra | 3.512 (41,8%) | 46,5 [44,9; 48,2] |
| lanciata | 1.980 (23,6%) | 54,2 [52,0; 56,4] |
| esiliata per Solitude | 1.123 (13,4%) | 53,0 [50,1; 55,9] |
| esiliata per March | 0 (0,0%) | — |
| in mano alla fine | 3.813 (45,4%) | 21,3 [20,1; 22,7] |
| Solitude arriva dopo (pescata o cercata) | 3.415 (40,6%) | 53,1 [51,5; 54,8] |

## Recruiter of the Guard e Quantum Riddler (D-2c, sezione 9.6)

Partite normali dei seed 1-100.000 con Recruiter lanciato: 14.473. Turno proprio del lancio, carta cercata (nello stesso turno), turno proprio in cui quella copia viene usata (lanciata o esiliata come costo), WR.

| Carta cercata | Partite | Turno del lancio (media) | Usata | Turni dal lancio all'uso (media) | WR di UW |
| --- | --- | --- | --- | --- | --- |
| Sanctifier en-Vec | 11.042 | 4,92 | 74,8% | 0,75 | 56,3 [55,4; 57,3] |
| Solitude | 2.687 | 5,70 | 62,0% | 0,31 | 61,1 [59,3; 62,9] |
| Guide of Souls | 326 | 5,77 | 96,3% | 0,26 | *77,9 [73,1; 82,1]* |
| Witch Enchanter | 240 | 4,03 | 28,3% | 1,91 | *65,4 [59,2; 71,1]* |
| Ocelot Pride | 112 | 7,03 | 97,3% | 0,11 | — |
| Phelia, Exuberant Shepherd | 66 | 8,58 | 25,8% | 0,47 | — |

Solitude cercata: usata nello stesso turno nel 48,2% dei casi, al turno dopo nel 9,8%, mai nel 38,0%.


**Quantum Riddler:** 37.905 partite con Riddler entrata (lanciata dalla mano o dall'esilio), al turno proprio 4,79 in media; WR 56,1 [55,6; 56,6]. Nel turno in cui entra UW pesca in media **3,01** carte, di cui **0,355** risposte dirette; nei turni propri senza Riddler (dal secondo) 1,07 carte e 0,167 risposte. Il record non separa la pescata del turno da quelle di Riddler: la differenza è quello che Riddler aggiunge. Con almeno una risposta pescata nel turno di Riddler: 31,7% delle partite.


## Tempi

| Passo | Tempo |
| --- | --- |
| metriche.exe --mulligan-forzato uw, seed 1-100.000 (8 processi, con il record di K e M) | 36,9 s |
| metriche.exe --mulligan-forzato rakdos, seed 1-100.000 (8 processi) | 30,4 s |
| metriche.exe --mulligan-forzato uw, seed 100.001-200.000 (8 processi) | 32,4 s |
| Analisi: regressione e politica | 3,1 s |
| Analisi: record K delle mani solo Witch | 1,1 s |
| Analisi: lettura di record.jsonl | 7,4 s |
| Analisi: lettura delle coppie | 8,6 s |
| Analisi, in tutto | 29,8 s |
