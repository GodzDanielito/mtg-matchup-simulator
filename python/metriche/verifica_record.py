"""Seconda implementazione del record compatto (docs/metriche.md, sezione 11): lo ricostruisce dal registro
completo (replay.jsonl di matchup.exe) e lo confronta con quello scritto da metriche.exe (record.jsonl).

Il registro completo non ha l'identita' delle copie (solo i nomi), quindi il confronto e' per carta: la lista
ordinata degli eventi (turno, codice) di tutte le copie con quel nome. In piu': mani del mulligan, carte in
fondo, mulligan, turno del delirium di Rakdos e quante volte lo perde.

Uso:
    uv run --no-project python python/metriche/verifica_record.py --replay REPLAY.jsonl --record RECORD.jsonl
"""

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

GIOCATORI = {"UW Blink": "uw", "Rakdos Delirium": "rakdos"}


def _codice(mossa, ciclo):
    """Il codice di uno spostamento dopo il turno 0, None se non e' un evento della copia."""
    da, a, causa = mossa["da"], mossa["a"], mossa["causa"]
    if da == "grimorio":
        if a == "mano":
            return {"pescata": "P", "cercata": "C"}.get(causa, "?")
        if a in ("cimitero", "esilio"):
            return {"macinata": "M", "sorveglianza": "V"}.get(causa, "?")
        if a == "campo":
            return "F" if causa == "cercata" else "?"
        return "?"
    if da == "mano":
        if a == "pila" and causa == "lanciata":
            return "L"
        if a == "campo" and causa == "giocata":
            return "T"
        if a == "esilio" and causa == "costo":
            return "E"
        if a in ("cimitero", "esilio") and causa == "scartata":
            return "K" if ciclo else "S"
        return "?"
    if a == "mano":
        return "R" if da == "campo" else "?"
    if a == "pila" and causa == "lanciata":
        return {"cimitero": "Y", "esilio": "X"}.get(da)
    return None


def ricostruisci(righe, carte_uw, carte_rakdos):
    """Dal registro completo (righe JSON gia' lette) al record di ogni partita, per seed."""
    nella_lista = {"uw": set(carte_uw), "rakdos": set(carte_rakdos)}
    partite = {}
    p = None
    for r in righe:
        ev = r["evento"]
        if ev == "partita":
            p = {
                "uw": {"mulligan": 0, "mani": [], "fondo": [], "eventi": defaultdict(list)},
                "rakdos": {"mulligan": 0, "mani": [], "fondo": [], "eventi": defaultdict(list)},
                "delirium": None,
                "_iniziata": False,
                "_ciclo": {"uw": Counter(), "rakdos": Counter()},
                "_delirium": False,
            }
            partite[r["seed"]] = p
        elif ev == "mano":
            p[GIOCATORI[r["giocatore"]]]["mani"].append(list(r["carte"]))
        elif ev == "turno":
            if not p["_iniziata"]:  # la mano tenuta: l'ultima mano vista meno le carte in fondo
                p["_iniziata"] = True
                for chi in ("uw", "rakdos"):
                    g = p[chi]
                    tenuta = Counter(g["mani"][-1]) - Counter(g["fondo"])
                    for carta, n in tenuta.items():
                        g["eventi"][carta].extend([(0, "I")] * n)
        elif ev == "decisione":
            chi = GIOCATORI[r["giocatore"]]
            if r["tipo"] == "mulligan" and r["azione"] == "mulligan":
                p[chi]["mulligan"] += 1
            elif r["tipo"] == "carte in fondo":
                p[chi]["fondo"].append(r["carta"])
            elif r.get("azione") == "ciclo":
                p["_ciclo"][chi][r["carta"]] += 1
        elif ev == "pescata":
            chi = GIOCATORI[r["giocatore"]]
            p[chi]["eventi"][r["carta"]].append((r["turno"], "P"))
        elif ev == "spostamento":
            chi = GIOCATORI[r["giocatore"]]
            carta = r["carta"]
            if carta not in nella_lista[chi]:
                continue  # i token
            cicli = p["_ciclo"][chi]
            ciclo = r["da"] == "mano" and cicli[carta] > 0
            if r["da"] == "mano" and cicli[carta] > 0:
                cicli[carta] -= 1  # l'attivazione del ciclo vale per la prossima uscita dalla mano
            codice = _codice(r, ciclo)
            if codice is not None:
                p[chi]["eventi"][carta].append((r["turno"], codice))
        elif ev == "cimitero" and GIOCATORI[r["giocatore"]] == "rakdos":
            ora = r["delirium"]
            if ora and p["delirium"] is None:
                p["delirium"] = {"turno": r["turno"], "perso": 0, "fine": True}
            if p["delirium"] is not None:
                if p["_delirium"] and not ora:
                    p["delirium"]["perso"] += 1
                p["delirium"]["fine"] = ora
            p["_delirium"] = ora
    for p in partite.values():
        for chi in ("uw", "rakdos"):
            p[chi]["eventi"] = {c: sorted(e) for c, e in p[chi]["eventi"].items()}
        for k in [k for k in p if k.startswith("_")]:
            del p[k]
    return partite


