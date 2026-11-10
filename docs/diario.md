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

- **Pubblicazione:** repository pubblico https://github.com/GodzDanielito/mtg-matchup-simulator. Il primo workflow è passato: Release con MSVC 19.51 (Visual Studio 2026), nessun avviso con `/WX`, 53 test C++ e 16 test Python.

**Decisioni**
- **Email dei commit:** resta `dani8.paoli@gmail.com`, scelta esplicita dell'utente, sapendo che è visibile pubblicamente. *Scartata:* l'email noreply di GitHub con riscrittura della storia.
- **Posizione obbligatoria:** spostare carte nel grimorio senza indicarla è un errore. *Scartato:* "in cima" come valore predefinito, perché un default silenzioso sul mulligan avrebbe falsato le partite senza farsene accorgere.
- **Mescolamento:** con `Shuffle` ogni grimorio coinvolto si mescola una volta, dopo l'intero gruppo, in ordine di giocatore, così il risultato è deterministico.

**Errori e correzioni**
- **`gh` non installato:** il comando non era su questa macchina, quindi il `gh auth login` fatto altrove non valeva qui. Installato con winget; il login è stato fatto con il codice dispositivo, perché la riga `!` nell'estensione di VS Code non esegue comandi.
- **Push rifiutato:** il token non aveva il permesso `workflow`, obbligatorio per caricare file in `.github/workflows/`. Aggiunto con `gh auth refresh --scopes workflow`; il repository era ancora vuoto, quindi non è stato pubblicato niente a metà.
- **Previsione sbagliata:** era stato detto che `windows-latest` usa Visual Studio 2022. Il log mostra Visual Studio 2026, lo stesso compilatore usato in locale.
- **Avvisi del primo workflow:** `setup-uv@v6` girava su Node.js 20, che è deprecato, e la sua cache cercava file di dipendenze inesistenti. Passati a `setup-uv@v7` con `enable-cache: false`.

---

## Passo 6: turno, priorità, azioni di stato, mulligan, terre, 9/11/2026

Corrisponde al "passo 7 del motore" della richiesta.

**Fatto**
- **Regola nuova nel `CLAUDE.md`:** prima di implementare una carta si legge il testo Oracle aggiornato da Scryfall e lo si riporta nel test. Le terre usano il bulk dell'8/11/2026, 22:02 UTC.
- **Struttura del turno** (`engine/game`):
  - fasi e step;
  - chi gioca per primo salta la prima pescata;
  - una terra per turno;
  - stap e debolezza da evocazione azzerati all'inizio del turno del controllore;
  - riserva di mana svuotata a ogni cambio di step;
  - scarto fino a 7 carte nella cancellazione, con la nuova cancellazione prevista dalla 514.3a.
- **Priorità:** `PendingDecision`, `legal_actions` e `apply`. Il passaggio automatico avviene solo quando "passa" è l'unica azione; viene registrato nel `GameLog` come `AutoPass` ma non diventa una decisione.
- **Azioni di stato**, applicate insieme e ripetute finché non cambia più nulla: punti vita, pescata da grimorio vuoto, costituzione 0 o meno, danno letale, regola delle leggende (con la scelta del controllore), token fuori dal campo, annullamento dei segnalini +1/+1 e -1/-1.
- **Mulligan London:** decide prima chi gioca per primo; chi fa mulligan ripesca 7 carte; dopo aver tenuto mette in fondo una carta alla volta, nell'ordine scelto; massimo 7 mulligan.
- **14 terre in `cards/`**, una per file, con l'Oracle in testa al file e nei test: Plains, Swamp, Blood Crypt, Hallowed Fountain, 6 fetchland, Blackcleave Cliffs, Raucous Theater, Meticulous Archive e il retro di Witch Enchanter.
- **Pila per le abilità** (fetch attivata, sorvegliare innescato) e `RandomAgent`.
- **Robustezza:** 10.000 partite casuali con il controllo degli invarianti dopo ogni azione, in circa 4,8 s in Release (circa 2.070 partite al secondo, 185 decisioni per partita), più la verifica "stesso seed, stessa partita" su 50 seed.

**Decisioni**
- **Abilità di mana:** non sono azioni di priorità. Si attiveranno durante il pagamento dei costi (601.2g), quando esisteranno le magie. *Scartata:* esporle come azioni, che moltiplica le mosse inutili (tappare una terra a vuoto) per l'agente casuale e per la ricerca ad albero.
- **Carte identiche, una sola azione:** giocare, cercare, scartare o mettere in fondo una di due Plains è la stessa mossa. Fa eccezione la regola delle leggende, perché due copie possono differire per segnalini e stato.
- **Fetchland:** la terra cercata entra in campo e poi il grimorio si mescola. Se la terra chiede di pagare vita, la scelta avviene dopo il mescolamento; il risultato è equivalente, perché la scelta non dipende dall'ordine (nascosto) del grimorio.
- **"Non trovare" sempre disponibile** nella ricerca della fetch (701.19b).
- **Ordine dei trigger:** con le terre del pool due trigger simultanei dello stesso giocatore sono impossibili. Invece di scrivere codice non verificabile da nessun test, il motore lancia un errore esplicito; l'ordinamento scelto dal giocatore arriverà con le carte che lo rendono possibile.
- **Forza e costituzione con asterisco** (Nethergoyf): calcolarle è un errore esplicito finché la carta non sarà implementata. *Scartato:* uno 0 silenzioso.
- **Combattimento:** non esiste ancora, quindi dopo la dichiarazione degli attaccanti si va alla fine del combattimento (508.8, nessun attaccante dichiarato).
- **Registrazione delle carte esplicita** (`cards/all_cards.cpp`). *Scartata:* la registrazione automatica con inizializzatori statici, che in una libreria statica il linker può scartare senza dare errori.
- **Agente casuale:** usa il generatore dello stato, come richiesto. Conseguenza: le scelte dell'agente consumano numeri casuali, quindi agenti diversi con lo stesso seed producono anche mescolamenti diversi.
- **`check_invariants`:** non alloca memoria quando lo stato è coerente, perché viene chiamata dopo ogni azione e milioni di volte nel test di robustezza. Controlla anche la pila e la coerenza tra partita finita e decisione `GameOver`.

**Errori e correzioni**
- **Test del mulligan troppo fragili:** controllavano la dimensione della mano dopo che il motore era già avanzato, forse oltre una pescata. Ora si controllano il fondo del grimorio e la somma mano + grimorio.
- **Test della fetch che passava comunque:** confrontava il grimorio "prima" (con la carta cercata) e "dopo" (senza), quindi passava anche senza mescolare. Ora confronta lo stesso insieme di carte.
- **Sostituzione nello script di modifica dei test:** non era andata a segno; l'ha rivelato il controllo stampato dallo script stesso.

**Osservazione dal test di robustezza**
- Il 36% delle partite casuali finisce per punti vita, e sono **tutte** sconfitte di UW Blink. È coerente con le liste: le terre di UW possono costare fino a 30 vita (10 fetch, 4 Hallowed Fountain da 2, 4 Witch-Blessed Meadow da 3), quelle di Rakdos al massimo 16 (10 fetch, 3 Blood Crypt da 2). L'agente casuale paga vita senza criterio; quello euristico non dovrà farlo.

---

## Passo 7: lancio delle magie e prime carte, 9/11/2026

Corrisponde al "passo 8" della richiesta, preceduto da tre correzioni.

**Correzioni**
- **Fetch e sorvegliare in pila:** l'abilità delle fetchland (non di mana) e il sorvegliare (innescato) andavano già in pila, ma nessun test lo verificava. Aggiunti due test che controllano la pila nel momento in cui l'avversario può rispondere.
- **Generatore dell'agente separato:**
  - `RandomAgent(seed)` ha un suo PCG32 su uno stream dedicato, e `choose()` riceve lo stato in sola lettura;
  - la partita usa il suo generatore solo per la casualità delle regole;
  - test: due agenti con seed diversi, sulla stessa partita, hanno gli stessi grimori e le stesse mani, anche dopo un mulligan di entrambi.
  - Il motivo (numeri casuali comuni per confrontare agenti e mazzi) è in `docs/stato_di_gioco.md`, sezione 8.

**Fatto**
- **Lancio delle magie (601.2):** la carta va in pila, poi bersaglio, costo, abilità di mana e pagamento; chi lancia riceve la priorità. Istantanei e abilità a qualsiasi priorità; il resto solo in fase principale, a pila vuota.
- **Pagamento del mana** (`engine/mana`, soluzione descritta nella sezione 10 di `docs/stato_di_gioco.md`): classi di fonti per colori prodotti e vettori di conteggi; un'opzione per ogni insieme diverso di fonti rimaste; teorema di Hall per la fattibilità, cammini aumentanti per i colori; pagamento automatico (registrato come `AutoPay`) quando c'è un solo modo.
- **Risoluzione:** dalla cima quando entrambi passano; bersagli ricontrollati con `ObjRef`; magia neutralizzata (al cimitero, senza effetto) se il bersaglio è illegale.
- **Cinque carte** con Oracle verificato su Scryfall:
  - Lightning Bolt (qualsiasi bersaglio);
  - Thoughtseize (rivela, sceglie una carta non terra, 2 vita; `known_to`);
  - Street Wraith (ciclo pagando 2 vita; lanciabile come creatura);
  - Mishra's Bauble (guarda la cima, `known_to`, pescata ritardata al prossimo mantenimento);
  - Stitcher's Supplier (macina 3 entrando e morendo, un solo evento di gruppo).
- **Trigger ritardati e trigger di morte**, con il controllore noto per ultimo (`ZoneChange::controller`).
- **Robustezza:** 10.000 partite in circa 3,5 s in Release. Un controllo di copertura verifica che ogni carta nuova venga lanciata o attivata e che compaiano le decisioni nuove.

