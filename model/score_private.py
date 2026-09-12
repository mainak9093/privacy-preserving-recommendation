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
  3. the normaliser -- which one is now RECORDED PER FILE, not assumed

THE CONFIGURATION IS READ FROM THE FILENAME, NOT HARDCODED. Until 2026-09-13
this script asserted `b=64` and `reveal-norm` on every row it wrote, because
train.exe could only produce that one configuration. It can now also produce
the spec-faithful FSS normaliser at b=128, whose COST was measured in task 4.1
but whose QUALITY had never been measured at all -- there was no B to score.
A hardcoded label would have quietly mislabelled those rows as leaking.

  new scheme:  B_private_{reveal,fss}_b{64,128}_ell{N}.bin
  legacy:      B_private_ell{N}.bin          -> (reveal, b=64)

The legacy pattern is still honoured so the six already-committed rows stay
reproducible.

WHAT ELSE THIS WRITES, and why it is here rather than in train.exe.
`train.exe` exports only B; the user matrix is A = U B^T, which needs the
ratings and is what serve.hpp computes anyway. So for each model this also
writes the two files `demo --a/--b/--expect` needs to serve that model:

    A_private_<tag>.bin            m*d int64 LE, t-scaled, row-major
    top10_u42_private_<tag>.txt    the expected ranking for the demo user

Without those, the privately trained model could be scored here but never
actually SERVED, which is the composition the whole project claims.

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
D_DEFAULT = 16
DEMO_USER = 42
OUT = os.path.join("model", "out")
RESULTS = os.path.join("bench", "results", "train_quality.jsonl")

NORM_LABEL = {
    "reveal": "reveal-norm (LEAKS ||v||)",
    "fss": "fss (reveals nothing)",
}


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception as e:
        print(f"WARNING: git sha unavailable ({type(e).__name__}: {e})",
              file=sys.stderr)
        return "unknown"


def encode(v, t=T):
    """float -> ring element. Mirrors Encode<u64>; see model/export.py.

    C++ std::round rounds half AWAY FROM ZERO; numpy.rint rounds half to even.
    They disagree on every exact half-quantum, so this matches C++, which is
    what ships.
    """
    x = float(np.asarray(v, dtype=np.float64)) * float(1 << t)
    scaled = int(np.trunc(x + np.copysign(0.5, x)))
    if not (-(1 << 63) <= scaled < (1 << 63)):
        raise OverflowError(
            f"encode({v}) = {scaled} does not fit int64 at t={t}.")
    return scaled


def pack_i64(values):
    return b"".join(struct.pack("<q", int(v)) for v in values)


def parse_model(path):
    """(normalizer, ring_bits, ell, tag) from the filename.

    The filename is the only thing that travels with the .bin, so it is the
    only honest place to read the configuration from.
    """
    base = os.path.basename(path)
    m = re.match(r"B_private_(reveal|fss)_b(\d+)_ell(\d+)\.bin$", base)
    if m:
        return m.group(1), int(m.group(2)), int(m.group(3)), \
            f"{m.group(1)}_b{m.group(2)}_ell{m.group(3)}"
    m = re.match(r"B_private_ell(\d+)\.bin$", base)
    if m:
        # The pre-2026-09-13 scheme, which could only ever be this one config.
        return "reveal", 64, int(m.group(1)), f"ell{m.group(1)}"
    return None


