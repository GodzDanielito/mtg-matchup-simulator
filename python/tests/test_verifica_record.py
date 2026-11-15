"""Test di metriche/verifica_record.py: il record compatto ricostruito dal registro completo (replay.jsonl).

Esecuzione:  uv run --no-project python -m unittest discover -s python/tests
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import verifica_record as vr  # noqa: E402

UW = "UW Blink"
RK = "Rakdos Delirium"
CARTE_UW = {"Plains", "Solitude", "Ephemerate", "Witch Enchanter", "Quantum Riddler", "Ocelot Pride", "Guide of Souls"}
CARTE_RK = {"Swamp", "Street Wraith", "Troll of Khazad-dûm", "Nethergoyf", "Thoughtseize", "Lightning Bolt"}


def mossa(turno, giocatore, carta, da, a, causa):
    return {"evento": "spostamento", "turno": turno, "giocatore": giocatore, "carta": carta, "da": da, "a": a,
            "causa": causa}


def decisione(turno, giocatore, tipo, azione, carta=None):
    d = {"evento": "decisione", "turno": turno, "giocatore": giocatore, "tipo": tipo, "azione": azione}
    if carta is not None:
        d["carta"] = carta
    return d


def cimitero(turno, giocatore, delirium):
    return {"evento": "cimitero", "turno": turno, "giocatore": giocatore, "delirium": delirium}


MANO_UW_1 = ["Plains", "Solitude", "Ephemerate", "Witch Enchanter", "Quantum Riddler", "Ocelot Pride", "Plains"]
MANO_UW_2 = ["Solitude", "Plains", "Witch Enchanter", "Ephemerate", "Plains", "Guide of Souls", "Quantum Riddler"]
MANO_RK = ["Swamp", "Street Wraith", "Troll of Khazad-dûm", "Nethergoyf", "Thoughtseize", "Lightning Bolt", "Swamp"]

REPLAY = [
    {"evento": "partita", "seed": 3, "giocatori": [UW, RK]},
    {"evento": "mano", "giocatore": UW, "carte": MANO_UW_1, "mulligan_fatti": 0},
    decisione(0, UW, "mulligan", "mulligan"),
    {"evento": "mano", "giocatore": RK, "carte": MANO_RK, "mulligan_fatti": 0},
    decisione(0, RK, "mulligan", "tiene"),
    {"evento": "mano", "giocatore": UW, "carte": MANO_UW_2, "mulligan_fatti": 1},
    decisione(0, UW, "mulligan", "tiene"),
    decisione(0, UW, "carte in fondo", "sceglie", "Plains"),
    {"evento": "turno", "turno": 1, "attivo": UW},
    mossa(1, UW, "Plains", "mano", "campo", "giocata"),
    mossa(1, UW, "Witch Enchanter", "mano", "esilio", "costo"),
    mossa(1, UW, "Solitude", "mano", "pila", "lanciata"),
    mossa(1, UW, "Gatto", "campo", "cimitero", "azione di stato"),  # un token: non e' nella lista
    {"evento": "turno", "turno": 2, "attivo": RK},
    {"evento": "pescata", "turno": 2, "giocatore": RK, "carta": "Swamp"},
    decisione(2, RK, "priorita'", "ciclo", "Troll of Khazad-dûm"),
    decisione(2, RK, "pagamento del mana", "modo di pagare 1"),
    mossa(2, RK, "Troll of Khazad-dûm", "mano", "cimitero", "scartata"),
    mossa(2, RK, "Swamp", "grimorio", "mano", "cercata"),
    mossa(2, RK, "Lightning Bolt", "grimorio", "cimitero", "macinata"),
    cimitero(2, RK, False),
    mossa(2, RK, "Nethergoyf", "grimorio", "cimitero", "sorveglianza"),
    cimitero(2, RK, True),
    mossa(2, RK, "Nethergoyf", "cimitero", "pila", "lanciata"),
    cimitero(2, RK, False),
    decisione(2, RK, "carta dalla mano rivelata", "sceglie", "Ephemerate"),
    mossa(2, UW, "Ephemerate", "mano", "cimitero", "scartata"),
    {"evento": "turno", "turno": 3, "attivo": UW},
    {"evento": "pescata", "turno": 3, "giocatore": UW, "carta": "Quantum Riddler"},
    mossa(3, UW, "Quantum Riddler", "mano", "pila", "lanciata"),
    mossa(3, UW, "Quantum Riddler", "campo", "esilio", "risoluzione"),
    mossa(3, UW, "Quantum Riddler", "esilio", "pila", "lanciata"),
    mossa(3, RK, "Swamp", "grimorio", "campo", "cercata"),
    mossa(3, UW, "Guide of Souls", "campo", "mano", "risoluzione"),
    cimitero(3, RK, True),
    {"evento": "fine", "vincitore": RK, "esito": "vinta da Rakdos"},
]


class TestRicostruisci(unittest.TestCase):
    def setUp(self):
        self.r = vr.ricostruisci(REPLAY, CARTE_UW, CARTE_RK)[3]

    def test_mani_fondo_mulligan(self):
        uw = self.r["uw"]
        self.assertEqual(uw["mulligan"], 1)
        self.assertEqual(uw["mani"], [MANO_UW_1, MANO_UW_2])
        self.assertEqual(uw["fondo"], ["Plains"])
        self.assertEqual(self.r["rakdos"]["mulligan"], 0)
        self.assertEqual(self.r["rakdos"]["fondo"], [])

    def test_eventi_uw(self):
        e = self.r["uw"]["eventi"]
        self.assertEqual(e["Plains"], [(0, "I"), (1, "T")])  # una Plains in fondo, l'altra tenuta e giocata
        self.assertEqual(e["Witch Enchanter"], [(0, "I"), (1, "E")])
        self.assertEqual(e["Solitude"], [(0, "I"), (1, "L")])
        self.assertEqual(e["Ephemerate"], [(0, "I"), (2, "S")])
        self.assertEqual(e["Quantum Riddler"], [(0, "I"), (3, "L"), (3, "P"), (3, "X")])
        self.assertEqual(e["Guide of Souls"], [(0, "I"), (3, "R")])
        self.assertNotIn("Gatto", e)

    def test_eventi_rakdos(self):
        e = self.r["rakdos"]["eventi"]
        self.assertEqual(e["Troll of Khazad-dûm"], [(0, "I"), (2, "K")])
        self.assertEqual(e["Swamp"], [(0, "I"), (0, "I"), (2, "C"), (2, "P"), (3, "F")])
        self.assertEqual(e["Lightning Bolt"], [(0, "I"), (2, "M")])
        self.assertEqual(e["Nethergoyf"], [(0, "I"), (2, "V"), (2, "Y")])

    def test_delirium(self):
        self.assertEqual(self.r["delirium"], {"turno": 2, "perso": 1, "fine": True})


class TestConfronta(unittest.TestCase):
    def record(self):
        def eventi(carte):
            return [{"c": c, "e": [[t, k] for t, k in ev]} for c, ev in carte]

        return {
            "seed": 3,
            "delirium": {"turno": 2, "turno_rakdos": 1, "perso": 1, "fine": True},
            "uw": {"mulligan": 1, "mani": [MANO_UW_1, MANO_UW_2], "fondo": ["Plains"],
                   "carte": eventi([("Plains", [(0, "I"), (1, "T")]), ("Witch Enchanter", [(0, "I"), (1, "E")]),
                                    ("Solitude", [(0, "I"), (1, "L")]), ("Ephemerate", [(0, "I"), (2, "S")]),
                                    ("Guide of Souls", [(0, "I"), (3, "R")]),
                                    ("Quantum Riddler", [(0, "I")]),
                                    ("Quantum Riddler", [(3, "P"), (3, "L"), (3, "X")])])},
            "rakdos": {"mulligan": 0, "mani": [MANO_RK], "fondo": [],
                       "carte": eventi([("Swamp", [(0, "I")]), ("Street Wraith", [(0, "I")]),
                                        ("Troll of Khazad-dûm", [(0, "I"), (2, "K")]),
                                        ("Nethergoyf", [(0, "I"), (2, "V"), (2, "Y")]),
                                        ("Thoughtseize", [(0, "I")]), ("Lightning Bolt", [(0, "I"), (2, "M")]),
                                        ("Swamp", [(0, "I")]), ("Swamp", [(2, "P")]), ("Swamp", [(2, "C")]),
                                        ("Swamp", [(3, "F")])])},
        }

    def test_record_uguale(self):
        ricostruito = vr.ricostruisci(REPLAY, CARTE_UW, CARTE_RK)[3]
        self.assertEqual(vr.confronta(self.record(), ricostruito), [])

    def test_differenze_trovate(self):
        ricostruito = vr.ricostruisci(REPLAY, CARTE_UW, CARTE_RK)[3]
        rec = self.record()
        rec["rakdos"]["carte"][2]["e"][1][1] = "S"  # il ciclo del Troll scritto come scarto
        rec["delirium"]["perso"] = 0
        rec["uw"]["fondo"] = []
        diff = vr.confronta(rec, ricostruito)
        self.assertEqual(len(diff), 3)


if __name__ == "__main__":
    unittest.main()
