# Resoconto: proposta della ricerca e misure di velocità (fase 4), 16/11/2026

## 1. Cosa è stato fatto

- **Decisioni del passo 4 scritte nei documenti:** approvazione con le 7 scelte (sezione 18 di `metriche.md`); M-LOG resta variante candidata, **riferimento da battere** per il mulligan cercato; il passo 3 delle metriche (campo `source`) **dopo gli agenti forti**.
- **Registro:** D-2c "da verificare con Daniele": i dati mostrano Recruiter che cerca Sanctifier nel 76% dei casi e Solitude nel 19%, al turno 5 e non al 3 (il tuo esempio era "turno 3, prendo Solitude e la uso subito"). Il 76% viene dall'ordine di ricerca dell'agente: Sanctifier per primo quando l'avversario ha mostrato nero o rosso (sezione 15 di `agente_uw.md`), cioè quasi sempre contro Rakdos.
- **Misure di velocità** con un programma di prova (`sonda_ricerca`) che usa motore e agenti senza modificarli. **Non è nel repository** (hai chiesto il commit del solo documento): è nella cartella temporanea della sessione; se lo vuoi in `tools/`, lo aggiungo.
- **`docs/ricerca.md`**: la proposta completa, nove sezioni. Nessuna ricerca implementata.

**Nel commit solo documenti:** `docs/ricerca.md` (nuovo), `docs/metriche.md`, `docs/ipotesi_daniele.md`, `docs/diario.md`, `docs/ultimo_resoconto.md`, `CLAUDE.md` (una riga sulla ricerca). Nessun file C++ o Python cambiato: suite e riferimenti MD5 non toccati.

## 2. La macchina

| | |
| --- | --- |
| Processore | i7-13650HX: **14 core fisici** (6 P con hyper-threading, 8 E), **20 thread logici** |
| Memoria | 16 GB |
| Alimentazione | Alla corrente, piano "Prestazioni elevate" |
| Game Shift | **Non disponibile** (Alienware Command Center non si installa): tutte le misure sono **senza** Game Shift e possono essere inferiori a quelle con le ventole al massimo |

## 3. Un core

| Misura | P-core | E-core |
| --- | --- | --- |
| **Rollout da metà partita** (registro spento, 6,9 turni e 133 decisioni in media) | **0,53-0,59 ms** (1.700-1.880 al secondo) | 1,13-1,42 ms |
| Partita intera da inizio | 0,76-0,80 ms | 1,76-1,79 ms |
| **Copia dello stato** (11.584 byte) | **0,12-0,22 µs** | 0,69-0,88 µs |
| Determinizzazione di prova, copia compresa | 0,57-0,68 µs | 1,4-1,7 µs |
| Costruzione dei due agenti | 2,4 µs | 4,3-6,6 µs |

La copia dello stato è lo 0,04% di un rollout: il requisito "trivially copyable" ripaga. Il tempo di un rollout è per il 65% in `apply`, 18% in `legal_actions`, 17% negli agenti.

## 4. Throughput sostenuto (10 minuti, tutti i thread)

| Serie | Processi | Andamento | Ultimi 5 minuti |
| --- | --- | --- | --- |
| 1 (da fermo) | 20 | 20.700 per 20 s, 14.650 fino a 160 s, poi **limite basso** | **8.758** |
| 1 | 16 | limite basso | 8.863 |
| 1 | 12 | limite basso per 2 minuti, poi 12.000 | 10.778 |
| 2 (verifica) | 12 | limite basso per 5 minuti, poi 12.000 | **11.938** |
| 2 | 16 | stabile per 10 minuti | **13.534** |
| 2 | 20 | stabile per 10 minuti | **14.983** |

- **Due regimi:** un **limite basso** (8.000-8.900 rollout/s, uguale con 12, 16 o 20 processi: limita la potenza) che compare dopo le partenze da fermo e dura 5-10 minuti, e un **regime normale** in cui più processi rendono di più. La serie 2 l'ho aggiunta io (30 minuti) perché la serie 1 confondeva l'ordine delle prove con il numero di processi.
- **Valore per le stime: 15.000 rollout/s** con 20 processi; caso peggiore 8.800.
- **Quanti processi: 20** per le esecuzioni lunghe senza nessuno al PC (+10% su 16, +25% su 12; nel limite basso non peggio); **16** se il PC serve nel frattempo. **Memoria: 16,4 MB per processo.**

## 5. Varianza appaiata

Due mosse alla radice (la scelta dell'euristico e un'altra a caso) sugli stessi 40 mondi, da 1.000 stati:

| Misura | Valore |
| --- | --- |
| Correlazione degli esiti negli stessi mondi | **0,81** |
| Varianza della differenza: mondi comuni contro indipendenti | **un terzo** (0,096 contro 0,290) |
| Stati con esiti identici in tutti i 40 mondi | **45%** |
| Deviazione standard della differenza con R mondi | 3,1 punti con R = 100, 1,4 con 500, 0,7 con 2.000 |

## 6. Decisioni per partita (euristici v1)

| Priorità per giocatore e partita | UW | Rakdos |
| --- | --- | --- |
| Propria fase principale, pila vuota | **18,4** (3,15 opzioni, passa 38%) | **22,5** (3,22 opzioni, passa 20%) |
| Pila non vuota (risposta) | 17,7 (passa 98%) | 11,5 (passa 98%) |
| Altro, pila vuota | 42,2 (passa 96%) | 33,6 (passa 98%) |
| Tutte le decisioni con almeno due opzioni | 106,1 | 95,0 |

