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

Fase 1 (motore) in corso. Esistono:

- le liste dei due mazzi in [config/decks/](config/decks/), con un caricatore che verifica 60 carte esatte e al massimo 4 copie;
- i dati delle carte (costi, tipi, forza e costituzione) generati da Scryfall in [config/cards.json](config/cards.json);
- lo **stato di gioco**: carte, zone, giocatori, generatore casuale con seed, spostamenti tra zone con il riepilogo del cimitero (delirium, forza di Nethergoyf, prove per Detective's Phoenix);
- la **struttura della partita**: turni con fasi e step, priorità, azioni di stato, mulligan London, pila per le abilità;
- le **14 terre del pool** in [cards/](cards/) (base, shockland, fetchland, Blackcleave Cliffs, Raucous Theater, Meticulous Archive, il retro di Witch Enchanter), ognuna con il suo testo Oracle nei test;
- un **agente casuale** e un test di robustezza: 10.000 partite giocate solo con le terre, con il controllo di tutti gli invarianti dopo ogni azione (circa 5 secondi in Release).

Non esistono ancora il lancio delle magie, il combattimento e le abilità delle carte non terra. Il progetto dello stato è descritto in [docs/stato_di_gioco.md](docs/stato_di_gioco.md); la storia delle decisioni in [docs/diario.md](docs/diario.md).

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

Per rigenerare i dati delle carte dopo aver modificato una lista (scarica il bulk "oracle_cards" di Scryfall, circa 25 MB, la prima volta):

```sh
uv run --no-project python python/build_card_data.py
```

## Struttura

| Cartella | Contenuto |
| --- | --- |
| [engine/](engine/) | Motore delle regole in C++20 |
| [cards/](cards/) | Implementazione delle carte, una per file (ancora vuota) |
| [tests/](tests/) | Test Catch2 del motore |
| [python/](python/) | Script di supporto e loro test |
| [config/](config/) | Liste dei mazzi e dati delle carte |
| [data/](data/) | Registri e risultati delle simulazioni (i file generati non sono nel repository) |
| [docs/](docs/) | Specifica, progetto dello stato, diario |

## Roadmap

Dalla specifica ([docs/progetto.md](docs/progetto.md)). Alcune fasi hanno un controllo da superare prima di passare alla successiva. Le durate sono **indicative**, per un lavoro part-time.

| Fase | Durata | Contenuto | Controllo per passare oltre | Stato |
| --- | --- | --- | --- | --- |
| 0. Specifiche | 1-2 settimane | Liste, formato del registro, regole da coprire | | **Completata** |
| 1. Motore | 4-6 settimane | Regole base e goldfish di Rakdos Delirium | Goldfish plausibile | **In corso** |
| 2. Mazzi main | 4-6 settimane | Tutte le 43 carte e l'agente euristico | Tutti i test di regole superati | |
| 3. Metriche | 2-3 settimane | Registro, metriche stile 17lands, primi dati | Dati coerenti con i risultati dei tornei | |
| 4. Gioco forte | 6-10 settimane | ISMCTS e rete neurale allenata su GPU con CUDA | | |
| Dopo | | Sideboard e partite al meglio delle tre | | |

## Crediti e avvertenze

- I dati delle carte provengono da [Scryfall](https://scryfall.com/) tramite i suoi bulk data. Scryfall non è affiliato a questo progetto.
- Questo progetto è contenuto non ufficiale consentito dalla [Fan Content Policy](https://company.wizards.com/en/legal/fancontentpolicy). Non è approvato né sponsorizzato da Wizards of the Coast. Alcune parti sono materiale di proprietà di Wizards of the Coast. ©Wizards of the Coast LLC.
- Codice rilasciato con licenza [MIT](LICENSE).
