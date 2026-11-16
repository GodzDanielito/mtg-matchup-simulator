# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "numpy==2.5.5",
#   "pandas==3.1.1",
#   "pyarrow==26.0.1",
#   "statsmodels==0.15.0",
# ]
# ///
"""Metriche per carta degli agenti euristici v1 (docs/metriche.md, sezioni 3-5).

Dalle tabelle Parquet di converti.py: per ogni carta non terra dei due mazzi (Witch Enchanter compresa, come
carta) OH, GD, GIH, GNS, "solo cercata", "vista solo dal cimitero", IWD classico, IWD relativo, IWD a turno fisso,
OH sulla prima mano da 7, curva delle copie viste; le coppie decise prima (sezione 4) e la matrice esplorativa;
la media degli IWD per mazzo (il difetto della lunghezza della partita); i descrittivi (lanci, carte rimaste in
mano); le terre a parte; i controlli di coerenza della sezione 11. Intervalli: Wilson per le proporzioni,
bootstrap sulle partite (B = 2.000, seed fisso) per le differenze.

Uso:
    uv run --no-project python/metriche/metriche_carte.py [--dati data/metriche/v1] [--doc docs/risultati_v1.md]
        [--partite-matchup data/matchup/partite.jsonl] [--carte-morte data/matchup/carte_morte.csv]
        [--tempi data/metriche/v1/tempi.json]
"""

import argparse
import itertools
import json
import math
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import ablazione  # noqa: E402
from metriche import statistica as st  # noqa: E402

LATI = ("play", "draw")
ETICHETTA = {"uw": "UW", "rakdos": "Rakdos"}
NON_VISTA = 10_000  # turno "mai" per i confronti

# Le differenze: nome -> (gruppo "si", gruppo "no"). Quelle a turno fisso si aggiungono per ogni T.
DIFFERENZE = {"iwd": ("GIH", "GNS"), "oh7_diff": ("OH7", "non_OH7")}
GRUPPI_PUNTO = ("OH", "GD", "cercata", "cimitero", "copie_0", "copie_1", "copie_2", "copie_3+")
CELLE = ("AB", "AnB", "nAB", "nAnB")

# Coppie decise prima di guardare i dati (sezione 4, con la decisione 5 di Daniele).
COPPIE_DECISE = {
    "uw": [("Solitude", "Ephemerate"), ("Solitude", "Witch Enchanter"), ("Recruiter of the Guard", "Solitude"),
           ("Quantum Riddler", "Consign to Memory"), ("Phelia, Exuberant Shepherd", "Solitude"),
           ("Phelia, Exuberant Shepherd", "Ephemerate"), ("Guide of Souls", "Ocelot Pride"),
           ("Consign to Memory", "Phelia, Exuberant Shepherd"), ("Ephemerate", "Quantum Riddler"),
           ("Thraben Charm", "Sanctifier en-Vec")],
    "rakdos": [("Nethergoyf", "Stitcher's Supplier"), ("Nethergoyf", "Dark Matter Manipulator"),
               ("Detective's Phoenix", "Fear of Missing Out"), ("Thoughtseize", "Lightning Bolt"),
               ("Flare of Malice", "Stitcher's Supplier"), ("Street Wraith", "Nethergoyf")],
}
# Coppie decise in cui la seconda carta conta "in mano per qualsiasi via" (I, P o C), non solo GIH: una Solitude
# cercata da Recruiter e' fuori da GIH per definizione, quindi la coppia normale non misura D-2c (nota di Daniele
# del 16/11/2026). Il gruppo "non vista" resta GNS.
COPPIE_IPC = {"uw": [("Recruiter of the Guard", "Solitude")]}
VARIANTE_IPC = "ipc"


@dataclass
class Risultati:
    carte: pd.DataFrame
    coppie: pd.DataFrame
    bias: pd.DataFrame
    # Stime bootstrap (una per ricampionamento) dell'IWD classico e a turno fisso di ogni carta:
    # (giocatore, carta, "iwd" | "iwd_t5" ...) -> vettore di B valori. Servono a Spearman con l'ablazione, che usa
    # la stessa matrice di pesi (stesse partite, stesso seed del bootstrap).
    repliche: dict = field(default_factory=dict)


class _Somme:
    """Colonne di indicatori sulle partite (0/1) e le loro somme: puntuali e, se b > 0, per ricampionamento."""

    def __init__(self, b, seed, blocco=256):
        self.b, self.seed, self.blocco = b, seed, blocco
        self.nomi, self.dati, self.s0, self.sb = [], [], {}, {}

    def aggiungi(self, nome, v):
        self.nomi.append(nome)
        self.dati.append(np.asarray(v, dtype=np.float32))
        if len(self.dati) >= self.blocco:
            self.chiudi()

    def gruppo(self, nome, indicatore, vince, lati):
        """n e vittorie del gruppo, per lato (play, draw) o per un solo lato ("tutte")."""
        for lato, maschera in lati.items():
            g = indicatore & maschera
            self.aggiungi(f"{nome}|{lato}|n", g)
            self.aggiungi(f"{nome}|{lato}|w", g & vince)

    def chiudi(self):
        if not self.dati:
            return
        x = np.column_stack(self.dati)
        s0 = x.sum(axis=0, dtype=np.float64)
        sb = st.somme_bootstrap(x, self.b, self.seed) if self.b > 0 else None
        for j, nome in enumerate(self.nomi):
            self.s0[nome] = s0[j]
            if sb is not None:
                self.sb[nome] = sb[:, j]
        self.nomi, self.dati = [], []


def _rapporto(w, n):
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.divide(w, n)


def _wr(v, nome, lati):
    n = sum(v(f"{nome}|{l}|n") for l in lati)
    return _rapporto(sum(v(f"{nome}|{l}|w") for l in lati), n), n


def _vince(partite, chi):
    return (partite["vincitore"] == ETICHETTA[chi]).to_numpy()


def _in_play(partite, chi):
    p = partite["uw_in_play"].to_numpy(bool)
    return p if chi == "uw" else ~p


def _allinea(carte, chi, carta, seeds):
    sub = carte[(carte["giocatore"] == chi) & (carte["carta"] == carta)].sort_values("seed")
    if not np.array_equal(sub["seed"].to_numpy(), seeds):
        raise ValueError(f"{chi} {carta}: le righe non coprono esattamente le partite")
    return sub


def _turno_partita(t, in_play):
    """Turno della partita del T-esimo turno del giocatore."""
    return np.where(in_play, 2 * t - 1, 2 * t)


