# Agente euristico di UW Blink (rivisto con Daniele il 10/11/2026, implementato l'11/11/2026)

Questo documento descrive come giocherà l'agente euristico di UW Blink: regole scritte a mano, ognuna con il suo motivo. L'implementazione è in `agents/uw_agent.cpp`, una funzione per regola; le scelte fatte implementando, dove il documento non decideva, sono nella sezione 27. Struttura e principi sono quelli di `docs/agente_rakdos.md`.

**Revisione con Daniele, pilota di UW Blink (10/11/2026).** Le domande di strategia sono nella sezione 26, con la risposta e lo stato. Le risposte di Daniele sono **regole di partenza**: la ricerca ad albero le confermerà o le smentirà. Dove la risposta è "dipende dalla situazione", il documento usa la regola di partenza indicata e la segna come **da far decidere all'albero**. Le regole che dipendono da una domanda lo dicono con il riferimento (D-n).

La lista è quella di simulazione, `config/decks/uw_blink_sim.json`. I testi delle carte citati vengono dal bulk Oracle di Scryfall del 9/11/2026, lo stesso dei test. Le regole del gioco sono le Comprehensive Rules in vigore dal 6/11/2026.

## 1. Principi

- **Vede solo quello che vedrebbe il giocatore.** La propria mano, le zone pubbliche (campo, cimiteri, esilio, pila), i punti vita, l'energia, quante carte ha in mano l'avversario. Non legge mai la mano avversaria né l'ordine dei grimori. Sa quali delle proprie carte l'avversario conosce (per esempio dopo un Thoughtseize): è informazione sua.
- **Non sa chi ha davanti, ma vede lo stato pubblico.** Nessuna regola dipende dal tipo di avversario; tutte dipendono da quello che c'è sul tavolo: creature, segnalini, carte nei cimiteri, colori mostrati. Le regole nominano le carte avversarie perché le carte sono pubbliche, non perché l'agente sa che è Rakdos. Dove il motivo di una regola è il matchup, la regola resta scritta sullo stato: per esempio "si preferisce esiliare" vale sempre, e il motivo è che le carte nei cimiteri avversari possono aiutare l'avversario.
- **Un piano per turno, una lista di priorità.** Nella propria fase principale l'agente scorre una lista fissa di giocate (sezione 4) e fa la prima legale e conveniente. Nel turno avversario reagisce secondo le regole del mana aperto (sezione 5). Le altre decisioni (bersagli, scelte di carte, blocchi) hanno regole proprie.
- **Non gioca attorno ai trucchi nelle giocate** (D-17): non tiene mana e non cambia l'ordine delle giocate per paura di Lightning Bolt o Flare of Malice in mano all'avversario, e **non protegge Teferi da Bolt**. Lo stesso limite dell'agente di Rakdos, misurato dal confronto con la ricerca ad albero.
- **Bolt conta invece nella scelta dei bersagli** (D-17). Le creature con costituzione 3 o meno sono esposte a Lightning Bolt: Guide of Souls, e le creature che dopo un lampeggio restano con costituzione 3 o meno. Quando si sceglie dove investire (i segnalini di Guide, sezione 20; un lampeggio per valore, sezioni 8 e 9), a parità di valore si preferisce la creatura che dopo ha **costituzione 4 o più**. *Perché:* un investimento su una creatura che muore al primo Bolt va perso insieme a lei.
- **Esiliare è meglio che distruggere.** Tra due risposte che tolgono la stessa minaccia si sceglie quella che esilia, o che la rimette in mano. *Perché:* ogni carta che finisce in un cimitero avversario può dare un tipo per il delirium, forza a Nethergoyf, prove per la Phoenix, carte per Dark Matter Manipulator, segnalini tolti a Moonshadow; e le creature che muoiono possono avere trigger (Stitcher's Supplier). **Eccezione dello stato:** con un nostro Sanctifier en-Vec in campo le carte nere e rosse vanno comunque in esilio, quindi distruggerle equivale a esiliarle.
- **Soglia di vita** (D-16). Le azioni che costano vita per scelta (fetch, Hallowed Fountain pagata, Witch-Blessed Meadow pagata):
  - **nei nostri primi tre turni** si paga la vita che serve per seguire la curva;
  - **dopo**, solo se dopo il pagamento la vita resta **sopra la forza totale delle creature avversarie in campo più 3**, e comunque **almeno 5**, salvo che il pagamento vinca la partita.

  *Perché:* all'inizio la curva vale più di qualche punto vita; dopo, il "+3" è il danno di un Lightning Bolt e la forza in campo è quello che può arrivare nel prossimo attacco. La soglia è scritta sullo stato pubblico. *(Da far decidere all'albero.)*
- **Pareggi.** Tra opzioni equivalenti per le regole sceglie a caso, con un generatore suo: stesso seed, stessa partita.
- **Contatori delle regole, nessun "predefinito".** Ogni decisione registra la regola che l'ha presa. A differenza della prima versione dell'agente di Rakdos, **ogni tipo di decisione del motore ha una regola esplicita** (sezione 24): la scelta di riserva esiste solo come rete di sicurezza, e il test dell'agente pretende **zero decisioni "predefinito"** in una serie di partite del matchup.

## 2. Mulligan

**Con 7 carte si tiene se:**

- ci sono **da 2 a 4 terre**, contando Witch Enchanter come terra; oppure **una sola terra** che produce {W}, con **Solitude ed Ephemerate insieme** in mano (D-2);
- c'è almeno **una giocata entro il secondo turno**: una creatura da uno o due mana (Ocelot Pride, Guide of Souls, Phelia, Sanctifier en-Vec), Seam Rip, oppure il warp di Starfield Shepherd. Solitude ed Ephemerate insieme valgono come giocata. Il warp di Quantum Riddler non conta: non si gioca al secondo turno (D-8);
- c'è almeno **una risposta**: Solitude, March of Otherworldly Light, Sanctifier en-Vec, Seam Rip o Teferi. **Eccezione:** sul play si tiene anche senza risposte con almeno **tre creature da un mana** (per esempio Guide of Souls, Guide of Souls, Ocelot Pride) (D-2);
- le terre producono i colori per la giocata del secondo turno.

*Perché:* è la regola di partenza della specifica (`docs/progetto.md`, "Mulligan e play/draw"), con le giocate economiche e le risposte di questa lista. UW perde le partite in cui l'avversario sviluppa due creature che crescono senza trovare né un blocco né una risposta.

**Witch Enchanter nel conto delle terre:** conta come terra (D-1). *Perché:* il retro Witch-Blessed Meadow è una terra vera; in partita resta in mano come carta bianca per Solitude quando la terra non serve (sezione 3).

**Solitude ed Ephemerate con una terra:** la coppia toglie la prima minaccia e lascia una 3/2 con legame vitale, con il solo {W} di Ephemerate. Nessuna terra: sempre mulligan.

**Con 6 carte** si tiene con 2-4 terre (Witch compresa), o una con Solitude ed Ephemerate, e almeno una giocata o una risposta. **Con 5 o meno** si tiene sempre. Le terre si contano dopo aver messo in fondo.

**Carte da mettere in fondo** (mulligan London), una alla volta:

