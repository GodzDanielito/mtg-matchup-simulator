# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "numpy==2.5.5",
#   "pandas==3.1.1",
#   "pyarrow==26.0.1",
# ]
# ///
"""Dal record compatto (record.jsonl di metriche.exe) a tre tabelle Parquet (docs/metriche.md, sezione 10).

- partite.parquet: una riga per partita (esito, mulligan, terre della mano tenuta, terre a fine turno, delirium);
- carte.parquet: una riga per partita x giocatore x carta della lista (anche le carte mai viste), con la classe
  della partizione della sezione 3 (OH, GD, cercata, cimitero, GNS), le copie per codice e i turni;
- mani.parquet: una riga per mano da 7 vista (ogni decisione di mulligan), con le terre.

Uso:
    uv run --no-project python/metriche/converti.py [--record data/metriche/v1/record.jsonl] [--out data/metriche/v1]
"""

import argparse
import json
import sys
import time
from collections import Counter
from pathlib import Path

CLASSI = ("OH", "GD", "cercata", "cimitero", "GNS")
CODICI = "IPCFRLTYXESKMV"
VISTA = set("IPCRYX")     # la copia e' stata vista (sezione 3, GNS)
LANCIO = set("LYX")       # lanciata (dalla mano, dal cimitero, dall'esilio)
IN_MANO = set("IPCR")     # arriva in mano
DA_MANO = set("LTESK")    # lascia la mano
GIOCATORI = ("uw", "rakdos")
WITCH = "Witch Enchanter"


def turno_giocatore(turno, in_play):
    """Turni propri iniziati dal giocatore entro il turno della partita dato (1 = il suo primo turno)."""
    return (turno + 1) // 2 if in_play else turno // 2


def classe(codici):
    """La classe della partizione per l'insieme dei codici di tutte le copie di una carta in una partita.

    OH (almeno una copia nella mano tenuta) > GD (almeno una pescata) > cercata > cimitero (lanciata dal
    cimitero o dall'esilio, o tornata in mano, senza essere mai stata in mano prima) > GNS.
    """
    if "I" in codici:
        return "OH"
    if "P" in codici:
        return "GD"
    if "C" in codici:
        return "cercata"
    if codici & {"Y", "X", "R"}:
        return "cimitero"
    return "GNS"


def _in_play(rec, chi):
    return rec["uw_in_play"] if chi == "uw" else not rec["uw_in_play"]


def righe_carte(rec, mazzi):
    """Le righe di carte.parquet per un record: una per giocatore e per carta della sua lista."""
    righe = []
    for chi in GIOCATORI:
        g = rec[chi]
        in_play = _in_play(rec, chi)
        copie = {}
        for c in g["carte"]:
            copie.setdefault(c["c"], []).append(c["e"])
        prima_mano = Counter(g["mani"][0]) if g["mani"] else Counter()
        for carta, n_lista in mazzi[chi].items():
            r = {"seed": rec["seed"], "giocatore": chi, "carta": carta, "copie_lista": n_lista}
            k = dict.fromkeys(CODICI, 0)
            tutti = set()
            viste = lanciate = mano_fine = 0
            turno_ip = turno_vista = turno_ipc = None
            lanci_turno = []
            for eventi in copie.get(carta, []):
                cod = {e[1] for e in eventi}
                tutti |= cod
                for c in cod:
                    if c in k:
                        k[c] += 1
                viste += bool(cod & VISTA)
                in_mano = False
                primo_lancio = None
                for t, c in eventi:
                    if c in IN_MANO:
                        in_mano = True
                    elif c in DA_MANO:
                        in_mano = False
                    if c in "IP" and (turno_ip is None or t < turno_ip):
                        turno_ip = t
                    if c in "IPC" and (turno_ipc is None or t < turno_ipc):
                        turno_ipc = t
                    if c in VISTA and (turno_vista is None or t < turno_vista):
                        turno_vista = t
                    if c in LANCIO and primo_lancio is None:
                        primo_lancio = t
                mano_fine += in_mano
                if primo_lancio is not None:
                    lanciate += 1
                    lanci_turno.append(turno_giocatore(primo_lancio, in_play))
            r["classe"] = classe(tutti)
            r.update({f"k_{c}": v for c, v in k.items()})
            r.update({"copie_viste": viste, "copie_lanciate": lanciate, "copie_mano_fine": mano_fine,
                      "copie_mano7": prima_mano.get(carta, 0), "turno_ip": turno_ip, "turno_vista": turno_vista,
                      "turno_ipc": turno_ipc,
                      "lanci_turno": lanci_turno})
            righe.append(r)
    return righe


