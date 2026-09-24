"""Recompute the cleartext oracle's ell sweep and check it against the data.

WHY THIS EXISTS. bench/results/oracle_ell_sweep.jsonl is the data behind
figure F4 (fig_ell) and the "quality is flat in ell" finding the final report
leans on. It was written by a one-off run on 2026-09-06, and until 2026-09-24
no script in the repository could produce it again -- the only data file with
that property. A figure whose data cannot be regenerated is a claim, not a
measurement.

So this recomputes every committed row with the same oracle (model/mf.py) and
compares. It does NOT append: make_figures.py loads this file with no sha
filter (one row per ell), so a second copy of each row would be plotted twice.
Verification is the point, and it exits non-zero on any mismatch so
`mingw32-make reproduce` fails loudly if the oracle has drifted.

    py -3.13 model/ell_sweep.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data as dataset           # noqa: E402
import metrics                   # noqa: E402
import mf                        # noqa: E402

RESULTS = os.path.join("bench", "results", "oracle_ell_sweep.jsonl")

# Tolerances. nDCG and recall are stored to six places, so 5e-6 is "equal as
# written". The subspace angle at ell=400 is ~6.7e-6 rad and is a difference of
# nearly-equal floating-point quantities, so compare it relatively.
TOL_METRIC = 5e-6
TOL_ANGLE_REL = 1e-3


def main():
    with open(RESULTS, "r", encoding="utf-8") as fh:
        committed = [json.loads(line) for line in fh if line.strip()]

    train, test = dataset.load_split("u1")
    failures = 0
    print(f"{'ell':>5} {'nDCG@20':>9} {'committed':>10} {'angle (rad)':>12} "
          f"{'committed':>12}  verdict")
    for row in committed:
        ell, d = row["ell"], row["d"]
        A, B = mf.approx_factor(train, d=d, ell=ell, seed=0)
        S = mf.scores_from(A, B)
        ndcg = metrics.ndcg_at_k(S, test, train, k=20)
        rec = metrics.recall_at_k(S, test, train, k=20)
        ang = mf.svd_crosscheck(train, B)

        ok = (abs(ndcg - row["ndcg_at_20"]) <= TOL_METRIC
              and abs(rec - row["recall_at_20"]) <= TOL_METRIC
              and abs(ang - row["svd_subspace_angle_rad"])
              <= TOL_ANGLE_REL * max(abs(row["svd_subspace_angle_rad"]), 1e-12))
        failures += (not ok)
        print(f"{ell:>5} {ndcg:>9.6f} {row['ndcg_at_20']:>10.6f} "
              f"{ang:>12.4e} {row['svd_subspace_angle_rad']:>12.4e}  "
              f"{'ok' if ok else 'MISMATCH'}")

    if failures:
        print(f"\nell_sweep: {failures} row(s) no longer match the committed "
              f"data. The oracle has changed; F4 and the report's "
              f"ell-flatness claim need re-checking.")
        return 1
    print(f"\nell_sweep: all {len(committed)} committed rows reproduce.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