1. le terre oltre la terza, tenendo prima Witch Enchanter (carta bianca per Solitude), poi Hallowed Fountain, poi le fetch, poi Plains, per ultima Meticulous Archive;
2. poi la carta di valore più basso nella classifica della sezione 22, senza mai lasciare la mano senza la sua unica risposta o la sua unica giocata del secondo turno.

*Perché:* tre terre lanciano quasi tutto (Teferi, Recruiter, i warp); Solitude e Starfield Shepherd si giocano anche senza cinque terre (evocazione, warp).

## 3. Terre, fetch e shockland

Le terre della lista: 4 Arid Mesa, 2 Flooded Strand, 4 Windswept Heath, 4 Hallowed Fountain, 2 Meticulous Archive, 3 Plains, e i 4 retri di Witch Enchanter. Le fetch cercano solo Plains, Hallowed Fountain e Meticulous Archive (tipi Plains e Island).

**Blood Moon** è fuori dal main di Rakdos per ora (solo gara 1): nessuna regola lo considera. Nel Bo3 andrà rivisto l'ordine delle ricerche (prima le Plains base), ma oggi non serve e non si anticipa.

**Quale terra giocare**, nell'ordine:

1. **Meticulous Archive** se il mana di questo turno non serve. *Perché:* entra tappata, quindi è meglio giocarla quando non costa nulla; sorvegliare 1 migliora la pescata.
2. **Una fetch**, rotta subito (cerca: sotto). *Perché:* rotta subito è una terra stappata; tenerla non dà nulla in questo pool.
3. **Plains**, poi **Hallowed Fountain**.
4. **Witch-Blessed Meadow** (il retro di Witch Enchanter) solo se serve alla curva o al piano: non c'è un'altra terra da giocare, e senza una terra in questo turno una giocata prevista per questo turno o il prossimo non si potrebbe pagare. Altrimenti Witch Enchanter resta in mano come carta bianca da esiliare per Solitude, che è il suo uso principale. Si pagano 3 vita solo se il mana serve subito (D-1).

**Cosa cercare con la fetch:**

- se in questo turno serve il blu e non lo abbiamo: **Hallowed Fountain**, pagando 2 vita se il mana serve subito;
- altrimenti **Plains** finché ce ne sono, poi Hallowed Fountain tappata;
- **Meticulous Archive** solo se il mana di questo turno non serve e le Plains sono finite.

*Perché:* la vita conta contro creature che crescono e Lightning Bolt; il blu serve a Consign, Force of Negation pagata, Teferi e Quantum Riddler, ma non sempre subito.

**Hallowed Fountain (pagare 2 vita o tappata):** si paga solo se il mana serve in questo turno per una giocata prevista o per tenere aperto il mana della sezione 5, e rispettando la soglia di vita.

## 4. Ordine delle giocate nel turno

**Mantenimento:** il rimbalzo di Ephemerate (sezione 8).

**Prima fase principale**, in quest'ordine:

1. **Terra**, e rompere la fetch (sezione 3).
2. **Sanctifier en-Vec** se l'avversario ha mostrato carte nere o rosse (sezione 14).
3. **Teferi, Time Raveler**, se lo lanciamo in questo turno: prima del combattimento (sezione 17). *Perché:* lanciato prima di attaccare, toglie all'avversario Lightning Bolt e Flare of Malice durante il nostro combattimento.
4. **Seam Rip** su un bersaglio della sezione 12.
5. **Creature da un mana:** Guide of Souls prima di Ocelot Pride. *Perché:* Guide guadagna vita ed energia per ogni creatura che entra dopo di lei, e la vita guadagnata dà il gatto di Ocelot alla nostra sottofase finale.
6. **Starfield Shepherd con il warp** (sezione 16), oppure **Phelia** se non la teniamo per il lampo. Quantum Riddler con il warp solo come ricarica, a mano quasi vuota (sezione 16).
7. **Recruiter of the Guard**, **Witch Enchanter** con un bersaglio, **Starfield Shepherd** o **Quantum Riddler** a costo pieno.
8. **Rimozione a velocità stregoneria prima di attaccare:** solo se toglie il bloccante che fermerebbe un attacco importante. Le risposte istantanee preferiscono il turno avversario (sezione 5).

**Combattimento:** sezione 21. Il trigger di Guide of Souls: sezione 20. Phelia: sezione 9.

**Seconda fase principale:** le creature non giocate prima che non cambiano il combattimento, Teferi se non lanciato prima (+1 o −3, sezione 17).

**Prima di passare il turno:** il mana non speso resta aperto per le istantanee della sezione 5. La sezione 5 dice anche quando una giocata della fase principale si rimanda per tenere mana aperto.

*Perché questa divisione:* prima del combattimento si gioca ciò che lo cambia (Teferi, i bloccanti tolti, Guide con le energie); le risposte istantanee aspettano di vedere che cosa fa l'avversario.

## 5. Mana aperto: quando e per cosa

UW ha molte istantanee e due creature con lampo. Una parte del mazzo si gioca meglio nel turno avversario; la difficoltà è decidere quanto mana non spendere nel proprio turno.

**Regola di base.** Nel proprio turno si gioca la lista della sezione 4. Poi una giocata della fase principale si **rimanda** se toglierebbe il mana di una delle istantanee qui sotto con la sua condizione vera. Si tiene il mana per **una sola** istantanea, la prima della lista con la condizione vera (D-3).

| Istantanea in mano | Mana | Condizione per tenerlo |
| --- | --- | --- |
| Thraben Charm | {1}{W} | C'è un uso previsto: esiliare il cimitero avversario in combattimento o alla fine del suo turno (modi 1-3 della sezione 11), oppure il danno su una minaccia della sezione 6 (D-3) |
| Ephemerate | {W} | Abbiamo in campo una creatura con il warp da salvare (sezione 8), oppure in mano una Solitude da evocare con un bersaglio (sezione 7) |
| Solitude a costo pieno | {3}{W}{W} | Mai tenuti apposta: Solitude si evoca (sezione 7) |
| Phelia (lampo) | {1}{W} | Se resta mana alla fine del turno avversario e Phelia non è stata lanciata (sezione 9) |
| March of Otherworldly Light | X+1 | L'avversario ha una creatura che la sezione 6 vuole togliere e che March può esiliare con il mana che abbiamo |
| Consign to Memory | {U} | **Mai tenuto apposta.** Si usa quando il mana è comunque disponibile (sezione 19, D-3) |

Force of Negation nel turno avversario non chiede mana (sezione 18).

**Cosa fare con il mana tenuto e non usato:** nella sottofase finale avversaria si spende, nell'ordine: la rimozione su una minaccia della sezione 6 (March, poi Solitude), Thraben Charm sul cimitero avversario (sezione 11), Phelia con lampo, Ephemerate su una creatura con un'entrata utile (sezione 8). La rimozione viene prima: è la giocata che cambia di più la partita. *Perché:* il mana non speso nel turno avversario è perso, e la sottofase finale è il momento in cui si è visto tutto.

**Nessuna condizione guarda il mana avversario.** Tenere {W} perché l'avversario "potrebbe avere Bolt" sarebbe giocare attorno ai trucchi, escluso nella prima versione (sezione 1, D-17). Se però il mana è rimasto aperto per un'altra ragione, Ephemerate salva una creatura da un Bolt (sezione 8).

**Con Teferi in campo** l'avversario può lanciare magie solo a velocità stregoneria: nel nostro turno non può rispondere, e nel suo lancia solo nella fase principale.

