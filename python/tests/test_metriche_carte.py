"""Test di metriche/metriche_carte.py: IWD, IWD relativo, IWD a turno fisso, OH7, curva delle copie, sinergia
e media degli IWD per mazzo, su un insieme sintetico con il risultato calcolato a mano (docs/metriche.md, sezioni 3-5).

Otto partite, UW con due carte (A, B), Rakdos con una (Z). Vittorie di UW: 1, 0, 1, 0, 1, 0, 0, 1.

    seed  UW in play  turni  vince UW | A: classe  turno_ip turno_vista copie_viste mano7 | B: classe turno_ip
      1     si          10      1     |    OH        0        0           1          1   |    GNS      -
      2     si           4      0     |    GD        3        3           1          0   |    OH       0
      3     no          12      1     |    GD        7        7           2          0   |    GD       9
      4     no           6      0     |    GNS       -        -           0          0   |    GNS      -
      5     si           8      1     |    GNS       -        -           0          0   |    GD       5
      6     no           5      0     |    cercata   -        2           1          0   |    GNS      -
      7     si           9      0     |    OH        0        0           3          1   |    OH       0
      8     no          11      1     |    GNS       -        -           0          0   |    OH       0

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import sys
import unittest
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import metriche_carte as mc  # noqa: E402

VINCE_UW = [1, 0, 1, 0, 1, 0, 0, 1]
PARTITE = pd.DataFrame({
    "seed": range(1, 9),
    "uw_in_play": [True, True, False, False, True, False, True, False],
    "turni": [10, 4, 12, 6, 8, 5, 9, 11],
    "vincitore": ["UW" if v else "Rakdos" for v in VINCE_UW],
})

N = None
A = [("OH", 0, 0, 1, 1), ("GD", 3, 3, 1, 0), ("GD", 7, 7, 2, 0), ("GNS", N, N, 0, 0),
     ("GNS", N, N, 0, 0), ("cercata", N, 2, 1, 0), ("OH", 0, 0, 3, 1), ("GNS", N, N, 0, 0)]
B = [("GNS", N, N, 0, 0), ("OH", 0, 0, 1, 1), ("GD", 9, 9, 1, 0), ("GNS", N, N, 0, 0),
     ("GD", 5, 5, 1, 0), ("GNS", N, N, 0, 0), ("OH", 0, 0, 1, 1), ("OH", 0, 0, 1, 1)]
# Rakdos (vince quando UW perde): Z GIH nelle partite 1, 4, 6.
Z = [("OH", 0, 0, 1, 1), ("GNS", N, N, 0, 0), ("GNS", N, N, 0, 0), ("GD", 8, 8, 1, 0),
     ("GNS", N, N, 0, 0), ("OH", 0, 0, 1, 1), ("GNS", N, N, 0, 0), ("GNS", N, N, 0, 0)]


def _righe(chi, carta, dati):
    return [{"seed": s, "giocatore": chi, "carta": carta, "classe": c, "turno_ip": ip, "turno_vista": tv,
             "copie_viste": cv, "copie_mano7": m7} for s, (c, ip, tv, cv, m7) in zip(range(1, 9), dati)]


CARTE = pd.DataFrame(_righe("uw", "A", A) + _righe("uw", "B", B) + _righe("rakdos", "Z", Z))
# turno_ipc (in mano per qualsiasi via: I, P o C): come turno_ip, piu' la ricerca di A nella partita 6 (turno 2).
CARTE["turno_ipc"] = CARTE["turno_ip"]
CARTE.loc[(CARTE["carta"] == "A") & (CARTE["seed"] == 6), "turno_ipc"] = 2
for col in ("turno_ip", "turno_vista", "turno_ipc"):
    CARTE[col] = CARTE[col].astype("Int16")

NOMI = {"uw": ["A", "B"], "rakdos": ["Z"]}
COPPIE = {"uw": [("A", "B")], "rakdos": []}


class TestMetrichePunto(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = mc.calcola(PARTITE, CARTE, NOMI, COPPIE, turni=(3,), b=0)
        cls.c = cls.r.carte.set_index(["giocatore", "carta"])

    def v(self, chi, carta, col):
        return self.c.loc[(chi, carta), col]

    def test_classi_e_win_rate(self):
        self.assertEqual(self.v("uw", "A", "n_GIH"), 4)
        self.assertEqual(self.v("uw", "A", "n_GNS"), 3)
        self.assertEqual(self.v("uw", "A", "n_cercata"), 1)
        self.assertEqual(self.v("uw", "A", "n_OH"), 2)
        self.assertAlmostEqual(self.v("uw", "A", "wr_GIH"), 0.5)
        self.assertAlmostEqual(self.v("uw", "A", "wr_GNS"), 2 / 3)
        self.assertAlmostEqual(self.v("uw", "A", "wr_OH"), 0.5)  # partite 1 e 7

    def test_iwd_classico(self):
        self.assertAlmostEqual(self.v("uw", "A", "iwd"), 0.5 - 2 / 3)    # -0,1667
        self.assertAlmostEqual(self.v("uw", "B", "iwd"), 0.6 - 1 / 3)    # GIH 3/5, GNS 1/3
        self.assertAlmostEqual(self.v("rakdos", "Z", "iwd"), 2 / 3 - 2 / 5)

    def test_iwd_play_e_draw(self):
        # UW in play: partite 1, 2, 5, 7. A: GIH {1, 2, 7} 1/3, GNS {5} 1.
        self.assertAlmostEqual(self.v("uw", "A", "iwd_play"), 1 / 3 - 1)
        # UW in draw: partite 3, 4, 6, 8. A: GIH {3} 1, GNS {4, 8} 1/2.
        self.assertAlmostEqual(self.v("uw", "A", "iwd_draw"), 1 - 0.5)

    def test_iwd_relativo(self):
        # Media degli IWD di UW pesata per le partite GIH: (4 * -1/6 + 5 * 4/15) / 9 = 2/27.
        media = (4 * (-1 / 6) + 5 * (4 / 15)) / 9
        self.assertAlmostEqual(self.v("uw", "A", "iwd_rel"), -1 / 6 - media)
        self.assertAlmostEqual(self.v("uw", "B", "iwd_rel"), 4 / 15 - media)
        self.assertAlmostEqual(self.v("rakdos", "Z", "iwd_rel"), 0.0)

    def test_iwd_a_turno_fisso(self):
        # Turno 3 di UW: turno della partita 5 in play, 6 in draw. Ancora in corso: 1, 3, 4, 5, 7, 8.
        # A vista entro il turno: {1, 7} (1/2); non vista: {3, 4, 5, 8} (3/4).
        self.assertEqual(self.v("uw", "A", "n_t3_si"), 2)
        self.assertEqual(self.v("uw", "A", "n_t3_no"), 4)
        self.assertAlmostEqual(self.v("uw", "A", "iwd_t3"), 0.5 - 0.75)
        # B vista: {5, 7, 8} (2/3); non vista: {1, 3, 4} (2/3).
        self.assertAlmostEqual(self.v("uw", "B", "iwd_t3"), 0.0)

    def test_oh7_e_curva(self):
        self.assertEqual(self.v("uw", "A", "n_OH7"), 2)
        self.assertAlmostEqual(self.v("uw", "A", "wr_OH7"), 0.5)
        self.assertAlmostEqual(self.v("uw", "A", "wr_non_OH7"), 0.5)
        self.assertAlmostEqual(self.v("uw", "A", "oh7_diff"), 0.0)
        self.assertEqual(self.v("uw", "A", "n_copie_0"), 3)
        self.assertAlmostEqual(self.v("uw", "A", "wr_copie_0"), 2 / 3)
        self.assertAlmostEqual(self.v("uw", "A", "wr_copie_1"), 1 / 3)
        self.assertAlmostEqual(self.v("uw", "A", "wr_copie_2"), 1.0)
        self.assertAlmostEqual(self.v("uw", "A", "wr_copie_3+"), 0.0)

    def test_sinergia(self):
        c = self.r.coppie.set_index(["giocatore", "a", "b"]).loc[("uw", "A", "B")]
        # Celle: AB {2, 3, 7} 1/3, A non B {1} 1, non A B {5, 8} 1, nessuna {4} 0; partita 6 esclusa (A cercata).
        self.assertEqual(int(c["n_AB"]), 3)
        self.assertAlmostEqual(c["wr_AB"], 1 / 3)
        self.assertAlmostEqual(c["sinergia"], (1 / 3 - 1) - (1 - 0))
        # IWD di A condizionato a B vista e a B non vista.
        self.assertAlmostEqual(c["iwd_a_con_b"], 1 / 3 - 1)
        self.assertAlmostEqual(c["iwd_a_senza_b"], 1 - 0)
        # Turno 3: AB {7} 0, A non B {1} 1, non A B {5, 8} 1, nessuna {3, 4} 1/2.
        self.assertAlmostEqual(c["sinergia_t3"], (0 - 1) - (1 - 0.5))

    def test_media_per_mazzo(self):
        b = self.r.bias.set_index("metrica")
        media_uw = (4 * (-1 / 6) + 5 * (4 / 15)) / 9
        self.assertAlmostEqual(b.loc["iwd", "uw"], media_uw)
        self.assertAlmostEqual(b.loc["iwd", "rakdos"], 4 / 15)
        self.assertAlmostEqual(b.loc["iwd", "differenza"], media_uw - 4 / 15)
        self.assertAlmostEqual(b.loc["iwd_rel", "uw"], 0.0)


class TestVarianteQualsiasiVia(unittest.TestCase):
    """Coppia con la seconda carta "in mano per qualsiasi via" (I, P o C): Recruiter + Solitude, dove la Solitude
    cercata da Recruiter e' fuori da GIH per definizione. Qui la coppia (B, A), con A cercata nella partita 6."""

    def setUp(self):
        self.r = mc.calcola(PARTITE, CARTE, NOMI, {"uw": [], "rakdos": []}, turni=(3,), b=0,
                            coppie_ipc={"uw": [("B", "A")]})
        cp = self.r.coppie
        self.c = cp[(cp["a"] == "B") & (cp["b"] == "A") & (cp["variante"] == "ipc")].iloc[0]

    def test_celle_e_sinergia(self):
        # A vista per qualsiasi via: {1, 2, 3, 6, 7}; GNS {4, 5, 8}. B GIH {2, 3, 5, 7, 8}, GNS {1, 4, 6}.
        # BA {2, 3, 7} 1/3; B senza A {5, 8} 1; A senza B {1, 6} 1/2; nessuna {4} 0.
        self.assertEqual(int(self.c["n_AB"]), 3)
        self.assertEqual(int(self.c["n_nAB"]), 2)
        self.assertAlmostEqual(self.c["wr_nAB"], 0.5)
        self.assertAlmostEqual(self.c["sinergia"], (1 / 3 - 1) - (0.5 - 0))
        # Turno 3 (in corso {1, 3, 4, 5, 7, 8}): B vista {5, 7, 8}, non vista {1, 3, 4}; A in mano {1, 7}, non
        # vista {3, 4, 5, 8}. BA {7} 0; B senza A {5, 8} 1; A senza B {1} 1; nessuna {3, 4} 1/2.
        self.assertAlmostEqual(self.c["sinergia_t3"], (0 - 1) - (1 - 0.5))

    def test_la_coppia_normale_resta(self):
        # Senza la variante la partita 6 (A solo cercata) e' fuori: la sinergia classica e' un'altra.
        r = mc.calcola(PARTITE, CARTE, NOMI, {"uw": [("B", "A")], "rakdos": []}, turni=(3,), b=0)
        c = r.coppie.set_index(["giocatore", "a", "b"]).loc[("uw", "B", "A")]
        self.assertAlmostEqual(c["sinergia"], (1 / 3 - 1) - (1 - 0))
        self.assertEqual(c["variante"], "")


