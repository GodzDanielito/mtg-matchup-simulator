# mtg-matchup-simulator

[![CI](https://github.com/GodzDanielito/mtg-matchup-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/GodzDanielito/mtg-matchup-simulator/actions/workflows/ci.yml)

Simulatore in C++ di un singolo matchup di Magic: The Gathering in formato Modern: **UW Blink contro Rakdos Delirium**, solo **gara 1** e solo con i **mazzi di main**.

## Obiettivo

Giocare 100.000 partite tra due liste fisse e ricavarne dati per carta in stile [17lands](https://www.17lands.com/), adattati al constructed:

- **win rate del matchup**, complessivo e separato tra play e draw (con 100.000 partite il margine d'errore scende a circa ±0,3 punti percentuali);
- **metriche per carta**: win rate quando la carta era nella mano iniziale (OH), pescata dopo (GD), vista in qualsiasi modo (GIH), mai vista (GNS), e la differenza IWD;
- **mulligan**: win rate per mani da 7, 6 e 5 carte e per tipo di mano tenuta;
- **turni chiave**: quando Rakdos raggiunge il delirium, quando arrivano le minacce grandi e con quale carta vengono gestite.

La specifica completa è in [docs/progetto.md](docs/progetto.md).

## Stato attuale

**Fase 1 (motore) completata**, fase 2 (tutte le carte del pool e l'agente euristico) in corso. Esistono:

- le liste dei due mazzi in [config/decks/](config/decks/), con un caricatore che verifica 60 carte esatte e al massimo 4 copie;
- i dati delle carte (costi, tipi, forza e costituzione) generati da Scryfall in [config/cards.json](config/cards.json);
- lo **stato di gioco**: carte, zone, giocatori, generatore casuale con seed, spostamenti tra zone con il riepilogo del cimitero (delirium, forza di Nethergoyf, prove per Detective's Phoenix);
- la **struttura della partita**: turni con fasi e step, priorità, azioni di stato, mulligan London;
- il **lancio delle magie** (601.2) con bersagli e pagamento del mana, la pila con risoluzione dalla cima e magie neutralizzate se il bersaglio non è più legale;
- il **combattimento** (506-511): dichiarazioni di attacco e blocco come decisioni successive senza duplicati, divisione del danno tra più bloccanti, step del danno da attacco improvviso; parole chiave volare, rapidità, legame vitale, attacco improvviso, minaccia, swampwalk e protezione;
- **tutte le 41 carte** delle liste di simulazione giocabili in [cards/](cards/), ognuna con il suo testo Oracle nei test (un test di configurazione fallisce se una carta delle liste non è implementata):
  - le 13 terre del pool;
  - le 12 magie e creature di Rakdos: Lightning Bolt, Thoughtseize, Street Wraith, Mishra's Bauble, Stitcher's Supplier, Nethergoyf (con la fuga), Moonshadow, Dark Matter Manipulator, Troll of Khazad-dûm, Fear of Missing Out (con i combattimenti aggiuntivi), Detective's Phoenix (con il conferimento, anche dal cimitero) e Flare of Malice (con il costo alternativo);
  - le 16 carte non terra di UW Blink: Ocelot Pride (con l'ascesa e le copie dei token), Guide of Souls (vita, energia e segnalini in attacco), Witch Enchanter (con il retro Witch-Blessed Meadow), Recruiter of the Guard, Starfield Shepherd (con il warp), Sanctifier en-Vec (con l'esilio delle carte nere e rosse al posto del cimitero), Solitude (con l'evocazione), Ephemerate (con il rimbalzo), Phelia, March of Otherworldly Light (con X e la riduzione del costo), Thraben Charm (tre modi), Seam Rip (esilio finché resta in campo), Teferi, Time Raveler (planeswalker: fedeltà, abilità, attaccabile), Quantum Riddler (pescate in più e warp), Consign to Memory (neutralizza abilità innescate, con il replicare) e Force of Negation (gratis nel turno avversario);
- un **agente casuale** con un generatore suo, e un test di robustezza: 10.000 partite UW Blink contro Rakdos e 10.000 Rakdos contro Rakdos, con il controllo di tutti gli invarianti dopo ogni azione (circa 10 secondi in Release);
- l'**agente euristico di Rakdos** in [agents/](agents/), con le regole di [docs/agente_rakdos.md](docs/agente_rakdos.md);
- l'**agente euristico di UW Blink** in [agents/](agents/), con le regole di [docs/agente_uw.md](docs/agente_uw.md), e il **matchup** tra i due agenti (`matchup.exe`, 10.000 partite in circa 10 secondi). Dall'11/11/2026 l'agente di UW gioca con tre varianti decise da Daniele, pilota di UW (Teferi su qualsiasi bersaglio utile, Quantum Riddler con il warp e Consign sull'esilio, la ricarica di Riddler senza contare Force of Negation e Consign): UW vince circa il 39,6% delle partite contro il 35,8% di prima. Le varianti restano opzioni disattivabili per i confronti A/B (`varianti_uw.exe`);
- il **goldfish** in [tools/](tools/): Rakdos contro 60 Plains che non fanno nulla, 100.000 partite in circa 25 secondi in Release, con il turno del delirium e della vittoria in play e in draw;
- il **registro delle partite in JSON Lines**: decisioni con la regola dell'agente, pescate con il turno, spostamenti, cimitero, vita con la fonte, esito.

Non esistono ancora le altre carte del pool (abilità e lancio). Il progetto dello stato è descritto in [docs/stato_di_gioco.md](docs/stato_di_gioco.md); la storia delle decisioni in [docs/diario.md](docs/diario.md).

## Requisiti

- **Windows** (è l'ambiente di sviluppo e quello della CI)
- **Visual Studio 2026** (o 2022) con il carico di lavoro "Sviluppo di applicazioni desktop con C++"
- **CMake** 3.24 o successivo
- **[uv](https://docs.astral.sh/uv/)** per gli script Python (gestisce da solo l'interprete)

Catch2 e nlohmann/json vengono scaricati automaticamente da CMake alla prima configurazione.

## Compilare ed eseguire i test

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Test degli script Python:

```sh
uv run --no-project python -m unittest discover -s python/tests
```

Goldfish di Rakdos (risultati in `data/goldfish/`, esclusi dal repository):

```sh
build/Release/goldfish.exe                 # 100.000 partite, registro completo delle prime 100
build/Release/goldfish.exe --mulligan A    # mulligan con una terra: A (sempre mulligan), B (predefinita), C
build/Release/goldfish.exe --bolt-face     # variante: Lightning Bolt in faccia appena possibile
```

Per rigenerare i dati delle carte dopo aver modificato una lista (scarica il bulk "oracle_cards" di Scryfall, circa 25 MB, la prima volta):

```sh
uv run --no-project python python/build_card_data.py
```

## Struttura

| Cartella | Contenuto |
| --- | --- |
| [engine/](engine/) | Motore delle regole in C++20 |
| [cards/](cards/) | Implementazione delle carte, una per file |
| [agents/](agents/) | Agenti giocatori: euristico di Rakdos, avversario del goldfish |
| [tools/](tools/) | Goldfish e scrittura del registro JSON Lines |
| [tests/](tests/) | Test Catch2 |
| [python/](python/) | Script di supporto e loro test |
| [config/](config/) | Liste dei mazzi e dati delle carte |
| [data/](data/) | Registri e risultati delle simulazioni (i file generati non sono nel repository) |
| [docs/](docs/) | Specifica, progetto dello stato, diario |

## Roadmap

Dalla specifica ([docs/progetto.md](docs/progetto.md)). Alcune fasi hanno un controllo da superare prima di passare alla successiva. Le durate sono **indicative**, per un lavoro part-time.

| Fase | Durata | Contenuto | Controllo per passare oltre | Stato |
| --- | --- | --- | --- | --- |
| 0. Specifiche | 1-2 settimane | Liste, formato del registro, regole da coprire | | **Completata** |
| 1. Motore | 4-6 settimane | Regole base e goldfish di Rakdos Delirium | Goldfish plausibile | **Completata** |
| 2. Mazzi main | 4-6 settimane | Tutte le 43 carte e l'agente euristico | Tutti i test di regole superati | **In corso** |
| 3. Metriche | 2-3 settimane | Registro, metriche stile 17lands, primi dati | Dati coerenti con i risultati dei tornei | |
| 4. Gioco forte | 6-10 settimane | ISMCTS e rete neurale allenata su GPU con CUDA | | |
| Dopo | | Sideboard e partite al meglio delle tre | | |

## Crediti e avvertenze

- I dati delle carte provengono da [Scryfall](https://scryfall.com/) tramite i suoi bulk data. Scryfall non è affiliato a questo progetto.
- Questo progetto è contenuto non ufficiale consentito dalla [Fan Content Policy](https://company.wizards.com/en/legal/fancontentpolicy). Non è approvato né sponsorizzato da Wizards of the Coast. Alcune parti sono materiale di proprietà di Wizards of the Coast. ©Wizards of the Coast LLC.
- Codice rilasciato con licenza [MIT](LICENSE).
