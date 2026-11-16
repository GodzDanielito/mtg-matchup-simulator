# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "numpy==2.5.5",
#   "pandas==3.1.1",
#   "pyarrow==26.0.1",
#   "statsmodels==0.15.0",
# ]
# ///
"""Esegue tutti i test Python (unittest) con le dipendenze fissate delle metriche.

Uso:
    uv run --no-project python/esegui_test.py        # come in CI
"""

import sys
import unittest
from pathlib import Path

if __name__ == "__main__":
    cartella = Path(__file__).resolve().parent / "tests"
    suite = unittest.defaultTestLoader.discover(str(cartella), top_level_dir=str(cartella))
    esito = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if esito.wasSuccessful() else 1)
