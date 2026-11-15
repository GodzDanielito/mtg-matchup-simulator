# Ultimo resoconto: registro delle ipotesi di Daniele, D-1 allineata, regole degli agenti (15/11/2026)

Resoconto completo dell'ultimo passo, sovrascritto a ogni passo (la storia è in [diario.md](diario.md) e in Git). Passo di sola documentazione: **nessun agente, test o registro cambia**, nessuna misura rifatta.

## In breve

- **Nuovo [docs/ipotesi_daniele.md](ipotesi_daniele.md):** il registro delle regole degli agenti euristici, trattate come convinzioni del pilota e non come verità. 18 risposte D di UW, 6 varianti di UW (T1, W1, R1, M1, M1-R, M3), 14 regole di Rakdos (mulligan, X1-C, X2, orologio contro Teferi, ordine di Thoughtseize, X5, correzione del turno 1, X4, X6, Bolt a vista, A-4, soglia di vita).
- **Divergenze registrate:** M1 +1,18 [+1,04; +1,31] contro base_r3 contro la linea del pilota; X1-C (+1,79) adottata dopo la misura.
- **D-1 allineata all'agente:** "serve alla curva o al piano" include istantanee e creature con lampo. **Interpretazione provvisoria da confermare con Daniele.**
- **CLAUDE.md:** nuova sezione "Regole degli agenti" e URL del resoconto in fondo a ogni resoconto.

## Fonti nel registro

| Fonte | Righe |
| --- | --- |
| Daniele | D-1...D-5, D-8, D-11, D-12, D-14, D-15, D-18 (risposte del 10/11/2026), W1, X1-C (ipotesi), X5 (linea guida), la decisione su M1 |
| Claude e approvata da Daniele | T1, R1, correzione del turno 1, X6, D-10 (come regola di partenza) |
| Claude (non adottate o mai riviste) | M1-R, M3, X2, X4, R-BOLT, A-4 |
| Da verificare con Daniele | mulligan base di Rakdos, orologio contro Teferi, ordine di Thoughtseize di base_r1, soglia di vita 5, i numeri della soglia di UW (D-16), la classifica delle minacce (D-6) |

## Stato delle regole

| Stato | Regole |
| --- | --- |
| Decise | D-1 (tranne l'interpretazione), D-2, D-3, D-4, D-5, D-8, D-11, D-12, D-14, D-15, D-17, D-18, T1, W1, R1, correzione del turno 1 |
| Provvisorie | interpretazione di D-1, X1-C, X5 (riga c), X6, soglia dell'orologio, R-MUL, R-BOLT, R-SOGLIA |
| Da far decidere all'albero | D-6, D-7, D-9, D-10, D-13, D-16, A-4 |
| Non adottate | M1 (spenta contro la misura), M1-R, M3, X2, X3 (soglie diverse), X4 |

## Come l'albero le verifica

Per ogni regola, con gli stessi seed: l'**accordo** con la scelta dell'albero nelle posizioni in cui la regola si applica, e la **differenza di win rate tra albero libero e regola forzata**, appaiata, con l'intervallo al 95%, anche sulle sole partite in cui l'albero sceglie diversamente. L'albero va misurato **contro più versioni di avversario** (UW base_v1, base_v2, base_v2 + M1; Rakdos base_r1, base_r2, base_r3): una regola confermata deve avere lo stesso segno contro tutte. La tabella per regola del registro è vuota finché l'albero non esiste.

## Modifiche ai file

| File | Modifica |
| --- | --- |
| `docs/ipotesi_daniele.md` | Nuovo |
| `docs/agente_uw.md` | Riga D-1, sezione 3 punto 4, nota nella sezione 29 |
| `CLAUDE.md` | Sezione "Regole degli agenti"; l'URL del resoconto |
| `docs/diario.md`, `docs/ultimo_resoconto.md` | Voce del passo; questo resoconto |

**Resoconto su GitHub:** https://github.com/GodzDanielito/mtg-matchup-simulator/blob/main/docs/ultimo_resoconto.md
