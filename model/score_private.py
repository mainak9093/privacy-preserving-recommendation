"""Task 3.7: does PRIVATE training match the cleartext oracle's quality?

Scores the B matrices produced by `train.exe` (secret-shared power iteration,
fixed point, real truncation protocol) with the SAME nDCG@20 that
model/metrics.py already applies to the cleartext oracle. Using the same
metric on both is what makes the two numbers comparable by construction rather
than by assertion.

WHAT IS BEING COMPARED, PRECISELY. The private run differs from the oracle in
three ways at once, and it is worth separating them when reading the result:

  1. fixed point instead of float64
  2. a real truncation protocol, whose error is one unit per call
  3. a normaliser that reveals ||v|| (see below)

So a gap between the two is not necessarily "MPC is worse" -- it is the cost of
quantisation plus truncation. If the gap is small, that is the finding: the
protocol's approximations do not cost recommendation quality.

THE LEAK IS CARRIED THROUGH TO HERE. The private runs used
RevealNormNormalizer, which opens one scalar per normalisation. Every row this
script writes records that, so a quality number can never be quoted without the
leakage it was obtained under.

Run:  py -3.13 model/score_private.py
"""
import glob
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data as dataset          # noqa: E402
import metrics                  # noqa: E402
import mf                       # noqa: E402

T = 20
D = 16
RESULTS = os.path.join("bench", "results", "train_quality.jsonl")


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception as e:
        print(f"WARNING: git sha unavailable ({type(e).__name__}: {e})",
              file=sys.stderr)
        return "unknown"


def load_B(path, d, n, t=T):
    """B as float64, from the little-endian int64 the C++ side writes."""
    raw = open(path, "rb").read()
    vals = struct.unpack("<" + "q" * (len(raw) // 8), raw)
    if len(vals) != d * n:
        raise ValueError(f"{path}: {len(vals)} elements, expected {d * n}")
    return np.asarray(vals, dtype=np.float64).reshape(d, n) / float(1 << t)


def main():
    train, test = dataset.load_split("u1")
    n = train.shape[1]

    # The cleartext oracle at the same ell, for the side-by-side.
    print("scoring private training against the cleartext oracle")
    print(f"  {'ell':>4}  {'private nDCG@20':>15}  {'oracle nDCG@20':>15} "
          f" {'gap':>8}  {'subspace angle':>14}")

    sha, host, stamp = git_sha(), socket.gethostname(), \
        time.strftime("%Y-%m-%dT%H:%M:%S")
    rows = []

    for path in sorted(glob.glob(os.path.join("model", "out", "B_private_ell*.bin")),
                       key=lambda p: int(re.search(r"ell(\d+)", p).group(1))):
        ell = int(re.search(r"ell(\d+)", path).group(1))
        Bp = load_B(path, D, n)

        # A = U B^T, then scores = A B. Exactly what serve.hpp computes, so
        # this measures the model the private pipeline would actually serve.
        Ap = train @ Bp.T
        Sp = Ap @ Bp
        ndcg_p = metrics.ndcg_at_k(Sp, test, train, k=20)

        # The oracle at the same ell.
        Ao, Bo = mf.approx_factor(train, d=D, ell=ell, seed=0)
        ndcg_o = metrics.ndcg_at_k(Ao @ Bo, test, train, k=20)

        # How close are the two SUBSPACES? Power iteration fixes neither sign
        # nor the order of near-degenerate components, so comparing the spans
        # is the meaningful comparison -- the same check mf.py makes against
        # SVD.
        try:
            from scipy.linalg import subspace_angles
            ang = float(np.max(subspace_angles(Bp.T, Bo.T)))
        except Exception:
            ang = float("nan")

        print(f"  {ell:>4}  {ndcg_p:>15.4f}  {ndcg_o:>15.4f}  "
              f"{ndcg_p - ndcg_o:>+8.4f}  {ang:>14.3e}")

        rows.append({
            "git_sha": sha, "host": host, "profile": "local",
            "stage": "S2", "phase": "oracle", "op": "private_vs_oracle",
            "dataset": "ml-100k", "d": D, "ell": ell, "t": T, "b": 64,
            "ndcg_at_20_private": float(ndcg_p),
            "ndcg_at_20_oracle": float(ndcg_o),
            "gap": float(ndcg_p - ndcg_o),
            "subspace_angle_rad": ang,
            "normalizer": "reveal-norm (LEAKS ||v||)",
            "wall_ms": 0, "bytes_sent": 0, "timestamp": stamp,
        })

    if not rows:
        print("no B_private_ell*.bin found. Run:")
        print("  for L in 1 2 5 10 25 50; do "
              "./build/train.exe --d 16 --ell $L "
              "--out model/out/B_private_ell$L.bin; done")
        return 1

    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)
    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    print(f"\n  appended {len(rows)} rows to {RESULTS}")

    best = max(rows, key=lambda r: r["ndcg_at_20_private"])
    print(f"\n  best private nDCG@20 = {best['ndcg_at_20_private']:.4f} at "
          f"ell={best['ell']}, against {best['ndcg_at_20_oracle']:.4f} for the "
          f"oracle (gap {best['gap']:+.4f}).")
    print("  Obtained with a normaliser that reveals ||v||; see "
          "docs/threat-model.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
