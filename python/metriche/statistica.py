"""Intervalli di confidenza delle metriche (docs/metriche.md, sezione 5).

- Proporzioni: intervallo di Wilson.
- Differenze: bootstrap sulle partite (B = 2.000, percentili 2,5 e 97,5) con un generatore a seed fisso, scritto
  nelle uscite. Ogni statistica di questo passo e' una funzione di somme pesate sulle partite (vittorie e partite
  per gruppo), quindi il bootstrap si fa una volta per tutte: per ogni ricampionamento b i pesi w_b (quante volte
  ogni partita e' estratta) e le somme S_b = w_b @ X. Le stesse righe di pesi servono a tutte le carte e a tutti e
  due i giocatori, cosi' le statistiche che ne combinano piu' d'una (IWD relativo, media per mazzo) restano
  coerenti tra loro dentro ogni ricampionamento.
- Formule normali (appaiata e tra due proporzioni) per il controllo del bootstrap.
"""

import math

import numpy as np

Z95 = 1.959963984540054
B_BOOTSTRAP = 2000
SEED_BOOTSTRAP = 20261116
SOGLIA_NASCOSTO = 200     # decisione 4: sotto le 200 partite il valore non si mostra
SOGLIA_INDICATIVO = 1000  # sotto le 1.000 si mostra come "indicativo"


def wilson(k, n, z=Z95):
    """(stima, inferiore, superiore) dell'intervallo di Wilson per k successi su n; NaN se n = 0."""
    if n <= 0:
        return math.nan, math.nan, math.nan
    p = k / n
    z2 = z * z
    den = 1 + z2 / n
    centro = (p + z2 / (2 * n)) / den
    semi = z / den * math.sqrt(p * (1 - p) / n + z2 / (4 * n * n))
    return p, max(0.0, centro - semi), min(1.0, centro + semi)


def normale_appaiata(d, z=Z95):
    """(media, inferiore, superiore) della differenza appaiata d (una per seed), come varianti_uw."""
    d = np.asarray(d, dtype=float)
    m = d.mean()
    se = d.std(ddof=1) / math.sqrt(len(d))
    return m, m - z * se, m + z * se


def normale_due_proporzioni(k1, n1, k2, n2, z=Z95):
    """(differenza, inferiore, superiore) di k1/n1 - k2/n2 con gruppi indipendenti."""
    p1, p2 = k1 / n1, k2 / n2
    se = math.sqrt(p1 * (1 - p1) / n1 + p2 * (1 - p2) / n2)
    return p1 - p2, p1 - p2 - z * se, p1 - p2 + z * se


_cache = {}


def pesi_bootstrap(n, b, seed):
    """Matrice (b, n) di uint8: quante volte ogni partita e' estratta in ognuno dei b ricampionamenti.

    Ricampionamento con reimmissione di n partite su n; generatore PCG64 con il seed dato. Il risultato
    dell'ultima chiamata resta in memoria (stessi n, b e seed: stessa matrice, senza rigenerarla).
    """
    chiave = (n, b, seed)
    if chiave not in _cache:
        rng = np.random.default_rng(seed)
        w = np.empty((b, n), dtype=np.uint8)
        for i in range(b):
            conti = np.bincount(rng.integers(0, n, size=n), minlength=n)
            if conti.max() > 255:
                raise OverflowError("una partita estratta piu' di 255 volte")
            w[i] = conti
        _cache.clear()
        _cache[chiave] = w
    return _cache[chiave]


def somme_bootstrap(x, b=B_BOOTSTRAP, seed=SEED_BOOTSTRAP, blocco=100):
    """Somme pesate (b, K) delle colonne di x (n, K) per ognuno dei b ricampionamenti delle righe.

    Pesi e valori sono interi piccoli e i pesi di un ricampionamento sommano a n: ogni somma parziale vale al
    massimo n * max(x). In float32 gli interi sono esatti fino a 2^24 (16,7 milioni), molto sopra le
    100.000-1.000.000 di partite con indicatori 0/1; si restituiscono in float64.
    """
    x = np.asarray(x, dtype=np.float32)
    w = pesi_bootstrap(x.shape[0], b, seed)
    if x.size and x.shape[0] * float(np.abs(x).max()) >= 2**24:
        raise OverflowError("troppe partite per le somme esatte in float32")
    out = np.empty((b, x.shape[1]), dtype=np.float64)
    for i in range(0, b, blocco):
        out[i:i + blocco] = w[i:i + blocco].astype(np.float32) @ x
    return out


def percentili(valori):
    """Intervallo dei percentili 2,5 e 97,5 di un vettore di stime bootstrap (i NaN si ignorano)."""
    v = np.asarray(valori, dtype=float)
    v = v[~np.isnan(v)]
    if len(v) == 0:
        return math.nan, math.nan
    lo, hi = np.percentile(v, [2.5, 97.5])
    return float(lo), float(hi)


def livello(n):
    """Come si mostra un valore calcolato su n partite (decisione 4)."""
    if n < SOGLIA_NASCOSTO:
        return "nascosto"
    if n < SOGLIA_INDICATIVO:
        return "indicativo"
    return "pieno"
