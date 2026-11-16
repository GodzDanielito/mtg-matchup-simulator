# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "numpy==2.5.5",
#   "pandas==3.1.1",
#   "pyarrow==26.0.1",
#   "statsmodels==0.15.0",
# ]
# ///
"""Ablazione: il valore causale di una carta per gli agenti euristici v1 (docs/metriche.md, sezione 3, aggiunta A;
passo 2b della sezione 14).

Per ogni carta non terra si gioca la stessa serie di seed con UNA copia sostituita da una terra base (Plains per UW,
Swamp per Rakdos) nella stessa posizione della lista (`metriche.exe --sostituisci "Carta:Terra"`): lo stesso seed
mescola gli stessi oggetti nello stesso ordine, e le due partite coincidono finche' la copia non conta. L'effetto
della carta e' la differenza appaiata di win rate del giocatore che la possiede, lista vera meno lista con la copia
tolta, con l'intervallo al 95% (bootstrap sui seed con la stessa matrice di pesi delle metriche per carta, e formula
normale appaiata), in play e in draw separati. Per una coppia (A, B), con entrambe sostituite:
interazione = effetto(A e B) - effetto(A) - effetto(B), per seed w(A) + w(B) - w(A e B) - w(base).

Uso:
    uv run --no-project python/metriche/ablazione.py gioca [--carte] [--coppie] [--controlli]
        [--exe build/Release/metriche.exe] [--processi 8] [--games 100000] [--dati data/metriche/v1] [--solo NOME,...]

`gioca --coppie` sceglie le coppie da `coppie.csv` di metriche_carte.py (le 5 con la sinergia al turno 5 piu' forte
e le 5 piu' negativa, dai due mazzi insieme, piu' Recruiter + Solitude e Solitude + Ephemerate). Le partite vanno in
`DATI/ablazione/<cartella>/partite.jsonl` (esclusi da Git), l'elenco delle esecuzioni in
`DATI/ablazione/esecuzioni.json`. L'analisi la fa metriche_carte.py, che scrive le tabelle in docs/risultati_v1.md
quando trova la cartella dell'ablazione.
"""

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
import time
import unicodedata
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import statistica as st  # noqa: E402

ETICHETTA = {"uw": "UW", "rakdos": "Rakdos"}
TERRA = {"uw": "Plains", "rakdos": "Swamp"}
LATI = ("", "_play", "_draw")
TURNO = 5
N_COPPIE = 5
# Coppie sempre giocate (richiesta di Daniele del 16/11/2026).
FISSE = [("uw", "Recruiter of the Guard", "Solitude"), ("uw", "Solitude", "Ephemerate")]
RADICE = Path(__file__).resolve().parents[2]


# --- Statistiche ---------------------------------------------------------------------------------------------

def _ordina(df):
    return df.sort_values("seed").reset_index(drop=True)


def _vittorie(df, chi):
    return (df["vincitore"] == ETICHETTA[chi]).to_numpy(np.int8)


def _in_play(df, chi):
    p = df["uw_in_play"].to_numpy(bool)
    return p if chi == "uw" else ~p


def _allinea(base, altra, nome):
    if len(base) != len(altra) or not np.array_equal(base["seed"].to_numpy(), altra["seed"].to_numpy()):
        raise ValueError(f"{nome}: i seed non sono quelli della base")


def _contrasti(contrasti, b, seed, repliche=None):
    """Media per seed di vettori appaiati d (uno per seed), in tutte le partite, in play e in draw del giocatore.

    contrasti: nome -> (d, in_play). Restituisce nome -> {lato: (stima, lo, hi, norm_lo, norm_hi, n)}; il bootstrap
    ricampiona i seed con la matrice di pesi condivisa (statistica.somme_bootstrap), la formula normale e' quella
    appaiata di varianti_uw. Con `repliche` (dizionario) salva le B stime di "tutte le partite" per nome.
    """
    colonne, chiavi = [], []
    for nome, (d, in_play) in contrasti.items():
        for lato, m in (("", np.ones(len(d), bool)), ("_play", in_play), ("_draw", ~in_play)):
            colonne += [np.where(m, d, 0), m.astype(np.int8)]
            chiavi.append((nome, lato))
    sb = st.somme_bootstrap(np.column_stack(colonne), b, seed) if b > 0 and colonne else None
    out = {}
    for j, (nome, lato) in enumerate(chiavi):
        d, in_play = contrasti[nome]
        m = {"": np.ones(len(d), bool), "_play": in_play, "_draw": ~in_play}[lato]
        stima, nlo, nhi = st.normale_appaiata(d[m]) if m.sum() > 1 else (math.nan,) * 3
        lo = hi = math.nan
        if sb is not None:
            valori = sb[:, 2 * j] / sb[:, 2 * j + 1]
            lo, hi = st.percentili(valori)
            if lato == "" and repliche is not None:
                repliche[nome] = valori
        out.setdefault(nome, {})[lato] = (float(stima), lo, hi, float(nlo), float(nhi), int(m.sum()))
    return out


