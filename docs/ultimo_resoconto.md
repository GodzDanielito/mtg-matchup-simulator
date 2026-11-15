# Ultimo resoconto: fase 3, agenti euristici v1, registro, test di confine di X6, proposta delle metriche (15/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). **Nessun agente cambia**: i registri del matchup e del goldfish sono identici byte per byte a quelli di base_r3.

## In breve

- **Agenti congelati come "euristici v1":** UW base_v2 contro Rakdos base_r3; M1, M1-R e M3 spente. Riferimenti MD5 salvati (tabella sotto e sezione 1 di [metriche.md](metriche.md)).
- **Registro delle ipotesi corretto:** X1-C è il tuo principio, le soglie B e C sono mie; quattro righe passano a "Claude, non rivista" e "provvisoria"; nuove D-2b e D-2c, "da far decidere ai dati".
- **X6:** un secondo test case per ciascuna delle due condizioni; le due mutazioni di confine ora fanno fallire 2 test case ciascuna (prima 1). Suite: 519 test passati.
- **Proposta [docs/metriche.md](metriche.md), senza codice:** è il punto in cui mi fermo. Servono le tue decisioni sulle 11 domande della sezione 13 prima di scrivere lo strumento.
- **Un fatto nuovo sulla domanda di Witch (+7,14):** in quelle mani Solitude non c'è mai, per costruzione. Dettagli sotto.

## 1. Agenti euristici v1: riferimenti

Release ricompilato da HEAD `828ba9c`, albero pulito, prima di toccare qualsiasi file.

| Registro | Comando | MD5 |
| --- | --- | --- |
| Matchup, 10.000 partite | `matchup.exe --replays 20` | `partite.jsonl` `2302d021603ed245978dc22d9cce88ae` |
| Matchup, primi 20 registri completi | come sopra | `replay.jsonl` `a9f2908d0209afc4a1621c9bcc23c094` |
| Matchup, 100.000 partite | `matchup.exe --games 100000` | `partite.jsonl` `0ad6744d85b251264f85ab4fd6155b60` |
| Matchup, 100.000 partite: primi 10 registri completi | come sopra | `replay.jsonl` `9c13ada970161de30affee8ec24c83d7` |
| Goldfish, 10.000 partite | `goldfish.exe --games 10000 --replays 20` | `partite_C.jsonl` `63648b4c3600def504af46db50205d0c` |
| Goldfish, primi 20 registri completi | come sopra | `replay_C.jsonl` `eb4afb405eda3d5b84ff6c71cd323969` |
| Goldfish, 100.000 partite | `goldfish.exe` | `partite_C.jsonl` `3fc4b26daf526857f3ee70ab9f33d871` |

Il primo, il secondo e l'ultimo coincidono con quelli di base_r3 della sezione 24 di `agente_rakdos.md`. Dopo le modifiche di questo passo il matchup `--replays 20` dà ancora `2302d021...` e `a9f2908d...`.

**Matchup da 100.000 partite, euristici v1** (74 s, 1.352 partite al secondo su un core):

| | UW in play | UW in draw | Totale |
| --- | --- | --- | --- |
| UW vince | 42,72% | 31,79% | **37,26%** |
| Mani tenute da UW (7 / 6 / 5) | 54,2 / 38,1 / 7,7% | | 53,5 / 38,7 / 7,8% |
| Mani tenute da Rakdos (7 / 6 / 5) | 78,2 / 18,4 / 3,4% | | 78,0 / 18,6 / 3,4% |

## 2. Registro delle ipotesi: cosa è cambiato

