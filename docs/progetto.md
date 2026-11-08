# Simulatore di matchup Modern: UW Blink vs Rakdos Delirium

Nov 8, 2026 · @Daniele Paolì

## Obiettivo

Costruire un simulatore che gioca 100.000 partite di gara 1 tra UW Blink e Rakdos Delirium, usando solo i mazzi di main di due liste fisse, e produce dati per carta in stile 17lands. Con 100.000 partite il margine d'errore sul win rate del matchup scende a circa ±0,3 punti percentuali (intervallo di confidenza al 95%), contro i ±5-10 punti dei dati da torneo.

Rakdos Delirium è il mazzo in più forte crescita del meta e, dalle nostre analisi, il matchup più difficile per UW Blink: è qui che dati affidabili valgono di più.

Output attesi:

- **Win rate del matchup in gara 1**: complessivo e separato tra play e draw.
- **Metriche per carta**: quanto vinci quando una carta è in mano, nella mano iniziale, pescata dopo o mai vista (dettaglio nella sezione Metriche).
- **Analisi dei mulligan**: win rate per mani da 7, 6 e 5 carte e per tipo di mano tenuta.
- **Analisi dei turni chiave**: in che turno l'avversario raggiunge il delirium, quando arrivano le sue minacce grandi e con quale carta le gestisci.

Perimetro della prima versione: niente sideboard. Il Bo3 con i piani di side diventa una fase successiva, da aggiungere quando il motore è validato.

## Architettura

&#91;embedded content: architettura del simulatore · 7 componenti\]

L'orchestratore legge le liste, fa giocare le partite al motore e salva ogni evento nel registro, compreso lo stato del cimitero. Gli agenti vedono solo le azioni legali e rispondono con una mossa, quindi si possono sostituire senza toccare il motore.

## Motore delle regole

Il cuore del progetto è un motore in C++ che implementa solo le regole e le carte di questo matchup, ottimizzato per velocità e per essere copiato rapidamente. Con agenti semplici l'obiettivo indicativo è nell'ordine delle centinaia di partite al secondo per core.

Componenti principali:

- **Stato di gioco**: zone (grimorio, mano, campo, cimitero, esilio, pila), punti vita, energia (per Guide of Souls), riserva di mana, turno, fase, step e priorità.
- **Cimitero tracciato con precisione**: contro Rakdos Delirium è la parte più importante. Servono in ogni momento il numero di carte, i tipi di carta presenti (delirium e forza di Nethergoyf) e il valore di mana totale (prove per Detective's Phoenix).
- **Generatore di azioni legali**: a ogni punto di priorità restituisce la lista delle mosse possibili. È l'unica interfaccia che gli agenti vedono.
- **Pila e trigger**: trigger di entrata e di morte, ordine dei trigger simultanei, risposte in pila come Bolt sul one-drop o Solitude sulla minaccia.
- **Combattimento completo**: attacchi, blocchi, volare e una fase di combattimento aggiuntiva, che Fear of Missing Out concede con il delirium.
- **Generatore casuale con seed**: ogni partita è riproducibile e si può rigiocare passo per passo.
- **Copia veloce dello stato**: indispensabile per la ricerca ad albero (MCTS).
- **Collegamento a Python** tramite pybind11, per orchestrare le partite e allenare le reti neurali.

Le regole più delicate sono il mulligan London, i costi alternativi (evocare Solitude, Force of Negation gratis, i costi alternativi dell'avversario), il ciclo (Street Wraith pagando vita), il warp di Quantum Riddler e Starfield Shepherd e l'esilio temporaneo di Phelia ed Ephemerate.

## Pool di carte

Servono circa 43 carte diverse, solo dai main dei due mazzi: 24 di UW Blink e 20 di Rakdos Delirium, con Arid Mesa in comune. Ogni carta si implementa come un piccolo modulo che dichiara le sue abilità e ha i suoi test.

Come lista di riferimento per Rakdos propongo la versione aggiornata con Stitcher's Supplier e Flare of Malice, [arrivata seconda in un torneo da 182 giocatori il 17 ottobre 2026](https://mtgtop8.com/event?e=92445&d=902225&f=MO):

- **Creature**: 4 Dark Matter Manipulator, 4 Detective's Phoenix, 4 Fear of Missing Out, 4 Moonshadow, 4 Nethergoyf, 3 Stitcher's Supplier, 4 Street Wraith, 1 Troll of Khazad-dûm
- **Magie**: 3 Flare of Malice, 4 Lightning Bolt, 4 Thoughtseize, 4 Mishra's Bauble
- **Terre**: 1 Arid Mesa, 3 Blood Crypt, 4 Bloodstained Mire, 4 Marsh Flats, 1 Mountain, 2 Raucous Theater, 2 Swamp, 1 Verdant Catacombs

Domanda aperta: la lista come riportata somma 61 carte, quindi un conteggio va verificato sulla fonte originale su melee.gg prima di fissarla.

Le carte più difficili da implementare, da affrontare per prime:

| Carta | Difficoltà | Cosa va gestito |
| --- | --- | --- |
| Fear of Missing Out | Alta | Scarta e pesca all'entrata; con il delirium, creatura stappata e fase di combattimento aggiuntiva |
| Detective's Phoenix | Alta | Conferire raccogliendo prove dal cimitero (valore 6), volare, ritorno quando la creatura incantata muore |
| Nethergoyf | Alta | Forza pari ai tipi di carta nel cimitero, rientro dal cimitero |
| Phelia, Exuberant Shepherd | Alta | Esilio fino alla fine del turno, ritorno sotto il tuo controllo, pescata se era dell'avversario |
| Flare of Malice | Media | Costo alternativo sacrificando una creatura nera non token; l'avversario sacrifica la sua creatura di valore di mana più alto |
| Dark Matter Manipulator e Stitcher's Supplier | Media | Macinare carte e aggiornare subito delirium e forze |
| Moonshadow | Media | Crescita legata alle carte che finiscono nel cimitero |
| Solitude | Media | Evocazione, flash, esilio con guadagno di vita |

Per la prima versione conviene semplificare la tua lista: sostituire temporaneamente Aang e Mockingbird con carte più semplici e reinserirle quando il motore è stabile.

## Agenti giocatori

La qualità dei dati dipende dalla qualità del gioco: se un agente sbaglia sempre la stessa giocata, il win rate del matchup lo riflette. Per questo gli agenti crescono per livelli, e ogni livello serve anche a controllare il precedente.

1. **Agente casuale**: sceglie una mossa legale a caso. Serve solo a scovare bug nel motore.
2. **Agente euristico**: regole scritte a mano, per esempio "usa Solitude sulla minaccia più grande" o "riempi il cimitero prima di attaccare con Nethergoyf". È velocissimo e trasparente, ma porta con sé le nostre convinzioni sul matchup.
3. **Ricerca ad albero con informazione nascosta (ISMCTS)**: a ogni decisione l'agente immagina molte volte la mano e il mazzo dell'avversario coerenti con quello che sa, gioca i futuri possibili e sceglie la mossa migliore in media. Gioca molto meglio, ma è centinaia di volte più lento.
4. **Rete neurale allenata in self-play con CUDA**: una rete politica e valore, in PyTorch sulla GPU, che impara giocando contro le sue versioni precedenti e guida la ricerca ad albero come fa AlphaZero.

In questo matchup l'informazione nascosta pesa ancora di più: Thoughtseize rivela la tua mano, e un buon agente deve tenerne conto quando immagina le mosse dell'avversario.

Il confronto tra livelli è un controllo di qualità: se il win rate cambia di molto passando dal livello 2 al 3, l'agente euristico stava distorcendo i dati.

Domanda aperta: quale GPU hai a disposizione? Determina quanto è grande la rete che conviene allenare e quanto tempo richiede il training.

## Quante partite servono

Solo la rete neurale ha bisogno di allenamento: l'agente euristico gioca come è programmato, e l'ISMCTS ragiona al momento, con una lungimiranza che dipende dalle simulazioni per mossa. Per la rete, su questo matchup ristretto, gli ordini di grandezza indicativi sono questi:

| Partite di self-play | Cosa impara la rete |
| --- | --- |
| 10.000-100.000 | Le basi: sviluppare il campo, attaccare, rimuovere le minacce evidenti |
| 100.000-1 milione | Le tempistiche: rimuovere le minacce prima che crescano, scegliere tra esilio e distruzione |
| 1-10 milioni | Le scelte lungimiranti: gestire il cimitero avversario nel tempo, giocare attorno a Flare of Malice, mulligan raffinati |

Per confronto, AlphaZero ha giocato circa 44 milioni di partite a scacchi. Qui il problema è più ristretto, ma informazione nascosta e casualità rendono ogni partita meno istruttiva. A 10 partite al secondo un milione di partite richiede circa 28 ore, a 1 partita al secondo circa 12 giorni.

Per accorciare il percorso, la rete parte imitando le partite giocate dall'agente ISMCTS e solo dopo passa al self-play. I progressi si misurano contro le versioni precedenti della rete, contro l'agente ISMCTS e con le posizioni test.

### Posizioni test

Sono posizioni preparate a mano in cui la giocata corretta è chiara. Si misura quante volte l'agente la sceglie: è il controllo più diretto sulla qualità delle sue decisioni.

| Posizione | Giocata corretta |
| --- | --- |
| Moonshadow appena lanciato, 2 mana disponibili, March e Solitude in mano | March con X=1: costa 2 mana e risparmia le due carte di Solitude evocata |
| Sanctifier en-Vec in mano al turno 2, cimitero avversario quasi vuoto | Giocarlo subito: da quel momento le loro carte nere e rosse vengono esiliate invece di finire nel cimitero |
| Avversario a un tipo di carta dal delirium, Thraben Charm in mano | Esiliare il loro cimitero: Nethergoyf si rimpicciolisce e il delirium si allontana |
| Teferi in mano con 3 mana, avversario con mana rosso aperto | Lanciare Teferi prima di attaccare: Bolt e Flare of Malice non si possono più usare nel tuo turno |
| Detective's Phoenix conferito su un Nethergoyf grande, Solitude in mano | Esiliare Nethergoyf: Phoenix resta in campo, ma come creatura piccola |

Un principio generale da verificare nei dati: contro questo mazzo esiliare è meglio che distruggere, perché ogni carta che finisce nel loro cimitero li aiuta. La lista va ampliata nel tempo con le situazioni che incontri giocando davvero il matchup.

## Mulligan e play/draw

Ogni simulazione è una singola partita di gara 1 con i soli mazzi di main. Le variabili strutturali si riducono a due: chi gioca per primo e come si gestisce il mulligan.

- **Play e draw**: alternati 50/50 e registrati separatamente. Contro un mazzo con dodici one-drop aggressivi, essere sul draw pesa molto.
- **Mulligan London**: all'inizio con regole fisse, poi sostituite da una rete che stima se conviene tenere 7 carte o scendere a 6. Una regola di partenza ragionevole per UW Blink: tieni se hai 2-4 terre, almeno una creatura economica e almeno una risposta per le loro minacce grandi (Solitude, March o Sanctifier). Le regole fisse si possono confrontare tra loro in test A/B.

La sideboard resta fuori dalla prima versione. Quando il motore sarà validato, il Bo3 si aggiungerà con un file di configurazione per matchup con gli in e out di entrambi i giocatori. Il primo piano da testare sarà quello già discusso: dentro le altre 2 copie di Sanctifier en-Vec ed eventualmente Rest in Peace, fuori Consign, Teferi e Mockingbird. Dall'altra parte andrà considerato Blood Moon, che spegne le tue fonti blu non base.

## Metriche stile 17lands

Ogni partita salva un registro degli eventi: mano iniziale, mulligan, ogni carta pescata con il turno, ogni carta giocata e su cosa, lo stato del cimitero avversario turno per turno e l'esito. Da questi registri si calcolano le metriche di 17lands, adattate al constructed.

| Metrica | Cosa misura | A cosa serve |
| --- | --- | --- |
| GIH WR | Win rate quando la carta è stata in mano, iniziale o pescata | Valore complessivo della carta nel matchup |
| OH WR | Win rate quando la carta era nella mano iniziale | Decisioni di mulligan |
| GD WR | Win rate quando la carta è stata pescata dopo | Valore come pescata tardiva |
| GNS WR | Win rate quando la carta non è mai stata vista | Base di confronto |
| IWD | Differenza tra GIH WR e GNS WR | Quanto la carta migliora la partita: la metrica chiave per il side |
| WR per turno del delirium | Win rate in base al turno in cui l'avversario raggiunge il delirium | Capire quanto conta rallentare il loro cimitero |
| Risposta alla minaccia grande | Quale carta gestisce Moonshadow, Nethergoyf o il Phoenix conferito, e con che esito | Valutare Solitude, March, Sanctifier e Thraben Charm |

Rispetto al Limited c'è una differenza da gestire: nel constructed le carte sono in 3-4 copie e molto correlate tra loro. L'IWD di Solitude, per esempio, va letto insieme a quello di Ephemerate, perché le due carte si potenziano a vicenda. Ogni valore va mostrato con il suo intervallo di confidenza e con il numero di partite su cui è calcolato.

I dati finiscono in file Parquet o in un database SQLite, analizzati in Python con pandas. Una dashboard interattiva può venire dopo, quando i numeri sono affidabili.

## Validazione

Un simulatore che sbaglia una regola o gioca male produce numeri precisi ma falsi, quindi ogni risultato va confrontato con la realtà prima di fidarsi.

- **Test di regole**: ogni interazione chiave ha un test automatico che deve passare sempre, prima di qualsiasi simulazione.
- **Goldfish**: Rakdos Delirium da solo, senza avversario, deve raggiungere il delirium e uccidere nei turni plausibili. Se il simulatore lo fa vincere molto prima o molto dopo di quanto succede nelle partite reali, c'è un problema.
- **Confronto con i dati di torneo**: Karsten ha misurato per Rakdos Delirium un [win rate complessivo del 57,1% ai cinque RC di inizio ottobre](https://magic.gg/news/metagame-mentor-modern-developments-at-five-2026-fall-regional-championships) e del [58,3% negli RC di fine ottobre](https://www.magic.gg/news/metagame-mentor-modern-developments-at-rcs-in-southeast-asia-australia-new-zealand-and-south-america). Sono dati contro tutto il field e includono le gare post-board, ma un win rate simulato molto lontano da questa forza è un campanello d'allarme.
- **Confronto tra livelli di agente**: se il win rate cambia molto tra agente euristico e ricerca ad albero, il dato dell'agente più debole non è affidabile.
- **Lettura dei registri**: ogni settimana leggi a campione qualche partita completa in formato testo, come un replay. Il tuo occhio da giocatore trova errori di gioco che nessuna statistica mostra.

## Roadmap

&#91;embedded content: roadmap · 5 fasi, 3 controlli\]

Ogni rombo è un controllo da superare prima di passare alla fase successiva. Già la fase 1 è utile da sola: il goldfish dice in che turno Rakdos raggiunge il delirium e quanto velocemente può uccidere senza interazione. Sideboard e Bo3 arrivano dopo la fase 4, come estensione del motore già validato.

## Rischi e limiti

| Rischio | Impatto | Come mitigarlo |
| --- | --- | --- |
| Agenti che giocano male | Win rate distorti in modo sistematico | Livelli di agente confrontati, lettura dei registri |
| Errori nelle regole | Risultati falsi su interazioni chiave | Test automatici per ogni carta e interazione |
| Errori nel tracciamento del cimitero | Delirium, Nethergoyf e Phoenix sbagliati falsano tutto il matchup | Controllo del cimitero dopo ogni azione nei test |
| Lista di Rakdos in evoluzione | Il mazzo è nuovo e cambia ogni settimana | Liste in file di configurazione, aggiornabili senza toccare il motore |
| Annuncio B&R del 30 novembre 2026 | Un ban può cambiare il meta e le liste | Ricontrollare liste e banlist dopo l'annuncio |
| Solo gara 1 | I dati non dicono nulla sul post-board | Aggiungere sideboard e Bo3 come fase successiva |
| Tempo di sviluppo | Progetto di mesi | Fasi con obiettivi intermedi già utili da soli |

Il rischio principale resta il primo: il simulatore misura come giocano i suoi agenti, non come giocheresti tu. I dati diventano davvero affidabili solo dalla fase della ricerca ad albero in poi.

## Fonti

- [Metagame Mentor: Modern Developments at RCs in Southeast Asia, Australia/New Zealand, and South America](https://www.magic.gg/news/metagame-mentor-modern-developments-at-rcs-in-southeast-asia-australia-new-zealand-and-south-america), Frank Karsten, 29 ottobre 2026
- [Metagame Mentor: Modern Developments at Five 2026 Fall Regional Championships](https://magic.gg/news/metagame-mentor-modern-developments-at-five-2026-fall-regional-championships), Frank Karsten, ottobre 2026
- [Lista Rakdos Delirium di Moonsteak, secondo posto il 17 ottobre 2026](https://mtgtop8.com/event?e=92445&d=902225&f=MO), MTGTop8
- [Rakdos Delirium in Modern: Deck Tech & Sideboard Guide](https://mtgdecks.net/guides/rakdos-delirium-in-modern-deck-tech-sideboard-guide-mtg-432), Lucas Giggs, MTGDecks, ottobre 2026
- [Testo Oracle di Flare of Malice](https://grimoria.app/cards/flare-of-malice)
