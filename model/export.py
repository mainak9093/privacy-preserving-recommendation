"""Export the cleartext model into the ring, for the C++ side to consume.

THIS FILE IS ONE HALF OF A CROSS-LANGUAGE CONTRACT. The other half is
include/oblivrec/fixedpoint.hpp, which names this file as "the ONLY place a
float becomes a ring element. The C++ side never sees a float."

The three classic bugs at this boundary, each of which gets its own guard:

  1. ENDIANNESS. RingTraits<u64>::ToBytes writes little-endian with explicit
     shifts, so we write '<q' explicitly rather than relying on the host.
  2. SIGN EXTENSION. A has negatives (observed min -61.4). Encoding goes
     through a signed int64 and is checked to round-trip through the two's
     complement representation.
  3. t VERSUS 2t SCALE. Scores are a product of two t-scaled values, so they
     come out 2t-scaled. Truncating back to t needs Trunc_t, which is S2
     work, so S1 keeps scores at 2t and decodes with t=40. Getting this wrong
     silently scales every score by a million.

Outputs, all under model/out/ (gitignored):
    manifest.json          parameters and provenance
    A.bin                  m*d   int64 little-endian, row-major
    B.bin                  d*n   int64 little-endian, row-major
    scores_u42.bin         n     int64 little-endian, 2t-scaled
    top10_u42.txt          the expected answer, ids and titles

And one file that IS committed, because .gitignore un-ignores !tests/**:
    tests/data/fixedpoint_vectors.txt

That last file is the contract test. It is plain text, one
"<repr of double> <int64>" pair per line, so the C++ side parses it with
fscanf and needs no JSON dependency. It means the boundary is tested on a
fresh clone with no export run.
"""
import json
import os
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data as dataset          # noqa: E402
import mf                       # noqa: E402

OUT = os.path.join("model", "out")
VECTORS = os.path.join("tests", "data", "fixedpoint_vectors.txt")

T = 20                          # fractional bits, ARCHITECTURE section 2
DEMO_USER = 42                  # 0-based; the Phase 2 exit criterion uses --user 42


def encode(v, t=T):
    """float -> ring element, as a Python int in int64 two's complement range.

    Mirrors Encode<u64> in include/oblivrec/fixedpoint.hpp exactly:
    round(v * 2^t), cast through the signed twin.

    ROUNDING MODE IS LOAD-BEARING, and this is not the obvious choice.
    C++ std::round rounds half AWAY FROM ZERO: round(0.5) == 1.
    numpy.rint and Python's built-in round use BANKER'S rounding, half to
    even: rint(0.5) == 0. They disagree on every exact .5, which in fixed
    point means every value at exactly half a quantum.

    The committed contract test caught this on its first run, at +/- q/2.
    C++ is the reference here because it is what ships, so this matches
    std::round rather than the other way round.
    """
    x = float(np.asarray(v, dtype=np.float64)) * float(1 << t)
    scaled = int(np.trunc(x + np.copysign(0.5, x)))
    if not (-(1 << 63) <= scaled < (1 << 63)):
        raise OverflowError(
            f"encode({v}) = {scaled} does not fit int64 at t={t}. "
            "The ring width or the fractional bits need revisiting.")
    return scaled


def pack_i64(values):
    """int64 little-endian, explicitly. Not host-dependent."""
    return b"".join(struct.pack("<q", int(v)) for v in values)


def write_matrix(path, M, t=T):
    """Row-major, so C++ reads M[i][j] at index i*ncols + j."""
    flat = np.asarray(M, dtype=np.float64).reshape(-1)
    enc = [encode(x, t) for x in flat]
    with open(path, "wb") as fh:
        fh.write(pack_i64(enc))
    return len(enc)


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception as e:
        # Do NOT fail silently. This bit for real on 2026-09-10: under memory
        # pressure the git subprocess could not spawn, and every result row was
        # stamped "unknown", which RULES D5 forbids. A swallowed provenance
        # failure is invisible in the data and only shows up much later.
        print(f"WARNING: git sha unavailable ({type(e).__name__}: {e}); "
              "rows will be stamped 'unknown' and RULES D5 is not satisfied",
              file=sys.stderr)
        return "unknown"


def write_contract_vectors(A, B, S):
    """The committed cross-language test vectors.

    Deliberately includes the awkward cases rather than only round numbers:
    zero, both signs, the quantum and half the quantum, the observed extremes
    of every matrix, and values that exercise two's complement.
    """
    q = 1.0 / float(1 << T)
    cases = [
        0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0,
        q, -q, q / 2.0, -q / 2.0,          # at and below the quantum
        0.1, -0.1, 3.14159265358979, -2.718281828459045,
        float(np.max(A)), float(np.min(A)),
        float(np.max(B)), float(np.min(B)),
        float(np.max(S)), float(np.min(S)),
        123.456, -123.456, 1e-6, -1e-6,
    ]
    os.makedirs(os.path.dirname(VECTORS), exist_ok=True)
    with open(VECTORS, "w", encoding="ascii", newline="\n") as fh:
        fh.write("# Cross-language fixed-point contract vectors.\n")
        fh.write("# Written by model/export.py, read by tests/test_ring.cpp.\n")
        fh.write(f"# Format: <double repr> <int64 Encode(double) at t={T}>\n")
        fh.write("# COMMITTED on purpose: this is the only part of the\n")
        fh.write("# boundary that is testable without running an export.\n")
        fh.write(f"t {T}\n")
        for v in cases:
            fh.write(f"{v!r} {encode(v)}\n")
    return len(cases)


