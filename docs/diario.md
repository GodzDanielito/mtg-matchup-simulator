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