## 6. Quale minaccia togliere

Le risposte (Solitude, March, Seam Rip, Teferi −3, Thraben Charm, Witch Enchanter, Phelia) scelgono il bersaglio con questa classifica, sullo stato pubblico. Ogni sezione dice poi quali bersagli può davvero colpire.

**Minacce, dalla più importante:**

1. **Fear of Missing Out quando il cimitero del suo controllore ha quattro o più tipi**: ogni primo attacco del turno dà un combattimento in più e stappa un attaccante.
2. **La creatura con Detective's Phoenix conferita**: +2/+2, volare e rapidità. Se si esilia la creatura, la Phoenix resta come creatura 2/2 volante: la minaccia scende comunque da grande a piccola (posizione test della specifica).
3. **La creatura con più forza** tra quelle che crescono: Nethergoyf (forza pari ai tipi nel cimitero), Moonshadow (perde un segnalino −1/−1 a ogni carta permanente che va nel cimitero), Dark Matter Manipulator (+2/+0 ogni sette carte). A parità, quella che può crescere ancora di più: Moonshadow con più segnalini, Nethergoyf con meno di sei tipi.
4. **Troll of Khazad-dûm, Street Wraith**: grandi ma fermi.
5. **Fear of Missing Out senza delirium**, Detective's Phoenix come creatura.

**Stitcher's Supplier non è una minaccia** (D-6): si tratta come una magia, già giocata. Non si spende rimozione su di lui; in combattimento si blocca e si scambia come ogni altra creatura.

*Perché:* l'ordine segue il danno che la creatura farà nei prossimi turni; le creature che crescono vanno tolte prima che crescano. Il trigger di FOMO va in cima perché moltiplica gli attacchi di tutte le altre. La classifica, Supplier escluso, è la regola di partenza (D-6). *(Da far decidere all'albero.)*

**Esilio prima della distruzione** (sezione 1): con due risposte disponibili per lo stesso bersaglio si usa quella che esilia; il danno (Thraben Charm) e la distruzione (Witch Enchanter, Thraben Charm) vengono dopo.

## 7. Solitude

"When this creature enters, exile up to one other target creature. That creature's controller gains life equal to its power. Evoke—Exile a white card from your hand." Lampo, legame vitale, 3/2.

**Lanciare o evocare:**

- **A costo pieno** ({3}{W}{W}) quando abbiamo cinque mana: resta in campo una 3/2 con legame vitale. Con il lampo, nel turno avversario: durante la sua dichiarazione degli attacchi, dopo che ha scelto gli attaccanti, oppure nella sua sottofase finale.
- **Evocata, in modo aggressivo** (D-4): appena c'è un bersaglio della classifica della sezione 6 e non abbiamo cinque mana. Nel turno avversario, prima che la minaccia attacchi o durante la dichiarazione degli attacchi; nel nostro turno, se toglie un bloccante. *Perché:* contro creature che crescono, toglierle subito vale le due carte.
- **Con Quantum Riddler in mano si evoca sempre**, appena c'è un bersaglio della classifica, anche se i cinque mana arriverebbero presto: Riddler restituirà le carte (con una carta o meno in mano pesca due carte, sezione 16).
- **Evocata con Ephemerate** (il trucco): con {W} disponibile ed Ephemerate in mano, si evoca e, con il trigger di sacrificio in pila, si lancia Ephemerate su Solitude. Solitude rientra come oggetto nuovo: nessun sacrificio, un secondo trigger di esilio, e resta in campo. È la giocata migliore della mano quando ci sono due bersagli.

**Ordine dei due trigger** (entrata ed evocazione): il sacrificio va in pila per primo, l'esilio sopra, così l'esilio si risolve per primo. Con Ephemerate o Consign to Memory in mano si risponde al trigger di sacrificio quando è in cima.

**Quale carta bianca esiliare** per l'evocazione (la carta di valore più basso, sezione 22, tra le bianche):

1. **Witch Enchanter**, il suo uso principale (D-1), salvo che serva come terra per la curva (sezione 3);
2. un'altra Solitude se ne abbiamo tre o più tra mano e campo;
3. Recruiter of the Guard, Starfield Shepherd, poi le altre in ordine di valore;
4. mai Ephemerate se il piano è il trucco con Ephemerate, mai l'ultima risposta rimasta.

**Bersaglio:** la prima minaccia della sezione 6. Solitude non bersaglia mai le nostre creature. *Perché:* la vita va al controllore della creatura esiliata; per noi non c'è nulla da guadagnare.

## 8. Ephemerate

"Exile target creature you control, then return it to the battlefield under its owner's control. Rebound." La creatura torna come oggetto nuovo: perde segnalini, danno e Aure, e si rifanno i suoi trigger di entrata.

**Su cosa**, dalla giocata migliore (D-5): prima i pezzi con l'evocazione o il warp, poi la protezione, poi il valore.

1. **Solitude evocata, con il trigger di sacrificio in pila** (sezione 7): Solitude resta e il suo esilio scatta di nuovo.
2. **Una creatura con il warp, prima della sottofase finale** (Quantum Riddler, Starfield Shepherd): torna come oggetto nuovo, il trigger del warp non la trova più e resta in campo per sempre. Rientrando fa di nuovo il suo trigger (pescata, ricerca). La regola è provata dal test T-1 dell'audit.
3. **Salvare una creatura da un bersaglio**: in risposta a Lightning Bolt su una nostra creatura di valore (sezione 22) o su una creatura che altrimenti morirebbe in combattimento. Bolt perde il bersaglio. *Non* salva da Flare of Malice: il sacrificio si sceglie alla risoluzione.
4. **Un'entrata utile** (a parità di valore, la creatura che dopo resta con costituzione 4 o più, sezione 1): Solitude lanciata (un altro esilio, se c'è un bersaglio), Witch Enchanter (se l'avversario ha un artefatto o un incantesimo), Sanctifier en-Vec (se nei cimiteri ci sono carte nere o rosse), Recruiter of the Guard, Quantum Riddler (una carta), Starfield Shepherd (una ricerca). Si gioca nella sottofase finale avversaria, con il mana tenuto e non usato (sezione 5).

**Guide of Souls** guadagna 1 vita e un'energia per ogni creatura che rientra (è "another creature"): un'Ephemerate vale sempre almeno questo.

**Rimbalzo, al nostro mantenimento** (D-5): si rilancia **solo su una creatura con un effetto di entrata rilevante adesso** (la lista del punto 4, con il suo effetto utile: un bersaglio per Solitude o Witch Enchanter, carte nere o rosse nei cimiteri per Sanctifier; Quantum Riddler, Recruiter e Starfield Shepherd sempre). Altrimenti si rifiuta, e la carta resta in esilio.

**In corsa non si rimbalza un attaccante.** Siamo in corsa quando il nostro orologio è di 2 turni o meno: i turni nostri per portare l'avversario a 0 con il danno per turno del nostro piano d'attacco, lo stesso conto dell'agente di Rakdos (`docs/agente_rakdos.md`, sezione 8). In corsa il rimbalzo non prende una creatura che attaccherebbe in questo turno. *Perché:* la creatura rientrata è un oggetto nuovo e non può attaccare nel turno in cui rientra; mentre si chiude la partita, un attacco vale più di un'entrata.

## 9. Phelia, Exuberant Shepherd

"Flash. Whenever Phelia attacks, exile up to one other target nonland permanent. At the beginning of the next end step, return that card to the battlefield under its owner's control. If it entered under your control, put a +1/+1 counter on Phelia."

**Lancio:** con il lampo alla fine del turno avversario, se resta il mana (sezione 5); nel nostro turno solo se non c'è niente di meglio da fare con due mana. *Perché:* con il lampo evita la rimozione a velocità stregoneria e attacca comunque nel nostro turno.

**Attacco:** Phelia attacca se c'è un bersaglio utile qui sotto **e** sopravvive al blocco migliore dell'avversario, oppure se nessun blocco conviene all'avversario (sezione 21). Attacco e bersaglio sono la regola di partenza (D-7): per Daniele è un problema "da scacchi". *(Da far decidere all'albero.)*