**Decisioni**
- **Lancio solo se completabile:** `CastSpell` è offerta solo se il lancio si può finire (tempi, bersagli, mana), così non serve mai annullare un lancio a metà (601.2).
- **Pagamento automatico con un solo modo**, come il passaggio automatico: nessuna scelta, nessun nodo di ricerca.
- **Bersagli identici, una sola azione:** due permanenti con stessa carta, controllore e stato danno un solo bersaglio. Planeswalker e battaglie non sono ancora bersagliabili, perché nessuno può essere in campo.
- **Trigger simultanei intercambiabili** (stesso effetto, stessa fonte, stesso bersaglio, per esempio due pescate di Bauble): vanno in pila senza chiedere l'ordine. Per trigger diversi resta l'errore esplicito, perché con queste carte non possono capitare.
- **Mescolare azzera `known_to`** per le carte del grimorio: dopo un mescolamento nessuno sa più dov'è una carta (Bauble seguito da una fetch). Le carte in mano restano note.
- **`event_serial`** nello stato: conta gli eventi di cambio zona. Permette ai test di verificare che una macinata di 3 carte sia **un** evento.
- **Thoughtseize** resta in pila durante la scelta della carta, e l'invariante della pila lo prevede.
- **Swampwalk** di Street Wraith arriverà con il combattimento.

**Errori e correzioni**
- **Test della seconda correzione:** con Arid Mesa in campo il giocatore 0 riceveva una decisione già nel mantenimento di Rakdos; il test ora passa le priorità fino alla fase principale.
- **Test di Thoughtseize:** controllava la mano dopo che il motore era già avanzato oltre una pescata.
- **Compilazione fallita non vista:** controllavo solo il numero di avvisi, quindi un test è girato sul binario vecchio. Ora si controlla anche il codice d'uscita della compilazione.
- **Test delle carte implementate:** affermava "nessuna carta non terra è implementata"; ora usa un elenco esplicito, da aggiornare a ogni carta.

**Osservazione dal test di robustezza**
- Con le magie, quasi tutte le partite casuali finiscono per punti vita (9.483 su 10.000), e 8.451 sono sconfitte di Rakdos. L'agente casuale sceglie a caso anche il bersaglio di Lightning Bolt, quindi spesso colpisce se stesso, e paga vita con Thoughtseize, ciclo, fetch e shockland. È un comportamento da agente casuale, non un dato sul matchup.

---

## Passo 8: combattimento, 9/11/2026

Corrisponde al "passo 9" della richiesta, preceduto da due verifiche.

**Verifiche**
- **Mishra's Bauble:** mancavano i test di due casi, ora aggiunti: attivata nel turno avversario (si pesca nel proprio mantenimento) e attivata nel proprio mantenimento (non si pesca in quello stesso). Il motore era già corretto.
- **Teferi:** nella sezione 11 di `docs/stato_di_gioco.md` ci sono i requisiti della carta: planeswalker bersagliabili da Lightning Bolt e attaccabili in combattimento, segnalini fedeltà, azione di stato 704.5i.

**Prima del codice**
- **Regole lette dalle Comprehensive Rules del 6/11/2026** (506-511, 702), scaricate da Wizards. La 510.1c attuale conferma che il danno di un attaccante con più bloccanti si divide liberamente: non ci sono più ordine di assegnazione né obbligo di danno letale.
- **Parole chiave verificate sul testo Oracle del pool:** servono anche attacco improvviso (Ocelot Pride) e protezione (Sanctifier en-Vec), che non erano nella richiesta; raggiungere no (solo Aang, rinviato).
- **Progetto nella sezione 13**, approvato con quattro aggiunte: step del danno da attacco improvviso, protezione completa, flag della vita guadagnata, test dei bloccanti spariti.

**Fatto**
- **Dichiarazione degli attaccanti:** una decisione per creatura, in ordine canonico. Tra creature identiche le scelte sono monotone (si sceglie quante attaccano); le scelte obbligate avvengono da sole. Gli attaccanti si tappano e ricevono `AttackedThisTurn`; il punto in cui scatteranno i trigger di attacco è pronto.
- **Dichiarazione dei bloccanti:** per attaccante, un bloccante alla volta in ordine canonico (uno per classe), poi "fine".
  - Minaccia e "almeno tre bloccanti" (Troll) diventano controlli locali.
  - Per gli attaccanti identici, il profilo dei bloccanti non può superare quello dell'attaccante precedente.
  - Un'opzione è offerta solo se esiste un completamento legale (ricerca esaustiva locale).
- **Danno:** step `FirstStrikeDamage` solo se serve (510.4); divisione tra più bloccanti con un'opzione per esito distinto; danno simultaneo; prevenzione per protezione; legame vitale; attaccante bloccato senza bloccanti che non colpisce.
- **Parole chiave e restrizioni:** volare, rapidità, legame vitale, attacco improvviso, minaccia, swampwalk, protezione (blocchi, danno, bersagli: Lightning Bolt non bersaglia Sanctifier), minimo di tre bloccanti di Troll. Sono dichiarate nei file delle carte, con il testo Oracle; delle otto creature non ancora implementate ci sono solo le parole chiave.
- **Vita guadagnata:** `gained_life_this_turn` per giocatore. Tutti i guadagni di vita passano da `gain_life`.
- **Test esaustivo:** su 6 configurazioni scelte e 40 casuali, le dichiarazioni raggiunte dal motore coincidono con quelle generate per forza bruta, con regole riscritte nel test. Sono 269 dichiarazioni di attacco e 675 di blocco (495 con blocchi multipli), con un percorso per ogni dichiarazione. Anche la divisione del danno è confrontata con la forza bruta.
- **Robustezza:** 10.000 partite UW Blink contro Rakdos (3,2 s in Release) e 10.000 Rakdos contro Rakdos (4,5 s), con gli invarianti anche sul combattimento e la copertura di attacchi, blocchi e divisioni del danno.