## 7. La proposta in sintesi (`docs/ricerca.md`)

| Sezione | Contenuto |
| --- | --- |
| 2. Livelli | **A** ricerca piatta: K azioni × R mondi comuni, rollout con gli euristici v1; strategy fusion quasi assente (dopo la prima mossa giocano agenti che non vedono il nascosto), difetti: distorsione dei rollout, avversario fisso, niente inferenza. **B** ISMCTS a una vista con avversario euristico nell'albero, albero sul turno e il turno avversario. **C** rete di valore e politica allenata sulle posizioni di A e B |
| 3. Determinizzazione | **Si scambiano le identità** (`def`) tra gli oggetti nascosti al giocatore, compreso il **proprio grimorio** (tranne le posizioni note: fondo del mulligan, cima guardata); toglie le tracce di `CardId`, `incarnation` e generatore; forma canonica prima di campionare. **Test (a):** stato e gemello (differenti solo nel nascosto) devono dare mondi identici; (b) stessa scelta; (c) carte note; (d) distribuzione; (e) proprio grimorio; (f) invarianti |
| 4. Decisioni | Cercate: mulligan, fase principale, risposte, bersagli (mosse composte), Thoughtseize, attacchi e blocchi come insiemi. Euristica: pagamento, ordine dei trigger, sorveglianza (all'inizio). Budget adattivo con dimezzamento successivo; **a parità, la scelta dell'euristico** |
| 4.4 Mulligan | Cercato con 1.000-2.000 mondi per azione, confrontato con D-2 e **M-LOG (+4,09)** sugli stessi seed 100.001-200.000 |
| 5. Valutazione | **10 posizioni test** (le 5 della specifica, le tue 5), curva di convergenza su 1.000 decisioni vere, forti contro euristici appaiati, forti contro forti, robustezza contro UW base_v1 e base_v2, Rakdos base_r1, r2, r3 |
| 6. Registro | L'euristico come ombra a ogni decisione cercata: accordo e "rimpianto stimato" per regola gratis; differenza di win rate (regola forzata) solo per le 5-10 regole con il rimpianto più alto |
| 7. Record e metriche | Il record compatto vale per qualsiasi agente; serve un'interfaccia degli agenti in `play_matchup` (registri degli euristici identici) |
| 8. GPU | A e B solo CPU; la GPU per addestrare la rete del livello C; parallelismo per partite, come `metriche.exe` |

## 8. Tempi stimati

| Configurazione | 10.000 partite (15.000 / 8.800 rollout/s) | 100.000 partite |
| --- | --- | --- |
| A minimo: UW forte, fase principale e mulligan | 1,2 h / 2,1 h | 12 h / 21 h |
| **A proposto: UW forte** (circa 42 decisioni cercate, budget adattivo) | **2,0 h** / 3,4 h | **20 h** / 34 h |
| A proposto: UW e Rakdos forti | 4,0 h / 6,8 h | 40 h / 68 h |
| B: UW forte, 2.000 iterazioni per decisione | 16 h / 27 h | 6,7 giorni / 11 giorni |
| Ablazione con UW forte: 4 carte + base, 20.000 seed ciascuna | circa 20 h | — |

## 9. Ordine dei passi proposto

0. Generatore per giocatore (decisione 10 di `metriche.md`), con il ricalcolo dei riferimenti MD5.
1. Interfaccia degli agenti in `play_matchup` e determinizzazione con i test (a)-(f).
2. Livello A minimo, posizioni test, curva di convergenza.
3. Budget adattivo e mulligan cercato contro M-LOG.
4. Valutazione su 10.000 seed e accordo per regola.
5. Record e metriche con i forti; poi il passo 3 delle metriche.
6. Livello B, poi C.

## 10. Domande per te

| # | Domanda | Proposta |
| --- | --- | --- |
| 1 | La determinizzazione può assumere che ogni giocatore conosca le **60 carte esatte** dell'avversario? | Sì, come gli euristici oggi |
| 2 | **Inferenza** dalle scelte avversarie nella prima versione? | No; dopo, come variante misurata |
| 3 | **Generatore per giocatore** come passo 0, con il ricalcolo di tutti gli MD5? | Sì |
| 4 | Da che lato si parte? | UW forte, poi Rakdos |
| 5 | Esecuzioni di notte da 8-10 ore, con 20 processi? | Sì |
| 6 | Confermi le giocate corrette delle posizioni 6-10? Recruiter al turno 3 **senza** minacce in campo: cosa cerchi? | — |
| 7 | **"A parità, la scelta dell'euristico"** (un'azione migliore di meno di ε non si trova, ma ogni scostamento è significativo)? | Sì |

## 11. Tempi del passo

| Misura | Tempo |
| --- | --- |
| Misure su un core (P e E, partite, rollout, copia) | circa 2 minuti |
| Varianza appaiata (120.000 rollout su un P-core) | 62 s |
| Throughput sostenuto, serie 1 (20, 16, 12 processi) | 21 minuti |
| Throughput sostenuto, serie 2 (12, 16, 20 processi, 10 minuti ciascuna) | 31 minuti |

---

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
