# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "numpy==2.5.5",
#   "pandas==3.1.1",
#   "pyarrow==26.0.1",
#   "statsmodels==0.15.0",
# ]
# ///
"""Mulligan descrittivo e contestuale degli agenti euristici v1 (docs/metriche.md, sezioni 8 e 9; passo 4).

Per ogni seed `metriche.exe --mulligan-forzato` gioca due partite: K con la prima decisione di mulligan del giocatore
forzata a "tieni", M a "mulligan"; Delta = vittoria(K) - vittoria(M) e' il valore di tenere quella prima mano da 7,
per questi agenti. Qui: le caratteristiche delle mani (9.1), Delta per tipo di mano con la scelta dell'agente
accanto (9.3), la "settima carta", la regressione logistica e la politica che ne viene (9.4), Witch Enchanter (9.5),
Recruiter e Riddler (D-2c) e il confronto con D-2, M1, M1-R e M3 (9.6); il mulligan descrittivo (8).

Uso:
    uv run --no-project python/metriche/mulligan.py gioca [--exe build/Release/metriche.exe] [--processi 8]
    uv run --no-project python/metriche/mulligan.py analizza [--doc docs/risultati_v1_mulligan.md]
"""

import argparse
import json
import math
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import statistica as st  # noqa: E402

RADICE = Path(__file__).resolve().parents[2]
ETICHETTA = {"uw": "UW", "rakdos": "Rakdos"}

# --- Caratteristiche delle mani (sezione 9.1) ----------------------------------------------------------------

WITCH = "Witch Enchanter"
UNO_MANA_UW = ("Guide of Souls", "Ocelot Pride")
DUE_MANA_UW = ("Phelia, Exuberant Shepherd", "Sanctifier en-Vec")
RISPOSTE_DIRETTE = ("Solitude", "March of Otherworldly Light", "Sanctifier en-Vec", "Seam Rip", "Teferi, Time Raveler")
RISPOSTE_INDIRETTE = ("Recruiter of the Guard", "Quantum Riddler")
# Giocate entro il turno 2 di D-2 (agente_uw.md, sezione 2): creature da 1-2 mana, Seam Rip, il warp di Shepherd.
GIOCATE_UW = UNO_MANA_UW + DUE_MANA_UW + ("Seam Rip", "Starfield Shepherd")
# Carte singole della "settima carta" (decisione 8): colonna c_<nome breve>.
SINGOLE_UW = {"recruiter": "Recruiter of the Guard", "witch": WITCH, "phelia": "Phelia, Exuberant Shepherd",
              "ephemerate": "Ephemerate", "riddler": "Quantum Riddler", "consign": "Consign to Memory",
              "force": "Force of Negation", "teferi": "Teferi, Time Raveler", "shepherd": "Starfield Shepherd",
              "thraben": "Thraben Charm", "solitude": "Solitude", "march": "March of Otherworldly Light",
              "sanctifier": "Sanctifier en-Vec", "seam_rip": "Seam Rip", "guide": "Guide of Souls",
              "ocelot": "Ocelot Pride"}
SETTIMA = ("recruiter", "witch", "phelia", "ephemerate", "riddler", "consign", "force", "teferi", "shepherd", "thraben")
UNO_MANA_RK = ("Stitcher's Supplier", "Dark Matter Manipulator", "Moonshadow", "Nethergoyf")
CERCANO_RK = ("Street Wraith", "Mishra's Bauble", "Troll of Khazad-dûm")
SINGOLE_RK = {"thoughtseize": "Thoughtseize", "bolt": "Lightning Bolt", "flare": "Flare of Malice",
              "fomo": "Fear of Missing Out", "phoenix": "Detective's Phoenix"}


@dataclass
class InfoCarte:
    terre: set
    bianche: set
    terre_mazzo: dict  # giocatore -> terre nella lista (Witch compresa per UW)

    @classmethod
    def carica(cls):
        carte = json.loads((RADICE / "config/cards.json").read_text(encoding="utf-8"))["cards"]
        terre = {c["name"] for c in carte if "Land" in c["types"]}
        bianche = {c["name"] for c in carte if "W" in (c.get("colors") or [])}
        mazzi = {}
        for chi, f in (("uw", "uw_blink_sim.json"), ("rakdos", "rakdos_delirium.json")):
            lista = json.loads((RADICE / "config/decks" / f).read_text(encoding="utf-8"))["main"]
            mazzi[chi] = sum(v["count"] for v in lista if v["card"] in terre or (chi == "uw" and v["card"] == WITCH))
        return cls(terre, bianche, mazzi)


def p_terre(in_mano, in_play, terre_mazzo, servono=3, mazzo=60, mano=7):
    """P(almeno `servono` terre entro il proprio turno 3): le terre in mano piu' quelle pescate (2 in play, 3 in
    draw) dalle 53 carte restanti, ipergeometrica. Le ricerche non contano."""
    pescate = 2 if in_play else 3
    resto, terre_resto = mazzo - mano, terre_mazzo - in_mano
    mancano = servono - in_mano
    if mancano <= 0:
        return 1.0
    tot = math.comb(resto, pescate)
    return sum(math.comb(terre_resto, k) * math.comb(resto - terre_resto, pescate - k)
               for k in range(mancano, pescate + 1)) / tot


def caratteristiche_uw(mano, in_play, info):
    n = {k: sum(c == v for c in mano) for k, v in SINGOLE_UW.items()}
    terre = sum(c in info.terre for c in mano)
    coppia = n["solitude"] > 0 and n["ephemerate"] > 0
    car = {"in_play": in_play, "terre": terre, "witch": n["witch"], "terre_totali": terre + n["witch"],
           "uno_mana": sum(c in UNO_MANA_UW for c in mano), "due_mana": sum(c in DUE_MANA_UW for c in mano),
           "risposte_dirette": sum(c in RISPOSTE_DIRETTE for c in mano),
           "risposte_indirette": sum(c in RISPOSTE_INDIRETTE for c in mano),
           "giocata": any(c in GIOCATE_UW for c in mano) or coppia, "coppia": coppia,
           "bianche": sum(c in info.bianche and c != "Solitude" for c in mano),
           "p3": p_terre(terre + n["witch"], in_play, info.terre_mazzo["uw"])}
    car["risposta"] = car["risposte_dirette"] > 0
    car.update({f"c_{k}": v for k, v in n.items()})
    return car


def _cat_terre(n):
    return "5+" if n >= 5 else str(n)


def tipo_uw(c):
    """Tipo di mano (sezione 9.3): terre (Witch compresa), risposta diretta, giocata entro il turno 2, play o draw."""
    return _cat_terre(c["terre_totali"]), bool(c["risposta"]), bool(c["giocata"]), "play" if c["in_play"] else "draw"


def caratteristiche_rakdos(mano, in_play, info):
    car = {"in_play": in_play, "terre": sum(c in info.terre for c in mano),
           "uno_mana": sum(c in UNO_MANA_RK for c in mano), "cercano": sum(c in CERCANO_RK for c in mano),
           "p3": p_terre(sum(c in info.terre for c in mano), in_play, info.terre_mazzo["rakdos"])}
    car.update({f"c_{k}": sum(c == v for c in mano) for k, v in SINGOLE_RK.items()})
    return car