def load_B(path, n, t=T):
    """B as float64, and its raw t-scaled ints, from little-endian int64.

    d is INFERRED from the file length rather than assumed to be 16, so a
    model trained at another d is scored rather than rejected.
    """
    raw = open(path, "rb").read()
    vals = struct.unpack("<" + "q" * (len(raw) // 8), raw)
    if not vals or len(vals) % n != 0:
        raise ValueError(f"{path}: {len(vals)} elements, not a multiple of "
                         f"n = {n}")
    d = len(vals) // n
    B_int = np.asarray(vals, dtype=object).reshape(d, n)
    return np.asarray(vals, dtype=np.float64).reshape(d, n) / float(1 << t), \
        B_int, d


def write_serving_files(tag, train, B_float, B_int, d, n, titles):
    """A_private_<tag>.bin and top10_u42_private_<tag>.txt.

    Mirrors model/export.py's contract exactly: A is t-scaled row-major int64
    LE, and the ranking is taken on the ENCODED scores with items the user
    already rated masked out -- because that is what the system ranks.
    """
    A = train @ B_float.T                       # m x d, scale t
    a_path = os.path.join(OUT, f"A_private_{tag}.bin")
    enc_A = [encode(x) for x in np.asarray(A, dtype=np.float64).reshape(-1)]
    with open(a_path, "wb") as fh:
        fh.write(pack_i64(enc_A))

    # Scores for the demo user, at 2t, from the encoded factors -- the same
    # arithmetic demo.cpp performs on shares.
    a_enc = [encode(x) for x in A[DEMO_USER]]
    enc_scores = [sum(int(a_enc[kk]) * int(B_int[kk][j]) for kk in range(d))
                  for j in range(n)]
    for v in enc_scores:
        if not (-(1 << 63) <= v < (1 << 63)):
            raise OverflowError(
                f"score {v} does not fit int64 at 2t={2 * T}; b=64 too narrow")

    ranked = sorted((j for j in range(n) if train[DEMO_USER][j] == 0),
                    key=lambda j: (-enc_scores[j], j))
    top = ranked[:10]
    t_path = os.path.join(OUT, f"top10_u42_private_{tag}.txt")
    with open(t_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# rank\titem\tencoded_score_2t\ttitle\n")
        fh.write(f"# private model, tag {tag}\n")
        for rank, j in enumerate(top):
            fh.write(f"{rank}\t{j}\t{enc_scores[j]}\t"
                     f"{titles.get(int(j), '?')}\n")
    return a_path, t_path, len(enc_A)


def main():
    train, test = dataset.load_split("u1")
    titles = dataset.load_titles()
    n = train.shape[1]

    models = []
    for path in glob.glob(os.path.join(OUT, "B_private*.bin")):
        parsed = parse_model(path)
        if parsed is None:
            print(f"  ignoring {os.path.basename(path)}: unrecognised name",
                  file=sys.stderr)
            continue
        models.append((parsed, path))
    models.sort(key=lambda x: (x[0][0], x[0][1], x[0][2]))

    if not models:
        print("no B_private*.bin found. Run:")
        print("  for L in 1 2 5 10 25 50; do ./build/train.exe --d 16 "
              "--ell $L --out model/out/B_private_ell$L.bin; done")
        print("  ./build/train.exe --b 128 --normalizer fss --ell 10 \\")
        print("      --out model/out/B_private_fss_b128_ell10.bin")
        return 1

    print("scoring private training against the cleartext oracle")
    print(f"  {'norm':>7} {'b':>4} {'ell':>4}  {'private nDCG@20':>15}  "
          f"{'oracle nDCG@20':>15}  {'gap':>8}  {'subspace angle':>14}")

    sha, host, stamp = git_sha(), socket.gethostname(), \
        time.strftime("%Y-%m-%dT%H:%M:%S")
    rows = []

    # The oracle depends only on ell, and mf.approx_factor is not cheap, so
    # cache it across the several normaliser/ring arms that share an ell.
    oracle_cache = {}

    for (norm, ring_bits, ell, tag), path in models:
        B_float, B_int, d = load_B(path, n)
        Ap = train @ B_float.T
        ndcg_p = metrics.ndcg_at_k(Ap @ B_float, test, train, k=20)
        recall_p = metrics.recall_at_k(Ap @ B_float, test, train, k=20)

        if (ell, d) not in oracle_cache:
            Ao, Bo = mf.approx_factor(train, d=d, ell=ell, seed=0)
            oracle_cache[(ell, d)] = (
                Ao, Bo,
                metrics.ndcg_at_k(Ao @ Bo, test, train, k=20),
                metrics.recall_at_k(Ao @ Bo, test, train, k=20))
        Ao, Bo, ndcg_o, recall_o = oracle_cache[(ell, d)]

        # How close are the two SUBSPACES? Power iteration fixes neither sign
        # nor the order of near-degenerate components, so comparing the spans
        # is the meaningful comparison -- the same check mf.py makes against
        # SVD.
        try:
            from scipy.linalg import subspace_angles
            ang = float(np.max(subspace_angles(B_float.T, Bo.T)))
        except Exception:
            ang = float("nan")

        a_path, t_path, na = write_serving_files(tag, train, B_float, B_int,
                                                 d, n, titles)

        print(f"  {norm:>7} {ring_bits:>4} {ell:>4}  {ndcg_p:>15.4f}  "
              f"{ndcg_o:>15.4f}  {ndcg_p - ndcg_o:>+8.4f}  {ang:>14.3e}")

        rows.append({
            "git_sha": sha, "host": host, "profile": "local",
            "stage": "S2", "phase": "oracle", "op": "private_vs_oracle",
            "dataset": "ml-100k", "d": d, "ell": ell, "t": T, "b": ring_bits,
            "ndcg_at_20_private": float(ndcg_p),
            "ndcg_at_20_oracle": float(ndcg_o),
            "recall_at_20_private": float(recall_p),
            "recall_at_20_oracle": float(recall_o),
            "gap": float(ndcg_p - ndcg_o),
            "subspace_angle_rad": ang,
            "normalizer": NORM_LABEL[norm],
            "model_tag": tag,
            "model_path": path.replace("\\", "/"),
            "wall_ms": 0, "bytes_sent": 0, "timestamp": stamp,
        })
        print(f"          -> {os.path.basename(a_path)} ({na} elems), "
              f"{os.path.basename(t_path)}")

    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)
    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    print(f"\n  appended {len(rows)} rows to {RESULTS}")

    # The comparison the project could not make until the FSS models existed.
    leaky = [r for r in rows if r["normalizer"].startswith("reveal")]
    noleak = [r for r in rows if r["normalizer"].startswith("fss")]
    if leaky and noleak:
        for r in noleak:
            peer = [x for x in leaky if x["ell"] == r["ell"]]
            if not peer:
                continue
            dd = r["ndcg_at_20_private"] - peer[0]["ndcg_at_20_private"]
            print(f"\n  NO-LEAK vs LEAKY at ell={r['ell']}: "
                  f"{r['ndcg_at_20_private']:.4f} (fss, b={r['b']}) against "
                  f"{peer[0]['ndcg_at_20_private']:.4f} "
                  f"(reveal-norm, b={peer[0]['b']}), delta {dd:+.4f}.")
            print("  That is what revealing nothing costs in QUALITY; the cost "
                  "in ROUNDS is in bench/results/bench_sweep.jsonl.")
    else:
        best = max(rows, key=lambda r: r["ndcg_at_20_private"])
        print(f"\n  best private nDCG@20 = {best['ndcg_at_20_private']:.4f} at "
              f"ell={best['ell']}, against {best['ndcg_at_20_oracle']:.4f} for "
              f"the oracle (gap {best['gap']:+.4f}).")
        print(f"  Normaliser: {best['normalizer']}. See docs/threat-model.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
