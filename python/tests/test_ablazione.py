"""Test di metriche/ablazione.py (docs/metriche.md, sezione 3, aggiunta A; passo 2b): effetto di una carta
come differenza appaiata (lista vera - lista con una copia sostituita da una terra base), interazione di coppia,
Spearman tra ablazione e IWD, scelta delle coppie, carte in disaccordo. Dati sintetici con il risultato calcolato a
mano.

Otto partite, UW in play con il seed dispari. Vincitore nella base e nelle partite con le sostituzioni:

    seed            1   2   3   4   5   6   7   8
    base            U   R   U   U   R   R   U   R
    X (carta UW)    R   R   U   R   R   R   U   U      UW: base - X = 1 0 0 1 0 0 0 -1   -> +1/8
    W (carta UW)    U   U   U   U   R   R   U   R      UW: base - W = 0 -1 0 0 0 0 0 0   -> -1/8
    X + W           R   R   R   R   R   R   U   R      UW: base - XW = 1 0 1 1 0 0 0 0   -> +3/8
    Y (Rakdos)      U   U   U   U   R   R   U   R      Rakdos: base - Y = 0 1 0 0 0 0 0 0 -> +1/8

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import math
import sys
import unittest
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metriche import ablazione as ab  # noqa: E402
from metriche import statistica as st  # noqa: E402


def partite(vincitori):
    nomi = {"U": "UW", "R": "Rakdos", "-": "nessuno"}
    return pd.DataFrame({"seed": range(1, len(vincitori) + 1),
                         "uw_in_play": [s % 2 == 1 for s in range(1, len(vincitori) + 1)],
                         "vincitore": [nomi[v] for v in vincitori]})


BASE = partite("URUURRUR")
X = partite("RRURRRUU")
W = partite("UUUURRUR")
XW = partite("RRRRRRUR")
Y = partite("UUUURRUR")


class TestEffetto(unittest.TestCase):
    def setUp(self):
        self.e = ab.effetti(BASE, {"X": ("uw", X), "Y": ("rakdos", Y)}, b=0).set_index("nome")

    def test_differenza_appaiata(self):
        self.assertAlmostEqual(self.e.loc["X", "effetto"], 1 / 8)
        self.assertAlmostEqual(self.e.loc["Y", "effetto"], 1 / 8)
        self.assertAlmostEqual(self.e.loc["X", "wr_base"], 4 / 8)
        self.assertAlmostEqual(self.e.loc["X", "wr_variante"], 3 / 8)
        self.assertEqual(self.e.loc["X", "n"], 8)
        self.assertEqual(self.e.loc["X", "esiti_diversi"], 3)  # seed 1, 4, 8

    def test_play_e_draw_del_giocatore_che_possiede_la_carta(self):
        # UW in play: seed 1, 3, 5, 7 -> 1 0 0 0. In draw: 0 1 0 -1.
        self.assertAlmostEqual(self.e.loc["X", "effetto_play"], 1 / 4)
        self.assertAlmostEqual(self.e.loc["X", "effetto_draw"], 0.0)
        # Rakdos in play con il seed pari: 1 0 0 0.
        self.assertAlmostEqual(self.e.loc["Y", "effetto_play"], 1 / 4)
        self.assertAlmostEqual(self.e.loc["Y", "effetto_draw"], 0.0)

    def test_intervallo_normale_appaiato(self):
        d = np.array([1, 0, 0, 1, 0, 0, 0, -1])
        m, lo, hi = st.normale_appaiata(d)
        self.assertAlmostEqual(self.e.loc["X", "effetto_norm_lo"], lo)
        self.assertAlmostEqual(self.e.loc["X", "effetto_norm_hi"], hi)
        _, lo_p, _ = st.normale_appaiata(d[0::2])
        self.assertAlmostEqual(self.e.loc["X", "effetto_play_norm_lo"], lo_p)

    def test_patta_non_e_una_vittoria(self):
        e = ab.effetti(BASE, {"P": ("uw", partite("-RUURRUR"))}, b=0).set_index("nome")
        self.assertAlmostEqual(e.loc["P", "effetto"], 1 / 8)

    def test_bootstrap_ripetibile_e_intorno_alla_stima(self):
        e1 = ab.effetti(BASE, {"X": ("uw", X)}, b=200, seed=7)
        e2 = ab.effetti(BASE, {"X": ("uw", X)}, b=200, seed=7)
        pd.testing.assert_frame_equal(e1, e2)
        r = e1.iloc[0]
        self.assertLessEqual(r["effetto_lo"], r["effetto"])
        self.assertGreaterEqual(r["effetto_hi"], r["effetto"])

    def test_seed_diversi_rifiutati(self):
        with self.assertRaises(ValueError):
            ab.effetti(BASE, {"X": ("uw", X.iloc[:7])}, b=0)


class TestInterazione(unittest.TestCase):
    def test_interazione_di_coppia(self):
        # effetto(XW) - effetto(X) - effetto(W) = 3/8 - 1/8 - (-1/8) = 3/8; per seed wX + wW - wXW - wbase.
        i = ab.interazioni(BASE, [("X+W", "uw", X, W, XW)], b=0).set_index("nome")
        self.assertAlmostEqual(i.loc["X+W", "effetto_ab"], 3 / 8)
        self.assertAlmostEqual(i.loc["X+W", "effetto_a"], 1 / 8)
        self.assertAlmostEqual(i.loc["X+W", "effetto_b"], -1 / 8)
        self.assertAlmostEqual(i.loc["X+W", "interazione"], 3 / 8)
        d = np.array([0, 1, 1, 0, 0, 0, 0, 1])
        _, lo, hi = st.normale_appaiata(d)
        self.assertAlmostEqual(i.loc["X+W", "interazione_norm_lo"], lo)
        # In play (seed dispari): 0 1 0 0 -> 1/4.
        self.assertAlmostEqual(i.loc["X+W", "interazione_play"], 1 / 4)


class TestSpearman(unittest.TestCase):
    def test_valori_noti(self):
        self.assertAlmostEqual(ab.spearman([1, 2, 3, 4], [10, 20, 30, 40]), 1.0)
        self.assertAlmostEqual(ab.spearman([1, 2, 3, 4], [4, 3, 2, 1]), -1.0)
        # Con i ranghi medi per i pari merito: ranghi x 1 2,5 2,5 4, y 1 3 2 4 -> 3 / sqrt(10).
        self.assertAlmostEqual(ab.spearman([1, 2, 2, 3], [1, 3, 2, 4]), 3 / math.sqrt(10))

    def test_bootstrap_riga_per_riga(self):
        # Una riga per ricampionamento: Spearman di ogni riga, poi i percentili.
        x = np.array([[1, 2, 3], [1, 2, 3], [3, 2, 1]])
        y = np.array([[1, 2, 3], [3, 2, 1], [3, 2, 1]])
        self.assertEqual(ab.spearman_righe(x, y).tolist(), [1.0, -1.0, 1.0])


class TestScelte(unittest.TestCase):
    def test_coppie_piu_forti_e_piu_negative_al_turno_5(self):
        cp = pd.DataFrame({
            "giocatore": ["uw", "uw", "uw", "rakdos", "uw", "rakdos", "uw", "uw"],
            "a": ["A", "B", "Ephemerate", "Z", "C", "Q", "D", "Recruiter of the Guard"],
            "b": ["B", "C", "Solitude", "Q", "D", "W", "A", "Solitude"],
            "sinergia_t5": [0.10, 0.02, 0.11, 0.12, -0.09, -0.03, -0.01, 0.0],
            "variante": ["", "", "", "", "", "", "", "ipc"],
        })
        scelte = ab.scegli_coppie(cp, n=2, fisse=[("uw", "Recruiter of the Guard", "Solitude"),
                                                  ("uw", "Solitude", "Ephemerate")])
        chiavi = [(s["giocatore"], frozenset((s["a"], s["b"]))) for s in scelte]
        # Le due piu' forti (Z+Q, Ephemerate+Solitude) e le due piu' negative (C+D, Q+W), dai due mazzi insieme;
        # poi le fisse. Solitude + Ephemerate e' gia' scelta (scritta al contrario): non si ripete, e il motivo
        # dice tutte e due le cose. Le varianti (riga "ipc") non si scelgono.
        self.assertEqual(chiavi[:4], [("rakdos", frozenset("ZQ")), ("uw", frozenset(("Ephemerate", "Solitude"))),
                                      ("uw", frozenset("CD")), ("rakdos", frozenset("QW"))])
        self.assertEqual(len(scelte), 5)
        self.assertEqual(chiavi[4], ("uw", frozenset(("Recruiter of the Guard", "Solitude"))))
        self.assertEqual([s["motivo"] for s in scelte],
                         ["più forte", "più forte, fissa", "più negativa", "più negativa", "fissa"])

    def test_disaccordi(self):
        # Sei carte: soglia del rango round(6 / 3) = 2. Ranghi (dal piu' alto) ablazione A..F = 1..6, tutte
        # positive; IWD: A 1, B 2, C 3, F 4, E 5, D 6. Segno diverso: D ed E (IWD negativo). Rango: F (6 contro 4)
        # e D (4 contro 6).
        df = pd.DataFrame({"giocatore": ["uw"] * 6, "carta": list("ABCDEF"),
                           "ablazione": [0.06, 0.05, 0.04, 0.03, 0.02, 0.01],
                           "iwd_t5": [0.10, 0.08, 0.06, -0.05, -0.02, 0.01]})
        d = ab.disaccordi(df).set_index("carta")
        self.assertEqual(sorted(d.index), ["D", "E", "F"])
        self.assertTrue(d.loc["E", "segno_diverso"])
        self.assertFalse(d.loc["F", "segno_diverso"])
        self.assertEqual(d.loc["F", "scarto_rango"], 2)
        self.assertTrue(d.loc["D", "segno_diverso"])
        self.assertEqual(d.loc["D", "scarto_rango"], 2)
        self.assertEqual(ab.soglia_rango(16), 5)

    def test_segno_relativo(self):
        # Con l'IWD T5 relativo: l'ablazione meno la media del mazzo (0,035) contro l'IWD relativo.
        df = pd.DataFrame({"giocatore": ["uw"] * 6, "carta": list("ABCDEF"),
                           "ablazione": [0.06, 0.05, 0.04, 0.03, 0.02, 0.01],
                           "iwd_t5": [0.10, 0.08, 0.06, -0.05, -0.02, 0.01],
                           "iwd_t5_rel": [0.05, 0.03, 0.01, -0.10, -0.07, -0.04]})
        d = ab.disaccordi(df).set_index("carta")
        # D: 0,03 - 0,035 < 0 e relativo < 0 -> concordano; E ed F lo stesso. C: +0,005 e +0,01: concordano.
        self.assertFalse(d["segno_relativo_diverso"].any())
        df.loc[df["carta"] == "D", "iwd_t5_rel"] = 0.02
        d = ab.disaccordi(df).set_index("carta")
        self.assertTrue(d.loc["D", "segno_relativo_diverso"])
        self.assertEqual(ab.soglia_rango(12), 4)

    def test_nome_della_carta_sulla_riga_di_comando(self):
        # Sulla riga di comando di Windows le lettere non ASCII arrivano storpiate: si passa il prefisso ASCII.
        self.assertEqual(ab.argomento_carta("Solitude"), "Solitude")
        self.assertEqual(ab.argomento_carta("Troll of Khazad-dûm"), "Troll of Khazad-d")
        self.assertEqual(ab.cartella("Troll of Khazad-dûm"), "troll_of_khazad_dum")
        self.assertEqual(ab.cartella("Phelia, Exuberant Shepherd"), "phelia_exuberant_shepherd")


class TestAnalizza(unittest.TestCase):
    """analizza() unisce le esecuzioni scritte su disco con le metriche per carta: effetti, coppie, Spearman,
    controllo della terra al posto di se stessa. Le partite di test_metriche_carte (A e B di UW, Z di Rakdos)."""

    def test_su_disco(self):
        import json
        import tempfile

        from metriche import metriche_carte as mc
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import test_metriche_carte as tmc

        base = tmc.PARTITE[["seed", "uw_in_play", "vincitore"]]
        res = mc.calcola(tmc.PARTITE, tmc.CARTE, tmc.NOMI, tmc.COPPIE, turni=(3, 5, 7), b=40, seed=3)
        desc = pd.DataFrame({"giocatore": ["uw", "uw", "rakdos"], "carta": ["A", "B", "Z"],
                             "copie_mano_fine": [1, 0, 0], "copie_viste": [4, 5, 3]})
        vince = {"base": "URURURRU", "A": "RRURURRU", "B": "UUURURRU", "Z": "UUURURRU", "A+B": "RRRRURRU"}
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            esec = []
            for nome, v in vince.items():
                (d / nome).mkdir()
                partite(v).to_json(d / nome / "partite.jsonl", orient="records", lines=True)
            for nome, chi, carte, tipo in (("A", "uw", ["A"], "carta"), ("B", "uw", ["B"], "carta"),
                                           ("Z", "rakdos", ["Z"], "carta"), ("A+B", "uw", ["A", "B"], "coppia"),
                                           ("base", "", ["Plains", "Swamp"], "controllo")):
                esec.append({"nome": nome, "tipo": tipo, "giocatore": chi, "carte": carte, "motivo": "fissa",
                             "sostituzioni": [], "md5": "x" if tipo == "controllo" else "y", "partite": 8,
                             "secondi": 1.0})
            (d / "esecuzioni.json").write_text(json.dumps(esec), encoding="utf-8")
            a = ab.analizza(d, base, res, tmc.NOMI, desc, md5_base="x", b=40, seed=3)
        e = a["carte"].set_index("carta")
        # Base UW 1 0 1 0 1 0 0 1. A: 0 0 1 0 1 0 0 1 -> +1/8. B: 1 1 1 0 1 0 0 1 -> -1/8.
        self.assertAlmostEqual(e.loc["A", "effetto"], 1 / 8)
        self.assertAlmostEqual(e.loc["B", "effetto"], -1 / 8)
        # Rakdos: base 0 1 0 1 0 1 1 0, Z 0 0 0 1 0 1 1 0 -> +1/8.
        self.assertAlmostEqual(e.loc["Z", "effetto"], 1 / 8)
        self.assertAlmostEqual(e.loc["A", "iwd"], res.carte.set_index("carta").loc["A", "iwd"])
        self.assertAlmostEqual(e.loc["A", "mano_fine"], 0.25)
        # A+B: 0 0 0 0 1 0 0 1 -> effetto 2/8; interazione 2/8 - 1/8 + 1/8 = 2/8.
        c = a["coppie"].iloc[0]
        self.assertAlmostEqual(c["effetto_ab"], 2 / 8)
        self.assertAlmostEqual(c["interazione"], 2 / 8)
        # Con 8 partite il bootstrap non somiglia alla formula normale: quel controllo vale sui dati veri.
        self.assertTrue(all(r.startswith("OK") for r in a["controlli"] if "bootstrap" not in r), a["controlli"])
        self.assertTrue(any(r.startswith("OK") and "controllo" in r for r in a["controlli"]))
        cf = a["confronto"].set_index(["metrica", "gruppo"])
        self.assertAlmostEqual(cf.loc[("iwd", "Tutte"), "rho"],
                               ab.spearman(e["effetto"], e["iwd"].astype(float)))
        self.assertIn("rho_lo", cf.columns)
        # Il documento si scrive.
        testo = mc.sezione_ablazione(a, {"uw": {"A": 4, "B": 4}, "rakdos": {"Z": 4}}, res.bias)
        self.assertIn("## Ablazione", testo)


if __name__ == "__main__":
    unittest.main()