def controlla_righe(righe, mazzi, partite):
    """Controlli di coerenza della sezione 11 sulle righe delle carte; lista dei problemi (vuota se nessuno)."""
    problemi = []
    per_carta = Counter((r["giocatore"], r["carta"]) for r in righe)
    seed_per_carta = {}
    for r in righe:
        seed_per_carta.setdefault((r["giocatore"], r["carta"]), set()).add(r["seed"])
    for chi, mazzo in mazzi.items():
        for carta in mazzo:
            n, s = per_carta.get((chi, carta), 0), len(seed_per_carta.get((chi, carta), ()))
            if n != partite or s != partite:
                problemi.append(f"{chi} {carta}: {n} righe e {s} partite diverse su {partite} partite")
    for r in righe:
        if r["copie_viste"] > r["copie_lista"] or r["k_I"] > r["copie_lista"]:
            problemi.append(f"seed {r['seed']}, {r['giocatore']} {r['carta']}: {r['copie_viste']} copie viste, "
                            f"{r['k_I']} iniziali, {r['copie_lista']} nella lista")
        if r["classe"] not in CLASSI or (r["classe"] == "OH") != (r["k_I"] > 0) or \
                (r["classe"] == "GD" and (r["k_I"] > 0 or r["k_P"] == 0)):
            problemi.append(f"seed {r['seed']}, {r['giocatore']} {r['carta']}: classe {r['classe']} incoerente")
    return problemi


def mano_tenuta(g):
    return Counter(g["mani"][-1]) - Counter(g["fondo"])


def riga_partita(rec, terre):
    p = {k: rec[k] for k in ("seed", "uw_in_play", "vincitore", "fine", "turni", "vita_uw", "vita_rakdos")}
    p["vince_uw"] = rec["vincitore"] == "UW"
    d = rec["delirium"]
    p["delirium_turno"] = None if d is None else d["turno"]
    p["delirium_turno_rakdos"] = None if d is None else d["turno_rakdos"]
    p["delirium_perso"] = None if d is None else d["perso"]
    p["delirium_fine"] = None if d is None else d["fine"]
    for chi in GIOCATORI:
        g = rec[chi]
        tenuta = mano_tenuta(g)
        p[f"mulligan_{chi}"] = g["mulligan"]
        p[f"carte_mano_{chi}"] = sum(tenuta.values())
        p[f"terre_mano_{chi}"] = sum(n for c, n in tenuta.items() if c in terre)
        p[f"mano_fine_{chi}"] = g["mano_fine"]
        p[f"terre_{chi}"] = list(g["terre"])
        p[f"fondo_{chi}"] = list(g["fondo"])
    p["witch_mano_uw"] = mano_tenuta(rec["uw"]).get(WITCH, 0)
    return p


def righe_mani(rec, terre):
    righe = []
    for chi in GIOCATORI:
        g = rec[chi]
        vince = rec["vincitore"] == ("UW" if chi == "uw" else "Rakdos")
        for i, mano in enumerate(g["mani"]):
            righe.append({"seed": rec["seed"], "giocatore": chi, "in_play": _in_play(rec, chi), "indice": i,
                          "tenuta": i == len(g["mani"]) - 1, "carte": list(mano),
                          "terre": sum(c in terre for c in mano), "witch": sum(c == WITCH for c in mano),
                          "vince": vince})
    return righe


# --- Lettura delle liste e scrittura in Parquet -------------------------------------------------------------

RADICE = Path(__file__).resolve().parents[2]
LISTE = {"uw": RADICE / "config/decks/uw_blink_sim.json", "rakdos": RADICE / "config/decks/rakdos_delirium.json"}


def leggi_mazzi():
    mazzi = {}
    for chi, percorso in LISTE.items():
        lista = json.loads(percorso.read_text(encoding="utf-8"))["main"]
        mazzi[chi] = {v["card"]: v["count"] for v in lista}
    return mazzi


def leggi_terre():
    carte = json.loads((RADICE / "config/cards.json").read_text(encoding="utf-8"))["cards"]
    return {c["name"] for c in carte if "Land" in c["types"]}


