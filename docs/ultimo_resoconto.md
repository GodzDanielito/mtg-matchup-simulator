# Ultimo resoconto: fase 3, passo 2b delle metriche, ablazione (16/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). **Nessun agente cambiato, motore non toccato**: i registri della sezione 1 di [metriche.md](metriche.md) sono identici byte per byte. Commento completo nella sezione 17 di [metriche.md](metriche.md); tabelle generate in [risultati_v1.md](risultati_v1.md), sezione "Ablazione".

## In breve

- **`metriche.exe --sostituisci "Carta:Terra"`**: una copia sostituita da una terra base **nella stessa posizione della lista**, quindi lo stesso seed mescola gli stessi oggetti nello stesso ordine. I tre test chiesti passano: (a) opzione spenta, registri identici; (b) "Plains:Plains" e "Swamp:Swamp" danno **lo stesso MD5 della base su 100.000 seed** (differenza esattamente zero); (c) la prima differenza tra i registri è l'arrivo in mano della copia sostituita.
- **28 carte, 11 coppie, 4.000.000 di partite in 11 minuti** (8 processi per esecuzione; la stima era 35 minuti su un core).
- **L'ablazione e l'IWD ordinano le carte quasi nello stesso modo dentro un mazzo** (Spearman al turno 5: UW +0,92, Rakdos +0,95). **Tra i due mazzi l'IWD classico non dice niente (+0,11), quello al turno 5 sì (+0,83).** Nessuno scarto di rango grande; 7 carte con il segno diverso, che diventano 3, tutte a meno di 0,2 punti dalla media, togliendo a tutte e due le misure la media del mazzo.
- **Per questi agenti tre carte di UW valgono meno di una Plains:** Force of Negation −1,36, Consign to Memory −1,13, Witch Enchanter −0,54, le stesse che l'agente tiene in mano. In testa Sanctifier en-Vec +6,25. Tutte le carte di Rakdos valgono più di una Swamp (da +0,80 a +4,11).
- **Le interazioni di coppia sono piccole e non seguono la sinergia osservazionale:** da −0,65 a +0,49 punti contro sinergie al turno 5 da −7,6 a +13,1. Thraben Charm + Sanctifier, la più negativa di UW (−6,2), ha l'unica interazione positiva chiara (+0,49). La tua nota sull'artefatto di selezione è confermata.
- **Recruiter + Solitude:** con Solitude "per qualsiasi via" la sinergia al turno 5 è +0,8 [−1,2; +2,9], l'interazione causale +0,01 [−0,15; +0,19]. Ma l'ablazione toglie una Solitude su quattro: Recruiter ne trova ancora una, quindi **D-2c non è ancora misurata** (passo 4).
- **Un errore trovato:** il controllo delle copie del record sommava male le voci ripetute di una lista; invisibile con le liste vere, corretto partendo da un test.

## 1. Cosa ho fatto

| Passo | Cosa | Tempo |
| --- | --- | --- |
| Test C++ prima del codice | `tests/test_ablazione.cpp`, 8 test case: la stringa "Carta:Terra", la copia nella stessa posizione, stesso grimorio a parte la copia, il mazzo giusto e i prefissi, (a), (b), (c), il controllo delle copie con voci ripetute. Con lo stub 7 rossi (il test (a) passa già: è una guardia) | — |
| `metriche.exe` | `--sostituisci` (ripetibile, nome Oracle o prefisso unico) e `--senza-record` | — |
| Test Python prima del codice | `test_ablazione.py` (effetti, play/draw, IC normale, patte, bootstrap, interazione, Spearman, scelta delle coppie, disaccordi, analisi su disco), `turno_ipc` in `test_converti.py`, variante "per qualsiasi via" in `test_metriche_carte.py`: 15 rossi con lo stub, poi verdi | 1,7 s (75 test) |
| `ablazione.py gioca` | 28 carte + controllo, poi 11 coppie; 8 processi da 12.500 seed per esecuzione | 485 s + 186 s |
| `converti.py` | Rifatto per la colonna `turno_ipc` | 84 s (con le partite in parallelo) |
| `metriche_carte.py` | Metriche, variante di Recruiter + Solitude, analisi dell'ablazione con la stessa matrice di pesi del bootstrap, `docs/risultati_v1.md` | 43 s (ablazione 17 s) |
| Verifiche | Suite C++ in Release; tutti gli MD5 della sezione 1 e di base_r1 | 537 test passati, 12 s |

