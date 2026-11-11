# Simulatore UW Blink vs Rakdos Delirium

Simulatore in C++ del matchup Modern tra UW Blink e Rakdos Delirium: solo gara 1, solo mazzi di main.
La specifica completa è in [docs/progetto.md](docs/progetto.md): leggila prima di qualsiasi modifica non banale.

## Regole fisse

1. **C++20 e CMake.** Niente estensioni del compilatore (`CMAKE_CXX_EXTENSIONS OFF`); il codice deve compilare con MSVC, GCC e Clang. Gli avvisi del compilatore sono attivi al livello massimo e vanno risolti, non ignorati.
2. **Test prima del codice.** Ogni comportamento nuovo parte da un test Catch2 che fallisce; solo dopo si scrive il codice che lo fa passare. Un bug si corregge partendo da un test che lo riproduce.
3. **Casualità solo con seed.** Tutta la casualità (mescolare, mulligan, scelte degli agenti casuali) passa da un generatore con seed esplicito, posseduto dallo stato di partita. Vietati `std::random_device`, `rand()`, l'orario o qualsiasi fonte non deterministica dentro il motore. Stesso seed e stesse scelte devono dare la stessa partita, evento per evento.
4. **Una carta per file in `cards/`.** Ogni carta è un modulo autonomo che dichiara le sue abilità. Il nome del file è il nome della carta in `snake_case` (es. `cards/lightning_bolt.cpp`).
5. **Registro delle partite in JSON Lines.** Una riga JSON per evento, in `data/`. Il registro deve permettere di ricostruire la partita: mano iniziale, mulligan, pescate con il turno, carte giocate e bersagli, stato del cimitero, esito.
6. **Nessuna carta senza i suoi test.** Una carta non entra nel pool finché non ha test per ogni sua abilità e per le interazioni chiave elencate in `docs/progetto.md` (in particolare l'effetto sul cimitero: numero di carte, tipi, valore di mana).

## Git

Il repository è **pubblico**.

- **Mai `git add -A` o `git add .` alla cieca.** Prima di ogni commit si controlla `git status` e si aggiungono per nome solo i file pertinenti al passo.
- Prima di aggiungere un file nuovo si verifica che non sia grande, generato o personale (cache, build, registri delle partite, credenziali).

## Carte: mai a memoria

Prima di implementare una carta si legge il suo **testo Oracle aggiornato da Scryfall** (bulk `oracle_cards`, cache in `data/scryfall/`) e lo si riporta come commento nel test della carta, con la data dei dati. Nessuna carta si implementa a memoria: le regole ricordate male (Phoenix, Phelia, Moonshadow, Fear of Missing Out) sono già costate correzioni.

## Diario

Alla fine di **ogni passo** si aggiunge una voce in [docs/diario.md](docs/diario.md): cosa è stato fatto, decisioni prese e alternative scartate, errori trovati e corretti (anche quelli di regole e di processo). La voce va nello stesso commit del passo.

## Struttura

| Cartella | Contenuto |
| --- | --- |
| `engine/` | Motore delle regole: stato di gioco, pila, combattimento, azioni legali |
| `cards/` | Una carta per file |
| `agents/` | Agenti giocatori (euristico di Rakdos, avversario del goldfish) |
| `tools/` | Eseguibili: goldfish, scrittura del registro JSON Lines |
| `tests/` | Test Catch2 |
| `python/` | Orchestrazione, analisi (pandas), reti neurali |
| `config/decks/` | Liste dei mazzi in JSON |
| `data/` | Registri e risultati delle simulazioni (i file generati non vanno in Git) |
| `docs/` | Specifica e documentazione |

## Formato delle liste

```json
{
  "name": "Rakdos Delirium",
  "format": "Modern",
  "source": "URL della lista",
  "main": [
    { "card": "Lightning Bolt", "count": 4 }
  ]
}
```

Il nome della carta è il nome inglese ufficiale (Oracle). Il main deve sommare esattamente 60 carte.

## Comandi

```sh
cmake -S . -B build                 # configura (scarica Catch2 la prima volta)
cmake --build build --config Debug  # compila
ctest --test-dir build -C Debug --output-on-failure   # esegue i test
```

Su Windows il generatore predefinito è Visual Studio 2026 (MSVC): per questo serve `--config` / `-C`.

I test `[robustness]` (due serie da 10.000 partite) richiedono circa 10 s in Release ma alcuni minuti in Debug. Durante lo sviluppo: `build/Debug/tests.exe "~[robustness]"`, poi la suite completa in Release prima del commit.

**Nomi dei `TEST_CASE` solo in ASCII** (niente lettere accentate): ctest passa il nome del test a riga di comando e su Windows i caratteri non ASCII arrivano storpiati, quindi il test non viene trovato e risulta fallito. Le `SECTION` e i messaggi possono usare l'italiano completo.

**L'eseguibile dei test non apre mai finestre di errore.** In Debug il runtime di MSVC mostra asserzioni (anche della libreria standard: indice fuori dai limiti, iteratore non valido) ed errori in una finestra modale, e Windows ne apre un'altra per i crash: senza nessuno a chiuderle il processo resta fermo e sembra bloccato. `tests/crt_no_dialogs.cpp`, eseguito prima di `main`, manda asserzioni ed errori su stderr e spegne le finestre di Windows: un crash termina subito il processo con un messaggio. Il file va linkato in ogni eseguibile di test, e il test `[runtime]` controlla che la configurazione sia attiva. Se una suite sembra bloccata, è un bug: va cercato, non aspettato.

Il caricatore delle liste è in `engine/deck_loader.hpp`; i test `[config]` verificano che le liste in `config/decks/` siano legali.

Goldfish: `build/Release/goldfish.exe` (100.000 partite, circa 25 s; `--mulligan A|B|C` per la regola del mulligan con una terra, `--bolt-face` per la variante di Bolt). Le regole dell'agente sono in `docs/agente_rakdos.md`: ogni regola nuova o cambiata va prima lì, poi in un test, poi nel codice.

Matchup: `build/Release/matchup.exe` (agente euristico di UW contro agente euristico di Rakdos, 10.000 partite, circa 7 s; `--games N`, `--replays K`). Uscite in `data/matchup/` (escluse da Git). Con `--readable-dir data/matchup/replay_leggibili` sceglie 10 partite (5 con UW in play e 5 in draw, 3+2 vinte da UW e 2+3 da Rakdos) e ne scrive il replay in italiano, un file di testo per partita. `build/Release/replay_leggibile.exe --input REGISTRO.jsonl [--out DIR]` fa lo stesso da un registro già scritto.

Carte morte: `build/Release/carte_morte.exe` (100.000 partite del matchup, circa 2 minuti; `--games N`) scrive `data/matchup/carte_morte.csv` e stampa le carte che le regole degli agenti tengono in mano senza lanciarle, e le sconfitte con carte in mano. Non cambia le partite.

Varianti di UW: `build/Release/varianti_uw.exe` (base_v1 con tutte le opzioni spente, R1, T1 e W1 da sole, le coppie, base_v2 e base_v2 + M1; 100.000 partite ciascuna sugli stessi seed, circa 20 minuti) confronta le win rate con la differenza appaiata e l'intervallo al 95% e scrive `data/matchup/varianti_uw.txt`. Le varianti sono opzioni dell'agente (`UwAgentOptions`): dall'11/11/2026 T1, W1 e R1 sono accese per default (base_v2), M1 è spenta; tutte spente è base_v1. Il registro del matchup con tutte le opzioni spente deve restare identico byte per byte (riferimenti MD5 nella sezione 28 di `docs/agente_uw.md`, da `matchup.exe --replays 20`). I test di una variante impostano l'opzione esplicitamente. Le regole dell'agente di UW sono in `docs/agente_uw.md`, con la stessa disciplina: prima il documento, poi un test, poi il codice.

## Dati delle carte e Python

Python non è installato nel sistema: si usa `uv` (gestisce lui l'interprete).

```sh
uv run --no-project python -m unittest discover -s python/tests   # test Python
uv run --no-project python python/build_card_data.py              # rigenera config/cards.json
uv run --no-project python python/build_card_data.py --refresh    # riscarica il bulk da Scryfall
```

- `config/cards.json` è generato ma **va in Git**: fissa i dati Oracle usati dai test e dalle simulazioni. Va rigenerato quando cambia una lista in `config/decks/`; il test `[config]` di `test_card_db.cpp` fallisce se una carta delle liste manca.
- La cache del bulk Scryfall sta in `data/scryfall/` ed è esclusa da Git.

## Motore: regole di implementazione

- `GameState` deve restare *trivially copyable* (c'è uno `static_assert`): niente `std::vector`, `std::string` o puntatori dentro lo stato.
- Le carte si spostano **solo** con `move_objects` / `move_object` (`engine/zones.hpp`). Nei test, dopo ogni spostamento si chiama `test::require_invariants`, che confronta il riepilogo del cimitero con il ricalcolo da zero.
- `docs/stato_di_gioco.md` è il progetto di riferimento dello stato.
