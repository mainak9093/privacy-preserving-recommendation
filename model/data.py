"""MovieLens loading.

ML-100K is the dev dataset (943 users, 1682 items). u.item is Latin-1, not
UTF-8, and decoding it as UTF-8 raises. The u1.base / u1.test pair is the
canonical 80/20 split shipped with the dataset, so we use it rather than
rolling our own, which makes the numbers comparable to published baselines.
"""
import os
import numpy as np

ML100K = os.path.join("data", "ml-100k")
N_USERS, N_ITEMS = 943, 1682


def load_ratings(path):
    """Return a dense (N_USERS, N_ITEMS) float64 matrix, unrated entries zero.

    PureSVD convention (Cremonesi et al. 2010): raw ratings, unrated as zero,
    no mean centring. This matters for S2, because it is exactly what the
    U^T U power iteration computes, so private training will not need a
    mean-centring MPC protocol.
    """
    R = np.zeros((N_USERS, N_ITEMS), dtype=np.float64)
    with open(path, "r", encoding="latin-1") as fh:
        for line in fh:
            if not line.strip():
                continue
            u, i, r, _ts = line.split("\t")
            R[int(u) - 1, int(i) - 1] = float(r)
    return R


def load_titles(path=None):
    """item_id (0-based) -> title string."""
    path = path or os.path.join(ML100K, "u.item")
    titles = {}
    with open(path, "r", encoding="latin-1") as fh:
        for line in fh:
            if not line.strip():
                continue
            f = line.rstrip("\n").split("|")
            titles[int(f[0]) - 1] = f[1]
    return titles


def load_split(which="u1"):
    base = load_ratings(os.path.join(ML100K, which + ".base"))
    test = load_ratings(os.path.join(ML100K, which + ".test"))
    return base, test
