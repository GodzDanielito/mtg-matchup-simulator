# Agente euristico di UW Blink (proposta, 10/11/2026)

Questo documento descrive come giocherà l'agente euristico di UW Blink: regole scritte a mano, ognuna con il suo motivo. È una **proposta da approvare**: niente codice finché non è rivista. Le decisioni di strategia su cui non sono sicuro sono raccolte nella sezione 26 ("Domande per Daniele"); le regole che dipendono da una di quelle domande lo dicono con il riferimento (D-n). Struttura e principi sono quelli di `docs/agente_rakdos.md`.

La lista è quella di simulazione, `config/decks/uw_blink_sim.json`. I testi delle carte citati vengono dal bulk Oracle di Scryfall del 9/11/2026, lo stesso dei test. Le regole del gioco sono le Comprehensive Rules in vigore dal 6/11/2026.

## 1. Principi

- **Vede solo quello che vedrebbe il giocatore.** La propria mano, le zone pubbliche (campo, cimiteri, esilio, pila), i punti vita, l'energia, quante carte ha in mano l'avversario. Non legge mai la mano avversaria né l'ordine dei grimori. Sa quali delle proprie carte l'avversario conosce (per esempio dopo un Thoughtseize): è informazione sua.
- **Non sa chi ha davanti, ma vede lo stato pubblico.** Nessuna regola dipende dal tipo di avversario; tutte dipendono da quello che c'è sul tavolo: creature, segnalini, carte nei cimiteri, colori mostrati. Le regole nominano le carte avversarie perché le carte sono pubbliche, non perché l'agente sa che è Rakdos. Dove il motivo di una regola è il matchup, la regola resta scritta sullo stato: per esempio "si preferisce esiliare" vale sempre, e il motivo è che le carte nei cimiteri avversari possono aiutare l'avversario.
- **Un piano per turno, una lista di priorità.** Nella propria fase principale l'agente scorre una lista fissa di giocate (sezione 4) e fa la prima legale e conveniente. Nel turno avversario reagisce secondo le regole del mana aperto (sezione 5). Le altre decisioni (bersagli, scelte di carte, blocchi) hanno regole proprie.
- **Non gioca attorno ai trucchi**, nella prima versione: non tiene conto di Lightning Bolt o Flare of Malice che l'avversario potrebbe avere in mano. Lo stesso limite dell'agente di Rakdos, misurato dal confronto con la ricerca ad albero (D-17).
- **Esiliare è meglio che distruggere.** Tra due risposte che tolgono la stessa minaccia si sceglie quella che esilia, o che la rimette in mano. *Perché:* ogni carta che finisce in un cimitero avversario può dare un tipo per il delirium, forza a Nethergoyf, prove per la Phoenix, carte per Dark Matter Manipulator, segnalini tolti a Moonshadow; e le creature che muoiono possono avere trigger (Stitcher's Supplier). **Eccezione dello stato:** con un nostro Sanctifier en-Vec in campo le carte nere e rosse vanno comunque in esilio, quindi distruggerle equivale a esiliarle.
- **Soglia di vita.** Le azioni che costano vita per scelta (fetch, Hallowed Fountain pagata, Witch-Blessed Meadow pagata) si fanno solo se dopo il pagamento la vita resta **sopra la forza totale delle creature avversarie in campo più 3**. Vale anche **almeno 5** come pavimento fisso, salvo che il pagamento vinca la partita. *Perché:* il "+3" è il danno di un Lightning Bolt, e la forza in campo è quello che può arrivare nel prossimo attacco; la soglia è scritta sullo stato pubblico (D-16).
- **Pareggi.** Tra opzioni equivalenti per le regole sceglie a caso, con un generatore suo: stesso seed, stessa partita.
- **Contatori delle regole, nessun "predefinito".** Ogni decisione registra la regola che l'ha presa. A differenza della prima versione dell'agente di Rakdos, **ogni tipo di decisione del motore ha una regola esplicita** (sezione 24): la scelta di riserva esiste solo come rete di sicurezza, e il test dell'agente pretende **zero decisioni "predefinito"** in una serie di partite del matchup.

## 2. Mulligan

**Con 7 carte si tiene se:**

- ci sono **da 2 a 4 terre** (come si conta Witch Enchanter: sotto);
- c'è almeno **una giocata entro il secondo turno**: una creatura da uno o due mana (Ocelot Pride, Guide of Souls, Phelia, Sanctifier en-Vec), Seam Rip, oppure un warp da due mana (Quantum Riddler, Starfield Shepherd);
- c'è almeno **una risposta**: Solitude, March of Otherworldly Light, Sanctifier en-Vec, Seam Rip o Teferi;
- le terre producono i colori per la giocata del secondo turno (il blu per il warp di Quantum Riddler).

*Perché:* è la regola di partenza della specifica (`docs/progetto.md`, "Mulligan e play/draw"), con le giocate economiche e le risposte di questa lista. UW perde le partite in cui l'avversario sviluppa due creature che crescono senza trovare né un blocco né una risposta.

**Witch Enchanter nel conto delle terre.** Conta come terra solo per arrivare al minimo di due, non per il massimo di quattro. *Perché:* il retro Witch-Blessed Meadow è una terra vera (3 vita o tappata); con molte terre resta una creatura da quattro mana (D-1).

Una sola terra, o nessuna, è sempre mulligan con 7 carte (D-2).

**Con 6 carte** si tiene con 2-4 terre (Witch compresa, come sopra) e almeno una giocata o una risposta. **Con 5 o meno** si tiene sempre. Le terre si contano dopo aver messo in fondo.

**Carte da mettere in fondo** (mulligan London), una alla volta:

1. le terre oltre la terza, tenendo prima Hallowed Fountain, poi le fetch, poi Plains, per ultima Meticulous Archive;
2. poi la carta di valore più basso nella classifica della sezione 22, senza mai lasciare la mano senza la sua unica risposta o la sua unica giocata del secondo turno.

*Perché:* tre terre lanciano quasi tutto (Teferi, Recruiter, i warp); Solitude e Starfield Shepherd si giocano anche senza cinque terre (evocazione, warp).

## 3. Terre, fetch e shockland

Le terre della lista: 4 Arid Mesa, 2 Flooded Strand, 4 Windswept Heath, 4 Hallowed Fountain, 2 Meticulous Archive, 3 Plains, e i 4 retri di Witch Enchanter. Le fetch cercano solo Plains, Hallowed Fountain e Meticulous Archive (tipi Plains e Island).

**Blood Moon** è fuori dal main di Rakdos per ora (solo gara 1): nessuna regola lo considera. Nel Bo3 andrà rivisto l'ordine delle ricerche (prima le Plains base), ma oggi non serve e non si anticipa.

**Quale terra giocare**, nell'ordine:

1. **Meticulous Archive** se il mana di questo turno non serve. *Perché:* entra tappata, quindi è meglio giocarla quando non costa nulla; sorvegliare 1 migliora la pescata.
2. **Una fetch**, rotta subito (cerca: sotto). *Perché:* rotta subito è una terra stappata; tenerla non dà nulla in questo pool.
3. **Plains**, poi **Hallowed Fountain**.
4. **Witch-Blessed Meadow** (il retro di Witch Enchanter) solo se non c'è un'altra terra da giocare e senza una terra in questo turno resteremmo indietro: meno di quattro terre in campo, oppure il mana serve per una giocata prevista. Senza pagare 3 vita, salvo che il mana serva subito e la soglia lo permetta (D-1).

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
6. **Warp da due mana**, con i limiti della sezione 16 (Quantum Riddler, Starfield Shepherd), oppure **Phelia** se non la teniamo per il lampo.
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
| Consign to Memory | {U} | L'avversario ha in campo Fear of Missing Out non tappato e nel cimitero ha già quattro tipi, oppure tre e una carta che il turno dopo ne porta un quarto in modo pubblico (sezione 19) |
| Thraben Charm | {1}{W} | Come sopra per Fear of Missing Out; oppure un Nethergoyf avversario con forza 4 o più (sezione 11) |
| Ephemerate | {W} | Abbiamo in campo una creatura con il warp da salvare (sezione 8), oppure in mano una Solitude da evocare con un bersaglio (sezione 7) |
| Solitude a costo pieno | {3}{W}{W} | Mai tenuti apposta: Solitude si evoca (sezione 7) |
| Phelia (lampo) | {1}{W} | Se resta mana alla fine del turno avversario e Phelia non è stata lanciata (sezione 9) |
| March of Otherworldly Light | X+1 | L'avversario ha una creatura che la sezione 6 vuole togliere e che March può esiliare con il mana che abbiamo |

Force of Negation nel turno avversario non chiede mana (sezione 18).

**Cosa fare con il mana tenuto e non usato:** nella sottofase finale avversaria si spende, nell'ordine: Phelia con lampo, March su una minaccia della sezione 6, Thraben Charm sul cimitero avversario (sezione 11), Ephemerate su una creatura con un'entrata utile (sezione 8). *Perché:* il mana non speso nel turno avversario è perso, e la sottofase finale è il momento in cui si è visto tutto.

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
6. **Stitcher's Supplier**: ultimo, e solo con l'esilio. Se muore macina tre carte.

*Perché:* l'ordine segue il danno che la creatura farà nei prossimi turni; le creature che crescono vanno tolte prima che crescano. Il trigger di FOMO va in cima perché moltiplica gli attacchi di tutte le altre (D-6).

**Esilio prima della distruzione** (sezione 1): con due risposte disponibili per lo stesso bersaglio si usa quella che esilia; il danno (Thraben Charm) e la distruzione (Witch Enchanter, Thraben Charm) vengono dopo.

## 7. Solitude

"When this creature enters, exile up to one other target creature. That creature's controller gains life equal to its power. Evoke—Exile a white card from your hand." Lampo, legame vitale, 3/2.

**Lanciare o evocare:**

- **A costo pieno** ({3}{W}{W}) quando abbiamo cinque mana: resta in campo una 3/2 con legame vitale. Con il lampo, nel turno avversario: durante la sua dichiarazione degli attacchi, dopo che ha scelto gli attaccanti, oppure nella sua sottofase finale.
- **Evocata** quando una minaccia tra le prime tre della sezione 6 attacca o sta per attaccare, e non abbiamo cinque mana. Anche nel nostro turno, se toglie il bloccante che ferma un attacco letale.
- **Evocata con Ephemerate** (il trucco): con {W} disponibile ed Ephemerate in mano, si evoca e, con il trigger di sacrificio in pila, si lancia Ephemerate su Solitude. Solitude rientra come oggetto nuovo: nessun sacrificio, un secondo trigger di esilio, e resta in campo. È la giocata migliore della mano quando ci sono due bersagli.
- **Non si evoca** contro le voci 4-6 della sezione 6: per quelle si aspetta il costo pieno, March o Seam Rip. *Perché:* l'evocazione costa due carte; vale solo contro una minaccia vera (D-4).

**Ordine dei due trigger** (entrata ed evocazione): il sacrificio va in pila per primo, l'esilio sopra, così l'esilio si risolve per primo. Con Ephemerate o Consign to Memory in mano si risponde al trigger di sacrificio quando è in cima.

**Quale carta bianca esiliare** per l'evocazione (la carta di valore più basso, sezione 22, tra le bianche):

1. un'altra Solitude se ne abbiamo tre o più tra mano e campo;
2. Witch Enchanter se abbiamo già almeno quattro terre tra campo e mano (altrimenti è una terra);
3. Recruiter of the Guard, Starfield Shepherd, poi le altre in ordine di valore;
4. mai Ephemerate se il piano è il trucco con Ephemerate, mai l'ultima risposta rimasta.

**Bersaglio:** la prima minaccia della sezione 6. Solitude non bersaglia mai le nostre creature. *Perché:* la vita va al controllore della creatura esiliata; per noi non c'è nulla da guadagnare.

## 8. Ephemerate

"Exile target creature you control, then return it to the battlefield under its owner's control. Rebound." La creatura torna come oggetto nuovo: perde segnalini, danno e Aure, e si rifanno i suoi trigger di entrata.

**Su cosa**, dalla giocata migliore:

1. **Solitude evocata, con il trigger di sacrificio in pila** (sezione 7): Solitude resta e il suo esilio scatta di nuovo.
2. **Una creatura con il warp, prima della sottofase finale** (Quantum Riddler, Starfield Shepherd): torna come oggetto nuovo, il trigger del warp non la trova più e resta in campo per sempre. Rientrando fa di nuovo il suo trigger (pescata, ricerca). La regola è provata dal test T-1 dell'audit.
3. **Salvare una creatura da un bersaglio**: in risposta a Lightning Bolt su una nostra creatura di valore (sezione 22) o su una creatura che altrimenti morirebbe in combattimento. Bolt perde il bersaglio. *Non* salva da Flare of Malice: il sacrificio si sceglie alla risoluzione.
4. **Un'entrata utile**: Solitude lanciata (un altro esilio, se c'è un bersaglio), Witch Enchanter (se l'avversario ha un artefatto o un incantesimo), Sanctifier en-Vec (se nei cimiteri ci sono carte nere o rosse), Recruiter of the Guard, Quantum Riddler (una carta), Starfield Shepherd (una ricerca). Si gioca nella sottofase finale avversaria, con il mana tenuto e non usato (sezione 5).

**Guide of Souls** guadagna 1 vita e un'energia per ogni creatura che rientra (è "another creature"): un'Ephemerate vale sempre almeno questo.

**Rimbalzo, al nostro mantenimento:** si rilancia sempre. Rifiutarlo lascia la carta in esilio per sempre. **Bersaglio:** la creatura con l'entrata più utile (punto 4). A parità, una che non serve per attaccare in questo turno. *Perché:* la creatura rientrata è un oggetto nuovo e non può attaccare nel turno in cui rientra (D-5).

## 9. Phelia, Exuberant Shepherd

"Flash. Whenever Phelia attacks, exile up to one other target nonland permanent. At the beginning of the next end step, return that card to the battlefield under its owner's control. If it entered under your control, put a +1/+1 counter on Phelia."

**Lancio:** con il lampo alla fine del turno avversario, se resta il mana (sezione 5); nel nostro turno solo se non c'è niente di meglio da fare con due mana. *Perché:* con il lampo evita la rimozione a velocità stregoneria e attacca comunque nel nostro turno.

**Attacco:** Phelia attacca se c'è un bersaglio utile qui sotto **e** sopravvive al blocco migliore dell'avversario, oppure se nessun blocco conviene all'avversario (sezione 21) (D-7).

**Bersaglio dell'esilio**, dal migliore:

1. **Una nostra creatura con il warp** (Quantum Riddler, Starfield Shepherd): torna alla sottofase finale come oggetto nuovo e resta per sempre, e Phelia prende il segnalino (test T-1 dell'audit).
2. **Il bloccante avversario** che ferma un attacco letale o un attacco importante.
3. **Una nostra creatura con un'entrata utile** (la lista della sezione 8, punto 4), o una Solitude lanciata con un bersaglio: rientra alla sottofase finale e rifà il trigger; Phelia prende il segnalino.
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
3. **Esiliare il cimitero avversario nella sua sottofase finale**, se ha quattro o più tipi e ha in campo Fear of Missing Out o Nethergoyf, o nel cimitero ci sono Detective's Phoenix o Nethergoyf da rilanciare. *Perché:* toglie delirium, forza, prove per la Phoenix e la fuga, tutto insieme.
4. **Danno** a una minaccia della sezione 6 che il doppio delle nostre creature uccide, quando non c'è un modo migliore per toglierla. Mai su Stitcher's Supplier.
5. **Distruggere un incantesimo**: Fear of Missing Out o una Phoenix conferita, solo se nessuna risposta che esilia è disponibile.

**Quali cimiteri:** solo quello avversario. *Perché:* il nostro cimitero non ci serve, ma non ci danneggia; esiliarlo non dà nulla.

**Quando:** il modo 1 è la ragione principale per tenere {1}{W} aperti (sezione 5). Il modo 3 si gioca alla fine del turno avversario solo se il modo 1 non è più probabile: Fear of Missing Out non è in campo (D-9).

## 12. Seam Rip

"When this enchantment enters, exile target nonland permanent an opponent controls with mana value 2 or less until this enchantment leaves the battlefield."

**Bersaglio:** la prima minaccia della sezione 6 con valore di mana 2 o meno: in questo pool lo sono tutte le creature da uno e due mana (Nethergoyf, Moonshadow, Dark Matter Manipulator, Stitcher's Supplier, Fear of Missing Out) e Mishra's Bauble.

**Quando:** nella prima fase principale, appena c'è un bersaglio tra le prime tre voci della sezione 6, oppure un Fear of Missing Out. *Perché:* costa {W} ed esilia una creatura che cresce prima che cresca; l'esilio impedisce anche la fuga di Nethergoyf. Un Seam Rip in mano senza bersaglio aspetta.

**Rischio:** se Seam Rip lascia il campo la carta torna, come oggetto nuovo (Moonshadow con sei segnalini). Nessuna nostra carta lo toglie volontariamente (sezione 9).

## 13. Witch Enchanter

"When this creature enters, destroy target artifact or enchantment an opponent controls." Il retro è una terra (sezione 3).

**Bersagli validi:** Mishra's Bauble, Fear of Missing Out (creatura incantesimo), Detective's Phoenix (creatura incantesimo, anche conferita).

**Lanciarla come creatura** solo se c'è un bersaglio che vale la carta:

1. **Fear of Missing Out**, soprattutto con il delirium vicino o presente;
2. **una Detective's Phoenix conferita su una creatura grande**: la creatura perde +2/+2, volare e rapidità. La Phoenix va nel cimitero e può essere conferita di nuovo da lì, quindi vale meno di quanto sembra;
3. **Detective's Phoenix come creatura**: ultima scelta, perché distrutta torna disponibile dal cimitero.

Mai su Mishra's Bauble: costa zero e si sacrifica in risposta.

**Altrimenti è una terra** (sezione 3) o una carta bianca da esiliare (sezioni 7 e 10). *Perché:* distruggere manda la carta nel cimitero avversario (sezione 1). Una Witch Enchanter senza bersaglio vale soprattutto come terra.

## 14. Sanctifier en-Vec

Protezione dal nero e dal rosso; entrando esilia tutte le carte nere o rosse dai cimiteri; finché è in campo, le carte nere o rosse che andrebbero in un cimitero vanno in esilio.

**Quando:** appena l'avversario ha mostrato una carta nera o rossa (in campo, nel cimitero, in pila, anche una terra che produce quei colori). È la giocata da due mana più importante della lista e viene prima delle altre (sezione 4). Esempio della specifica: Sanctifier al turno 2, con il cimitero avversario quasi vuoto, si gioca subito.

**In combattimento:** non può essere bloccato da creature nere o rosse e non subisce danno da loro.

- **Attacca** se dopo l'attacco i nostri bloccanti rimasti bastano a non scendere sotto la soglia di vita della sezione 1 nel prossimo turno avversario.
- Altrimenti **resta a bloccare** la creatura avversaria più forte: la blocca senza subire danno (D-10).

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

**Warp o costo pieno:**

- **Warp** al secondo o terzo turno se non c'è una giocata migliore da due mana (Sanctifier, Seam Rip su una minaccia, Phelia da tenere per il lampo). Riddler con il warp pesca, Starfield cerca: entrambi poi tornano disponibili dall'esilio.
- **Warp con un piano per tenerla:** con Ephemerate in mano e {W} libero, oppure con Phelia in campo che attaccherà in questo turno, il warp diventa una creatura permanente (sezioni 8 e 9). In quel caso si lancia nella prima fase principale, prima di Phelia.
- **Costo pieno** (D-8) con cinque mana e niente di più urgente: un 4/6 volante è un muro contro le creature di terra e Lightning Bolt non lo uccide.
- **Dall'esilio**, a costo pieno, alle stesse condizioni del costo pieno dalla mano.

**Senza un piano per tenerla**, la creatura con il warp si lancia nella **seconda** fase principale. *Perché:* non può attaccare (nessuna rapidità), quindi serve solo il trigger di entrata; lanciata dopo il combattimento, l'avversario sa meno sul nostro turno.

**Flare of Malice:** Quantum Riddler e Starfield Shepherd hanno valore di mana 5. Se l'avversario lancia Flare mentre una creatura con il warp sta per tornare in esilio, quella è il sacrificio (sezione 22).

## 17. Teferi, Time Raveler

"Each opponent can cast spells only any time they could cast a sorcery. +1: Until your next turn, you may cast sorcery spells as though they had flash. −3: Return up to one target artifact, creature, or enchantment to its owner's hand. Draw a card." Fedeltà 4.

**Quando lanciarlo:** nella prima fase principale, prima del combattimento, se l'avversario ha mana stappato. Altrimenti appena si hanno tre mana e niente di più urgente. *Perché:* la statica toglie all'avversario Bolt e Flare istantanei: nel nostro turno non può rispondere, nel suo può solo nella fase principale (posizione test della specifica).

**+1 o −3:**

- **−3** se c'è un bersaglio che vale una carta:
  1. **Moonshadow avversario** con tre o meno segnalini: torna in mano e si rilancia con sei segnalini;
  2. **una creatura con Detective's Phoenix conferita**: la Phoenix resta come creatura 2/2; oppure la Phoenix stessa, che torna in mano all'avversario;
  3. **una creatura avversaria che ci ucciderebbe** nel prossimo attacco;
  4. **una nostra Solitude lanciata**, per rilanciarla o evocarla di nuovo, se c'è un secondo bersaglio per lei.
- **+1** altrimenti. La lista non ha stregonerie: il +1 serve solo a far crescere la fedeltà, che a 5 esce dal raggio di Lightning Bolt.
- **−3 anche senza un bersaglio di valore** se Teferi morirà comunque nel prossimo turno avversario (creature avversarie non bloccabili con forza pari alla fedeltà). Si pesca la carta prima di perderlo.

**Proteggerlo:** le creature che attaccano Teferi si bloccano come quelle che attaccano noi (sezione 21), con la differenza che un blocco di sacrificio per Teferi si fa solo se Teferi resterebbe sopra 0 e la creatura bloccante vale meno di lui (sezione 22) (D-12).

## 18. Force of Negation

"If it's not your turn, you may exile a blue card from your hand rather than pay this spell's mana cost. Counter target noncreature spell. If that spell is countered this way, exile it instead of putting it into its owner's graveyard."

**Cosa contrastare** (magie non creatura; nel pool: Lightning Bolt, Flare of Malice, Thoughtseize, Mishra's Bauble, Detective's Phoenix conferita come Aura):

1. **Flare of Malice** che farebbe sacrificare una nostra creatura o Teferi di valore (sezione 22);
2. **Lightning Bolt** su Teferi, su una creatura tra le più preziose (sezione 22), oppure letale per noi;
3. **Detective's Phoenix conferita** su una creatura che diventerebbe la prima minaccia della sezione 6, o che ci darebbe un danno letale;
4. **Thoughtseize** solo nei primi due turni, se in mano abbiamo una carta che l'avversario prenderebbe e che non possiamo sostituire (D-13);
5. mai Mishra's Bauble (costa zero all'avversario, e Consign to Memory basta).

Contrastata da Force of Negation, la magia va in esilio invece che nel cimitero: anche questo toglie un tipo (istantaneo, stregoneria) al cimitero avversario.

**Pagare o esiliare:** nel turno avversario si esilia una carta blu, la meno preziosa (Consign to Memory, poi Quantum Riddler, mai Teferi). Nel nostro turno si paga {1}{U}{U} solo per i punti 1 e 2.

## 19. Consign to Memory

"Replicate {1}. Counter target triggered ability or colorless spell." Le abilità attivate (fetch, fedeltà, Mishra's Bauble attivata) non si possono contrastare; le abilità innescate di entrambi i giocatori sì.

**Bersagli, dal migliore** (molti dei migliori sono trigger nostri; D-15):

1. **Il trigger d'attacco di Fear of Missing Out con il delirium**: niente combattimento aggiuntivo e niente creatura stappata. È il miglior bersaglio avversario. Con Thraben Charm in mano si preferisce Thraben (sezione 11), che toglie anche il cimitero, salvo che servano i suoi {1}{W} per altro.
2. **Il ritorno di Phelia, quando l'esilio ha preso un permanente avversario**: la carta resta in esilio per sempre.
3. **Il sacrificio di Solitude evocata**: Solitude resta in campo come 3/2 con legame vitale.
4. **L'esilio del warp** su Quantum Riddler o Starfield Shepherd, se Ephemerate e Phelia non bastano: la creatura resta in campo.
5. **I trigger minori avversari**: entrata di Fear of Missing Out, macinate di Stitcher's Supplier e Dark Matter Manipulator, segnalino di Moonshadow, pescata ritardata di Mishra's Bauble, sorveglianza di Raucous Theater. Si contrastano solo con il mana che altrimenti andrebbe perso, nella sottofase finale avversaria o nel nostro turno dopo le giocate. È l'uso più debole.

Mishra's Bauble lanciata (magia incolore) è un bersaglio possibile, ma vale come il punto 5.

**Replicare:** si paga una copia in più per ogni secondo bersaglio dei punti 1-4 presente nello stesso momento (per esempio due trigger di Fear of Missing Out), finché il mana lo permette. Mai copie per i trigger del punto 5.

**Mana aperto:** {U} per il punto 1, alle condizioni della sezione 5.

## 20. Guide of Souls e Ocelot Pride

**Guide of Souls:** "Whenever another creature you control enters, you gain 1 life and get {E}. Whenever you attack, you may pay {E}{E}{E}. When you do, put two +1/+1 counters and a flying counter on target attacking creature."

**Quando spendere l'energia** (D-11): quando attacchiamo con almeno tre energie e c'è un attaccante a cui il bonus fa fare danno: la creatura bersaglio, dalla migliore:

1. **Sanctifier en-Vec** che attacca: le creature nere e rosse non la bloccano comunque, quindi i due segnalini sono due danni a ogni turno;
2. **la creatura attaccante che un bloccante avversario fermerebbe**, e che con volare non può più essere bloccata (l'avversario non ha volanti stappati);
3. **la creatura attaccante con più forza**.

Si spende al primo attacco in cui c'è un bersaglio di questa lista, senza accumulare. *Perché:* i segnalini restano, quindi prima arrivano più danno fanno; l'energia non ha altri usi nella lista.

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
2. Sanctifier en-Vec
3. Teferi, Time Raveler
4. Ephemerate
5. Phelia
6. March of Otherworldly Light
7. Thraben Charm
8. Seam Rip
9. Guide of Souls
10. Ocelot Pride
11. Quantum Riddler
12. Force of Negation
13. Consign to Memory
14. Recruiter of the Guard
15. Starfield Shepherd
16. Witch Enchanter

Le prime tre terre tra campo e mano valgono più di tutto; Witch Enchanter vale come una terra finché le terre sono meno di quattro. Le terre oltre la quinta valgono meno di tutto.

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
| Rimbalzo di Ephemerate | Sempre rilanciata; bersaglio: sezione 8 |
| Copie di Consign to Memory | Ogni copia prende un bersaglio nuovo dei punti 1-4 della sezione 19, altrimenti tiene il bersaglio (dominato: non succede, perché le copie si pagano solo per bersagli nuovi) |

**Limiti noti** (per la revisione e per il confronto con la ricerca ad albero):

- non gioca attorno a Lightning Bolt e Flare of Malice in mano all'avversario;
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

## 26. Domande per Daniele

Le decisioni che un pilota di UW Blink prende e su cui non voglio inventare. Per ognuna, la regola proposta qui sopra è un punto di partenza da confermare o correggere.

| ID | Domanda | Proposta nel documento |
| --- | --- | --- |
| D-1 | **Witch Enchanter come terra.** Nel mulligan conta come terra? In partita, quando la giochi come Witch-Blessed Meadow invece di tenerla come creatura? Paghi mai le 3 vita? | Conta solo per arrivare a due terre; si gioca come terra se non c'è un'altra terra e ne abbiamo meno di quattro; 3 vita solo se il mana serve subito (sezioni 2, 3) |
| D-2 | **Mulligan.** Tieni mani da 7 con una sola terra (con Recruiter o warp)? Una mano senza risposte ma con tre creature economiche? | No e no: servono 2-4 terre, una giocata entro il secondo turno e una risposta (sezione 2) |
| D-3 | **Mana aperto.** Quando rinunci a una giocata del tuo turno per tenere {U} (Consign) o {1}{W} (Thraben) contro il trigger di FOMO? Tieni mai mana per due istantanee? | Per una sola istantanea, con le condizioni della tabella della sezione 5 |
| D-4 | **Solitude evocata.** Contro quali minacce la evochi (due carte per una), e da che turno preferisci aspettare i cinque mana? | Solo contro le prime tre voci della sezione 6, o per un attacco letale; il trucco con Ephemerate sempre quando possibile (sezione 7) |
| D-5 | **Ephemerate.** La tieni per salvare una creatura dal Bolt o la usi per valore alla fine del turno avversario? Con il rimbalzo, su cosa la rilanci se l'unica creatura deve attaccare? | Prima il trucco con Solitude e il warp, poi la protezione, poi il valore; il rimbalzo sempre, preferendo una creatura che non deve attaccare (sezione 8) |
| D-6 | **Classifica delle minacce.** FOMO con il delirium sopra la creatura con la Phoenix conferita? Moonshadow con pochi segnalini sopra Nethergoyf grande? Quanto pesa Stitcher's Supplier? | Sezione 6 |
| D-7 | **Phelia.** Attacchi con Phelia quando può essere bloccata e morire? Preferisci esiliare il loro bloccante o lampeggiare una tua creatura (Solitude, Riddler con il warp)? | Attacca se sopravvive al blocco migliore o se nessun blocco conviene; bersagli nell'ordine della sezione 9 |
| D-8 | **Warp.** Al turno 2 preferisci Quantum Riddler con il warp (una carta) o Phelia, Seam Rip, una creatura da un mana? Ricast dall'esilio a 5 mana appena puoi? | Sezione 16: il warp solo senza giocate migliori da due mana; il ricast alle condizioni del costo pieno |
| D-9 | **Thraben Charm.** Lo tieni per il trigger di FOMO o lo usi alla fine del loro turno per esiliare un cimitero con quattro tipi? Usi mai il danno? | Tenuto finché FOMO è in campo; altrimenti esilio del cimitero alla fine del turno avversario; il danno solo se non c'è altro (sezione 11) |
| D-10 | **Sanctifier en-Vec.** Lo usi per attaccare (non bloccabile dalle loro creature) o lo tieni a bloccare? | Attacca se i bloccanti rimasti bastano per la soglia di vita, altrimenti blocca (sezione 14) |
| D-11 | **Energia di Guide of Souls.** La spendi alla prima occasione o la accumuli? Su quale attaccante? | Alla prima occasione con un bersaglio utile, prima su Sanctifier (sezione 20) |
| D-12 | **Teferi.** +1 quasi sempre o −3 appena c'è un bersaglio? Quali bersagli valgono il −3 (Moonshadow da resettare, la Phoenix conferita, la tua Solitude)? Blocchi di sacrificio per proteggerlo? | Sezione 17 |
| D-13 | **Force of Negation.** Cosa contrasti tra Bolt, Flare, Thoughtseize e la Phoenix conferita? Quale carta blu esili? La paghi mai nel tuo turno? | Sezione 18: Flare e Bolt importanti prima; Thoughtseize solo nei primi due turni; si esilia Consign prima di Riddler, mai Teferi |
| D-14 | **Valore delle carte in mano.** La classifica della sezione 22 va bene? Dove mettere Phelia, Riddler, Ephemerate rispetto alle risposte? | Sezione 22 |
| D-15 | **Consign to Memory.** Lo tieni per il trigger di FOMO o lo usi sui tuoi trigger (Phelia, Solitude evocata, warp) appena puoi? Replichi mai? | Sezione 19, nell'ordine indicato |
| D-16 | **Soglia di vita.** Contro un mazzo con Lightning Bolt e creature che crescono, quanta vita paghi per fetch e Hallowed Fountain? | Sopra la forza avversaria in campo più 3, e almeno 5 (sezione 1) |
| D-17 | **Giocare attorno a Bolt e Flare.** Il primo agente deve tenerne conto (per esempio non esporre Teferi a fedeltà 3 con il rosso aperto)? | No nella prima versione, come per l'agente di Rakdos: lo misura il confronto con la ricerca ad albero |
| D-18 | **Attacchi di UW.** Quanto sei aggressivo con Ocelot Pride e Guide of Souls nei primi turni, contro creature che crescono? | Si attacca solo se nessun blocco conviene all'avversario e restano bloccanti per la soglia di vita (sezione 21) |
