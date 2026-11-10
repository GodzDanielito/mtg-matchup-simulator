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

- ci sono **da 2 a 4 terre**, oppure **una sola terra** secondo la variante B (sotto);
- c'è almeno **una creatura da 1 o 2 mana** (Nethergoyf, Moonshadow, Dark Matter Manipulator, Stitcher's Supplier, Fear of Missing Out);
- le terre possono lanciarla: con sole Swamp serve una creatura nera.

*Perché:* il mazzo vince mettendo in campo creature economiche nei primi turni e riempiendo il cimitero; una mano senza giocate presto perde il tempo che il mazzo ha sull'avversario.

**Con 6 carte** si tiene con 2-4 terre (o una, con la variante B). **Con 5 o meno** si tiene sempre. Le terre si contano dopo aver messo in fondo.

*Perché:* sotto le 6 carte un altro mulligan costa più di una mano mediocre.

**Mani con una sola terra: tre varianti** (decisione del 9/11/2026). Rakdos Delirium tiene mani con una sola terra grazie alle carte che cercano carte: Street Wraith si cicla gratis, Mishra's Bauble costa zero e fa pescare, Troll of Khazad-dûm cerca direttamente una Swamp. È la pratica dei giocatori veri ed è il motivo delle 17 terre. Le varianti valgono con 7 o 6 carte; con 5 o meno si tiene già sempre.

| Variante | Regola con una sola terra |
| --- | --- |
| **B (predefinita)** | Si tiene se la mano finale ha **almeno due** carte tra Street Wraith, Mishra's Bauble e Troll of Khazad-dûm e **almeno una creatura da un mana** lanciabile con i colori di quella terra. Una fetch conta come terra che cerca la Swamp |
| C (più permissiva) | Come B, ma basta **una** di quelle carte |
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

**Ciclo di Troll of Khazad-dûm (swampcycling {1}):** si usa se non abbiamo una terra da giocare in questo turno o nel prossimo, oppure se ne abbiamo meno di quattro tra campo e mano, e solo se nel grimorio resta una terra con il tipo Swamp (Swamp, Blood Crypt, Raucous Theater). Cerca come una fetch. *Perché:* sei mana per lanciarlo arrivano raramente, e nel cimitero il Troll aggiunge comunque il tipo creatura.

## 4. Ordine delle giocate nel turno

**Mantenimento:** la pescata ritardata di Mishra's Bauble avviene da sola.

**Prima fase principale**, in quest'ordine:

1. **Ciclo di Street Wraith** (2 vita). *Perché:* pescare prima di giocare la terra: la carta pescata può essere la terra del turno.
2. **Ciclo di Troll** se serve una terra (sezione 3).
3. **Mishra's Bauble**: lanciarlo e sacrificarlo subito. *Perché:* costa zero, mette un artefatto nel cimitero e, sacrificato subito, non resta esposto a Witch Enchanter o Consign to Memory.
4. **Terra**, e rompere la fetch.
5. **Thoughtseize** (sezione 6).
6. **Creature che macinano:** Stitcher's Supplier, poi Dark Matter Manipulator. *Perché:* le carte macinate possono dare il delirium e le prove prima delle decisioni successive.
7. **Moonshadow**, poi **Nethergoyf**. *Perché:* Moonshadow cresce solo mentre è in campo, quindi prima entra e meglio è; Nethergoyf ha subito la forza piena.
8. **Fear of Missing Out** (sezione 9).
9. **Detective's Phoenix**: conferimento o lancio normale (sezione 10). Prima del combattimento, per la rapidità.
10. **Rimozione prima di attaccare:** Lightning Bolt o Flare of Malice su un bloccante che fermerebbe un attacco importante (sezioni 7 e 11). **Con Teferi avversario in campo**, qui si lanciano anche i Bolt e i Flare che l'agente lancerebbe nel turno di UW (sezioni 7 e 11).

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

**Cosa prendere**, dalla più importante (ordine deciso in revisione):

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