## 2. Controlli

| Controllo | Esito |
| --- | --- |
| (a) Opzione spenta | `metriche.exe` da 10.000: `partite.jsonl` `2302d021...`, `record.jsonl` `472f0196...`; con `--senza-record` lo stesso `partite.jsonl`. Test: stesse liste e stessi registri |
| (b) Terra al posto di se stessa | Test: 30 seed, registri identici voce per voce. 100.000 seed: `partite.jsonl` `0ad6744d...` = base, **differenza zero su tutti i seed** |
| (c) Prima divergenza | Solitude, Ocelot Pride, Thoughtseize, Nethergoyf, 40 seed ciascuna: quando la copia arriva in mano la prima voce diversa è quell'arrivo e cambia solo la definizione della carta; macinata o sorvegliata, la prima differenza è quello spostamento; mai toccata, partite identiche. Prima di allora solo decisioni del proprietario che guardano il grimorio: 3 partite di Rakdos su 80, sempre una fetch giocata perché nel grimorio c'è una Swamp in più |
| Esecuzioni | 28 + 11 + 1, 100.000 seed ciascuna, gli stessi della base |
| Bootstrap contro formula normale appaiata | 84 intervalli: scarto massimo 0,030 punti, medio 0,008 (soglia 0,05) |
| Controlli del passo 2 | Partizione, copie, partita per partita con `matchup.exe`, carte morte, bootstrap dell'IWD (0,049): tutti OK |
| Registri della sezione 1 | Matchup `2302d021...`/`a9f2908d...` e `0ad6744d...`/`9c13ada9...`, goldfish `63648b4c...`/`eb4afb40...` e `3fc4b26d...`, base_r1 `81c39eba...`/`2b4dded8...`: identici |

## 3. Effetto di ogni carta

WR del giocatore che possiede la carta con la lista vera meno con una copia sostituita (Plains per UW, Swamp per Rakdos), in punti, IC al 95% dal bootstrap sui seed; accanto l'IC dalla formula normale appaiata. "Esiti diversi": seed in cui cambia il vincitore.

### UW