def _colonne(riga, prefisso, valori):
    for lato, (stima, lo, hi, nlo, nhi, n) in valori.items():
        riga[f"{prefisso}{lato}"] = stima
        riga[f"{prefisso}{lato}_lo"], riga[f"{prefisso}{lato}_hi"] = lo, hi
        riga[f"{prefisso}{lato}_norm_lo"], riga[f"{prefisso}{lato}_norm_hi"] = nlo, nhi
        riga[f"n{lato}"] = n


def effetti(base, varianti, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP, repliche=None):
    """Effetto di ogni sostituzione: varianti = nome -> (giocatore che possiede la carta, partite con la copia
    sostituita). Effetto = win rate del giocatore con la lista vera meno con la lista cambiata, appaiato per seed."""
    base = _ordina(base)
    contrasti, info = {}, {}
    for nome, (chi, df) in varianti.items():
        df = _ordina(df)
        _allinea(base, df, nome)
        wb, wv = _vittorie(base, chi), _vittorie(df, chi)
        contrasti[nome] = ((wb - wv).astype(np.int8), _in_play(base, chi))
        info[nome] = {"nome": nome, "giocatore": chi, "wr_base": float(wb.mean()), "wr_variante": float(wv.mean()),
                      "esiti_diversi": int((base["vincitore"].to_numpy() != df["vincitore"].to_numpy()).sum())}
    stime = _contrasti(contrasti, b, seed, repliche)
    righe = []
    for nome, riga in info.items():
        _colonne(riga, "effetto", stime[nome])
        righe.append(riga)
    return pd.DataFrame(righe)


def interazioni(base, coppie, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP):
    """Interazione causale di coppie: (nome, giocatore, partite senza A, senza B, senza A e B).

    interazione = effetto(A e B) - effetto(A) - effetto(B); per seed w(A) + w(B) - w(A e B) - w(base). Positiva:
    togliere le due carte insieme costa piu' della somma dei due costi (le carte si potenziano a vicenda)."""
    base = _ordina(base)
    contrasti, info = {}, {}
    for nome, chi, da, db, dab in coppie:
        da, db, dab = _ordina(da), _ordina(db), _ordina(dab)
        for d in (da, db, dab):
            _allinea(base, d, nome)
        w0, wa, wb, wab = (_vittorie(x, chi) for x in (base, da, db, dab))
        ip = _in_play(base, chi)
        contrasti[f"{nome}|i"] = ((wa + wb - wab - w0).astype(np.int8), ip)
        contrasti[f"{nome}|ab"] = ((w0 - wab).astype(np.int8), ip)
        info[nome] = {"nome": nome, "giocatore": chi, "effetto_a": float((w0 - wa).mean()),
                      "effetto_b": float((w0 - wb).mean())}
    stime = _contrasti(contrasti, b, seed)
    righe = []
    for nome, riga in info.items():
        _colonne(riga, "interazione", stime[f"{nome}|i"])
        e = stime[f"{nome}|ab"][""]
        riga["effetto_ab"], riga["effetto_ab_lo"], riga["effetto_ab_hi"] = e[0], e[1], e[2]
        righe.append(riga)
    return pd.DataFrame(righe)


def spearman(x, y):
    """Correlazione di Spearman: Pearson dei ranghi, con il rango medio per i pari merito."""
    rx = pd.Series(np.asarray(x, float)).rank(method="average").to_numpy()
    ry = pd.Series(np.asarray(y, float)).rank(method="average").to_numpy()
    return float(np.corrcoef(rx, ry)[0, 1])