| Riga | Prima | Dopo |
| --- | --- | --- |
| R-X1C | Fonte: Claude, adottata dopo la misura; l'ipotesi di Daniele era B | Fonte: **Daniele il principio** ("le carte che si ciclano giustificano mani con una terra"); le soglie B e C, e Mishra's Bauble tra le carte che cercano, sono **operazionalizzazioni di Claude**; C scelta dopo la misura |
| R-B | Claude, scelta dopo il goldfish | Daniele il principio; la soglia "due carte" di Claude |
| R-SOGLIA (vita 5 di Rakdos) | Claude, da verificare con Daniele; provvisoria | **Claude, non rivista; provvisoria** |
| D-16 (numeri 3 e 5) | Da verificare con Daniele; da far decidere all'albero | **Claude, non rivisti; provvisoria**; poi l'albero |
| R-ORD (ordine di Thoughtseize di base_r1) | Da verificare con Daniele; sostituita da R-X5 | **Claude, non rivista; provvisoria**; resta come coda dopo Teferi |
| D-6 (classifica delle minacce) | Da verificare con Daniele; da far decidere all'albero | Supplier: Daniele; la classifica: **Claude, non rivista; provvisoria**; poi l'albero |
| **D-2b** (nuova) | | Mulligan contestuale: dipende dalla settima carta. **Daniele, 15/11/2026; da far decidere ai dati** |
| **D-2c** (nuova) | | Recruiter e Riddler come risposte indirette: Recruiter al turno 3 trova Solitude e si usa subito; Riddler scava verso le risposte. **Daniele, 15/11/2026; da far decidere ai dati** |

Nella legenda: fonte "Claude, non rivista" ridefinita (numeri o soglie mai visti da te) e nuovo stato **"da far decidere ai dati"** (ipotesi tua non ancora nell'agente, che le metriche della fase 3 misurano prima dell'albero). In testa al registro la definizione degli euristici v1.

**Restano "da verificare con Daniele"** tre righe che non erano nella tua lista: R-MUL (quanto il mulligan base di Rakdos sia tuo), R-X3b (soglia dell'orologio di 2 turni: l'avevi già corretta in "stima di Claude", ma la fonte dice ancora "da verificare") e R-BOLT (Teferi prima di Ocelot). Dimmi se vanno trattate come le altre quattro.

## 3. Test di confine di X6

Due test case nuovi in `tests/test_rakdos_agent.cpp`, entrambi con Rakdos in draw e con lo **stato finale** che cambia se la condizione è rotta (il Troll resta in mano, una terra in meno), non solo il nome della regola:

| Condizione | Test vecchio | Test nuovo |
| --- | --- | --- |
| Primo turno | In play: X6 lancia Moonshadow al turno 1, al turno 3 si cicla | In draw: Moonshadow al turno 1 con la regola normale (niente Troll in mano), il Troll pescato al turno 2 di Rakdos si cicla, poi la terra trovata e Nethergoyf |
| Una sola terra in campo | In play, due Swamp in campo | In draw, tre terre in campo (Swamp, Blood Crypt, Blackcleave Cliffs): il ciclo prima di ogni creatura, poi Supplier, Manipulator e Nethergoyf |

**Mutazioni** (script temporaneo, ripristino byte per byte, `agents/` pulito):

| Mutazione | Test case falliti prima | Dopo | Quali |
| --- | --- | --- | --- |
| X6 non scatta mai | 4 | 4 | Gli stessi di prima |
| X6 a ogni turno | 1 | **2** | Il vecchio del secondo turno, il nuovo del Troll pescato |
| X6 con qualsiasi numero di terre | 1 | **2** | Il vecchio con due terre, il nuovo con tre |

Ogni test nuovo fallisce solo con la sua mutazione. Suite completa in Release: **519 test passati** (erano 517).

## 4. La proposta delle metriche

Il documento completo è [docs/metriche.md](metriche.md). In sintesi:

| Sezione | Cosa propone |
| --- | --- |
| 2. Record compatto | Una riga JSON per partita: seed, play o draw, esito, per giocatore mulligan, mani da 7 viste, carte in fondo, **una voce per copia** con gli eventi `[turno, codice]` (iniziale, pescata, cercata, ritorno, lanciata, terra, dal cimitero, dall'esilio, esiliata come costo, scartata, ciclata, macinata), terre in campo per turno, turno del delirium, minacce grandi. Circa 2 KB a partita invece di circa 60 KB del registro completo |
| 3. GIH, OH, GD, GNS, IWD | Per nome Oracle, "almeno una copia"; OH e GD disgiunte; **le cercate e le carte usate solo dal cimitero fuori da GIH e GNS**; le terre fuori dalla tabella principale (Witch dentro, come carta); curva delle copie. **IWD a turno fisso** come metrica principale: l'IWD classico gonfia UW e sgonfia Rakdos perché UW vince le partite lunghe |
| 4. Correlazione | IWD condizionato; tabella 2×2 e **sinergia** per 11 coppie decise prima (Solitude + Ephemerate, Solitude + Witch, Recruiter + Solitude, Riddler + Consign...); matrice esplorativa con correzione per confronti multipli |
| 5. Intervalli | **Wilson** per le proporzioni, **bootstrap** sulle partite (2.000 ricampionamenti, seed fisso) per le differenze; sempre *n* accanto al valore |
| 6. Delirium | WR di UW per turno del delirium di Rakdos (1...6+, mai), più il confronto a turno fisso |
| 7. Minacce grandi | Moonshadow, Nethergoyf, Phoenix conferito, Troll: chi le risolve, quando, con che esito. Serve un campo `source` nel registro in memoria del motore |
| 8-9. Mulligan | WR per mani da 7, 6, 5; **mulligan contestuale**: due partite per seed (tieni / mulligan forzati), valore di tenere per tipo di mano, la settima carta, regressione logistica con stima e verifica su seed separati |
| 10. Formato e tempi | `metriche.exe` (C++) scrive JSON Lines, Python converte in **Parquet**; circa 2-2,5 min per 100.000 partite, più 9 min per il mulligan contestuale |
| 11. Garanzie | MD5 identici, test "il record non cambia la partita", aggancio del mulligan che forzando la scelta dell'agente dà registri identici, e la **riproduzione esatta di M1** sui seed in cui cambia la decisione a 7 carte |

**Precisione attesa con 100.000 partite:**

| Valore | IC al 95% |
| --- | --- |
| Win rate di un gruppo di 10.000 partite | ±1,0 punti |
| IWD di una carta (4 copie o 1 copia) | circa ±0,7 punti |
| Sinergia di una coppia (Solitude + Ephemerate) | circa ±1,3 punti |
| Valore di tenere un tipo di mano con 1.000 mani / 5.000 / 20.000 | ±4,3 / ±1,9 / ±1,0 punti |

### La domanda su Witch (+7,14)

Le 8.699 mani tenute da M1 grazie alla sola Witch **non contengono Solitude**, per costruzione: Solitude è una risposta di D-2, e una mano con Solitude non ha bisogno di M1 per essere tenuta. Quindi Witch come carta bianca per Solitude può pesare solo se Solitude arriva dopo. La proposta separa tre spiegazioni:

| Spiegazione | Come si misura |
| --- | --- |
| (a) Il mulligan evitato: quelle mani valgono più di una da 6 comunque | Valore di tenere nelle mani dello stesso tipo senza Witch |
| (b) Witch come carta bianca per una Solitude pescata dopo | Variante diagnostica "Witch non si esilia per Solitude" sulle partite tenute; quante volte Witch viene davvero esiliata |
| (c) Witch come terra o come creatura | Gli usi di Witch nelle partite tenute; variante diagnostica "Witch mai come terra" |

## 5. Decisioni che servono da te (sezione 13 di metriche.md)

1. IWD a turno fisso come metrica principale, con l'IWD classico accanto?
2. Cercate e carte usate solo dal cimitero fuori da GIH e GNS?
3. Terre fuori dalla tabella principale, Witch dentro?
4. Valori indicativi sotto 1.000 partite, nascosti sotto 200?
5. Le coppie della sezione 4: quali aggiungere o togliere?
6. "Minaccia grande" = forza 4 o più, più Phoenix conferito e Troll?
7. Il campo `source` nel motore?
8. Tipi di mano e caratteristiche della sezione 9: quali carte contano per "la settima carta"?
9. 1.000.000 di seed per il mulligan contestuale, o prima 100.000?
10. Un generatore casuale per giocatore (cambia tutti i registri): prima della fase 4?
11. Le due varianti diagnostiche di Witch?

## Modifiche ai file

| File | Modifica |
| --- | --- |
| `docs/metriche.md` | Nuovo: la proposta |
| `docs/ipotesi_daniele.md` | Legenda, euristici v1, X1-C e B, D-6, D-16, R-ORD, R-SOGLIA, D-2b e D-2c, rimando alla sezione 9 di metriche.md |
| `tests/test_rakdos_agent.cpp` | Due test case di confine di X6 |
| `docs/agente_rakdos.md` | Sezione 24, aggiornamento sulle mutazioni |
| `CLAUDE.md`, `README.md` | Euristici v1 e fase 3 iniziata |
| `docs/diario.md`, `docs/ultimo_resoconto.md` | Voce del passo; questo resoconto |

**Resoconto su GitHub:** https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