| Carta | Copie | Effetto | IC normale | In play | In draw | Esiti diversi | IWD T5 | IWD classico |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Consign to Memory | 3 | -1,13 [-1,31; -0,93] | [-1,32; -0,94] | -1,44 [-1,71; -1,16] | -0,82 [-1,07; -0,57] | 9.155 (9,2%) | -5,3 | +6,8 |
| Ephemerate | 3 | +0,33 [+0,14; +0,52] | [+0,15; +0,52] | +0,25 [-0,02; +0,52] | +0,42 [+0,16; +0,66] | 9.222 (9,2%) | +2,8 | +11,2 |
| Force of Negation | 1 | -1,36 [-1,55; -1,17] | [-1,54; -1,17] | -1,67 [-1,96; -1,40] | -1,04 [-1,30; -0,79] | 8.824 (8,8%) | -5,4 | +5,5 |
| Guide of Souls | 4 | +2,05 [+1,84; +2,26] | [+1,84; +2,25] | +2,31 [+2,02; +2,61] | +1,78 [+1,51; +2,05] | 10.773 (10,8%) | +15,5 | +31,2 |
| March | 2 | +0,47 [+0,28; +0,68] | [+0,27; +0,68] | +0,21 [-0,08; +0,52] | +0,74 [+0,45; +1,00] | 10.695 (10,7%) | -0,4 | +8,5 |
| Ocelot Pride | 4 | +2,58 [+2,38; +2,77] | [+2,38; +2,78] | +2,98 [+2,69; +3,27] | +2,18 [+1,91; +2,44] | 10.781 (10,8%) | +18,2 | +33,8 |
| Phelia | 4 | -0,00 [-0,19; +0,18] | [-0,19; +0,19] | -0,04 [-0,32; +0,22] | +0,03 [-0,22; +0,29] | 9.344 (9,3%) | +1,2 | +10,0 |
| Quantum Riddler | 4 | +0,96 [+0,75; +1,16] | [+0,76; +1,15] | +1,11 [+0,81; +1,40] | +0,80 [+0,54; +1,07] | 10.198 (10,2%) | +7,2 | +11,7 |
| Recruiter | 1 | +1,69 [+1,50; +1,87] | [+1,49; +1,88] | +2,05 [+1,75; +2,33] | +1,32 [+1,07; +1,58] | 10.156 (10,2%) | +9,2 | +17,3 |
| Sanctifier en-Vec | 1 | +6,25 [+6,02; +6,48] | [+6,02; +6,49] | +7,23 [+6,90; +7,57] | +5,28 [+4,98; +5,59] | 14.378 (14,4%) | +17,3 | +27,0 |
| Seam Rip | 1 | +1,99 [+1,78; +2,20] | [+1,78; +2,20] | +1,53 [+1,23; +1,83] | +2,45 [+2,16; +2,73] | 11.221 (11,2%) | +4,6 | +13,6 |
| Solitude | 4 | +0,65 [+0,44; +0,85] | [+0,44; +0,86] | +0,30 [+0,00; +0,61] | +1,00 [+0,69; +1,27] | 11.116 (11,1%) | -3,7 | +11,4 |
| Starfield Shepherd | 3 | +1,52 [+1,32; +1,71] | [+1,32; +1,72] | +1,57 [+1,26; +1,86] | +1,47 [+1,20; +1,73] | 10.228 (10,2%) | +9,0 | +19,1 |
| Teferi | 1 | +1,02 [+0,82; +1,22] | [+0,82; +1,23] | +1,14 [+0,83; +1,44] | +0,91 [+0,65; +1,18] | 11.078 (11,1%) | +2,4 | +9,9 |
| Thraben Charm | 1 | +0,18 [-0,01; +0,37] | [-0,01; +0,38] | +0,01 [-0,27; +0,29] | +0,35 [+0,09; +0,61] | 9.841 (9,8%) | -1,9 | +8,8 |
| Witch Enchanter | 4 | -0,54 [-0,71; -0,37] | [-0,71; -0,37] | -0,60 [-0,85; -0,35] | -0,48 [-0,72; -0,24] | 7.658 (7,7%) | -4,0 | +6,1 |

### Rakdos

