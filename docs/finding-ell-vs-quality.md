# Finding: recommendation quality is flat in the power-iteration round count

**Measured 2026-09-06, day 1 of the S1 sprint. Raw data: `bench/results/oracle_ell_sweep.jsonl`.**
Reproduce with `py -3.13 model/mf.py` and the sweep in that JSONL.

## What was measured

`ApproxFactor` (cleartext, `model/mf.py`) on MovieLens-100K, `u1.base`/`u1.test`, `d = 16`,
sweeping the inner power-iteration round count `ell`.

| `ell` | subspace angle vs SVD (rad) | nDCG@20 | Recall@20 |
|---:|---:|---:|---:|
| 10 | 9.659e-01 | 0.4536 | 0.4693 |
| 25 | 4.170e-01 | 0.4545 | 0.4677 |
| 50 | 1.834e-01 | 0.4525 | 0.4650 |
| 100 | 6.966e-02 | 0.4523 | 0.4662 |
| 200 | 3.604e-03 | 0.4526 | 0.4664 |
| 400 | 6.652e-06 | 0.4526 | 0.4664 |

## Two separate conclusions

**1. The implementation is correct.** The angle falls monotonically to 6.7e-6 rad, so power
iteration does converge to the top-`d` right singular subspace of `U`. The large angle at
`ell = 10` is slow convergence, not a bug. The spectrum explains the rate: the eigenvalue gap
`sigma_16^2 / sigma_17^2` is **1.032**, a 3% gap, and power iteration converges as a power of that
ratio. The top component is easy (`sigma_1^2/sigma_2^2 = 6.44`); the sixteenth is not.

This distinction matters and was nearly missed. Had the SVD cross-check simply been dropped when it
first failed, a correct implementation would have been "fixed" into a wrong one.

**2. Quality is flat in `ell`, and that is the useful part.** Across a 40x increase in `ell`, and a
five-order-of-magnitude improvement in subspace convergence, nDCG@20 moves between 0.4523 and
0.4545, a spread of about 0.5%, with no monotone trend. Recall@20 behaves the same way.

The reason is that ranking does not need the singular vectors, only a good enough subspace. Once
the top few components are approximately right, the ordering of scores is stable, and the
remaining rotation within the subspace does not change which items land in the top 20.

## Why this matters for Phase 3, which is the actual point

`ell` is the multiplier on the **only interactive cost in private training**. Under 2-of-3
replicated sharing the matrix-vector products are non-interactive and free. `Trunc_t` and
`ApproxNormalize` are the entire communication cost, and they run **once per inner iteration**, so
communication is linear in `ell`.

So this measurement says: **run private training at `ell = 10` and the 40x communication saving
costs nothing in recommendation quality.** That is a design decision made on evidence rather than
on the ARCHITECTURE section 2 default, and it was available for about twenty minutes of work on
day 1.

It also satisfies the instruction ARCHITECTURE section 5 already gave and which had not been acted
on: *"Do not assume a fixed `ell` is enough. Measure the residual against the cleartext oracle and
report `ell` vs quality."*

## Caveats, stated plainly

- One dataset (ML-100K), one split (`u1`), one seed, `d = 16`. The flatness may not hold at ML-1M
  or at `d = 32`, and it should be re-measured before being relied on in the final report.
- nDCG@20 with binary relevance at rating >= 4. NUDGE's 0.29 is on Netflix data under its own
  convention, so these numbers are **not** directly comparable to it. They are a within-project
  baseline, which is what B1 is for.
- The flatness argument applies to *ranking quality*. If a later stage needs the actual singular
  vectors rather than the subspace, this conclusion does not transfer.
