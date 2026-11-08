# Diario del progetto

Una voce per ogni passo: cosa è stato fatto, le decisioni prese con le alternative scartate, gli errori trovati e come sono stati corretti. Le voci più recenti sono in fondo.

---

## Passo 1: scheletro del progetto (fase 0), 8/11/2026

**Fatto**
- Repository Git, struttura delle cartelle (`engine/`, `cards/`, `tests/`, `python/`, `config/decks/`, `data/`, `docs/`).
- `CMakeLists.txt` con C++20, avvisi severi (`/W4 /permissive-` con MSVC), Catch2 v3.8.1 scaricato con FetchContent, un test di prova.
- Liste vuote in `config/decks/` e `CLAUDE.md` con le regole fisse del progetto.

**Decisioni**
- **Compilatore:** MSVC di Visual Studio 2026, già installato, con il generatore predefinito di CMake. Nessuna dipendenza da installare a mano.
- **Versioni fissate** (Catch2 `v3.8.1`), per build riproducibili.
- **Formato delle liste:** un array di voci `{ "card", "count" }`. *Scartata:* una mappa `{"Lightning Bolt": 4}`, perché non permette di aggiungere campi a una voce.

**Errori e correzioni**
- La specifica non era in `docs/progetto.md` ma nella root, con il titolo come nome del file: spostata.

---

## Passo 2: liste dei mazzi, caricatore, proposta per lo stato, 8/11/2026

**Fatto**
- Liste di UW Blink e Rakdos Delirium (di Calvinchen, Top 4 al RC del Sud-Est Asiatico del 17/10/2026), 60 carte ciascuna.
- `engine/deck_loader`: lettura JSON con nlohmann/json e verifica di legalità (60 carte esatte, massimo 4 copie tranne le terre base), con test anche sulle liste vere.
- Prima versione di `docs/stato_di_gioco.md`.
- `.gitattributes` per i fine riga LF.

**Decisioni**
- **Lettura e verifica separate:** il JSON malformato è un'eccezione, mentre le violazioni delle regole tornano tutte insieme come elenco. Chi aggiorna una lista vede tutti gli errori in un passaggio.
- **Copie sommate per nome:** la stessa carta su più righe conta una volta sola ai fini del limite di 4.
- **Proposta per lo stato:**
  - definizione immutabile separata dall'istanza in partita;
  - tabelle piatte senza puntatori (stato *trivially copyable*);
  - `ObjRef = { carta, incarnation }` per la regola 400.7;
  - riepilogo del cimitero incrementale;
  - un motore che si ferma alle decisioni invece di chiamare gli agenti.
- **Alternative scartate:** classi per carta con metodi virtuali e `new` (copia lenta e puntatori da ricollegare); `std::vector` dentro lo stato (allocazioni a ogni copia); `std::mt19937` (2,5 KB e numeri diversi tra compilatori).

**Errori e correzioni**
- **Errore nel test delle 59 carte:** costruiva il JSON con `rfind(',')`, che trovava la virgola dentro l'ultima voce. Il fallimento era nel test, non nel caricatore; il test è stato riscritto con un parametro.
- **ctest e i nomi accentati:** i test passavano lanciando l'eseguibile ma fallivano con ctest, perché la `è` nei nomi dei `TEST_CASE` arriva storpiata a riga di comando su Windows. Corretto il nome e aggiunta la regola al `CLAUDE.md`.
- **Lista Rakdos della specifica:** sommava 61 carte; sostituita con la lista di Calvinchen, di 60.
- **Nome del Troll:** il nome ufficiale è "Troll of Khazad-dûm", con l'accento circonflesso.

---

## Passo 3: revisione della proposta per lo stato, 8/11/2026

**Fatto**
- `docs/stato_di_gioco.md` corretto e completato con l'elenco delle decisioni.
- `config/decks/uw_blink_sim.json`: lista di simulazione con una terza Ephemerate al posto di Aang e una quarta Witch Enchanter al posto di Mockingbird. Un test verifica che differisca dalla lista vera esattamente per queste due sostituzioni.

**Errori di regole corretti in revisione**
- **Detective's Phoenix:** la proposta aveva un trigger "quando la creatura incantata muore, ritorna". *Sbagliato.*
  - Il conferimento costa {R} più raccogliere prove 6, e si può lanciare anche dal cimitero.
  - Quando l'Aura si stacca, la Phoenix resta in campo come creatura incantesimo: è un cambio di caratteristiche, non un trigger.
- **Phelia:** la specifica diceva "ritorno sotto il tuo controllo, pescata se era dell'avversario". *Sbagliato.*
  - La carta torna all'inizio della prossima sottofase finale sotto il controllo del **proprietario**.
  - Phelia prende un segnalino +1/+1 se la carta entra sotto il controllo di chi controlla Phelia.
  - I token esiliati smettono di esistere.
- **Moonshadow:** la descrizione era vaga ("cresce con il cimitero").
  - Entra con sei segnalini -1/-1 e ne rimuove uno ogni volta che **una o più carte permanente** finiscono nel suo cimitero.
  - Ne derivano due requisiti: eventi di cambio zona **raggruppati** (macinare 3 carte è un evento, non tre) e contatori +1/+1 e -1/-1 **separati**, con l'azione di stato che li annulla a vicenda.
- **Guide of Souls:** dà anche un segnalino volare, quindi servono i segnalini abilità.
- **Fear of Missing Out:** in questo passo il flag "ha già attaccato" è stato messo sul **giocatore**, interpretando l'abilità come "quando attacchi". *Sbagliato*, corretto nel passo 4.