| Carta | Copie | Effetto | IC normale | In play | In draw | Esiti diversi | IWD T5 | IWD classico |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Dark Matter Manipulator | 4 | +4,11 [+3,88; +4,36] | [+3,88; +4,35] | +4,09 [+3,76; +4,42] | +4,14 [+3,80; +4,48] | 14.780 (14,8%) | +11,5 | +9,7 |
| Detective's Phoenix | 4 | +2,08 [+1,86; +2,31] | [+1,86; +2,30] | +2,06 [+1,74; +2,38] | +2,10 [+1,78; +2,42] | 12.855 (12,9%) | +4,3 | +3,9 |
| Fear of Missing Out | 4 | +3,62 [+3,39; +3,84] | [+3,39; +3,85] | +3,53 [+3,21; +3,87] | +3,70 [+3,37; +4,03] | 14.088 (14,1%) | +10,4 | +8,1 |
| Moonshadow | 4 | +2,43 [+2,19; +2,66] | [+2,20; +2,65] | +2,39 [+2,07; +2,71] | +2,46 [+2,13; +2,77] | 13.698 (13,7%) | +4,6 | -0,5 |
| Nethergoyf | 4 | +3,81 [+3,58; +4,05] | [+3,58; +4,05] | +3,66 [+3,32; +4,01] | +3,96 [+3,63; +4,30] | 14.323 (14,3%) | +8,6 | +5,7 |
| Stitcher's Supplier | 3 | +1,33 [+1,09; +1,57] | [+1,09; +1,57] | +1,18 [+0,85; +1,52] | +1,47 [+1,12; +1,82] | 14.621 (14,6%) | +1,6 | -2,6 |
| Street Wraith | 4 | +1,81 [+1,59; +2,04] | [+1,58; +2,04] | +1,89 [+1,56; +2,22] | +1,73 [+1,38; +2,06] | 14.090 (14,1%) | +2,2 | -2,9 |
| Troll of Khazad-dûm | 1 | +1,45 [+1,23; +1,68] | [+1,23; +1,68] | +1,31 [+0,99; +1,63] | +1,60 [+1,29; +1,93] | 13.066 (13,1%) | -0,6 | -4,6 |
| Flare of Malice | 4 | +0,80 [+0,58; +1,03] | [+0,58; +1,03] | +0,61 [+0,29; +0,93] | +1,00 [+0,69; +1,31] | 12.827 (12,8%) | -2,8 | -5,8 |
| Lightning Bolt | 3 | +2,10 [+1,87; +2,33] | [+1,87; +2,32] | +1,73 [+1,43; +2,06] | +2,46 [+2,13; +2,78] | 13.159 (13,2%) | +3,0 | -0,0 |
| Thoughtseize | 4 | +0,98 [+0,77; +1,22] | [+0,75; +1,21] | +1,14 [+0,83; +1,46] | +0,83 [+0,50; +1,18] | 13.911 (13,9%) | -0,4 | -7,9 |
| Mishra's Bauble | 4 | +2,35 [+2,12; +2,59] | [+2,12; +2,58] | +2,23 [+1,92; +2,57] | +2,47 [+2,14; +2,80] | 13.526 (13,5%) | +3,5 | +0,5 |

## 4. Medie per mazzo: IWD e ablazione (richiesta 6)

In punti. IWD: media delle carte non terra pesata per le partite del gruppo "vista"; ablazione: media semplice delle carte.

| Metrica | UW | Rakdos | UW − Rakdos |
| --- | --- | --- | --- |
| IWD classico | +15,12 [+14,94; +15,31] | +0,68 [+0,49; +0,88] | +14,44 [+14,15; +14,74] |
| IWD relativo | 0 per costruzione | 0 per costruzione | 0 |
| IWD T3 | +2,00 [+1,87; +2,12] | +2,63 [+2,49; +2,77] | −0,63 [−0,81; −0,45] |
| IWD T5 | +4,39 [+4,24; +4,54] | +4,18 [+4,01; +4,34] | +0,21 [−0,00; +0,44] |
| IWD T7 | +6,16 [+5,94; +6,41] | +3,74 [+3,47; +4,02] | +2,42 [+2,07; +2,79] |
| Ablazione | +1,04 [+0,92; +1,16] | +2,24 [+2,09; +2,39] | — |

L'IWD classico mette UW molto sopra Rakdos (lunghezza della partita); quello a turno fisso li mette alla pari; l'ablazione mette Rakdos sopra: per questi agenti una carta media di Rakdos vale più di una Swamp di quanto una carta media di UW valga più di una Plains.

## 5. Ablazione di coppia

Interazione = effetto(A e B) − effetto(A) − effetto(B), in punti.