**Bersaglio dell'esilio**, dal migliore:

1. **Una nostra creatura con il warp** (Quantum Riddler, Starfield Shepherd): torna alla sottofase finale come oggetto nuovo e resta per sempre, e Phelia prende il segnalino (test T-1 dell'audit).
2. **Il bloccante avversario** che ferma un attacco letale o un attacco importante.
3. **Una nostra creatura con un'entrata utile** (la lista della sezione 8, punto 4), o una Solitude lanciata con un bersaglio: rientra alla sottofase finale e rifà il trigger; Phelia prende il segnalino. A parità, quella che dopo resta con costituzione 4 o più (sezione 1).
4. **Teferi** con fedeltà sotto 4: torna con 4 segnalini, come oggetto nuovo (test T-2 dell'audit).
5. **Moonshadow avversario con due o meno segnalini −1/−1**: torna sotto il controllo del proprietario, con sei segnalini.
6. Nessun bersaglio ("up to one").

**Mai:** Seam Rip (lasciando il campo restituisce la carta che teneva in esilio), Stitcher's Supplier o Dark Matter Manipulator avversari (tornando macinano tre carte), Fear of Missing Out avversario se non è il bloccante del punto 2 (tornando gli fa scartare e pescare).

## 10. March of Otherworldly Light

"As an additional cost to cast this spell, you may exile any number of white cards from your hand. This spell costs {2} less to cast for each card exiled this way. Exile target artifact, creature, or enchantment with mana value X or less."

**X:** il valore di mana del bersaglio, mai di più. Il motore offre solo questi valori. Contro le creature economiche di questo pool X è quasi sempre 1 ({1}{W}).

**Carte bianche da esiliare:** nessuna, se il mana basta. Si esilia una carta bianca (la meno preziosa, come per Solitude, sezione 7) solo se serve per lanciare March **adesso** contro una minaccia tra le prime tre della sezione 6. *Perché:* ogni carta esiliata risparmia due mana ma costa una carta. Con X=1 il risparmio non vale quasi mai la carta: è la posizione test della specifica (March con X=1 costa due mana e risparmia le due carte di Solitude evocata).

**Bersaglio e momento:** la prima minaccia della sezione 6 che March può colpire, nel turno avversario. Durante il suo combattimento se la minaccia attacca, altrimenti nella sua sottofase finale. March esilia: tra March e Witch Enchanter o Thraben Charm sullo stesso bersaglio, March.

## 11. Thraben Charm

Tre modi: danno pari al doppio delle nostre creature a una creatura; distruggere un incantesimo; esiliare i cimiteri di un qualsiasi numero di giocatori bersaglio.

**Quale modo**, dal più forte:

1. **Esiliare il cimitero avversario in risposta al trigger d'attacco di Fear of Missing Out con il delirium.** Il trigger ricontrolla il delirium alla risoluzione ("if there are four or more card types", 603.4): senza cimitero non stappa nulla e non arriva il combattimento aggiuntivo.
2. **Esiliare il cimitero avversario con un Nethergoyf in combattimento**: Nethergoyf diventa 0/1 prima del danno.
3. **Esiliare il cimitero avversario nella sua sottofase finale**, se ha tre o più tipi (il delirium, o un tipo dal delirium: posizione test della specifica) e ha in campo Fear of Missing Out o Nethergoyf, o nel cimitero ci sono Detective's Phoenix o Nethergoyf da rilanciare. *Perché:* toglie delirium, forza, prove per la Phoenix e la fuga, tutto insieme.
4. **Danno** (il doppio delle nostre creature) quando uccide una minaccia della sezione 6 e l'esilio del cimitero vale meno: nessuna delle condizioni dei modi 1-3 è vera (D-9). Mai su Stitcher's Supplier (sezione 6). *(Da far decidere all'albero.)*
5. **Distruggere un incantesimo**: Fear of Missing Out o una Phoenix conferita, solo se nessuna risposta che esilia è disponibile.

**Quali cimiteri:** solo quello avversario. *Perché:* il nostro cimitero non ci serve, ma non ci danneggia; esiliarlo non dà nulla.

**Quando:** i modi 1-4 sono le ragioni per tenere {1}{W} aperti (sezione 5, D-3). Il modo 3 si gioca alla fine del turno avversario solo se il modo 1 non è più probabile: Fear of Missing Out non è in campo.

## 12. Seam Rip

"When this enchantment enters, exile target nonland permanent an opponent controls with mana value 2 or less until this enchantment leaves the battlefield."

**Bersaglio:** la prima minaccia della sezione 6 con valore di mana 2 o meno: in questo pool lo sono tutte le creature da uno e due mana (Nethergoyf, Moonshadow, Dark Matter Manipulator, Fear of Missing Out) e Mishra's Bauble. Mai Stitcher's Supplier (sezione 6).

**Quando:** nella prima fase principale, appena c'è un bersaglio tra le prime tre voci della sezione 6, oppure un Fear of Missing Out. *Perché:* costa {W} ed esilia una creatura che cresce prima che cresca; l'esilio impedisce anche la fuga di Nethergoyf. Un Seam Rip in mano senza bersaglio aspetta.

**Rischio:** se Seam Rip lascia il campo la carta torna, come oggetto nuovo (Moonshadow con sei segnalini). Nessuna nostra carta lo toglie volontariamente (sezione 9).

## 13. Witch Enchanter

"When this creature enters, destroy target artifact or enchantment an opponent controls." Il retro è una terra (sezione 3).

**Bersagli validi:** Mishra's Bauble, Fear of Missing Out (creatura incantesimo), Detective's Phoenix (creatura incantesimo, anche conferita).

**Il suo uso principale è la carta bianca da esiliare per Solitude** (sezione 7, D-1). Come terra si gioca solo se serve alla curva o al piano (sezione 3).

**Lanciarla come creatura** è un bonus di situazione, con un bersaglio che vale la carta:

1. **Fear of Missing Out**;
2. **una Detective's Phoenix conferita su una creatura grande**: la creatura perde +2/+2, volare e rapidità. La Phoenix va nel cimitero e può essere conferita di nuovo da lì, quindi vale meno di quanto sembra;
3. **Detective's Phoenix come creatura**: ultima scelta, perché distrutta torna disponibile dal cimitero.

**Controllo del cimitero:** Fear of Missing Out e Detective's Phoenix sono creature incantesimo, quindi distrutte aggiungono **due tipi** (creatura e incantesimo) al cimitero del loro controllore. Si distrugge solo se dopo il cimitero non passa a quattro o più tipi, cioè se non dà il delirium che prima mancava. Con un nostro Sanctifier en-Vec in campo il controllo non serve: vanno in esilio.

Mai su Mishra's Bauble: costa zero e si sacrifica in risposta.

*Perché:* distruggere manda la carta nel cimitero avversario (sezione 1); il danno di una Witch lanciata male è dare il delirium.

## 14. Sanctifier en-Vec

Protezione dal nero e dal rosso; entrando esilia tutte le carte nere o rosse dai cimiteri; finché è in campo, le carte nere o rosse che andrebbero in un cimitero vanno in esilio.

**Quando:** appena l'avversario ha mostrato una carta nera o rossa (in campo, nel cimitero, in pila, anche una terra che produce quei colori). È la giocata da due mana più importante della lista e viene prima delle altre (sezione 4). Esempio della specifica: Sanctifier al turno 2, con il cimitero avversario quasi vuoto, si gioca subito.

**In combattimento:** non può essere bloccato da creature nere o rosse e non subisce danno da loro.

- **Attacca** se dopo l'attacco i nostri bloccanti rimasti bastano a non scendere sotto la soglia di vita della sezione 1 nel prossimo turno avversario.
- Altrimenti **resta a bloccare** la creatura avversaria più forte: la blocca senza subire danno.

Attaccare o bloccare è la regola di partenza (D-10). *(Da far decidere all'albero.)*

**Con Ephemerate o Phelia:** rientrando esilia di nuovo le carte nere o rosse dai cimiteri. Serve solo se ce ne sono arrivate mentre Sanctifier non era in campo.

## 15. Recruiter of the Guard e Starfield Shepherd

**Recruiter of the Guard** cerca "a creature card with toughness 2 or less" (facoltativo). Nella lista: Guide of Souls, Ocelot Pride, Phelia, Sanctifier en-Vec, Solitude, Starfield Shepherd, Witch Enchanter. Cosa cercare, nell'ordine:

1. **Sanctifier en-Vec** se non è già in campo o in mano e l'avversario ha mostrato carte nere o rosse;
2. **Solitude**, se non ne abbiamo in mano e l'avversario ha una minaccia della sezione 6;
3. **Witch Enchanter** se ci servono terre (meno di quattro tra campo e mano): è una terra;
4. **Guide of Souls** o **Ocelot Pride** (la prima che non abbiamo in campo), altrimenti Phelia.

**Starfield Shepherd** cerca "a basic Plains card or a creature card with mana value 1 or less": Plains, Guide of Souls, Ocelot Pride.

1. **Plains** se abbiamo meno di quattro terre tra campo e mano;
2. **Guide of Souls** se non è in campo;
3. **Ocelot Pride**.

*Perché:* la ricerca deve coprire prima il punto debole del momento (il cimitero avversario, le terre), poi le creature economiche che creano valore insieme (Guide e Ocelot).

## 16. Quantum Riddler e il warp

Quantum Riddler: 4/6 volante, entrando pesca una carta; con una carta in mano o nessuna, ogni pescata dà una carta in più. Warp {1}{U}. Starfield Shepherd: warp {1}{W}. Una creatura lanciata con il warp va in esilio alla prossima sottofase finale e si può rilanciare dall'esilio da un turno successivo, a costo pieno.

**Quantum Riddler con il warp: solo come ricarica** (D-8). **Mai al secondo turno.** Si lancia con il warp quando, dopo il lancio, in mano resta una carta o nessuna: entrando pesca due carte invece di una. *Perché:* con la mano piena il warp dà una sola carta e un turno di Riddler in campo; a mano quasi vuota la sostituzione raddoppia la pescata, ed è lì che il warp vale i due mana. Al secondo turno la mano è piena per definizione.

**Riddler in mano si tiene per la ricarica** (D-14): non si esilia per Force of Negation se c'è un'altra carta blu, e le altre giocate vengono prima. Il warp arriva quando la mano si è svuotata.

**Starfield Shepherd con il warp:** al secondo o terzo turno se non c'è una giocata migliore da due mana (Sanctifier, Seam Rip su una minaccia, Phelia da tenere per il lampo). Cerca, e poi torna disponibile dall'esilio.

**Warp con un piano per tenerla:** con Ephemerate in mano e {W} libero, oppure con Phelia in campo che attaccherà in questo turno, il warp diventa una creatura permanente (sezioni 8 e 9). In quel caso si lancia nella prima fase principale, prima di Phelia. Per Quantum Riddler vale solo insieme alla condizione della ricarica.

**Costo pieno:**

- **Quantum Riddler** con cinque mana e niente di più urgente: un 4/6 volante è un muro contro le creature di terra e Lightning Bolt non lo uccide.
- **Starfield Shepherd** con cinque mana e niente di più urgente.
- **Dall'esilio**, a costo pieno, alle stesse condizioni del costo pieno dalla mano.

**Senza un piano per tenerla**, la creatura con il warp si lancia nella **seconda** fase principale. *Perché:* non può attaccare (nessuna rapidità), quindi serve solo il trigger di entrata; lanciata dopo il combattimento, l'avversario sa meno sul nostro turno.

**Flare of Malice:** Quantum Riddler e Starfield Shepherd hanno valore di mana 5. Se l'avversario lancia Flare mentre una creatura con il warp sta per tornare in esilio, quella è il sacrificio (sezione 22).

## 17. Teferi, Time Raveler

"Each opponent can cast spells only any time they could cast a sorcery. +1: Until your next turn, you may cast sorcery spells as though they had flash. −3: Return up to one target artifact, creature, or enchantment to its owner's hand. Draw a card." Fedeltà 4.

**Quando lanciarlo:** nella prima fase principale, prima del combattimento, se l'avversario ha mana stappato. Altrimenti appena si hanno tre mana e niente di più urgente. *Perché:* la statica toglie all'avversario Bolt e Flare istantanei: nel nostro turno non può rispondere, nel suo può solo nella fase principale (posizione test della specifica).

**+1 o −3** (D-12). Teferi è una carta debole nel matchup: il −3 si usa **sempre** quando c'è un bersaglio ragionevole:

1. **Moonshadow avversario da far ripartire** (tre o meno segnalini): torna in mano e si rilancia con sei segnalini;
2. **una creatura con Detective's Phoenix conferita**: la Phoenix resta come creatura 2/2; oppure la Phoenix stessa, che torna in mano all'avversario;
3. **una nostra Solitude** in campo, per rifare l'entrata: si rilancia o si evoca di nuovo, se c'è un bersaglio per lei.

- **+1** altrimenti. La lista non ha stregonerie: il +1 serve solo a far crescere la fedeltà.
- **−3 anche senza un bersaglio di valore** se Teferi morirà comunque nel prossimo turno avversario (creature avversarie non bloccabili con forza pari alla fedeltà). Si pesca la carta prima di perderlo.

**Proteggerlo:** non da Lightning Bolt (D-17). Le creature che attaccano Teferi si bloccano come quelle che attaccano noi (sezione 21), con la differenza che un blocco di sacrificio per Teferi si fa solo se Teferi resterebbe sopra 0 e la creatura bloccante vale meno di lui (sezione 22) (D-12).

## 18. Force of Negation

"If it's not your turn, you may exile a blue card from your hand rather than pay this spell's mana cost. Counter target noncreature spell. If that spell is countered this way, exile it instead of putting it into its owner's graveyard."

Carta debole nel matchup (D-13). **Cosa contrastare**, tra le magie non creatura del pool (Lightning Bolt, Flare of Malice, Thoughtseize, Mishra's Bauble, Detective's Phoenix conferita come Aura):

1. **Flare of Malice**;
2. **Lightning Bolt sulla nostra creatura migliore** (sezione 22), oppure **letale per noi**.

Nient'altro: né Thoughtseize, né la Phoenix conferita, né Mishra's Bauble. *(Da far decidere all'albero.)*

Contrastata da Force of Negation, la magia va in esilio invece che nel cimitero: anche questo toglie un tipo (istantaneo, stregoneria) al cimitero avversario.

**Pagare o esiliare:** nel turno avversario si esilia una carta blu: **Consign to Memory prima di qualunque altra**, poi Teferi, **quasi mai Quantum Riddler** (solo se è l'unica carta blu). Nel nostro turno si paga {1}{U}{U} per gli stessi due casi.

## 19. Consign to Memory

"Replicate {1}. Counter target triggered ability or colorless spell." Le abilità attivate (fetch, fedeltà, Mishra's Bauble attivata) non si possono contrastare; le abilità innescate di entrambi i giocatori sì.

**Si usa quando il mana è disponibile**, mai tenendolo apposta (D-3, D-15). **Bersagli, dal migliore**, prima i nostri trigger:

1. **Il ritorno di Phelia, quando l'esilio ha preso un permanente avversario**: la carta resta in esilio per sempre.
2. **Il sacrificio di Solitude evocata**: Solitude resta in campo come 3/2 con legame vitale.
3. **L'esilio del warp** su Quantum Riddler o Starfield Shepherd, se Ephemerate e Phelia non bastano: la creatura resta in campo.
4. **Il trigger d'attacco di Fear of Missing Out con il delirium**: niente combattimento aggiuntivo e niente creatura stappata. È il miglior bersaglio avversario. Con Thraben Charm in mano si preferisce Thraben (sezione 11), che toglie anche il cimitero, salvo che servano i suoi {1}{W} per altro.
5. **I trigger minori avversari**: entrata di Fear of Missing Out, macinate di Stitcher's Supplier e Dark Matter Manipulator, segnalino di Moonshadow, pescata ritardata di Mishra's Bauble, sorveglianza di Raucous Theater. Si contrastano solo con il mana che altrimenti andrebbe perso, nella sottofase finale avversaria o nel nostro turno dopo le giocate. È l'uso più debole.

Mishra's Bauble lanciata (magia incolore) è un bersaglio possibile, ma vale come il punto 5.

**Replicare:** si paga una copia in più per ogni secondo bersaglio dei punti 1-4 presente nello stesso momento (per esempio due trigger di Fear of Missing Out), finché il mana lo permette. Mai copie per i trigger del punto 5.

**Mana aperto:** mai tenuto apposta per Consign (sezione 5).

## 20. Guide of Souls e Ocelot Pride

**Guide of Souls:** "Whenever another creature you control enters, you gain 1 life and get {E}. Whenever you attack, you may pay {E}{E}{E}. When you do, put two +1/+1 counters and a flying counter on target attacking creature."

**Quando spendere l'energia** (D-11): **sempre**, appena attacchiamo con almeno tre energie. **Bersaglio**, tra le creature attaccanti, dalla migliore:

1. **quella che con i due segnalini arriva a costituzione 4 o più**: esce dalla portata di Lightning Bolt;
2. **quella che vola meglio**: un bloccante avversario la fermerebbe, e con volare non può più essere bloccata (l'avversario non ha volanti stappati);
3. **quella con più forza**.

A parità nel punto 1 decide il punto 2, poi il 3. *Perché:* i segnalini restano, quindi prima arrivano più danno fanno; l'energia non ha altri usi nella lista; la costituzione 4 protegge la creatura dalla rimozione più comune dell'avversario.

**Ocelot Pride:** attacco improvviso, legame vitale; alla nostra sottofase finale, se abbiamo guadagnato vita, un gatto (con la benedizione della città, una copia di ogni token entrato nel turno). Le regole che contano:

- **guadagnare vita ogni turno** quando Ocelot è in campo: Guide of Souls, Solitude con legame vitale, Ocelot che colpisce;
- **attacca** se nessun bloccante la uccide sopravvivendo (con l'attacco improvviso uccide prima le creature con costituzione 1: Stitcher's Supplier, Moonshadow con sei segnalini, Nethergoyf con il cimitero vuoto);
- **la benedizione della città** (dieci permanenti) moltiplica i gatti: a nove permanenti, un permanente in più vale più di una giocata equivalente.

## 21. Attacchi e blocchi contro creature che crescono

Il motore chiede attaccanti e bloccanti una creatura alla volta, come per Rakdos.

**Attacca se vale almeno una di queste** (D-18), e se dopo l'attacco i nostri bloccanti rimasti ci tengono sopra la soglia di vita della sezione 1 nel prossimo turno avversario:

1. **Non può essere bloccata**: volare contro nessun volatore stappato, protezione contro bloccanti tutti neri o rossi (Sanctifier).
2. **Nessun blocco conviene all'avversario**: nessun bloccante la uccide sopravvivendo. Il conto tiene conto di attacco improvviso, protezione e dei segnalini di Guide.
3. **Fa parte di un attacco letale**.
4. **Phelia con un bersaglio utile** (sezione 9).
5. **Una creatura volante potenziata da Guide of Souls** (con i segnalini +1/+1 e volare) attacca sempre, salvo la soglia di vita (D-18).

**I token gatto non attaccano:** restano a bloccare (D-18). *Perché:* un gatto 1/1 fa un danno attaccando, ma bloccando ferma una creatura che cresce o ne assorbe l'attacco.

**Planeswalker avversari:** se il motore offre di attaccarne uno, la creatura attacca comunque il giocatore. *Perché:* nel pool nessun planeswalker è nella lista avversaria; la regola esiste per non lasciare la decisione a "predefinito", e andrà rivista se arrivasse un planeswalker avversario.

**Blocchi.** Le creature avversarie crescono: ogni turno in cui restano in campo pesano di più. Per ogni attaccante:

1. **Blocca se il bloccante uccide l'attaccante e sopravvive** (Ocelot Pride con l'attacco improvviso contro le costituzioni 1; Sanctifier contro qualsiasi creatura nera o rossa; Quantum Riddler 4/6).
2. **Blocca in due** se insieme uccidono un attaccante della sezione 6 tra le prime tre voci e muore al massimo il bloccante meno prezioso (sezione 22). Moonshadow (minaccia) va bloccata in due comunque; Troll of Khazad-dûm in tre.
3. **Scambia** (muoiono entrambi) se l'attaccante è più in alto nella sezione 6 del valore del bloccante (sezione 22). *Perché:* contro creature che crescono, uno scambio oggi vale più di uno domani.
4. **Blocco di sacrificio** solo se senza bloccare scenderemmo sotto la soglia di vita della sezione 1 (non solo a zero) o per salvare Teferi (sezione 17).
5. Altrimenti **non blocca**.

**Divisione del danno** tra più bloccanti: danno letale ai bloccanti nell'ordine della sezione 6, poi il resto all'ultimo.

**Il combattimento aggiuntivo di Fear of Missing Out:** le stesse regole, sullo stato aggiornato.

## 22. Valore delle carte

Serve per gli scambi in combattimento, il sacrificio di Flare of Malice, gli scarti, le carte da esiliare (Solitude, March, Force of Negation) e le carte da mettere in fondo.

**In mano**, dalla più preziosa (D-14):

1. Solitude
2. Quantum Riddler: molto in alto, perché il piano è svuotare la mano e poi ricaricare con il warp (sezione 16)
3. Sanctifier en-Vec
4. March of Otherworldly Light
5. Thraben Charm
6. Seam Rip
7. Guide of Souls
8. Ocelot Pride
9. Phelia
10. Ephemerate; **con una Solitude in mano sale sopra Phelia** (il trucco della sezione 7). Da sole Phelia ed Ephemerate sono deboli
11. Teferi, Time Raveler (carta debole nel matchup, D-12)
12. Recruiter of the Guard
13. Starfield Shepherd
14. Consign to Memory
15. Force of Negation (carta debole nel matchup, D-13)
16. Witch Enchanter

Le prime tre terre tra campo e mano valgono più di tutto; Witch Enchanter vale come una terra finché le terre sono meno di quattro, e con una Solitude in mano vale come la sua carta da esiliare (sezione 7). Le terre oltre la quinta valgono meno di tutto.

**In campo**, dalla meno preziosa (per Flare of Malice, che fa scegliere tra i permanenti di valore di mana più alto):

1. una creatura con il warp che andrà in esilio alla prossima sottofase finale;
2. Starfield Shepherd e Recruiter of the Guard (la ricerca è già fatta);
3. Witch Enchanter;
4. Solitude (il trigger è già usato), poi Quantum Riddler;
5. Teferi, il più prezioso: con Flare tra Teferi e una creatura di pari valore di mana si sacrifica la creatura.

## 23. Decisioni minori

| Decisione | Cosa fa l'agente |
| --- | --- |
| Sorvegliare 1 (Meticulous Archive) | Nel cimitero se è una terra e abbiamo almeno cinque terre tra campo e mano, o se è una carta in fondo alla classifica della sezione 22 senza bersagli in campo (Witch Enchanter senza artefatti o incantesimi avversari); altrimenti resta in cima |
| Scarto a fine turno | La carta di valore più basso (sezione 22) |
| Ordine dei trigger | Solitude evocata: il sacrificio in pila per primo, l'esilio sopra (sezione 7). Ritorno di Phelia ed esilio del warp insieme: l'ordine non cambia l'esito (test T-1), prima il ritorno. Trigger di Guide of Souls e altri: prima si risolvono quelli che danno vita, poi gli altri. *Perché:* la vita guadagnata dà il gatto di Ocelot e la vita conta prima delle altre scelte |
| Modo di pagare il mana | Si tengono stappate le terre dei colori ancora necessari per le istantanee della sezione 5 ({W} per Ephemerate, {U} per Consign); si paga prima con Plains, poi con le fetch già rotte, per ultime Hallowed Fountain e Meticulous Archive (bicolori) |
| Ricerca senza nulla da trovare | Non cerca nulla (succede solo a grimorio senza carte del tipo) |
| Regola delle leggende (Phelia, Teferi) | Si tiene la copia con più segnalini (+1/+1 per Phelia, fedeltà per Teferi); a parità, quella che può già attaccare |
| Sacrificio per Flare of Malice | Tra i permanenti di valore più alto offerti, il meno prezioso della sezione 22 (in campo) |
| Rimbalzo di Ephemerate | Solo su una creatura con un'entrata rilevante, mai su un attaccante in corsa (sezione 8); altrimenti si rifiuta |
| Copie di Consign to Memory | Ogni copia prende un bersaglio nuovo dei punti 1-4 della sezione 19, altrimenti tiene il bersaglio (dominato: non succede, perché le copie si pagano solo per bersagli nuovi) |

**Limiti noti** (per la revisione e per il confronto con la ricerca ad albero):

- non gioca attorno a Lightning Bolt e Flare of Malice nelle giocate; ne tiene conto solo nella scelta dei bersagli (sezione 1);
- tiene mana aperto per una sola istantanea alla volta;
- pianifica un turno alla volta;
- non usa l'informazione su quali delle sue carte l'avversario conosce, salvo i pareggi tra copie identiche.

## 24. Ogni decisione del motore

Ogni tipo di decisione ha una regola: nessuna scelta resta a "predefinito".

| Decisione del motore | Sezione |
| --- | --- |
| Mulligan, carte in fondo | 2 |
| Priorità (terra, magie, abilità, passare) | 3, 4, 5, 7-19 |
| Vita o terra tappata (Hallowed Fountain, Witch-Blessed Meadow) | 3 |
| Ricerca della fetch, di Recruiter, di Starfield Shepherd | 3, 15 |
| Sorveglianza | 23 |
| Scarto a fine turno | 23 |
| Regola delle leggende | 23 |
| Bersaglio di una magia o di un'abilità | 6-19 |
| Modo della magia (Thraben Charm) | 11 |
| Valore di X (March of Otherworldly Light) | 10 |
| Carte dalla mano come costo (Solitude evocata, March) | 7, 10 |
| Force of Negation con il costo alternativo | 18 |
| Quante copie replicare, bersagli delle copie | 19, 23 |
| Pagamento del mana | 23 |
| Attaccanti, bloccanti, divisione del danno | 21 |
| Ordine dei trigger | 23 |
| Pagare le energie di Guide of Souls | 20 |
| Rilancio dall'esilio (rimbalzo) | 8 |
| Sacrificio per Flare of Malice | 22, 23 |

Decisioni del motore che UW non incontra con questa lista: carta dalla mano di Thoughtseize, esilio dal cimitero (fuga e prove), scarto di Fear of Missing Out, sacrificio come costo di Flare. Se arrivassero, sarebbe un errore del motore o della lista, e il test lo segnalerebbe come "predefinito".

**Approssimazioni del motore che l'agente eredita** (audit 1, B-4 e B-5): March offre solo gli X pari al valore di mana di un bersaglio; Consign offre copie solo per altri bersagli; le carte da esiliare si scelgono dopo il bersaglio. Nessuna cambia le regole di questo documento.

## 25. Come si verifica

UW non ha un goldfish: il suo mazzo non vince da solo, e la misura che conta è il matchup. L'agente si verifica così:

- **Test per regola**, come per Rakdos: una posizione preparata per ogni regola, la scelta attesa e la regola registrata.
- **Le posizioni test della specifica** (`docs/progetto.md`), che diventano test dell'agente: March con X=1; Sanctifier al turno 2; Thraben Charm con l'avversario a un tipo dal delirium; Teferi prima di attaccare con il rosso avversario aperto; Solitude su Nethergoyf con la Phoenix conferita. A queste si aggiungono le interazioni di questo documento: il trucco di Solitude con Ephemerate, il warp salvato da Ephemerate o da Phelia, Thraben Charm e Consign to Memory sul trigger di Fear of Missing Out.
- **Nessun "predefinito"** in una serie di partite del matchup (sezione 1).
- **Contatori delle regole** nel matchup, per vedere quali regole lavorano e quali non scattano mai.
- **Test A/B** con gli stessi seed per le regole segnate come incerte nella sezione 26.
- **Le regole "da far decidere all'albero"** sono i primi confronti con la ricerca ad albero: per ognuna si misura quanto spesso l'albero sceglie diversamente e quanto vale la differenza.

## 26. Domande per Daniele

Revisione del 10/11/2026 con Daniele, pilota di UW Blink: tutte le domande hanno una risposta. **Stato:** "decisa" è una regola di partenza di Daniele; "da far decidere all'albero" è una regola di partenza che la ricerca ad albero confermerà o cambierà.

| ID | Domanda | Risposta e regola | Stato |
| --- | --- | --- | --- |
| D-1 | Witch Enchanter come terra | Nel mulligan conta come terra. In partita si gioca come terra solo se serve alla curva o al piano; altrimenti resta in mano come carta bianca per Solitude, il suo uso principale. 3 vita solo se il mana serve subito. Distruggere FOMO o la Phoenix è un bonus di situazione: si controlla che i due tipi in più (creatura e incantesimo) non diano il delirium (sezioni 2, 3, 7, 13) | Decisa |
| D-2 | Mulligan | 2-4 terre, oppure una con Solitude ed Ephemerate insieme. Senza risposte: mulligan, tranne sul play con almeno tre creature da un mana (sezione 2) | Decisa |
| D-3 | Mana aperto | {1}{W} per Thraben Charm quando c'è un uso previsto (cimitero in combattimento o a fine turno, oppure rimozione). Mai mana tenuto solo per Consign (sezione 5) | Decisa |
| D-4 | Solitude evocata | In modo aggressivo contro le minacce della classifica; sempre con Quantum Riddler in mano, che restituirà le carte (sezione 7) | Decisa |
| D-5 | Ephemerate | Prima i pezzi con il warp o l'evocazione, poi la protezione, poi il valore. Rimbalzo solo su creature con un'entrata rilevante; in corsa non si rimbalza un attaccante (sezione 8) | Decisa |
| D-6 | Classifica delle minacce | Stitcher's Supplier si tratta come una magia: niente rimozione su di lui. Per le altre creature resta la classifica della sezione 6 | Da far decidere all'albero |
| D-7 | Phelia | Regola di partenza della sezione 9; per Daniele è un problema "da scacchi" | Da far decidere all'albero |
| D-8 | Warp | Mai Quantum Riddler con il warp al secondo turno: si usa come ricarica, a mano quasi vuota, quando pesca due carte (sezione 16) | Decisa |
| D-9 | Thraben Charm | Anche il danno, quando uccide una minaccia della classifica e l'esilio del cimitero vale meno (sezione 11) | Da far decidere all'albero |
| D-10 | Sanctifier en-Vec | Regola di partenza della sezione 14 | Da far decidere all'albero |
| D-11 | Energia di Guide of Souls | Sempre, appena possibile. Bersaglio: la creatura che arriva a costituzione 4 o più, poi quella che vola meglio (sezione 20) | Decisa |
| D-12 | Teferi | Carta debole nel matchup: −3 sempre con un bersaglio ragionevole (Moonshadow da far ripartire, Phoenix conferita, la nostra Solitude per rifare l'entrata), altrimenti +1 (sezione 17) | Decisa |
| D-13 | Force of Negation | Carta debole nel matchup: contrasta Flare of Malice, o Bolt sulla nostra creatura migliore o letale. Si esilia Consign prima di qualunque altra carta blu, quasi mai Quantum Riddler (sezione 18) | Da far decidere all'albero |
| D-14 | Valore delle carte in mano | Phelia ed Ephemerate da sole sono deboli: Phelia prima di Ephemerate, tranne con Solitude in mano. Quantum Riddler molto in alto: si svuota la mano per poi ricaricare con il warp (sezioni 16, 22) | Decisa |
| D-15 | Consign to Memory | Sui nostri trigger (ritorno di Phelia, sacrificio di Solitude evocata, esilio del warp) quando il mana è disponibile (sezione 19) | Decisa |
| D-16 | Soglia di vita | Nei primi tre turni si paga per seguire la curva; dopo, la soglia (forza avversaria in campo più 3, almeno 5) (sezione 1) | Da far decidere all'albero |
| D-17 | Giocare attorno a Bolt e Flare | Teferi non va protetto da Bolt. Bolt conta invece nella scelta dei bersagli: Guide of Souls e i lampeggi su creature che restano con costituzione 3 o meno sono esposti (sezione 1) | Decisa |
| D-18 | Attacchi di UW | Si attacca con le creature volanti potenziate da Guide; i token gatto restano a bloccare (sezione 21) | Decisa |

## 27. Scelte fatte implementando (11/11/2026)

Dove il documento non decideva, l'implementazione ha scelto così. Ognuna è una regola con il suo contatore.

| Tema | Scelta |
| --- | --- |
| Due risposte per la stessa minaccia | Quella che costa meno carte: March pagata con il mana, poi Solitude a costo pieno, poi Solitude evocata. È la posizione test della specifica (March con X=1 risparmia le due carte di Solitude evocata) |
| Quando si evoca Solitude nel turno avversario | Quando una minaccia della classifica attacca (dopo la dichiarazione degli attaccanti), oppure nella sottofase finale avversaria se la minaccia è ancora in campo |
| Rimozione a velocità stregoneria prima di attaccare (sezione 4, punto 8) | Non implementata: le risposte di UW sono istantanee e si usano nel turno avversario. Seam Rip resta l'unica rimozione del nostro turno |
| Phelia | Si lancia solo con il lampo, alla fine del turno avversario |
| Warp di Quantum Riddler come ricarica | Con due carte o meno in mano prima del lancio (una o nessuna dopo), mai nel secondo turno di UW |
| "Mana necessario ora" (Hallowed Fountain, Witch-Blessed Meadow, Archive, ricerca della fetch) | Si contano solo le giocate previste della sezione 4 (creature senza lampo, Teferi, Seam Rip, i warp), non le istantanee. La terra che sta entrando non conta come mana già disponibile |
| Attacco letale | Viene prima di ogni altra regola d'attacco, gatti compresi: con un attacco letale non servono bloccanti. Senza questa regola, nelle partite bloccate i gatti di Ocelot Pride si accumulavano fino al limite dei token del motore |
| Mana tenuto aperto | Una sola istantanea, nell'ordine: Thraben Charm con un uso previsto; Ephemerate con una creatura con il warp in campo o Solitude in mano con un bersaglio; March con un bersaglio pagabile. Una giocata della fase principale si fa solo se lascia quel mana |
| Bolt su una nostra creatura | Prima Ephemerate (una carta), se la creatura muore al Bolt; poi Force of Negation, solo se è la nostra creatura migliore |
| Trigger minori avversari e replicare | Consign non li contrasta, e non si replica mai: nella prima implementazione nessuna coppia di bersagli di valore arriva insieme |
| Teferi −3 "se morirà comunque" | Non implementato: si usa il −3 solo con un bersaglio ragionevole (D-12) |
| Blocchi | Scambi e blocchi in due solo contro le prime tre voci della classifica; blocco di sacrificio se senza bloccare la vita scende sotto 5 |
| Thraben Charm con Nethergoyf in combattimento | Nello step dei bloccanti, se Nethergoyf (forza 2 o più) attacca o blocca |
| "In corsa" per il rimbalzo | Il danno per turno è la forza delle nostre creature che le regole d'attacco manderebbero all'attacco |
| Sorvegliare 1 | Nel cimitero solo una terra in più (cinque o più tra campo e mano) |
