# Registro delle ipotesi di Daniele (regole degli agenti euristici)

Le regole degli agenti euristici di UW e di Rakdos sono **convinzioni di un pilota** (Daniele) e di chi le ha scritte con lui: non sono verità. La ricerca ad albero della fase 4 (`docs/progetto.md`, "Roadmap") le confermerà o le smentirà. Questo file ha una riga per regola: da dove viene, a che punto è, come si verifica, che numeri ha oggi.

Compilato il 15/11/2026 da `docs/agente_uw.md` (sezioni 26, 28, 29), `docs/agente_rakdos.md` (sezioni 2, 6, 8, 17, 19-24) e da `docs/diario.md`. **Le regole nuove degli agenti entrano qui con la loro fonte** (regola di `CLAUDE.md`).

## Come leggere le colonne

| Colonna | Valori |
| --- | --- |
| **Fonte** | **Daniele**: la regola o la risposta è sua. **Claude e approvata da Daniele**: proposta o scritta da Claude, adottata dopo la sua decisione. **Claude non rivista**: scritta da Claude, mai passata in revisione con Daniele. **Da verificare con Daniele**: la fonte nei documenti non è certa |
| **Stato** | **decisa**: Daniele l'ha decisa e non è in attesa dell'albero. **provvisoria**: adottata ma da confermare o smentire (interpretazione, ipotesi misurata, regola mai rivista). **da far decidere all'albero**: regola di partenza di cui Daniele non ha una risposta; la decide la ricerca |
| **Come si verifica** | La misura già fatta o prevista nel matchup (test A/B con gli stessi seed, differenza appaiata e intervallo al 95%) |
| **Risultato attuale** | Differenze di win rate in **punti percentuali**, sempre dell'agente della regola; 100.000 partite, seed 1-100.000. Gli intervalli sono al 95% |

"Win rate" è sempre quello dell'agente a cui la regola appartiene (UW per le regole di UW, Rakdos per quelle di Rakdos). Le misure di UW fino al 15/11/2026 sono contro Rakdos base_r1 (sezione 28 di `agente_uw.md`) o base_r3 (sezione 29): è scritto nella riga.

---

## 1. UW Blink: risposte di Daniele (D-1 ... D-18)

Revisione del 10/11/2026 con Daniele, pilota di UW Blink: tutte le risposte sono **regole di partenza**. Fonte di ogni riga: **Daniele**, salvo dove è scritto altrimenti. Nessuna di queste è stata misurata da sola nel matchup, salvo dove la riga ha un numero.

