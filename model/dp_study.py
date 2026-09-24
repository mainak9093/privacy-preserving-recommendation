"""Task 4.6 / D9.2: what differential privacy on the item embeddings costs,
and whether it defends against the section 9.3 reconstruction attack.

THE MECHANISM IS NUDGE'S, NOT ONE WE INVENTED. Nudge section 9 ("Extension:
Scaling up Nudge", the differential-privacy paragraph) specifies it in two
steps, and both versions of the paper carry the identical passage:

    "Dwork et al. show that, to achieve differential privacy, it suffices to
     add gaussian noise to each entry of the matrix U^T U. Following this
     approach, Nudge can provide differential privacy with two extra steps:

       - After collecting ratings, the three Nudge servers normalize each row
         of the matrix U (by dividing the i-th row in U by the number of
         ratings submitted by user i).

       - Each pair of servers picks a matrix in R^{n x n} with i.i.d. gaussian
         entries and splits it across all three servers with replicated secret
         sharing. (This requires no communication, using PRFs.) The servers sum
         these matrices to E. At each step of power iteration, when the servers
         compute Mul(U^T, Mul(U, v)), they add to this Mul(E, v). This incurs
         no communication."

So the noise goes on the GRAM MATRIX U^T U, not on B. Power iteration computes
v <- U^T(U v) = (U^T U) v, so adding E v makes it (U^T U + E) v. That is why it
costs no communication: E is shared once by PRF and every step is a matvec the
protocol already performs. This file reproduces that in cleartext, which is
where quality is measured -- the shared version's cost is already known from
task 4.1 and adding E changes no round count.

-----------------------------------------------------------------------------
WHAT THE PAPER DOES NOT GIVE, AND WHAT WE HAD TO SUPPLY.

It gives no noise scale. So sigma comes from the Dwork et al. result it cites
-- "Analyze Gauss" (Dwork, Talwar, Thakurta, Zhang, STOC 2014) -- under which,
for A = U^T U with every row satisfying ||u_i||_2 <= 1, releasing A + E with E
symmetric and upper-triangle entries i.i.d. N(0, sigma^2),

    sigma = sqrt(2 ln(1.25 / delta)) / epsilon

is (epsilon, delta)-differentially private. The sensitivity is exact: changing
one user's row u changes U^T U by u u^T, whose Frobenius norm is ||u||_2^2 <= 1.

WE DEVIATE FROM THE PAPER ON THE NORMALISATION, DELIBERATELY. Nudge says to
divide row i by the NUMBER of ratings c_i. That does not bound ||u_i||_2 by 1:
with ratings in [1,5] it gives ||u_i||_2 <= 5 sqrt(c_i) / c_i = 5 / sqrt(c_i),
which exceeds 1 whenever c_i < 25. MovieLens-100K guarantees only 20 ratings
per user, so real users fall on the wrong side of that line and the Analyze
Gauss bound would not apply to them. We therefore L2-normalise each row to unit
norm, which makes the sensitivity exactly 1 and the (epsilon, delta) claim
meaningful. Both normalisations are run, and the count-normalised arm is
reported too, so the deviation is visible rather than asserted.

-----------------------------------------------------------------------------
TWO DIFFERENT THREATS, AND CONFLATING THEM WOULD BE THE ONE SERIOUS ERROR HERE.

Nudge's DP protects the TRAINING DATA: it bounds what the published B can
reveal about any one user's rating vector. Section 9.3's attack is a different
question entirely -- an observer watches which items a user fetches and
recovers THAT user's embedding a, using the public B as a known basis.

There is a real reason to expect DP not to help against the second. The
adversary and the user work against the SAME published matrix. If B is noisier,
the user's recommendations are computed from the noisy B and the adversary
attacks with the noisy B; the attack's geometry is unchanged. This study
measures whether that expectation holds rather than assuming it, and reports
the answer either way.

Run:  py -3.13 model/dp_study.py
"""
import json
import math
import os
import socket
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import attack                    # noqa: E402  -- estimator + controls, reused
import data as dataset           # noqa: E402
import metrics                   # noqa: E402
import mf                        # noqa: E402

RESULTS = os.path.join("bench", "results", "dp_study.jsonl")
SEED = 0
TOPK = 20
J_OBSERVED = 10                  # the operating point section 9.3 reports
DELTA = 2.0 ** -40               # Nudge's stated delta

# eps = None is the non-private control: the same pipeline, no noise.
EPS_GRID = [None, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25]


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception as e:
        print(f"WARNING: git sha unavailable ({type(e).__name__}: {e})",
              file=sys.stderr)
        return "unknown"