**Decisioni**
- **Raccolta delle prove una carta alla volta**, fino a 6. *Scartata:* l'enumerazione dei sottoinsiemi, che può produrre migliaia di azioni per una sola decisione.
- **Teferi:** con il +1 il controllore può lanciare stregonerie come se avessero lampo.
- **Token:** 32 slot riciclabili, con incarnation che sale a ogni riuso ed errore esplicito se gli slot finiscono.
- **Ordine del cimitero:** si mantiene.
- **Passaggio automatico della priorità** solo quando "passa" è l'unica azione legale; non diventa un nodo della ricerca.
- **Mockingbird e Aang:** rimandati.

**Errori di processo**
- **`git add -A` alla cieca:** ha incluso nel commit la nuova `progetto.md` e una copia intermedia della specifica nella root, entrambe salvate dall'utente in quel momento. Risolto nel passo 5 con la regola "mai `git add -A`".

---

## Passo 4: inizio del motore, 8-9/11/2026

**Fatto**
- **Fear of Missing Out, correzione:** l'Oracle dice "Whenever this creature attacks for the first time each turn".
  - Il flag sta sulla **creatura** e si azzera al cambio di zona.
  - Due FOMO danno due combattimenti aggiuntivi; nel combattimento aggiuntivo la stessa creatura non riscatta.
  - Con un bersaglio illegale non c'è combattimento aggiuntivo.
- `python/build_card_data.py`: genera `config/cards.json` dal bulk "oracle_cards" di Scryfall per le sole 43 carte delle liste. Fallisce elencando i nomi che non esistono.
- **Motore:** `CardDef` e `CardDatabase`, `CardObject`, `ObjRef` con incarnation, `StaticVector` a capacità fissa, `PlayerState`, `GameState` *trivially copyable* da 4.464 byte.
- **Generatore casuale:** PCG32 con il metodo di Lemire e Fisher-Yates, verificato sulla sequenza di riferimento ufficiale dell'algoritmo.
- **Partita:** `create_game` da due liste e un seed.
- **Spostamenti:** `move_objects` per gruppi, con un unico evento, riepilogo del cimitero incrementale e `check_invariants` dopo ogni spostamento nei test. Sono testati i sei casi di Moonshadow.

**Decisioni**
- **Contenuto di `cards.json`:** anche supertipi, fedeltà e il **retro completo** delle bifronti, perché Witch-Blessed Meadow è una terra giocabile. Il file è in Git, così i dati Oracle usati restano fissi.
- **Database delle carte:** si passa come parametro (`const CardDatabase&`). *Scartato:* un puntatore dentro `GameState`.
- **Spostamento di gruppo tutto o niente:** prima si controlla l'intero gruppo, poi si muove.
- **Token nel cimitero:** restano nella lista ma non contano nel riepilogo, finché l'azione di stato non li rimuove.
- **Simboli di mana:** il parser C++ rifiuta ibridi e phyrexiani, che non compaiono nel pool. Se una carta futura li porta, il caricamento si ferma con un errore invece di andare avanti in silenzio.

**Errori e correzioni**
- **API di Scryfall cambiata:** il bulk non è più un JSON unico (`download_uri`) ma JSON Lines compresso (`jsonl_download_uri`). Lo script ora gestisce entrambi i formati.
- **Test con `memcmp`:** confrontava due `GameState` byte per byte, ma i byte di riempimento (padding) non sono garantiti uguali dopo una copia. Sostituito con il confronto dei campi.
- **Avviso C4244 nascosto:** un `{0, true}` convertito in `pair<int8_t, bool>`. Non era visibile perché il comando `sed` che filtrava l'output della compilazione era rotto. Ora gli avvisi si contano su una compilazione da zero.
- **Nomi dei test accentati, di nuovo:** aggiunto un controllo in CMake che blocca la configurazione. Il primo tentativo non funzionava, perché `file(STRINGS)` senza `ENCODING UTF-8` spezza le righe proprio sui caratteri accentati; il controllo è stato provato prima di fidarsene.
- **Fine riga di `cards.json`:** era scritto con CRLF da Python su Windows; ora sempre LF.

---

## Passo 5: correzioni e pubblicazione, 9/11/2026

**Fatto**
- Rimossa dalla root la copia intermedia della specifica.
- Regola nel `CLAUDE.md`: mai `git add -A` alla cieca, perché il repository è pubblico.
- **Grimorio:** la posizione è obbligatoria (cima, fondo o mescolato) e c'è `shuffle_library` per fetchland e ciclo di Troll. I riferimenti si elencano dall'alto verso il basso: con il mulligan London le carte vanno in fondo nell'ordine scelto.
- **Per la pubblicazione:** README, licenza MIT, questo diario, workflow GitHub Actions (compilazione Release con avvisi trattati come errori, ctest, test Python).
- **Roadmap del README:** la prima versione era una ricostruzione, perché il diagramma delle fasi di `progetto.md` non era finito nel Markdown. È stata sostituita con la roadmap originale fornita dall'utente: fasi 0-4 con durate indicative part-time e controlli tra le fasi.

**Decisioni**
- **Posizione obbligatoria:** spostare carte nel grimorio senza indicarla è un errore. *Scartato:* "in cima" come valore predefinito, perché un default silenzioso sul mulligan avrebbe falsato le partite senza farsene accorgere.
- **Mescolamento:** con `Shuffle` ogni grimorio coinvolto si mescola una volta, dopo l'intero gruppo, in ordine di giocatore, così il risultato è deterministico.