def tipo_rakdos(c):
    """Tipo di mano di Rakdos: terre, creatura da un mana, carte che cercano (0, 1, 2+), play o draw."""
    return (_cat_terre(c["terre"]), c["uno_mana"] > 0, "2+" if c["cercano"] >= 2 else str(c["cercano"]),
            "play" if c["in_play"] else "draw")


# --- Delta, gruppi e politiche (sezioni 9.2, 9.3, 9.6) -------------------------------------------------------

def _stima_gruppi(d, maschere, b, seed):
    """Media di d nei gruppi (maschere booleane sulle stesse righe): stima, intervallo bootstrap sulle righe (seed)
    con la matrice di pesi condivisa, intervallo normale appaiato."""
    d = np.asarray(d, dtype=float)
    out = []
    sb = None
    if b > 0 and maschere:
        x = np.column_stack([c for m in maschere for c in (np.where(m, d, 0), m.astype(np.int8))])
        sb = st.somme_bootstrap(x, b, seed)
    for j, m in enumerate(maschere):
        dm = d[m]
        stima = float(dm.mean()) if len(dm) else math.nan
        nlo = nhi = lo = hi = math.nan
        if len(dm) > 1:
            _, nlo, nhi = st.normale_appaiata(dm)
        if sb is not None:
            with np.errstate(divide="ignore", invalid="ignore"):
                lo, hi = st.percentili(sb[:, 2 * j] / sb[:, 2 * j + 1])
        out.append((stima, lo, hi, nlo, nhi, int(m.sum())))
    return out


def delta_per_gruppo(df, chiavi, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP):
    """Per gruppo: mani, WR con K, WR con M, Delta medio (K - M) con IC (bootstrap sui seed e normale appaiato), e
    la quota di mani che l'agente tiene."""
    df = df.reset_index(drop=True)
    d = (df["vince_K"] - df["vince_M"]).to_numpy()
    gruppi = list(df.groupby(chiavi, sort=True).indices.items())
    maschere = []
    for _, idx in gruppi:
        m = np.zeros(len(df), bool)
        m[idx] = True
        maschere.append(m)
    stime = _stima_gruppi(d, maschere, b, seed)
    righe = []
    for (chiave, idx), (stima, lo, hi, nlo, nhi, n) in zip(gruppi, stime):
        chiave = chiave if isinstance(chiave, tuple) else (chiave,)
        r = dict(zip(chiavi, chiave))
        r.update({"n": n, "wr_K": float(df["vince_K"].iloc[idx].mean()), "wr_M": float(df["vince_M"].iloc[idx].mean()),
                  "delta": stima, "delta_lo": lo, "delta_hi": hi, "delta_norm_lo": nlo, "delta_norm_hi": nhi})
        if "tiene" in df.columns:
            r["tiene"] = float(df["tiene"].iloc[idx].mean())
        righe.append(r)
    return pd.DataFrame(righe)


def esito_politica(df, tieni):
    """Vittoria per seed della politica che tiene (K) dove `tieni` e' vero, altrimenti M."""
    return np.where(np.asarray(tieni, bool), df["vince_K"].to_numpy(), df["vince_M"].to_numpy()).astype(int)


def settima_carta(df, carte, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP):
    """Per ogni carta (colonna di conteggio): Delta nelle mani con e senza, e la differenza (con - senza)."""
    df = df.reset_index(drop=True)
    d = (df["vince_K"] - df["vince_M"]).to_numpy(float)
    righe = []
    for carta in carte:
        con = df[carta].to_numpy() > 0
        (sc, clo, chi, _, _, nc), (ss, slo, shi, _, _, ns) = _stima_gruppi(d, [con, ~con], b, seed)
        r = {"carta": carta, "n_con": nc, "delta_con": sc, "delta_con_lo": clo, "delta_con_hi": chi, "n_senza": ns,
             "delta_senza": ss, "differenza": sc - ss}
        if nc > 1 and ns > 1:
            se = math.sqrt(d[con].var(ddof=1) / nc + d[~con].var(ddof=1) / ns)
            r["differenza_lo"], r["differenza_hi"] = sc - ss - st.Z95 * se, sc - ss + st.Z95 * se
        righe.append(r)
    return pd.DataFrame(righe)


# --- Regressione logistica (sezione 9.4) ---------------------------------------------------------------------

def _matrice(df, x, tieni):
    import statsmodels.api as sm  # noqa: F401  (solo per coerenza dell'import pigro)
    base = df[x].astype(float).reset_index(drop=True)
    m = pd.DataFrame({"costante": 1.0}, index=base.index)
    m = pd.concat([m, base], axis=1)
    m["tieni"] = float(tieni)
    for c in x:
        m[f"tieni:{c}"] = base[c] * float(tieni)
    return m


def regressione(df, x):
    """logit P(vince) = a + b.x + tieni (g0 + g.x) sulle 2N partite (K e M di ogni seed), errori raggruppati per
    seed."""
    import statsmodels.api as sm
    xk, xm = _matrice(df, x, True), _matrice(df, x, False)
    X = pd.concat([xk, xm], ignore_index=True)
    y = np.concatenate([df["vince_K"].to_numpy(), df["vince_M"].to_numpy()]).astype(float)
    gruppi = np.concatenate([df["seed"].to_numpy(), df["seed"].to_numpy()])
    return sm.Logit(y, X).fit(disp=0, cov_type="cluster", cov_kwds={"groups": gruppi})


def delta_previsto(modello, df, x):
    """Delta previsto per ogni mano: P(vince | tieni) - P(vince | mulligan)."""
    return modello.predict(_matrice(df, x, True)).to_numpy() - modello.predict(_matrice(df, x, False)).to_numpy()


def decili(previsto, osservato, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP):
    """Validazione: per decile del Delta previsto, media prevista e osservata (con IC)."""
    previsto, osservato = np.asarray(previsto, float), np.asarray(osservato, float)
    ordine = np.argsort(previsto, kind="stable")
    rango = np.empty(len(previsto), int)
    rango[ordine] = np.arange(len(previsto))
    decile = rango * 10 // len(previsto)
    maschere = [decile == k for k in range(10)]
    stime = _stima_gruppi(osservato, maschere, b, seed)
    return pd.DataFrame([{"decile": k + 1, "n": n, "previsto": float(previsto[m].mean()), "osservato": s,
                          "osservato_lo": lo, "osservato_hi": hi}
                         for k, (m, (s, lo, hi, _, _, n)) in enumerate(zip(maschere, stime))])


# --- Varianti, Recruiter e Riddler (sezioni 9.5 e 9.6) ---------------------------------------------------------

def _vince(esito, chi="uw"):
    return int(esito["vincitore"] == ETICHETTA[chi])


def varianti(righe, nome, chi="uw"):
    """Le partite in cui la variante `nome` decide diversamente dall'agente: a 7 o a 6 carte, il gruppo della mano
    tenuta (solo Recruiter, solo Witch, entrambe, altro) e il guadagno per seed (variante - partita dell'agente)."""
    out = []
    for r in righe:
        for v in r.get("varianti", []):
            if v["nome"] != nome:
                continue
            agente = r["K"] if r["agente"]["7"]["tieni"] else r["M"]
            mano = v["mano_tenuta"]
            rec, wit = "Recruiter of the Guard" in mano, WITCH in mano
            gruppo = "entrambe" if rec and wit else "solo Recruiter" if rec else "solo Witch" if wit else "altro"
            out.append({"seed": r["seed"], "in_play": r["uw_in_play"], "cambia_a": v["cambia_a"], "gruppo": gruppo,
                        "guadagno": _vince(v, chi) - _vince(agente, chi), "uguale": v["uguale_a_K_o_M"]})
    return pd.DataFrame(out)