def sigma_for(eps, delta=DELTA):
    """Analyze Gauss calibration, for unit-L2-norm rows (sensitivity 1)."""
    return math.sqrt(2.0 * math.log(1.25 / delta)) / eps


def normalise_rows(U, how):
    """Bound each user's contribution. See the header on why these differ."""
    U = np.asarray(U, dtype=np.float64)
    if how == "l2":
        nrm = np.linalg.norm(U, axis=1, keepdims=True)
    elif how == "count":
        nrm = (U > 0).sum(axis=1, keepdims=True).astype(np.float64)
    else:
        raise ValueError(how)
    nrm[nrm == 0.0] = 1.0
    return U / nrm


def symmetric_gaussian(n, sigma, rng):
    """E symmetric with upper-triangle entries i.i.d. N(0, sigma^2).

    Analyze Gauss samples the upper triangle (including the diagonal) and
    mirrors it, so the released matrix is symmetric like U^T U. Sampling a
    full i.i.d. matrix and symmetrising would halve the off-diagonal variance
    and silently give a weaker guarantee than the epsilon claims.
    """
    E = np.zeros((n, n), dtype=np.float64)
    iu = np.triu_indices(n)
    E[iu] = rng.normal(0.0, sigma, size=iu[0].size)
    E = E + np.triu(E, 1).T
    return E


def approx_factor_dp(U, E, d=mf.D_DEFAULT, ell=mf.ELL_DEFAULT, seed=SEED):
    """mf.approx_factor with Nudge section 9's noise term folded in.

    Identical to mf.approx_factor except for the single `+ E @ v`, which is
    exactly what the paper adds and exactly where it adds it.
    """
    rng = np.random.default_rng(seed)
    n = U.shape[1]
    B = np.zeros((d, n), dtype=np.float64)
    for i in range(d):
        v = rng.standard_normal(n)
        v = mf.normalize(mf.set_orthogonal(v, B, i))
        for _ in range(ell):
            v = U.T @ (U @ v)
            if E is not None:
                v = v + E @ v            # Mul(<<E>>, v), Nudge section 9
            v = mf.set_orthogonal(v, B, i)
            v = mf.normalize(v)
        B[i] = v
    A = U @ B.T
    return A, B


def run_attack(B, A_true, train, rated, pop_order, rand_vecs, j=J_OBSERVED):
    """The section 9.3 estimator against this B, with the same controls.

    Returns (cos_mean, overlap_mean) for the attack and for the popularity
    control. The control is the load-bearing part: popular items sit near the
    top of nearly everyone's ranking, so an estimator can look strong while
    having learned only what is public.
    """
    m = A_true.shape[0]
    Bn = attack.unit_rows(B.T)
    S = A_true @ B
    observed = attack.top_indices(S, rated, j)
    pop_fetched = np.tile(pop_order[:j], (m, 1))

    out = {}
    for name, a_hat in (("centroid", attack.estimate_centroid(Bn, observed)),
                        ("popularity", attack.estimate_centroid(Bn, pop_fetched)),
                        ("random", rand_vecs)):
        cos = attack.cos_rows(a_hat, A_true)
        seen = rated.copy()
        np.put_along_axis(seen, observed, True, axis=1)
        true_top = attack.top_indices(S, seen, TOPK)
        est_top = attack.top_indices(a_hat @ B, seen, TOPK)
        overlap = np.array([
            len(set(true_top[i]).intersection(est_top[i])) / TOPK
            for i in range(m)])
        out[name] = (float(cos.mean()), float(overlap.mean()))
    return out


