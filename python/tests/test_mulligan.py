"""Test di metriche/mulligan.py (docs/metriche.md, sezioni 8 e 9; passo 4): caratteristiche delle mani da 7, tipo di
mano, probabilita' ipergeometrica, valore di tenere (Delta = vittoria K - vittoria M) per tipo, "settima carta",
regressione logistica con la politica, il confronto con le varianti, Recruiter e Riddler dal record. Dati sintetici
con il risultato calcolato a mano.

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import math
import sys
import unittest
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import mulligan as mu  # noqa: E402
from metriche import statistica as st  # noqa: E402

# La lista di simulazione di UW (config/decks/uw_blink_sim.json): 19 terre e 4 Witch.
MANO = ["Witch Enchanter", "Recruiter of the Guard", "Witch Enchanter", "Quantum Riddler", "Hallowed Fountain",
        "Sanctifier en-Vec", "Consign to Memory"]


class TestCaratteristiche(unittest.TestCase):
    def setUp(self):
        self.info = mu.InfoCarte.carica()

    def test_mano_uw(self):
        c = mu.caratteristiche_uw(MANO, in_play=True, info=self.info)
        self.assertEqual(c["terre"], 1)
        self.assertEqual(c["witch"], 2)
        self.assertEqual(c["terre_totali"], 3)
        self.assertEqual(c["uno_mana"], 0)
        self.assertEqual(c["due_mana"], 1)          # Sanctifier
        self.assertEqual(c["risposte_dirette"], 1)  # Sanctifier
        self.assertEqual(c["risposte_indirette"], 2)  # Recruiter, Riddler
        self.assertTrue(c["giocata"])               # Sanctifier entro il turno 2
        self.assertFalse(c["coppia"])
        # Carte bianche oltre a Solitude: Witch, Witch, Recruiter, Sanctifier.
        self.assertEqual(c["bianche"], 4)
        self.assertEqual(c["c_recruiter"], 1)
        self.assertEqual(c["c_consign"], 1)
        self.assertEqual(c["c_witch"], 2)
        # In play, 3 terre gia' in mano (Witch compresa): almeno 3 entro il turno 3 di sicuro.
        self.assertAlmostEqual(c["p3"], 1.0)

    def test_probabilita_ipergeometrica(self):
        # 2 terre in mano su 23 nel mazzo: ne restano 21 su 53. In play 2 pescate: P(almeno 1) = 1 - C(32,2)/C(53,2).
        self.assertAlmostEqual(mu.p_terre(2, True, 23), 1 - math.comb(32, 2) / math.comb(53, 2))
        # In draw 3 pescate, 1 terra in mano: P(almeno 2 su 3) con 22 terre su 53.
        attesa = (math.comb(22, 2) * 31 + math.comb(22, 3)) / math.comb(53, 3)
        self.assertAlmostEqual(mu.p_terre(1, False, 23), attesa)
        self.assertAlmostEqual(mu.p_terre(0, True, 23), 0.0)  # servono 3 terre in 2 pescate

    def test_tipo(self):
        c = mu.caratteristiche_uw(MANO, in_play=False, info=self.info)
        self.assertEqual(mu.tipo_uw(c), ("3", True, True, "draw"))
        cinque = mu.caratteristiche_uw(["Plains"] * 5 + ["Solitude", "Ephemerate"], in_play=True, info=self.info)
        self.assertEqual(mu.tipo_uw(cinque)[0], "5+")
        self.assertTrue(cinque["coppia"])
        self.assertTrue(cinque["giocata"])  # Solitude ed Ephemerate insieme valgono come giocata (D-2)

    def test_mano_rakdos(self):
        mano = ["Swamp", "Blood Crypt", "Nethergoyf", "Street Wraith", "Thoughtseize", "Mishra's Bauble",
                "Lightning Bolt"]
        c = mu.caratteristiche_rakdos(mano, in_play=False, info=self.info)
        self.assertEqual(c["terre"], 2)
        self.assertEqual(c["uno_mana"], 1)   # Nethergoyf
        self.assertEqual(c["cercano"], 2)    # Street Wraith, Bauble
        self.assertEqual(mu.tipo_rakdos(c), ("2", True, "2+", "draw"))


# Otto seed: tipo, vittoria K, vittoria M, scelta dell'agente (tieni).
def coppie_sintetiche():
    return pd.DataFrame({
        "seed": range(1, 9),
        "tipo": ["a", "a", "a", "a", "b", "b", "b", "b"],
        "vince_K": [1, 1, 0, 1, 0, 0, 1, 0],
        "vince_M": [0, 1, 0, 0, 1, 0, 0, 1],
        "tiene": [True, True, False, True, False, False, False, True],
        "in_play": [True, False] * 4,
    })


class TestDelta(unittest.TestCase):
    def test_delta_per_tipo(self):
        t = mu.delta_per_gruppo(coppie_sintetiche(), ["tipo"], b=0).set_index("tipo")
        # a: Delta 1, 0, 0, 1 -> 1/2; b: -1, 0, 1, -1 -> -1/4.
        self.assertAlmostEqual(t.loc["a", "delta"], 0.5)
        self.assertAlmostEqual(t.loc["b", "delta"], -0.25)
        self.assertEqual(t.loc["a", "n"], 4)
        self.assertAlmostEqual(t.loc["a", "wr_K"], 0.75)
        self.assertAlmostEqual(t.loc["a", "tiene"], 0.75)  # quota delle mani che l'agente tiene
        _, lo, hi = st.normale_appaiata([1, 0, 0, 1])
        self.assertAlmostEqual(t.loc["a", "delta_norm_lo"], lo)

    def test_esito_di_una_politica(self):
        df = coppie_sintetiche()
        # La politica dell'agente: K dove tiene, M altrove. 1,1,0,1 | 1,0,0,0 -> 4/8.
        self.assertAlmostEqual(mu.esito_politica(df, df["tiene"]).mean(), 4 / 8)
        # Tenere sempre: la media di K.
        self.assertAlmostEqual(mu.esito_politica(df, np.ones(8, bool)).mean(), 4 / 8)
        # Differenza appaiata tra "tieni se il tipo ha Delta > 0" e l'agente.
        segno = df["tipo"].map({"a": True, "b": False}).to_numpy()
        d = mu.esito_politica(df, segno) - mu.esito_politica(df, df["tiene"])
        # a: tiene sempre (1,1,0,1) contro (1,1,0,1): 0. b: M sempre (1,0,0,1) contro (1,0,0,0): +1 nel seed 8.
        self.assertAlmostEqual(d.mean(), 1 / 8)

    def test_settima_carta(self):
        df = coppie_sintetiche()
        df["c_witch"] = [1, 0, 1, 0, 1, 1, 0, 0]
        r = mu.settima_carta(df, ["c_witch"], b=0).set_index("carta").loc["c_witch"]
        # Con Witch (seed 1, 3, 5, 6): Delta 1, 0, -1, 0 -> 0; senza (2, 4, 7, 8): 0, 1, 1, -1 -> 1/4.
        self.assertAlmostEqual(r["delta_con"], 0.0)
        self.assertAlmostEqual(r["delta_senza"], 0.25)
        self.assertAlmostEqual(r["differenza"], -0.25)
        self.assertEqual(r["n_con"], 4)


class TestRegressione(unittest.TestCase):
    def test_ritrova_il_segno_del_valore_di_tenere(self):
        # Partite simulate: tenere vale +0,30 se x = 1, -0,20 se x = 0 (probabilita' di vittoria 0,5 con M).
        rng = np.random.default_rng(1)
        n = 6000
        x = rng.integers(0, 2, n)
        p_k = np.where(x == 1, 0.8, 0.3)
        df = pd.DataFrame({"seed": np.arange(n), "x": x, "vince_K": (rng.random(n) < p_k).astype(int),
                           "vince_M": (rng.random(n) < 0.5).astype(int)})
        m = mu.regressione(df, ["x"])
        d = mu.delta_previsto(m, df, ["x"])
        self.assertAlmostEqual(d[x == 1].mean(), 0.30, delta=0.04)
        self.assertAlmostEqual(d[x == 0].mean(), -0.20, delta=0.04)
        self.assertIn("tieni:x", m.params.index)
        self.assertGreater(m.params["tieni:x"], 0)
        # Errori raggruppati per seed: due righe (K e M) per seed.
        self.assertEqual(m.nobs, 2 * n)

    def test_decili(self):
        prev = np.linspace(-0.5, 0.4, 100)
        oss = (prev > 0).astype(float)
        t = mu.decili(prev, oss, b=0)
        self.assertEqual(len(t), 10)
        self.assertAlmostEqual(t["previsto"].iloc[0], prev[:10].mean())
        self.assertAlmostEqual(t["osservato"].iloc[-1], 1.0)


class TestVarianti(unittest.TestCase):
    def test_guadagno_e_gruppi(self):
        # Tre seed: nel 1 la variante cambia a 7 carte (partita = K), nel 2 a 6 carte (la sua partita), nel 3 niente.
        righe = [
            {"seed": 1, "uw_in_play": True, "agente": {"7": {"tieni": False}}, "K": {"vincitore": "UW"},
             "M": {"vincitore": "Rakdos"},
             "varianti": [{"nome": "m1", "cambia_a": 7, "vincitore": "UW", "mano_tenuta": ["Witch Enchanter", "Plains"],
                           "uguale_a_K_o_M": True}]},
            {"seed": 2, "uw_in_play": False, "agente": {"7": {"tieni": False}}, "K": {"vincitore": "Rakdos"},
             "M": {"vincitore": "Rakdos"},
             "varianti": [{"nome": "m1", "cambia_a": 6, "vincitore": "UW",
                           "mano_tenuta": ["Recruiter of the Guard", "Witch Enchanter"], "uguale_a_K_o_M": False}]},
            {"seed": 3, "uw_in_play": True, "agente": {"7": {"tieni": True}}, "K": {"vincitore": "UW"},
             "M": {"vincitore": "Rakdos"}, "varianti": []},
        ]
        v = mu.varianti(righe, "m1")
        self.assertEqual(len(v), 2)
        self.assertEqual(v.set_index("seed").loc[1, "gruppo"], "solo Witch")
        self.assertEqual(v.set_index("seed").loc[2, "gruppo"], "entrambe")
        # Guadagno per seed: variante - partita dell'agente. Seed 1: K (UW) - M (Rakdos) = +1; seed 2: UW - M = +1.
        self.assertEqual(v["guadagno"].tolist(), [1, 1])
        self.assertEqual(v["cambia_a"].tolist(), [7, 6])


def copia(nome, *eventi):
    return {"c": nome, "e": [list(e) for e in eventi]}


class TestRecordUW(unittest.TestCase):
    def test_recruiter_cerca_solitude(self):
        # UW in draw: turni propri = turno della partita / 2. Recruiter lanciato al turno 6 (suo 3), cerca Solitude
        # (C al 6), Solitude lanciata al turno 8 (suo 4).
        rec = {"seed": 2, "uw_in_play": False, "vincitore": "UW", "uw": {"carte": [
            copia("Recruiter of the Guard", (0, "I"), (6, "L")), copia("Solitude", (6, "C"), (8, "L")),
            copia("Plains", (0, "I"), (2, "T"))]}}
        r = mu.recruiter(rec)
        self.assertEqual(r, {"seed": 2, "turno_lancio": 3, "cercata": "Solitude", "turno_uso": 4, "vince": True,
                             "in_play": False})

    def test_recruiter_che_non_cerca_niente(self):
        rec = {"seed": 1, "uw_in_play": True, "vincitore": "Rakdos", "uw": {"carte": [
            copia("Recruiter of the Guard", (0, "I"), (5, "L"))]}}
        r = mu.recruiter(rec)
        self.assertIsNone(r["cercata"])
        self.assertEqual(r["turno_lancio"], 3)

    def test_riddler_pescate_e_risposte(self):
        # UW in play: turno della partita 9 = suo turno 5. Riddler lanciata al 9: nel turno 9 tre pescate (la
        # pescata del turno e due di Riddler), una e' March (risposta diretta).
        rec = {"seed": 1, "uw_in_play": True, "vincitore": "UW", "uw": {"carte": [
            copia("Quantum Riddler", (0, "I"), (9, "L")), copia("Plains", (9, "P")), copia("March of Otherworldly Light",
            (9, "P")), copia("Ocelot Pride", (9, "P")), copia("Solitude", (7, "P"))]}}
        r = mu.riddler(rec)
        self.assertEqual(r["turno"], 5)
        self.assertEqual(r["pescate_nel_turno"], 3)
        self.assertEqual(r["risposte_nel_turno"], 1)


class TestEtichettaDeiDati(unittest.TestCase):
    """Dal passo 0 della ricerca le tabelle generate dicono con quale generatore sono state calcolate."""

    def test_titolo_con_il_generatore(self):
        self.assertEqual(mu.ETICHETTA_DATI, "euristici v1, generatore unico")
        self.assertEqual(mu.titolo(), "# Mulligan degli euristici v1, generatore unico (passo 4)\n")


if __name__ == "__main__":
    unittest.main()