| Mazzo | A | B | Scelta | Sinergia T5 (osservazionale) | Effetto A | Effetto B | Effetto A e B | Interazione | IC normale | In play | In draw |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UW | Guide of Souls | Ocelot Pride | più forte | +13,1 [+11,7; +14,6] | +2,05 | +2,58 | +4,39 [+4,15; +4,65] | -0,24 [-0,42; -0,05] | [-0,42; -0,05] | -0,30 [-0,57; -0,02] | -0,17 [-0,43; +0,08] |
| Rakdos | Flare of Malice | Stitcher's Supplier | più forte | +11,0 [+9,7; +12,4] | +0,80 | +1,33 | +1,48 [+1,22; +1,75] | -0,65 [-0,87; -0,43] | [-0,87; -0,43] | -0,56 [-0,85; -0,26] | -0,74 [-1,07; -0,43] |
| UW | Quantum Riddler | Consign to Memory | più forte | +9,5 [+8,2; +10,8] | +0,96 | -1,13 | -0,42 [-0,65; -0,19] | -0,25 [-0,41; -0,08] | [-0,42; -0,08] | -0,30 [-0,56; -0,05] | -0,20 [-0,43; +0,02] |
| UW | Guide of Souls | Starfield Shepherd | più forte | +7,9 [+6,4; +9,4] | +2,05 | +1,52 | +3,51 [+3,27; +3,76] | -0,06 [-0,24; +0,13] | [-0,24; +0,13] | -0,03 [-0,30; +0,23] | -0,08 [-0,31; +0,17] |
| UW | Solitude | Ephemerate | più forte, fissa | +7,5 [+5,9; +9,1] | +0,65 | +0,33 | +0,76 [+0,52; +0,99] | -0,23 [-0,40; -0,05] | [-0,40; -0,05] | -0,21 [-0,47; +0,04] | -0,25 [-0,48; -0,01] |
| Rakdos | Detective's Phoenix | Nethergoyf | più negativa | -7,6 [-9,3; -5,9] | +2,08 | +3,81 | +5,73 [+5,46; +6,01] | -0,16 [-0,39; +0,05] | [-0,38; +0,06] | -0,03 [-0,34; +0,28] | -0,29 [-0,61; +0,02] |
| UW | Thraben Charm | Sanctifier en-Vec | più negativa | -6,2 [-8,3; -4,1] | +0,18 | +6,25 | +6,93 [+6,66; +7,19] | +0,49 [+0,31; +0,68] | [+0,30; +0,69] | +0,54 [+0,26; +0,81] | +0,45 [+0,19; +0,73] |
| Rakdos | Nethergoyf | Dark Matter Manipulator | più negativa | -5,2 [-6,7; -3,7] | +3,81 | +4,11 | +7,76 [+7,48; +8,05] | -0,16 [-0,38; +0,06] | [-0,39; +0,07] | -0,06 [-0,39; +0,25] | -0,26 [-0,60; +0,06] |
| UW | Solitude | Starfield Shepherd | più negativa | -4,9 [-6,4; -3,4] | +0,65 | +1,52 | +2,20 [+1,95; +2,45] | +0,03 [-0,15; +0,21] | [-0,15; +0,21] | +0,16 [-0,11; +0,41] | -0,09 [-0,33; +0,15] |
| Rakdos | Dark Matter Manipulator | Fear of Missing Out | più negativa | -4,8 [-6,2; -3,3] | +4,11 | +3,62 | +7,51 [+7,23; +7,80] | -0,21 [-0,44; +0,00] | [-0,44; +0,01] | -0,12 [-0,45; +0,20] | -0,31 [-0,63; +0,02] |
| UW | Recruiter | Solitude | fissa | +0,0 [-2,0; +2,1] | +1,69 | +0,65 | +2,35 [+2,11; +2,59] | +0,01 [-0,15; +0,19] | [-0,17; +0,19] | +0,07 [-0,19; +0,35] | -0,05 [-0,29; +0,20] |

