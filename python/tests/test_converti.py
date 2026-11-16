"""Test di metriche/converti.py: dal record compatto alle righe delle tabelle (docs/metriche.md, sezioni 2-3 e 10).

Il punto centrale e' la partizione di ogni (partita, carta) in OH, GD, "solo cercata", "vista solo dal cimitero"
e GNS: costruita a mano su record scritti qui, con la classe attesa di ogni carta.

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import sys
import unittest
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import converti as cv  # noqa: E402

MAZZI = {
    "uw": {"Plains": 4, "Solitude": 4, "Ephemerate": 3, "Recruiter of the Guard": 1, "Teferi, Time Raveler": 1,
           "Witch Enchanter": 4, "Quantum Riddler": 4},
    "rakdos": {"Swamp": 4, "Nethergoyf": 4, "Lightning Bolt": 3, "Thoughtseize": 4, "Detective's Phoenix": 4,
               "Street Wraith": 4},
}
TERRE = {"Plains", "Swamp"}


def copia(nome, *eventi):
    return {"c": nome, "e": [list(e) for e in eventi]}


def giocatore(mani, carte, mulligan=0, fondo=(), terre=(1, 2), mano_fine=0):
    return {"mulligan": mulligan, "mani": mani, "fondo": list(fondo), "terre": list(terre), "mano_fine": mano_fine,
            "carte": carte}


MANO_UW = ["Plains", "Solitude", "Witch Enchanter", "Quantum Riddler", "Plains", "Ephemerate", "Plains"]
MANO_UW_6 = ["Plains", "Solitude", "Witch Enchanter", "Plains", "Quantum Riddler", "Plains", "Solitude"]
MANO_RK = ["Swamp", "Thoughtseize", "Street Wraith", "Swamp", "Lightning Bolt", "Swamp", "Nethergoyf"]

# Partita 1, UW in play. UW: mulligan a 6 (la prima mano aveva Ephemerate), una Plains in fondo.
#   Solitude: una copia nella mano tenuta e lanciata, un'altra pescata e rimasta in mano -> OH (non GD).
#   Ephemerate: pescata e rimasta in mano -> GD.   Teferi: solo cercato -> solo cercata.
#   Witch Enchanter: tenuta e giocata come terra -> OH.   Recruiter: mai -> GNS.
# Rakdos (in draw): Nethergoyf macinata e lanciata dal cimitero -> vista solo dal cimitero; Bolt macinato
#   e mai usato -> GNS; Thoughtseize nella mano e lanciato -> OH; Phoenix pescata e lanciata -> GD;
#   Swamp cercata -> solo cercata (terra); Street Wraith tenuta e ciclata -> OH.
REC1 = {
    "seed": 1, "uw_in_play": True, "vincitore": "UW", "fine": "vita a zero", "turni": 9, "vita_uw": 5,
    "vita_rakdos": -2, "delirium": {"turno": 6, "turno_rakdos": 3, "perso": 1, "fine": True},
    "uw": giocatore([MANO_UW, MANO_UW_6], [
        copia("Plains", (0, "I"), (1, "T")),
        copia("Solitude", (0, "I"), (3, "L")),
        copia("Witch Enchanter", (0, "I"), (3, "T")),
        copia("Plains", (0, "I"), (5, "T")),
        copia("Quantum Riddler", (0, "I"), (5, "L"), (5, "X")),
        copia("Solitude", (0, "I"), (7, "L")),
        copia("Ephemerate", (5, "P")),
        copia("Solitude", (7, "P")),
        copia("Teferi, Time Raveler", (7, "C"), (9, "L")),
    ], mulligan=1, fondo=["Plains"], terre=(1, 2, 3, 3, 4), mano_fine=2),
    "rakdos": giocatore([MANO_RK], [
        copia("Swamp", (0, "I"), (2, "T")),
        copia("Thoughtseize", (0, "I"), (2, "L")),
        copia("Street Wraith", (0, "I"), (4, "K")),
        copia("Swamp", (0, "I"), (4, "T")),
        copia("Lightning Bolt", (0, "I"), (6, "L")),
        copia("Swamp", (0, "I")),
        copia("Nethergoyf", (0, "I"), (2, "L")),
        copia("Nethergoyf", (4, "M"), (6, "Y")),
        copia("Lightning Bolt", (4, "M")),
        copia("Detective's Phoenix", (6, "P"), (8, "L")),
        copia("Swamp", (4, "C")),
    ], terre=(0, 1, 2, 2), mano_fine=2),
}

# Partita 2, UW in draw: quasi niente visto. UW tiene una 7 senza Solitude; Rakdos tiene la sua 7.
MANO_UW_2 = ["Plains", "Plains", "Witch Enchanter", "Ephemerate", "Quantum Riddler", "Plains", "Recruiter of the Guard"]
REC2 = {
    "seed": 2, "uw_in_play": False, "vincitore": "Rakdos", "fine": "vita a zero", "turni": 6, "vita_uw": 0,
    "vita_rakdos": 20, "delirium": None,
    "uw": giocatore([MANO_UW_2], [copia(c, (0, "I")) for c in MANO_UW_2], terre=(1, 2, 3), mano_fine=4),
    "rakdos": giocatore([MANO_RK], [copia(c, (0, "I")) for c in MANO_RK], terre=(1, 2, 3), mano_fine=4),
}


def per_carta(righe, chi):
    return {r["carta"]: r for r in righe if r["giocatore"] == chi}


class TestClassi(unittest.TestCase):
    def setUp(self):
        self.righe = cv.righe_carte(REC1, MAZZI)
        self.uw = per_carta(self.righe, "uw")
        self.rk = per_carta(self.righe, "rakdos")

    def test_una_riga_per_carta_della_lista(self):
        self.assertEqual(set(self.uw), set(MAZZI["uw"]))
        self.assertEqual(set(self.rk), set(MAZZI["rakdos"]))
        self.assertEqual(self.uw["Solitude"]["copie_lista"], 4)

    def test_classi_uw(self):
        attese = {"Solitude": "OH", "Ephemerate": "GD", "Teferi, Time Raveler": "cercata", "Witch Enchanter": "OH",
                  "Recruiter of the Guard": "GNS", "Quantum Riddler": "OH", "Plains": "OH"}
        self.assertEqual({c: r["classe"] for c, r in self.uw.items()}, attese)

    def test_classi_rakdos(self):
        attese = {"Nethergoyf": "OH", "Lightning Bolt": "OH", "Thoughtseize": "OH", "Detective's Phoenix": "GD",
                  "Swamp": "OH", "Street Wraith": "OH"}
        self.assertEqual({c: r["classe"] for c, r in self.rk.items()}, attese)

    def test_vista_solo_dal_cimitero_e_gns_da_macinata(self):
        # Nella partita 1 Nethergoyf e Bolt sono anche nella mano iniziale: tolta quella copia, una Nethergoyf
        # macinata e lanciata dal cimitero e' "vista solo dal cimitero", un Bolt macinato e mai usato e' GNS.
        rec = {**REC1, "rakdos": {**REC1["rakdos"], "carte": [
            copia("Nethergoyf", (4, "M"), (6, "Y")), copia("Lightning Bolt", (4, "M"))]}}
        rk = per_carta(cv.righe_carte(rec, MAZZI), "rakdos")
        self.assertEqual(rk["Nethergoyf"]["classe"], "cimitero")
        self.assertEqual(rk["Lightning Bolt"]["classe"], "GNS")
        self.assertEqual(rk["Lightning Bolt"]["k_M"], 1)

    def test_conti_delle_copie(self):
        sol = self.uw["Solitude"]
        self.assertEqual(sol["copie_viste"], 3)
        self.assertEqual(sol["k_I"], 2)
        self.assertEqual(sol["k_P"], 1)
        self.assertEqual(sol["k_L"], 2)
        self.assertEqual(sol["copie_lanciate"], 2)
        self.assertEqual(sol["copie_mano_fine"], 1)  # la copia pescata al turno 7
        self.assertEqual(sol["copie_mano7"], 1)       # la prima mano da 7 ha una Solitude
        self.assertEqual(self.uw["Ephemerate"]["copie_mano7"], 1)
        self.assertEqual(self.uw["Ephemerate"]["copie_mano_fine"], 1)
        self.assertEqual(self.uw["Recruiter of the Guard"]["copie_viste"], 0)
        # La carta mandata in fondo non ha eventi: la Plains in fondo non e' tra le copie.
        self.assertEqual(self.uw["Plains"]["copie_viste"], 2)

    def test_turni(self):
        sol = self.uw["Solitude"]
        self.assertEqual(sol["turno_ip"], 0)
        self.assertEqual(sol["turno_vista"], 0)
        # Turni del giocatore del primo lancio di ogni copia lanciata: UW in play, turni della partita 3 e 7.
        self.assertEqual(sorted(sol["lanci_turno"]), [2, 4])
        eph = self.uw["Ephemerate"]
        self.assertEqual((eph["turno_ip"], eph["turno_vista"]), (5, 5))
        tef = self.uw["Teferi, Time Raveler"]
        self.assertIsNone(tef["turno_ip"])
        self.assertEqual(tef["turno_vista"], 7)
        self.assertIsNone(self.uw["Recruiter of the Guard"]["turno_vista"])
        # Rakdos in draw: Phoenix lanciata al turno della partita 8 = suo turno 4.
        self.assertEqual(self.rk["Detective's Phoenix"]["lanci_turno"], [4])
        # Nethergoyf: una copia lanciata dalla mano al turno 2 (suo turno 1), l'altra dal cimitero al 6 (turno 3).
        self.assertEqual(sorted(self.rk["Nethergoyf"]["lanci_turno"]), [1, 3])


class TestPartizione(unittest.TestCase):
    def test_partizione_su_piu_partite(self):
        recs = [REC1, REC2, {**REC1, "seed": 3}, {**REC2, "seed": 4, "uw_in_play": True}]
        righe = [r for rec in recs for r in cv.righe_carte(rec, MAZZI)]
        for chi, mazzo in MAZZI.items():
            for carta in mazzo:
                classi = Counter(r["classe"] for r in righe if r["giocatore"] == chi and r["carta"] == carta)
                # Ogni partita cade in esattamente una classe.
                self.assertEqual(sum(classi.values()), len(recs), (chi, carta))
                self.assertTrue(set(classi) <= set(cv.CLASSI), (chi, carta, classi))
        # OH e GD disgiunte: una carta con una copia iniziale e una pescata e' solo OH.
        sol = [r for r in righe if r["giocatore"] == "uw" and r["carta"] == "Solitude" and r["seed"] == 1][0]
        self.assertEqual(sol["classe"], "OH")

    def test_controllo_partizione(self):
        righe = [r for rec in (REC1, REC2) for r in cv.righe_carte(rec, MAZZI)]
        self.assertEqual(cv.controlla_righe(righe, MAZZI, partite=2), [])
        rotte = [dict(r) for r in righe]
        rotte[0]["copie_viste"] = 99  # piu' copie viste che nella lista
        rotte.append(dict(righe[1]))  # una (partita, carta) contata due volte
        self.assertEqual(len(cv.controlla_righe(rotte, MAZZI, partite=2)), 2)


class TestPartita(unittest.TestCase):
    def test_riga_partita(self):
        p = cv.riga_partita(REC1, TERRE)
        self.assertEqual(p["seed"], 1)
        self.assertTrue(p["vince_uw"])
        self.assertEqual(p["delirium_turno_rakdos"], 3)
        self.assertEqual(p["mulligan_uw"], 1)
        # Mano tenuta di UW: MANO_UW_6 meno una Plains in fondo -> 2 Plains vere, Witch a parte.
        self.assertEqual(p["terre_mano_uw"], 2)
        self.assertEqual(p["witch_mano_uw"], 1)
        self.assertEqual(p["terre_mano_rakdos"], 3)
        self.assertEqual(p["terre_uw"], [1, 2, 3, 3, 4])
        p2 = cv.riga_partita(REC2, TERRE)
        self.assertIsNone(p2["delirium_turno_rakdos"])

    def test_righe_mani(self):
        mani = cv.righe_mani(REC1, TERRE)
        uw = [m for m in mani if m["giocatore"] == "uw"]
        self.assertEqual([m["indice"] for m in uw], [0, 1])
        self.assertEqual([m["tenuta"] for m in uw], [False, True])
        self.assertEqual(uw[0]["carte"], MANO_UW)
        self.assertEqual(uw[0]["terre"], 3)


if __name__ == "__main__":
    unittest.main()