def _turno_proprio(t, in_play):
    return (t + 1) // 2 if in_play else t // 2


def recruiter(rec):
    """Recruiter of the Guard nella partita (record compatto): turno proprio del primo lancio, carta cercata nello
    stesso turno, turno proprio in cui quella copia viene usata (lanciata o esiliata come costo), vittoria."""
    ip = rec["uw_in_play"]
    carte = rec["uw"]["carte"]
    lancio = None
    for c in carte:
        if c["c"] == "Recruiter of the Guard":
            for t, k in c["e"]:
                if k == "L" and (lancio is None or t < lancio):
                    lancio = t
    if lancio is None:
        return None
    cercata = uso = None
    for c in carte:
        if c["c"] in ("Recruiter of the Guard", "Starfield Shepherd"):
            continue
        ev = c["e"]
        if any(k == "C" and t == lancio for t, k in ev):
            cercata = c["c"]
            usi = [t for t, k in ev if k in ("L", "E") and t >= lancio]
            uso = _turno_proprio(min(usi), ip) if usi else None
            break
    return {"seed": rec["seed"], "turno_lancio": _turno_proprio(lancio, ip), "cercata": cercata, "turno_uso": uso,
            "vince": rec["vincitore"] == "UW", "in_play": ip}


def riddler(rec):
    """Quantum Riddler: turno proprio del primo ingresso (lanciata dalla mano o dall'esilio), pescate in quel turno
    della partita e risposte dirette tra loro. Il record non separa la pescata del turno da quelle di Riddler: il
    confronto si fa con i turni propri senza Riddler (pescate_base)."""
    ip = rec["uw_in_play"]
    carte = rec["uw"]["carte"]
    entra = None
    for c in carte:
        if c["c"] == "Quantum Riddler":
            for t, k in c["e"]:
                if k in ("L", "X") and (entra is None or t < entra):
                    entra = t
    if entra is None:
        return None
    pescate = [c["c"] for c in carte for t, k in c["e"] if k == "P" and t == entra]
    return {"seed": rec["seed"], "turno": _turno_proprio(entra, ip), "pescate_nel_turno": len(pescate),
            "risposte_nel_turno": sum(x in RISPOSTE_DIRETTE for x in pescate), "vince": rec["vincitore"] == "UW",
            "in_play": ip}

# Le esecuzioni: (cartella, giocatore, primo seed, partite, con il record)
ESECUZIONI = [
    ("uw", "uw", 1, 100_000, True),
    ("rakdos", "rakdos", 1, 100_000, False),
    ("uw_politica", "uw", 100_001, 100_000, False),  # seed nuovi per la politica della regressione (9.4)
]
FILE = ("mulligan.jsonl", "record_K.jsonl", "record_M.jsonl")