def main():
    train, test = dataset.load_split("u1")
    m, n = train.shape
    rng = np.random.default_rng(SEED)

    rated = train > 0
    pop = np.asarray((train > 0).sum(axis=0)).ravel()
    pop_order = np.argsort(-pop)
    rand_vecs = attack.unit_rows(rng.standard_normal((m, mf.D_DEFAULT)))

    counts = (train > 0).sum(axis=1)
    print(f"ML-100K u1: {m} users x {n} items, "
          f"ratings per user min={counts.min()} median={int(np.median(counts))}")
    print(f"Nudge's count-normalisation gives ||u||_2 <= 1 only for users with")
    print(f"  >= 25 ratings; {int((counts < 25).sum())} of {m} users have fewer.")
    print(f"  That is why the L2 arm exists. delta = 2^-40.")
    print()

    sha, host = git_sha(), socket.gethostname()
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    rows = []

    print(f"  {'norm':>6} {'eps':>6} {'sigma':>9}  {'nDCG@20':>8} {'Recall@20':>9}"
          f"  {'attack cos':>10} {'pop ctrl':>9}  {'attack top20':>12}")

    # The project's standard reference point: no normalisation, no noise. The
    # mechanism REQUIRES row normalisation, so that step has its own cost and
    # is separated out rather than folded into the noise's.
    A0, B0 = approx_factor_dp(np.asarray(train, dtype=np.float64), None)
    ndcg0 = metrics.ndcg_at_k(A0 @ B0, test, train, k=TOPK)
    print(f"  {'raw':>6} {'none':>6} {0.0:>9.1f}  {ndcg0:>8.4f} "
          f"{metrics.recall_at_k(A0 @ B0, test, train, k=TOPK):>9.4f}"
          f"  {'--':>10} {'--':>9}  {'--':>11}")

    for how in ("l2", "count"):
        Un = normalise_rows(train, how)
        # Signal against noise, in spectral norm. This is the number that
        # explains the whole result, so it is measured rather than argued.
        signal = float(np.linalg.svd(Un.T @ Un, compute_uv=False)[0])
        for eps in EPS_GRID:
            if eps is None:
                E, sigma = None, 0.0
            else:
                sigma = sigma_for(eps)
                E = symmetric_gaussian(n, sigma, np.random.default_rng(SEED + 17))

            A, B = approx_factor_dp(Un, E)
            S = A @ B
            ndcg = metrics.ndcg_at_k(S, test, train, k=TOPK)
            recall = metrics.recall_at_k(S, test, train, k=TOPK)

            att = run_attack(B, A, train, rated, pop_order, rand_vecs)
            cos_a, ov_a = att["centroid"]
            cos_p, ov_p = att["popularity"]
            cos_r, ov_r = att["random"]

            noise_spec = 0.0 if E is None else 2.0 * sigma * math.sqrt(n)
            snr = float("inf") if noise_spec == 0 else signal / noise_spec
            label = "none" if eps is None else f"{eps:g}"
            print(f"  {how:>6} {label:>6} {sigma:>9.1f}  {ndcg:>8.4f} "
                  f"{recall:>9.4f}  {cos_a:>10.4f} {cos_p:>9.4f}  {ov_a:>11.1%}")

            rows.append({
                "git_sha": sha, "host": host, "profile": "local",
                "m": int(m), "n": int(n), "d": int(mf.D_DEFAULT),
                "ell": int(mf.ELL_DEFAULT), "b": None, "t": None, "k": TOPK,
                "stage": "S2", "phase": "oracle", "op": "dp_gram_noise",
                "dataset": "ml-100k", "normalisation": how,
                "epsilon": eps, "delta": DELTA, "sigma": float(sigma),
                "j_observed": J_OBSERVED,
                "ndcg_at_20": float(ndcg), "recall_at_20": float(recall),
                "cos_attack": cos_a, "cos_popularity": cos_p,
                "cos_random": cos_r,
                "overlap_attack": ov_a, "overlap_popularity": ov_p,
                "overlap_random": ov_r,
                "mechanism": "Nudge section 9 / Analyze Gauss on U^T U",
                "ndcg_at_20_unnormalised": float(ndcg0),
                "signal_spectral": signal,
                "noise_spectral": float(noise_spec),
                "snr_spectral": None if snr == float("inf") else float(snr),
                "wall_ms": 0, "bytes_sent": 0, "timestamp": stamp,
            })

    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)
    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    print(f"\n  appended {len(rows)} rows to {RESULTS}")

    # The two questions this study exists to answer, stated from the data.
    l2 = [r for r in rows if r["normalisation"] == "l2"]
    base = next(r for r in l2 if r["epsilon"] is None)
    at1 = next((r for r in l2 if r["epsilon"] == 1.0), None)
    if at1:
        print(f"\n  UTILITY at eps=1: nDCG@20 {base['ndcg_at_20']:.4f} -> "
              f"{at1['ndcg_at_20']:.4f} "
              f"({100*(at1['ndcg_at_20']-base['ndcg_at_20'])/base['ndcg_at_20']:+.1f}%). "
              f"Nudge report 0.29 -> 0.24 on Netflix, i.e. -17%.")
        print(f"  PRIVACY at eps=1 against the section 9.3 attack: "
              f"cos {base['cos_attack']:.4f} -> {at1['cos_attack']:.4f}, "
              f"against a popularity floor of {at1['cos_popularity']:.4f}.")
        margin_b = base["cos_attack"] - base["cos_popularity"]
        margin_1 = at1["cos_attack"] - at1["cos_popularity"]
        print(f"  The margin over the control is what the attack actually "
              f"claims: {margin_b:+.4f} -> {margin_1:+.4f}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