def eventi_per_carta(giocatore):
    """Dal record di un giocatore: carta -> lista ordinata degli eventi di tutte le sue copie."""
    out = defaultdict(list)
    for copia in giocatore["carte"]:
        out[copia["c"]].extend((t, k) for t, k in copia["e"])
    return {c: sorted(e) for c, e in out.items()}


def confronta(record, ricostruito):
    """Le differenze tra il record di metriche.exe e quello ricostruito; vuota se coincidono."""
    diff = []
    seed = record["seed"]
    for chi in ("uw", "rakdos"):
        a, b = record[chi], ricostruito[chi]
        for campo in ("mulligan", "mani", "fondo"):
            if a[campo] != b[campo]:
                diff.append(f"seed {seed}, {chi}: {campo} {a[campo]} contro {b[campo]}")
        ea, eb = eventi_per_carta(a), b["eventi"]
        for carta in sorted(set(ea) | set(eb)):
            if ea.get(carta, []) != eb.get(carta, []):
                diff.append(f"seed {seed}, {chi}, {carta}: {ea.get(carta, [])} contro {eb.get(carta, [])}")
    da = record["delirium"]
    da = None if da is None else {"turno": da["turno"], "perso": da["perso"], "fine": da["fine"]}
    if da != ricostruito["delirium"]:
        diff.append(f"seed {seed}: delirium {da} contro {ricostruito['delirium']}")
    return diff


def _nomi(lista):
    return {voce["card"] for voce in json.loads(Path(lista).read_text(encoding="utf-8"))["main"]}


def main(argv=None):
    radice = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--replay", required=True, help="replay.jsonl di matchup.exe")
    ap.add_argument("--record", required=True, help="record.jsonl di metriche.exe (stessi seed)")
    args = ap.parse_args(argv)
    with open(args.replay, encoding="utf-8") as f:
        ricostruiti = ricostruisci((json.loads(r) for r in f),
                                   _nomi(radice / "config/decks/uw_blink_sim.json"),
                                   _nomi(radice / "config/decks/rakdos_delirium.json"))
    diff, confrontate = [], 0
    with open(args.record, encoding="utf-8") as f:
        for riga in f:
            rec = json.loads(riga)
            if rec["seed"] in ricostruiti:
                confrontate += 1
                diff.extend(confronta(rec, ricostruiti[rec["seed"]]))
    for d in diff:
        print(d)
    print(f"Partite confrontate: {confrontate} su {len(ricostruiti)} nel registro completo; differenze: {len(diff)}")
    return 0 if not diff and confrontate == len(ricostruiti) else 1


if __name__ == "__main__":
    sys.exit(main())
