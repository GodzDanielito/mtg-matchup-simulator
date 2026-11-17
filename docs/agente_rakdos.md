# Agente euristico di Rakdos Delirium (approvato e implementato, 9/11/2026)

Questo documento descrive come gioca l'agente euristico di Rakdos: regole scritte a mano, ognuna con il suo motivo. La struttura è stata approvata il 9/11/2026; le decisioni prese in revisione sono nella sezione 17. L'implementazione è in `agents/rakdos_agent.cpp`, una funzione per regola; le scelte fatte implementando, dove il documento non decideva, sono nella sezione 18. Le regole riflettono convinzioni sul matchup: le versioni successive le verificheranno con test A/B e con il confronto contro la ricerca ad albero.

I testi delle carte citati vengono dal bulk Oracle di Scryfall del 9/11/2026, lo stesso dei test.

## 1. Principi

- **Vede solo quello che vedrebbe il giocatore.** La propria mano, le zone pubbliche (campo, cimiteri, esilio, pila), i punti vita, quante carte ha in mano l'avversario e le carte che ha rivelato (Thoughtseize). Non legge mai la mano avversaria nascosta né l'ordine dei grimori, anche se lo stato del motore li contiene.
- **Un piano per turno, una lista di priorità.** Quando ha la priorità, l'agente scorre una lista fissa di giocate (sezione 4) e fa la prima che è legale e conveniente. Le altre decisioni (bersagli, scarti, blocchi...) hanno regole proprie.
- **Non gioca attorno ai trucchi.** Non tiene conto di Solitude, Phelia o Force of Negation che l'avversario potrebbe avere in mano: un giocatore esperto lo fa, l'agente euristico no. È il limite principale, e il confronto con la ricerca ad albero servirà a misurarlo.
- **Non sa CHI ha davanti, ma vede lo stato pubblico.** Nessuna regola dipende dal tipo di avversario (UW, goldfish, mirror): contro tutti l'agente applica le stesse regole. Le regole però possono e devono dipendere dallo stato pubblico del momento: creature in campo, punti vita, carte nei cimiteri. Ogni carta che in partita si giocherebbe comunque si gioca anche nel goldfish (Thoughtseize, Mishra's Bauble, le fetch, il ciclo di Street Wraith). *Perché:* altrimenti il goldfish misurerebbe un mazzo diverso da quello che gioca il matchup. Due casi mostrano la differenza, con Flare of Malice:
  - **Valore di una carta in mano** (scarti, carte in fondo, scelte tra carte): non guarda il campo avversario. Flare conserva il suo valore anche senza creature avversarie, perché possono arrivare dopo.
  - **Lancio:** guarda il campo. Flare si lancia solo se l'avversario ha almeno una creatura o un planeswalker. **Mai** sacrificare una propria creatura per un Flare che non colpisce nulla.
- **Pareggi.** Tra opzioni equivalenti per le regole sceglie a caso, con un generatore suo (come l'agente casuale): stesso seed, stessa partita.
- **Soglia di vita.** Le azioni che costano vita per scelta (fetch, shockland pagata, ciclo di Street Wraith, Thoughtseize) si fanno solo se dopo il pagamento restano **almeno 5 punti vita**, salvo che vincano la partita. *Perché:* UW non ha danni diretti, ma Ocelot Pride, Guide of Souls e Phelia attaccano, e Solitude guadagna vita mentre noi la perdiamo.
- **Contatori delle regole.** Ogni decisione registra quale regola l'ha presa, oppure "predefinito". Così si vede quali regole lavorano davvero e dove l'agente sceglie senza una regola.

## 2. Mulligan

**Con 7 carte si tiene se:**

- ci sono **da 2 a 4 terre**, oppure **una sola terra** secondo la variante C (sotto; fino al 15/11/2026 la B);
- c'è almeno **una creatura da 1 o 2 mana** (Nethergoyf, Moonshadow, Dark Matter Manipulator, Stitcher's Supplier, Fear of Missing Out);
- le terre possono lanciarla: con sole Swamp serve una creatura nera.

*Perché:* il mazzo vince mettendo in campo creature economiche nei primi turni e riempiendo il cimitero; una mano senza giocate presto perde il tempo che il mazzo ha sull'avversario.

**Con 6 carte** si tiene con 2-4 terre (o una, con la variante C). **Con 5 o meno** si tiene sempre. Le terre si contano dopo aver messo in fondo.

*Perché:* sotto le 6 carte un altro mulligan costa più di una mano mediocre.

**Mani con una sola terra: tre varianti** (decisione del 9/11/2026). Rakdos Delirium tiene mani con una sola terra grazie alle carte che cercano carte: Street Wraith si cicla gratis, Mishra's Bauble costa zero e fa pescare, Troll of Khazad-dûm cerca direttamente una Swamp. È la pratica dei giocatori veri ed è il motivo delle 17 terre. Le varianti valgono con 7 o 6 carte; con 5 o meno si tiene già sempre.

| Variante | Regola con una sola terra |
| --- | --- |
| B (predefinita fino al 15/11/2026, base_r1) | Si tiene se la mano finale ha **almeno due** carte tra Street Wraith, Mishra's Bauble e Troll of Khazad-dûm e **almeno una creatura da un mana** lanciabile con i colori di quella terra. Una fetch conta come terra che cerca la Swamp |
| **C (predefinita dal 15/11/2026, base_r2)** | Come B, ma basta **una** di quelle carte. **Ipotesi di Daniele (pilota), misurata, da confermare con la ricerca ad albero** (sezioni 21 e 22) |
| A (solo come confronto) | Una terra: sempre mulligan |

Con zero terre si fa sempre mulligan, anche con tre carte che cercano. *Perché:* il ciclo di Street Wraith pesca ma non trova una terra con certezza, e il Troll costa {1}.

I risultati del confronto sono nella sezione 19.

**Carte da mettere in fondo** (mulligan London), una alla volta:

1. le terre oltre la terza, tenendo prima le fetch, poi Blood Crypt e Blackcleave Cliffs, per ultime le Swamp;
2. poi la carta di valore più basso nella classifica della sezione 12.

**Mano con una sola terra (varianti B e C):** le carte che cercano carte non vanno in fondo, finché ne restano due o meno; la creatura da un mana non va in fondo se è l'unica. Si mette in fondo la carta di valore più basso tra le altre. *Perché:* il valore più basso della classifica è proprio il Troll, poi Street Wraith e Mishra's Bauble: le regole della mano da una terra le terrebbero fuori dalla mano che le giustifica. Nella variante A resta la regola di sempre.

*Perché:* tre terre bastano a lanciare tutto tranne il Troll, e le fetch servono anche al cimitero.

## 3. Terre, fetch e shockland

**Quale terra giocare**, nell'ordine:

1. **Una fetch**, rotta subito. *Perché:* mette una terra nel cimitero (un tipo per il delirium e per Nethergoyf), fa togliere un segnalino a Moonshadow ed è comunque mana stappato.
2. **Raucous Theater** se il mana di questo turno non serve. *Perché:* entra tappata, ma sorvegliare 1 può mandare una carta nel cimitero.
3. **Blackcleave Cliffs** entro il terzo turno. *Perché:* dopo entra tappata.
4. **Blood Crypt**, poi **Swamp**.

**Cosa cercare con la fetch** (Swamp, Blood Crypt o Raucous Theater):

- se il mana serve in questo turno e manca il rosso: **Blood Crypt**, pagando 2 vita;
- se serve solo il nero: **Swamp**; se non ce ne sono più, Blood Crypt;
- se il mana non serve in questo turno: **Raucous Theater**, altrimenti Blood Crypt tappata.

*Perché:* il rosso serve a FOMO, alla Phoenix e a Bolt, ma non sempre subito; la vita va spesa solo quando compra qualcosa.

**Blood Crypt (pagare 2 vita o entrare tappata):** si paga solo se il mana serve in questo turno per una giocata prevista, e rispettando la soglia di vita.

**Ciclo di Troll of Khazad-dûm (swampcycling {1}):** si usa se non abbiamo una terra da giocare in questo turno o nel prossimo, oppure se ne abbiamo meno di quattro tra campo e mano, e solo se nel grimorio resta una terra con il tipo Swamp (Swamp, Blood Crypt, Raucous Theater). Cerca come una fetch. *Perché:* sei mana per lanciarlo arrivano raramente, e nel cimitero il Troll aggiunge comunque il tipo creatura. **Eccezioni al primo turno** (dal 15/11/2026): senza una creatura da un mana lanciabile, Thoughtseize viene prima del ciclo se in mano resta un'altra terra per il turno 2 (sezione 6); con una creatura da un mana lanciabile e una sola terra in campo, la creatura viene prima del ciclo (X6, base_r3, sezione 24).

## 4. Ordine delle giocate nel turno

**Mantenimento:** la pescata ritardata di Mishra's Bauble avviene da sola.

**Prima fase principale**, in quest'ordine:

1. **Ciclo di Street Wraith** (2 vita). *Perché:* pescare prima di giocare la terra: la carta pescata può essere la terra del turno.
2. **Ciclo di Troll** se serve una terra (sezione 3). Al primo turno, senza una creatura da un mana lanciabile e con un'altra terra in mano, Thoughtseize (punto 5) viene prima (sezione 6). Al primo turno, con una sola terra in campo e una creatura da un mana lanciabile, la creatura (punto 6) viene prima del ciclo (X6, predefinita dal 15/11/2026, base_r3, sezioni 23 e 24); in base_r2 il ciclo veniva prima della creatura.
3. **Mishra's Bauble**: lanciarlo e sacrificarlo subito. *Perché:* costa zero, mette un artefatto nel cimitero e, sacrificato subito, non resta esposto a Witch Enchanter o Consign to Memory.
4. **Terra**, e rompere la fetch.
5. **Thoughtseize** (sezione 6).
6. **Creature che macinano:** Stitcher's Supplier, poi Dark Matter Manipulator. *Perché:* le carte macinate possono dare il delirium e le prove prima delle decisioni successive.
7. **Moonshadow**, poi **Nethergoyf**. *Perché:* Moonshadow cresce solo mentre è in campo, quindi prima entra e meglio è; Nethergoyf ha subito la forza piena.
8. **Fear of Missing Out** (sezione 9).
9. **Detective's Phoenix**: conferimento o lancio normale (sezione 10). Prima del combattimento, per la rapidità.
10. **Rimozione prima di attaccare:** Lightning Bolt o Flare of Malice su un bloccante che fermerebbe un attacco importante (sezioni 7 e 11). **Con Teferi avversario in campo**, in modalità partita lunga qui si lanciano anche i Bolt e i Flare che l'agente lancerebbe nel turno di UW (sezioni 7 e 11); in modalità corsa qui va Bolt in faccia (sezione 8, "Teferi in campo: corsa o partita lunga").

**Combattimento:** sezione 8.

**Seconda fase principale:** le creature rimaste che non cambiano il combattimento, la fuga di Nethergoyf (sezione 13), il Troll lanciato per sei mana.

*Perché questa divisione:* prima del combattimento si gioca tutto ciò che rende l'attacco più forte (delirium per FOMO, forza di Nethergoyf, rapidità della Phoenix); dopo, il resto.

**Nel turno avversario:** Lightning Bolt e Flare of Malice secondo le loro sezioni. Con Teferi, Time Raveler in campo si possono lanciare solo a velocità stregoneria, quindi nel nostro turno: si spostano nella prima fase principale, prima del combattimento (punto 10).

**Mana:** nella prima versione l'agente non tiene mana di riserva per Bolt; Bolt usa il mana avanzato. *Perché:* tenere mana aperto è una decisione da giocatore esperto che l'agente euristico non sa pesare. **Contro UW andrà rivisto:** senza {R} aperto non si può uccidere Ocelot Pride nel turno avversario prima che crei il gatto, né rispondere a Phelia quando attacca. Il trigger di Ocelot scatta all'inizio della sottofase finale: la prima priorità in quella sottofase arriva con il trigger già in pila, e il trigger si risolve anche senza la sua fonte. Per impedire il gatto, Ocelot va ucciso **prima** della sottofase finale: in combattimento o nella fase principale avversaria. Ucciderlo nella sottofase finale toglie i gatti dei turni successivi, non quello del turno in corso. **Test A/B da fare nel matchup** (audit 1, A-4): tenere {R} aperto quando UW ha in campo Ocelot Pride, Guide of Souls o Phelia, contro la regola attuale, con gli stessi seed.

## 5. Riempire il cimitero

L'obiettivo è il **delirium**: quattro tipi tra le carte nel cimitero. I tipi disponibili nel mazzo sono terra, creatura, artefatto, incantesimo, istantaneo e stregoneria:

| Tipo | Fonti, dalla più economica |
| --- | --- |
| Terra | fetch rotta, macinate, sorveglianza di Raucous Theater |
| Creatura | ciclo di Street Wraith, ciclo di Troll, Stitcher's Supplier che muore, creatura sacrificata per Flare, macinate |
| Artefatto | Mishra's Bauble |
| Incantesimo | Detective's Phoenix o Fear of Missing Out scartate, macinate o morte |
| Istantaneo | Lightning Bolt, Flare of Malice |
| Stregoneria | Thoughtseize |

**Regole:**

- **Prima le azioni gratuite** (Bauble, fetch, Wraith), poi le creature che macinano, poi le magie che servono comunque.
- **Il quarto tipo prima di attaccare con FOMO.** Se il cimitero ha tre tipi e una giocata economica porta il quarto, la si fa prima del combattimento. Esempio: Lightning Bolt in faccia per aggiungere l'istantaneo, se non c'è una creatura che lo meriti. *Perché:* il trigger di FOMO controlla il delirium quando attacca, e un combattimento aggiuntivo vale più di un Bolt tenuto in mano.
- **Le carte esiliate dal cimitero (fuga, prove) devono costare meno tipi possibile** (sezione 13).
- **Contro Sanctifier en-Vec** le carte nere e rosse vanno in esilio invece che nel cimitero; restano solo terre e Bauble. L'agente lo sa e non conta su quei tipi.

## 6. Thoughtseize

**Quando:** il prima possibile, idealmente al turno 1 o 2, nella prima fase principale. Non si lancia solo se dopo i 2 punti vita si scende sotto la soglia. Si lancia anche quando non c'è nulla da prendere.

*Perché:* nei primi turni l'avversario ha più carte e più risposte da togliere. In ogni caso Thoughtseize finisce nel cimitero come stregoneria e avvicina il delirium.

**Il turno 1:** una creatura da un mana viene prima di Thoughtseize, Thoughtseize prima di niente. *Perché:* il mazzo vince con la pressione presto; Supplier e Manipulator al turno 1 riempiono anche il cimitero.

**Correzione del turno 1** (decisione di Daniele, 15/11/2026, base_r2). La verifica della sezione 21 ha trovato due regole che, nell'ordine della sezione 4, precedono Thoughtseize e ne prendono l'unico mana. Se non c'è una creatura da un mana lanciabile:

- **Thoughtseize viene prima del ciclo di Troll of Khazad-dûm**, tranne quando dopo la terra giocata al turno 1 non resta un'altra terra in mano per il turno 2: allora si cicla il Troll, come prima. *Perché:* con la seconda terra già in mano il ciclo non serve a giocare la terra del turno 2, e Thoughtseize al turno 1 toglie una carta quando UW ne ha di più.
- **Lightning Bolt su Guide of Souls o Ocelot Pride a vista resta prima di Thoughtseize** (sezione 7). *Perché:* ogni turno in campo vale vita, energia o gatti a UW.

Opzione dell'agente `t1_seize_before_troll` (accesa per default; spenta è base_r1). Il contatore la registra come "Thoughtseize: turno 1 prima del ciclo del Troll".

**Cosa prendere dal 15/11/2026 (base_r2): la variante X5.** **Linea guida di Daniele (pilota di UW Blink), da confermare o smentire con la ricerca ad albero:** "la risposta più forte; in mancanza di risposte la minaccia più pericolosa". Le regole sono nella sezione 21 (variante X5); la riga c) resta un'**interpretazione provvisoria**. Per Flare of Malice "sarebbe il permanente di valore più alto" significa **strettamente** più alto: a pari valore sceglie l'avversario (Oracle: "Each opponent sacrifices a creature or planeswalker with the greatest mana value among creatures and planeswalkers they control"; il sacrificio è scelto da chi sacrifica). Opzione `thoughtseize_answer_or_threat` (accesa per default; spenta è base_r1, l'ordine qui sotto). Dopo Teferi, X5 usa la coda dell'ordine qui sotto (punto 9).

**Cosa prendere in base_r1**, dalla più importante (ordine deciso in revisione):

1. **Sanctifier en-Vec**: spegne il nostro cimitero e, con la protezione dal nero e dal rosso, blocca le nostre creature senza subire danno. Solo Flare of Malice lo toglie, e solo se è il loro permanente di valore di mana più alto.
2. **Solitude**: rimozione gratuita, con l'evocazione, della nostra minaccia migliore.
3. **Thraben Charm**: esilia il nostro cimitero (delirium, Nethergoyf, Phoenix dal cimitero).
4. **March of Otherworldly Light**: rimozione che esilia.
5. **Teferi, Time Raveler**: ci costringe a velocità stregoneria (niente Bolt né Flare nel loro turno) e rimbalza una creatura.
6. **Ocelot Pride**: crea gatti ogni turno in cui UW guadagna vita.
7. **Guide of Souls**: vita ed energia a ogni creatura che entra, e rende enormi gli attaccanti.
8. **Phelia, Exuberant Shepherd**: toglie i nostri bloccanti e rientra le loro creature.
9. **Il resto**, in quest'ordine proposto: Seam Rip, Quantum Riddler, Ephemerate, Force of Negation, Witch Enchanter, Starfield Shepherd, Consign to Memory, Recruiter of the Guard.

## 7. Lightning Bolt

**Bersagli**, dal più importante:

1. **Teferi**, in modalità partita lunga (sezione 8), se Bolt lo toglie dal campo: da solo (fedeltà 3 o meno) oppure insieme al danno di combattimento previsto su di lui in questo turno (fedeltà meno quel danno pari a 3 o meno). Se l'attacco da solo basta a toglierlo, Bolt resta per gli altri bersagli. *Perché:* Teferi ci toglie Bolt e Flare nel turno di UW, rimbalza una creatura e pesca ogni due turni; finché resta in campo metà della nostra interazione è ferma. **Candidato a un test A/B** (Teferi prima di Ocelot Pride, o dopo).
2. **Ocelot Pride**: crea gatti ogni turno in cui UW guadagna vita.
3. **Guide of Souls**: vita ed energia a ogni creatura che entra, e rende enormi gli attaccanti.
4. **Phelia**: toglie i nostri bloccanti e rientra le loro creature.
5. **Starfield Shepherd** e **Solitude**, 3/2: solo se attaccano, se bloccherebbero un attacco importante, o se l'avversario non ha altro.
6. **Witch Enchanter**, **Recruiter of the Guard**, i gatti.

Mai Sanctifier en-Vec (protezione dal rosso) né Quantum Riddler (4/6, Bolt non basta).

**Danno di combattimento previsto su Teferi:** la forza delle nostre creature che attaccheranno (sezione 8) e non possono essere bloccate. Con un attacco letale sul giocatore è zero: Teferi si ignora. Si conta nella prima fase principale, prima del combattimento; dopo, Bolt su Teferi solo se lo toglie da solo.

**In faccia** solo se:

- è letale in questo turno (Bolt più il danno di combattimento previsto);
- l'avversario ha **6 vita o meno** e non ci sono bersagli tra Ocelot Pride, Guide of Souls e Phelia; finché ha più di 6 vita, Bolt resta per le creature (decisione della prima versione);
- serve per il delirium prima dell'attacco di FOMO (sezione 5).

**Quando:**

- **Teferi, Ocelot Pride e Guide of Souls a vista:** appena l'agente ha la priorità e ha il mana. *Perché:* ogni turno in più in campo vale gatti, vita o energia all'avversario. Con Teferi in campo Bolt si lancia solo a velocità stregoneria: Bolt su Teferi va nella nostra prima fase principale, prima del combattimento (così l'attacco toglie il resto della fedeltà).
- **Gli altri bersagli** nella sottofase finale avversaria, dopo aver visto cosa ha fatto. Fanno eccezione quelli tolti prima di attaccare (sezione 4).
- **Con Teferi avversario in campo**, in modalità partita lunga ogni Bolt che l'agente lancerebbe nel turno di UW si sposta nella nostra prima fase principale, prima del combattimento, con le stesse regole di bersaglio (creature della lista, oppure in faccia con 6 vita o meno). In modalità corsa Bolt va in faccia, anche sopra i 6 punti vita (sezione 8).

## 8. Attacchi e blocchi

Il motore chiede gli attaccanti **una creatura alla volta**. Per ognuna l'agente considera come possibili bloccanti le creature stappate dell'avversario, senza contare quelle con lampo che potrebbe avere in mano.

**Attacca se vale almeno una di queste:**

1. **Non può essere bloccata:** volare contro nessun volatore, minaccia contro meno di due bloccanti, Troll contro meno di tre.
2. **Nessun blocco conviene all'avversario:** nessun bloccante la uccide sopravvivendo. Il conto tiene conto di attacco improvviso (Ocelot Pride) e protezione (Sanctifier en-Vec non subisce danno dalle nostre creature).
3. **Fa parte di un attacco letale:** la forza totale degli attaccanti supera i punti vita dell'avversario più il danno che i suoi bloccanti possono fermare.
4. **È Fear of Missing Out e il trigger scatterà** (primo attacco del turno e delirium). *Perché:* il combattimento aggiuntivo vale più del rischio di perdere FOMO in un blocco.

**Resta indietro** comunque una creatura se le creature avversarie possono ucciderci nel prossimo turno. In quel caso resta la creatura che blocca meglio.

**Giocatore o Teferi** (solo in modalità partita lunga, sotto). Le creature che attaccano per le regole qui sopra scelgono chi attaccare:

- **con un attacco letale sul giocatore** (regola 3, contando come bloccanti tutte le creature stappate dell'avversario) tutte vanno sul giocatore: Teferi si ignora;
- altrimenti va su Teferi **abbastanza forza da togliergli tutta la fedeltà**, partendo dalle creature che non possono essere bloccate; il resto va sul giocatore. Dentro ogni gruppo (prima le non bloccabili, poi le altre) si sceglie la creatura con meno forza che basta da sola per la fedeltà rimasta, altrimenti la più forte, e si ripete finché la fedeltà è coperta;
- se tutte insieme non bastano a toglierlo, vanno tutte sul giocatore.

*Perché:* Teferi in campo ferma Bolt e Flare nel turno di UW e rimbalza una creatura ogni due turni; toglierlo vale più di qualche punto vita, ma solo se l'attacco lo toglie davvero. Le creature non bloccabili prima, perché il danno su Teferi arriva di sicuro; la più piccola che basta, per non sprecare forza che andrebbe sul giocatore.

**Teferi in campo: corsa o partita lunga** (revisione con un giocatore esperto, 10/11/2026). Teferi va spesso ignorato quando Rakdos può chiudere la partita; va tolto solo nelle partite stabilizzate o dopo che UW ha guadagnato vita. Con Teferi avversario in campo, a ogni decisione l'agente calcola l'**orologio**:

- **Danno per turno:** la forza delle nostre creature che attaccherebbero per le regole di questa sezione (le regole 1-4 e "resta indietro"), tolte le più forti, una per ogni creatura stappata dell'avversario (lo stesso conto dell'attacco letale, regola 3). Contano anche le creature che attaccheranno solo dal prossimo turno.
- **Bolt in mano:** 3 danni ciascuno.
- **Orologio:** i turni nostri che servono a portare UW a 0, cioè la vita di UW meno i Bolt in mano, diviso il danno per turno, arrotondato per eccesso. Se i Bolt bastano da soli, l'orologio è 1; con danno per turno zero e Bolt che non bastano, l'orologio è infinito. Il guadagno di vita di UW allunga l'orologio da solo, perché si parte dalla vita attuale.

**Due modalità:**

- **Corsa** (orologio di **2 turni o meno**): **Teferi si ignora.** Tutti gli attacchi vanno sul giocatore; niente Bolt su Teferi; niente Bolt e Flare spostati nella prima fase principale per colpa di Teferi. **Lightning Bolt va in faccia**, anche sopra i 6 punti vita, nella prima fase principale prima del combattimento (punto 10 della sezione 4), dopo le giocate che usano il mana. Il Bolt in faccia tiene l'orologio entro 2 turni per definizione: l'orologio lo conta già. Il Bolt letale (avversario a 3 vita o meno) resta il primo di tutti.
- **Partita lunga** (orologio oltre 2 turni): le regole contro Teferi di questo documento, cioè l'attacco su Teferi (sopra), Bolt su Teferi (sezione 7), Bolt e Flare spostati nella prima fase principale e Flare pagato con il mana (sezioni 4 e 11).

*Perché:* finché Rakdos chiude in due turni, Teferi non ha il tempo di pesare. Ogni danno spostato su di lui allunga la partita, che è proprio ciò che serve a UW. Quando l'orologio si allunga (UW stabilizzato, vita guadagnata), Teferi diventa il problema principale: ferma metà della nostra interazione e rimbalza le creature.

**Contatore delle regole:** con Teferi avversario in campo ogni decisione registra la regola con la modalità, per esempio "attacco: non puo' essere bloccata [corsa]" o "Bolt: Teferi, lo toglie dal campo [partita lunga]". Senza Teferi i nomi delle regole non cambiano.

**Da verificare con test A/B nel matchup** (sezione 17): la soglia di 2 turni, e l'intera regola contro la variante "togli sempre Teferi" (le regole contro Teferi applicate a ogni orologio). *Provato il 15/11/2026 (sezione 21, X3):* soglia 0, 1, 3 e "ignora" restano entro ±0,07 punti dalla soglia 2.

**Combattimenti aggiuntivi:** stesse regole, sullo stato aggiornato.

**Divisione del danno** tra più bloccanti: danno letale ai bloccanti in ordine di minaccia (la classifica di Bolt), poi il resto all'ultimo.

**Blocchi.** Il motore chiede i bloccanti attaccante per attaccante. Per ogni attaccante:

1. **Blocca se il bloccante uccide l'attaccante e sopravvive.**
2. **Scambia** (muoiono entrambi) se l'attaccante vale più del bloccante (sezione 12). Stitcher's Supplier blocca volentieri: morendo macina 3.
3. **Blocca in due** un attaccante grande solo se i due insieme lo uccidono e l'attaccante può uccidere al massimo il meno prezioso.
4. **Blocco di sacrificio** (il bloccante muore senza uccidere) solo se altrimenti moriremmo in questo turno.
5. Altrimenti **non blocca.**

*Perché:* Rakdos vince la corsa con creature che crescono; perderle in blocchi alla pari lo rallenta, salvo quando la creatura che muore ci guadagna (Supplier).

**Correzione del 16/11/2026 (passo 2 della ricerca, nessuna regola cambiata).** Il "moriremmo in questo turno" del punto 4 somma la forza di tutti gli attaccanti dichiarati. Se uno di loro era un **token uscito dal combattimento e sparito** (per esempio un Gatto di Ocelot Pride ucciso da Bolt prima dei bloccanti), il suo slot non ha più definizione e l'agente si fermava con un errore ("invalid vector subscript"). Le partite degli euristici v1 non avevano mai prodotto lo stato; un rollout del giocatore forte sì. Ora quel token si salta. Correzione **minima**: gli altri attaccanti usciti dal combattimento (una creatura morta e finita nel cimitero) contano ancora come prima, perché cambiarlo cambierebbe le partite degli euristici congelati; è un difetto noto, da decidere con Daniele (diario del 16/11/2026). Tutti i registri della sezione 1 di `docs/metriche.md` restano identici. Test: `tests/test_rakdos_agent.cpp`, "blocco con un attaccante token sparito prima dei bloccanti".

## 9. Fear of Missing Out

**Lancio:** nella prima fase principale, dopo le creature da un mana, se restano {1}{R}. Meglio se in mano c'è una carta che vogliamo nel cimitero.

**Scarto all'entrata** ("discard a card, then draw a card"), nell'ordine:

1. **Detective's Phoenix**, se potremo conferirla dal cimitero (abbiamo una fonte rossa). *Perché:* dal cimitero si lancia comunque, quindi lo scarto è quasi gratis.
2. Una carta che aggiunge **un tipo mancante** al cimitero, tra quelle di valore più basso.
3. **Una terra in più**, se tra campo e mano ne abbiamo almeno quattro.
4. La carta di valore più basso (sezione 12).

**Bersaglio da stappare** quando FOMO attacca con il delirium:

- **la nostra creatura attaccante con più forza**, con priorità a quelle che non possono essere bloccate (volare, minaccia);
- con due trigger, due creature diverse se ce ne sono abbastanza, altrimenti la stessa;
- mai una creatura avversaria.

*Perché:* la creatura stappata può attaccare di nuovo nel combattimento aggiuntivo; resta stappata anche per bloccare nel turno avversario.

## 10. Detective's Phoenix

**Conferimento o lancio normale.**

- **Conferimento** ({R} più prove 6) quando c'è un bersaglio buono e le prove non costano il delirium, oppure se il danno in più è letale. Il delirium si controlla sul cimitero **dopo l'esilio delle prove e dopo l'uscita della Phoenix**: conferita dal cimitero, la Phoenix stessa lascia il cimitero e porta via due tipi (creatura e incantesimo), se non ce ne sono altre copie. *Perché:* +2/+2, volare e rapidità su una creatura già in campo; se la creatura muore o viene esiliata, la Phoenix resta come creatura 2/2 volante. Sono due minacce in una carta.
- **Lancio normale** ({2}{R}, 2/2 volante con rapidità) se non ci sono prove o bersagli e abbiamo tre mana. Lo stesso quando le prove costerebbero il delirium: la Phoenix entra come creatura invece di restare in mano.
- **Dal cimitero** si può solo conferire; se la Phoenix si può conferire sia dalla mano sia dal cimitero, prima quella del cimitero. *Perché:* quella in mano resta disponibile.
- Sempre **prima del combattimento**.

**Su quale creatura**, dalla migliore:

1. quella che fa **più danno in questo turno** con +2 di forza e volare, contando che la rapidità fa attaccare anche una creatura appena entrata;
2. a parità, quella con più forza (Nethergoyf o Moonshadow cresciuti).

Mai creature avversarie né Sanctifier en-Vec. Conferire su una creatura che rischia la rimozione va bene: la Phoenix resta comunque.

**Quali prove esiliare:** sezione 13.

## 11. Flare of Malice

"Each opponent sacrifices a creature or planeswalker with the greatest mana value among creatures and planeswalkers they control." Il bersaglio non c'è: sceglie l'avversario, ma solo tra i permanenti di valore di mana più alto.

**Quando:**

- l'avversario ha **una sola** creatura o **un solo** planeswalker; oppure
- tutti i suoi permanenti di valore più alto sono minacce vere: Quantum Riddler, Teferi, Solitude, Starfield Shepherd, oppure Sanctifier en-Vec quando è lui il più alto (la protezione non ferma un sacrificio). **Teferi da solo come permanente di valore più alto è sempre un motivo valido** in modalità partita lunga (sezione 8), anche pagando con il mana nel nostro turno;
- **mai** se l'avversario non ha creature né planeswalker, né con il mana né con il sacrificio: una propria creatura non si sacrifica per un Flare che non colpisce nulla. La carta resta in mano e conserva il suo valore (sezione 12).

**Momento:** nella sottofase finale avversaria, dopo aver visto cosa ha fatto. Fanno eccezione due casi, che si giocano nella nostra prima fase principale:

- serve togliere il solo bloccante prima di un attacco importante;
- c'è Teferi in campo, in modalità partita lunga (sezione 8): il Flare che l'agente lancerebbe nel turno di UW si sposta nella prima fase principale, prima del combattimento, con le stesse condizioni di valore. In modalità corsa questa eccezione non vale.

**Costo alternativo o mana:**

- **Mana** ({2}{B}{B}) se è il turno avversario e abbiamo quattro mana inutilizzati, con due neri. *Perché:* il mana non speso nel turno avversario andrebbe perso comunque. **Con Teferi in campo** il mana si usa anche nella nostra prima fase principale: è il Flare spostato dal turno di UW, che sarebbe stato pagato con il mana. A questo punto della fase le creature sono già state lanciate (sezione 4), quindi il mana è quello avanzato.
- Altrimenti **sacrificio di una creatura nera non token**, dalla meno preziosa:
  1. **Stitcher's Supplier**: morendo macina 3;
  2. **Dark Matter Manipulator**, se non ha il bonus;
  3. **Moonshadow** con quattro o più segnalini;
  4. **Nethergoyf** con forza 2 o meno.

  Mai le altre creature: meglio aspettare il mana. *Perché:* il sacrificio costa una carta in campo, quindi conviene solo con creature che valgono poco o che guadagnano dal morire.

**Se è Rakdos a dover sacrificare** (nel mirror, o con un effetto simile): il permanente di valore più basso tra quelli possibili, preferendo Stitcher's Supplier.

## 12. Valore delle carte

Serve per gli scambi in combattimento, i sacrifici, gli scarti e le carte da mettere in fondo.

**In mano**, dalla più preziosa:

1. Fear of Missing Out
2. Nethergoyf
3. Thoughtseize, nei primi tre turni (dopo scende in fondo alla lista)
4. Moonshadow, nei primi tre turni (dopo scende dopo Flare)
5. Dark Matter Manipulator
6. Detective's Phoenix
7. Flare of Malice
8. Lightning Bolt
9. Stitcher's Supplier
10. Mishra's Bauble
11. Street Wraith
12. Troll of Khazad-dûm

Le prime tre terre tra campo e mano valgono più di tutto; le terre oltre la quarta meno di tutto. La classifica non guarda il campo avversario: Flare of Malice non perde valore solo perché in quel momento l'avversario non ha creature (principio "non sa chi ha davanti", sezione 1).

**In campo**, dalla meno preziosa: Stitcher's Supplier, Dark Matter Manipulator, Moonshadow con quattro o più segnalini, Nethergoyf con forza 2 o meno, Fear of Missing Out, Detective's Phoenix, poi le altre in ordine di forza. Una creatura con la Phoenix conferita vale un po' meno di quanto sembra: se muore, la Phoenix resta.

## 13. Fuga di Nethergoyf ed esilio dal cimitero

"Escape—{2}{B}, Exile any number of other cards from your graveyard with four or more card types among them."

**Quando usare la fuga:** nella seconda fase principale, con tre mana senza un uso migliore, e solo se dopo l'esilio migliore restano almeno **3 tipi** nel cimitero (Nethergoyf almeno 3/4). Il conto si fa sul cimitero **dopo l'esilio e dopo l'uscita di Nethergoyf**, che lasciando il cimitero porta via la sua creatura se non ce ne sono altre. Fa eccezione il caso in cui serve un bloccante per non morire o un attaccante per un danno letale. Se FOMO è in campo, la fuga non deve costare il delirium.

*Perché:* la fuga esilia carte con almeno quattro tipi, quindi può rimpicciolire proprio il Nethergoyf che rientra e togliere il delirium a FOMO.

**Quali carte esiliare** (vale anche per le prove della Phoenix). Il motore propone solo insiemi minimali, una carta alla volta. L'agente li confronta tutti, perché sono pochi (al massimo 10 opzioni per passo nelle partite casuali), e sceglie in quest'ordine:

1. quello che lascia **più tipi** nel cimitero (il delirium prima di tutto);
2. quello che **non esilia Detective's Phoenix** né un altro Nethergoyf: si possono lanciare dal cimitero;
3. quello che lascia **più valore di mana** nel cimitero, per le prove future;
4. quello che lascia **più carte**, se abbiamo Dark Matter Manipulator in campo (+2/+0 ogni sette).

*Perché:* il cimitero è la risorsa del mazzo; si spendono prima le copie dei tipi già presenti.

## 14. Decisioni minori e comportamenti predefiniti

| Decisione | Cosa fa l'agente |
| --- | --- |
| Sorvegliare 1 (Raucous Theater) | Nel cimitero se è Detective's Phoenix, una terra in più (almeno quattro tra campo e mano) o una carta di poco valore che aggiunge un tipo mancante; altrimenti resta in cima |
| Scarto a fine turno | Come lo scarto di FOMO (sezione 9) |
| Ordine dei trigger | Prima si risolvono le macinate, poi il resto. *Perché:* le carte macinate cambiano delirium e forze prima degli altri effetti |
| Modo di pagare il mana | Si tengono stappate le terre che producono i colori ancora necessari in mano (il rosso per Bolt, due neri per Flare); si paga prima con le terre di un solo colore |
| Ricerca senza terre da trovare | Non cerca nulla (succede solo se il grimorio non ha più terre del tipo cercato) |
| Bersaglio di Mishra's Bauble | Se stesso: l'agente non usa l'informazione sulla cima del grimorio, quindi la scelta non conta |
| Lancio del Troll per sei mana | Solo nella seconda fase principale, con sei terre e niente di meglio |
| Regola delle leggende | Non serve: Rakdos non ha leggende |
| Qualsiasi decisione senza regola | **Passare** se è tra le opzioni, altrimenti la prima opzione in ordine canonico. La scelta viene contata come "predefinito" |

**Limiti noti** (per la revisione e per il confronto con la ricerca ad albero):

- non gioca attorno a lampo, contromagie e rimozioni avversarie;
- non tiene mana aperto per Bolt; contro UW andrà rivisto (Ocelot Pride da uccidere nel combattimento o nella fase principale avversaria, prima che il suo trigger di fine turno vada in pila; risposta a Phelia);
- non usa l'informazione di Mishra's Bauble;
- pianifica un turno alla volta.

## 15. Ogni decisione del motore

| Decisione del motore | Sezione |
| --- | --- |
| Mulligan, carte in fondo | 2 |
| Priorità (terra, magie, abilità, passare) | 3, 4, 6, 7, 9, 10, 11, 13 |
| Vita o terra tappata (Blood Crypt) | 3 |
| Ricerca della fetch, ciclo del Troll | 3 |
| Sorveglianza | 14 |
| Scarto a fine turno, scarto di FOMO | 9, 14 |
| Bersaglio di una magia o di un'abilità | 7, 9, 10, 14 |
| Pagamento del mana | 14 |
| Carta dalla mano (Thoughtseize) | 6 |
| Attaccanti, bloccanti, divisione del danno | 8 |
| Ordine dei trigger | 14 |
| Esilio per la fuga e per le prove | 13 |
| Sacrificio come costo e per effetto (Flare) | 11 |
| Regola delle leggende | 14 |

## 16. Goldfish

Il goldfish misura quanto è veloce Rakdos senza interazione: in che turno arriva il delirium e in che turno uccide. È la validazione prevista dalla specifica: se il simulatore vince molto prima o molto dopo delle partite reali, c'è un problema nelle regole o nell'agente.

**L'avversario.** Un "mazzo goldfish" di **60 Plains**, legale perché le terre base non hanno limite di copie, con un agente che passa sempre: tiene sette carte, non gioca terre e scarta quando deve.

- *Perché le Plains:* con 60 carte e una pescata a turno, il grimorio dura oltre 50 turni, quindi non perde mai per grimorio vuoto prima della fine.
- *Alternativa scartata:* la lista UW che non fa nulla. Thoughtseize vedrebbe carte vere e il risultato dipenderebbe dalla lista avversaria, mentre il goldfish deve misurare solo Rakdos.

**L'agente gioca come in partita.** È lo stesso agente del matchup, senza regole speciali per il goldfish (sezione 1).

- **Thoughtseize si lancia** anche se contro 60 Plains non prende nulla. In partita finisce nel cimitero come stregoneria e contribuisce al delirium: senza lanciarla, il delirium del goldfish arriverebbe più tardi del reale.
- **Lo stesso vale per ogni carta che in partita si gioca comunque:** Mishra's Bauble, le fetch, il ciclo di Street Wraith e del Troll.
- **Distorsione residua: Lightning Bolt e Flare of Malice.** In partita si lanciano soprattutto sulle creature avversarie, che nel goldfish non ci sono. Bolt va in faccia solo sotto i 6 punti vita o per il delirium, e Flare non si lancia mai. L'istantaneo arriva quindi nel cimitero più tardi che in partita, e il turno del delirium del goldfish tende a essere più tardo del reale. Il confronto con le partite contro UW dirà di quanto.
- **Effetto della regola "Bolt sulle creature finché l'avversario ha più di 6 vita".** Nel goldfish non ci sono creature, quindi Bolt resta in mano finché l'avversario scende a 6 punti vita o meno. Questo ritarda sia il delirium (Bolt non arriva nel cimitero) sia la vittoria (il suo danno arriva tardi) rispetto a una partita reale. **Va tenuto presente nel leggere le soglie d'allarme:** un delirium o una vittoria un po' più tardi del previsto possono dipendere da questa regola e non da un errore. Per separare i due effetti si può rieseguire il goldfish con una variante che lancia Bolt in faccia appena possibile, con gli stessi seed (come variante di confronto, non come comportamento dell'agente).

**Le partite.**

- **100.000 partite**, seed da 1 a 100.000, e chi inizia dipende dalla parità del seed: esattamente **50.000 in play e 50.000 in draw**, sempre riportati separati. Con 50.000 partite per lato, una percentuale ha un intervallo di confidenza al 95% di circa ±0,4 punti e il turno medio di vittoria di pochi millesimi di turno.
- **Limite di 15 turni di Rakdos:** oltre, la partita è "non vinta entro il limite" e si conta a parte.
- **Stessi seed per ogni versione dell'agente:** due varianti di una regola (per esempio due mulligan) si confrontano partita per partita, con gli stessi grimori.

**Cosa misurare**, sempre in play e in draw separati:

| Misura | Dettaglio |
| --- | --- |
| Turno del delirium | Primo turno di Rakdos in cui il cimitero raggiunge quattro tipi: distribuzione, mediana, percentuale cumulativa entro il turno 2, 3, 4, 5 |
| Turno della vittoria | Turno di Rakdos in cui l'avversario arriva a 0: distribuzione, media, mediana, percentuale entro il turno 4, 5, 6, 7 |
| Mulligan | Frequenza di 7, 6, 5 carte e turno della vittoria per ogni dimensione della mano |
| Carte usate | Per ogni carta: quante volte pescata, lanciata o attivata, con il modo (conferimento dalla mano o dal cimitero, fuga, costo alternativo, ciclo) |
| Danno per carta | Quanto danno fa ogni carta (combattimento e Bolt) e in che percentuale sul totale |
| Vita spesa | Vita pagata da Rakdos (fetch, shockland, Street Wraith, Thoughtseize) al momento della vittoria |
| Tipi nel cimitero | Media dei tipi alla fine di ogni turno di Rakdos |
| Terre | Turni senza terra da giocare; turni con terre in eccesso |
| FOMO e Phoenix | Combattimenti aggiuntivi per partita, conferimenti per partita |
| Qualità dell'agente | Decisioni prese da ogni regola e da "predefinito" |

**Soglie d'allarme.** Sono **stime da esperienza di gioco, non dati misurati**:

- **turno della vittoria:** allarme se la mediana è **sotto il 4 o sopra il 7**;
- **turno del delirium:** atteso **tra il 2 e il 4**.

Fuori da questi intervalli, prima di fidarsi del simulatore si cercano errori nelle regole o nell'agente. Le soglie andranno sostituite da dati reali quando ce ne saranno.

**Definizioni dei turni.** I turni sono quelli di Rakdos (1 = il suo primo turno).

- **Turno del delirium:** il turno di Rakdos da cui il delirium si può usare. Se il quarto tipo arriva nel turno avversario (per esempio Lightning Bolt nella sua sottofase finale), conta il turno successivo di Rakdos. Si legge dal registro, spostamento per spostamento, non dallo stato tra una decisione e l'altra: una decisione può far avanzare la partita fino al turno dopo.
- **Turno della vittoria:** il turno di Rakdos in corso, o appena finito, quando l'avversario arriva a 0.
- **"Mai" nel delirium** comprende anche le partite vinte prima di raggiungerlo.

**Uscite.** L'agente sta in `agents/`, l'eseguibile in `tools/` (`goldfish [--games N] [--first-seed S] [--limit T] [--mulligan A|B|C] [--bolt-face] [--replays K] [--out DIR]`).

- Una riga JSON per partita in `data/goldfish/partite_B.jsonl` (il suffisso è la variante del mulligan: `_A`, `_B`, `_C`, poi `_bolt_faccia` se c'è `--bolt-face`), esclusa da Git. Ogni riga ha anche le scelte di mulligan (`mulligan_scelte`, per esempio `MK`) e la mano tenuta.
- Il riepilogo stampato a fine esecuzione e salvato in `data/goldfish/riepilogo_B.txt`.
- Il registro completo, evento per evento, per le prime 100 partite, da leggere come un replay (`data/goldfish/replay_B.jsonl`).
- L'analisi con pandas può arrivare dopo.

**Test del goldfish.**

- L'avversario non lancia, non gioca terre e non attacca mai.
- Il limite di turni viene rispettato.
- Stesso seed, stesso risultato.
- Il turno del delirium registrato coincide con il primo turno in cui il ricalcolo del cimitero dà quattro tipi.

## 17. Decisioni prese in revisione (9/11/2026)

| Tema | Decisione |
| --- | --- |
| Thoughtseize contro UW Blink | Sanctifier en-Vec, Solitude, Thraben Charm, March of Otherworldly Light, Teferi, Ocelot Pride, Guide of Souls, Phelia, poi il resto (sezione 6) |
| Lightning Bolt | Sulle creature finché l'avversario ha più di 6 vita, nella prima versione; Ocelot Pride e Guide of Souls a vista (sezione 7) |
| Mani da 7 con una sola terra | *Superata il 9/11/2026:* il test A/B è stato fatto e la variante B è la predefinita (sezioni 2 e 19) |
| Mulligan con una sola terra | Variante B predefinita: si tiene con 7 o 6 carte se ci sono almeno due carte tra Street Wraith, Mishra's Bauble e Troll e una creatura da un mana lanciabile con quella terra (una fetch conta come terra che cerca la Swamp). C: basta una carta. A: sempre mulligan, solo come confronto |
| Mana aperto per Bolt | Niente nella prima versione; contro UW andrà rivisto: Ocelot Pride va ucciso nel combattimento o nella fase principale avversaria (nella sottofase finale il trigger del gatto è già in pila e si risolve anche senza Ocelot), risposta a Phelia (sezione 4). Correzione in revisione: la prima versione indicava la sottofase finale. *10/11/2026:* resta la regola, da decidere con un test A/B nel matchup (audit 1, A-4) |
| Agente contro Teferi (audit 1, 10/11/2026) | Attacco: senza un attacco letale sul giocatore, su Teferi abbastanza forza da toglierlo, prima le creature non bloccabili, il resto sul giocatore (sezione 8). Bolt: Teferi primo bersaglio se Bolt lo toglie, da solo o dopo il danno di combattimento previsto (sezione 7). Con Teferi in campo, Bolt e Flare del turno di UW si spostano nella nostra prima fase principale, prima del combattimento (sezioni 4, 7, 11). Flare: Teferi come permanente di valore più alto è un motivo valido, anche pagando con il mana nel nostro turno (sezione 11). *Rivista il 10/11/2026:* queste regole valgono solo in modalità partita lunga (riga sotto) |
| Teferi: corsa o partita lunga (revisione con un giocatore esperto, 10/11/2026) | Orologio: turni per portare UW a 0 con il danno di combattimento del piano d'attacco più i Bolt in mano. Orologio di 2 turni o meno: corsa, Teferi si ignora, attacchi sul giocatore e Bolt in faccia anche sopra i 6 punti vita. Oltre: partita lunga, le regole contro Teferi. Il contatore delle regole distingue le due modalità (sezione 8) |
| Carta lanciata dal cimitero | Conferendo la Phoenix dal cimitero esce anche la Phoenix (creatura e incantesimo); con la fuga esce anche Nethergoyf (creatura). I controlli "le prove non costano il delirium" e "restano almeno 3 tipi" si fanno sul cimitero dopo l'esilio **e** dopo l'uscita della carta lanciata (sezioni 10 e 13) |
| Candidati ai test A/B futuri | Turno 1: creatura da un mana contro Thoughtseize (sezione 6). Nel matchup: tenere una mano da 7 con 2-4 terre, Thoughtseize e Lightning Bolt ma senza creature da 1-2 mana (sezione 20); Bolt su Teferi prima di Ocelot Pride, contro dopo (sezione 7); mana aperto per Bolt (sezione 4, audit 1 A-4); **Teferi: orologio a 2 turni contro rimozione sempre** (sezione 8), e la soglia di 2 turni. *(Le mani da 7 con una sola terra sono state provate: sezione 19)* *15/11/2026:* provati nel matchup il turno 1 (X4), la mano da 7 senza creature economiche (X2), Teferi (X3) e di nuovo il mulligan con una terra (X1): sezione 21. Restano Bolt su Teferi prima o dopo Ocelot Pride e il mana aperto per Bolt. *Seguito del 15/11/2026:* X2, X3 e X4 non adottate; robustezza di X1 contro tre versioni di UW, verifica della regola del turno 1 e variante X5 (cosa prende Thoughtseize) nella sezione 21. *Variante X6 (15/11/2026):* al turno 1 la creatura da un mana invece del ciclo del Troll, sezione 23; predefinita da base_r3, sezione 24 |
| Soglie del goldfish | Mediana del turno della vittoria sotto il 4 o sopra il 7: allarme. Turno del delirium atteso tra il 2 e il 4. Stime, non dati (sezione 16) |
| Stato pubblico e avversario | L'agente non sa chi ha davanti, ma le regole dipendono dallo stato pubblico. Valore in mano: Flare conserva valore senza creature avversarie. Lancio: Flare solo se l'avversario ha una creatura o un planeswalker, mai sacrificando una propria creatura a vuoto (sezioni 1 e 11) |
| Bolt nel goldfish | Con "Bolt sulle creature finché l'avversario ha più di 6 vita", nel goldfish Bolt resta in mano fino a 6 punti vita: ritarda delirium e vittoria rispetto al reale, da considerare nelle soglie (sezione 16) |
| Cartelle | `agents/` per gli agenti euristici, `tools/` per l'eseguibile del goldfish |
| Goldfish e carte giocate comunque | L'agente lancia Thoughtseize come in partita, anche se contro 60 Plains non prende nulla: in partita finisce nel cimitero come stregoneria e contribuisce al delirium. Vale per ogni carta che in partita si giocherebbe comunque (sezioni 1 e 16) |
| base_r2 (decisione di Daniele, 15/11/2026) | Diventano predefiniti X1-C (mulligan con una terra e una sola carta che cerca: ipotesi di Daniele, misurata, da confermare con la ricerca ad albero), X5 (cosa prende Thoughtseize: linea guida di Daniele, da confermare o smentire con la ricerca ad albero; riga c provvisoria; Flare "rimuove" solo un valore strettamente più alto) e la correzione del turno 1 (Thoughtseize prima del ciclo di Troll se resta un'altra terra in mano; Bolt a vista su Guide o Ocelot prima di Thoughtseize). L'agente di prima è base_r1; le opzioni restano disattivabili (sezione 22) |
| base_r3 (decisione di Daniele, 15/11/2026) | Diventa predefinita X6: al primo turno, con una sola terra in campo, Troll e una creatura da un mana lanciabile, la creatura prima del ciclo. Misurata, coerente con la linea del pilota sul turno 1, da confermare con la ricerca ad albero. base_r2 è base_r3 con X6 spenta (sezione 24) |

**Conseguenze applicate al documento:**

- tolta la regola "Troll conta come terra", che avrebbe tenuto alcune mani da 7 con una sola terra;
- tolta l'eccezione "Thoughtseize non si lancia se la mano avversaria nota non ha carte non terra";
- tolto il calo di valore di Flare of Malice contro un avversario senza creature: nel goldfish l'avrebbe fatta scartare sempre, anticipando il delirium rispetto al reale. Resta però la regola di lancio: Flare non si lancia se non colpisce nulla;
- tolto l'aggiustamento che anteponeva le risposte quando abbiamo una minaccia grande: contraddiceva l'ordine deciso per Thoughtseize.

## 18. Scelte fatte implementando (9/11/2026)

Dove il documento non decideva, l'implementazione ha scelto così. Ognuna è una regola con il suo contatore, quindi si vede quanto lavora.

| Tema | Scelta |
| --- | --- |
| Mulligan a 6 carte | Le terre si contano dopo aver messo in fondo, con la regola della sezione 2 (prima le terre oltre la terza): sette carte con cinque terre diventano sei con quattro e si tengono |
| Creatura lanciabile nella mano da 7 | I colori delle terre in mano, e per le fetch quelli delle terre che possono cercare nel grimorio |
| Valore della quarta terra | Tra Lightning Bolt e Detective's Phoenix: il documento fissa solo le prime tre (sopra tutto) e quelle oltre la quarta (sotto tutto) |
| "Carta di poco valore" | Nella sorveglianza: Stitcher's Supplier e sotto. Nello scarto per il tipo mancante: Lightning Bolt e sotto |
| "Il mana serve in questo turno" | Si contano le giocate previste della sezione 4 pagabili con e senza la terra in più (fetch, shockland, Raucous Theater): serve se con la terra se ne pagano di più |
| Attacco importante (rimozione del solo bloccante) | Forza totale delle nostre creature pronte ad attaccare di almeno 4 |
| Bolt in faccia con 6 vita o meno | Nella nostra seconda fase principale o nella sottofase finale avversaria. "Letale" è applicato come avversario a 3 vita o meno, in qualsiasi momento: il danno di combattimento previsto si vede dopo il combattimento |
| Fuga per un attacco letale | Non implementata: Nethergoyf non ha rapidità, quindi non attacca nel turno in cui rientra. Resta l'eccezione del bloccante |
| Teferi tra i bersagli di Bolt | *Superata il 10/11/2026:* Teferi è il primo bersaglio quando Bolt lo toglie dal campo (sezioni 7 e 17) |
| Teferi: attacco e Bolt insieme (10/11/2026) | Bolt su Teferi si decide quando l'agente ha la priorità nella prima fase principale, con le creature in campo in quel momento: chi entra dopo (la Phoenix con rapidità) non conta nel danno previsto. Tra creature identiche, quelle mandate su Teferi sono le ultime nell'ordine del motore, che vieta scelte decrescenti dentro una classe (nessuno, giocatore, planeswalker). Il Flare spostato nella prima fase principale preferisce il mana al sacrificio, come nel turno di UW |
| Orologio contro Teferi (10/11/2026) | Le creature del danno per turno sono tutte le nostre creature per cui le regole d'attacco direbbero di attaccare, anche se tappate o appena entrate: l'orologio guarda ai prossimi turni. Il conto non aggiunge i combattimenti in più di Fear of Missing Out. La soglia è un'opzione dell'agente (2 turni); la variante "togli sempre Teferi" è la soglia 0, cioè mai in corsa. Il Bolt in faccia della corsa va nel punto 10 della sezione 4, dopo le creature, così non toglie il mana alle giocate che lo precedono |
| Blocchi in due | Si cerca una coppia che uccide l'attaccante senza perdere la creatura più preziosa; il secondo bloccante si ricorda tra una decisione e l'altra |
| Pareggi | Con il generatore dell'agente, separato da quello della partita |
| Carte in fondo con una sola terra | Nelle varianti B e C le carte che cercano carte (finché sono due o meno) e la creatura da un mana (se è l'unica) restano in mano; senza questa regola il valore più basso della classifica metterebbe in fondo proprio il Troll e Street Wraith |
| "Partite in cui la decisione di mulligan cambia" | Quelle in cui le scelte di mulligan (`M` mulligan, `K` tiene) di due varianti differiscono. Con gli stessi seed si può dire esattamente dove: B tiene dove A fa mulligan, mai il contrario |

## 19. Confronto delle varianti del mulligan con una terra (9/11/2026)

Goldfish da 100.000 partite per variante, seed da 1 a 100.000, play (50.000) e draw (50.000) separati, **stessi seed**: le tre varianti vedono gli stessi grimori e gli stessi mazzi, e la partita è identica finché le scelte di mulligan coincidono. Controllo: la variante A riproduce partita per partita il goldfish precedente alla modifica (100.000 partite identiche su 19 campi). Gli intervalli di confidenza (95%) delle differenze sono **appaiati** per seed.

**1. Mani tenute**

| | Play 7 | Play 6 | Play 5 | Draw 7 | Draw 6 | Draw 5 |
| --- | --- | --- | --- | --- | --- | --- |
| A | 58,62% | 26,66% | 14,72% | 58,68% | 26,75% | 14,58% |
| B | 67,45% | 23,86% | 8,69% | 67,51% | 24,04% | 8,46% |
| C | 78,02% | 18,42% | 3,56% | 78,08% | 18,54% | 3,38% |

**2. Turno della vittoria**

| | Play: media (±) | mediana | entro T4 | entro T5 | Draw: media (±) | mediana | entro T4 | entro T5 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 4,942 (±0,008) | 5 | 34,25% | 80,32% | 4,709 (±0,007) | 5 | 44,97% | 88,23% |
| B | 4,866 (±0,008) | 5 | 36,23% | 82,96% | 4,659 (±0,007) | 5 | 46,70% | 90,03% |
| C | 4,856 (±0,007) | 5 | 35,90% | 83,33% | 4,660 (±0,006) | 5 | 46,31% | 89,93% |

Differenze appaiate (IC 95%):

| | Play: turni | entro T4 (punti %) | entro T5 (punti %) | Draw: turni | entro T4 (punti %) | entro T5 (punti %) |
| --- | --- | --- | --- | --- | --- | --- |
| **B − A** | −0,0754 [−0,0800; −0,0707] | +1,98 [+1,78; +2,18] | +2,64 [+2,44; +2,84] | −0,0502 [−0,0544; −0,0461] | +1,73 [+1,51; +1,95] | +1,80 [+1,62; +1,98] |
| **C − B** | −0,0101 [−0,0144; −0,0057] | −0,33 [−0,53; −0,14] | +0,36 [+0,16; +0,56] | +0,0007 [−0,0031; +0,0046] | −0,40 [−0,61; −0,18] | −0,10 [−0,27; +0,07] |

**3. Solo le partite in cui la decisione di mulligan cambia**

| | Partite | Variante | Vittoria media (±) | entro T4 | entro T5 | Mani tenute |
| --- | --- | --- | --- | --- | --- | --- |
| Play, A → B | 5.816 (11,63%) | A | 5,336 (±0,030) | 21,1% | 65,6% | 6 carte 48,2%, 5 carte 51,8% |
| | | B | 4,835 (±0,020) | 35,5% | 83,7% | 7 carte 75,9%, 6 carte 24,1% |
| Play, B → C | 6.421 (12,84%) | B | 5,181 (±0,026) | 26,0% | 70,6% | 6 carte 60,1%, 5 carte 39,9% |
| | | C | 5,102 (±0,021) | 23,4% | 73,5% | 7 carte 82,3%, 6 carte 17,7% |
| Draw, A → B | 5.884 (11,77%) | A | 5,020 (±0,026) | 32,0% | 77,3% | 6 carte 48,0%, 5 carte 52,0% |
| | | B | 4,691 (±0,019) | 43,8% | 89,3% | 7 carte 75,0%, 6 carte 25,0% |
| Draw, B → C | 6.441 (12,88%) | B | 4,875 (±0,022) | 37,2% | 82,4% | 6 carte 60,6%, 5 carte 39,4% |
| | | C | 4,882 (±0,020) | 34,2% | 81,6% | 7 carte 82,1%, 6 carte 17,9% |

Differenza sulle sole partite cambiate: B − A −0,501 turni in play [−0,537; −0,465] e −0,329 in draw [−0,362; −0,297]; C − B −0,078 in play [−0,112; −0,045] e +0,006 in draw [−0,024; +0,036].

**4. Non vinte entro il limite di 15 turni:** A 0 in play e 2 in draw; B 0 e 2; C 0 e 1 (su 50.000 ciascuna).

**Lettura**

- **B batte A** in entrambi i lati: la vittoria arriva 0,05-0,08 turni prima in media, ed entro il turno 5 si vince il 2-2,6 punti percentuali più spesso. Nelle sole partite cambiate il guadagno è di mezzo turno in play e di un terzo in draw: tenere la mano con una terra e due carte che cercano vale più di un mulligan a 6 o 5 carte.
- **C non batte B in modo chiaro.** In play C vince un po' più spesso entro T5 (+0,36 punti) ma meno entro T4 (−0,33); in draw la media è identica e C è peggiore entro T4. Nelle partite cambiate, la mano con una sola carta che cerca è più veloce di un mulligan solo di poco in play e per niente in draw. C resta una variante, B resta la predefinita.
- **Il goldfish misura solo la velocità.** Contro un avversario che interagisce, una mano con una terra sola ha più rischi (Thoughtseize, Solitude, un turno senza terra) che qui non si vedono. Il confronto B contro C e contro A andrà ripetuto contro UW Blink, e poi contro la ricerca ad albero.
- **Quanto pesa la regola.** B cambia la decisione in circa una partita su nove (11,6-11,8%), C in un'altra su otto (12,8-12,9%). Le mani da 7 carte passano dal 58,6% (A) al 67,5% (B) e al 78,0% (C).

## 20. Mani rifiutate per motivo, variante B (9/11/2026)

Goldfish da 100.000 partite con la variante B, seed da 1 a 100.000 (gli stessi della sezione 19). Ogni mano rifiutata prende il motivo dalla regola che l'ha rifiutata (sezione 2); le terre nella mano pescata separano i casi di "terre fuori da 2-4". Il goldfish lo scrive nella riga di ogni partita (`mulligan_motivi`). Le terre si contano dopo le carte in fondo, che tolgono terre solo dalle mani con più di tre: zero o una terra restano tali.

| | Play: mani da 7 | Play: mani da 6 | Draw: mani da 7 | Draw: mani da 6 |
| --- | --- | --- | --- | --- |
| Mani viste | 50.000 | 16.274 | 50.000 | 16.247 |
| Rifiutate | 16.274 (32,5%) | 4.345 (26,7%) | 16.247 (32,5%) | 4.228 (26,0%) |
| Zero terre | 4.211 (25,9%) | 1.359 (31,3%) | 4.154 (25,6%) | 1.354 (32,0%) |
| Una terra senza abbastanza carte che cercano | 8.963 (55,1%) | 2.955 (68,0%) | 8.963 (55,2%) | 2.850 (67,4%) |
| Cinque o più terre | 791 (4,9%) | 31 (0,7%) | 773 (4,8%) | 24 (0,6%) |
| Nessuna creatura da 1-2 mana | 2.309 (14,2%) | 0 | 2.357 (14,5%) | 0 |
| Altro | 0 | 0 | 0 | 0 |

Le percentuali tra parentesi sono sulle mani rifiutate di quella colonna; la riga "Rifiutate" è sulle mani viste. Con 6 carte la regola della creatura economica non si applica (sezione 2), per questo la riga è vuota. "Altro" è vuoto per costruzione: ogni ramo della regola ha il suo motivo.

**Dato per un futuro test A/B nel matchup.** Il 4,6-4,7% delle mani da 7 (circa una su venti) viene rifiutato solo perché non ha una creatura da 1-2 mana lanciabile: ha 2-4 terre. Nel goldfish la regola è giusta, perché senza creature non si vince in fretta. Contro UW Blink potrebbe non esserlo: una mano con due terre, Thoughtseize e Lightning Bolt ma senza creature economiche toglie la minaccia o la risposta migliore di UW e uccide Ocelot Pride o Guide of Souls prima che crescano. Da provare quando l'agente di UW gioca davvero: variante che tiene queste mani se hanno almeno una tra Thoughtseize e Lightning Bolt (o entrambe), contro la regola attuale, con gli stessi seed e solo sulle partite in cui la decisione cambia.

## 21. Test A/B nel matchup (15/11/2026)

Prima di misurare il matchup, le regole di Rakdos ricevono la stessa cura di quelle di UW (`docs/agente_uw.md`, sezione 28): varianti messe alla prova contro l'agente di UW base_v2, con gli stessi seed. **Ogni variante è un'opzione dell'agente (`RakdosAgentOptions`) spenta per default:** con tutte le opzioni al valore predefinito il registro del matchup resta identico byte per byte (`matchup.exe --replays 20`: `partite.jsonl` `81c39eba3ce8e4900754136b82d84ff2`, `replay.jsonl` `2b4dded814c94073724c280797dde696`), e così il goldfish (`goldfish.exe --games 10000 --replays 20`: `partite_B.jsonl` `29a9db59727e85dfb89f295e206cbe03`, `replay_B.jsonl` `43d5374437046d328482d93ccfaec22c`). **Nessuna variante è adottata:** si decide insieme.

**Le varianti**

| Variante | Opzione | Regola |
| --- | --- | --- |
| Base | tutto predefinito | Mulligan B, orologio contro Teferi con soglia 2, creatura da un mana prima di Thoughtseize al turno 1 |
| X1-A | `mulligan = A` | Una terra: sempre mulligan (sezione 2) |
| X1-C | `mulligan = C` | Una terra: basta una carta che cerca (sezione 2) |
| X2 | `mulligan_keep_disruption` | Con 7 carte, se la mano ha 2-4 terre ma nessuna creatura da 1-2 mana lanciabile, si tiene se ha almeno due carte tra Thoughtseize, Lightning Bolt e Flare of Malice (anche due copie della stessa). Con 6 carte non cambia nulla: si tiene già con 2-4 terre |
| X3 togli sempre | `teferi_race_clock = 0` | Mai in corsa: le regole contro Teferi a ogni orologio (sezione 8) |
| X3 ignora | `teferi_race_clock = kTeferiAlwaysRace` | Sempre in corsa: Teferi si ignora a ogni orologio, anche infinito; Bolt va in faccia come nella corsa (sezione 8) |
| X3 soglia 1, soglia 3 | `teferi_race_clock = 1`, `3` | L'orologio con un'altra soglia |
| X4 | `t1_thoughtseize_first` | Al primo turno di Rakdos, Thoughtseize prima della creatura da un mana (con la soglia di vita della sezione 1). Dal secondo turno non cambia nulla |

**Perché "ignora" è la corsa permanente.** La sezione 8 dice "Corsa: Teferi si ignora": la variante porta la soglia all'infinito, l'estremo opposto di "togli sempre" (soglia 0). *Alternativa scartata:* giocare come se Teferi non ci fosse. Non si può davvero, perché Teferi impone la velocità stregoneria: le regole senza Teferi aspetterebbero il turno di UW per Bolt e Flare, che con Teferi in campo non arriva mai.

**Le partite.** 100.000 per variante, seed da 1 a 100.000 (UW in play con il seed dispari, quindi Rakdos in play con il seed pari): 50.000 per lato. Win rate di Rakdos in play, in draw e in totale (quella di UW è 100 meno quella di Rakdos), con la differenza appaiata per seed e l'intervallo al 95% rispetto alla base (approssimazione normale, come in `docs/agente_uw.md`, sezione 28). Lo strumento è `varianti_rakdos` (`[--games N] [--first-seed S] [--limit T] [--out DIR]`, uscita in `data/matchup/varianti_rakdos.txt`).

**Le misure in più**

- **Per X1 e X2:** le mani di Rakdos da 7, 6 e 5 carte (e 4 o meno), le partite in cui la decisione di mulligan cambia rispetto alla base (il numero di mulligan è diverso: le varianti aggiungono o tolgono solo delle tenute, quindi è esattamente dove le scelte differiscono) e la win rate in quelle partite contro la base sugli stessi seed.
- **Per X3:** le partite in cui UW lancia Teferi; i turni di Rakdos con Teferi avversario in campo, divisi per la modalità della prima decisione di Rakdos di quel turno (corsa: lo ignora; partita lunga: lo tratta da togliere); le volte in cui Teferi va dal campo al cimitero (succede solo per mano di Rakdos: Bolt, attacco, Flare); la win rate di Rakdos nelle partite con Teferi, in quelle con almeno un turno in corsa e in quelle in cui Teferi viene tolto. Fino al lancio di Teferi le varianti di X3 giocano la stessa partita, quindi le partite con Teferi sono le stesse in tutte: la differenza appaiata su di esse è significativa.
- **Per X4:** le partite con Thoughtseize lanciata al primo turno di Rakdos, quante volte prende una carta e quale; le partite in cui la regola di X4 decide davvero (Thoughtseize al posto della creatura da un mana) e la win rate in quelle partite.

**Controllo di coerenza.** La base dà Rakdos al 60,44%, cioè UW al 39,56%: lo stesso valore di base_v2 in `docs/agente_uw.md`, sezione 28 (stessi seed, stesso codice). Nessuna variante produce decisioni "predefinito" di Rakdos.

### Risultati (15/11/2026)

100.000 partite per variante, seed da 1 a 100.000; uscita completa in `data/matchup/varianti_rakdos.txt`.

**1. Win rate di Rakdos e differenza appaiata rispetto alla base** (punti percentuali, intervallo al 95%)

| Variante | Rakdos in play | Rakdos in draw | Totale | Differenza in play | Differenza in draw | Differenza totale |
| --- | --- | --- | --- | --- | --- | --- |
| Base | 65,75% | 55,14% | 60,44% | | | |
| X1-A | 62,90% | 53,35% | 58,13% | −2,85 [−3,06; −2,63] | −1,78 [−1,99; −1,57] | **−2,31 [−2,47; −2,16]** |
| X1-C | 67,80% | 56,67% | 62,23% | +2,05 [+1,84; +2,26] | +1,53 [+1,32; +1,74] | **+1,79 [+1,64; +1,94]** |
| X2 | 65,57% | 55,04% | 60,31% | −0,18 [−0,28; −0,07] | −0,09 [−0,19; +0,00] | **−0,14 [−0,21; −0,06]** |
| X3 togli sempre | 65,71% | 55,13% | 60,42% | −0,04 [−0,08; +0,01] | −0,00 [−0,05; +0,04] | −0,02 [−0,05; +0,01] |
| X3 ignora | 65,89% | 55,13% | 60,51% | +0,14 [+0,04; +0,23] | −0,01 [−0,11; +0,10] | +0,07 [−0,01; +0,14] |
| X3 soglia 1 | 65,73% | 55,14% | 60,44% | −0,02 [−0,06; +0,02] | +0,01 [−0,03; +0,05] | −0,01 [−0,04; +0,02] |
| X3 soglia 3 | 65,75% | 55,14% | 60,44% | −0,00 [−0,04; +0,04] | +0,00 [−0,04; +0,05] | +0,00 [−0,03; +0,03] |
| X4 | 64,42% | 55,42% | 59,92% | −1,33 [−1,57; −1,08] | +0,28 [−0,01; +0,57] | **−0,52 [−0,71; −0,33]** |

**2. X1 e X2: mani di Rakdos e partite in cui la decisione di mulligan cambia**

| Variante | 7 carte | 6 carte | 5 carte | Partite cambiate | Rakdos vince (variante) | (base, stessi seed) | Differenza |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Base | 67,47% | 24,04% | 8,48% | | | | |
| X1-A | 58,82% | 26,82% | 14,35% | 11.553 | 44,49% | 62,50% | −18,01 [−19,20; −16,82] |
| X1-C | 78,03% | 18,56% | 3,41% | 12.881 | 62,55% | 48,65% | +13,90 [+12,78; +15,02] |
| X2 | 70,46% | 21,83% | 7,71% | 2.989 | 45,23% | 49,75% | −4,52 [−6,93; −2,10] |

Per lato, sulle partite cambiate: X1-A −22,07 in play e −13,95 in draw; X1-C +16,05 e +11,78; X2 −5,74 [−9,18; −2,30] in play e −3,23 [−6,61; +0,15] in draw. Nessuna variante arriva a mani da 4 carte o meno: con 5 si tiene sempre.

**3. X3: Teferi.** UW lancia Teferi in 14.378 partite (14,38%), le stesse in tutte le varianti (sopra).

| Variante | Turni di Rakdos con Teferi | In corsa (lo ignora) | In partita lunga | Teferi tolto (partite) | Rakdos vince con Teferi (base 47,65%) | Partite con un turno in corsa: Rakdos vince | Partite con Teferi tolto: Rakdos vince |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Base | 21.495 | 3.566 | 17.929 | 9.131 | 47,65% | 2.861: 96,26% | 9.131: 40,99% |
| Togli sempre | 17.844 | 0 | 17.844 | 11.325 | 47,50% (−0,15 [−0,36; +0,07]) | 0 | 11.325: 51,89% (base 52,10%) |
| Ignora | 33.394 | 33.394 | 0 | 5.315 | 48,10% (+0,45 [−0,04; +0,94]) | 13.220: 52,31% (base 51,82%) | 5.315: 27,54% (base 31,83%) |
| Soglia 1 | 18.759 | 1.505 | 17.254 | 10.394 | 47,60% (−0,05 [−0,25; +0,15]) | 1.419: 98,80% (base 98,87%) | 10.394: 47,77% (base 47,87%) |
| Soglia 3 | 23.355 | 5.295 | 18.060 | 8.581 | 47,66% (+0,01 [−0,19; +0,21]) | 3.872: 92,20% (base 91,58%) | 8.581: 37,79% (base 38,18%) |

Teferi esce dal campo al più una volta per partita (una copia nella lista). Anche con "ignora" viene tolto in 5.315 partite. Dalla lettura del codice (non misurato) l'unica via è Flare of Malice lanciato per la regola del solo bloccante: in corsa gli attacchi vanno tutti sul giocatore e Bolt su Teferi non si lancia, ma Flare fa sacrificare a UW la creatura o il planeswalker di valore di mana più alto, che può essere Teferi. Le colonne "Rakdos vince" delle ultime due sono sulle partite scelte dalla variante, con la base sugli stessi seed tra parentesi.

**4. X4: Thoughtseize al primo turno di Rakdos**

| | Base | X4 |
| --- | --- | --- |
| Lanciata al primo turno | 3.495 partite (3,50%) | 35.587 (35,59%) |
| Prende una carta | 3.495 (100%) | 35.586 (100,00%: una mano di UW con sole terre) |
| Solitude | 45,01% | 44,91% |
| Sanctifier en-Vec | 13,68% | 13,97% |
| March of Otherworldly Light | 13,25% | 13,21% |
| Ocelot Pride | 6,95% | 6,49% |
| Teferi, Time Raveler | 5,55% | 4,98% |
| Thraben Charm | 4,52% | 4,47% |
| Phelia, Exuberant Shepherd | 4,41% | 4,58% |
| Guide of Souls | 3,26% | 3,16% |
| Le altre (Seam Rip, Riddler, Ephemerate, Witch Enchanter, Starfield Shepherd, Force of Negation, Consign, Recruiter) | 3,23% | 4,22% |

La regola di X4 decide in 32.092 partite: lì Rakdos vince il 58,56% contro il 60,19% della base sugli stessi seed, **−1,63 [−2,22; −1,04]**.

**Lettura** (per la decisione, non adottata)

- **X1: C batte B anche contro UW**, di +1,79 punti; A resta la peggiore (−2,31). Nel goldfish C non batteva B in modo chiaro (sezione 19): contro UW tenere la mano da 7 con una terra e una sola carta che cerca vale più di un mulligan, anche con le rimozioni avversarie. Nelle partite cambiate C vince il 62,55% contro il 48,65% della base.
- **X2 peggiora di poco** (−0,14 totale, −4,52 nelle 2.989 partite cambiate): contro UW base_v2 la mano senza creature economiche ma con due interazioni non vale un mulligan a 6.
- **X3: la soglia quasi non conta.** Tutte le varianti sono entro ±0,07 punti dalla base. La corsa scatta per lo più quando Rakdos vince già (96% con la soglia 2): togliere o ignorare Teferi cambia poco. "Ignora" è l'unica con un segnale, +0,14 [+0,04; +0,23] con Rakdos in play, nullo in draw; nelle partite con Teferi +0,45, al limite della significatività.
- **X4 peggiora con Rakdos in play** (−1,33) e non cambia in draw (+0,28, non significativo): al primo turno la pressione della creatura vale più della carta presa. Thoughtseize prende quasi sempre le stesse carte, con o senza X4 (Solitude in quasi metà dei casi).

### Seguito dei test A/B (15/11/2026)

**Decisione di Daniele:** X2, X3 e X4 non si adottano, il comportamento predefinito non cambia. Su X1-C si decide dopo il controllo di robustezza qui sotto. Le opzioni restano, spente, per i confronti futuri.

**1. Robustezza di X1.** Il guadagno di C cresce con la permissività (A < B < C) e potrebbe dipendere da un UW che non punisce un inciampo. A, B e C si rigiocano contro tre versioni di UW: base_v1 (tutte le opzioni di `UwAgentOptions` spente), base_v2 (la predefinita) e base_v2 + M1. 100.000 partite per riga, seed 1-100.000; win rate di Rakdos in play, in draw e in totale, con la differenza appaiata rispetto a B **contro la stessa versione di UW**. Poi, contro base_v2, le sole partite in cui C tiene una mano che B rifiuta (il numero di mulligan di Rakdos è diverso): quante sono, la win rate con C e con B sugli stessi seed e, nelle mani tenute da C, tre misure, separate in play e in draw:
- **seconda terra entro la fine del turno 2 (e del turno 3) di Rakdos:** in un momento qualsiasi fino all'inizio del turno successivo di Rakdos, Rakdos controlla almeno due terre. Si guarda il massimo, perché una fetch rotta lascia il campo per un momento prima che arrivi la terra cercata;
- **non gioca nulla nei primi due turni:** nessuna magia lanciata da Rakdos (in qualsiasi turno, anche Bolt nel turno di UW) prima dell'inizio del suo terzo turno. Le terre, le fetch rotte e i cicli (Street Wraith, Troll) non contano come giocate; Mishra's Bauble lanciato sì.

Le stesse misure si danno anche per B sugli stessi seed, per confronto. Non si adotta niente.

**2. Verifica della regola del turno 1** (sezione 6: "una creatura da un mana viene prima di Thoughtseize, Thoughtseize prima di niente"). Sulle 100.000 partite della base, un osservatore passivo guarda ogni priorità di Rakdos nelle fasi principali del suo primo turno. Il caso da verificare: Thoughtseize in mano a una di quelle priorità, nessuna creatura da un mana lanciabile (nessuna priorità del turno con un lancio legale di una creatura di valore di mana 1 dalla mano) e Thoughtseize non lanciata nel turno. Motivi:
- **nessuna fonte di mana nera:** il lancio di Thoughtseize non è mai legale nel turno (nessuna terra, o solo una terra tappata come Raucous Theater);
- **soglia di vita:** il lancio è legale, ma dopo i 2 punti vita ne resterebbero meno di 5 (sezione 1);
- **senza motivo valido:** il lancio è legale con la vita sopra la soglia, oppure non è mai legale pur con una fonte nera stappata. Deve essere zero; altrimenti le prime 5 partite si guardano come replay leggibile.

Non si cambia l'agente.

**3. Variante X5: cosa prende Thoughtseize** (`thoughtseize_answer_or_threat`, spenta per default). **Linea guida di Daniele, da confermare o smentire con la ricerca ad albero:** "la risposta più forte; in mancanza di risposte la minaccia più pericolosa". Sostituisce l'ordine della sezione 6 così, con la mano di UW rivelata:

*Livello 1, risposte*, in quest'ordine:
- a. **Sanctifier en-Vec**, se noi non abbiamo Flare of Malice in mano;
- b. **Solitude**, se UW ha anche Ephemerate in mano;
- c. **Solitude**, se UW ha anche almeno una rimozione singola tra March of Otherworldly Light, Seam Rip e Thraben Charm. **Interpretazione provvisoria**, da confermare;
- d. poi: Sanctifier en-Vec (con Flare in mano), Solitude da sola, Thraben Charm, March of Otherworldly Light.

Teferi, Time Raveler **non** è una risposta.

*Livello 2, se UW non ha risposte:* la minaccia che la nostra mano non può rimuovere. Minacce: Guide of Souls, Ocelot Pride, Phelia, Quantum Riddler, Starfield Shepherd. Una minaccia è **rimovibile** se abbiamo in mano Lightning Bolt e ha costituzione 3 o meno (Guide 1/2, Ocelot 1/1, Phelia 2/2, Starfield Shepherd 3/2; non Riddler 4/6), oppure Flare of Malice e sarebbe il permanente di valore di mana più alto. Tra le non rimovibili prende la prima nell'ordine Guide, Ocelot, Phelia, Riddler, Shepherd; se sono tutte rimovibili, prende comunque la prima dell'ordine. **Teferi viene dopo tutte le minacce.**

*Scelte fatte implementando* (la linea guida non le decide):
- **"Sarebbe il permanente di valore di mana più alto"**: il valore di mana della minaccia è **strettamente** maggiore di quello di ogni creatura e planeswalker di UW in campo al momento di Thoughtseize. A pari valore sceglie UW cosa sacrificare, quindi la rimozione non è certa. Con il campo di UW vuoto ogni minaccia è rimovibile con Flare;
- **dopo Teferi**, le carte rimaste nell'ordine della sezione 6 (Seam Rip, Ephemerate, Force of Negation, Witch Enchanter, Consign to Memory, Recruiter of the Guard), poi la carta di valore più alto, come la base. Seam Rip conta come rimozione nella riga c), ma non è nella lista delle risposte del livello 1;
- la costituzione è quella stampata (la carta è in mano).

*Misure:* win rate di Rakdos in play, in draw e in totale contro la base, con la differenza appaiata e l'intervallo al 95%; le partite in cui la scelta cambia (almeno una risoluzione di Thoughtseize in cui X5 prende una carta diversa da quella che la base prenderebbe **nello stesso stato**: l'osservatore chiede la scelta a un agente con le opzioni predefinite) e la win rate in quelle partite contro la base sugli stessi seed; le carte prese da tutte le Thoughtseize della partita, base contro X5.

### Risultati del seguito (15/11/2026)

100.000 partite per riga, seed da 1 a 100.000; uscite complete in `data/matchup/x1_robustezza.txt`, `data/matchup/verifica_t1.txt` e `data/matchup/x5/varianti_rakdos.txt`. Con le opzioni predefinite il registro del matchup (10.000 partite) e il goldfish restano identici byte per byte (gli MD5 della sezione 21).

**1. Robustezza di X1** (win rate di Rakdos; differenza appaiata rispetto a B contro la stessa versione di UW)

| UW | Mulligan | Rakdos in play | Rakdos in draw | Totale | Differenza in play | Differenza in draw | Differenza totale |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base_v1 | B | 69,52% | 58,94% | 64,23% | | | |
| | A | 66,76% | 57,23% | 62,00% | −2,75 [−2,97; −2,54] | −1,71 [−1,92; −1,50] | **−2,23 [−2,38; −2,08]** |
| | C | 71,45% | 60,36% | 65,90% | +1,93 [+1,73; +2,14] | +1,42 [+1,21; +1,62] | **+1,67 [+1,53; +1,82]** |
| base_v2 | B | 65,75% | 55,14% | 60,44% | | | |
| | A | 62,90% | 53,35% | 58,13% | −2,85 [−3,06; −2,63] | −1,78 [−1,99; −1,57] | **−2,31 [−2,47; −2,16]** |
| | C | 67,80% | 56,67% | 62,23% | +2,05 [+1,84; +2,26] | +1,53 [+1,32; +1,74] | **+1,79 [+1,64; +1,94]** |
| base_v2 + M1 | B | 64,89% | 53,77% | 59,33% | | | |
| | A | 62,11% | 51,96% | 57,04% | −2,78 [−3,00; −2,56] | −1,81 [−2,02; −1,60] | **−2,29 [−2,45; −2,14]** |
| | C | 66,81% | 55,20% | 61,01% | +1,92 [+1,71; +2,13] | +1,43 [+1,23; +1,64] | **+1,68 [+1,53; +1,82]** |

Controlli di coerenza: B contro base_v1 dà UW al 35,77% e contro base_v2 + M1 al 40,67%, i valori di `docs/agente_uw.md`, sezione 28; le righe contro base_v2 sono identiche a quelle della sezione 21. Contro base_v2 + M1 le mani di Rakdos cambiano di un centesimo di punto (24,03% da 6 carte invece di 24,04%): i mulligan di UW cambiano il mescolamento successivo, quindi alcune mani di Rakdos sono diverse.

**Le partite in cui C tiene una mano che B rifiuta** (contro base_v2; nessuna partita con più mulligan con C che con B)

| | Partite | Rakdos vince con C | con B | C − B | Seconda terra entro il turno 2: C (B) | entro il turno 3: C (B) | Nessuna magia nei primi due turni: C (B) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Rakdos in play | 6.381 | 67,87% | 51,83% | +16,05 [+14,46; +17,64] | 62,51% (90,75%) | 77,03% (93,87%) | 0,00% (1,16%) |
| Rakdos in draw | 6.500 | 57,32% | 45,54% | +11,78 [+10,21; +13,36] | 76,52% (93,62%) | 86,15% (95,75%) | 0,00% (0,52%) |
| Totale | 12.881 | 62,55% | 48,65% | +13,90 [+12,78; +15,02] | 69,58% (92,20%) | 81,63% (94,82%) | 0,00% (0,84%) |

*Lettura.* **Il guadagno di C non dipende dalla versione di UW:** +1,67, +1,79 e +1,68 punti, con intervalli che si sovrappongono; non cresce contro l'UW più debole (base_v1) e non sparisce contro quello che tiene più mani (M1). Le mani tenute da C inciampano davvero: con Rakdos in play il 37% non ha la seconda terra entro il turno 2 e il 23% entro il turno 3 (con B, sulle mani tenute dopo il mulligan negli stessi seed, il 9% e il 6%). Ma non restano mai ferme: la creatura da un mana lanciabile con l'unica terra è una condizione di C, quindi ogni mano gioca almeno quella nei primi due turni. UW non punisce l'inciampo abbastanza da compensare la carta in più.

**2. Verifica della regola del turno 1** (base contro UW base_v2)

| Caso | Rakdos in play | Rakdos in draw | Totale |
| --- | --- | --- | --- |
| Thoughtseize non in mano al primo turno | 30.284 | 27.654 | 57.938 |
| Lanciata | 1.982 | 1.513 | 3.495 |
| Non lanciata, una creatura da un mana lanciabile | 16.179 | 19.361 | 35.540 |
| Non lanciata, nessuna fonte di mana nera | 1.448 | 1.335 | 2.783 |
| Non lanciata, soglia di vita | 0 | 0 | 0 |
| **Non lanciata, senza motivo valido** | **107** | **137** | **244** |

Controllo incrociato: le 3.495 partite con Thoughtseize lanciata sono quelle della tabella 4 della sezione 21, contate dal registro con un'altra funzione.

**I casi senza motivo valido non sono zero.** Tutti vengono da due regole dell'agente che, nell'ordine della sezione 4, precedono Thoughtseize e usano l'unico mana del turno:
- **ciclo di Troll of Khazad-dûm, 217 casi** (107 in play, 110 in draw): con una sola terra in campo e meno di quattro terre tra campo e mano, "serve una terra" (sezione 3) e il ciclo (punto 2 della sezione 4) viene prima di Thoughtseize (punto 5). Nei replay la fetch cerca la Swamp con la regola "serve il nero ora", perché il piano di mana conta Thoughtseize, e subito dopo quel nero paga il ciclo;
- **Lightning Bolt su Guide of Souls o Ocelot Pride a vista, 27 casi**, tutti con Rakdos in draw: UW ha lanciato la creatura al suo turno 1, e la sezione 7 dice "appena l'agente ha la priorità e ha il mana".

I replay leggibili delle prime 5 partite (seed 665, 715, 1151, 1248 con il Troll; 1821 con Bolt) sono in `data/matchup/verifica_t1/`. L'agente non è stato cambiato: la regola "Thoughtseize va giocato sempre" e l'ordine della sezione 4 sono in conflitto in questi due casi, e la scelta tocca a Daniele.

**3. X5: cosa prende Thoughtseize** (linea guida di Daniele, da confermare o smentire con la ricerca ad albero)

| | Rakdos in play | Rakdos in draw | Totale |
| --- | --- | --- | --- |
| Base | 65,75% | 55,14% | 60,44% |
| X5 | 65,85% | 55,33% | 60,59% |
| Differenza appaiata | +0,10 [−0,01; +0,20] | +0,19 [+0,09; +0,30] | **+0,15 [+0,07; +0,22]** |

**La scelta cambia in 6.235 partite** (3.367 con Rakdos in play, 2.868 in draw): lì Rakdos vince il 56,44% contro il 54,10% della base sugli stessi seed, **+2,34 [+1,17; +3,51]**; +1,46 [−0,09; +3,00] in play, +3,38 [+1,60; +5,17] in draw.

Carte prese da tutte le Thoughtseize della partita (93.120 nella base, 93.220 con X5):

| Carta | Base | X5 |
| --- | --- | --- |
| Solitude | 19.500 (20,94%) | 19.669 (21,10%) |
| Phelia, Exuberant Shepherd | 13.296 (14,28%) | 13.803 (14,81%) |
| March of Otherworldly Light | 12.743 (13,68%) | 12.755 (13,68%) |
| Quantum Riddler | 9.533 (10,24%) | 11.722 (12,57%) |
| Thraben Charm | 6.028 (6,47%) | 6.039 (6,48%) |
| Sanctifier en-Vec | 5.636 (6,05%) | 5.484 (5,88%) |
| Witch Enchanter | 5.206 (5,59%) | 4.778 (5,13%) |
| Ephemerate | 4.252 (4,57%) | 3.938 (4,22%) |
| Ocelot Pride | 3.963 (4,26%) | 3.645 (3,91%) |
| Teferi, Time Raveler | 3.783 (4,06%) | 1.048 (1,12%) |
| Guide of Souls | 2.418 (2,60%) | 3.007 (3,23%) |
| Consign to Memory | 2.300 (2,47%) | 2.316 (2,48%) |
| Force of Negation | 1.972 (2,12%) | 1.864 (2,00%) |
| Seam Rip | 1.742 (1,87%) | 1.009 (1,08%) |
| Starfield Shepherd | 580 (0,62%) | 1.978 (2,12%) |
| Recruiter of the Guard | 168 (0,18%) | 165 (0,18%) |

*Lettura.* X5 migliora poco ma in modo significativo (+0,15 punti in totale, +0,19 in draw). Lo spostamento principale è da Teferi (da 3.783 a 1.048 prese) e Seam Rip verso le minacce che Bolt non toglie (Quantum Riddler +2.189, Starfield Shepherd +1.398) e verso Guide e Phelia quando la nostra mano non ha rimozioni. Le risposte del livello 1 cambiano poco: Sanctifier scende di 152 prese (con Flare in mano X5 preferisce Solitude, se UW ha Ephemerate o una rimozione). Le mani di Rakdos sono identiche (la variante agisce dopo i mulligan). Non adottata: si decide insieme.

## 22. Il nuovo comportamento predefinito: base_r2 (15/11/2026)

**Decisione di Daniele (15/11/2026).** Tre modifiche diventano il comportamento predefinito dell'agente (**base_r2**); l'agente com'era prima è **base_r1**. Ogni modifica resta un'opzione di `RakdosAgentOptions`, disattivabile per i confronti futuri; `RakdosAgentOptions::base_r1()` dà le opzioni di base_r1.

| Modifica | Opzione (base_r1 → base_r2) | Regola | Stato |
| --- | --- | --- | --- |
| **X1-C** | `mulligan`: B → **C** | Con 7 o 6 carte si tiene una mano con **una sola terra** se ha almeno **una** carta che cerca tra Street Wraith, Mishra's Bauble e Troll of Khazad-dûm e una creatura da un mana lanciabile con quella terra (sezione 2) | **Ipotesi di Daniele (pilota), misurata, da confermare con la ricerca ad albero** |
| **X5** | `thoughtseize_answer_or_threat`: spenta → **accesa** | Cosa prende Thoughtseize: la risposta più forte, poi la minaccia che la nostra mano non può rimuovere, Teferi dopo le minacce (sezioni 6 e 21). Flare "rimuove" solo un permanente di valore **strettamente** più alto | **Linea guida di Daniele (pilota di UW Blink), da confermare o smentire con la ricerca ad albero.** La riga c) resta un'interpretazione provvisoria |
| **T1** (correzione del turno 1) | `t1_seize_before_troll`: spenta → **accesa** | Al primo turno, senza creatura da un mana lanciabile, Thoughtseize prima del ciclo di Troll, tranne quando non resta un'altra terra in mano per il turno 2. Bolt su Guide o Ocelot a vista resta prima di Thoughtseize (sezione 6) | Correzione di un conflitto tra regole (sezione 21, verifica del turno 1) |

**Riferimento base_r1**, salvato prima di toccare l'agente (UW base_v2; con le opzioni di base_r1 questi registri devono restare identici byte per byte):

| Registro | Comando | MD5 |
| --- | --- | --- |
| Matchup, 10.000 partite | `matchup.exe --replays 20` (`--rakdos-r1` dopo il cambio) | `partite.jsonl` `81c39eba3ce8e4900754136b82d84ff2` |
| Matchup, primi 20 registri completi | come sopra | `replay.jsonl` `2b4dded814c94073724c280797dde696` |
| Goldfish, 10.000 partite | `goldfish.exe --games 10000 --replays 20` (`--rakdos-r1` dopo il cambio) | `partite_B.jsonl` `29a9db59727e85dfb89f295e206cbe03` |
| Goldfish, primi 20 registri completi | come sopra | `replay_B.jsonl` `43d5374437046d328482d93ccfaec22c` |
| Goldfish, 100.000 partite | `goldfish.exe` (`--rakdos-r1` dopo il cambio) | `partite_B.jsonl` `3807bc7f38e13c2e696c882129f00bcf` |

Sono gli stessi MD5 della sezione 21: dal passo precedente il comportamento predefinito non era cambiato.

**Le misure.**

1. **Verifica del turno 1** su 100.000 partite (seed 1-100.000) con base_r2: Thoughtseize in mano al primo turno di Rakdos, nessuna creatura da un mana lanciabile, non lanciata, per motivo. I motivi validi ora sono: nessuna fonte nera, soglia di vita, **ciclo di Troll senza un'altra terra in mano** (la riga sopra) e **Bolt a vista su Guide of Souls o Ocelot Pride**. Il numero senza motivo valido deve essere zero. La stessa tabella per base_r1, con i nuovi motivi, mostra quanti dei 244 casi della sezione 21 diventano validi.
2. **Confronto da 100.000 partite** contro UW base_v2: base_r1, X1-C, X5 e T1 da sole sopra base_r1, le tre coppie, base_r2. Win rate di Rakdos in play, in draw e in totale; differenza appaiata e intervallo al 95% rispetto a base_r1; le sovrapposizioni (la somma delle differenze da sole contro la differenza insieme) per ogni coppia e per le tre insieme.
3. **Goldfish da 100.000 partite** con base_r2 contro base_r1: turno del delirium, turno della vittoria, mani da 7, 6 e 5 carte, partite non vinte, e le soglie d'allarme della sezione 16.

**Riferimento base_r2** (nuovo predefinito, UW base_v2): matchup `matchup.exe --replays 20` `partite.jsonl` `b25af1f58354eb1b7570221e7daa78c8`, `replay.jsonl` `32a56483d4f56a837bc3aa4487737c00`; goldfish `goldfish.exe --games 10000 --replays 20` `partite_C.jsonl` `4433a3a0d5ca49d52f43865e7ac03e50`, `replay_C.jsonl` `4c317586326bade6ad24fc1c0378f4c7`; goldfish da 100.000 `partite_C.jsonl` `592c5c6c9aa4302b1a045afc7829a584`. Con `--rakdos-r1` i cinque registri di base_r1 qui sopra sono identici byte per byte dopo il cambio.

### Risultati (15/11/2026)

Uscite complete in `data/matchup/verifica_t1.txt` e `data/matchup/base_r2.txt`.

**1. Verifica del turno 1** (100.000 partite, contro UW base_v2)

| Caso | base_r2: in play | in draw | totale | base_r1: totale |
| --- | --- | --- | --- | --- |
| Thoughtseize non in mano | 29.480 | 27.004 | 56.484 | 57.938 |
| Lanciata | 1.633 | 1.239 | 2.872 | 3.495 |
| Non lanciata, creatura da un mana lanciabile | 17.367 | 20.312 | 37.679 | 35.540 |
| Non lanciata, nessuna fonte nera | 1.520 | 1.427 | 2.947 | 2.783 |
| Non lanciata, soglia di vita | 0 | 0 | 0 | 0 |
| Non lanciata, ciclo del Troll senza un'altra terra in mano | 0 | 0 | 0 | 6 |
| Non lanciata, Bolt a vista su Guide o Ocelot | 0 | 18 | 18 | 27 |
| **Non lanciata, senza motivo valido** | **0** | **0** | **0** | **211** |

**base_r2 ha zero casi senza motivo valido.** Con la classificazione nuova, i 244 casi di base_r1 della sezione 21 si dividono così: 27 Bolt a vista (ora validi), 6 cicli del Troll senza un'altra terra in mano (validi), 211 cicli del Troll con un'altra terra in mano, che la correzione trasforma in Thoughtseize. In base_r2 i Thoughtseize lanciati al turno 1 scendono a 2.872 non per la correzione ma per X1-C: le mani tenute con una sola terra hanno per costruzione una creatura da un mana, che al turno 1 viene prima (la riga "creatura lanciabile" sale di 2.139). Con la sola correzione sopra base_r1 i lanci salgono da 3.495 a 3.706 (+211, tabella 2).

**2. Win rate di Rakdos contro UW base_v2** (100.000 partite per riga, seed 1-100.000; differenza appaiata rispetto a base_r1, intervallo al 95%)

| Agente | In play | In draw | Totale | Differenza in play | Differenza in draw | Differenza totale |
| --- | --- | --- | --- | --- | --- | --- |
| base_r1 | 65,75% | 55,14% | 60,44% | | | |
| X1-C | 67,80% | 56,67% | 62,23% | +2,05 [+1,84; +2,26] | +1,53 [+1,32; +1,74] | **+1,79 [+1,64; +1,94]** |
| X5 | 65,85% | 55,33% | 60,59% | +0,10 [−0,01; +0,20] | +0,19 [+0,09; +0,30] | **+0,15 [+0,07; +0,22]** |
| T1 | 65,76% | 55,16% | 60,46% | +0,01 [−0,01; +0,03] | +0,02 [−0,00; +0,05] | **+0,02 [−0,00; +0,03]** |
| X1-C + X5 | 67,88% | 56,82% | 62,35% | +2,13 [+1,90; +2,36] | +1,68 [+1,45; +1,91] | +1,91 [+1,74; +2,07] |
| X1-C + T1 | 67,81% | 56,69% | 62,25% | +2,06 [+1,85; +2,27] | +1,55 [+1,35; +1,76] | +1,81 [+1,66; +1,96] |
| X5 + T1 | 65,86% | 55,34% | 60,60% | +0,11 [+0,00; +0,21] | +0,20 [+0,10; +0,31] | +0,15 [+0,08; +0,23] |
| **base_r2** | **67,89%** | **56,83%** | **62,36%** | +2,14 [+1,91; +2,38] | +1,69 [+1,46; +1,92] | **+1,92 [+1,75; +2,08]** |

UW contro base_r2: 37,64% (era 39,56% contro base_r1). Controllo di coerenza: le righe base_r1, X1-C e X5 sono identiche a quelle della sezione 21.

Dove ogni modifica decide (contro base_r1 sugli stessi seed): X1-C cambia il mulligan in 12.881 partite (+13,90 [+12,78; +15,02] lì); X5 cambia la scelta di Thoughtseize in 6.235 (+2,34 [+1,17; +3,51]); **la correzione del turno 1 decide in sole 211 partite** (+7,58 [−0,47; +15,64], non significativo). In base_r2: 12.881, 6.463 e 194 partite.

**Sovrapposizioni** (somma delle differenze da sole meno la differenza insieme, per seed, con l'intervallo al 95%):

| Coppia | Da sole (somma) | Insieme | Sovrapposizione |
| --- | --- | --- | --- |
| X1-C e X5 | +1,94 | +1,91 | +0,03 [−0,01; +0,07] |
| X1-C e T1 | +1,81 | +1,81 | −0,00 [−0,01; +0,00] |
| X5 e T1 | +0,16 | +0,15 | +0,01 [−0,00; +0,01] |
| Tutte e tre | +1,95 | +1,92 | +0,03 [−0,00; +0,07] |

**3. Goldfish da 100.000 partite, base_r2 contro base_r1** (stessi seed; differenze appaiate)

| | Play base_r1 | Play base_r2 | Draw base_r1 | Draw base_r2 |
| --- | --- | --- | --- | --- |
| Mani da 7 / 6 / 5 carte | 67,45 / 23,86 / 8,69% | 78,02 / 18,42 / 3,56% | 67,51 / 24,04 / 8,46% | 78,08 / 18,54 / 3,38% |
| Turno della vittoria: media, mediana | 4,866, 5 | 4,856, 5 | 4,659, 5 | 4,660, 5 |
| Vinte entro T4 / T5 | 36,23 / 82,96% | 35,90 / 83,33% | 46,70 / 90,03% | 46,32 / 89,92% |
| Non vinte entro 15 turni | 0 | 0 | 2 | 1 |
| Turno del delirium: mediana | 2 | 2 | 2 | 2 |
| Delirium entro T2 / T3 / T4 | 51,53 / 70,35 / 81,41% | 53,86 / 72,38 / 83,06% | 58,69 / 77,19 / 86,98% | 60,93 / 78,94 / 88,33% |
| Delirium mai | 5.306 | 4.804 | 3.959 | 3.513 |

Differenze appaiate (base_r2 − base_r1): turno della vittoria −0,0101 [−0,0145; −0,0057] in play, +0,0010 [−0,0029; +0,0049] in draw; vinte entro T4 −0,33 e −0,38 punti; delirium entro T2 +2,34 [+2,11; +2,56] in play e +2,24 [+2,01; +2,46] in draw. Sono i numeri di C − B della sezione 19: un goldfish di controllo con base_r1 e il solo mulligan C (`--rakdos-r1 --mulligan C`) differisce da base_r2 in 232 partite su 100.000, senza differenze misurabili. Contro 60 Plains X5 non agisce (non c'è nulla da prendere) e la correzione del turno 1 cambia solo l'ordine.

**Le soglie d'allarme della sezione 16 sono rispettate:** mediana della vittoria 5 in play e in draw (allarme sotto 4 o sopra 7), mediana del delirium 2 (atteso tra 2 e 4). Il delirium resta al bordo basso dell'intervallo, come in base_r1.

**Lettura**

- **Quasi tutto il guadagno di base_r2 viene da X1-C** (+1,79 dei +1,92 punti); X5 aggiunge +0,15, la correzione del turno 1 +0,02, non significativo. Le tre modifiche si sommano quasi esattamente: nessuna sovrapposizione è significativa (al massimo +0,03 punti tra X1-C e X5, che toccano mani in parte comuni).
- **La correzione del turno 1 è una correzione di coerenza, non di forza:** decide in 211 partite su 100.000 e porta a zero i casi senza motivo valido. Nelle partite in cui decide la win rate sale di 7,6 punti, ma l'intervallo è largo (−0,5; +15,6) e sul totale pesa 0,02 punti.
- **Nel goldfish base_r2 è veloce come base_r1:** stesse mediane, vittoria media identica entro un centesimo di turno; il delirium arriva un po' prima (+2,3 punti entro il turno 2). Spiegazione probabile, non misurata: C tiene più mani da 7, cioè una carta in più, e ognuna delle mani in più ha una carta che cerca (Street Wraith ciclato e Mishra's Bauble sacrificato portano creatura e artefatto nel cimitero).

## 23. Variante X6: la creatura da un mana prima del ciclo del Troll (15/11/2026)

**Da dove viene.** Osservazione del passo base_r2 (diario, 15/11/2026), dalla lettura del codice: al primo turno, con una creatura da un mana lanciabile e una sola terra in campo, il ciclo di Troll of Khazad-dûm (punto 2 della sezione 4) viene prima della creatura (punto 6) se serve una terra (sezione 3). Con una sola terra si può fare una cosa sola: il ciclo prende l'unico mana del turno. La correzione del turno 1 non tocca questo caso (vale senza creatura da un mana). Con X1-C queste mani sono più frequenti: una mano con una sola terra si tiene se ha una carta che cerca, che può essere il Troll, e una creatura da un mana.

**La regola** (opzione `t1_creature_before_troll`, **spenta per default** al momento della misura; predefinita dal 15/11/2026, base_r3, sezione 24): al primo turno di Rakdos, con **una sola terra in campo**, Troll of Khazad-dûm in mano e il ciclo che l'agente farebbe (serve una terra e nel grimorio c'è una Swamp da cercare), se c'è una creatura da un mana lanciabile si lancia la creatura invece del ciclo, nell'ordine del punto 6 (Stitcher's Supplier, Dark Matter Manipulator, poi Moonshadow, Nethergoyf). Il Troll resta in mano: si potrà ciclare dal turno 2. Dal secondo turno, o con due terre in campo, non cambia nulla. Il contatore la registra come "creatura: turno 1 prima del ciclo del Troll (variante X6)".

*Perché la condizione "una sola terra in campo":* è il caso in cui ciclo e creatura si escludono. Al primo turno, dopo la terra giocata, le terre in campo sono comunque una; la condizione resta esplicita perché la regola parla di questo.

**Il comportamento predefinito non cambia:** con X6 spenta il registro del matchup resta identico byte per byte (`matchup.exe --replays 20`, base_r2: `partite.jsonl` `b25af1f58354eb1b7570221e7daa78c8`, `replay.jsonl` `32a56483d4f56a837bc3aa4487737c00`; con `--rakdos-r1` i due MD5 di base_r1 della sezione 22).

**Le misure** (`varianti_rakdos --x6`, uscita in `data/matchup/x6.txt`): 100.000 partite, seed da 1 a 100.000, base_r2 e base_r2 + X6 contro UW base_v2, sugli stessi seed. Separate in play e in draw:

- le partite in cui la regola scatta (la variante lancia la creatura al posto del ciclo almeno una volta);
- in quelle partite, la win rate di Rakdos con X6 e con base_r2 sugli stessi seed, con la differenza appaiata e l'intervallo al 95%. Fino alla decisione le due partite sono identiche, quindi sono esattamente le partite in cui base_r2 cicla il Troll in quella posizione;
- sul totale, la win rate di Rakdos con la differenza appaiata e l'intervallo al 95%.

Non si adotta niente: si decide insieme.

### Risultati (15/11/2026)

100.000 partite per riga, seed da 1 a 100.000, contro UW base_v2; uscita completa in `data/matchup/x6.txt`. Con X6 spenta i registri sono identici byte per byte ai riferimenti: matchup base_r2 e base_r1 (`--rakdos-r1`) e goldfish (`--games 10000 --replays 20`, `partite_C.jsonl` `4433a3a0...`, `replay_C.jsonl` `4c317586...`). Controllo di coerenza: la riga base_r2 è identica a quella della sezione 22.

**Win rate di Rakdos e differenza appaiata rispetto a base_r2** (punti percentuali, intervallo al 95%)

| | Rakdos in play | Rakdos in draw | Totale |
| --- | --- | --- | --- |
| base_r2 | 67,89% | 56,83% | 62,36% |
| base_r2 + X6 | 68,21% | 57,28% | 62,74% |
| Differenza appaiata | +0,32 [+0,16; +0,48] | +0,45 [+0,29; +0,61] | **+0,38 [+0,27; +0,50]** |

**Le partite in cui la regola scatta** (la creatura al posto del ciclo del Troll)

| | Partite | Rakdos vince con X6 | con base_r2 (stessi seed) | Differenza |
| --- | --- | --- | --- | --- |
| Rakdos in play | 4.671 | 69,41% | 66,02% | +3,38 [+1,68; +5,08] |
| Rakdos in draw | 5.087 | 57,13% | 52,70% | +4,42 [+2,81; +6,04] |
| Totale | 9.758 | 63,00% | 59,08% | **+3,92 [+2,76; +5,09]** |

La regola di X6 non compare mai nelle partite di base_r2; le mani di Rakdos sono identiche (la variante agisce dopo i mulligan). Prime partite in cui scatta: seed 13, 19, 29, 30, 65, 72, 90, 100, 123, 141.

**Lettura** (per la decisione, non adottata)

- **X6 migliora in modo significativo, in play e in draw:** +0,38 punti sul totale, +3,92 nelle partite in cui decide. Al turno 1 la pressione e il cimitero della creatura (Supplier e Manipulator macinano tre carte) valgono più della terra garantita per il turno 2; è lo stesso segno di X4, dove la creatura batteva Thoughtseize.
- **La regola scatta spesso:** in quasi il 10% delle partite. Il Troll è una sola copia, ma al turno 1 "serve una terra" quasi sempre (meno di quattro terre tra campo e mano) e la lista ha 15 creature da un mana (Stitcher's Supplier 3, Dark Matter Manipulator 4, Moonshadow 4, Nethergoyf 4).
- **Non misurato:** quante volte il Troll resta in mano senza mai essere ciclato e quanto spesso la seconda terra arriva in ritardo per X6. Si possono aggiungere con l'osservatore delle prime giocate (`EarlyRakdosTracker`) se servono per decidere.

## 24. Il nuovo comportamento predefinito: base_r3 (15/11/2026)

**Decisione di Daniele (15/11/2026).** X6 diventa predefinita (**base_r3**): al primo turno, con una sola terra in campo, Troll of Khazad-dûm e una creatura da un mana lanciabile in mano, si lancia la creatura prima del ciclo (regola nella sezione 23). **Misurata, coerente con la linea del pilota sul turno 1, da confermare con la ricerca ad albero.** Resta un'opzione disattivabile (`t1_creature_before_troll`); `RakdosAgentOptions::base_r2()` dà l'agente di prima (X6 spenta), `base_r1()` spegne anche X6.

**Riferimento base_r2**, salvato prima di toccare l'agente (Release ricompilato da HEAD, UW base_v2). Con le opzioni di base_r2 (`--rakdos-r2`, nuovo in `matchup.exe` e `goldfish.exe`) questi registri restano identici byte per byte; con `--rakdos-r1` restano identici quelli di base_r1 della sezione 22:

| Registro | Comando | MD5 |
| --- | --- | --- |
| Matchup, 10.000 partite | `matchup.exe --replays 20 --rakdos-r2` | `partite.jsonl` `b25af1f58354eb1b7570221e7daa78c8` |
| Matchup, primi 20 registri completi | come sopra | `replay.jsonl` `32a56483d4f56a837bc3aa4487737c00` |
| Goldfish, 10.000 partite | `goldfish.exe --games 10000 --replays 20 --rakdos-r2` | `partite_C_r2.jsonl` `4433a3a0d5ca49d52f43865e7ac03e50` |
| Goldfish, primi 20 registri completi | come sopra | `replay_C_r2.jsonl` `4c317586326bade6ad24fc1c0378f4c7` |
| Goldfish, 100.000 partite | `goldfish.exe --rakdos-r2` | `partite_C_r2.jsonl` `592c5c6c9aa4302b1a045afc7829a584` |

Sono i MD5 di base_r2 della sezione 22. Controllato dopo il cambio: i registri di base_r2 e di base_r1 (`--rakdos-r1`: matchup `81c39eba...` e `2b4dded8...`, goldfish `29a9db59...` e `43d53744...`) sono identici byte per byte.

**Riferimento base_r3** (nuovo predefinito, UW base_v2): matchup `matchup.exe --replays 20` `partite.jsonl` `2302d021603ed245978dc22d9cce88ae`, `replay.jsonl` `a9f2908d0209afc4a1621c9bcc23c094`; goldfish da 100.000 `partite_C.jsonl` `3fc4b26daf526857f3ee70ab9f33d871`.

**Dal 16/11/2026 tutti gli MD5 di questa sezione e della sezione 22 sono storici (generatore unico):** con un generatore per giocatore (passo 0 della ricerca) i riferimenti validi di base_r1, base_r2 e base_r3 sono nella sezione 1 di `docs/metriche.md`; il confronto tra i due generatori (X6 +0,46 [+0,35; +0,56], base_r2 − base_r1 +1,84 [+1,68; +1,99]) è nella sezione 19.

### Risultati (15/11/2026)

**1. Win rate di Rakdos contro UW base_v2** (sezione 23, 100.000 partite): base_r3 **62,74%** (68,21% in play, 57,28% in draw), +0,38 [+0,27; +0,50] rispetto a base_r2. UW contro base_r3: 37,26%.

**2. Robustezza di X6** (`varianti_rakdos --x6-robustezza`, uscita in `data/matchup/x6_robustezza.txt`): base_r2 e base_r3 contro tre versioni di UW, 100.000 partite per riga, seed 1-100.000. Differenza appaiata rispetto a base_r2 contro la stessa versione di UW, in punti, con l'intervallo al 95%.

| UW | Rakdos base_r2: totale | base_r3: totale | Differenza in play | Differenza in draw | **Differenza totale** | Partite in cui X6 scatta | Differenza lì |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base_v1 | 66,23% | 66,64% | +0,30 [+0,15; +0,46] | +0,52 [+0,36; +0,68] | **+0,41 [+0,30; +0,52]** | 9.758 | +4,21 [+3,07; +5,35] |
| base_v2 | 62,36% | 62,74% | +0,32 [+0,16; +0,48] | +0,45 [+0,29; +0,61] | **+0,38 [+0,27; +0,50]** | 9.758 | +3,92 [+2,76; +5,09] |
| base_v2 + M1 | 61,13% | 61,57% | +0,41 [+0,25; +0,57] | +0,46 [+0,30; +0,63] | **+0,44 [+0,32; +0,55]** | 9.742 | +4,50 [+3,32; +5,67] |

**X6 migliora contro tutte e tre le versioni di UW**, in play e in draw, con effetti simili (da +0,38 a +0,44 punti). La riga contro base_v2 è identica alla sezione 23. Contro base_v2 + M1 X6 scatta in 16 partite in meno: M1 cambia il mulligan di UW e da lì la partita (il generatore casuale è uno solo, nello stato, e UW in play gioca prima del primo turno di Rakdos).

**3. Goldfish da 100.000 partite, base_r3 contro base_r2** (stessi seed; 9.970 partite su 100.000 diverse; differenze appaiate base_r3 − base_r2)

| | Play base_r2 | Play base_r3 | Draw base_r2 | Draw base_r3 |
| --- | --- | --- | --- | --- |
| Mani da 7 / 6 / 5 carte | 78,02 / 18,42 / 3,56% | uguali | 78,08 / 18,54 / 3,38% | uguali |
| Turno della vittoria: media, mediana | 4,856, 5 | 4,832, 5 | 4,660, 5 | 4,635, 5 |
| Vinte entro T4 / T5 | 35,90 / 83,33% | 37,38 / 84,13% | 46,32 / 89,92% | 48,12 / 90,56% |
| Non vinte entro 15 turni | 0 | 0 | 1 | 1 |
| Turno del delirium: mediana | 2 | 2 | 2 | 2 |
| Delirium entro T2 / T3 / T4 | 53,86 / 72,38 / 83,06% | 53,95 / 72,26 / 83,03% | 60,93 / 78,94 / 88,33% | 60,87 / 78,79 / 88,25% |
| Delirium mai | 4.804 | 4.870 | 3.513 | 3.617 |

Differenze appaiate: turno della vittoria −0,0239 [−0,0261; −0,0217] in play e −0,0247 [−0,0269; −0,0225] in draw; vinte entro T4 +1,48 [+1,32; +1,63] e +1,80 [+1,63; +1,96] punti; delirium entro T2 +0,09 [−0,06; +0,24] e −0,06 [−0,22; +0,09], non significativo.

**Le soglie d'allarme della sezione 16 sono rispettate:** mediana della vittoria 5 in play e in draw (allarme sotto 4 o sopra 7), mediana del delirium 2 (atteso tra 2 e 4).

**4. Mutazione** (script temporaneo fuori dal repository, ripristino byte per byte): con X6 rotta (la regola non scatta mai) falliscono **4 test case**, 3 senza contare la guardia dei seed 13 e 30: X6 con Supplier, X6 con Manipulator e Thoughtseize, il secondo turno con una sola terra. Due mutazioni di confine fanno fallire un solo test case ciascuna: X6 a ogni turno (il caso del secondo turno) e X6 con qualsiasi numero di terre in campo (il caso con due terre). La seconda condizione al primo turno di una partita vera è sempre vera (dopo la terra giocata le terre in campo sono una), quindi solo una partita preparata la può distinguere.

**Aggiornamento del 15/11/2026 (test di confine):** un secondo test case per ognuna delle due condizioni, in draw e con lo stato finale diverso (il Troll resta in mano, una terra in meno), non solo il nome della regola: il Troll pescato al secondo turno di Rakdos (Moonshadow lanciata al primo turno dalla regola normale), e tre terre già in campo al primo turno con tre creature da un mana. Rilanciate le mutazioni: X6 a ogni turno e X6 con qualsiasi numero di terre fanno fallire **2 test case** ciascuna (prima 1), ognuna solo il suo test nuovo più quello vecchio; X6 che non scatta mai resta a 4 (i due test nuovi non hanno X6 che decide).

**Lettura**

- **X6 è robusta:** stesso segno e stessa grandezza contro UW base_v1, base_v2 e base_v2 + M1, in play e in draw.
- **Nel goldfish base_r3 vince prima:** circa 0,025 turni in meno in media, +1,5 e +1,8 punti di partite vinte entro il turno 4. La creatura al turno 1 attacca un turno prima; il delirium non cambia in modo misurabile, anche se le partite senza delirium salgono di poco (+66 e +104 su 50.000): il Troll ciclato più tardi o mai porta il tipo creatura più tardi.