1. **Teferi**, se Bolt lo toglie dal campo: da solo (fedeltà 3 o meno) oppure insieme al danno di combattimento previsto su di lui in questo turno (fedeltà meno quel danno pari a 3 o meno). Se l'attacco da solo basta a toglierlo, Bolt resta per gli altri bersagli. *Perché:* Teferi ci toglie Bolt e Flare nel turno di UW, rimbalza una creatura e pesca ogni due turni; finché resta in campo metà della nostra interazione è ferma. **Candidato a un test A/B** (Teferi prima di Ocelot Pride, o dopo).
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
- **Con Teferi avversario in campo**, ogni Bolt che l'agente lancerebbe nel turno di UW si sposta nella nostra prima fase principale, prima del combattimento, con le stesse regole di bersaglio (creature della lista, oppure in faccia con 6 vita o meno).

## 8. Attacchi e blocchi

Il motore chiede gli attaccanti **una creatura alla volta**. Per ognuna l'agente considera come possibili bloccanti le creature stappate dell'avversario, senza contare quelle con lampo che potrebbe avere in mano.

**Attacca se vale almeno una di queste:**

1. **Non può essere bloccata:** volare contro nessun volatore, minaccia contro meno di due bloccanti, Troll contro meno di tre.
2. **Nessun blocco conviene all'avversario:** nessun bloccante la uccide sopravvivendo. Il conto tiene conto di attacco improvviso (Ocelot Pride) e protezione (Sanctifier en-Vec non subisce danno dalle nostre creature).
3. **Fa parte di un attacco letale:** la forza totale degli attaccanti supera i punti vita dell'avversario più il danno che i suoi bloccanti possono fermare.
4. **È Fear of Missing Out e il trigger scatterà** (primo attacco del turno e delirium). *Perché:* il combattimento aggiuntivo vale più del rischio di perdere FOMO in un blocco.

**Resta indietro** comunque una creatura se le creature avversarie possono ucciderci nel prossimo turno. In quel caso resta la creatura che blocca meglio.

**Giocatore o Teferi.** Le creature che attaccano per le regole qui sopra scelgono chi attaccare:

- **con un attacco letale sul giocatore** (regola 3, contando come bloccanti tutte le creature stappate dell'avversario) tutte vanno sul giocatore: Teferi si ignora;
- altrimenti va su Teferi **abbastanza forza da togliergli tutta la fedeltà**, partendo dalle creature che non possono essere bloccate; il resto va sul giocatore. Dentro ogni gruppo (prima le non bloccabili, poi le altre) si sceglie la creatura con meno forza che basta da sola per la fedeltà rimasta, altrimenti la più forte, e si ripete finché la fedeltà è coperta;
- se tutte insieme non bastano a toglierlo, vanno tutte sul giocatore.

*Perché:* Teferi in campo ferma Bolt e Flare nel turno di UW e rimbalza una creatura ogni due turni; toglierlo vale più di qualche punto vita, ma solo se l'attacco lo toglie davvero. Le creature non bloccabili prima, perché il danno su Teferi arriva di sicuro; la più piccola che basta, per non sprecare forza che andrebbe sul giocatore.

**Combattimenti aggiuntivi:** stesse regole, sullo stato aggiornato.

**Divisione del danno** tra più bloccanti: danno letale ai bloccanti in ordine di minaccia (la classifica di Bolt), poi il resto all'ultimo.

**Blocchi.** Il motore chiede i bloccanti attaccante per attaccante. Per ogni attaccante:

1. **Blocca se il bloccante uccide l'attaccante e sopravvive.**
2. **Scambia** (muoiono entrambi) se l'attaccante vale più del bloccante (sezione 12). Stitcher's Supplier blocca volentieri: morendo macina 3.
3. **Blocca in due** un attaccante grande solo se i due insieme lo uccidono e l'attaccante può uccidere al massimo il meno prezioso.
4. **Blocco di sacrificio** (il bloccante muore senza uccidere) solo se altrimenti moriremmo in questo turno.
5. Altrimenti **non blocca.**

*Perché:* Rakdos vince la corsa con creature che crescono; perderle in blocchi alla pari lo rallenta, salvo quando la creatura che muore ci guadagna (Supplier).

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
- tutti i suoi permanenti di valore più alto sono minacce vere: Quantum Riddler, Teferi, Solitude, Starfield Shepherd, oppure Sanctifier en-Vec quando è lui il più alto (la protezione non ferma un sacrificio). **Teferi da solo come permanente di valore più alto è sempre un motivo valido**, anche pagando con il mana nel nostro turno;
- **mai** se l'avversario non ha creature né planeswalker, né con il mana né con il sacrificio: una propria creatura non si sacrifica per un Flare che non colpisce nulla. La carta resta in mano e conserva il suo valore (sezione 12).

**Momento:** nella sottofase finale avversaria, dopo aver visto cosa ha fatto. Fanno eccezione due casi, che si giocano nella nostra prima fase principale:

- serve togliere il solo bloccante prima di un attacco importante;
- c'è Teferi in campo: il Flare che l'agente lancerebbe nel turno di UW si sposta nella prima fase principale, prima del combattimento, con le stesse condizioni di valore.

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
| Agente contro Teferi (audit 1, 10/11/2026) | Attacco: senza un attacco letale sul giocatore, su Teferi abbastanza forza da toglierlo, prima le creature non bloccabili, il resto sul giocatore (sezione 8). Bolt: Teferi primo bersaglio se Bolt lo toglie, da solo o dopo il danno di combattimento previsto (sezione 7). Con Teferi in campo, Bolt e Flare del turno di UW si spostano nella nostra prima fase principale, prima del combattimento (sezioni 4, 7, 11). Flare: Teferi come permanente di valore più alto è un motivo valido, anche pagando con il mana nel nostro turno (sezione 11) |
| Carta lanciata dal cimitero | Conferendo la Phoenix dal cimitero esce anche la Phoenix (creatura e incantesimo); con la fuga esce anche Nethergoyf (creatura). I controlli "le prove non costano il delirium" e "restano almeno 3 tipi" si fanno sul cimitero dopo l'esilio **e** dopo l'uscita della carta lanciata (sezioni 10 e 13) |
| Candidati ai test A/B futuri | Turno 1: creatura da un mana contro Thoughtseize (sezione 6). Nel matchup: tenere una mano da 7 con 2-4 terre, Thoughtseize e Lightning Bolt ma senza creature da 1-2 mana (sezione 20); Bolt su Teferi prima di Ocelot Pride, contro dopo (sezione 7); mana aperto per Bolt (sezione 4, audit 1 A-4). *(Le mani da 7 con una sola terra sono state provate: sezione 19)* |
| Soglie del goldfish | Mediana del turno della vittoria sotto il 4 o sopra il 7: allarme. Turno del delirium atteso tra il 2 e il 4. Stime, non dati (sezione 16) |
| Stato pubblico e avversario | L'agente non sa chi ha davanti, ma le regole dipendono dallo stato pubblico. Valore in mano: Flare conserva valore senza creature avversarie. Lancio: Flare solo se l'avversario ha una creatura o un planeswalker, mai sacrificando una propria creatura a vuoto (sezioni 1 e 11) |
| Bolt nel goldfish | Con "Bolt sulle creature finché l'avversario ha più di 6 vita", nel goldfish Bolt resta in mano fino a 6 punti vita: ritarda delirium e vittoria rispetto al reale, da considerare nelle soglie (sezione 16) |
| Cartelle | `agents/` per gli agenti euristici, `tools/` per l'eseguibile del goldfish |
| Goldfish e carte giocate comunque | L'agente lancia Thoughtseize come in partita, anche se contro 60 Plains non prende nulla: in partita finisce nel cimitero come stregoneria e contribuisce al delirium. Vale per ogni carta che in partita si giocherebbe comunque (sezioni 1 e 16) |

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
