"""Test di metriche/statistica.py: intervallo di Wilson, formule normali e bootstrap (docs/metriche.md, sezione 5).

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import statistica as st  # noqa: E402


class TestWilson(unittest.TestCase):
    def test_40_su_100(self):
        # Calcolo a mano con z = 1,96: centro (0,4 + 0,019208) / 1,038416 = 0,40370;
        # semiampiezza 1,96 / 1,038416 * sqrt(0,0024 + 0,00009604) = 0,09430.
        p, lo, hi = st.wilson(40, 100)
        self.assertAlmostEqual(p, 0.4)
        self.assertAlmostEqual(lo, 0.3094, places=4)
        self.assertAlmostEqual(hi, 0.4980, places=4)

    def test_0_su_10(self):
        # Con p = 0 centro e semiampiezza coincidono: 0,19208 / 1,38416 = 0,13877, quindi [0; 0,2775].
        p, lo, hi = st.wilson(0, 10)
        self.assertEqual(p, 0.0)
        self.assertAlmostEqual(lo, 0.0, places=12)
        self.assertAlmostEqual(hi, 0.2775, places=4)

    def test_10_su_10_simmetrico(self):
        p, lo, hi = st.wilson(10, 10)
        self.assertEqual(p, 1.0)
        self.assertAlmostEqual(lo, 1 - 0.2775, places=4)
        self.assertAlmostEqual(hi, 1.0, places=12)

    def test_nessuna_partita(self):
        self.assertTrue(all(math.isnan(x) for x in st.wilson(0, 0)))


class TestFormuleNormali(unittest.TestCase):
    def test_appaiata(self):
        d = np.array([1, 0, -1, 1, 1, 0, 0, 1], dtype=float)  # media 3/8
        m, lo, hi = st.normale_appaiata(d)
        se = d.std(ddof=1) / math.sqrt(len(d))
        self.assertAlmostEqual(m, 0.375)
        self.assertAlmostEqual(lo, 0.375 - st.Z95 * se)
        self.assertAlmostEqual(hi, 0.375 + st.Z95 * se)

    def test_due_proporzioni(self):
        diff, lo, hi = st.normale_due_proporzioni(60, 100, 40, 100)
        se = math.sqrt(0.6 * 0.4 / 100 + 0.4 * 0.6 / 100)
        self.assertAlmostEqual(diff, 0.2)
        self.assertAlmostEqual(lo, 0.2 - st.Z95 * se)
        self.assertAlmostEqual(hi, 0.2 + st.Z95 * se)


class TestBootstrap(unittest.TestCase):
    def test_pesi_ripetibili_e_somma_n(self):
        a = st.pesi_bootstrap(1000, 50, seed=7)
        b = st.pesi_bootstrap(1000, 50, seed=7)
        c = st.pesi_bootstrap(1000, 50, seed=8)
        self.assertEqual(a.shape, (50, 1000))
        self.assertTrue(np.array_equal(a, b))
        self.assertFalse(np.array_equal(a, c))
        self.assertTrue(np.all(a.sum(axis=1) == 1000))  # ogni ricampionamento ha n partite

    def test_somme_ripetibili(self):
        rng = np.random.default_rng(1)
        x = rng.integers(0, 2, size=(5000, 3)).astype(np.float32)
        s1 = st.somme_bootstrap(x, b=200, seed=3)
        s2 = st.somme_bootstrap(x, b=200, seed=3)
        self.assertEqual(s1.shape, (200, 3))
        self.assertTrue(np.array_equal(s1, s2))
        # Le somme sono esatte: con i pesi interi coincidono con il conto fatto a mano.
        w = st.pesi_bootstrap(5000, 200, seed=3)
        self.assertTrue(np.array_equal(s1, w.astype(np.float64) @ x.astype(np.float64)))

    def test_differenza_appaiata_come_la_formula(self):
        # Due esiti binari correlati sullo stesso seed (come due varianti dell'agente): bootstrap delle coppie
        # contro la formula normale appaiata di varianti_uw. Devono coincidere entro 0,05 punti.
        n = 100_000
        rng = np.random.default_rng(11)
        a = (rng.random(n) < 0.40).astype(np.float32)
        cambia = rng.random(n) < 0.10
        b = np.where(cambia, (rng.random(n) < 0.45), a).astype(np.float32)
        s = st.somme_bootstrap(np.column_stack([a, b]), b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP)
        lo, hi = st.percentili((s[:, 0] - s[:, 1]) / n)
        _, lo_n, hi_n = st.normale_appaiata(a.astype(float) - b.astype(float))
        self.assertLess(abs(lo - lo_n), 0.0005)
        self.assertLess(abs(hi - hi_n), 0.0005)

    def test_differenza_tra_gruppi_come_la_formula(self):
        # Una differenza semplice tra due gruppi disgiunti (come GIH e GNS): le dimensioni dei gruppi cambiano
        # a ogni ricampionamento, l'intervallo deve comunque coincidere con quello della formula normale.
        n = 100_000
        rng = np.random.default_rng(12)
        g = rng.random(n) < 0.6
        y = np.where(g, rng.random(n) < 0.42, rng.random(n) < 0.37)
        x = np.column_stack([g, g & y, ~g, ~g & y]).astype(np.float32)
        s = st.somme_bootstrap(x, b=st.B_BOOTSTRAP, seed=st.SEED_BOOTSTRAP)
        lo, hi = st.percentili(s[:, 1] / s[:, 0] - s[:, 3] / s[:, 2])
        _, lo_n, hi_n = st.normale_due_proporzioni(int(x[:, 1].sum()), int(x[:, 0].sum()),
                                                    int(x[:, 3].sum()), int(x[:, 2].sum()))
        self.assertLess(abs(lo - lo_n), 0.0005)
        self.assertLess(abs(hi - hi_n), 0.0005)


class TestSoglie(unittest.TestCase):
    def test_soglie_della_decisione_4(self):
        self.assertEqual(st.livello(199), "nascosto")
        self.assertEqual(st.livello(200), "indicativo")
        self.assertEqual(st.livello(999), "indicativo")
        self.assertEqual(st.livello(1000), "pieno")


if __name__ == "__main__":
    unittest.main()
