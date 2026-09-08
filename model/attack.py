"""The reconstruction attack: what an observer learns from watching fetches.

This is design/ARCHITECTURE-draft-v1.md section 9.3, "the analysis that only we
can do", carried out. It is the argument for why the delivery layer has to be
private at all, and neither reference paper measures it, because neither
implements both halves of the system.

  THE SETTING. NUDGE publishes the item embedding matrix B in the clear -- that
  is not a leak, it is the design (mf.py's docstring: each converged row of B is
  opened before the next is computed, which is what keeps SetOrthogonal cheap).
  So B is a complete latent-factor model of the catalogue, known to everyone.

  THE QUESTION. A user fetches j recommended records. WITHOUT PIR, an observer
  learns which j items those were. Since the recommendations are the top of
  score = a . B, and B is public, each observed fetch is not one bit -- it is a
  constraint on a, a projection onto a known basis. How fast does a collapse?

  WHAT IS MEASURED. cos(a_hat, a) against j, and the top-20 overlap between the
  ranking the adversary would produce from a_hat and the user's true ranking.
  The second is the one a non-cryptographer reads immediately: it is the share
  of the user's FUTURE recommendations the adversary can predict.

  ---------------------------------------------------------------------------
  THE CONTROL IS THE LOAD-BEARING PART OF THIS EXPERIMENT.

  A random-vector control is too weak and would flatter the attack. Popular
  items sit near the top of nearly everyone's ranking, so an estimator can look
  impressive while having learned nothing about THIS user -- it has only learned
  what is popular, which is public.

  So the honest control is POPULARITY: run the identical estimator on the j
  globally most-popular items, ignoring the user entirely. The attack's claim is
  only whatever it achieves ABOVE that line. The random control is kept as well,
  to show where the floor actually is.

  ON READING THE RANDOM CONTROL. Its MEAN cosine is ~0.003, i.e. zero, as it
  should be for a random direction. What is not negligible is its SPREAD: the
  IQR is about [-0.17, +0.19], a half-width near 1/sqrt(d) = 0.25 at d = 16.

  The consequence matters for how the result is stated. A cosine of 0.2 for one
  individual user is worth nothing -- it is inside the noise of guessing. Only
  the population mean, and the gap above the popularity control, carry a claim.
  An earlier version of this comment asserted the random control's mean would
  sit near 0.25; that was wrong, and the measurement is what corrected it.

Outputs (append-only, per RULES B5):
    bench/results/leakage_attack.jsonl

Run:  py -3.13 model/attack.py
"""
import json
import os
import socket
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data as dataset          # noqa: E402
import mf                       # noqa: E402

RESULTS = os.path.join("bench", "results", "leakage_attack.jsonl")

# The j grid: how many fetches the observer has seen.
J_GRID = [1, 2, 3, 5, 8, 10, 15, 20, 30, 50]

TOPK = 20           # for the overlap metric
SEED = 0            # same seed as export.py, so this attacks the demo's model
MARGIN_STEPS = 200
MARGIN_NEG = 64     # negatives sampled per user per step
MARGIN_LR = 0.5


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return "unknown"


def unit_rows(X, eps=1e-12):
    """Row-normalise, leaving zero rows alone."""
    nrm = np.linalg.norm(X, axis=-1, keepdims=True)
    return np.where(nrm > eps, X / np.maximum(nrm, eps), X)


def cos_rows(X, Y, eps=1e-12):
    """Row-wise cosine between two (m, d) stacks."""
    num = np.sum(X * Y, axis=1)
    den = np.linalg.norm(X, axis=1) * np.linalg.norm(Y, axis=1)
    return num / np.maximum(den, eps)


def top_indices(scores, exclude_mask, k):
    """Top-k column indices per row, with excluded entries pushed to the bottom.

    exclude_mask is a boolean (m, n): True means "not a candidate".
    """
    s = np.where(exclude_mask, -np.inf, scores)
    # argpartition then sort the survivors, which is what makes this tractable
    # at 943 x 1682 across ten values of j.
    idx = np.argpartition(-s, kth=k - 1, axis=1)[:, :k]
    rows = np.arange(s.shape[0])[:, None]
    order = np.argsort(-s[rows, idx], axis=1)
    return idx[rows, order]


def estimate_centroid(Bn, fetched):
    """Estimator A: centroid of the fetched items' (unit) embeddings.

    Bn      (n, d) unit-normalised item vectors
    fetched (m, j) item indices the observer saw
    """
    return unit_rows(Bn[fetched].sum(axis=1))


