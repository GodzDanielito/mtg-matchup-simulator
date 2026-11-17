# Resoconto: fase 4, passo 3, punto 0a (il limite della pila) e preparazione del punto 0b

Per Daniele, 17/11/2026. I dettagli sono in [docs/ricerca.md](ricerca.md), sezione 11. Il punto 0b è un'esecuzione notturna di circa 5,5 ore: come chiesto mi sono fermato prima di lanciarla (comando in fondo). Il resoconto completo del punto 0 arriva dopo la notte.

## In breve

| Punto | Esito |
| --- | --- |
| 0a, riproduzione | I 4 rollout interrotti del passo 2 **si riproducono tutti**, identici, con un replay leggibile di ognuno (`data/ricerca/pila/`) |
| 0a, regole | **Stato legittimo**: nessun ciclo, nessun trigger duplicato. Tre superano la pila (due Flare of Malice che sacrificano i due Stitcher's Supplier con 3-4 Moonshadow in campo: 17 elementi), uno supera i **64 token** (quattro Ocelot Pride che raddoppiano i gatti al turno 30). Nessuna correzione delle regole, `kMaxStack` non alzato |
| 0a, anomalie | Un rollout oltre il limite **non conta**: il suo mondo si scarta per tutte le mosse. Una partita vera oltre il limite va in `anomalie.jsonl`, fuori dai dati |
| 0a, effetto sul +26,55 | **Nessuno**: le tre partite con rollout interrotti, rigiocate con il codice nuovo, sono identiche riga per riga |
| Euristici | 23 riferimenti MD5 identici; ctest in Release 615/615 |
| 0b | Pronto, **da lanciare stanotte** (circa 5,5 ore, 20 thread) |

## I quattro rollout interrotti

| Seed, decisione | Mossa del rollout | Limite | Cosa succede |
| --- | --- | --- | --- |
| 227, 59 (due volte, mondo 158) | March X = 1 su un proprio Cat Token | pila, 16 | Sottofase finale di UW con 5 trigger di UW in pila. Rakdos (tre Moonshadow) lancia Flare sacrificando un Supplier (macina + 3 trigger di Moonshadow), in risposta la seconda Flare sull'altro Supplier (altri 4); la prima macina mette in cimitero carte permanente e innesca di nuovo le Moonshadow: **17** |
| 1593, 22 (mondo 73) | Solitude evocata su Ocelot Pride | pila, 16 | Stesso schema con **quattro** Moonshadow, sopra l'esilio del warp di Riddler e Consign: **17** |
| 1382, 57 (mondo 121) | **passa** (la mossa dell'euristico) | 64 token | Turno 30, partita ferma (UW 42 vite, Rakdos 36): quattro Ocelot Pride con la benedizione della città, ogni trigger crea un gatto e copia tutti i token entrati nel turno; con 49 gatti il trigger successivo supera i 64 posti |

Perché è legittimo (testi Oracle di Scryfall del 9/11/2026, già nei test delle carte): Moonshadow si innesca **una volta per evento** ("whenever one or more permanent cards are put into your graveyard"), e sacrificio e macina sono due eventi; il Supplier macina quando muore; Flare può costare il sacrificio di una creatura nera non token, e una seconda Flare in risposta alla prima è una giocata legale. Nel gioco la pila non ha un limite: 17 elementi sono una situazione vera, oltre la capacità fissa del motore.

Due cose che ho imparato e che correggono il passo 2:
- **Non è sempre la pila**: un caso su quattro è il limite dei token.
- **Non nasce solo da mosse che gli euristici non fanno**: nel seed 1382 il rollout interrotto è quello della mossa dell'euristico. Quindi anche una partita vera ci può arrivare, ed è per questo che le anomalie servono davvero.

## Il contatore di anomalie

| Caso | Prima (passo 2) | Ora |
| --- | --- | --- |
| Rollout oltre il limite | Valeva 0 solo per la sua mossa (una sconfitta inventata in quel mondo) | Il mondo si **scarta per tutte le mosse**: vittorie, intervallo al 99% e migliore mossa contano solo i mondi rimasti; tutti scartati: si gioca l'euristico |
| Partita vera oltre il limite | Fermava tutto il blocco | **Anomalia**: riga in `anomalie.jsonl` (seed, errore), fuori da `partite.jsonl`, `ricerca.jsonl` e dal confronto appaiato; il riepilogo dice quante e quali |
| Altro errore del motore | Fermava il blocco | Ferma ancora il blocco (è un bug, non un'anomalia) |
| Conteggi | `interrotti` | `interrotti` (rollout) e `scartati` (mondi) in ogni riga di ricerca, totali nel riepilogo |

Test scritti prima e visti fallire: la tabella con un mondo scartato è uguale alla tabella senza quella colonna; la pila piena in tutti i mondi e poi solo nel mondo 1; la partita vera come anomalia (e un errore diverso che ferma ancora il blocco); il Rakdos vero scelto per nome.

## Effetto sui risultati del passo 2

| Seed | Riga di `partite.jsonl`, passo 2 contro codice nuovo | Ricerche | Scelte |
| --- | --- | --- | --- |
| 227 | identica (UW vince al turno 9) | 58 contro 58 | identiche (una ricerca con un mondo scartato) |
| 1382 | identica (UW vince al turno 20) | 130 contro 130 | identiche (una ricerca con un mondo scartato) |
| 1593 | identica (UW vince al turno 9) | 49 contro 49 | identiche (una ricerca con un mondo scartato) |

Nelle altre 1.997 partite non c'era nessun rollout interrotto, quindi **il +26,55 [+24,42; +28,68] resta valido così com'è**.

## Un'osservazione per te (non un errore)

Nel caso dei token (seed 1382, turno 30) UW ha 49 gatti contro tre bloccanti e Rakdos a 36 vite, e attacca solo con Guide of Souls, Ocelot Pride e Solitude: le regole di attacco dell'euristico non mandano i gatti in un attacco di massa. Non ho toccato niente; l'ho annotata in `ipotesi_daniele.md` come "da verificare con te", da guardare quando la ricerca coprirà gli attacchi.

## Punto 0b: cosa parte stanotte

UW forte (R = 256, rollout che assumono **sempre** base_r3) contro Rakdos **base_r1** e **base_r2** sui seed 1-2.000, appaiato con UW euristico v1 contro lo stesso Rakdos: differenze con IC 95%, tutte, in play e in draw.

| | Valore |
| --- | --- |
| Durata stimata | circa 2,7 ore per avversario, **circa 5,5 ore in tutto** (8 blocchi da 500 seed, 40-45 minuti l'uno, come contro base_r3) |
| Cosa occupa | tutti i 20 processori logici (20 thread di notte, 16 se un blocco parte tra le 7 e le 22); memoria modesta; circa 220 MB su disco in `data/ricerca/` (esclusa da Git) |
| Scadenza | nessun blocco parte se non può finire entro le 06:50 (stima dal blocco precedente); rilanciando lo script si riprende dai blocchi mancanti |
| Uscite | `data/ricerca/forte_R256_contro_base_r1/` e `_contro_base_r2/` (con `riepilogo.txt`), `data/ricerca/riepilogo_notte_passo3.txt` |
| Attenzione | il PC non deve andare in sospensione; non ricompilare `ricerca.exe` mentre gira |

Comando da lanciare (Git Bash, dalla cartella del progetto):

```sh
bash data/ricerca/notte_passo3.sh
```

Per lanciarlo partendo dalle 22:00 servono circa 5,5 ore: finisce verso le 03:30. Se lo lanci dopo l'01:20 il secondo avversario potrebbe non finire entro le 06:50; lo script si ferma pulito e la sera dopo riprende dai blocchi mancanti.

URL: https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
