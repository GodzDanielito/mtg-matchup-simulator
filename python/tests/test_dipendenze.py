"""Le dipendenze Python delle metriche sono fissate una volta sola (PEP 723) e i test girano con quelle.

Gli script delle metriche dichiarano le dipendenze nei metadati in testa al file (letti da `uv run`); anche
python/esegui_test.py, che lancia i test. Qui si controlla che i blocchi coincidano, che ogni versione sia fissata
con `==` e che le versioni installate nell'ambiente dei test siano quelle.

Esecuzione:  uv run --no-project python/esegui_test.py
"""

import re
import sys
import tomllib
import unittest
from importlib import metadata
from pathlib import Path

PYTHON = Path(__file__).resolve().parents[1]
SCRIPT = [PYTHON / "esegui_test.py", PYTHON / "metriche" / "converti.py", PYTHON / "metriche" / "metriche_carte.py",
          PYTHON / "metriche" / "ablazione.py"]
# L'espressione di riferimento della specifica PEP 723.
BLOCCO = re.compile(r"(?m)^# /// script$\s(?P<corpo>(^#(| .*)$\s)+)^# ///$")


def metadati(percorso):
    testo = percorso.read_text(encoding="utf-8")
    m = BLOCCO.search(testo)
    if m is None:
        return None
    corpo = "".join(riga[2:] if riga.startswith("# ") else riga[1:] for riga in m.group("corpo").splitlines(True))
    return tomllib.loads(corpo)


class TestDipendenze(unittest.TestCase):
    def test_blocchi_uguali_e_fissati(self):
        blocchi = {p.name: metadati(p) for p in SCRIPT}
        for nome, b in blocchi.items():
            self.assertIsNotNone(b, f"{nome} senza metadati PEP 723")
            for dip in b["dependencies"]:
                self.assertRegex(dip, r"^[A-Za-z0-9_.-]+==[0-9][0-9A-Za-z.]*$", f"{nome}: {dip} non fissata")
        primo = blocchi[SCRIPT[0].name]
        for nome, b in blocchi.items():
            self.assertEqual(b, primo, f"{nome} ha dipendenze diverse da esegui_test.py")

    def test_versioni_installate(self):
        b = metadati(SCRIPT[0])
        for dip in b["dependencies"]:
            nome, versione = dip.split("==")
            self.assertEqual(metadata.version(nome), versione, f"{nome}: i test girano con un'altra versione")


if __name__ == "__main__":
    sys.exit(unittest.main())