def spearman_righe(x, y):
    """Spearman di ogni riga di due matrici (una riga per ricampionamento del bootstrap)."""
    rx = pd.DataFrame(np.asarray(x, float)).rank(axis=1, method="average").to_numpy()
    ry = pd.DataFrame(np.asarray(y, float)).rank(axis=1, method="average").to_numpy()
    rx = rx - rx.mean(axis=1, keepdims=True)  # to_numpy di pandas 3 e' in sola lettura
    ry = ry - ry.mean(axis=1, keepdims=True)
    return (rx * ry).sum(axis=1) / np.sqrt((rx * rx).sum(axis=1) * (ry * ry).sum(axis=1))


# --- Scelte --------------------------------------------------------------------------------------------------

def scegli_coppie(coppie, n=N_COPPIE, fisse=FISSE, turno=TURNO):
    """Le n coppie con la sinergia al turno `turno` piu' forte e le n piu' negativa (dai due mazzi insieme, dalle
    coppie decise e dalla matrice esplorativa, senza le varianti), poi le coppie fisse che mancano."""
    col = f"sinergia_t{turno}"
    cp = coppie
    if "variante" in cp.columns:
        cp = cp[cp["variante"].fillna("") == ""]
    cp = cp[cp[col].notna()]
    scelte = []

    def chiave(chi, a, b):
        return chi, frozenset((a, b))

    for motivo, parte in (("più forte", cp.sort_values(col, ascending=False).head(n)),
                          ("più negativa", cp.sort_values(col, ascending=True).head(n))):
        for r in parte.itertuples():
            scelte.append({"giocatore": r.giocatore, "a": r.a, "b": r.b, "motivo": motivo,
                           "sinergia": float(getattr(r, col))})
    gia = {chiave(s["giocatore"], s["a"], s["b"]): s for s in scelte}
    for chi, a, b in fisse:
        if chiave(chi, a, b) in gia:
            gia[chiave(chi, a, b)]["motivo"] += ", fissa"
            continue
        r = cp[(cp["giocatore"] == chi) & (((cp["a"] == a) & (cp["b"] == b)) | ((cp["a"] == b) & (cp["b"] == a)))]
        scelte.append({"giocatore": chi, "a": a, "b": b, "motivo": "fissa",
                       "sinergia": float(r[col].iloc[0]) if len(r) else math.nan})
    return scelte


def soglia_rango(n):
    """Scarto di rango "grande" tra ablazione e IWD in un mazzo di n carte: un terzo delle posizioni."""
    return round(n / 3)


def disaccordi(df, col_iwd="iwd_t5"):
    """Carte in cui ablazione e IWD non concordano: segno diverso, o rango nel mazzo lontano almeno
    soglia_rango(n) posizioni. Una riga per carta in disaccordo."""
    righe = []
    for chi, g in df.groupby("giocatore", sort=False):
        g = g.copy()
        g["rango_ablazione"] = g["ablazione"].rank(ascending=False, method="average")
        g["rango_iwd"] = g[col_iwd].rank(ascending=False, method="average")
        g["scarto_rango"] = (g["rango_ablazione"] - g["rango_iwd"]).abs()
        g["segno_diverso"] = np.sign(g["ablazione"]) != np.sign(g[col_iwd])
        if f"{col_iwd}_rel" in g.columns:
            # Lo stesso confronto tolta la media del mazzo da tutte e due: l'IWD a turno fisso ha uno scarto comune a
            # tutte le carte (le medie per mazzo sono positive), l'ablazione no.
            g["segno_relativo_diverso"] = (np.sign(g["ablazione"] - g["ablazione"].mean())
                                           != np.sign(g[f"{col_iwd}_rel"]))
        g["soglia"] = soglia_rango(len(g))
        righe.append(g[g["segno_diverso"] | (g["scarto_rango"] >= g["soglia"])])
    return pd.concat(righe, ignore_index=True) if righe else pd.DataFrame()


def argomento_carta(nome):
    """Il nome da passare a metriche.exe: fino alla prima lettera non ASCII (un prefisso unico)."""
    for i, ch in enumerate(nome):
        if ord(ch) > 127:
            return nome[:i]
    return nome


def cartella(nome):
    ascii_ = unicodedata.normalize("NFKD", nome).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", ascii_.lower()).strip("_")


# --- Esecuzione delle partite ------------------------------------------------------------------------------

def esecuzioni_carte(nomi):
    return [{"nome": cartella(c), "tipo": "carta", "giocatore": chi, "carte": [c],
             "sostituzioni": [f"{argomento_carta(c)}:{TERRA[chi]}"], "motivo": ""}
            for chi, lista in nomi.items() for c in lista]