def gioca(cartella, giocatore, primo, partite, record, exe, processi):
    """metriche.exe --mulligan-forzato in `processi` processi, file concatenati nell'ordine dei seed."""
    cartella.mkdir(parents=True, exist_ok=True)
    per = -(-partite // processi)
    t0 = time.perf_counter()
    lavori = []
    for k in range(processi):
        quante = min(per, partite - k * per)
        if quante <= 0:
            break
        cmd = [str(exe), "--mulligan-forzato", giocatore, "--games", str(quante), "--first-seed",
               str(primo + k * per), "--out", str(cartella / f"parte_{k}")]
        if not record:
            cmd.append("--senza-record")
        lavori.append(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
    for p in lavori:
        so, se = p.communicate()
        if p.returncode != 0:
            raise RuntimeError(f"{cartella.name}: metriche.exe ha restituito {p.returncode}: {so.decode()} {se.decode()}")
    for nome in FILE:
        parti = [cartella / f"parte_{k}" / nome for k in range(len(lavori))]
        if not parti[0].exists():
            continue
        with open(cartella / nome, "wb") as f:
            for x in parti:
                with open(x, "rb") as g:
                    shutil.copyfileobj(g, f)
    for k in range(len(lavori)):
        shutil.rmtree(cartella / f"parte_{k}")
    return time.perf_counter() - t0


# --- Analisi -------------------------------------------------------------------------------------------------

# Caratteristiche della regressione (UW): le terre come probabilita' di 3 entro il turno 3 piu' il numero (e i casi
# estremi), le carte per gruppo e le singole della "settima carta".
X_UW = ["in_play", "terre_totali", "t0", "t5p", "p3", "witch", "uno_mana", "due_mana", "risposte_dirette",
        "c_recruiter", "c_riddler", "c_ephemerate", "c_consign", "c_force", "c_teferi", "c_shepherd", "c_thraben",
        "bianche", "coppia"]
# Pubblicati nella sezione 29 di agente_uw.md (partite cambiate, Delta lì, guadagno sul totale), da riprodurre.
# M3: le stesse partite di M1-R in play (1.536, +24,54).
PUBBLICATI = {"m1": (11_792, 0.0996, 0.0118), "m1r": (3_122, 0.1778, 0.0056), "m3": (1_536, 0.2454, 0.0038)}
PUBBLICATI_GRUPPI_M1 = {"solo Recruiter": (1_729, 0.1943), "solo Witch": (8_699, 0.0714), "entrambe": (1_364, 0.1598)}


def leggi_coppie(percorso, info):
    righe = [json.loads(r) for r in open(percorso, encoding="utf-8")]
    chi = righe[0]["giocatore"]
    tab = []
    for r in righe:
        in_play = r["uw_in_play"] if chi == "uw" else not r["uw_in_play"]
        car = caratteristiche_uw(r["mano"], in_play, info) if chi == "uw" else \
            caratteristiche_rakdos(r["mano"], in_play, info)
        a6 = r["agente"]["6"]
        car.update({"seed": r["seed"], "tiene": r["agente"]["7"]["tieni"], "regola": r["agente"]["7"]["regola"],
                    "tiene6": None if a6 is None else a6["tieni"], "vince_K": _vince(r["K"], chi),
                    "vince_M": _vince(r["M"], chi), "turni_K": r["K"]["turni"], "turni_M": r["M"]["turni"]})
        tab.append(car)
    df = pd.DataFrame(tab)
    if chi == "uw":
        df["t0"] = df["terre_totali"] == 0
        df["t5p"] = df["terre_totali"] >= 5
        df[["tipo_terre", "tipo_risposta", "tipo_giocata", "lato"]] = pd.DataFrame(
            [tipo_uw(c) for c in df.to_dict("records")], index=df.index)
    else:
        df[["tipo_terre", "tipo_uno", "tipo_cercano", "lato"]] = pd.DataFrame(
            [tipo_rakdos(c) for c in df.to_dict("records")], index=df.index)
    return righe, df


def leggi_record(percorso, seeds=None):
    """Le righe del record compatto (tutte, o solo quelle dei seed dati)."""
    out = []
    with open(percorso, encoding="utf-8") as f:
        for riga in f:
            if seeds is not None:
                s = int(riga[8:riga.index(",")])
                if s not in seeds:
                    continue
            out.append(json.loads(riga))
    return out


def _mano_tenuta(g):
    mano = list(g["mani"][-1]) if g["mani"] else []
    for c in g["fondo"]:
        mano.remove(c)
    return mano


def descrittivo(record, info):
    """Sezione 8: WR per carte nella mano tenuta (7, 6, 5, 4 o meno) e per tipo di mano tenuta, per giocatore."""
    righe = []
    for rec in record:
        for chi in ("uw", "rakdos"):
            g = rec[chi]
            in_play = rec["uw_in_play"] if chi == "uw" else not rec["uw_in_play"]
            mano = _mano_tenuta(g)
            car = caratteristiche_uw(mano, in_play, info) if chi == "uw" else caratteristiche_rakdos(mano, in_play, info)
            tipo = tipo_uw(car) if chi == "uw" else tipo_rakdos(car)
            righe.append({"giocatore": chi, "lato": "play" if in_play else "draw", "carte": max(4, len(mano)),
                          "tipo": " / ".join({True: "sì", False: "no"}.get(x, str(x)) if isinstance(x, bool) else
                                             str(x) for x in tipo[:-1]),
                          "vince": rec["vincitore"] == ETICHETTA[chi]})
    return pd.DataFrame(righe)


def tabella_wr(df, chiavi):
    righe = []
    for k, g in df.groupby(chiavi):
        k = k if isinstance(k, tuple) else (k,)
        p, lo, hi = st.wilson(int(g["vince"].sum()), len(g))
        righe.append({**dict(zip(chiavi, k)), "n": len(g), "wr": p, "wr_lo": lo, "wr_hi": hi})
    return pd.DataFrame(righe)


def confronto_varianti(righe_uw, n_seed):
    """9.6: per ogni variante, partite in cui decide diversamente, Delta lì, guadagno sul totale; a 7 carte K - M,
    a 6 carte la partita della variante; i gruppi di M1."""
    out, gruppi = [], []
    for nome in ("m1", "m1r", "m3"):
        v = varianti(righe_uw, nome)
        pub = PUBBLICATI[nome]
        r = {"variante": nome, "cambiate": len(v), "cambiate_7": int((v["cambia_a"] == 7).sum()),
             "cambiate_6": int((v["cambia_a"] == 6).sum()), "uguali_7": int(v.loc[v["cambia_a"] == 7, "uguale"].sum()),
             "delta": v["guadagno"].mean(), "delta_7": v.loc[v["cambia_a"] == 7, "guadagno"].mean(),
             "delta_6": v.loc[v["cambia_a"] == 6, "guadagno"].mean(), "guadagno": v["guadagno"].sum() / n_seed,
             "pub_cambiate": pub[0], "pub_delta": pub[1], "pub_guadagno": pub[2]}
        _, r["delta_lo"], r["delta_hi"] = st.normale_appaiata(v["guadagno"])
        out.append(r)
        if nome == "m1":
            for gr, g in v.groupby("gruppo"):
                riga = {"gruppo": gr, "n": len(g), "delta": g["guadagno"].mean(),
                        "n_7": int((g["cambia_a"] == 7).sum()), "delta_7": g.loc[g["cambia_a"] == 7, "guadagno"].mean(),
                        "n_6": int((g["cambia_a"] == 6).sum())}
                riga["pub_n"], riga["pub_delta"] = PUBBLICATI_GRUPPI_M1.get(gr, (None, None))
                gruppi.append(riga)
    return pd.DataFrame(out), pd.DataFrame(gruppi)


def witch_usi(record_k, seeds):
    """9.5 (b) e (c): nelle partite K delle mani "solo Witch", cosa fa Witch Enchanter e quando arriva Solitude."""
    righe = []
    for rec in record_k:
        if rec["seed"] not in seeds:
            continue
        carte = rec["uw"]["carte"]
        lanci_sol = {t for c in carte if c["c"] == "Solitude" for t, k in c["e"] if k == "L"}
        usi = set()
        for c in carte:
            if c["c"] != WITCH:
                continue
            ev = c["e"]
            if any(k == "T" for _, k in ev):
                usi.add("terra")
            if any(k == "L" for _, k in ev):
                usi.add("lanciata")
            for t, k in ev:
                if k == "E":
                    usi.add("esiliata per Solitude" if t in lanci_sol else "esiliata per March")
            if ev and ev[-1][1] in "IPCR":
                usi.add("in mano alla fine")
        sol_dopo = any(k in ("P", "C") and t > 0 for c in carte if c["c"] == "Solitude" for t, k in c["e"])
        righe.append({"seed": rec["seed"], "vince": rec["vincitore"] == "UW", "solitude_dopo": sol_dopo,
                      **{u: u in usi for u in ("terra", "lanciata", "esiliata per Solitude", "esiliata per March",
                                                "in mano alla fine")}})
    return pd.DataFrame(righe)


def analizza(dati, b=st.B_BOOTSTRAP):
    info = InfoCarte.carica()
    cart = dati / "mulligan"
    t0 = time.perf_counter()
    righe_uw, uw = leggi_coppie(cart / "uw" / "mulligan.jsonl", info)
    _, rk = leggi_coppie(cart / "rakdos" / "mulligan.jsonl", info)
    _, pol = leggi_coppie(cart / "uw_politica" / "mulligan.jsonl", info)
    t_lettura = time.perf_counter() - t0
    r = {"n": len(uw), "tempi": {}}
    # 9.2: K e M in generale
    for chi, df in (("uw", uw), ("rakdos", rk)):
        r[f"totale_{chi}"] = delta_per_gruppo(df.assign(tutte="tutte"), ["tutte"], b)
        r[f"lato_{chi}"] = delta_per_gruppo(df, ["lato"], b)
    # 9.3: per tipo
    r["tipi_uw"] = delta_per_gruppo(uw, ["tipo_terre", "tipo_risposta", "tipo_giocata", "lato"], b)
    r["tipi_rakdos"] = delta_per_gruppo(rk, ["tipo_terre", "tipo_uno", "tipo_cercano", "lato"], b)
    # Accordo di D-2 con il segno di Delta e punti persi (stima del segno sui seed 1-50.000, misura sugli altri)
    for chi, df, chiavi in (("uw", uw, ["tipo_terre", "tipo_risposta", "tipo_giocata", "lato"]),
                            ("rakdos", rk, ["tipo_terre", "tipo_uno", "tipo_cercano", "lato"])):
        stima = df[df["seed"] <= 50_000]
        verifica = df[df["seed"] > 50_000].reset_index(drop=True)
        segno = delta_per_gruppo(stima, chiavi, b=0).set_index(chiavi)["delta"] > 0
        tieni = np.array([bool(segno.get(tuple(k), False)) for k in verifica[chiavi].itertuples(index=False)])
        d = esito_politica(verifica, tieni) - esito_politica(verifica, verifica["tiene"])
        m, lo, hi = st.normale_appaiata(d)
        t = r[f"tipi_{chi}"]
        grandi = t[t["n"] >= st.SOGLIA_NASCOSTO]
        accordo = (grandi["tiene"] > 0.5) == (grandi["delta"] > 0)
        r[f"accordo_{chi}"] = {"tipi": len(grandi), "tipi_concordi": int(accordo.sum()),
                               "mani_concordi": int(grandi.loc[accordo, "n"].sum()), "mani": int(grandi["n"].sum()),
                               "punti_persi": m, "punti_persi_lo": lo, "punti_persi_hi": hi,
                               "cambiate": int((tieni != verifica["tiene"].to_numpy()).sum()), "n_verifica": len(verifica)}
    # Settima carta per le mani al confine
    gruppi_confine = {
        "senza risposta (2-4 terre, con giocata)": (uw["terre_totali"].between(2, 4)) & uw["giocata"] & ~uw["risposta"],
        "senza giocata (2-4 terre, con risposta)": (uw["terre_totali"].between(2, 4)) & ~uw["giocata"] & uw["risposta"],
        "una terra": uw["terre_totali"] == 1}
    r["settima"] = []
    for nome, m in gruppi_confine.items():
        for lato in ("tutte", "play", "draw"):
            sel = m if lato == "tutte" else m & (uw["lato"] == lato)
            s7 = settima_carta(uw[sel], [f"c_{c}" for c in SETTIMA], b)
            s7.insert(0, "lato", lato)
            s7.insert(0, "gruppo", nome)
            s7["delta_gruppo"] = (uw.loc[sel, "vince_K"] - uw.loc[sel, "vince_M"]).mean()
            s7["n_gruppo"] = int(sel.sum())
            s7["tiene_gruppo"] = uw.loc[sel, "tiene"].mean()
            r["settima"].append(s7)
    r["settima"] = pd.concat(r["settima"], ignore_index=True)
    # 9.4: regressione su 1-50.000, verifica su 50.001-100.000, politica su 100.001-200.000
    t1 = time.perf_counter()
    stima = uw[uw["seed"] <= 50_000].reset_index(drop=True)
    verifica = uw[uw["seed"] > 50_000].reset_index(drop=True)
    modello = regressione(stima, X_UW)
    r["modello"] = modello
    coef = pd.DataFrame({"coef": modello.params, "se": modello.bse, "z": modello.tvalues, "p": modello.pvalues})
    r["coefficienti"] = coef
    prev_v = delta_previsto(modello, verifica, X_UW)
    r["decili"] = decili(prev_v, (verifica["vince_K"] - verifica["vince_M"]).to_numpy(), b)
    r["validazione"] = {"corr": float(np.corrcoef(prev_v, verifica["vince_K"] - verifica["vince_M"])[0, 1]),
                        "media_prevista": float(prev_v.mean()),
                        "media_osservata": float((verifica["vince_K"] - verifica["vince_M"]).mean())}
    prev_p = delta_previsto(modello, pol, X_UW)
    tieni_pol = prev_p > 0
    d = esito_politica(pol, tieni_pol) - esito_politica(pol, pol["tiene"])
    (s, lo, hi, nlo, nhi, _), = _stima_gruppi(d, [np.ones(len(d), bool)], b, st.SEED_BOOTSTRAP)
    cambia = tieni_pol != pol["tiene"].to_numpy()
    r["politica"] = {"n": len(pol), "differenza": s, "lo": lo, "hi": hi, "norm_lo": nlo, "norm_hi": nhi,
                     "wr_politica": esito_politica(pol, tieni_pol).mean(), "wr_d2": esito_politica(pol, pol["tiene"]).mean(),
                     "tiene_politica": tieni_pol.mean(), "tiene_d2": pol["tiene"].mean(), "cambiate": int(cambia.sum()),
                     "tiene_in_piu": int((tieni_pol & ~pol["tiene"].to_numpy()).sum()),
                     "rifiuta_in_piu": int((~tieni_pol & pol["tiene"].to_numpy()).sum()),
                     "delta_cambiate": float(d[cambia].mean()) if cambia.any() else math.nan}
    r["tempi"]["regressione e politica"] = time.perf_counter() - t1
    # 9.6: M1, M1-R, M3
    r["varianti"], r["gruppi_m1"] = confronto_varianti(righe_uw, len(uw))
    # 9.5: Witch
    v = varianti(righe_uw, "m1")
    solo_witch_7 = set(v.loc[(v["gruppo"] == "solo Witch") & (v["cambia_a"] == 7), "seed"])
    t2 = time.perf_counter()
    rk_k = leggi_record(cart / "uw" / "record_K.jsonl", solo_witch_7)
    r["witch_usi"] = witch_usi(rk_k, solo_witch_7)
    sel = uw["terre_totali"].between(2, 4) & uw["giocata"] & ~uw["risposta"]
    r["witch_a"] = delta_per_gruppo(uw[sel].assign(con_witch=uw.loc[sel, "witch"] > 0), ["terre_totali", "con_witch"], b)
    r["witch_solo"] = delta_per_gruppo(uw[uw["seed"].isin(solo_witch_7)].assign(g="solo Witch"), ["g"], b)
    # Le stesse mani senza Witch: stesso tipo (2-4 terre, giocata, nessuna risposta), M1 non cambia niente
    r["tempi"]["record K delle mani solo Witch"] = time.perf_counter() - t2
    # D-2c e il descrittivo, dal record degli euristici v1 (le partite normali dei seed 1-100.000)
    t3 = time.perf_counter()
    record = leggi_record(dati / "record.jsonl")
    r["tempi"]["lettura di record.jsonl"] = time.perf_counter() - t3
    r["recruiter"] = pd.DataFrame([x for x in (recruiter(rec) for rec in record) if x is not None])
    r["riddler"] = pd.DataFrame([x for x in (riddler(rec) for rec in record) if x is not None])
    r["riddler_base"] = pescate_base(record)
    desc = descrittivo(record, info)
    r["desc_carte"] = tabella_wr(desc, ["giocatore", "lato", "carte"])
    r["desc_tipo"] = tabella_wr(desc, ["giocatore", "tipo"])
    r["tempi"]["lettura delle coppie"] = t_lettura
    r["uw"], r["rk"] = uw, rk
    return r


def pescate_base(record):
    """Per confronto con Riddler: nei turni propri di UW (dal 2) senza Riddler entrata, pescate e risposte dirette
    pescate per turno, in media."""
    turni = pescate = risposte = 0
    for rec in record:
        ip = rec["uw_in_play"]
        carte = rec["uw"]["carte"]
        entrate = {t for c in carte if c["c"] == "Quantum Riddler" for t, k in c["e"] if k in ("L", "X")}
        per_turno = {}
        for c in carte:
            for t, k in c["e"]:
                if k == "P":
                    per_turno.setdefault(t, []).append(c["c"])
        for t in range(1, rec["turni"] + 1):
            proprio = (t % 2 == 1) == ip
            if not proprio or t in entrate or _turno_proprio(t, ip) < 2:
                continue
            turni += 1
            p = per_turno.get(t, [])
            pescate += len(p)
            risposte += sum(x in RISPOSTE_DIRETTE for x in p)
    return {"turni": turni, "pescate": pescate / turni, "risposte": risposte / turni}


# --- Documento -------------------------------------------------------------------------------------------------

def documento(r, info_run):
    from metriche.metriche_carte import _intero, _num, _tabella, fmt_diff, fmt_wr

    def pt(x, dec=1, segno=True):
        return "—" if x is None or (isinstance(x, float) and math.isnan(x)) else _num(100 * x, dec, segno)

    def d_ic(row, k="delta", dec=1):
        return fmt_diff(int(row["n"]), row[k], row.get(f"{k}_lo"), row.get(f"{k}_hi"), dec)

    s = ["# Mulligan degli euristici v1 (passo 4)\n"]
    s.append(f"Generato da `python/metriche/mulligan.py analizza` (sezioni 8 e 9 di [metriche.md](metriche.md)). "
             f"{_intero(r['n'])} seed (1-{_intero(r['n'])}) per UW e per Rakdos, due partite per seed: **K** con la "
             f"prima decisione di mulligan del giocatore forzata a \"tieni\", **M** a \"mulligan\" (`metriche.exe "
             f"--mulligan-forzato`); il resto lo decidono gli euristici v1 (UW base_v2, Rakdos base_r3). **Delta** = "
             f"vittoria(K) − vittoria(M): il valore di tenere la prima mano da 7 **per questi agenti**, in punti. "
             f"Intervalli al 95%: bootstrap sui seed (B = {_intero(info_run['b'])}, seed {st.SEED_BOOTSTRAP}) e "
             f"Wilson per le proporzioni. *Corsivo* sotto 1.000 mani, \"—\" sotto 200. I seed 100.001-200.000 servono "
             f"solo alla politica della regressione.\n")
    for chi, nome in (("uw", "UW"), ("rakdos", "Rakdos")):
        t = r[f"totale_{chi}"].iloc[0]
        lati = r[f"lato_{chi}"].set_index("lato")
        s.append(f"- **{nome}:** l'agente tiene la prima mano da 7 nel {pt(t['tiene'], 2, False)}% dei seed; K vince "
                 f"il {pt(t['wr_K'], 2, False)}%, M il {pt(t['wr_M'], 2, False)}%; Delta medio {d_ic(t)} (in play "
                 f"{d_ic(lati.loc['play'])}, in draw {d_ic(lati.loc['draw'])}).")
    # 8
    s.append("\n## Mulligan descrittivo (sezione 8)\n")
    s.append("Le partite normali dei seed 1-100.000 (gli stessi registri della sezione 1), per carte nella mano "
             "tenuta: WR del giocatore con Wilson. È una descrizione: una mano da 6 è anche \"la mano da 7 era "
             "brutta\".\n")
    dc = r["desc_carte"]
    for chi in ("uw", "rakdos"):
        x = dc[dc["giocatore"] == chi]
        s.append(_tabella(["", "7 carte", "6 carte", "5 carte", "4 o meno"],
                          [[f"{ETICHETTA[chi]} in {lato}"] + [
                              (lambda q: f"{_intero(q['n'])}: {fmt_wr(q['n'], q['wr'], q['wr_lo'], q['wr_hi'])}"
                               if q is not None else "—")(
                                  next(iter(x[(x["lato"] == lato) & (x["carte"] == k)].to_dict("records")), None))
                              for k in (7, 6, 5, 4)] for lato in ("play", "draw")]))
        s.append("")
    s.append("Per tipo di mano tenuta (dopo le carte in fondo). UW: terre (Witch compresa) / risposta diretta / "
             "giocata entro il turno 2; Rakdos: terre / creatura da un mana / carte che cercano. Solo i tipi con almeno "
             "1.000 partite.\n")
    dt = r["desc_tipo"]
    for chi in ("uw", "rakdos"):
        x = dt[(dt["giocatore"] == chi) & (dt["n"] >= 1000)].sort_values("n", ascending=False)
        s.append(_tabella(["Tipo", "Partite", "WR"],
                          [[q["tipo"], _intero(q["n"]), fmt_wr(q["n"], q["wr"], q["wr_lo"], q["wr_hi"])]
                           for q in x.to_dict("records")]))
        s.append("")
    # 9.3
    s.append("\n## Valore di tenere per tipo di mano (sezione 9.3)\n")
    s.append("Tipo della **prima mano da 7** (decisa prima: terre con Witch, risposta diretta, giocata entro il turno "
             "2 di D-2, play o draw). **D-2 tiene**: quota delle mani di quel tipo che l'agente tiene. Accordo: D-2 "
             "tiene la maggioranza dove Delta > 0 e la rifiuta dove Delta < 0. La riga si mostra da 200 mani. "
             "**Attenzione:** M è \"mulligan, poi decide D-2\" (anche a 6 carte): Delta confronta tenere con il "
             "mulligan *giocato da questo agente*, non con il mulligan migliore.\n")
    t = r["tipi_uw"]
    t = t[t["n"] >= st.SOGLIA_NASCOSTO]
    s.append(_tabella(["Terre", "Risposta", "Giocata", "Lato", "Mani", "WR K", "WR M", "Delta", "D-2 tiene", "Accordo"],
                      [[q["tipo_terre"], "sì" if q["tipo_risposta"] else "no", "sì" if q["tipo_giocata"] else "no",
                        q["lato"], _intero(q["n"]), pt(q["wr_K"], 1, False), pt(q["wr_M"], 1, False), d_ic(q),
                        f"{pt(q['tiene'], 0, False)}%",
                        "sì" if (q["tiene"] > 0.5) == (q["delta"] > 0) else "**no**"]
                       for q in t.to_dict("records")]))
    a = r["accordo_uw"]
    s.append(f"\n**Accordo di D-2:** {a['tipi_concordi']} tipi su {a['tipi']} ({_intero(a['mani_concordi'])} mani su "
             f"{_intero(a['mani'])}). **Punti persi da D-2** (politica \"tieni se il Delta del tipo è positivo\", segno "
             f"stimato sui seed 1-50.000, misurata sui seed 50.001-100.000 contro D-2): "
             f"{pt(a['punti_persi'], 2)} [{pt(a['punti_persi_lo'], 2)}; {pt(a['punti_persi_hi'], 2)}] punti, "
             f"{_intero(a['cambiate'])} decisioni diverse su {_intero(a['n_verifica'])}.\n")
    s.append("\n### Rakdos\n")
    t = r["tipi_rakdos"]
    t = t[t["n"] >= st.SOGLIA_NASCOSTO]
    s.append(_tabella(["Terre", "Creatura da 1", "Cercano", "Lato", "Mani", "WR K", "WR M", "Delta", "Agente tiene",
                       "Accordo"],
                      [[q["tipo_terre"], "sì" if q["tipo_uno"] else "no", q["tipo_cercano"], q["lato"], _intero(q["n"]),
                        pt(q["wr_K"], 1, False), pt(q["wr_M"], 1, False), d_ic(q), f"{pt(q['tiene'], 0, False)}%",
                        "sì" if (q["tiene"] > 0.5) == (q["delta"] > 0) else "**no**"]
                       for q in t.to_dict("records")]))
    a = r["accordo_rakdos"]
    s.append(f"\n**Accordo dell'agente di Rakdos:** {a['tipi_concordi']} tipi su {a['tipi']} "
             f"({_intero(a['mani_concordi'])} mani su {_intero(a['mani'])}); punti persi {pt(a['punti_persi'], 2)} "
             f"[{pt(a['punti_persi_lo'], 2)}; {pt(a['punti_persi_hi'], 2)}].\n")
    # Settima carta
    s.append("\n## La settima carta (D-2b)\n")
    s.append("Nelle mani al confine di D-2, Delta con e senza ogni carta singola (decisione 8). **Differenza** = "
             "Delta con − Delta senza (IC normale tra gruppi indipendenti). Una carta che porta Delta da negativo a "
             "positivo dà ragione a D-2b per quella carta.\n")
    st7 = r["settima"]
    for gruppo in st7["gruppo"].unique():
        x = st7[(st7["gruppo"] == gruppo) & (st7["lato"] == "tutte")]
        g0 = x.iloc[0]
        s.append(f"\n**{gruppo}**: {_intero(g0['n_gruppo'])} mani, D-2 ne tiene il {pt(g0['tiene_gruppo'], 0, False)}%, "
                 f"Delta del gruppo {pt(g0['delta_gruppo'])} punti.\n")
        xp = st7[(st7["gruppo"] == gruppo) & (st7["lato"] == "play")].set_index("carta")
        xd = st7[(st7["gruppo"] == gruppo) & (st7["lato"] == "draw")].set_index("carta")
        s.append(_tabella(["Carta", "Mani con", "Delta con", "Delta senza", "Differenza", "Delta con, in play",
                           "Delta con, in draw"],
                          [[q["carta"][2:], _intero(q["n_con"]),
                            fmt_diff(q["n_con"], q["delta_con"], q["delta_con_lo"], q["delta_con_hi"]),
                            fmt_diff(q["n_senza"], q["delta_senza"]),
                            fmt_diff(min(q["n_con"], q["n_senza"]), q["differenza"], q.get("differenza_lo"),
                                     q.get("differenza_hi")),
                            fmt_diff(xp.loc[q["carta"], "n_con"], xp.loc[q["carta"], "delta_con"]),
                            fmt_diff(xd.loc[q["carta"], "n_con"], xd.loc[q["carta"], "delta_con"])]
                           for q in x.to_dict("records")]))
    # 9.4
    s.append("\n## Regressione logistica (sezione 9.4)\n")
    s.append("logit P(vince) = a + b·x + tieni·(g0 + g·x) sulle partite K e M dei seed 1-50.000 (100.000 righe), "
             "errori raggruppati per seed. I coefficienti **g** (\"tieni:...\") dicono quanto ogni caratteristica "
             "cambia il valore di tenere (in logit). Qui quelli con |z| ≥ 2, per |z|:\n")
    c = r["coefficienti"]
    g = c[c.index.str.startswith("tieni")].copy()
    g = g[g["z"].abs() >= 2].reindex(g["z"].abs().sort_values(ascending=False).index)
    g = g[g["z"].abs() >= 2]
    s.append(_tabella(["Coefficiente", "Stima", "Errore standard", "z", "p"],
                      [[i, _num(q["coef"], 3, True), _num(q["se"], 3), _num(q["z"], 1, True),
                        f"{q['p']:.1e}" if q["p"] < 0.001 else _num(q["p"], 3)] for i, q in g.iterrows()]))
    va = r["validazione"]
    s.append(f"\n**Validazione sui seed 50.001-100.000:** Delta medio previsto {pt(va['media_prevista'], 2)}, osservato "
             f"{pt(va['media_osservata'], 2)}; correlazione tra previsto e osservato per seed {_num(va['corr'], 3)} "
             f"(l'osservato è −1, 0 o +1). Per decile del previsto:\n")
    s.append(_tabella(["Decile", "Mani", "Delta previsto", "Delta osservato"],
                      [[q["decile"], _intero(q["n"]), pt(q["previsto"]),
                        fmt_diff(q["n"], q["osservato"], q["osservato_lo"], q["osservato_hi"])]
                       for q in r["decili"].to_dict("records")]))
    p = r["politica"]
    s.append(f"\n**Politica \"tieni se il Delta previsto è positivo\"** (modello stimato sui seed 1-50.000), misurata "
             f"sui seed nuovi 100.001-200.000 contro D-2: differenza appaiata **{pt(p['differenza'], 2)} "
             f"[{pt(p['lo'], 2)}; {pt(p['hi'], 2)}]** punti (normale [{pt(p['norm_lo'], 2)}; {pt(p['norm_hi'], 2)}]); WR "
             f"{pt(p['wr_politica'], 2, False)}% contro {pt(p['wr_d2'], 2, False)}%. La politica tiene il "
             f"{pt(p['tiene_politica'], 1, False)}% delle prime mani, D-2 il {pt(p['tiene_d2'], 1, False)}%; decide "
             f"diversamente in {_intero(p['cambiate'])} seed ({_intero(p['tiene_in_piu'])} mani tenute in più, "
             f"{_intero(p['rifiuta_in_piu'])} rifiutate in più), dove vale {pt(p['delta_cambiate'], 2)} punti. "
             f"**Solo una variante candidata**: non si adotta.\n")
    # 9.6 varianti
    s.append("\n## D-2 contro M1, M1-R e M3 (sezione 9.6)\n")
    s.append("Per ogni variante, i seed in cui decide diversamente da D-2: a 7 carte la sua partita è K o M (controllato "
             "voce per voce: \"identiche\"), a 6 carte (nella partita M) è giocata a parte con la variante. "
             "\"Pubblicato\": sezione 29 di `agente_uw.md` (stessi seed, partite intere con la variante).\n")
    s.append(_tabella(["Variante", "Cambiate (a 7 / a 6)", "Identiche a K o M", "Delta lì", "Pubblicato",
                       "Guadagno sul totale", "Pubblicato"],
                      [[q["variante"].upper().replace("M1R", "M1-R"), f"{_intero(q['cambiate'])} ({_intero(q['cambiate_7'])} / "
                        f"{_intero(q['cambiate_6'])})", f"{_intero(q['uguali_7'])} su {_intero(q['cambiate_7'])}",
                        f"{pt(q['delta'], 2)} [{pt(q['delta_lo'], 2)}; {pt(q['delta_hi'], 2)}]",
                        f"{_intero(q['pub_cambiate'])}, {pt(q['pub_delta'], 2)}", pt(q["guadagno"], 2),
                        pt(q["pub_guadagno"], 2)] for q in r["varianti"].to_dict("records")]))
    s.append("\nM1 per gruppo (la carta che fa scattare il cambio nella mano tenuta):\n")
    s.append(_tabella(["Gruppo", "Cambiate", "Delta", "Pubblicato", "A 7 carte: cambiate, K − M", "A 6 carte"],
                      [[q["gruppo"], _intero(q["n"]), pt(q["delta"], 2),
                        f"{_intero(q['pub_n'])}, {pt(q['pub_delta'], 2)}" if q["pub_n"] else "—",
                        f"{_intero(q['n_7'])}, {pt(q['delta_7'], 2)}", _intero(q["n_6"])]
                       for q in r["gruppi_m1"].to_dict("records")]))
    s.append("\nQui il gruppo è dato da quali delle due carte ci sono nella mano tenuta; la sezione 29 lo dava dalla "
             "carta che fa scattare il cambio. Una partita su 11.792 cade in un gruppo diverso (\"entrambe\" e \"solo "
             "Recruiter\"); i totali coincidono.\n")
    # 9.5 Witch
    s.append("\n## Witch Enchanter (sezione 9.5)\n")
    ws = r["witch_solo"].iloc[0]
    s.append(f"**Le mani \"solo Witch\" a 7 carte** (M1 le tiene, D-2 no): {_intero(ws['n'])} mani, K − M {d_ic(ws)}.\n")
    s.append("\n**(a) Il mulligan evitato:** mani dello stesso tipo (2-4 terre con Witch, una giocata, nessuna "
             "risposta diretta), con e senza Witch, per numero di terre:\n")
    s.append(_tabella(["Terre (con Witch)", "Witch", "Mani", "WR K", "WR M", "Delta", "D-2 tiene"],
                      [[q["terre_totali"], "sì" if q["con_witch"] else "no", _intero(q["n"]), pt(q["wr_K"], 1, False),
                        pt(q["wr_M"], 1, False), d_ic(q), f"{pt(q['tiene'], 0, False)}%"]
                       for q in r["witch_a"].to_dict("records")]))
    wu = r["witch_usi"]
    s.append(f"\n**(b) e (c): cosa fa Witch nelle partite K delle mani \"solo Witch\" a 7 carte** ({_intero(len(wu))} "
             f"partite). Una partita può avere più usi (più copie):\n")
    righe = []
    for u in ("terra", "lanciata", "esiliata per Solitude", "esiliata per March", "in mano alla fine", "solitude_dopo"):
        sel = wu[u]
        pw, lo, hi = st.wilson(int(wu.loc[sel, "vince"].sum()), int(sel.sum()))
        righe.append([u.replace("solitude_dopo", "Solitude arriva dopo (pescata o cercata)"),
                      f"{_intero(sel.sum())} ({pt(sel.mean(), 1, False)}%)", fmt_wr(int(sel.sum()), pw, lo, hi)])
    s.append(_tabella(["Uso", "Partite", "WR di UW (K)"], righe))
    # D-2c
    s.append("\n## Recruiter of the Guard e Quantum Riddler (D-2c, sezione 9.6)\n")
    rc = r["recruiter"]
    s.append(f"Partite normali dei seed 1-100.000 con Recruiter lanciato: {_intero(len(rc))}. Turno proprio del lancio, "
             f"carta cercata (nello stesso turno), turno proprio in cui quella copia viene usata (lanciata o esiliata "
             f"come costo), WR.\n")
    righe = []
    for cerc, g in rc.groupby(rc["cercata"].fillna("nessuna")):
        p_, lo, hi = st.wilson(int(g["vince"].sum()), len(g))
        uso = g["turno_uso"].dropna()
        righe.append([cerc, _intero(len(g)), _num(g["turno_lancio"].mean(), 2),
                      f"{pt(len(uso) / len(g), 1, False)}%", _num((uso - g.loc[uso.index, "turno_lancio"]).mean(), 2)
                      if len(uso) else "—", fmt_wr(len(g), p_, lo, hi)])
    righe.sort(key=lambda x: -int(x[1].replace(".", "")))
    s.append(_tabella(["Carta cercata", "Partite", "Turno del lancio (media)", "Usata", "Turni dal lancio all'uso "
                       "(media)", "WR di UW"], righe))
    sol = rc[rc["cercata"] == "Solitude"]
    if len(sol):
        s.append(f"\nSolitude cercata: usata nello stesso turno nel {pt((sol['turno_uso'] == sol['turno_lancio']).mean(), 1, False)}%"
                 f" dei casi, al turno dopo nel {pt((sol['turno_uso'] == sol['turno_lancio'] + 1).mean(), 1, False)}%, "
                 f"mai nel {pt(sol['turno_uso'].isna().mean(), 1, False)}%.\n")
    rd, base = r["riddler"], r["riddler_base"]
    p_, lo, hi = st.wilson(int(rd["vince"].sum()), len(rd))
    s.append(f"\n**Quantum Riddler:** {_intero(len(rd))} partite con Riddler entrata (lanciata dalla mano o "
             f"dall'esilio), al turno proprio {_num(rd['turno'].mean(), 2)} in media; WR {fmt_wr(len(rd), p_, lo, hi)}. "
             f"Nel turno in cui entra UW pesca in media **{_num(rd['pescate_nel_turno'].mean(), 2)}** carte, di cui "
             f"**{_num(rd['risposte_nel_turno'].mean(), 3)}** risposte dirette; nei turni propri senza Riddler (dal "
             f"secondo) {_num(base['pescate'], 2)} carte e {_num(base['risposte'], 3)} risposte. Il record non separa "
             f"la pescata del turno da quelle di Riddler: la differenza è quello che Riddler aggiunge. Con almeno una "
             f"risposta pescata nel turno di Riddler: {pt((rd['risposte_nel_turno'] > 0).mean(), 1, False)}% delle "
             f"partite.\n")
    if info_run.get("tempi"):
        s.append("\n## Tempi\n")
        s.append(_tabella(["Passo", "Tempo"], [[k, v] for k, v in info_run["tempi"].items()]))
    return "\n".join(s) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("comando", choices=["gioca", "analizza"])
    ap.add_argument("--exe", default=str(RADICE / "build/Release/metriche.exe"))
    ap.add_argument("--processi", type=int, default=8)
    ap.add_argument("--dati", default=str(RADICE / "data/metriche/v1"))
    ap.add_argument("--doc", default=str(RADICE / "docs/risultati_v1_mulligan.md"))
    ap.add_argument("--b", type=int, default=st.B_BOOTSTRAP)
    ap.add_argument("--tempi", default=None, help="JSON {passo: tempo} da riportare nel documento")
    ap.add_argument("--solo", default=None)
    args = ap.parse_args(argv)
    dati = Path(args.dati)
    if args.comando == "gioca":
        tempi = {}
        for nome, giocatore, primo, partite, record in ESECUZIONI:
            if args.solo and nome not in args.solo.split(","):
                continue
            s = gioca(dati / "mulligan" / nome, giocatore, primo, partite, record, Path(args.exe), args.processi)
            tempi[nome] = s
            print(f"{nome}: {partite} seed, {s:.1f} s", flush=True)
        return 0
    t0 = time.perf_counter()
    r = analizza(dati, args.b)
    tempi = json.loads(Path(args.tempi).read_text(encoding="utf-8")) if args.tempi else {}
    for k, v in r["tempi"].items():
        tempi[f"Analisi: {k}"] = f"{v:.1f} s".replace(".", ",")
    tempi["Analisi, in tutto"] = f"{time.perf_counter() - t0:.1f} s".replace(".", ",")
    out = dati / "mulligan"
    for nome in ("tipi_uw", "tipi_rakdos", "settima", "coefficienti", "decili", "varianti", "gruppi_m1", "witch_a",
                 "witch_usi", "recruiter", "riddler", "desc_carte", "desc_tipo"):
        r[nome].to_csv(out / f"{nome}.csv", index=nome == "coefficienti")
    Path(args.doc).write_text(documento(r, {"b": args.b, "tempi": tempi}), encoding="utf-8")
    print(f"Fatto in {time.perf_counter() - t0:.1f} s; politica {r['politica']}; varianti\n{r['varianti']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
