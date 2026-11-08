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

## Struttura

| Cartella | Contenuto |
| --- | --- |
| `engine/` | Motore delle regole: stato di gioco, pila, combattimento, azioni legali |
| `cards/` | Una carta per file |
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

**Nomi dei `TEST_CASE` solo in ASCII** (niente lettere accentate): ctest passa il nome del test a riga di comando e su Windows i caratteri non ASCII arrivano storpiati, quindi il test non viene trovato e risulta fallito. Le `SECTION` e i messaggi possono usare l'italiano completo.

Il caricatore delle liste è in `engine/deck_loader.hpp`; i test `[config]` verificano che le liste in `config/decks/` siano legali.

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