Come si legge: le cinque sinergie osservazionali più forti hanno interazioni causali negative o nulle (rendimenti decrescenti: due carte che fanno cose simili, tolte insieme, costano un po' meno della somma); delle cinque più negative quattro sono nulle e una (Thraben Charm + Sanctifier) è positiva: senza Sanctifier, Thraben Charm conta di più. Sono 11 coppie e non 12 perché Solitude + Ephemerate è anche tra le cinque più forti.

**Recruiter + Solitude per qualsiasi via (la tua nota):**

| Variante | n entrambe / solo Recruiter / solo Solitude / nessuna | WR entrambe | Sinergia classica | T3 | T5 | T7 |
| --- | --- | --- | --- | --- | --- | --- |
| Solitude GIH (passo 2) | 15.429 / 4.220 / 55.147 / 23.554 | 54,2 | +11,1 [+9,3; +12,8] | −1,7 [−3,5; +0,2] | 0,0 [−2,0; +2,1] | +4,5 [+1,5; +7,4] |
| Solitude per I, P o C | 17.079 / 4.220 / 55.147 / 23.554 | 54,9 | +11,8 [+10,0; +13,4] | −1,6 [−3,5; +0,2] | +0,8 [−1,2; +2,9] | +5,1 [+2,2; +8,0] |

Le 1.650 partite con Solitude solo cercata passano nella cella "entrambe" e la sinergia al turno 5 resta nulla. L'ablazione di coppia (+0,01) toglie Recruiter e una Solitude su quattro: con tre Solitude nel grimorio Recruiter ne trova ancora una, quindi non misura "Recruiter senza Solitude". **D-2c resta aperta** per il passo 4 (dove Recruiter cerca Solitude, e quando la Solitude cercata viene usata).

## 6. Ablazione contro IWD

Spearman tra effetto e IWD, intervallo dal bootstrap congiunto (gli stessi ricampionamenti dei seed per le due misure).

| Metrica | UW (16 carte) | Rakdos (12 carte) | Tutte (28 carte) |
| --- | --- | --- | --- |
| IWD classico | +0,92 [+0,89; +0,95] | +0,90 [+0,89; +0,95] | +0,11 [+0,03; +0,21] |
| IWD T3 | +0,98 [+0,94; +0,99] | +0,94 [+0,90; +0,97] | +0,82 [+0,78; +0,87] |
| IWD T5 | +0,92 [+0,89; +0,94] | +0,95 [+0,92; +0,98] | +0,83 [+0,78; +0,87] |
| IWD T7 | +0,74 [+0,68; +0,76] | +0,82 [+0,67; +0,89] | +0,62 [+0,54; +0,66] |

Segno uguale per 21 carte su 28; tolta a tutte e due la media del mazzo (ablazione meno la media, IWD T5 relativo), **25 su 28**, e le tre restanti (Quantum Riddler, Detective's Phoenix, Mishra's Bauble) sono a meno di 0,2 punti dalla media del mazzo. L'IWD T5 vale circa 3,7-4 volte l'effetto di una copia (pendenza: UW 3,73, Rakdos 3,95) più uno scarto positivo comune a tutte le carte.

**Carte in disaccordo con l'IWD T5** (segno diverso o rango lontano almeno 5 posizioni per UW, 4 per Rakdos: nessuna per il rango):

| Mazzo | Carta | Ablazione | IWD T5 | Rango ablazione / IWD T5 | Segno diverso | Segno diverso, relativi | In mano alla fine | Solo cercata n | IWD classico − T5 | Copie | Spiegazione più probabile |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UW | Solitude | +0,65 | -3,7 | 9 / 13 | sì | no | 10,4% | 1.650 | +15,1 | 4 | Selezione del mulligan (D-2 tiene ogni mano con Solitude) e partite già decise prima del turno 5 (lancio mediano al turno 2); l'ablazione toglie la quarta copia |
| UW | March | +0,47 | -0,4 | 10 / 11 | sì | no | 8,7% | 0 | +8,9 | 2 | Scala: IWD nell'IC dello zero, tutte e due sotto la media del mazzo |
| UW | Phelia | -0,00 | +1,2 | 13 / 10 | sì | no | 27,6% | 34 | +8,9 | 4 | Agente passivo (in mano nel 27,6% delle copie viste) e scala |
| UW | Thraben Charm | +0,18 | -1,9 | 12 / 12 | sì | no | 13,4% | 0 | +10,7 | 1 | Scala; effetto nell'IC dello zero, una copia |
| Rakdos | Troll of Khazad-dûm | +1,45 | -0,6 | 9 / 11 | sì | no | 5,4% | 0 | -4,0 | 1 | Terra aggiunta e valore indiretto: si cicla per una Swamp (la Swamp che lo sostituisce non aggiunge una terra) e mette una creatura nel cimitero (delirium, Nethergoyf), un valore che va ad altre carte |
| Rakdos | Flare of Malice | +0,80 | -2,8 | 12 / 12 | sì | no | 28,0% | 0 | -3,1 | 4 | Agente passivo (in mano nel 28,0%) e selezione: vista entro il turno 5 senza poterla lanciare vuol dire una mano più lenta |
| Rakdos | Thoughtseize | +0,98 | -0,4 | 11 / 10 | sì | no | 4,9% | 0 | -7,4 | 4 | Partite già decise prima del turno 5 (il valore arriva al turno 1-2) e lunghezza residua |

Nessuna delle 7 è spiegata dalla lunghezza della partita da sola (il turno fisso la toglie già); "carta cercata" conta solo per Solitude.

## 7. Limiti (sezione 17 di metriche.md)

- **L'ablazione aggiunge una terra**: misura la carta contro una terra base in più, non contro una carta media; con una terra in più cambiano anche le mani, e per Rakdos la Swamp in più è anche un bersaglio delle fetch e del Troll.
- **Gli agenti sono tarati sulle liste attuali**: soglie del mulligan, D-2 e le regole che contano le copie restano uguali con una carta in meno.
- **Una copia, non la carta**: per le carte in più copie è il valore dell'ultima copia; per quelle da una copia la carta sparisce. Confronto a livello di carta, non di lista ottimale.
- **Precisione**: IC circa ±0,2 punti (±0,3 in play o in draw), più stretto della stima (±0,5-0,6), perché le due partite dello stesso seed coincidono finché la copia non conta: il vincitore cambia solo nel 7,7-14,8% dei seed.

## 8. Decisioni da approvare

| # | Proposta | Perché |
| --- | --- | --- |
| 1 | **Coppie per l'ablazione scelte tra tutte le coppie dei due mazzi** (decise e della matrice) per sinergia al turno 5 | La richiesta diceva "le 5 più forti e le 5 più negative": ho preso le più estreme in assoluto, comprese due coppie decise prima |
| 2 | **Scarto di rango "grande" = un terzo delle carte del mazzo** (5 per UW, 4 per Rakdos) | Serviva una soglia; con questa nessuna carta è in disaccordo per il rango |
| 3 | **Il confronto dei segni anche "relativo"** (ablazione meno la media del mazzo contro l'IWD T5 relativo), accanto a quello semplice | L'IWD T5 ha uno scarto comune a tutte le carte e una scala circa quattro volte quella di una copia: senza, i 7 segni diversi sembrerebbero disaccordi veri |
| 4 | **La copia sostituita è l'ultima della sua voce** | Qualsiasi copia andrebbe bene (il mescolamento è casuale); l'ultima lascia invariate le posizioni delle altre |

Restano da approvare, non toccate, le tre definizioni del passo 2 (vista entro T comprende tutto il turno T; IWD relativo pesato per il gruppo "vista"; p della matrice dall'approssimazione normale).

## 9. Tempi

| Passo | Tempo |
| --- | --- |
| Ablazione delle 28 carte e controllo (29 esecuzioni da 100.000 seed, 8 processi) | 485 s (8,1 minuti) |
| Ablazione delle 11 coppie | 186 s (3,1 minuti) |
| In tutto, 4.000.000 di partite | 672 s, circa 6.000 partite al secondo (stima: 35 minuti su un core) |
| `converti.py` rifatto (con le partite in parallelo) | 84 s |
| `metriche_carte.py` con l'analisi dell'ablazione | 43 s (ablazione 17 s) |
| Test Python (75) | 1,7 s |
| Suite C++ in Release (537) | 12 s |

La prima esecuzione di Recruiter + Solitude ha riportato 2.299 s di orologio (le altre 13-19 s), come `carte_morte.exe` al passo 2: probabilmente la macchina sospesa. Rifatta in 13,0 s, stesso MD5.

---

https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