class TestBenjaminiHochberg(unittest.TestCase):
    def test_soglie_a_mano(self):
        # m = 4, q = 0,05: soglie 0,0125, 0,025, 0,0375, 0,05 sui p ordinati.
        self.assertEqual(mc.benjamini_hochberg([0.01, 0.04, 0.03, 0.5]).tolist(), [True, False, False, False])
        self.assertEqual(mc.benjamini_hochberg([0.04, 0.01, 0.03, 0.02]).tolist(), [True, True, True, True])
        # Il passo verso l'alto: 0,04 supera la sua soglia (0,0375) ma non serve, perche' 0,045 <= 0,05.
        self.assertEqual(mc.benjamini_hochberg([0.001, 0.04, 0.045, 0.3, 0.9]).tolist(),
                         [True, False, False, False, False])
        self.assertEqual(mc.benjamini_hochberg([0.2, 0.3]).tolist(), [False, False])


class TestMatrice(unittest.TestCase):
    def test_matrice_a_turno_fisso(self):
        nomi = {"uw": ["A", "B"], "rakdos": ["Z"]}
        r = mc.calcola(PARTITE, CARTE, nomi, {"uw": [], "rakdos": []}, turni=(3,), b=50, seed=5, matrice=True)
        c = r.coppie.set_index(["giocatore", "a", "b"]).loc[("uw", "A", "B")]
        self.assertFalse(c["decisa"])
        # Una coppia della matrice ha la sinergia classica e quella al turno centrale (qui T3), con i p.
        self.assertAlmostEqual(c["sinergia"], (1 / 3 - 1) - (1 - 0))
        self.assertAlmostEqual(c["sinergia_t3"], (0 - 1) - (1 - 0.5))
        self.assertIn("bh_5", r.coppie.columns)
        self.assertIn("bh_5_t3", r.coppie.columns)


class TestBootstrapRipetibile(unittest.TestCase):
    def test_stesso_seed_stessi_intervalli(self):
        r1 = mc.calcola(PARTITE, CARTE, NOMI, COPPIE, turni=(3,), b=50, seed=5)
        r2 = mc.calcola(PARTITE, CARTE, NOMI, COPPIE, turni=(3,), b=50, seed=5)
        pd.testing.assert_frame_equal(r1.carte, r2.carte)
        self.assertIn("iwd_lo", r1.carte.columns)
        self.assertIn("sinergia_hi", r1.coppie.columns)


class TestEtichettaDeiDati(unittest.TestCase):
    """Dal passo 0 della ricerca (un generatore per giocatore) le tabelle generate dicono con quale generatore sono
    state calcolate: i dati di oggi sono degli euristici v1 con il generatore unico."""

    def test_titolo_con_il_generatore(self):
        self.assertEqual(mc.ETICHETTA_DATI, "euristici v1, generatore unico")
        self.assertEqual(mc.titolo(), "# Risultati delle metriche per carta: euristici v1, generatore unico\n")


if __name__ == "__main__":
    unittest.main()