| ID | Agente | Regola (una riga) | Fonte | Stato | Come si verifica | Risultato attuale |
| --- | --- | --- | --- | --- | --- | --- |
| D-1 | UW | Witch Enchanter conta come terra nel mulligan; in partita si gioca come terra solo se serve alla curva o al piano, altrimenti resta in mano per Solitude; 3 vita solo se il mana serve subito. **Interpretazione dell'agente:** "alla curva o al piano" include le istantanee e le creature con lampo (sezione 3 di `agente_uw.md`) | Daniele; l'interpretazione "istantanee e lampo compresi" è di Claude | decisa; **interpretazione provvisoria da confermare con Daniele** (15/11/2026) | Diagnostica `varianti_uw --witch-terra`: giocate che non soddisfano la lettura stretta (solo giocate previste di fase principale). Test A/B: Witch mai come terra, oppure con la lettura stretta | Witch-Blessed Meadow giocata nel 28,72% delle partite, tappata nel 72,36% delle giocate, 3 vita pagate nel 27,64%; 3,27 vita a partita per le terre. Il 19,62% delle giocate serve solo a istantanee o creature con lampo (lettura stretta); lo 0,31% dei pagamenti non ha una giocata prevista. Nessuna misura del valore in win rate |
| D-2 | UW | Mulligan: 2-4 terre (Witch compresa), oppure una con Solitude ed Ephemerate; serve una giocata entro il turno 2 e una risposta, tranne sul play con tre creature da un mana | Daniele | decisa | Mani tenute e win rate per mano (`varianti_uw`); confronto con M1, M1-R, M3 (righe sotto) | M1 (una famiglia di allargamenti di questa regola) +1,18 [+1,04; +1,31] contro base_r3: vedi la divergenza. Mani da 7: 53,49% |
| D-3 | UW | Mana aperto: {1}{W} per Thraben Charm solo con un uso previsto; mai {U} per Consign senza un bersaglio certo (con un trigger programmato sì: W1) | Daniele | decisa (con W1) | Contatori delle regole; carte morte (`carte_morte.exe`) | Consign resta in mano a fine partita nel 65,6% (base_v1); con W1 le sconfitte con 3+ carte scendono da 44,45% a 42,43% |
| D-4 | UW | Solitude evocata in modo aggressivo contro le minacce della classifica; sempre con Quantum Riddler in mano | Daniele | decisa | Contatori; test A/B "Solitude solo a costo pieno" non fatto | Nessun numero |
| D-5 | UW | Ephemerate: prima i pezzi con warp o evocazione, poi protezione, poi valore; in corsa non si rimbalza un attaccante | Daniele | decisa | Contatori; nessun A/B | Nessun numero |
| D-6 | UW | Classifica delle minacce (FOMO con delirium, Phoenix conferita, la creatura più grande che cresce, Troll e Wraith); **Stitcher's Supplier non è una minaccia** | Daniele (la parte su Supplier); la classifica è di Claude e non è stata rivista riga per riga: **da verificare con Daniele** | da far decidere all'albero | Albero libero contro classifica forzata nelle posizioni di scelta del bersaglio | Nessun numero |
| D-7 | UW | Phelia: regola di partenza della sezione 9 (solo con il lampo, alla fine del turno avversario). Per Daniele è un problema "da scacchi" | Daniele (la natura del problema); regola di partenza di Claude | da far decidere all'albero | Albero libero contro la regola nelle posizioni con Phelia in mano | Nessun numero |
| D-8 | UW | Warp di Quantum Riddler: mai al turno 2 senza Consign; con Consign e due blu sì (W1); come ricarica, a mano quasi vuota | Daniele | decisa (con W1 e R1) | W1 e R1 da sole e insieme (righe sotto) | Vedi W1 e R1 |
| D-9 | UW | Thraben Charm: anche il danno, quando uccide una minaccia della classifica e l'esilio del cimitero vale meno | Daniele | da far decidere all'albero | Albero libero contro la regola | Nessun numero |
| D-10 | UW | Sanctifier en-Vec: regola di partenza della sezione 14 (si lancia se l'avversario ha mostrato nero o rosso) | Claude e approvata da Daniele (come regola di partenza) | da far decidere all'albero | Albero libero contro la regola | Nessun numero |
| D-11 | UW | Energia di Guide of Souls: sempre, appena possibile; bersaglio la creatura che arriva a costituzione 4 o più, poi quella che vola meglio | Daniele | decisa | Contatori | Nessun numero |
| D-12 | UW | Teferi: carta debole nel matchup; −3 con un bersaglio ragionevole, altrimenti +1 (con T1: −3 anche su qualsiasi minaccia o nostra entrata utile) | Daniele (D-12); T1 è di Claude | decisa (con T1) | T1 da sola e insieme (righe sotto) | Vedi T1 |
| D-13 | UW | Force of Negation: contrasta Flare of Malice o Bolt sulla nostra creatura migliore; si esilia Consign prima di ogni altra carta blu | Daniele | da far decidere all'albero | Albero libero contro la regola | Force of Negation resta in mano a fine partita nel 74,5% (base_v1); nessuna misura A/B |
| D-14 | UW | Valore delle carte in mano: Phelia prima di Ephemerate (tranne con Solitude in mano); Quantum Riddler molto in alto | Daniele | decisa | Contatori | Nessun numero |
| D-15 | UW | Consign to Memory sui nostri trigger (ritorno di Phelia, Solitude evocata, esilio del warp) quando il mana è disponibile | Daniele | decisa | Contatori; W1 | Consign neutralizza davvero l'esilio nel 53,89% dei lanci di W1 |
| D-16 | UW | Soglia di vita: nei primi tre turni si paga per la curva; dopo, solo se la vita resta sopra la forza avversaria in campo più 3 e almeno 5 | Daniele (la struttura); i numeri 3 e 5 sono di Claude: **da verificare con Daniele** | da far decidere all'albero | Albero libero contro la soglia forzata; test A/B su soglie diverse non fatto | Nessun numero. Witch pagata scende sotto 10 vita in 714 partite su 28.721 |
| D-17 | UW | Non si gioca attorno a Bolt e Flare (Teferi non protetto da Bolt); Bolt conta solo nella scelta dei bersagli (costituzione 4 o più) | Daniele; la lettura sulla costituzione è un'interpretazione di Claude, **da confermare** (diario 10-11/11/2026) | decisa; **interpretazione della costituzione provvisoria** | Albero libero: misura quanto vale giocare attorno ai trucchi | Nessun numero. È anche il limite dichiarato dell'agente euristico |
| D-18 | UW | Attacchi di UW: con i volanti potenziati da Guide; i gatti restano a bloccare | Daniele | decisa | Contatori | Nessun numero |

## 2. UW Blink: varianti misurate (T1, W1, R1, M1, M1-R, M3)

Win rate di UW, differenza appaiata sugli stessi seed. T1, W1 e R1 sono il comportamento predefinito di UW dall'11/11/2026 (**base_v2**, +3,79 [+3,64; +3,93] contro base_v1, contro Rakdos base_r1).

| ID | Agente | Regola (una riga) | Fonte | Stato | Come si verifica | Risultato attuale |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | UW | Teferi −3 su qualsiasi minaccia avversaria della classifica o su una nostra entrata utile (estende D-12) | Claude (dalla diagnostica), approvata da Daniele l'11/11/2026 | decisa come predefinita; **la parte "qualsiasi bersaglio" è provvisoria** | `varianti_uw`: base_v1 + T1, base_v2 senza T1 | **+1,23** [+1,14; +1,31] contro base_v1; senza T1 base_v2 perde −2,56 [−2,68; −2,44]; nessuna sovrapposizione con R1 o W1 (−0,07 e −0,03) |
| W1 | UW | Quantum Riddler con il warp e Consign sul trigger di esilio: Riddler resta in campo (4/6) | **Daniele** (proposta sul turno 6 del seed 2); regola completa scritta da Claude | decisa come predefinita | `varianti_uw`: W1 da sola e in coppia | **+1,83** [+1,73; +1,92] contro base_v1 (+1,66 in play, +1,99 in draw); parte in 10,49% delle partite, lì +17,73 [+16,88; +18,57] |
| R1 | UW | Nella "mano quasi vuota" del warp di Riddler non contano Force of Negation e Consign | Claude (dalla diagnostica delle carte morte), approvata da Daniele l'11/11/2026 | decisa come predefinita; **provvisoria** | `varianti_uw`: R1 da sola e in coppia | **+1,52** [+1,43; +1,62] contro base_v1; sovrapposizione con W1: +0,86 (usano la stessa Riddler) |
| M1 | UW | Nel mulligan contano come risposta anche Recruiter of the Guard e Witch Enchanter | Claude (dalla diagnostica); **non adottata** da Daniele il 15/11/2026 | **decisa contro la misura** (spenta) | `varianti_uw`: base_v2 + M1; `--m1-scomposizione` | **+1,18 [+1,04; +1,31]** contro base_v2 con Rakdos base_r3 (era +1,11 con base_r1); +1,44 in play, +0,91 in draw. Dove cambia il mulligan (11.792 partite): +9,96 [+8,82; +11,10]. **Divergenza: vedi sotto** |
| M1-R | UW | Come M1, ma solo Recruiter conta come risposta (Witch no) | Claude, per capire da dove viene il guadagno di M1 | non adottata (spenta) | `varianti_uw --mulligan-recruiter` | **+0,56 [+0,48; +0,63]** contro base_v2; +0,75 in play, +0,36 in draw. Lì dove decide (3.122 partite): +17,78 |
| M3 | UW | Come M1-R, ma solo se UW gioca per primo | Claude, sulla lettura di Daniele "Recruiter vale di più in play" | non adottata (spenta) | `varianti_uw --mulligan-recruiter` | **+0,38 [+0,32; +0,43]** contro base_v2 (solo in play, +0,75) |

### Divergenza: M1 contro la linea del pilota

La linea di Daniele (15/11/2026): *"Recruiter non basta per un mulligan, Witch non è una risposta"*; Recruiter è una carta jolly, lenta nel matchup; Witch come terra può costare 3 vita. La misura dice il contrario: tenere quelle mani vince **+1,18 punti** contro base_r3 (+1,11 contro base_r1). Anche in draw, dove Recruiter dovrebbe valere meno, la mano tenuta vince più del mulligan (+0,36 con M1-R). Dove decide, Witch da sola vale +7,14 e Recruiter da solo +19,43; il guadagno di Witch probabilmente viene dal mulligan evitato più che dalla carta (non misurato). La decisione resta quella del pilota: l'albero dirà chi ha ragione. **Cosa mostra l'albero se ha ragione la misura:** nelle mani con Recruiter o Witch e senza altra risposta, l'albero tiene la mano più spesso di D-2.

## 3. Rakdos Delirium

Agente predefinito dal 15/11/2026: **base_r3**. Misure contro UW base_v2, differenza appaiata di Rakdos rispetto all'agente precedente. L'agente "di prima" è base_r1 (mulligan B, senza X5, senza correzione del turno 1, senza X6).

| ID | Agente | Regola (una riga) | Fonte | Stato | Come si verifica | Risultato attuale |
| --- | --- | --- | --- | --- | --- | --- |
| R-MUL | Rakdos | Mulligan a 7 carte: 2-4 terre e una creatura da 1-2 mana lanciabile; a 6 carte basta 2-4 terre; a 5 o meno si tiene sempre | Claude, approvata alla revisione del 9/11/2026 (da verificare con Daniele quanto sia sua: il documento dice solo "approvato") | provvisoria | X2 (sotto) per la parte "creatura da 1-2 mana"; `--x1-robustezza` per le terre | X2 ne tiene una parte in più e peggiora (−0,14). Nessuna misura della regola intera |
| R-A | Rakdos | Mulligan con una terra, variante A: sempre mulligan | Claude, solo come confronto | non usata | X1 | −2,31 [−2,47; −2,16] contro base_r1 (−2,23 contro UW base_v1, −2,29 contro base_v2 + M1) |
| R-B | Rakdos | Mulligan con una terra, variante B: si tiene con almeno due carte che cercano (Street Wraith, Bauble, Troll) e una creatura da un mana (base_r1) | Claude, scelta dopo il goldfish del 9/11/2026 | sostituita da R-X1C | Goldfish: B − A −0,075 turni in play, −0,050 in draw | Riferimento di base_r1 |
| R-X1C | Rakdos | **X1-C**: una terra basta con **una sola** carta che cerca e una creatura da un mana lanciabile | Claude, adottata dopo la misura; l'ipotesi di Daniele era B (due carte che cercano) | **provvisoria**: "da confermare con la ricerca ad albero" (decisione del 15/11/2026) | `varianti_rakdos`, `--x1-robustezza`: contro tre versioni di UW | **+1,79 [+1,64; +1,94]** contro base_r1 (+2,05 in play, +1,53 in draw); +1,67 contro UW base_v1, +1,68 contro base_v2 + M1. Dove cambia il mulligan (12.881 partite): +13,90 [+12,78; +15,02]. **Nota di processo:** la convinzione del pilota era B; C è stata adottata dopo la misura (+1,79), non prima |
| R-X2 | Rakdos | **X2**: con 7 carte e 2-4 terre ma senza creatura da 1-2 mana, tenere la mano se ha almeno due tra Thoughtseize, Bolt e Flare | Claude (dato della sezione 20 di `agente_rakdos.md`) | non adottata (spenta) | `varianti_rakdos` | −0,14 [−0,21; −0,06]; sulle 2.989 partite cambiate −4,52 [−6,93; −2,10] |
| R-X3a | Rakdos | **Principio dell'orologio contro Teferi:** quando Rakdos può chiudere la partita, Teferi si ignora (corsa: attacchi sul giocatore, Bolt in faccia); lo si toglie solo nelle partite stabilizzate o dopo che UW ha guadagnato vita | **Daniele** (revisione del 10/11/2026) | decisa | X3 "ignora" (Teferi sempre ignorato) contro la base | **+0,07 [−0,01; +0,14]** (+0,14 [+0,04; +0,23] in play, nullo in draw): nessuna prova contro il principio. Teferi in 14.378 partite (14,38%); la corsa scatta quasi solo dove Rakdos vince già (96,26%) |
| R-X3b | Rakdos | **Soglia dell'orologio:** corsa con orologio di 2 turni o meno, partita lunga oltre | **Claude** (stima, non misurata prima): **da verificare con Daniele** | provvisoria; **conta poco** | X3: soglie 0, 1, 3 contro la soglia 2 | Tutte entro ±0,07 dalla soglia 2: togli sempre (0) −0,02 [−0,05; +0,01], soglia 1 −0,01 [−0,04; +0,02], soglia 3 +0,00 [−0,03; +0,03] |
| R-ORD | Rakdos | **Ordine di Thoughtseize (base_r1):** Sanctifier, Solitude, Thraben Charm, March, Teferi, Ocelot, Guide, Phelia, il resto | Decisa in revisione il 9/11/2026: **da verificare con Daniele** | sostituita da R-X5 (resta come coda dopo Teferi) | X5 contro base | Riferimento di base_r1 |
| R-X5 | Rakdos | **X5, cosa prende Thoughtseize:** la risposta più forte; se non ci sono, la minaccia che la nostra mano non può rimuovere (Guide, Ocelot, Phelia, Riddler, Shepherd); Teferi dopo le minacce | **Daniele** (linea guida: "la risposta più forte; in mancanza di risposte la minaccia più pericolosa"); i dettagli sono di Claude | **provvisoria**: "da confermare o smentire con la ricerca ad albero". La **riga c)** (Solitude con una rimozione singola avversaria) e "Flare rimuove solo un valore strettamente più alto" sono **interpretazioni provvisorie** | `varianti_rakdos` (base contro X5); `--base-r2` | **+0,15 [+0,07; +0,22]** (+0,10 in play, +0,19 in draw). La scelta cambia in 6.235 partite: +2,34 [+1,17; +3,51] lì. Teferi preso da 3.783 a 1.048 volte |
| R-T1F | Rakdos | **Correzione del turno 1:** senza creatura da un mana, Thoughtseize prima del ciclo del Troll se resta un'altra terra in mano; Bolt a vista su Guide o Ocelot prima di Thoughtseize | Claude (verifica del turno 1), decisa da Daniele il 15/11/2026 | decisa; correzione di coerenza, non di forza | `--verifica-t1`: casi senza motivo valido; `--base-r2` | **+0,02 [−0,00; +0,03]** (non significativo); decide in 211 partite (+7,58 [−0,47; +15,64]). Casi senza motivo valido: da 211 a **0** |
| R-X4 | Rakdos | **X4:** al turno 1 Thoughtseize prima della creatura da un mana | Claude, come test della regola "creatura prima di Thoughtseize" | non adottata (spenta) | `varianti_rakdos` | **−0,52 [−0,71; −0,33]**; in play −1,33 [−1,57; −1,08]; dove la regola decide (32.092 partite) −1,63 [−2,22; −1,04] |
| R-X6 | Rakdos | **X6:** al turno 1, con una sola terra in campo, Troll in mano e una creatura da un mana lanciabile, la creatura prima del ciclo del Troll | Claude (osservazione dal codice), approvata da Daniele il 15/11/2026: "misurata, coerente con la linea del pilota sul turno 1" | **provvisoria**: "da confermare con la ricerca ad albero" | `--x6`, `--x6-robustezza` | **+0,38 [+0,27; +0,50]** contro UW base_v2 (+0,32 in play, +0,45 in draw); +0,41 contro base_v1, +0,44 contro base_v2 + M1. Scatta in 9.758 partite: +3,92 [+2,76; +5,09] lì |
| R-BOLT | Rakdos | **Bolt a vista:** su Teferi (se lo toglie), Ocelot Pride e Guide of Souls appena possibile; gli altri nella sottofase finale avversaria; in faccia solo se letale, con 6 vita o meno senza bersagli, o per il delirium | Claude, regola di partenza della prima versione ("decisione della prima versione", sezione 17); l'ordine Teferi-prima-di-Ocelot è un'ipotesi: **da verificare con Daniele** | provvisoria | Test A/B "Teferi prima o dopo Ocelot" mai fatto; `carte_morte.exe` | Nessun numero. Nel goldfish ritarda delirium e vittoria (sezione 16) |
| R-A4 | Rakdos | **A-4, mana aperto per Bolt:** nessun mana tenuto per Bolt nel turno di UW (oggi); da provare: tenere {R} aperto quando UW ha Ocelot Pride, Guide of Souls o Phelia | Claude (audit 1, A-4) | **da far decidere all'albero** | Albero libero contro la regola attuale. **È un A/B sulle regole euristiche non ancora misurato, rimandato alla ricerca ad albero** (nel matchup con gli stessi seed non è stato fatto) | Nessun numero |
| R-SOGLIA | Rakdos | **Soglia di vita 5:** le azioni che costano vita per scelta (fetch, shockland, ciclo di Wraith, Thoughtseize) solo se restano almeno 5 punti vita | Claude: **da verificare con Daniele** | provvisoria | Albero libero contro la soglia; la verifica del turno 1 non ha mai trovato casi per questo motivo (0 casi) | Nessun numero |