def main():
    os.makedirs(OUT, exist_ok=True)
    train, test = dataset.load_split("u1")
    titles = dataset.load_titles()

    print(f"factorising: d={mf.D_DEFAULT} ell={mf.ELL_DEFAULT}")
    A, B = mf.approx_factor(train, d=mf.D_DEFAULT, ell=mf.ELL_DEFAULT, seed=0)
    S = A @ B

    m, d = A.shape
    _, n = B.shape
    print(f"A {A.shape} |max|={np.abs(A).max():.4f}   "
          f"B {B.shape} |max|={np.abs(B).max():.4f}   "
          f"S {S.shape} |max|={np.abs(S).max():.4f}")

    # Headroom check, done here rather than assumed. The accumulation bound is
    # what actually decides whether b=64 survives, not the individual maxima.
    worst = int(np.abs(np.asarray([encode(np.abs(A).max())])).item()) * \
        int(np.abs(np.asarray([encode(np.abs(B).max())])).item()) * d
    print(f"worst-case d-term accumulation: {worst:.3e} "
          f"= {np.log2(float(worst)):.1f} bits of {63} available")
    if worst >= (1 << 62):
        raise OverflowError("b=64 has insufficient headroom for this model")

    na = write_matrix(os.path.join(OUT, "A.bin"), A)
    nb = write_matrix(os.path.join(OUT, "B.bin"), B)

    # Scores stay at 2t. See the module docstring.
    #
    # COMPUTED FROM THE ENCODED MATRICES, NOT FROM THE FLOAT PRODUCT.
    #
    # This distinction is not pedantic and it cost a test failure to find.
    # Quantise-then-multiply is a different operation from
    # multiply-then-quantise:
    #
    #   wrong:  encode(sum_k A[k]*B[k][j], 2t)     one rounding, at the end
    #   right:  sum_k encode(A[k]) * encode(B[k][j])   d roundings, then exact
    #
    # The servers only ever hold ENCODED values, so the second is what the
    # protocol computes and therefore what a reference must reproduce. The
    # first disagreed with C++ on 1650 of 1682 scores, by up to 2.1e7 out of
    # 4.3e12. That is about 5e-6 relative, which is invisible to any
    # tolerance-based check and irrelevant to the ranking, but it would have
    # left the oracle quietly wrong for every later exact comparison.
    #
    # Python integers are arbitrary precision, so this accumulates exactly;
    # the int64 fit was already checked by the headroom assertion above.
    a_enc = [encode(x) for x in A[DEMO_USER]]
    b_enc = [[encode(x) for x in B[k]] for k in range(d)]
    enc_scores = [sum(a_enc[k] * b_enc[k][j] for k in range(d))
                  for j in range(n)]
    for v in enc_scores:
        if not (-(1 << 63) <= v < (1 << 63)):
            raise OverflowError(
                f"score {v} does not fit int64 at 2t={2 * T}; b=64 is too narrow")
    with open(os.path.join(OUT, "scores_u42.bin"), "wb") as fh:
        fh.write(pack_i64(enc_scores))

    # The expected answer, masking items the user already rated in training.
    #
    # Ranked on the ENCODED scores, for the same reason those are computed
    # from encoded factors: that is what the system ranks. The float score is
    # written alongside purely so the file is readable by a human.
    su_float = S[DEMO_USER]
    ranked = sorted(
        (j for j in range(n) if train[DEMO_USER][j] == 0),
        key=lambda j: (-enc_scores[j], j))
    top = ranked[:10]
    with open(os.path.join(OUT, "top10_u42.txt"), "w", encoding="utf-8",
              newline="\n") as fh:
        fh.write("# rank\titem\tencoded_score_2t\tfloat_score\ttitle\n")
        for rank, j in enumerate(top):
            fh.write(f"{rank}\t{j}\t{enc_scores[j]}\t{float(su_float[j]):.9f}\t"
                     f"{titles.get(int(j), '?')}\n")

    nvec = write_contract_vectors(A, B, S)

    manifest = {
        "git_sha": git_sha(),
        "dataset": "ml-100k", "split": "u1",
        "m": int(m), "n": int(n), "d": int(d), "ell": int(mf.ELL_DEFAULT),
        "t": T, "b": 64, "seed": 0,
        "demo_user": DEMO_USER,
        "score_scale_bits": 2 * T,
        "A_elems": na, "B_elems": nb, "score_elems": len(enc_scores),
        "byte_order": "little-endian int64",
        "layout": "row-major",
    }
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8",
              newline="\n") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    print(f"wrote {OUT}/A.bin ({na} elems), B.bin ({nb} elems), "
          f"scores_u42.bin ({len(enc_scores)} elems), top10_u42.txt, manifest.json")
    print(f"wrote {VECTORS} ({nvec} vectors, committed)")
    print(f"top-1 for user {DEMO_USER}: {titles.get(int(top[0]), '?')}")


if __name__ == "__main__":
    main()
