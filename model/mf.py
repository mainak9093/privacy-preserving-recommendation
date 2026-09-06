"""Cleartext power-iteration matrix factorisation. The quality oracle (B1).

This is written to mirror design/ARCHITECTURE-draft-v1.md section 5 LINE FOR
LINE, deliberately, rather than shortcutting to numpy.linalg.svd. Two reasons:

  1. It is the reference the S2 (private) implementation gets checked against
     in Phase 3, so its structure has to match the protocol we will run under
     secret sharing, not merely its output.
  2. Running SVD alongside as a CROSS-CHECK is a real result: power iteration
     on U^T U converges to the top-d right singular vectors, so the subspace
     angle between B and Vt[:d] should be ~0. That check de-risks Phase 3 for
     two lines of code.

ApproxFactor(U) -> (A, B), per NUDGE Fig. 4:

    B := 0 in R^{d x n}
    for i in 1..d:
        v := random n-vector
        v := Normalize(SetOrthogonal(v, B))
        for j in 1..ell:
            v := Mul(U^T, Mul(U, v))     # free under replicated sharing
            v := SetOrthogonal(v, B)     # Gram-Schmidt vs already-found rows
            v := Normalize(v)            # the interactive step in S2
        B[i] := v                        # REVEALED IN THE CLEAR
    A := U . B^T

The load-bearing design decision is NUDGE's, not ours: each converged row of B
is opened before the next component is computed, which is what keeps
SetOrthogonal cheap (Gram-Schmidt against public vectors). It is also why the
item embedding model is public to every server, which is the premise of the
section 9.3 reconstruction analysis.
"""
import numpy as np

D_DEFAULT = 16      # d, ARCHITECTURE section 2
ELL_DEFAULT = 10    # ell, inner power-iteration rounds


def set_orthogonal(v, B, rows_filled):
    """Gram-Schmidt v against the already-converged (public) rows of B."""
    for i in range(rows_filled):
        v = v - np.dot(v, B[i]) * B[i]
    return v


def normalize(v, eps=1e-12):
    nrm = float(np.linalg.norm(v))
    return v / nrm if nrm > eps else v


def approx_factor(U, d=D_DEFAULT, ell=ELL_DEFAULT, seed=0):
    """Return (A, B) with B in R^{d x n} and A = U . B^T in R^{m x d}."""
    rng = np.random.default_rng(seed)
    n = U.shape[1]
    B = np.zeros((d, n), dtype=np.float64)
    for i in range(d):
        v = rng.standard_normal(n)
        v = normalize(set_orthogonal(v, B, i))
        for _ in range(ell):
            v = U.T @ (U @ v)                 # the two matrix-vector products
            v = set_orthogonal(v, B, i)
            v = normalize(v)
        B[i] = v
    A = U @ B.T
    return A, B


def scores_from(A, B):
    return A @ B


def svd_crosscheck(U, B):
    """Subspace angle between B's row space and the top-d right singular
    vectors of U. Near zero means power iteration converged to the right
    subspace. Returns the largest principal angle in radians."""
    from scipy.linalg import subspace_angles
    d = B.shape[0]
    _, _, Vt = np.linalg.svd(U, full_matrices=False)
    ang = subspace_angles(B.T, Vt[:d].T)
    return float(np.max(ang))


if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import data as dataset
    import metrics

    train, test = dataset.load_split("u1")
    print(f"train nnz={int((train>0).sum())}  test nnz={int((test>0).sum())}")

    A, B = approx_factor(train, d=D_DEFAULT, ell=ELL_DEFAULT, seed=0)
    S = scores_from(A, B)

    ndcg = metrics.ndcg_at_k(S, test, train, k=20)
    rec = metrics.recall_at_k(S, test, train, k=20)
    ang = svd_crosscheck(train, B)

    print(f"d={D_DEFAULT} ell={ELL_DEFAULT}")
    print(f"nDCG@20  = {ndcg:.4f}")
    print(f"Recall@20= {rec:.4f}")
    print(f"max principal angle vs SVD top-{D_DEFAULT} subspace = {ang:.3e} rad")
    print()
    print("On the angle: at ell=10 this is ~0.97 rad and that is EXPECTED, not a")
    print("bug. See docs/finding-ell-vs-quality.md. Correctness is established by")
    print("convergence at high ell (6.7e-6 rad at ell=400); the default ell=10 is")
    print("the QUALITY operating point, and quality is flat in ell.")

    # Correctness assertion, distinct from the quality run above: at high ell
    # the subspace must converge, otherwise approx_factor really is wrong.
    _, B_conv = approx_factor(train, d=D_DEFAULT, ell=400, seed=0)
    ang_conv = svd_crosscheck(train, B_conv)
    print(f"correctness check, ell=400: angle = {ang_conv:.3e} rad", end=" ")
    print("OK" if ang_conv < 1e-3 else "FAILED")