**Decisioni**
- **Parole chiave delle carte non implementate:** si dichiarano ora (Ocelot Pride, Sanctifier en-Vec, Moonshadow, Troll, Starfield Shepherd, Quantum Riddler, Detective's Phoenix, Solitude), così i test del combattimento usano carte vere. Le carte restano "non implementate" (non si lanciano).
- **Controllo incrociato con Scryfall:** `cards.json` ora contiene il campo `keywords` di Scryfall, e un test fallisce se una parola chiave di combattimento dichiarata a mano non coincide.
- **Classi di creature identiche:** contano carta, controllore, danno, segnalini e flag rilevanti. Per attaccare conta anche "ha già attaccato" (Fear of Missing Out); la debolezza da evocazione no.
- **Serie di robustezza Rakdos contro Rakdos:** UW Blink non ha ancora creature lanciabili, quindi senza il mirror i blocchi non sarebbero mai coperti.
- **`GameState` passa a 7.304 byte**, vicino al tetto di 8 KB del test, per lo stato del combattimento (candidati, scelte, assegnazioni del danno). Se servirà spazio si potranno ridurre le capacità fisse del combattimento.

**Errori e correzioni**
- **Sezione 7 del documento:** diceva che nel pool non c'è attacco improvviso; Ocelot Pride ce l'ha. Corretto prima di scrivere il codice.
- **Test di minaccia:** si aspettava tre opzioni (fine e i due bloccanti), ma corrette sono due. Partire dal secondo bloccante in ordine canonico non lascia nessuno per arrivare a due, e la coppia si costruisce in un solo ordine. L'errore era nel test, e il test ora spiega perché.
- **Supporto `block_with` nei test:** sceglieva i bloccanti nell'ordine scritto nel test invece che in quello offerto dal motore.
- **Controllo "una sola azione legale" nella robustezza:** in un primo momento valeva per tutte le decisioni, ma alcune restano decisioni anche con un'opzione (per esempio il mulligan dopo 7). Limitato a priorità e combattimento.
- **Script di modifica dei test:** un `\n` è diventato un vero a capo dentro una stringa C++. La compilazione fallita è stata vista subito, grazie al controllo del codice d'uscita introdotto nel passo precedente.

---

## Passo 9: Nethergoyf, Moonshadow, Dark Matter Manipulator, Troll, 9/11/2026

Corrisponde al "passo 10" della richiesta, preceduto da un'analisi della dimensione dello stato.

**Analisi della dimensione**
- **Ripartizione di `GameState`:** il test "Dimensione di GameState" ora stampa quanto occupa ogni parte e il costo di una copia, circa 40 ns in Release. Totale 7.400 byte, di cui oggetti 3.344, combattimento 1.690, pila 642, trigger in attesa 322, zone dei giocatori 564.
- **Proposte di riduzione presentate, non applicate:** si deciderà insieme se ridurre o alzare il tetto a 12 KB.

**Fatto**
- **Nethergoyf:**
  - forza e costituzione dai tipi nel cimitero del controllore, calcolate al volo;
  - lancio dalla mano;
  - fuga dal cimitero con {2}{B} ed esilio di un insieme minimale di altre carte con almeno 4 tipi, scelto una carta alla volta in ordine canonico (sezione 14 di `docs/stato_di_gioco.md`);
  - test esaustivo contro la forza bruta (29 insiemi);
  - nuovo invariante: la forza coincide sempre con il ricalcolo da zero dei tipi.
- **Moonshadow:**
  - entra con sei segnalini -1/-1;
  - trigger una volta per evento di gruppo con carte permanente nel cimitero del controllore;
  - i sei casi della sezione 12 sono diventati test veri, con Stitcher's Supplier che macina carte messe in cima al grimorio, fetch, ciclo, un token ucciso da Lightning Bolt;
  - il token che muore non lo fa scattare.
- **Dark Matter Manipulator:** macina 3 entrando; +2/+0 ogni sette carte nel cimitero. Il testo Oracle non contiene scelte o casi non gestiti, quindi l'ho implementato per intero.
- **Troll of Khazad-dûm:**
  - lancio per {5}{B};
  - swampcycling {1}: abilità dalla mano in pila, pagamento del mana, scarto, ricerca di una carta Swamp (Swamp, Blood Crypt, Raucous Theater) rivelata in mano, mescolamento.
- **Ordine dei trigger:** decisione `OrderTriggers` quando un giocatore ha trigger simultanei diversi. Prima era un errore esplicito, ora è raggiungibile: uno Stitcher's Supplier che muore con Moonshadow in campo.
- **Robustezza:** due serie da 10.000 partite (2,3 s e 4,2 s in Release), con la copertura delle carte nuove, della fuga, dell'ordine dei trigger e delle scelte di esilio.

**Decisioni**
- **Fuga solo con insiemi minimali.** Esiliare carte in più è legale ma sempre peggiore per Rakdos (cimitero più piccolo). È coerente con la regola "una carta alla volta fino alla soglia" approvata per la Phoenix, ma più precisa: niente duplicati da ordini diversi, niente carte superflue.
- **La raccolta delle prove della Phoenix seguirà lo stesso meccanismo** (insiemi minimali con valore di mana ≥ 6): annotato nella sezione 14.
- **Moonshadow che lascia il campo nello stesso evento** non fa scattare il proprio trigger: l'effetto non avrebbe nulla da fare.
- **`cost_of` e `after_mana`:** il costo di mana dipende dal modo di lancio (normale o fuga) o dall'abilità (ciclo della terra); i costi non di mana (scartare il Troll) si pagano dopo il mana.
- **`StackItem` registra il modo di lancio:** due byte in più per elemento, lo stato passa da 7.304 a 7.400 byte.

**Errori e correzioni**
- **Test preparati male:** in più test usavo tre Swamp, ma la lista di Rakdos ne ha due. In un altro, il lancio di Moonshadow con Swamp e Blood Crypt in campo aveva due modi di pagare, una decisione non prevista dal supporto del test. Corretti prima di scrivere il codice.
- **Script di modifica:** un blocco di testo passato alla shell con un apice all'interno non è stato interpretato. Da ora gli script di modifica si scrivono in un file temporaneo e si eseguono da lì.
- **Il vecchio test** "asterisco non ancora implementato" di Nethergoyf è stato sostituito dal test della 0/1 con il cimitero vuoto.

**Osservazioni dal test di robustezza**
- Con le creature nuove di Rakdos e nessuna creatura lanciabile per UW, le partite casuali UW contro Rakdos sono quasi tutte vinte da Rakdos (9.927 su 10.000).
- **Copertura fragile:** Troll of Khazad-dûm viene lanciato solo 2 volte su 10.000 partite, perché servono 6 mana con un agente casuale. La copertura passa ma è al limite: se una modifica futura cambiasse le partite, potrebbe scendere a 0.

## Passo 10: Fear of Missing Out, Detective's Phoenix, Flare of Malice, 9/11/2026

Corrisponde al "passo 11" della richiesta. Le riduzioni della dimensione decise insieme sono in un commit separato (`9ada7c2`).

**Dimensione dello stato (commit separato)**
- **Riduzioni applicate:** candidati, classi e scelte del combattimento a 32, pila a 16 (elementi e zona), 64 slot per i token, tetto del test a 12 KB. `GameState` occupa 7.576 byte e non cambia in questo passo: le tre carte hanno aggiunto solo valori di enumerazioni, nessun campo.
- **Capacità della pila controllata prima di spostare:** un errore non lascia mosse a metà.
- **Massimo di token osservato:** la robustezza lo registra. Oggi è 0 su 64, perché nessuna carta che crea token è ancora implementata: il dato diventerà utile con Ocelot Pride e Solitude.

**Fatto**
- **Fear of Missing Out:**
  - entrando scarta una carta, poi pesca (decisione `DiscardCard`); con la mano vuota pesca e basta;
  - trigger di delirium al primo attacco del turno: il bersaglio si sceglie quando il trigger va in pila; alla risoluzione stappa la creatura e aggiunge un combattimento dopo questa fase;
  - test: due FOMO danno due combattimenti aggiuntivi; nel combattimento aggiuntivo nessuna delle due fa scattare di nuovo il trigger; con il bersaglio reso illegale (Lightning Bolt in risposta) o il delirium perso prima della risoluzione non c'è nessun combattimento aggiuntivo.
- **Trigger con bersaglio:** vanno in pila uno alla volta; se hanno un bersaglio il controllore lo sceglie subito. Senza bersagli legali il trigger si toglie dalla pila (603.3d).
- **Detective's Phoenix:**
  - lancio normale, e conferimento per {R} più prove 6, dalla mano o dal cimitero (dal cimitero solo conferita);
  - prove scelte tra gli insiemi minimali di valore di mana (multinsiemi); la Phoenix in pila non conta;
  - con il bersaglio illegale si risolve come creatura; se la creatura incantata muore, la Phoenix resta in campo come creatura, senza cambiare oggetto;
  - +2/+2, volare e rapidità alla creatura incantata; niente conferimento su Sanctifier en-Vec (protezione dal rosso); nel cimitero conta due tipi;
  - test esaustivo delle prove contro la forza bruta su tre cimiteri (14 insiemi).
- **Flare of Malice:** costo alternativo con il sacrificio di una creatura nera non token. Il sacrificio è una morte, quindi Stitcher's Supplier macina. L'avversario sacrifica la creatura o il planeswalker di valore di mana più alto e sceglie lui tra i pari.
- **Nuovi invarianti:**
  - quando un giocatore ha la priorità, ogni Phoenix conferita in campo è attaccata a una creatura in campo;
  - in pila una carta è conferita solo se lanciata con il conferimento;
  - la forza di Nethergoyf tiene conto del bonus delle Aure.
- **Robustezza:** 10.000 partite UW Blink contro Rakdos (2,9 s in Release) e 10.000 Rakdos contro Rakdos (7,0 s), invarianti dopo ogni azione, stesso seed stessa partita. La copertura richiede ogni modo di lancio delle tre carte, la scelta dello scarto, i sacrifici per costo e per effetto, il trigger di FOMO (6.866 bersagli scelti nella prima serie) e gli attacchi nei combattimenti aggiuntivi (1.458).
- **Test:** 23 nuovi, 192 in tutto.

**Scelte fatte da solo**
- **Il conferimento è un modo di lancio** (`CastMethod::Bestow`) e la carta in pila porta già il flag `Bestowed`: lo stesso flag vale in pila e in campo.
- **Prove generalizzate:** lo stesso codice della fuga, con la misura (tipi o valore di mana) come parametro. Gli insiemi minimali superano la decisione di revisione "una carta alla volta fino a 6", che avrebbe dato percorsi duplicati e carte superflue.
- **Bersagli dei trigger:** due creature identiche sono lo stesso bersaglio solo se le bersagliano già lo stesso numero di abilità in pila. Con due FOMO, "la stessa creatura due volte" e "una ciascuno" restano due scelte.
- **Una creatura con un'Aura conferita non è identica a una senza:** il numero di Aure conferite entra nella chiave di classe del combattimento e nel confronto tra permanenti.
- **Aura staccata:** l'azione di stato si applica prima delle morti; non cambia zona, quindi l'ordine non conta.
- **Flare of Malice:** tra copie identiche nessuna decisione; un'Aura conferita non è una creatura e non si sacrifica; mentre l'avversario sceglie, Flare resta nella zona pila, come Thoughtseize.

**Errori e correzioni**
- **Test preparati male:** tre test della Phoenix mettevano il Troll nel cimitero *dopo* `start_game`. Il motore avanza fino alla prima decisione: senza nulla da fare nella fase principale era già arrivato alla dichiarazione d'attacco. Nuovo supporto `staged_with_graveyard`, che prepara il cimitero prima dell'avvio.
- **Bug trovato rileggendo il codice:** `add_unique` scartava le azioni doppie confrontando solo la definizione, non la zona. Con una Phoenix in mano e una nel cimitero si offriva un solo conferimento. Prima il test che lo riproduce, poi la correzione.
- **Errore di processo:** avevo applicato quella correzione prima di vedere il test fallire. L'ho annullata, ho verificato il rosso, poi l'ho riapplicata.
- **Prestazioni:** la prima enumerazione degli insiemi minimali ricostruiva un vettore a ogni nodo, e la robustezza saliva a 33 s. Riscritta senza allocazioni nei nodi e con lo stesso ordine di visita, quindi le stesse partite: 10 s.
- **Azioni di stato:** il controllo della costituzione trattava come creatura anche una Phoenix conferita. Oggi non ha effetti visibili; corretto per coerenza.
- **Copertura incompleta:** la robustezza contava i lanci di FOMO ma non verificava il trigger né i combattimenti aggiuntivi. Aggiunti i due contatori.
- **Tempo del passo 9 non riproducibile:** il diario riporta 4,2 s per il mirror, ma lo stesso binario oggi impiega 7,2-7,9 s. Misurati in sequenza sulla stessa macchina, il motore attuale costa per decisione quanto il precedente; le partite però sono più lunghe (81 e 99 decisioni per partita, prima 72 e 89). D'ora in poi i confronti di tempo si fanno solo con misure in sequenza.

## Passo 11: agente euristico di Rakdos e goldfish, 9/11/2026

Corrisponde al "passo 12" della richiesta. Comprende anche il commit separato `d1791d4` (tabella del fattore di ramificazione e requisiti di Teferi), che non aveva una voce sua.

**Prima del passo (commit `d1791d4`)**
- **Fattore di ramificazione:** la robustezza registra il numero di opzioni per tipo di decisione (massimo, media, quante decisioni). I massimi osservati: priorità 10, bersaglio di una magia 11, Thoughtseize 8, esilio per fuga e prove 10, blocco 7, divisione del danno 70 (media 4,4).
- **Requisiti di Teferi:** Flare of Malice può costringere UW a sacrificare Teferi (valore di mana 3) quando le sue altre creature costano meno; il test di Teferi dovrà coprire i tre casi.

**Documento dell'agente** ([docs/agente_rakdos.md](agente_rakdos.md))
- Scritto, rivisto e approvato prima del codice. Le decisioni della revisione sono nella sezione 17, le scelte fatte implementando nella sezione 18.
- **Due correzioni di regole prima dell'implementazione:**
  - Ocelot Pride: il trigger scatta all'inizio della sottofase finale e si risolve anche senza Ocelot; per impedire il gatto lo si uccide prima, in combattimento o nella fase principale avversaria;
  - la carta lanciata dal cimitero esce anche lei: conferendo la Phoenix dal cimitero spariscono creatura e incantesimo, con la fuga sparisce la creatura di Nethergoyf. Il controllo del delirium (prove) e dei 3 tipi (fuga) si fa dopo l'esilio **e** dopo l'uscita della carta. Un test per ciascun caso, con il controllo positivo.
- **Candidati ai test A/B:** mani da 7 con una terra; al turno 1, creatura da un mana contro Thoughtseize.

**Motore**
- **Registro degli eventi:** il `GameLog` registra cambi di zona, variazioni di vita con la fonte e la causa, inizio degli step, e il soggetto delle decisioni (la creatura che attacca). Il motore lo raggiunge con un puntatore `thread_local` valido durante `start_game` e `apply`. Lo stato non cambia: 7.576 byte.
- **`graveyard_exile_sets`:** gli insiemi di esilio di fuga e prove, valutabili prima di lanciare. Un test li confronta con quelli offerti davvero.
- **`combat::damage_division`:** le ripartizioni del danno della decisione in sospeso.

**Agente e goldfish**
- **`agents/rakdos_agent.cpp`:** una funzione per regola; ogni scelta registra la sua regola. Un test costruisce a ogni decisione uno stato gemello, con la mano di UW scambiata con il suo grimorio e i grimori rimescolati, e verifica che la scelta non cambi.
- **`agents/goldfish_agent.cpp`:** l'avversario che non fa nulla.
- **`tools/goldfish`:** 100.000 partite; una riga JSON per partita, il riepilogo, il registro completo delle prime 100.
- **`tools/replay`:** il registro in JSON Lines, una riga per evento.
- **Lista `config/decks/goldfish_plains.json`:** 60 Plains, verificata dai test `[config]`.
- **Test:** 55 nuovi, 251 in tutto: 38 dell'agente, 8 del registro, 8 del goldfish e del JSON Lines, 1 degli insiemi di esilio. Esteso il test della divisione del danno.

**Risultati del goldfish** (100.000 partite, 25,7 s in Release, circa 3.900 partite al secondo)

| | Play | Draw |
| --- | --- | --- |
| Vinte entro 15 turni | 50.000 su 50.000 | 49.998 su 50.000 |
| Turno della vittoria: media, mediana | 4,94; 5 | 4,71; 5 |
| Vittoria entro il turno 4 / 5 | 34,3% / 80,3% | 45,0% / 88,2% |
| Delirium: mediana | 3 | 2 |
| Delirium entro il turno 2 / 3 | 46,6% / 65,9% | 54,2% / 73,5% |
| Delirium mai (comprese le vittorie prima) | 12,3% | 9,3% |
| Mani da 7 / 6 / 5 | 58,6% / 26,7% / 14,7% | 58,7% / 26,7% / 14,6% |

- Entrambe le soglie d'allarme sono rispettate: mediana della vittoria tra 4 e 7, delirium tra 2 e 4.
- **Decisioni predefinite:** 0 su 7.965.018. Il 51% delle decisioni è "priorità: niente da fare ora": l'agente ha un'azione legale (ciclo, fetch, Bolt) ma la regola dice di aspettare.
- **Variante Bolt in faccia appena possibile, stessi seed:** la vittoria arriva in media 0,069 turni prima in play e 0,062 in draw (±0,003). È più veloce nell'8% delle partite e più lenta nell'1,7%. Il delirium arriva in media 0,07-0,10 turni prima.

**Scelte fatte da solo** (sezione 18 del documento)
- **Turno del delirium:** se arriva nel turno avversario, conta il turno successivo di Rakdos. Si legge dal registro, spostamento per spostamento.
- **Mulligan a 6:** le terre si contano dopo aver messo in fondo.
- **"Il mana serve in questo turno":** quante giocate previste si pagano con la terra in più e senza.
- **Phoenix:** lancio normale quando le prove costerebbero il delirium.
- **Bolt in faccia con 6 vita o meno:** nella seconda fase principale o nella sottofase finale avversaria.
- **Non implementati:** la fuga per un attacco letale (Nethergoyf non ha rapidità) e Teferi tra i bersagli di Bolt.

**Errori e correzioni**
- **Regole sbagliate nel documento** (correzioni della revisione): i tempi di Ocelot Pride, e la carta lanciata che esce dal cimitero.
- **Test del turno del delirium sbagliato:** confrontava il registro con lo stato dopo ogni decisione. Una decisione può risolvere una magia e poi arrivare da sola al turno dopo, quindi il turno dello stato era sbagliato in 10 partite su 100. Il test ora verifica che il turno del registro cada tra il turno prima e quello dopo la decisione; i due valori coincidono nella maggior parte delle partite.
- **Ciclo del Troll a vuoto:** con il grimorio senza terre Swamp, il Troll ciclava e la ricerca finiva su "predefinito". Trovato con il conteggio delle regole. Prima il test, poi la condizione nuova.
- **Turno sbagliato nel riepilogo del cimitero:** il registro JSON Lines usava il turno della voce successiva (lo scarto di fine turno finiva nel turno dopo). Prima il test, poi la correzione.
- **Test preparati male, corretti prima del codice:** nel test del Troll sei terre in campo facevano lanciare il Troll per sei mana. Nel test dell'esilio della fuga mi aspettavo 5 tipi rimasti, ma il conto a mano ne dà 6.
- **Verifica del rosso per gli helper del motore:** test e codice sono stati scritti insieme. Il rosso è stato verificato neutralizzando temporaneamente il codice nuovo.

**Osservazioni**
- **Il 41% delle mani va a mulligan.** Con 17 terre su 60, una mano da 7 ha da 2 a 4 terre solo nel 63% dei casi e una sola terra nel 27%. La regola "una terra: sempre mulligan" pesa più del previsto, ed è il primo test A/B da fare.
- **Flare of Malice non si lancia mai nel goldfish,** come previsto: l'avversario non ha creature.
- **La fuga di Nethergoyf è rara** (0,02 per partita): le partite finiscono prima che il cimitero basti.
- **Danno per carta:** Nethergoyf 30%, Dark Matter Manipulator 25%, Moonshadow 24%, FOMO 8-9%.
- **La variante di Bolt è più lenta nell'1,7% delle partite:** il mana speso per Bolt nella prima fase principale e il Bolt che non resta per il colpo finale cambiano la partita.

## Passo 12: mulligan con una sola terra, chiusura della fase 1, 9/11/2026

Corrisponde al "passo 13" della richiesta. La fase 1 (motore e goldfish) è chiusa; il README segna la fase 2 in corso.

**Nota per la ricerca ad albero.** Il registro degli eventi (passo 11) costa tempo e memoria: ogni cambio di zona, ogni variazione di vita e ogni step scrivono una voce. Nelle simulazioni interne della ricerca ad albero (ISMCTS) il registro va **spento**: `start_game` e `apply` non registrano nulla quando il `GameLog` passato è assente (`nullptr`), quindi basta non passarlo. Le partite vere del matchup e il goldfish lo passano; i nodi della ricerca no.

**Regola cambiata.** Rakdos Delirium tiene mani con una sola terra grazie alle carte che cercano carte: è la pratica dei giocatori veri ed è il motivo delle 17 terre. Tre varianti, con `--mulligan A|B|C`:
- **B (nuova predefinita):** con 7 o 6 carte si tiene una mano con una sola terra se ci sono almeno due carte tra Street Wraith, Mishra's Bauble e Troll of Khazad-dûm e una creatura da un mana lanciabile con quella terra (una fetch conta come terra che cerca la Swamp);
- **C:** come B, ma basta una di quelle carte;
- **A:** una terra, sempre mulligan; resta come confronto.

**Fatto**
- Test prima del codice: 13 test nuovi dell'agente (B e C tengono, B e C rifiutano, fetch, zero terre, mano da 6, mano da 5, mani da 2-4 terre invariate, carte in fondo) e 4 del goldfish. Prima rossi (6 falliti), poi verdi.
- `RakdosAgentOptions::mulligan` (predefinito B); `MulliganVariant`.
- Il goldfish registra per ogni partita le scelte di mulligan (`MK`) e la mano tenuta; la riga JSON le riporta.
- File di uscita per variante: `partite_A.jsonl`, `replay_B.jsonl`, `riepilogo_C.txt`, con `_bolt_faccia` in coda per la variante di Bolt.
- 268 test in tutto (251 + 17), tutti verdi in Release.

**Risultati** (100.000 partite per variante, stessi seed; dettaglio in `docs/agente_rakdos.md`, sezione 19)
- **Controllo:** la variante A riproduce partita per partita il goldfish del passo precedente (100.000 su 100.000, 19 campi).
- **B contro A:** la vittoria arriva in media 0,075 turni prima in play e 0,050 in draw (intervalli di confidenza appaiati: [−0,080; −0,071] e [−0,054; −0,046]); entro il turno 5 si vince il 2,6 e l'1,8 punti percentuali più spesso. Sulle sole partite in cui la decisione cambia (11,6% e 11,8%): −0,50 turni in play, −0,33 in draw.
- **C contro B:** differenza minima e mista. In play −0,010 turni, ma −0,33 punti entro T4 e +0,36 entro T5; in draw +0,0007 turni e −0,40 punti entro T4. B resta la predefinita.
- **Mani da 7 / 6 / 5 carte** (play): A 58,6 / 26,7 / 14,7%; B 67,5 / 23,9 / 8,7%; C 78,0 / 18,4 / 3,6%.
- **Partite non vinte entro il limite:** 0 in play, 2 in draw (A e B), 1 in draw (C), su 50.000 ciascuna.
- **Il goldfish misura solo la velocità.** Nel confronto non entrano i rischi di una mano con una sola terra contro un avversario che interagisce; il confronto va ripetuto contro UW Blink.

**Scelte fatte da solo**
- **Carte in fondo con una sola terra (B e C):** le carte che cercano carte (finché ne restano due o meno) e la creatura da un mana (se è l'unica) non vanno in fondo. Senza, la classifica del valore metterebbe in fondo il Troll e Street Wraith, cioè proprio le carte che giustificano la mano; nelle mani da 6 la regola del mulligan sarebbe incoerente con quella delle carte in fondo. La mano si valuta **dopo** aver simulato le carte in fondo.
- **Variante A intatta:** la regola di partenza è il percorso di prima, bit per bit, così il confronto è pulito.
- **"Partite in cui la decisione di mulligan cambia":** quelle in cui le stringhe `M`/`K` delle due varianti differiscono. Con gli stessi seed si può dire esattamente dove: in tutte B tiene dove A fa mulligan, mai il contrario (verificato da un test).

**Errori e correzioni**
- **Script lanciato a vuoto.** Un `cat > /dev/null` messo per sbaglio prima di un heredoc ha consumato lo script di modifica di `goldfish_main.cpp`: la compilazione "riuscita" era del vecchio codice e le tre esecuzioni sono uscite con codice 2 (opzione sconosciuta). Notato dal codice di uscita; ricreato lo script e rieseguito.
- **Un comando durato oltre il limite** (compilazione Release più `ctest` insieme) è stato spostato in background; nel frattempo il test di robustezza risultava 10 volte più lento (31 e 49 s) per la concorrenza sulla macchina. Rieseguito da solo: 3,0 e 4,1 s, come prima. Regola: compilare e provare in comandi separati.
- **Confronti con numeri di prima:** i risultati di Bolt in faccia del passo precedente erano con la regola A del mulligan. Con B vanno rifatti se servono; non ho tenuto i file vecchi (generati, fuori da Git).

## Passo 13: prime carte di UW Blink, inizio della fase 2, 9/11/2026

La richiesta lo chiama passo 13, come la numerazione del diario; il messaggio del commit precedente (mulligan) portava già "Passo 13", per questo il commit di questo passo dice "Fase 2, passo 13".

**Mani rifiutate per motivo (variante B), senza modifiche all'agente.** Il goldfish scrive per ogni mano rifiutata il motivo, preso dalla regola che l'ha rifiutata; le terre nella mano pescata separano "zero terre" da "cinque o più" (le carte in fondo tolgono terre solo dalle mani con più di tre). Su 100.000 partite, mani da 7 in play: rifiutate il 32,5%, di cui zero terre 25,9%, una terra senza abbastanza carte che cercano 55,1%, cinque o più terre 4,9%, nessuna creatura da 1-2 mana 14,2%, altro 0. In draw i valori sono quasi uguali. Dettaglio e mani da 6 in `docs/agente_rakdos.md`, sezione 20, con l'annotazione per il futuro test A/B nel matchup: tenere le mani con 2-4 terre, Thoughtseize e Bolt ma senza creature economiche (il 4,6-4,7% delle mani da 7). Rigiocato dopo le modifiche al motore, il goldfish dà un file identico, partita per partita.

**Fatto**
- Sei carte con tutte le loro abilità, testo Oracle da Scryfall del 9/11/2026 e regole dalle Comprehensive Rules in vigore dal 6/11/2026 (file di testo `MagicCompRules 20261106.txt`): Ocelot Pride, Guide of Souls, Witch Enchanter (il fronte; il retro c'era già), Recruiter of the Guard, Starfield Shepherd, Sanctifier en-Vec. Due file nuovi in `cards/` (Guide of Souls, Recruiter of the Guard).
- Motore: ricerca nel grimorio con un filtro e ricerca facoltativa; trigger della sottofase finale; trigger d'attacco "whenever you attack"; costo facoltativo durante la risoluzione (`PayOptionalCost`) e trigger riflessivo con bersaglio "creatura attaccante"; bersaglio "artefatto o incantesimo di un avversario"; energia; token creati come evento di gruppo; turno di arrivo nella zona (`zone_turn`); ascesa e benedizione della città; warp con trigger ritardato alla prossima sottofase finale e lancio dall'esilio; sostituzione di Sanctifier in `move_objects`. Dettagli in `docs/stato_di_gioco.md`, sezione 17.
- Test prima del codice: 27 test delle carte (`tests/test_uw_cards.cpp`), prima tutti rossi (26 su 26, compilati con le sole enumerazioni nuove), poi verdi; 1 test di combattimento e 2 del goldfish. 298 test in tutto (268 + 30), tutti verdi in Release.
- Robustezza: le carte nuove nella copertura (lanci, warp, lancio dall'esilio, energie pagate, bersagli di Witch Enchanter, ricerche, benedizione della città).

**Benedizione della città.** Dal testo Oracle: l'ascesa dà la benedizione a chi controlla dieci o più permanenti (702.131b: "any time", per il resto della partita). Ce l'ha Ocelot Pride stessa, e su tutte le 43 carte delle liste è l'unica con l'ascesa (controllato sul bulk di Scryfall). La benedizione quindi si ottiene: con Ocelot Pride in campo e dieci permanenti, terre e token compresi. La parte delle copie è implementata per intero; nelle 10.000 partite casuali UW contro Rakdos la benedizione arriva in 1.528 partite.

**Robustezza** (Release, da sola): UW contro Rakdos 10.000 partite in 4,07 s, Rakdos contro Rakdos in 4,34 s; suite completa con `ctest` 13,5 s. Massimo di token in campo insieme: **31 su 64**. UW: 10.389 Ocelot Pride e 10.389 Guide of Souls lanciate, 5.832 Starfield Shepherd con il warp e 1.935 rilanciate dall'esilio, 2.615 volte tre energie pagate, 715 bersagli di Witch Enchanter.

**Scelte fatte da solo**
- **Il trigger di entrata di Sanctifier en-Vec** ("exile all cards that are black or red from all graveyards") non era nella richiesta, che chiedeva la sostituzione; l'ho implementato perché nessuna carta entra nel pool senza tutte le sue abilità.
- **Trigger di Guide of Souls accorpati:** N creature che entrano insieme danno un solo trigger da N vite e N energie per ogni Guide. Stesso risultato, nessuna scelta tra trigger identici; senza, i gatti copiati riempivano la pila (vedi sotto).
  - *Nota della revisione:* il risultato è identico **nel pool**, ma per le regole sono N trigger separati. Un effetto che neutralizza un'abilità innescata li distinguerebbe: Consign to Memory ("Counter target triggered ability or colorless spell", con replicare) ne neutralizzerebbe uno solo, o tanti quante le copie. Nel matchup Consign to Memory è di UW e non ha motivo di neutralizzare i trigger della propria Guide, e Rakdos non neutralizza abilità; se una carta futura può farlo, i trigger vanno separati di nuovo (e servono capacità adeguate).
- **Ascesa controllata prima di ogni priorità e a metà del trigger di Ocelot Pride**, non dopo ogni singolo evento: è l'unico punto del pool in cui un permanente nuovo può servire nella stessa risoluzione.
- **"You may search":** un'opzione in più che non cerca e non mescola, distinta da "cerca e non trova nulla", che mescola.
- **Warp:** il trigger ritardato tiene l'`ObjRef` del permanente; se il permanente ha lasciato il campo non fa nulla. Dall'esilio si lancia solo per il costo di mana, da un turno successivo.
- **Il gatto lo registra Ocelot Pride** (`cards/ocelot_pride.cpp`); il database dei test non lo aggiunge più a parte.
- **Sostituzione anche negli spostamenti di prova:** passa da `move_objects` come tutto il resto, quindi le preparazioni dei test mettono Sanctifier in campo dopo aver riempito i cimiteri.

**Errori e correzioni**
- **Bug di capacità trovati dal test di robustezza**, ognuno riprodotto prima da un test:
  - più di 32 creature per lato in combattimento (partita 2043): i gatti raddoppiano con la benedizione. Test: 44 gatti attaccano e 20 bloccano. `kMaxCombatants` sale a 124, tutte le creature che un giocatore può controllare; le assegnazioni di danno salgono a 308 e scendono a 4 byte l'una. `GameState` passa da 7.576 a 10.760 byte (tetto 12 KB);
  - più di 16 trigger in attesa (partita 8357): decine di gatti che entrano insieme con Guide of Souls in campo. Test: 22 gatti, 22 vite e 22 energie. Corretto accorpando i trigger della stessa Guide.
- **Errore di processo del passo 10:** le riduzioni della dimensione avevano fissato a 32 le capacità del combattimento nello stesso commit (`9ada7c2`) in cui i token salivano a 64. Il caso peggiore dei token valeva anche per il combattimento, e non l'avevo ricontrollato. Allora non si vedeva: nessuna carta creava token.
- **Test preparati male, corretti prima del codice:** Ocelot Pride senza debolezza da evocazione può attaccare al turno 1, e la decisione di attacco interrompeva l'helper che passa la priorità; con una Bauble o una fetch in campo la prima priorità arriva nel mantenimento, non nella fase principale. Helper corretti (`pass_until` non attacca e non blocca, `to_main`).
- **Due test esistenti da adattare alle regole nuove:** nel test della Phoenix contro Sanctifier la preparazione metteva Sanctifier in campo prima del cimitero, e il Troll finiva in esilio; nel test esaustivo dei blocchi Guide of Souls in attacco mette in pila il suo trigger prima dei bloccanti.
- **Un dato sospetto controllato:** Ocelot Pride e Guide of Souls risultavano lanciate esattamente 10.389 volte ciascuna. Un conteggio indipendente partita per partita dà gli stessi totali, con valori diversi in 6.810 partite su 10.000: è una coincidenza, non un errore di conteggio.
- **Strumenti:** un heredoc ha trasformato `\\n` in `\n` nello script di modifica dei test; corretto con stringhe raw in Python.

## Passo 14: Solitude, Ephemerate, Phelia, March, Thraben Charm, Seam Rip, 10/11/2026

**Fatto**
- Sei carte con tutte le loro abilità, testo Oracle da Scryfall del 9/11/2026 nei test, regole dalle Comprehensive Rules in vigore dal 6/11/2026 (evocazione 702.74, rimbalzo 702.88, lampo 702.8, durate "until" 610.3, trigger ritardati 603.7c, oggetti nuovi 400.7, X 107.3, riduzioni 118.7 e 601.2f, modali 700.2, bersagli facoltativi 115.6). Cinque file nuovi in `cards/`; Solitude aveva solo le parole chiave.
- Motore: lampo; evocazione con il trigger di sacrificio insieme a quelli di entrata; rimbalzo con il trigger ritardato al proprio mantenimento e il lancio senza pagare; X e riduzione del costo con carte bianche esiliate dalla mano; magie modali; bersagli "fino a uno" e "qualsiasi numero di giocatori"; esilio con ritorno alla sottofase finale (Phelia) e "until" (Seam Rip). I controlli dei bersagli passano da un contesto unico (`TargetContext`). Dettagli in `docs/stato_di_gioco.md`, sezione 18.
- Test prima del codice: 24 test nuovi (`tests/test_uw_spells.cpp`), prima rossi (24 su 24, compilati con le sole enumerazioni nuove), poi verdi. Cinque test esistenti adattati (sotto). 322 test in tutto, tutti verdi in Release.
- Robustezza: copertura delle carte nuove (lanci, evocazione, bersagli di ogni carta, "nessun bersaglio", i tre modi di Thraben Charm, X, rilanci dal rimbalzo, ritorni da Seam Rip) e un invariante nuovo sull'esilio "until".

**Robustezza** (Release, da sola): UW contro Rakdos 10.000 partite in 4,9 s, Rakdos contro Rakdos in 4,4 s; suite completa con `ctest` 13,6 s. Nelle partite casuali di UW: 8.157 Solitude evocate e 969 lanciate per intero, 5.299 Ephemerate e 1.846 rilanci dal rimbalzo, 3.457 esili di Phelia, 1.107 di Seam Rip e 127 ritorni, 2.508 volte "nessun bersaglio". Token in campo insieme: massimo 35 su 64. Il goldfish di Rakdos resta identico, partita per partita.

**Scelte fatte da solo**
- **X di March:** il motore offre solo i valori di mana dei bersagli possibili. Un X intermedio ha gli stessi bersagli dell'X più basso a un costo maggiore o uguale: è dominato, come gli insiemi di esilio non minimali della fuga.
- **Carte esiliate per March:** solo finché riducono il generico, in ordine canonico (stesse carte, stessa scelta), e solo se il costo resta pagabile. L'ordine delle scelte (X, bersaglio, carte, mana) differisce da 601.2b, che annuncia i costi aggiuntivi prima dei bersagli; l'esito è lo stesso.
- **Thraben Charm, cimiteri:** solo insiemi non vuoti di giocatori. Zero bersagli è legale, ma la magia non farebbe nulla.
- **Scelte obbligate automatiche** anche per il modo e per X, come per le altre decisioni del motore.
- **"Up to one" senza bersagli possibili:** il trigger va in pila senza bersaglio, senza decisione (prima un trigger senza bersagli si toglieva: per "fino a uno" non è corretto, anche se l'esito non cambia).
- **Solitude:** la vita è pari alla forza con cui la creatura lascia il campo, al suo controllore di allora.
- **Rimbalzo:** la decisione arriva solo se c'è un bersaglio legale; altrimenti la carta resta in esilio per sempre.
- **Seam Rip:** chi trattiene la carta in esilio sta nel campo `attached_to` dell'oggetto, che in esilio non serve ad altro; lo stato cresce solo di 192 byte (10.952).

**Errori e correzioni**
- **Trigger di entrata non raccolti:** avevo dichiarato i trigger di Solitude e di Seam Rip nelle regole delle carte, ma non li raccoglievo in `collect_triggers`. Lo hanno mostrato i test rossi, prima di qualsiasi partita.
- **Un "blocco" che era un'asserzione di Debug.** Una suite in Debug sembrava non finire mai. Era un'asserzione del runtime Debug (indice fuori dai limiti di un `std::array`), che apre una finestra di dialogo modale: senza nessuno a chiuderla, il processo resta fermo. L'ho trovata con un file temporaneo che manda i messaggi delle asserzioni su stderr (poi tolto). La causa era nel test: `back()` su una pila vuota.
- **Test preparati male (cinque volte la stessa causa):** quando nessuno ha azioni oltre a "passa", la priorità passa da sola e la pila si risolve. Così i trigger si risolvevano prima del controllo, o la partita arrivava al turno dopo (Moonshadow perdeva la debolezza da evocazione, UW pescava una carta bianca). Corretto con una fetch in campo, che dà un'opzione e tiene la priorità.
- **Una voce sbagliata nella copertura:** il rilancio dal rimbalzo passa dalla decisione `CastFromExile`, non da un'azione di lancio; lo conta un contatore a parte.
- **Test esistenti adattati alle regole nuove:** l'elenco delle carte implementate; Thoughtseize e lo scarto a 7 (UW ora può evocare Solitude e risponde, o ha priorità nel turno di Rakdos); il registro (un token nasce da `Zone::None`, non dal grimorio: con le carte nuove la partita casuale di seed 1 crea un gatto); il test esaustivo dei blocchi (Phelia in attacco chiede un bersaglio: nessuno).

## Test senza finestre di errore di Debug, 10/11/2026

**Fatto**
- `tests/crt_no_dialogs.cpp`: un oggetto statico che, prima del `main` di Catch2, manda su stderr asserzioni, errori e avvisi del runtime Debug di MSVC (libreria standard compresa), manda su stderr gli errori del runtime, toglie a `abort()` il rapporto a Windows Error Reporting (il messaggio resta) e spegne le finestre di Windows per i crash. Nessun effetto su GCC e Clang.
- Test prima del codice: `[runtime]` (`tests/test_runtime.cpp`) controlla la configurazione; prima era rosso (modalità di rapporto 4, cioè finestra, per errori e asserzioni). 323 test in tutto.
- Regola in `CLAUDE.md` (sezione Comandi).

**Verifica del comportamento** (con una sonda temporanea, non nel commit): un indice fuori dai limiti di `std::array` in Debug stampa "Assertion failed: array subscript out of range" e termina in 110 ms (codice 0xC0000409, la terminazione immediata della libreria standard); un puntatore nullo termina in 47 ms con il rapporto di Catch2 (SIGSEGV). Prima, la stessa asserzione lasciava il processo fermo in attesa di un clic: è il "blocco" del passo 14.

**Errori e correzioni**
- **Avviso solo in Release:** `_CrtSetReportMode` in Release è una macro vuota, e la variabile del ciclo risultava inutilizzata (in CI gli avvisi sono errori). Il ciclo è ora dentro `_DEBUG`.
- **Il runtime memorizza il vero handle di stderr,** non il valore simbolico `_CRTDBG_FILE_STDERR`: il test lo confronta con `GetStdHandle(STD_ERROR_HANDLE)`.
- **Un falso allarme sui tempi:** due esecuzioni di `ctest` in Release sono durate 122 s (la robustezza 52 s per serie invece di 5). Sembrava colpa del file nuovo: disattivandolo il tempo tornava a 4,4 s, ma provando le tre chiamate una alla volta, e poi di nuovo tutte insieme, il tempo restava 4,4 s, e la suite intera 12,9 s. Era lo stato della macchina, come al passo 12. Lezione: un tempo anomalo si rimisura prima di attribuirlo a una modifica.
- **La modalità di errore di Windows** passava il test anche senza il file: il processo la eredita da Git Bash. Sotto `ctest` o Visual Studio non è detto, e il file la imposta comunque.

**Witch Enchanter in mano è una carta bianca** (verifica chiesta in revisione, 10/11/2026). Fuori dal campo e dalla pila una bifronte ha solo le caratteristiche del fronte: Witch Enchanter è una creatura bianca anche se il retro è una terra incolore. Un test nuovo (`[witch][solitude][march]`) verifica che si possa esiliare per evocare Solitude e per ridurre il costo di March of Otherworldly Light, e che una Plains in mano non basti. Era già così: i colori vengono dal fronte in `config/cards.json`. Il test è passato subito; per controllare che non passasse a vuoto l'ho provato con una mutazione temporanea del motore (le bifronti non contano come bianche), che lo fa fallire. 324 test.

## Passo 15: Teferi, Quantum Riddler, Consign to Memory, Force of Negation, 10/11/2026

Con questo passo **tutte le carte delle liste di simulazione sono implementate**.

**Prima del passo** (commit separato `687987a`): il reindirizzamento su stderr delle asserzioni di Debug è permanente nell'eseguibile dei test, con la regola in `CLAUDE.md`; un test verifica che Witch Enchanter in mano sia una carta bianca, per l'evocazione di Solitude e per March.

**Fatto**
- Quattro carte con tutte le loro abilità, testo Oracle da Scryfall del 9/11/2026 nei test, regole dalle Comprehensive Rules in vigore dal 6/11/2026 (planeswalker 306, abilità di fedeltà 606, azione di stato 704.5i, danno 120.3c, attacchi 506.2-506.4, 508.1b e 510.1b, replicare 702.56, copie 707.10, neutralizzare 701.6, pescate sostituite 121.2a e 614.5, 101.2). Tre file nuovi in `cards/`; Quantum Riddler aveva solo volare.
- Motore: segnalini fedeltà, abilità di fedeltà, danno e azione di stato dei planeswalker; attacchi ai planeswalker; la statica di Teferi; le pescate in più di Quantum Riddler; la pila come bersaglio, con un id per ogni magia e abilità; neutralizzare; replicare e copie con nuovi bersagli; il costo alternativo di Force of Negation. Dettagli in `docs/stato_di_gioco.md`, sezione 19.
- Test prima del codice: 24 test nuovi (`tests/test_teferi_riddler_counters.cpp`), prima rossi, poi verdi. Il test di configurazione sulle carte implementate ora chiede **tutte** le carte delle liste: fallisce se una carta nuova entra in una lista senza le sue regole. 348 test in tutto, tutti verdi in Release.
- Robustezza: copertura delle carte nuove (lanci, Force of Negation gratis, abilità +1 e −3 di Teferi, attacchi a Teferi, bersagli in pila, Consign replicata, copie con un bersaglio nuovo).

**Robustezza** (Release, da sola): UW contro Rakdos 10.000 partite in 5,7 s, Rakdos contro Rakdos in 4,5 s; suite completa con `ctest` 14,3 s. Nelle partite casuali di UW: 1.271 Teferi (1.474 volte +1, 905 volte −3, 1.363 attacchi contro di lui), 7.009 Quantum Riddler con il warp e 1.953 rilanciati dall'esilio, 5.691 Consign to Memory (312 replicate, 174 copie con un bersaglio nuovo), 1.008 Force of Negation gratis e 466 pagate, 7.339 bersagli in pila. Token in campo insieme: massimo 33 su 64. Il goldfish di Rakdos resta identico, partita per partita.

**Scelte fatte da solo**
- **Fedeltà all'ingresso in `move_objects`:** è il punto in cui ogni carta entra in campo, quindi un planeswalker messo in campo da un test ha già i suoi segnalini.
- **Identità in pila:** un id unico per ogni magia e abilità. Un indice non basterebbe: se un'altra neutralizzazione toglie un oggetto più in basso, gli indici sopra scendono.
- **Replicare:** al più una copia per ogni altro bersaglio legale (oltre, una copia neutralizzerebbe di nuovo lo stesso bersaglio: dominata), e la scelta arriva dopo il bersaglio, come le carte di March. Le copie nascono anche se l'originale è stato neutralizzato, con le sue scelte: le informazioni sono salvate nel trigger.
- **Copie:** tutte create insieme dal trigger, poi una alla volta la scelta tra tenere il bersaglio e uno nuovo; senza alternative nessuna decisione.
- **Force of Negation sulla Phoenix conferita:** è una magia Aura, non una magia creatura (702.103b).
- **Teferi −3 senza bersagli possibili:** nessuna decisione; con il bersaglio diventato illegale l'abilità non si risolve e non si pesca (608.2b).
- **Due Quantum Riddler:** ognuno aggiunge una carta (614.5: ogni sostituzione si applica una volta, e la mano non cambia finché non si pesca).
- **Agente di Rakdos invariato:** non attacca mai Teferi e non lo bersaglia con Lightning Bolt (sceglie una creatura o il giocatore). È un limite da affrontare con le regole dell'agente nel matchup, prima nel documento e poi nel codice.

**Errori e correzioni**
- **Lo stesso errore dei test, tre volte:** con la priorità che passa da sola la pila si risolveva prima dei controlli (Quantum Riddler appena lanciato; la copia di Consign dopo il nuovo bersaglio), oppure `resolve()` andava oltre il punto voluto. Nel test di Phelia arrivava fino al ritorno nella sottofase finale e lo passava. Corretti con una fetch in campo, che tiene la priorità, o con un solo passaggio esplicito.
- **Il reindirizzamento su stderr ha funzionato subito:** l'errore della copia di Consign chiedeva `back()` a una pila vuota. In Debug è un'asserzione, che ora ha stampato il messaggio e chiuso il processo invece di bloccarlo.
- **Un'indentazione sbagliata nello script di modifica** (8 spazi contro 12) ha fermato lo script prima di scrivere: nessun file toccato a metà.
- **Lo stato si avvicina al tetto:** 11.584 byte su 12 KB. Con le carte tutte implementate non dovrebbe crescere ancora molto; se servirà spazio, i candidati sono le capacità della pila e dei trigger in attesa.

## Correzioni dell'audit 1, gruppo 1: l'agente di Rakdos contro Teferi, 10/11/2026

Risolti A-1, A-2, A-3 e A-7 dell'audit. Prima le regole in `docs/agente_rakdos.md` (sezioni 4, 7, 8, 11, 17, 18), poi dieci test nuovi (`[teferi]`), sette rossi prima del codice, poi il codice.

**Regole nuove**
- **Attacco (sezione 8):** senza un attacco letale sul giocatore, su Teferi va abbastanza forza da toglierlo, partendo dalle creature che non possono essere bloccate; il resto va sul giocatore. Con un attacco letale disponibile Teferi si ignora.
- **Bolt (sezione 7):** Teferi è il primo bersaglio, prima di Ocelot Pride, se Bolt lo toglie da solo o dopo il danno di combattimento previsto. Segnato come candidato a un test A/B.
- **Momento (sezioni 4, 7, 11):** con Teferi in campo, i Bolt e i Flare che l'agente lancerebbe nel turno di UW si spostano nella propria prima fase principale, prima del combattimento.
- **Flare (sezione 11):** Teferi come permanente di valore di mana più alto è un motivo valido, anche pagando con il mana nel proprio turno.
- **A-4 (mana aperto per Bolt)** resta com'è: annotato tra i test A/B da fare nel matchup (sezioni 4 e 17).

**Scelte fatte da solo** (sezione 18 del documento)
- **Il piano d'attacco è una funzione dello stato.** Il motore chiede gli attaccanti uno alla volta, ma "abbastanza forza su Teferi" è una decisione di gruppo. A ogni decisione l'agente ricalcola chi attacca (le regole di prima, estratte in `attack_rule`) e chi va su Teferi. Durante la dichiarazione lo stato non cambia, quindi il piano resta lo stesso senza memoria nell'agente.
- **Dentro un gruppo, la creatura più piccola che basta da sola, altrimenti la più forte.** Così non si spreca forza che andrebbe sul giocatore.
- **A parità, l'ultima nell'ordine del motore.** Tra creature identiche il motore vieta scelte decrescenti (nessuno, giocatore, planeswalker): le creature mandate su Teferi devono essere le ultime della classe.
- **Se tutte insieme non bastano, tutto sul giocatore.** Abbassare la fedeltà senza toglierlo lascia comunque a Teferi il +1.
- **"Attacco letale" per Teferi** conta come bloccanti tutte le creature stappate dell'avversario; la regola 3 per la singola creatura resta com'era (i bloccanti che possono fermare quella creatura).
- **Danno previsto su Teferi:** solo le creature che attaccheranno e non possono essere bloccate, contate quando l'agente ha la priorità nella prima fase principale. La Phoenix lanciata dopo con rapidità non conta.
- **Il Flare spostato preferisce il mana al sacrificio**, come nel turno di UW.

**Verifiche:** 358 test in Release in 14,3 s. Il goldfish (variante B, 100.000 partite) è identico byte per byte, contatori delle regole compresi: le regole nuove lavorano solo con Teferi in campo.

**Errori e correzioni**
- **Un test che osservava lo stato sbagliato.** Il primo `defender_of` leggeva `combat.attackers`, ma il combattimento si chiude dentro la stessa `apply` quando UW non ha bloccanti: l'elenco non si vedeva mai tra una decisione e l'altra, e la partita correva fino al turno 15 (un 1/1 che toglieva 17 punti vita). Ora il `Driver` dei test registra la carta di ogni decisione, e chi attacca chi si legge dalle scelte.
- **Uno script di modifica bloccato:** un `cat` senza input aspettava lo stdin. Fermato, e lo script riscritto con lo strumento di scrittura dei file.

## Correzioni dell'audit 1, gruppo 2: motore, 10/11/2026

Risolti M-1 e M-2 dell'audit.

**M-1: copie identiche e informazione nascosta.** Due copie della stessa carta in mano non sono più la stessa scelta se l'avversario ne conosce una sola. `add_unique` confronta anche `known_to`, e così ogni scelta sulla mano che passa da lì: lancio, scarto, carte in fondo, Thoughtseize. Anche `hand_exile_options` (le carte da esiliare per Solitude, Force of Negation e March) raggruppa per definizione **e** per chi conosce la carta. Due test nuovi `[hidden]`, rossi prima della correzione: due Solitude in mano con una rivelata danno due azioni di evocazione, e dopo aver lanciato quella non nota l'avversario conosce ancora la copia rimasta. Con due Ocelot Pride (una rivelata) l'esilio per evocare Solitude offre due scelte. Prima la scelta era unica, quindi obbligata, e il motore non la chiedeva nemmeno.

**M-2: capacità e dimensione dello stato.** La robustezza registra anche il massimo di elementi in pila, di trigger in attesa e di trigger ritardati, misurati a ogni punto di decisione come i token. Nelle 20.000 partite casuali: pila 8 su 16, trigger in attesa 5 su 16, ritardati 4 su 16 (UW contro Rakdos); 5, 5 e 3 nel mirror di Rakdos. Il tetto di `sizeof(GameState)` nel test sale da 12 a 16 KB; lo stato resta a 11.584 byte.

**Scelte fatte da solo**
- **La chiave di accorpamento è (definizione, `known_to`).** Per una carta in mano il proprietario la conosce sempre, quindi le varianti sono al più due: nota o non nota all'avversario.
- **Soglia d'avviso a tre quarti dei posti** per pila e trigger (un quarto deve restare libero). Scartata la soglia "metà libera": la pila arriva esattamente a 8 su 16, il controllo sarebbe passato al limite e sarebbe diventato rosso al primo cambio degli agenti senza un rischio vero. Le capacità restano 16: con le carte tutte implementate non serve spazio in più.
- **Misura ai punti di decisione, non dentro `apply`.** Un picco interno a una sola `apply` (trigger raccolti e subito messi in pila) non si vede. Per i trigger in attesa il caso che conta è comunque la decisione sull'ordine, dove restano tutti in attesa.

**Verifiche:** 360 test in Release in 14,4 s; goldfish identico byte per byte.

**Errori e correzioni**
- **Un indice oltre la fine in un test nuovo:** `find_cards` restituisce al più `n` carte (una, se non si dice), e il test ne chiedeva la seconda. In Debug l'asserzione della libreria standard è finita su stderr e ha chiuso il processo subito, senza finestre: `crt_no_dialogs.cpp` ha fatto il suo lavoro.

## Correzioni dell'audit 1, gruppo 3: test, 10/11/2026

Risolti T-1, T-2, T-3, T-4, T-5 e T-6. Tutti i test nuovi sono in `tests/test_audit1.cpp` (25 casi, etichetta `[audit1]`); il commento sopra ognuno nomina il problema dell'audit o la mutazione che deve farlo fallire.

**Casi limite (T-1, T-2, T-3)**
- **T-1:** Quantum Riddler lanciato con il warp e salvato da Ephemerate resta in campo per sempre. Il test arriva fino al terzo turno di UW, oltre due sottofasi finali sue. Stessa cosa con Phelia che lo esilia attaccando: torna come oggetto nuovo, il trigger del warp non lo trova più, e Phelia prende il segnalino.
- **T-2:** Phelia esilia il proprio Teferi a fedeltà 1 dopo il +1. Teferi torna con 4 segnalini e senza il segno dell'abilità già attivata (oggetto nuovo); Phelia prende il segnalino.
- **T-3:** partita a specchio, Rakdos contro Rakdos, con due Mishra's Bauble attivate nello stesso turno. Le due pescate ritardate scattano allo stesso mantenimento, una per giocatore. In pila va prima quella del giocatore attivo, sopra quella dell'altro, che si risolve per prima. Provato con ciascuno dei due come giocatore attivo.

**Copertura sottile (T-5).** Un secondo test per ognuna delle 22 regole che facevano fallire un solo test. Il secondo usa un'altra situazione e, dove possibile, un altro effetto osservabile. Esempi: per l'attacco improvviso Ocelot Pride attacca invece di bloccare; per la protezione la creatura è rossa invece che nera; per il bonus di Dark Matter Manipulator si guarda il danno inflitto invece della forza calcolata; per Ephemerate su un token si controlla che Guide of Souls non scatti. **Verifica:** rilanciate le 22 mutazioni, ognuna ora fa fallire almeno due casi di test distinti, il nuovo e l'originale (Bolt sui planeswalker quattro, il segnalino di Phelia tre).

**T-4** è il secondo test di `grimorio_vuoto`: con Quantum Riddler in campo, mano vuota e una carta nel grimorio, la pescata del turno diventa due e fa perdere.

**T-6.** Un mazzo di prova con un Fear of Missing Out (2/3) al posto di un Windswept Heath: Recruiter of the Guard non lo offre. La mutazione `recruiter_costituzione`, l'unica non rilevata dall'audit, ora fa fallire un test.

**Scelte fatte da solo**
- **Un file per i test dell'audit**, invece di sparpagliarli nei file delle carte: si vede in un posto solo che cosa copre cosa, e il legame con l'audit resta nel nome dei casi.
- **Mazzi di prova con 60 carte esatte:** `create_game` accetta solo liste legali, quindi la carta in più ne sostituisce una che nessun test usa (Windswept Heath). Così servono anche i due Teferi per la regola delle leggende con un planeswalker.
- **T-4 non era nella richiesta**, ma è lo stesso test che serviva come secondo per `grimorio_vuoto`.

**Verifiche:** 385 test in Release in 14,8 s; goldfish identico byte per byte.

**Errori e correzioni**
- **Il mazzo di prova da 61 carte** è stato rifiutato da `create_game` ("il main ha 61 carte invece di 60"): corretto sostituendo una carta invece di aggiungerla.
- **Una scelta che non arriva:** con una sola altra carta bianca in mano, l'esilio per evocare Solitude è obbligato e il motore non lo chiede. Il test cercava la decisione: tolta. È lo stesso comportamento che ha reso rosso il test di M-1.
- **Lo script delle mutazioni lascia in `build/` l'ultima versione mutata:** prima della suite finale i test sono stati ricompilati da zero sui file ripristinati.

## Chiusura delle correzioni dell'audit 1, 10/11/2026

`docs/audit_1.md` ha ora la sezione "Stato delle correzioni", e nella tabella ogni voce risolta porta lo stato. **12 problemi su 21 risolti** in tre commit: `6450acf` (agente contro Teferi), `dab17d2` (motore), `4a32670` (test). Restano aperti per scelta:
- **A-4**, il mana aperto per Bolt: va deciso con un test A/B nel matchup;
- **A-5 e A-6**: li misurano il confronto con la ricerca ad albero e i primi dati del matchup;
- **B-1 ... B-6**: approssimazioni di gravità BASSA, con lo stesso esito nel pool.

Il prossimo passo previsto resta l'agente di UW. Due test A/B sono già in coda per il matchup: Bolt su Teferi prima o dopo Ocelot Pride, e il mana aperto per Bolt.

## Rakdos contro Teferi: orologio, corsa o partita lunga, 10/11/2026

Revisione delle regole contro Teferi con un giocatore esperto: Teferi va spesso ignorato quando Rakdos può chiudere la partita; va tolto solo nelle partite stabilizzate o dopo che UW ha guadagnato vita. Prima il documento (`docs/agente_rakdos.md`: sezione 8, "Teferi in campo: corsa o partita lunga", richiamata nelle sezioni 4, 7, 11, 17 e 18), poi quattro test (tre rossi), poi il codice.

**Regole**
- **Orologio:** i turni per portare UW a 0. Si parte dalla vita di UW, si tolgono 3 per ogni Bolt in mano, si divide per il danno per turno del piano d'attacco e si arrotonda per eccesso. Il danno per turno è quello delle creature che le regole manderebbero all'attacco, tolte le più forti, una per bloccante stappato: lo stesso conto dell'attacco letale. Il guadagno di vita di UW allunga l'orologio da solo.
- **Corsa** (orologio di 2 turni o meno): Teferi si ignora. Attacchi sul giocatore, niente Bolt su Teferi, niente Bolt e Flare spostati per colpa di Teferi. Bolt in faccia anche sopra i 6 punti vita, nella prima fase principale dopo le creature. Teferi non conta più come "minaccia vera" per Flare.
- **Partita lunga** (oltre 2 turni): le regole del gruppo 1 dell'audit.
- **Variante "togli sempre Teferi":** l'opzione `teferi_race_clock = 0` (mai in corsa). Annotata tra i test A/B del matchup, insieme alla soglia di 2 turni.
- **Contatore:** con Teferi avversario in campo ogni regola porta il suffisso " [corsa]" o " [partita lunga]"; senza Teferi i nomi non cambiano.

**Scelte fatte da solo**
- **Una sola soglia come opzione.** La variante "togli sempre" è la soglia 0. Così le due cose da provare (la regola e la soglia) usano lo stesso parametro.
- **La modalità vale per tutte le decisioni** prese con Teferi in campo, anche quelle che non lo riguardano (blocchi, scarti): il contatore le distingue tutte.
- **"Bolt in faccia se tiene l'orologio entro 2 turni"** coincide con la modalità corsa: l'orologio conta già i Bolt in mano, quindi lanciarli in faccia non lo cambia. In corsa Bolt va in faccia prima di Ocelot Pride e Guide of Souls, perché il danno è già nel conto.
- **Il Bolt della corsa va nel punto 10 della fase principale**, dopo le creature, e non "a vista" come il Bolt su Ocelot: non toglie il mana alle giocate che lo precedono.
- **L'orologio non conta i combattimenti aggiuntivi** di Fear of Missing Out: è prudente, al più allunga l'orologio.
- **La modalità si calcola una volta per decisione**, con una cache nelle regole: l'orologio chiama le regole d'attacco per ogni creatura.

**Verifiche:** 389 test in Release in 14,8 s. Il goldfish (variante B, 100.000 partite) è identico byte per byte: le partite e il riepilogo con i contatori delle regole. Tempo 28,8 s contro 27,8 s della misura di riferimento; le misure precedenti oscillavano già tra 27,8 e 28,2 s. Ora però ogni decisione copia il nome della regola in una stringa: una parte della differenza può venire da lì.

**Errori e correzioni**
- **Un avviso di conversione nel test** (vita assegnata da un `int` a un `int16_t`): il parametro dell'aiuto è ora del tipo giusto.

## Proposta dell'agente di UW, rivista con Daniele, 10-11/11/2026

`docs/agente_uw.md` (proposta nel commit `cbefe7c`) è stato rivisto con Daniele, pilota di UW Blink. Tutte le 18 domande della sezione 26 hanno una risposta. Le sue risposte sono **regole di partenza**: la ricerca ad albero le confermerà o le smentirà.

**Decise (12):** D-1 Witch Enchanter (conta come terra nel mulligan; in partita soprattutto carta bianca per Solitude; distruggere FOMO o la Phoenix solo se i due tipi in più non danno il delirium), D-2 mulligan (una terra con Solitude ed Ephemerate; senza risposte solo sul play con tre creature da un mana), D-3 mana aperto (Thraben sì, mai mana solo per Consign), D-4 Solitude evocata in modo aggressivo e sempre con Quantum Riddler in mano, D-5 Ephemerate (il rimbalzo si può rifiutare), D-8 mai Riddler con il warp al turno 2 (warp come ricarica), D-11 l'energia di Guide sempre, D-12 Teferi −3 appena c'è un bersaglio ragionevole, D-14 valore in mano (Riddler molto in alto, Phelia prima di Ephemerate tranne con Solitude), D-15 Consign sui nostri trigger, D-17 Teferi non protetto da Bolt ma Bolt pesa nella scelta dei bersagli, D-18 attaccano i volanti potenziati da Guide e i gatti restano a bloccare.

**Da far decidere all'albero (6):** D-6 classifica delle minacce (Stitcher's Supplier però è deciso: non si rimuove), D-7 Phelia, D-9 il danno di Thraben Charm, D-10 Sanctifier, D-13 Force of Negation, D-16 soglia di vita. Nel testo ogni regola di questo tipo porta "(Da far decidere all'albero.)".

**Errori e correzioni**
- **Il primo messaggio della revisione si era interrotto a metà della D-14.** Non ho completato la risposta: la D-14 è rimasta aperta finché è arrivato il resto.
- **Un motivo sbagliato, corretto rileggendo:** per il warp di Riddler avevo scritto che "costa una carta per pescarne una". Non è vero: con il warp Riddler va in esilio e si può rilanciare. Il motivo giusto è quello di Daniele: a mano quasi vuota la pescata raddoppia.
- **Interpretazione da confermare (D-17):** "i lampeggi su creature che restano con costituzione 3 o meno sono esposti" è diventata una regola di scelta del bersaglio: a parità di valore, segnalini di Guide e lampeggi per valore vanno su creature che dopo hanno costituzione 4 o più.