def _schemi():
    import pyarrow as pa
    i8, i16, i32, s = pa.int8(), pa.int16(), pa.int32(), pa.string()
    carte = pa.schema([("seed", i32), ("giocatore", s), ("carta", s), ("copie_lista", i8), ("classe", s)]
                      + [(f"k_{c}", i8) for c in CODICI]
                      + [("copie_viste", i8), ("copie_lanciate", i8), ("copie_mano_fine", i8), ("copie_mano7", i8),
                         ("turno_ip", i16), ("turno_vista", i16), ("turno_ipc", i16), ("lanci_turno", pa.list_(i16))])
    partite_campi = [("seed", i32), ("uw_in_play", pa.bool_()), ("vincitore", s), ("fine", s), ("turni", i16),
                     ("vita_uw", i16), ("vita_rakdos", i16), ("vince_uw", pa.bool_()), ("delirium_turno", i16),
                     ("delirium_turno_rakdos", i16), ("delirium_perso", i16), ("delirium_fine", pa.bool_())]
    for chi in GIOCATORI:
        partite_campi += [(f"mulligan_{chi}", i8), (f"carte_mano_{chi}", i8), (f"terre_mano_{chi}", i8),
                          (f"mano_fine_{chi}", i8), (f"terre_{chi}", pa.list_(i8)), (f"fondo_{chi}", pa.list_(s))]
    partite_campi.append(("witch_mano_uw", i8))
    mani = pa.schema([("seed", i32), ("giocatore", s), ("in_play", pa.bool_()), ("indice", i8),
                      ("tenuta", pa.bool_()), ("carte", pa.list_(s)), ("terre", i8), ("witch", i8),
                      ("vince", pa.bool_())])
    return pa.schema(partite_campi), carte, mani


def converti(record, out, blocco=5000):
    """Scrive le tre tabelle; restituisce (partite, problemi delle righe delle carte)."""
    import pyarrow as pa
    import pyarrow.parquet as pq
    mazzi, terre = leggi_mazzi(), leggi_terre()
    s_partite, s_carte, s_mani = _schemi()
    out.mkdir(parents=True, exist_ok=True)
    scrittori = {nome: pq.ParquetWriter(out / f"{nome}.parquet", schema, compression="zstd")
                 for nome, schema in (("partite", s_partite), ("carte", s_carte), ("mani", s_mani))}
    buf = {"partite": [], "carte": [], "mani": []}
    schemi = {"partite": s_partite, "carte": s_carte, "mani": s_mani}
    problemi, partite, seed_prec = [], 0, 0
    conti_carte = Counter()

    def svuota():
        for nome, righe in buf.items():
            if righe:
                scrittori[nome].write_table(pa.Table.from_pylist(righe, schema=schemi[nome]))
                righe.clear()

    with open(record, encoding="utf-8") as f:
        for riga in f:
            rec = json.loads(riga)
            if rec["seed"] <= seed_prec:
                problemi.append(f"seed {rec['seed']} fuori ordine")
            seed_prec = rec["seed"]
            partite += 1
            rc = righe_carte(rec, mazzi)
            # Controlli riga per riga qui; il conto delle righe per carta alla fine.
            problemi += controlla_righe(rc, mazzi, 1)
            conti_carte.update((r["giocatore"], r["carta"]) for r in rc)
            buf["carte"] += rc
            buf["partite"].append(riga_partita(rec, terre))
            buf["mani"] += righe_mani(rec, terre)
            if partite % blocco == 0:
                svuota()
    svuota()
    for w in scrittori.values():
        w.close()
    for (chi, carta), n in conti_carte.items():
        if n != partite:
            problemi.append(f"{chi} {carta}: {n} righe su {partite} partite")
    return partite, problemi


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--record", default=str(RADICE / "data/metriche/v1/record.jsonl"))
    ap.add_argument("--out", default=str(RADICE / "data/metriche/v1"))
    args = ap.parse_args(argv)
    t0 = time.perf_counter()
    partite, problemi = converti(Path(args.record), Path(args.out))
    for p in problemi[:50]:
        print(p)
    dim = {n: (Path(args.out) / f"{n}.parquet").stat().st_size for n in ("partite", "carte", "mani")}
    print(f"Partite: {partite}; problemi: {len(problemi)}; tempo {time.perf_counter() - t0:.1f} s")
    print("Byte: " + ", ".join(f"{n}.parquet {b}" for n, b in dim.items()))
    return 0 if not problemi else 1


if __name__ == "__main__":
    sys.exit(main())