def esecuzioni_coppie(scelte):
    return [{"nome": f"{cartella(s['a'])}__{cartella(s['b'])}", "tipo": "coppia", "giocatore": s["giocatore"],
             "carte": [s["a"], s["b"]], "motivo": s["motivo"], "sinergia_t5": s["sinergia"],
             "sostituzioni": [f"{argomento_carta(c)}:{TERRA[s['giocatore']]}" for c in (s["a"], s["b"])]}
            for s in scelte]


def esecuzione_controllo():
    return {"nome": "controllo_terre", "tipo": "controllo", "giocatore": "", "carte": ["Plains", "Swamp"],
            "sostituzioni": ["Plains:Plains", "Swamp:Swamp"], "motivo": "una terra base al posto di se stessa"}


def md5(percorso):
    h = hashlib.md5()
    with open(percorso, "rb") as f:
        for blocco in iter(lambda: f.read(1 << 20), b""):
            h.update(blocco)
    return h.hexdigest()


def gioca(esecuzione, exe, cartella_abl, games, processi):
    """metriche.exe in `processi` processi per intervalli di seed, file concatenati nell'ordine dei seed."""
    out = cartella_abl / esecuzione["nome"]
    out.mkdir(parents=True, exist_ok=True)
    per = -(-games // processi)
    t0 = time.perf_counter()
    lavori = []
    for k in range(processi):
        primo = 1 + k * per
        quante = min(per, games - k * per)
        if quante <= 0:
            break
        cmd = [str(exe), "--games", str(quante), "--first-seed", str(primo), "--senza-record", "--out",
               str(out / f"parte_{k}")]
        for s in esecuzione["sostituzioni"]:
            cmd += ["--sostituisci", s]
        lavori.append(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
    riepiloghi = []
    for p in lavori:
        so, se = p.communicate()
        if p.returncode != 0:
            raise RuntimeError(f"{esecuzione['nome']}: metriche.exe ha restituito {p.returncode}: {se.decode()}")
        riepiloghi.append(so.decode("utf-8", "replace"))
    with open(out / "partite.jsonl", "wb") as f:
        for k in range(len(lavori)):
            f.write((out / f"parte_{k}" / "partite.jsonl").read_bytes())
    (out / "riepilogo_parte_0.txt").write_text(riepiloghi[0], encoding="utf-8")
    for k in range(len(lavori)):
        for x in (out / f"parte_{k}").iterdir():
            x.unlink()
        (out / f"parte_{k}").rmdir()
    secondi = time.perf_counter() - t0
    return {**esecuzione, "partite": games, "processi": len(lavori), "secondi": round(secondi, 1),
            "md5": md5(out / "partite.jsonl"),
            "sostituzioni_risolte": [r for r in riepiloghi[0].splitlines() if r.startswith("Sostituzione")]}


def leggi_esecuzioni(cartella_abl):
    p = cartella_abl / "esecuzioni.json"
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else []


def scrivi_esecuzioni(cartella_abl, esecuzioni):
    (cartella_abl / "esecuzioni.json").write_text(json.dumps(esecuzioni, ensure_ascii=False, indent=1),
                                                  encoding="utf-8")


def leggi_partite(percorso):
    return pd.read_json(percorso, lines=True)[["seed", "uw_in_play", "vincitore"]]


# --- Analisi (chiamata da metriche_carte.py) -----------------------------------------------------------------

def analizza(cartella_abl, base, res, nomi, desc, md5_base, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP):
    """Effetti delle carte, interazioni delle coppie, confronto con l'IWD e controlli. `base`: le partite degli
    euristici v1 (seed, uw_in_play, vincitore), le stesse delle metriche per carta; `res`: i Risultati di
    metriche_carte.calcola (con le repliche del bootstrap)."""
    base = _ordina(base)
    esecuzioni = {e["nome"]: e for e in leggi_esecuzioni(cartella_abl)}
    partite = {nome: leggi_partite(cartella_abl / nome / "partite.jsonl") for nome in esecuzioni}
    ctrl, falliti = [], 0

    def esito(ok, testo):
        nonlocal falliti
        falliti += not ok
        ctrl.append(f"{'OK  ' if ok else 'NO  '} {testo}")

    # Carte
    per_carta = {e["carte"][0]: e for e in esecuzioni.values() if e["tipo"] == "carta"}
    varianti = {c: (e["giocatore"], partite[e["nome"]]) for c, e in per_carta.items()}
    rep_abl = {}
    eff = effetti(base, varianti, b, seed, rep_abl)
    c = res.carte.set_index(["giocatore", "carta"])
    for col in ("iwd", "iwd_t3", "iwd_t5", "iwd_t7", "iwd_t5_rel", "n_cercata"):
        eff[col] = [c.loc[(r.giocatore, r.nome), col] for r in eff.itertuples()]
    d = desc.set_index(["giocatore", "carta"])
    eff["mano_fine"] = [d.loc[(r.giocatore, r.nome), "copie_mano_fine"] / max(1, d.loc[(r.giocatore, r.nome),
                        "copie_viste"]) for r in eff.itertuples()]
    eff = eff.rename(columns={"nome": "carta"})
    ordine = {(chi, carta): i for i, (chi, carta) in enumerate((chi, x) for chi, l in nomi.items() for x in l)}
    eff["ordine"] = [ordine[(r.giocatore, r.carta)] for r in eff.itertuples()]
    eff = eff.sort_values("ordine").drop(columns="ordine").reset_index(drop=True)
    mancanti = [(chi, x) for chi, l in nomi.items() for x in l if x not in per_carta]
    esito(not mancanti, f"ablazione: {len(per_carta)} carte giocate su {sum(len(l) for l in nomi.values())}"
          + (f" (mancano {mancanti})" if mancanti else "") + f", {len(base)} seed ognuna, gli stessi della base")

    # Bootstrap contro formula normale
    if b > 0:
        scarti = [max(abs(r[f"effetto{l}_lo"] - r[f"effetto{l}_norm_lo"]), abs(r[f"effetto{l}_hi"] -
                      r[f"effetto{l}_norm_hi"])) for _, r in eff.iterrows() for l in LATI]
        esito(max(scarti) <= 0.0005, f"ablazione, bootstrap contro formula normale appaiata ({len(scarti)} intervalli: "
              f"carte x tutte/play/draw): scarto massimo {100 * max(scarti):.3f} punti, medio "
              f"{100 * np.mean(scarti):.3f}")

    # Controllo: una terra base al posto di se stessa
    for e in esecuzioni.values():
        if e["tipo"] != "controllo":
            continue
        p = _ordina(partite[e["nome"]])
        uguali = len(p) == len(base) and np.array_equal(p["vincitore"].to_numpy(), base["vincitore"].to_numpy())
        esito(uguali and e["md5"] == md5_base, f"controllo {', '.join(e['sostituzioni'])}: esiti uguali alla base "
              f"su tutti i {len(p)} seed (differenza esattamente zero), partite.jsonl MD5 {e['md5'][:8]}... "
              f"{'uguale a quello' if e['md5'] == md5_base else 'diverso da quello'} della base ({md5_base[:8]}...)")

    # Coppie
    righe_coppie = []
    for e in esecuzioni.values():
        if e["tipo"] != "coppia":
            continue
        a, bb = e["carte"]
        if a not in per_carta or bb not in per_carta:
            esito(False, f"coppia {a} + {bb}: manca l'ablazione di una delle due carte")
            continue
        righe_coppie.append((e["nome"], e["giocatore"], partite[per_carta[a]["nome"]],
                             partite[per_carta[bb]["nome"]], partite[e["nome"]]))
    inter = interazioni(base, righe_coppie, b, seed) if righe_coppie else pd.DataFrame()
    if len(inter):
        info = pd.DataFrame([{"nome": e["nome"], "a": e["carte"][0], "b": e["carte"][1], "motivo": e["motivo"],
                              "sinergia_t5_scelta": e.get("sinergia_t5", math.nan)}
                             for e in esecuzioni.values() if e["tipo"] == "coppia"])
        inter = inter.merge(info, on="nome")
        cp = res.coppie
        if "variante" in cp.columns:
            cp = cp[cp["variante"].fillna("") == ""]
        for col in ("sinergia_t5", "sinergia_t5_lo", "sinergia_t5_hi", "sinergia", "p_sinergia_t5"):
            inter[col] = [_valore_coppia(cp, r.giocatore, r.a, r.b, col) for r in inter.itertuples()]
        esito(True, f"ablazione di coppia: {len(inter)} coppie")

    # Confronto con l'IWD: Spearman per mazzo e per i due mazzi insieme, con il bootstrap congiunto
    confronto = []
    for metrica in ("iwd", "iwd_t3", "iwd_t5", "iwd_t7"):
        for gruppo, chi_sel in (("UW", ("uw",)), ("Rakdos", ("rakdos",)), ("Tutte", ("uw", "rakdos"))):
            sel = eff[eff["giocatore"].isin(chi_sel)]
            riga = {"metrica": metrica, "gruppo": gruppo, "carte": len(sel),
                    "rho": spearman(sel["effetto"], sel[metrica])}
            ra = [rep_abl.get(r.carta) for r in sel.itertuples()]
            ri = [res.repliche.get((r.giocatore, r.carta, metrica)) for r in sel.itertuples()]
            if b > 0 and all(x is not None for x in ra + ri) and len(sel) > 2:
                if len(ra[0]) != len(ri[0]):
                    raise ValueError("bootstrap dell'ablazione e delle metriche con B diversi")
                rho_b = spearman_righe(np.column_stack(ra), np.column_stack(ri))
                riga["rho_lo"], riga["rho_hi"] = st.percentili(rho_b)
            confronto.append(riga)
    confronto = pd.DataFrame(confronto)

    dis = disaccordi(eff.rename(columns={"effetto": "ablazione"}), "iwd_t5")

    # Media dell'effetto per mazzo (media semplice sulle carte), con le repliche
    medie = []
    for chi in nomi:
        sel = eff[eff["giocatore"] == chi]
        riga = {"giocatore": chi, "carte": len(sel), "media": float(sel["effetto"].mean())}
        if b > 0 and all(r in rep_abl for r in sel["carta"]):
            riga["media_lo"], riga["media_hi"] = st.percentili(np.column_stack([rep_abl[r] for r in sel["carta"]]
                                                                                ).mean(axis=1))
        medie.append(riga)
    tempi = {"partite": sum(e.get("partite", 0) for e in esecuzioni.values()),
             "secondi": sum(e.get("secondi", 0) for e in esecuzioni.values()), "esecuzioni": len(esecuzioni)}
    return {"carte": eff, "coppie": inter, "confronto": confronto, "disaccordi": dis, "medie": pd.DataFrame(medie),
            "controlli": ctrl, "falliti": falliti, "tempi": tempi}


def _valore_coppia(cp, chi, a, b, col):
    r = cp[(cp["giocatore"] == chi) & (((cp["a"] == a) & (cp["b"] == b)) | ((cp["a"] == b) & (cp["b"] == a)))]
    return float(r[col].iloc[0]) if len(r) and col in r.columns else math.nan


# --- Riga di comando ---------------------------------------------------------------------------------------

def main(argv=None):
    from metriche import converti
    from metriche.metriche_carte import non_terre
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("comando", choices=["gioca"])
    ap.add_argument("--carte", action="store_true")
    ap.add_argument("--coppie", action="store_true")
    ap.add_argument("--controlli", action="store_true")
    ap.add_argument("--solo", default=None, help="nomi di cartella separati da virgole")
    ap.add_argument("--exe", default=str(RADICE / "build/Release/metriche.exe"))
    ap.add_argument("--processi", type=int, default=8)
    ap.add_argument("--games", type=int, default=100_000)
    ap.add_argument("--dati", default=str(RADICE / "data/metriche/v1"))
    args = ap.parse_args(argv)
    dati = Path(args.dati)
    cartella_abl = dati / "ablazione"
    cartella_abl.mkdir(parents=True, exist_ok=True)
    nomi = non_terre(converti.leggi_mazzi())
    lista = []
    if args.controlli:
        lista.append(esecuzione_controllo())
    if args.carte:
        lista += esecuzioni_carte(nomi)
    if args.coppie:
        lista += esecuzioni_coppie(scegli_coppie(pd.read_csv(dati / "coppie.csv", keep_default_na=False,
                                                             na_values=[""]).assign(
            variante=lambda d: d["variante"].fillna("") if "variante" in d else "")))
    if args.solo:
        solo = set(args.solo.split(","))
        lista = [e for e in lista if e["nome"] in solo]
    fatte = {e["nome"]: e for e in leggi_esecuzioni(cartella_abl)}
    for e in lista:
        r = gioca(e, Path(args.exe), cartella_abl, args.games, args.processi)
        fatte[r["nome"]] = r
        scrivi_esecuzioni(cartella_abl, list(fatte.values()))
        print(f"{r['nome']}: {r['secondi']} s, MD5 {r['md5']}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