Nota sulla colonna "Fonte" di Rakdos: il documento `agente_rakdos.md` dice "approvato e implementato (9/11/2026)" ma non separa le regole di Daniele da quelle di Claude. Dove non c'è una frase esplicita (ipotesi, linea guida, decisione) la riga è "da verificare con Daniele".

---

## 4. Come l'albero le verifica

Nessuna di queste misure esiste ancora: la ricerca ad albero (fase 4) non c'è. Qui è fissato **cosa si misura**, così che ogni regola del registro abbia lo stesso confronto e nessun risultato si possa scegliere dopo averlo visto.

### Il confronto

Per ogni regola del registro, con gli **stessi seed** (1-100.000, UW in play con il seed dispari) e gli stessi mazzi:

1. **Accordo** = nelle posizioni in cui la regola si applica (le decisioni che il suo contatore registra), la quota di volte in cui la scelta dell'albero coincide con quella della regola. Si conta per decisione, non per partita. Un **accordo basso** dice che l'albero sceglie altro; non dice ancora che sia meglio.
2. **Differenza di win rate** = win rate dell'albero libero meno win rate dell'albero con **quella regola forzata** nelle sue posizioni (tutte le altre scelte restano all'albero), differenza appaiata per seed con l'intervallo al 95%. È il valore della regola contro la scelta libera: positiva = la regola costa punti all'albero; negativa o nulla = la regola regge.
3. **Posizioni in cui decide** = le partite in cui l'albero sceglie diversamente dalla regola almeno una volta. Come per X1-C e X6 si dà anche la differenza **lì**, non solo sul totale: sul totale quasi ogni regola pesa poco.
4. **Avversario: più versioni.** L'albero va misurato **contro più versioni di avversario**, perché una regola può reggere solo contro l'agente che l'ha ispirata (il guadagno di X1-C è stato verificato così, contro tre UW, e non è cambiato). Contro UW: base_v1, base_v2 e base_v2 + M1. Contro Rakdos: base_r1, base_r2 e base_r3. Una regola "confermata" deve avere lo stesso segno contro tutte; un segno che cambia con l'avversario va scritto nella riga.
5. **Informazione nascosta.** L'albero (ISMCTS) vede solo quello che vedrebbe il giocatore, come l'agente euristico: la differenza non può nascere da una mano avversaria letta.

### Cosa conta come "confermata" o "smentita"

| Esito | Condizione (per ogni versione di avversario) |
| --- | --- |
| **Confermata** | Differenza di win rate (albero libero − regola forzata) con intervallo che contiene lo zero **oppure** accordo alto (da fissare con i primi risultati) |
| **Smentita** | L'albero libero vince in modo significativo e la differenza ha lo stesso segno contro tutte le versioni |
| **Dipende dall'avversario** | Segno diverso tra versioni: la regola resta, la riga lo dice |

Le soglie dell'accordo si fissano **dopo** aver visto la distribuzione degli accordi su tutte le regole, non riga per riga.

### Per regola

Una riga per regola del registro. Le colonne si compilano quando esiste l'albero; fino ad allora sono vuote di proposito.

| ID | Posizioni in cui si applica (contatore) | Accordo con l'albero | Differenza di win rate: albero libero − regola forzata [IC 95%] | Avversari da misurare |
| --- | --- | --- | --- | --- |
| D-1 | Terra: Witch-Blessed Meadow (serve alla curva) e pagamento dei 3 punti vita | da misurare | da misurare | UW contro Rakdos base_r1, base_r2, base_r3 |
| D-2 | Mulligan di UW (tenere o rifiutare) | da misurare | da misurare | idem |
| D-3 | Mana tenuto aperto nel turno avversario | da misurare | da misurare | idem |
| D-4 | Solitude evocata | da misurare | da misurare | idem |
| D-5 | Ephemerate: rimbalzo e protezione | da misurare | da misurare | idem |
| D-6 | Scelta del bersaglio per ogni rimozione di UW | da misurare | da misurare | idem |
| D-7 | Phelia: lancio con il lampo e bersagli | da misurare | da misurare | idem |
| D-8 | Warp di Riddler (compresi W1 e R1) | da misurare | da misurare | idem |
| D-9 | Thraben Charm: danno o esilio | da misurare | da misurare | idem |
| D-10 | Sanctifier en-Vec | da misurare | da misurare | idem |
| D-11 | Energia di Guide of Souls | da misurare | da misurare | idem |
| D-12, T1 | Teferi: +1 o −3 | da misurare | da misurare | idem |
| D-13 | Force of Negation | da misurare | da misurare | idem |
| D-14 | Scarto e valore delle carte in mano | da misurare | da misurare | idem |
| D-15 | Consign to Memory | da misurare | da misurare | idem |
| D-16 | Pagamenti di vita (fetch, Fountain, Witch) | da misurare | da misurare | idem |
| D-17 | Bersagli esposti a Bolt (costituzione 4+) | da misurare | da misurare | idem |
| D-18 | Attacchi e blocchi di UW | da misurare | da misurare | idem |
| W1, R1 | Lancio di W1; ricarica di R1 | da misurare | da misurare | idem |
| M1, M1-R, M3 | Mulligan di UW con Recruiter o Witch senza altra risposta | da misurare | da misurare; **atteso:** l'albero tiene la mano più spesso di D-2 se ha ragione la misura (+1,18), meno spesso se ha ragione il pilota | idem |
| R-MUL, R-X1C, R-A, R-B | Mulligan di Rakdos (soprattutto una sola terra) | da misurare | da misurare | Rakdos contro UW base_v1, base_v2, base_v2 + M1 |
| R-X2 | Mulligan di Rakdos: mano senza creatura da 1-2 mana | da misurare | da misurare | idem |
| R-X3a, R-X3b | Attacchi e Bolt con Teferi in campo (corsa e partita lunga) | da misurare | da misurare | idem |
| R-ORD, R-X5 | Scelta di Thoughtseize | da misurare | da misurare | idem |
| R-T1F, R-X4, R-X6 | Turno 1: ciclo del Troll, Thoughtseize, creatura da un mana | da misurare | da misurare | idem |
| R-BOLT | Bolt: bersaglio e momento | da misurare | da misurare | idem |
| R-A4 | Mana aperto per Bolt nel turno di UW | da misurare | da misurare | idem |
| R-SOGLIA | Pagamenti di vita di Rakdos | da misurare | da misurare | idem |

### Cosa serve all'albero perché il confronto si possa fare

- **Contatore per regola nell'albero:** ogni regola deve poter essere forzata per nome nelle sue posizioni. I nomi dei contatori dell'agente euristico sono già la chiave (sezione 24 di `agente_uw.md`, sezione 15 di `agente_rakdos.md`); l'albero li deve riusare.
- **Stessi seed e stesso modo di contare** della differenza appaiata di `varianti_uw` e `varianti_rakdos`.
- **Conoscenze sull'avversario in configurazione, non nel codice** (regola di `CLAUDE.md`): ordini di Thoughtseize, tabelle di valore, classifica delle minacce. Così "regola forzata" e "albero libero" cambiano un file, non il motore.