def calcola(partite, carte, nomi, coppie, turni=(3, 5, 7), b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP,
            matrice=False, coppie_ipc=None):
    coppie_ipc = coppie_ipc or {}
    con_ipc = {(chi, bb) for chi, lista in coppie_ipc.items() for _, bb in lista}
    partite = partite.sort_values("seed").reset_index(drop=True)
    seeds = partite["seed"].to_numpy()
    lunghezza = partite["turni"].to_numpy()
    diff = dict(DIFFERENZE)
    for t in turni:
        diff[f"iwd_t{t}"] = (f"t{t}_si", f"t{t}_no")
    boot, punto = _Somme(b, seed), _Somme(0, seed)
    indicatori = {}  # (chi, carta) -> {"GIH": ..., "GNS": ..., "t3_si": ...}: per le coppie

    for chi, lista in nomi.items():
        vince, in_play = _vince(partite, chi), _in_play(partite, chi)
        lati = {"play": in_play, "draw": ~in_play}
        for carta in lista:
            r = _allinea(carte, chi, carta, seeds)
            classe = r["classe"].to_numpy()
            ip = r["turno_ip"].astype("Float64").fillna(NON_VISTA).to_numpy(float)
            vista = r["turno_vista"].astype("Float64").fillna(NON_VISTA).to_numpy(float)
            copie = r["copie_viste"].to_numpy()
            m7 = r["copie_mano7"].to_numpy()
            ind = {"GIH": np.isin(classe, ("OH", "GD")), "GNS": classe == "GNS", "OH7": m7 > 0,
                   "non_OH7": m7 == 0}
            for t in turni:
                g = _turno_partita(t, in_play)
                in_corso = lunghezza >= g
                ind[f"t{t}_si"] = in_corso & (ip <= g)
                ind[f"t{t}_no"] = in_corso & (vista > g)
            if (chi, carta) in con_ipc:  # in mano per qualsiasi via (I, P o C): la variante delle coppie
                ipc = r["turno_ipc"].astype("Float64").fillna(NON_VISTA).to_numpy(float)
                ind["GIHC"] = np.isin(classe, ("OH", "GD", "cercata"))
                for t in turni:
                    g = _turno_partita(t, in_play)
                    ind[f"t{t}_si_c"] = (lunghezza >= g) & (ipc <= g)
            indicatori[(chi, carta)] = ind
            for nome, v in ind.items():
                boot.gruppo(f"{chi}|{carta}|{nome}", v, vince, lati)
            punti = {c: classe == c for c in ("OH", "GD", "cercata", "cimitero")}
            punti.update({"copie_0": copie == 0, "copie_1": copie == 1, "copie_2": copie == 2, "copie_3+": copie >= 3})
            for nome, v in punti.items():
                punto.gruppo(f"{chi}|{carta}|{nome}", v, vince, lati)

    # Coppie: le celle (entrambe viste, solo A, solo B, nessuna) tra le partite in cui le due carte sono GIH o GNS.
    elenco_coppie = []  # (giocatore, A, B, decisa, variante)
    for chi in nomi:
        decise = [tuple(c) for c in coppie.get(chi, [])]
        elenco_coppie += [(chi, a, bb, True, "") for a, bb in decise]
        elenco_coppie += [(chi, a, bb, True, VARIANTE_IPC) for a, bb in coppie_ipc.get(chi, [])]
        if matrice:
            gia = {frozenset(c) for c in decise}
            elenco_coppie += [(chi, a, bb, False, "") for a, bb in itertools.combinations(nomi[chi], 2)
                              if frozenset((a, bb)) not in gia]
    # Le coppie decise hanno la sinergia a ogni turno fisso; quelle della matrice solo al turno centrale, perche'
    # la sinergia classica ha lo stesso difetto della lunghezza della partita dell'IWD classico.
    turno_matrice = turni[len(turni) // 2] if turni else None

    def turni_coppia(decisa):
        return list(turni) if decisa else ([turno_matrice] if turno_matrice else [])

    def _pref_coppia(chi, a, bb, variante):
        return f"{chi}|{a}&{bb}" + (f"~{variante}" if variante else "")

    for chi, a, bb, decisa, variante in elenco_coppie:
        vince = _vince(partite, chi)
        tutte = {"tutte": np.ones(len(seeds), dtype=bool)}
        ia, ib = indicatori[(chi, a)], indicatori[(chi, bb)]
        c = "_c" if variante == VARIANTE_IPC else ""
        varianti = [("", "GIH", "GIHC" if c else "GIH", "GNS")] + \
                   [(f"_t{t}", f"t{t}_si", f"t{t}_si{c}", f"t{t}_no") for t in turni_coppia(decisa)]
        for suff, si_a, si_b, no in varianti:
            celle = {"AB": ia[si_a] & ib[si_b], "AnB": ia[si_a] & ib[no], "nAB": ia[no] & ib[si_b],
                     "nAnB": ia[no] & ib[no]}
            for cella, v in celle.items():
                boot.gruppo(f"{_pref_coppia(chi, a, bb, variante)}|{cella}{suff}", v, vince, tutte)
    boot.chiudi()
    punto.chiudi()

    def con_intervallo(riga, nome, f):
        """Stima puntuale di f e, con il bootstrap, intervallo dei percentili; restituisce le stime bootstrap."""
        riga[nome] = float(f(boot.s0.__getitem__))
        if b > 0:
            valori = f(boot.sb.__getitem__)
            riga[f"{nome}_lo"], riga[f"{nome}_hi"] = st.percentili(valori)
            return valori
        return None

    # --- Carte
    righe, per_bias = [], {}
    for chi, lista in nomi.items():
        for carta in lista:
            riga = {"giocatore": chi, "carta": carta}
            pref = f"{chi}|{carta}"
            for g in ("GIH", "GNS", "OH7", "non_OH7") + tuple(x for t in turni for x in (f"t{t}_si", f"t{t}_no")):
                n = int(sum(boot.s0[f"{pref}|{g}|{l}|n"] for l in LATI))
                k = int(sum(boot.s0[f"{pref}|{g}|{l}|w"] for l in LATI))
                riga[f"n_{g}"] = n
                riga[f"wr_{g}"], riga[f"wr_{g}_lo"], riga[f"wr_{g}_hi"] = st.wilson(k, n)
            for g in GRUPPI_PUNTO:
                n = int(sum(punto.s0[f"{pref}|{g}|{l}|n"] for l in LATI))
                k = int(sum(punto.s0[f"{pref}|{g}|{l}|w"] for l in LATI))
                riga[f"n_{g}"] = n
                riga[f"wr_{g}"], riga[f"wr_{g}_lo"], riga[f"wr_{g}_hi"] = st.wilson(k, n)
            for nome, (si, no) in diff.items():
                for suff, lati_d in (("", LATI), ("_play", ("play",)), ("_draw", ("draw",))):
                    def f(v, si=si, no=no, lati_d=lati_d, pref=pref):  # legati ora: f si richiama dopo
                        return _wr(v, f"{pref}|{si}", lati_d)[0] - _wr(v, f"{pref}|{no}", lati_d)[0]
                    valori = con_intervallo(riga, nome + suff, f)
                    n_si = sum(boot.s0[f"{pref}|{si}|{l}|n"] for l in lati_d)
                    n_no = sum(boot.s0[f"{pref}|{no}|{l}|n"] for l in lati_d)
                    riga[f"nmin_{nome}{suff}"] = int(min(n_si, n_no))
                    if suff == "":
                        per_bias.setdefault((chi, nome), []).append((f, si, pref, valori))
            righe.append(riga)
    df_carte = pd.DataFrame(righe)
    repliche = {(chi, pref.split("|", 1)[1], nome): valori
                for (chi, nome), voci in per_bias.items() for _, _, pref, valori in voci if valori is not None}

    # --- IWD relativo e media per mazzo, pesata per le partite del gruppo "si" (GIH per l'IWD classico)
    metriche_rel = ["iwd"] + [f"iwd_t{t}" for t in turni]
    medie = {}
    for chi in nomi:
        for nome in metriche_rel:
            voci = per_bias[(chi, nome)]

            def media(v, voci=voci):
                num = sum(_nan0(f(v)) * _n(v, pref, si) for f, si, pref, _ in voci)
                return num / sum(_n(v, pref, si) for f, si, pref, _ in voci)
            medie[(chi, nome)] = media
            for f, si, pref, _ in voci:
                carta = pref.split("|", 1)[1]
                i = df_carte.index[(df_carte["giocatore"] == chi) & (df_carte["carta"] == carta)][0]
                riga = {}
                con_intervallo(riga, f"{nome}_rel", lambda v, f=f, media=media: f(v) - media(v))
                for k, val in riga.items():
                    df_carte.loc[i, k] = val

    righe_bias = []
    for nome in metriche_rel + ["iwd_rel"]:
        riga = {"metrica": nome}
        base = nome.removesuffix("_rel")
        rel = nome.endswith("_rel")
        for chi in nomi:
            m = medie[(chi, base)]
            con_intervallo(riga, chi, (lambda v: m(v) - m(v)) if rel else m)
        if len(nomi) == 2:
            mu, mr = medie[("uw", base)], medie[("rakdos", base)]
            con_intervallo(riga, "differenza", (lambda v: (mu(v) - mu(v)) - (mr(v) - mr(v))) if rel
                           else (lambda v: mu(v) - mr(v)))
        righe_bias.append(riga)
    df_bias = pd.DataFrame(righe_bias)

    # --- Coppie
    righe = []
    for chi, a, bb, decisa, variante in elenco_coppie:
        pref = _pref_coppia(chi, a, bb, variante)
        riga = {"giocatore": chi, "a": a, "b": bb, "decisa": decisa, "variante": variante}
        for cella in CELLE:
            n, k = int(boot.s0[f"{pref}|{cella}|tutte|n"]), int(boot.s0[f"{pref}|{cella}|tutte|w"])
            riga[f"n_{cella}"] = n
            riga[f"wr_{cella}"], riga[f"wr_{cella}_lo"], riga[f"wr_{cella}_hi"] = st.wilson(k, n)

        def cella_wr(v, cella, suff="", pref=pref):
            return _wr(v, f"{pref}|{cella}{suff}", ("tutte",))[0]

        for suff in [""] + [f"_t{t}" for t in turni_coppia(decisa)]:
            def sin(v, suff=suff):
                return (cella_wr(v, "AB", suff) - cella_wr(v, "AnB", suff)) - \
                       (cella_wr(v, "nAB", suff) - cella_wr(v, "nAnB", suff))
            valori = con_intervallo(riga, f"sinergia{suff}", sin)
            riga[f"nmin_sinergia{suff}"] = int(min(boot.s0[f"{pref}|{c}{suff}|tutte|n"] for c in CELLE))
            if valori is not None:  # p bilaterale dalla deviazione standard del bootstrap
                sd = float(np.nanstd(valori, ddof=1))
                z = riga[f"sinergia{suff}"] / sd if sd > 0 else math.nan
                riga[f"p_sinergia{suff}"] = math.erfc(abs(z) / math.sqrt(2)) if not math.isnan(z) else math.nan
        con_intervallo(riga, "iwd_a_con_b", lambda v: cella_wr(v, "AB") - cella_wr(v, "nAB"))
        con_intervallo(riga, "iwd_a_senza_b", lambda v: cella_wr(v, "AnB") - cella_wr(v, "nAnB"))
        righe.append(riga)
    df_coppie = pd.DataFrame(righe)
    if b > 0 and matrice and len(df_coppie):
        for suff in [""] + ([f"_t{turno_matrice}"] if turno_matrice else []):
            df_coppie[f"bh_5{suff}"] = False
            for chi in nomi:
                sel = (df_coppie["giocatore"] == chi) & ~df_coppie["decisa"]
                if sel.any():
                    df_coppie.loc[sel, f"bh_5{suff}"] = benjamini_hochberg(
                        df_coppie.loc[sel, f"p_sinergia{suff}"].to_numpy())
    return Risultati(df_carte, df_coppie, df_bias, repliche)


def _n(v, pref, si):
    return sum(v(f"{pref}|{si}|{l}|n") for l in LATI)


def _nan0(x):
    return np.nan_to_num(x, nan=0.0)


def benjamini_hochberg(p, q=0.05):
    """Maschera dei test che superano Benjamini-Hochberg al livello q."""
    p = np.asarray(p, dtype=float)
    m = len(p)
    ordine = np.argsort(p)
    soglie = q * np.arange(1, m + 1) / m
    passa = p[ordine] <= soglie
    k = np.max(np.nonzero(passa)[0]) + 1 if passa.any() else 0
    esito = np.zeros(m, dtype=bool)
    esito[ordine[:k]] = True
    return esito


# --- Descrittivi, terre e controlli ----------------------------------------------------------------------

def descrittivi(carte, nomi):
    righe = []
    for chi, lista in nomi.items():
        for carta in lista:
            r = carte[(carte["giocatore"] == chi) & (carte["carta"] == carta)]
            viste = int(r["copie_viste"].sum())
            lanci = np.concatenate([np.asarray(x) for x in r["lanci_turno"] if len(x)] or [np.array([])])
            riga = {"giocatore": chi, "carta": carta, "copie_viste": viste,
                    "lanciate": int(r["copie_lanciate"].sum()),
                    "turno_medio_lancio": float(lanci.mean()) if len(lanci) else math.nan,
                    "turno_mediano_lancio": float(np.median(lanci)) if len(lanci) else math.nan,
                    "copie_mano_fine": int(r["copie_mano_fine"].sum()),
                    "partite_mano_fine": int((r["copie_mano_fine"] > 0).sum())}
            for c in "LTESKYM":
                riga[f"k_{c}"] = int(r[f"k_{c}"].sum())
            righe.append(riga)
    return pd.DataFrame(righe)


def prima_terra_mancata(terre):
    """Il primo turno proprio t con meno di t terre in campo alla sua fine; None se mai."""
    for t, n in enumerate(terre, start=1):
        if n < t:
            return t
    return None


def tabelle_terre(partite):
    righe = []
    for chi in ("uw", "rakdos"):
        vince, in_play = _vince(partite, chi), _in_play(partite, chi)
        misure = {"terre nella mano tenuta": partite[f"terre_mano_{chi}"].clip(upper=5).to_numpy()}
        if chi == "uw":
            misure["terre + Witch nella mano tenuta"] = (partite["terre_mano_uw"] + partite["witch_mano_uw"]
                                                          ).clip(upper=5).to_numpy()
        mancata = np.array([prima_terra_mancata(t) or 0 for t in partite[f"terre_{chi}"]])
        misure["prima terra mancata (turno proprio)"] = np.where(mancata == 0, 99, np.minimum(mancata, 7))
        for misura, valori in misure.items():
            for val in sorted(set(valori.tolist())):
                for lato, m in (("tutte", np.ones_like(in_play)), ("play", in_play), ("draw", ~in_play)):
                    sel = (valori == val) & m
                    n, k = int(sel.sum()), int((sel & vince).sum())
                    p, lo, hi = st.wilson(k, n)
                    righe.append({"giocatore": chi, "misura": misura, "valore": val, "lato": lato, "n": n,
                                  "wr": p, "wr_lo": lo, "wr_hi": hi})
    return pd.DataFrame(righe)


def controlli(partite, carte, mazzi, res, partite_matchup=None, carte_morte=None):
    """I controlli di coerenza della sezione 11; restituisce righe di testo e il numero di controlli falliti."""
    out, falliti = [], 0
    n = len(partite)

    def esito(ok, testo):
        nonlocal falliti
        falliti += not ok
        out.append(f"{'OK  ' if ok else 'NO  '} {testo}")

    # 1. Partizione per carta
    gruppi = carte.groupby(["giocatore", "carta"])
    righe_per_carta = gruppi.size()
    seed_per_carta = gruppi["seed"].nunique()
    attese = sum(len(m) for m in mazzi.values())
    esito(len(righe_per_carta) == attese and (righe_per_carta == n).all() and (seed_per_carta == n).all(),
          f"partizione: {len(righe_per_carta)} carte (attese {attese}), ognuna con una riga per ognuna delle "
          f"{n} partite; OH + GD + solo cercata + solo cimitero + GNS = {n} per ogni carta")
    oh_senza_i = int(((carte["classe"] == "OH") & (carte["k_I"] == 0)).sum())
    gd_con_i = int(((carte["classe"] == "GD") & (carte["k_I"] > 0)).sum())
    esito(oh_senza_i == 0 and gd_con_i == 0, f"OH e GD disgiunte: OH senza copia iniziale {oh_senza_i}, "
          f"GD con copia iniziale {gd_con_i}")
    cim = carte["classe"] == "cimitero"
    solo_r_x = int((cim & (carte["k_Y"] == 0)).sum())
    out.append(f"     'vista solo dal cimitero': {int(cim.sum())} righe, di cui {solo_r_x} senza un lancio dal "
               f"cimitero (solo ritorno in mano o lancio dall'esilio)")
    # 2. Copie
    troppe = int((carte["copie_viste"] > carte["copie_lista"]).sum())
    esito(troppe == 0, f"copie viste <= copie nella lista: {troppe} righe oltre")
    # 3. Riepilogo del matchup
    for nome, sel in (("tutte", np.ones(n, bool)), ("UW in play", partite["uw_in_play"].to_numpy()),
                      ("UW in draw", ~partite["uw_in_play"].to_numpy())):
        p = partite[sel]
        mani = {chi: p[f"carte_mano_{chi}"].value_counts(normalize=True).sort_index(ascending=False)
                for chi in ("uw", "rakdos")}
        out.append(f"     {nome}: {len(p)} partite, UW {100 * p['vince_uw'].mean():.2f}%; mani UW "
                   + ", ".join(f"{k}: {100 * v:.2f}%" for k, v in mani["uw"].items()) + "; Rakdos "
                   + ", ".join(f"{k}: {100 * v:.2f}%" for k, v in mani["rakdos"].items()))
    if partite_matchup is not None:
        m = pd.read_json(partite_matchup, lines=True).sort_values("seed").reset_index(drop=True)
        p = partite.sort_values("seed").reset_index(drop=True)
        uguali = len(m) == len(p) and all(
            np.array_equal(m[c].to_numpy(), p[c].to_numpy())
            for c in ("seed", "uw_in_play", "vincitore", "turni", "mulligan_uw", "mulligan_rakdos", "vita_uw",
                      "vita_rakdos"))
        esito(uguali, f"partita per partita uguale a matchup.exe ({partite_matchup.name}, {len(m)} partite): seed, "
              f"play/draw, vincitore, turni, mulligan, vite finali; UW {100 * m['vincitore'].eq('UW').mean():.2f}% "
              f"contro {100 * p['vince_uw'].mean():.2f}%")
    # 4. Carte morte
    if carte_morte is not None:
        cm = pd.read_csv(carte_morte)
        cm["giocatore"] = np.where(cm["mazzo"].str.startswith("UW"), "uw", "rakdos")
        diverse, confrontate = [], 0
        for chi in ("uw", "rakdos"):
            in_play = _in_play(partite.sort_values("seed"), chi)
            seed_play = set(partite.sort_values("seed")["seed"].to_numpy()[in_play])
            for carta in mazzi[chi]:
                r = carte[(carte["giocatore"] == chi) & (carte["carta"] == carta)]
                fine = r[r["copie_mano_fine"] > 0]
                nostri = {"tutte": len(fine), "play": int(fine["seed"].isin(seed_play).sum())}
                nostri["draw"] = nostri["tutte"] - nostri["play"]
                for lato, val in nostri.items():
                    riga = cm[(cm["giocatore"] == chi) & (cm["carta"] == carta) & (cm["lato"] == lato)]
                    loro = int(riga["partite_ancora_in_mano_alla_fine"].iloc[0]) if len(riga) else 0
                    confrontate += 1
                    if loro != val:
                        diverse.append(f"{chi} {carta} {lato}: {val} contro {loro}")
        esito(not diverse, f"carte in mano a fine partita uguali a carte_morte.exe: {confrontate} valori "
              f"(carta x lato), {len(diverse)} diversi" + (": " + "; ".join(diverse[:10]) if diverse else ""))
    # 5. Bootstrap contro formula normale, sull'IWD classico di ogni carta
    if "iwd_lo" in res.carte.columns:
        scarti = []
        for _, r in res.carte.iterrows():
            kg = round(r["wr_GIH"] * r["n_GIH"])
            kn = round(r["wr_GNS"] * r["n_GNS"])
            _, lo, hi = st.normale_due_proporzioni(kg, r["n_GIH"], kn, r["n_GNS"])
            scarti.append(max(abs(lo - r["iwd_lo"]), abs(hi - r["iwd_hi"])))
        esito(max(scarti) <= 0.0005, f"bootstrap contro formula normale (IWD classico, {len(scarti)} carte): "
              f"scarto massimo degli estremi {100 * max(scarti):.3f} punti, medio {100 * np.mean(scarti):.3f}")
    return out, falliti


# --- Formattazione del documento -------------------------------------------------------------------------

def _num(x, dec=1, segno=False):
    s = f"{x:+.{dec}f}" if segno else f"{x:.{dec}f}"
    return s.replace(".", ",")


def _intero(n):
    return f"{int(n):,}".replace(",", ".")


def _livello(n, testo):
    lv = st.livello(n)
    if lv == "nascosto":
        return "—"
    return f"*{testo}*" if lv == "indicativo" else testo


def fmt_wr(n, p, lo, hi):
    if n == 0 or (isinstance(p, float) and math.isnan(p)):
        return "—"
    return _livello(n, f"{_num(100 * p)} [{_num(100 * lo)}; {_num(100 * hi)}]")


def fmt_diff(nmin, d, lo=None, hi=None, dec=1):
    if nmin == 0 or math.isnan(d):
        return "—"
    testo = _num(100 * d, dec, True)
    if lo is not None and not math.isnan(lo):
        testo += f" [{_num(100 * lo, dec, True)}; {_num(100 * hi, dec, True)}]"
    return _livello(nmin, testo)


def _tabella(intestazione, righe):
    linee = ["| " + " | ".join(intestazione) + " |", "| " + " | ".join(["---"] * len(intestazione)) + " |"]
    linee += ["| " + " | ".join(str(c) for c in r) + " |" for r in righe]
    return "\n".join(linee)


def _breve(carta):
    return {"Phelia, Exuberant Shepherd": "Phelia", "Teferi, Time Raveler": "Teferi",
            "March of Otherworldly Light": "March", "Recruiter of the Guard": "Recruiter"}.get(carta, carta)


def _breve_b(d):
    """La seconda carta di una coppia, con la variante "in mano per qualsiasi via"."""
    return _breve(d["b"]) + (" (I, P o C)" if d.get("variante") == VARIANTE_IPC else "")


def documento(res, desc, terre, mazzi, ctrl, info, turni, abl=None):
    c = res.carte
    s = []
    s.append("# Risultati delle metriche per carta: euristici v1\n")
    s.append(f"Generato da `python/metriche/metriche_carte.py` (passo 2 di [metriche.md](metriche.md), sezioni 3-5). "
             f"{_intero(info['partite'])} partite (seed da {info['seed_min']} a {_intero(info['seed_max'])}), "
             f"UW base_v2 contro Rakdos base_r3; `record.jsonl` MD5 `{info.get('md5_record', '?')}`. "
             f"Bootstrap sulle partite: B = {_intero(info['b'])}, seed {info['seed_boot']}.\n")
    s.append("**Come si legge.** Win rate (WR) del giocatore che possiede la carta, in percentuale, con l'intervallo "
             "al 95% (Wilson per le proporzioni, bootstrap per le differenze, in punti). Valori in *corsivo*: "
             "indicativi (meno di 1.000 partite; per una differenza, nel gruppo più piccolo); \"—\": meno di 200, "
             "non mostrati. Le metriche misurano **come giocano gli agenti euristici v1**, non il valore della carta "
             "per un pilota; sono **descrittive, non causali**: il valore causale lo darà l'ablazione (passo 2b).\n")
    s.append("Classi (sezione 3): **OH** almeno una copia nella mano iniziale tenuta; **GD** nessuna nella mano, "
             "almeno una pescata; **GIH** = OH ∪ GD; **solo cercata**: arrivata solo con una ricerca; **solo "
             "cimitero**: usata solo dal cimitero (Nethergoyf, Phoenix); **GNS** mai vista. IWD = WR GIH − WR GNS. "
             "**IWD relativo**: IWD meno la media degli IWD del mazzo pesata per le partite GIH. **IWD T*n***: al "
             "turno *n* del giocatore, tra le partite ancora in corso, WR di chi l'ha vista (in mano o pescata) entro "
             "quel turno meno chi non l'ha vista in nessun modo. **OH7**: almeno una copia nella prima mano da 7, "
             "tenuta o no.\n")
    for chi in ("uw", "rakdos"):
        nome = "UW Blink" if chi == "uw" else "Rakdos Delirium"
        cc = c[c["giocatore"] == chi]
        s.append(f"\n## {nome}\n")
        s.append("### Classi della partizione\n")
        s.append(_tabella(
            ["Carta", "Copie", "OH n", "OH WR", "GD n", "GD WR", "GIH n", "GIH WR", "GNS n", "GNS WR",
             "Solo cercata n (WR)", "Solo cimitero n (WR)"],
            [[_breve(r.carta), mazzi[chi][r.carta], _intero(r.n_OH), fmt_wr(r.n_OH, r.wr_OH, r.wr_OH_lo, r.wr_OH_hi),
              _intero(r.n_GD), fmt_wr(r.n_GD, r.wr_GD, r.wr_GD_lo, r.wr_GD_hi), _intero(r.n_GIH),
              fmt_wr(r.n_GIH, r.wr_GIH, r.wr_GIH_lo, r.wr_GIH_hi), _intero(r.n_GNS),
              fmt_wr(r.n_GNS, r.wr_GNS, r.wr_GNS_lo, r.wr_GNS_hi),
              f"{_intero(r.n_cercata)} ({fmt_wr(r.n_cercata, r.wr_cercata, r.wr_cercata_lo, r.wr_cercata_hi)})",
              f"{_intero(r.n_cimitero)} ({fmt_wr(r.n_cimitero, r.wr_cimitero, r.wr_cimitero_lo, r.wr_cimitero_hi)})"]
             for r in cc.itertuples()]))
        s.append("\n### IWD (punti)\n")
        intest = ["Carta", "IWD classico", "IWD relativo"] + [f"IWD T{t}" for t in turni] + \
                 [f"IWD T{t} relativo" for t in turni[1:2]]
        righe = []
        for r in cc.itertuples():
            d = r._asdict()
            riga = [_breve(r.carta), fmt_diff(d["nmin_iwd"], d["iwd"], d.get("iwd_lo"), d.get("iwd_hi")),
                    fmt_diff(d["nmin_iwd"], d["iwd_rel"], d.get("iwd_rel_lo"), d.get("iwd_rel_hi"))]
            for t in turni:
                riga.append(fmt_diff(d[f"nmin_iwd_t{t}"], d[f"iwd_t{t}"], d.get(f"iwd_t{t}_lo"), d.get(f"iwd_t{t}_hi")))
            t = turni[1]
            riga.append(fmt_diff(d[f"nmin_iwd_t{t}"], d[f"iwd_t{t}_rel"], d.get(f"iwd_t{t}_rel_lo"),
                                 d.get(f"iwd_t{t}_rel_hi")))
            righe.append(riga)
        s.append(_tabella(intest, righe))
        s.append("\nPartite nei gruppi a turno fisso (vista entro il turno / non vista; le altre partite ancora in "
                 "corso l'hanno vista solo con una ricerca o dal cimitero):\n")
        s.append(_tabella(["Carta"] + [f"T{t} vista / non vista" for t in turni],
                          [[_breve(r.carta)] + [f"{_intero(getattr(r, f'n_t{t}_si'))} / "
                                                f"{_intero(getattr(r, f'n_t{t}_no'))}" for t in turni]
                           for r in cc.itertuples()]))
        s.append("\n### In play e in draw (punti)\n")
        t = turni[1]
        s.append(_tabella(
            ["Carta", "IWD in play", "IWD in draw", f"IWD T{t} in play", f"IWD T{t} in draw"],
            [[_breve(r.carta)] + [fmt_diff(d[f"nmin_{m}"], d[m], d.get(f"{m}_lo"), d.get(f"{m}_hi"))
                                  for m in ("iwd_play", "iwd_draw", f"iwd_t{t}_play", f"iwd_t{t}_draw")]
             for r in cc.itertuples() for d in [r._asdict()]]))
        s.append("\n### Prima mano da 7 (OH7) e curva delle copie viste\n")
        s.append(_tabella(
            ["Carta", "OH7 n", "WR con OH7", "WR senza", "Differenza (punti)", "0 copie", "1 copia", "2 copie",
             "3+ copie"],
            [[_breve(r.carta), _intero(r.n_OH7), fmt_wr(r.n_OH7, r.wr_OH7, r.wr_OH7_lo, r.wr_OH7_hi),
              fmt_wr(r.n_non_OH7, r.wr_non_OH7, r.wr_non_OH7_lo, r.wr_non_OH7_hi),
              fmt_diff(d["nmin_oh7_diff"], d["oh7_diff"], d.get("oh7_diff_lo"), d.get("oh7_diff_hi"))]
             + [f"{_intero(d[f'n_copie_{k}'])}: {fmt_wr(d[f'n_copie_{k}'], d[f'wr_copie_{k}'], d[f'wr_copie_{k}_lo'], d[f'wr_copie_{k}_hi'])}"
                if k != "3+" or mazzi[chi][r.carta] >= 3 else "—" for k in ("0", "1", "2", "3+")]
             # to_dict e non _asdict: namedtuple rinomina i campi non validi come "n_copie_3+".
             for r, d in zip(cc.itertuples(), cc.to_dict("records"))]))
        s.append("\n### Descrittivi (non misure di valore)\n")
        dd = desc[desc["giocatore"] == chi]
        s.append("Per copia vista: quota lanciata (dalla mano, dal cimitero o dall'esilio), turno proprio del primo "
                 "lancio, quota rimasta in mano a fine partita; partite con almeno una copia in mano alla fine; usi "
                 "(quota delle copie viste con almeno un evento: T giocata come terra, E esiliata come costo, S "
                 "scartata, K ciclata, M macinata).\n")
        s.append(_tabella(
            ["Carta", "Copie viste", "Lanciate", "Turno del lancio (media; mediana)", "In mano alla fine",
             "Partite con la carta in mano alla fine", "T", "E", "S", "K", "M"],
            [[_breve(r.carta), _intero(r.copie_viste), _pct(r.lanciate, r.copie_viste),
              f"{_num(r.turno_medio_lancio, 2)}; {_num(r.turno_mediano_lancio, 0)}" if not math.isnan(r.turno_medio_lancio) else "—",
              _pct(r.copie_mano_fine, r.copie_viste), _intero(r.partite_mano_fine)]
             + [_pct(getattr(r, f"k_{k}"), r.copie_viste) for k in "TESKM"]
             for r in dd.itertuples()]))
    # Coppie
    cp = res.coppie
    s.append("\n## Coppie decise prima (sezione 4)\n")
    s.append("Celle tra le partite in cui ognuna delle due carte è GIH o GNS: WR di AB (entrambe viste), A senza B, "
             "B senza A, nessuna. **Sinergia** = (WR AB − WR A senza B) − (WR B senza A − WR nessuna), in punti; "
             "**IWD di A con B** = WR AB − WR B senza A; **senza B** = WR A senza B − WR nessuna. Sinergia a turno "
             "fisso: le stesse celle con \"vista entro il turno *n*\". **B (I, P o C)**: B conta come vista se è "
             "arrivata in mano per qualsiasi via, anche cercata (Recruiter + Solitude: una Solitude cercata è fuori da "
             "GIH per definizione, quindi la coppia normale non misura D-2c); \"non vista\" resta GNS.\n")
    dec = cp[cp["decisa"]]
    s.append(_tabella(
        ["Mazzo", "A", "B", "n AB / A / B / nessuna", "WR AB", "WR A senza B", "WR B senza A", "WR nessuna"],
        [[ETICHETTA[d["giocatore"]], _breve(d["a"]), _breve_b(d),
          " / ".join(_intero(d[f"n_{k}"]) for k in CELLE)]
         + [fmt_wr(d[f"n_{k}"], d[f"wr_{k}"], d[f"wr_{k}_lo"], d[f"wr_{k}_hi"]) for k in CELLE]
         for d in dec.to_dict("records")]))
    s.append("")
    s.append(_tabella(
        ["Mazzo", "A", "B", "Sinergia", "IWD di A con B", "IWD di A senza B"] + [f"Sinergia T{t}" for t in turni],
        [[ETICHETTA[d["giocatore"]], _breve(d["a"]), _breve_b(d),
          fmt_diff(d["nmin_sinergia"], d["sinergia"], d.get("sinergia_lo"), d.get("sinergia_hi")),
          fmt_diff(min(d["n_AB"], d["n_nAB"]), d["iwd_a_con_b"], d.get("iwd_a_con_b_lo"), d.get("iwd_a_con_b_hi")),
          fmt_diff(min(d["n_AnB"], d["n_nAnB"]), d["iwd_a_senza_b"], d.get("iwd_a_senza_b_lo"),
                   d.get("iwd_a_senza_b_hi"))]
         + [fmt_diff(d[f"nmin_sinergia_t{t}"], d[f"sinergia_t{t}"], d.get(f"sinergia_t{t}_lo"),
                     d.get(f"sinergia_t{t}_hi")) for t in turni]
         for d in (r._asdict() for r in dec.itertuples())]))
    esp = cp[~cp["decisa"]]
    tm = turni[len(turni) // 2]
    if len(esp) and f"bh_5_t{tm}" in esp.columns:
        s.append("\n### Matrice esplorativa\n")
        s.append(f"Tutte le altre coppie di carte non terra dello stesso mazzo: solo come mappa, con "
                 f"Benjamini-Hochberg al 5% per mazzo (p bilaterale dalla deviazione standard del bootstrap), senza "
                 f"conclusioni sulle coppie che non erano nella lista. La sinergia **classica** ha lo stesso difetto "
                 f"della lunghezza della partita dell'IWD (le partite in cui si vedono tutte e due le carte sono le più "
                 f"lunghe): la mappa usa quella **al turno {tm}**, e la classica serve solo a mostrare il difetto. "
                 f"**Le sinergie negative al turno {tm} possono essere un artefatto di selezione:** si condiziona sulle "
                 f"partite ancora in corso e sull'aver visto (o no) entrambe le carte entro quel turno; per quelle "
                 f"coppie vale solo l'ablazione di coppia (sezione \"Ablazione\").\n")
        s.append(_tabella(["Mazzo", "Coppie", "Oltre la soglia, classica (positive / negative)",
                           f"Oltre la soglia, T{tm} (positive / negative)"],
                          [[ETICHETTA[chi], len(e)] + [
                              f"{int(e[col].sum())} ({int((e[col] & (e[val] > 0)).sum())} / "
                              f"{int((e[col] & (e[val] < 0)).sum())})"
                              for col, val in (("bh_5", "sinergia"), (f"bh_5_t{tm}", f"sinergia_t{tm}"))]
                           for chi in ("uw", "rakdos") for e in [esp[esp["giocatore"] == chi]]]))
        for chi in ("uw", "rakdos"):
            e = esp[esp["giocatore"] == chi]
            sig = e[e[f"bh_5_t{tm}"]].copy()
            sig["ordine"] = -sig[f"sinergia_t{tm}"].abs()
            sig = sig.sort_values("ordine")
            if len(sig):
                s.append(f"\n**{ETICHETTA[chi]}, coppie oltre la soglia al turno {tm}** (per valore assoluto)\n")
                s.append(_tabella(
                    ["A", "B", f"Sinergia T{tm}", "Sinergia classica"],
                    [[_breve(d["a"]), _breve(d["b"]),
                      fmt_diff(d[f"nmin_sinergia_t{tm}"], d[f"sinergia_t{tm}"], d[f"sinergia_t{tm}_lo"],
                               d[f"sinergia_t{tm}_hi"]),
                      fmt_diff(d["nmin_sinergia"], d["sinergia"], d["sinergia_lo"], d["sinergia_hi"])]
                     for d in sig.to_dict("records")]))
    # Bias
    s.append("\n## Il difetto della lunghezza della partita (sezione 3)\n")
    s.append("Media degli IWD delle carte non terra di ciascun mazzo, pesata per le partite del gruppo \"vista\" "
             "(GIH per l'IWD classico), e differenza UW − Rakdos, in punti. UW vince le partite lunghe, in cui si "
             "vedono più carte: l'IWD classico gonfia le carte di UW e sgonfia quelle di Rakdos. L'IWD relativo toglie "
             "la media per costruzione (zero); l'IWD a turno fisso la toglie confrontando partite arrivate allo stesso "
             "turno.\n")
    nomi_m = {"iwd": "IWD classico", "iwd_rel": "IWD relativo"}
    nomi_m.update({f"iwd_t{t}": f"IWD T{t}" for t in turni})
    s.append(_tabella(["Metrica", "Media UW", "Media Rakdos", "UW − Rakdos"],
                      [[nomi_m[d["metrica"]]] + (["0 per costruzione"] * 3 if d["metrica"].endswith("_rel") else
                                                 [fmt_diff(10**6, d[k], d.get(f"{k}_lo"), d.get(f"{k}_hi"), 2)
                                                  for k in ("uw", "rakdos", "differenza")])
                       for d in res.bias.to_dict("records")]))
    # Terre
    s.append("\n## Terre (fuori dalla tabella principale)\n")
    s.append("WR del giocatore per terre nella mano iniziale tenuta (5 = 5 o più) e per il primo turno proprio "
             "chiuso con meno terre in campo del numero del turno (7 = 7 o più; \"mai\": nessuna terra mancata "
             "finché la partita è durata). Anche queste sono descrizioni: la prima terra mancata dipende dalla "
             "durata della partita.\n")
    for chi in ("uw", "rakdos"):
        tt = terre[terre["giocatore"] == chi]
        for misura in tt["misura"].unique():
            m = tt[tt["misura"] == misura]
            valori = sorted(m["valore"].unique())
            etich = {99: "mai"}
            s.append(f"\n**{ETICHETTA[chi]}, {misura}**\n")
            s.append(_tabella(
                ["", *[etich.get(v, str(v) + ("+" if (v == 5 and "mano" in misura) or (v == 7 and "mancata" in misura)
                                               else "")) for v in valori]],
                [[lato] + [f"{_intero(x.n)}: {fmt_wr(x.n, x.wr, x.wr_lo, x.wr_hi)}"
                           for v in valori for x in m[(m["valore"] == v) & (m["lato"] == lato)].itertuples()]
                 for lato in ("tutte", "play", "draw")]))
    if abl is not None:
        s.append(sezione_ablazione(abl, mazzi, res.bias))
    s.append("\n## Controlli di coerenza (sezione 11)\n")
    s.append("```text\n" + "\n".join(ctrl) + "\n```")
    if info.get("tempi"):
        s.append("\n## Tempi\n")
        s.append(_tabella(["Passo", "Tempo"], [[k, v] for k, v in info["tempi"].items()]))
    return "\n".join(s) + "\n"


NOMI_METRICA = {"iwd": "IWD classico", "iwd_t3": "IWD T3", "iwd_t5": "IWD T5", "iwd_t7": "IWD T7"}


def sezione_ablazione(abl, mazzi, bias):
    """La sezione dell'ablazione (passo 2b) di risultati_v1.md."""
    s = ["\n## Ablazione (passo 2b, aggiunta A)\n"]
    e = abl["carte"]
    n = int(e["n"].iloc[0]) if len(e) else 0
    s.append(f"Per ogni carta non terra, gli stessi {_intero(n)} seed della base con **una copia sostituita da una "
             "terra base** (Plains per UW, Swamp per Rakdos) nella stessa posizione della lista (`metriche.exe "
             "--sostituisci`). **Effetto** = WR del giocatore che possiede la carta con la lista vera meno con la "
             "copia tolta, appaiato per seed, in punti; intervallo al 95% dal bootstrap sui seed (la stessa matrice di "
             "pesi delle tabelle sopra) e, accanto, dalla formula normale appaiata. È il valore causale della carta "
             "**rispetto a una terra in più**, per questi agenti; i limiti sono nella sezione 17 di "
             "[metriche.md](metriche.md). \"Esiti diversi\": seed in cui cambia il vincitore.\n")
    s.append("**Lista di simulazione:** UW gioca `uw_blink_sim.json`, con 4 Witch Enchanter e 3 Ephemerate al posto di "
             "Aang, Swift Savior e Mockingbird (la lista vera ha 3 Witch e 2 Ephemerate). L'effetto è quello dell'ultima "
             "copia: il valore di Witch (la quarta copia) e della terza Ephemerate va letto con cautela per la lista "
             "vera.\n")
    for chi in ("uw", "rakdos"):
        cc = e[e["giocatore"] == chi]
        s.append(f"\n### {ETICHETTA[chi]}\n")
        s.append(_tabella(
            ["Carta", "Copie", "Effetto", "IC normale", "In play", "In draw", "Esiti diversi", "IWD T5",
             "IWD classico"],
            [[_breve(d["carta"]), mazzi[chi][d["carta"]],
              fmt_diff(n, d["effetto"], d["effetto_lo"], d["effetto_hi"], 2),
              f"[{_num(100 * d['effetto_norm_lo'], 2, True)}; {_num(100 * d['effetto_norm_hi'], 2, True)}]",
              fmt_diff(n, d["effetto_play"], d["effetto_play_lo"], d["effetto_play_hi"], 2),
              fmt_diff(n, d["effetto_draw"], d["effetto_draw_lo"], d["effetto_draw_hi"], 2),
              f"{_intero(d['esiti_diversi'])} ({_num(100 * d['esiti_diversi'] / n)}%)",
              fmt_diff(n, d["iwd_t5"]), fmt_diff(n, d["iwd"])]
             for d in cc.to_dict("records")]))
    s.append("\n**Media degli effetti per mazzo** (media semplice sulle carte non terra, in punti) e, accanto, la "
             "media degli IWD dello stesso mazzo (pesata per le partite del gruppo \"vista\", come sopra):\n")
    b = bias.set_index("metrica")
    m = abl["medie"].set_index("giocatore")
    s.append(_tabella(
        ["Mazzo", "Carte", "Ablazione", "IWD classico", "IWD T3", "IWD T5", "IWD T7"],
        [[ETICHETTA[chi], int(m.loc[chi, "carte"]),
          fmt_diff(10**6, m.loc[chi, "media"], m.loc[chi].get("media_lo"), m.loc[chi].get("media_hi"), 2)]
         + [fmt_diff(10**6, b.loc[k, chi], b.loc[k].get(f"{chi}_lo"), b.loc[k].get(f"{chi}_hi"), 2)
            for k in ("iwd", "iwd_t3", "iwd_t5", "iwd_t7")]
         for chi in ("uw", "rakdos")]))
    cp = abl["coppie"]
    if len(cp):
        s.append("\n### Ablazione di coppia\n")
        s.append("Le due carte sostituite insieme (una copia ciascuna). **Interazione** = effetto(A e B) − effetto(A) − "
                 "effetto(B), in punti: positiva se togliere le due carte insieme costa più della somma dei due costi "
                 "(si potenziano a vicenda), negativa se si sostituiscono a vicenda. Coppie scelte dalla sinergia "
                 "osservazionale al turno 5 (le 5 più forti e le 5 più negative, dai due mazzi insieme), più "
                 "Recruiter + Solitude e Solitude + Ephemerate.\n")
        s.append(_tabella(
            ["Mazzo", "A", "B", "Scelta", "Sinergia T5 (osservazionale)", "Effetto A", "Effetto B", "Effetto A e B",
             "Interazione", "IC normale", "In play", "In draw"],
            [[ETICHETTA[d["giocatore"]], _breve(d["a"]), _breve(d["b"]), d["motivo"],
              fmt_diff(n, d["sinergia_t5"], d["sinergia_t5_lo"], d["sinergia_t5_hi"]),
              fmt_diff(n, d["effetto_a"], dec=2), fmt_diff(n, d["effetto_b"], dec=2),
              fmt_diff(n, d["effetto_ab"], d["effetto_ab_lo"], d["effetto_ab_hi"], 2),
              fmt_diff(n, d["interazione"], d["interazione_lo"], d["interazione_hi"], 2),
              f"[{_num(100 * d['interazione_norm_lo'], 2, True)}; {_num(100 * d['interazione_norm_hi'], 2, True)}]",
              fmt_diff(n, d["interazione_play"], d["interazione_play_lo"], d["interazione_play_hi"], 2),
              fmt_diff(n, d["interazione_draw"], d["interazione_draw_lo"], d["interazione_draw_hi"], 2)]
             for d in cp.to_dict("records")]))
    s.append("\n### Ablazione contro IWD\n")
    s.append("Correlazione di Spearman tra l'effetto dell'ablazione e l'IWD delle stesse carte, per mazzo e per i due "
             "mazzi insieme; intervallo dal bootstrap congiunto (ogni ricampionamento usa gli stessi seed per "
             "l'ablazione e per l'IWD). Con 16 e 12 carte l'intervallo è largo.\n")
    cf = abl["confronto"]
    gruppi = ("UW", "Rakdos", "Tutte")
    s.append(_tabella(
        ["Metrica", "UW (16 carte)", "Rakdos (12 carte)", "Tutte (28 carte)"],
        [[NOMI_METRICA[met]] + [
            (lambda r: f"{_num(r['rho'], 2, True)} [{_num(r['rho_lo'], 2, True)}; {_num(r['rho_hi'], 2, True)}]"
             if "rho_lo" in r and not math.isnan(r["rho_lo"]) else _num(r["rho"], 2, True))(
                cf[(cf["metrica"] == met) & (cf["gruppo"] == g)].iloc[0].to_dict()) for g in gruppi]
         for met in NOMI_METRICA]))
    dis = abl["disaccordi"]
    segno = int((np.sign(e["effetto"]) == np.sign(e["iwd_t5"])).sum())
    centrato = e["effetto"] - e.groupby("giocatore")["effetto"].transform("mean")
    segno_rel = int((np.sign(centrato) == np.sign(e["iwd_t5_rel"])).sum())
    s.append(f"\nStesso segno di ablazione e IWD T5: **{segno} carte su {len(e)}**; tolta a tutte e due la media del "
             f"mazzo (ablazione meno la media, IWD T5 relativo): **{segno_rel} su {len(e)}**.\n")
    s.append(f"\n**Carte in cui ablazione e IWD T5 non concordano**: segno diverso, o rango nel mazzo lontano almeno "
             f"un terzo delle posizioni ({ablazione.soglia_rango(16)} per UW, {ablazione.soglia_rango(12)} per "
             f"Rakdos). Rango 1 = la più alta del mazzo. \"Segno diverso, relativi\": lo stesso confronto con "
             f"l'ablazione meno la media del mazzo e l'IWD T5 relativo (l'IWD T5 ha uno scarto positivo comune a tutte "
             f"le carte). Gli indizi per la spiegazione: quota delle copie viste "
             f"rimaste in mano a fine partita (agente passivo), partite \"solo cercata\" (carta cercata), IWD classico "
             f"− IWD T5 (lunghezza della partita), copie nella lista (con una copia l'ablazione toglie la carta).\n")
    if len(dis):
        s.append(_tabella(
            ["Mazzo", "Carta", "Ablazione", "IWD T5", "Rango ablazione / IWD T5", "Segno diverso",
             "Segno diverso, relativi", "In mano alla fine", "Solo cercata n", "IWD classico − T5", "Copie"],
            [[ETICHETTA[d["giocatore"]], _breve(d["carta"]), fmt_diff(n, d["ablazione"], dec=2),
              fmt_diff(n, d["iwd_t5"]),
              f"{_num(d['rango_ablazione'], 0)} / {_num(d['rango_iwd'], 0)}", "sì" if d["segno_diverso"] else "no",
              "sì" if d["segno_relativo_diverso"] else "no",
              f"{_num(100 * d['mano_fine'])}%", _intero(d["n_cercata"]), fmt_diff(n, d["iwd"] - d["iwd_t5"]),
              mazzi[d["giocatore"]][d["carta"]]]
             for d in dis.to_dict("records")]))
    return "\n".join(s)


def _pct(k, n):
    return "—" if n == 0 else f"{_num(100 * k / n)}%"


def non_terre(mazzi):
    terre = {c["name"] for c in json.loads((RADICE / "config/cards.json").read_text(encoding="utf-8"))["cards"]
             if "Land" in c["types"]}
    return {chi: [c for c in m if c not in terre] for chi, m in mazzi.items()}


RADICE = Path(__file__).resolve().parents[2]


def main(argv=None):
    from metriche import converti
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dati", default=str(RADICE / "data/metriche/v1"))
    ap.add_argument("--doc", default=str(RADICE / "docs/risultati_v1.md"))
    ap.add_argument("--partite-matchup", default=str(RADICE / "data/matchup/partite.jsonl"))
    ap.add_argument("--carte-morte", default=str(RADICE / "data/matchup/carte_morte.csv"))
    ap.add_argument("--tempi", default=None, help="JSON {passo: tempo} da riportare nel documento")
    ap.add_argument("--md5-record", default=None)
    ap.add_argument("--b", type=int, default=st.B_BOOTSTRAP)
    args = ap.parse_args(argv)
    dati = Path(args.dati)
    t0 = time.perf_counter()
    partite = pd.read_parquet(dati / "partite.parquet").sort_values("seed").reset_index(drop=True)
    carte = pd.read_parquet(dati / "carte.parquet")
    t_lettura = time.perf_counter() - t0
    mazzi = converti.leggi_mazzi()
    nomi = non_terre(mazzi)
    turni = (3, 5, 7)
    t1 = time.perf_counter()
    res = calcola(partite, carte, nomi, COPPIE_DECISE, turni=turni, b=args.b, seed=st.SEED_BOOTSTRAP, matrice=True,
                  coppie_ipc=COPPIE_IPC)
    t_calcolo = time.perf_counter() - t1
    desc = descrittivi(carte, nomi)
    terre = tabelle_terre(partite)
    pm = Path(args.partite_matchup) if args.partite_matchup and Path(args.partite_matchup).exists() else None
    cm = Path(args.carte_morte) if args.carte_morte and Path(args.carte_morte).exists() else None
    ctrl, falliti = controlli(partite, carte, mazzi, res, pm, cm)
    uscite = [("metriche_carte", res.carte), ("coppie", res.coppie), ("media_per_mazzo", res.bias),
              ("descrittivi", desc), ("terre", terre)]
    # Ablazione (passo 2b): se ci sono le partite di ablazione.py gioca, la stessa matrice di pesi del bootstrap.
    abl, t_abl = None, None
    if (dati / "ablazione" / "esecuzioni.json").exists():
        t2 = time.perf_counter()
        abl = ablazione.analizza(dati / "ablazione", partite[["seed", "uw_in_play", "vincitore"]], res, nomi, desc,
                                 ablazione.md5(dati / "partite.jsonl"), b=args.b, seed=st.SEED_BOOTSTRAP)
        t_abl = time.perf_counter() - t2
        ctrl += abl["controlli"]
        falliti += abl["falliti"]
        uscite += [(f"ablazione_{k}", abl[k]) for k in ("carte", "coppie", "confronto", "disaccordi", "medie")]
    for nome, df in uscite:
        df.to_csv(dati / f"{nome}.csv", index=False)
    t_tot = time.perf_counter() - t0
    tempi = json.loads(Path(args.tempi).read_text(encoding="utf-8")) if args.tempi else {}
    tempi[f"Metriche (lettura Parquet {_num(t_lettura)} s, calcolo con il bootstrap {_num(t_calcolo)} s)"] = \
        f"{_num(t_tot)} s"
    if abl is not None:
        tempi[f"Ablazione: {abl['tempi']['esecuzioni']} esecuzioni di metriche.exe, "
              f"{_intero(abl['tempi']['partite'])} partite (8 processi)"] = f"{_num(abl['tempi']['secondi'])} s"
        tempi["Ablazione: analisi (lettura delle partite, bootstrap)"] = f"{_num(t_abl)} s"
    info = {"partite": len(partite), "seed_min": int(partite["seed"].min()), "seed_max": int(partite["seed"].max()),
            "b": args.b, "seed_boot": st.SEED_BOOTSTRAP, "md5_record": args.md5_record, "tempi": tempi}
    Path(args.doc).write_text(documento(res, desc, terre, mazzi, ctrl, info, turni, abl), encoding="utf-8")
    (dati / "controlli.txt").write_text("\n".join(ctrl) + "\n", encoding="utf-8")
    print("\n".join(ctrl))
    print(f"Tempo: lettura {t_lettura:.1f} s, calcolo {t_calcolo:.1f} s, totale {t_tot:.1f} s; controlli falliti {falliti}")
    return 0 if falliti == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
