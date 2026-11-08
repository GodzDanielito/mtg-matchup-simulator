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