def estimate_margin(Bn, fetched, rng, n_items, steps=MARGIN_STEPS):
    """Estimator B: the fetched set is the TOP of the score vector, so every
    fetched item outranks every unfetched one. Turn that into a margin loss and
    take gradient steps, re-normalising each time.

    This uses strictly more of what the observer knows than the centroid does:
    the centroid only uses "these were fetched", while this also uses "and
    everything else was not".
    """
    m = fetched.shape[0]
    a = estimate_centroid(Bn, fetched)          # warm start, not from noise
    pos = Bn[fetched].mean(axis=1)              # (m, d), fixed across steps
    for _ in range(steps):
        neg_idx = rng.integers(0, n_items, size=(m, MARGIN_NEG))
        negs = Bn[neg_idx]                                   # (m, neg, d)
        # hinge: penalise negatives that outscore the positive centroid
        s_pos = np.einsum("md,md->m", a, pos)[:, None]       # (m, 1)
        s_neg = np.einsum("md,mkd->mk", a, negs)             # (m, neg)
        active = (s_neg + 0.05) > s_pos                      # (m, neg)
        grad = pos - (negs * active[:, :, None]).sum(axis=1) / \
            np.maximum(active.sum(axis=1, keepdims=True), 1)
        a = unit_rows(a + MARGIN_LR * grad)
    return a


def main():
    t0 = time.time()
    train, _test = dataset.load_split("u1")
    print(f"factorising: d={mf.D_DEFAULT} ell={mf.ELL_DEFAULT} seed={SEED}")
    A, B = mf.approx_factor(train, d=mf.D_DEFAULT, ell=mf.ELL_DEFAULT, seed=SEED)
    m, d = A.shape
    n = B.shape[1]

    S = A @ B                       # true scores, the thing the user ranks by
    rated = train > 0               # masked out of recommendations
    Bn = unit_rows(B.T)             # (n, d) unit item vectors

    # Popularity, for the control: how often an item was rated in training.
    # This is public information -- an observer knows it without watching anyone.
    pop = np.asarray((train > 0).sum(axis=0)).ravel()
    pop_order = np.argsort(-pop)

    rng = np.random.default_rng(SEED)
    rand_vecs = unit_rows(rng.standard_normal((m, d)))

    # The largest j we will ever need to observe, taken once.
    jmax = max(J_GRID)
    observed_all = top_indices(S, rated, jmax)      # (m, jmax)

    sha, host = git_sha(), socket.gethostname()
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)

    print(f"users={m} items={n} d={d}   attacking {len(J_GRID)} values of j")
    print()
    print(f"  {'j':>3}  {'estimator':<12} {'cos mean':>9} {'cos IQR':>15} "
          f"{'top20 overlap':>14}")

    rows_out = []
    for j in J_GRID:
        fetched = observed_all[:, :j]

        # The popularity control fetches the SAME NUMBER of items, but the
        # globally most popular ones, identically for every user.
        pop_fetched = np.tile(pop_order[:j], (m, 1))

        estimators = {
            "centroid":   estimate_centroid(Bn, fetched),
            "margin":     estimate_margin(Bn, fetched, rng, n),
            "popularity": estimate_centroid(Bn, pop_fetched),
            "random":     rand_vecs,
        }

        for name, a_hat in estimators.items():
            cos = cos_rows(a_hat, A)
            q1, q2, q3 = np.percentile(cos, [25, 50, 75])

            # Overlap on the items the observer has NOT already seen, so this
            # measures prediction of FUTURE recommendations rather than
            # regurgitation of the j fetches it was handed.
            seen = rated.copy()
            np.put_along_axis(seen, fetched, True, axis=1)
            true_top = top_indices(S, seen, TOPK)
            est_top = top_indices(a_hat @ B, seen, TOPK)
            overlap = np.array([
                len(set(true_top[i]).intersection(est_top[i])) / TOPK
                for i in range(m)])

            print(f"  {j:>3}  {name:<12} {cos.mean():>9.4f} "
                  f"{f'[{q1:.3f}, {q3:.3f}]':>15} {overlap.mean():>13.1%}")

            rows_out.append({
                "git_sha": sha, "host": host, "profile": "local",
                "m": int(m), "n": int(n), "d": int(d),
                "ell": int(mf.ELL_DEFAULT), "b": None, "t": None,
                "k": TOPK, "stage": "S1", "phase": "leakage",
                "op": name, "j": int(j), "seed": SEED,
                "cos_mean": float(cos.mean()), "cos_median": float(q2),
                "cos_q1": float(q1), "cos_q3": float(q3),
                "overlap_mean": float(overlap.mean()),
                "overlap_median": float(np.median(overlap)),
                "timestamp": stamp,
            })
        print()

    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows_out:
            fh.write(json.dumps(r) + "\n")

    print(f"appended {len(rows_out)} rows to {RESULTS}  "
          f"({time.time() - t0:.1f}s)")

    # The headline, printed so a run states its own conclusion rather than
    # leaving it to be read off a figure.
    best = [r for r in rows_out if r["op"] == "centroid"]
    ctrl = {r["j"]: r for r in rows_out if r["op"] == "popularity"}
    print()
    print("Attack minus popularity control (cos), which is the part that is")
    print("actually about the user rather than about what is popular:")
    for r in best:
        gap = r["cos_mean"] - ctrl[r["j"]]["cos_mean"]
        print(f"  j={r['j']:>3}: {r['cos_mean']:.4f} - "
              f"{ctrl[r['j']]['cos_mean']:.4f} = {gap:+.4f}")


if __name__ == "__main__":
    main()
