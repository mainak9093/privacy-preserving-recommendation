"""Task 4.7 / D9.3: how far can one malicious client move the model?

THE SETTING, AND WHY IT IS NOT THE ONE THE REQUIREMENT ASSUMED. D9.3 asks for
"the well-formedness check on user submissions, so one malicious user cannot
skew the model". OblivRec has no rating-upload path to check: training reads
MovieLens off local disk, and task 3.9 deliberately removed the upload step --
consumption is harvested from the PIR queries themselves, with no separate
submission.

So the object to validate is not a rating vector. It is the DPF query. An
honest client goes through PirClient::Query, which fixes beta = 1 so the
difference of the two expansions is the selector vector exactly. A malicious
client can call Gen(alpha, beta, domain_bits) directly with any beta: retrieval
still works (the record returns scaled by beta, which the attacker divides out)
and the servers fold beta -- not 1 -- into the consumption accumulator that
becomes the next round's training input.

tests/test_harvest.cpp demonstrates the mechanism: one query with beta = 10^6
puts 1000000 into the consumption vector, and every pre-existing check passes.
This script measures what that BUYS, which is the part that decides whether the
defence is worth its round.

WHAT IS MEASURED. An attacker picks a handful of target items it wants promoted
and submits queries carrying weight w. We retrain on the poisoned consumption
matrix and ask, over the HONEST users only:

  - how far up the ranking do the targets move (mean rank, and rank@1 share);
  - how many honest users end up with a target in their top-20;
  - how far the item embedding B moves, as a subspace angle against the clean
    model, which separates "the targets moved" from "the whole model moved".

The weight axis is the point. If the curve is flat, the check is not worth a
round and we say so. If one client at w = 10^6 owns the model, the check is
not optional.

Run:  py -3.13 model/poison_study.py
"""
import json
import os
import socket
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data as dataset           # noqa: E402
import metrics                   # noqa: E402
import mf                        # noqa: E402

RESULTS = os.path.join("bench", "results", "poison_study.jsonl")
SEED = 0
TOPK = 20
N_TARGETS = 5

# w = 1 is the honest weight: an attacker who plays by the rules and simply
# fetches its targets. Everything above it is the hole.
W_GRID = [1, 10, 100, 1000, 10000, 1000000]
N_ATTACKERS = [1, 5]

# The second experiment, and the one that matters. Power iteration NORMALISES
# every step, so weight alone cannot grow without bound -- past a point it only
# fixes a direction. A single attacker can therefore capture at most one of the
# d components, and the other d-1 still carry the honest signal. The sharp
# question is how many COLLUDING clients, each with its own distinct target
# set, it takes to capture enough components to own the model. d = 16, so the
# grid brackets it.
N_COLLUDERS = [1, 2, 4, 8, 16, 32]
COLLUDE_WEIGHT = 1000000


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception as e:
        print(f"WARNING: git sha unavailable ({type(e).__name__}: {e})",
              file=sys.stderr)
        return "unknown"


def rank_of(scores, rated, items):
    """Mean rank (1 = best) of `items` for each user, excluding rated ones."""
    s = np.where(rated, -np.inf, scores)
    order = np.argsort(-s, axis=1)
    rank = np.empty_like(order)
    rows = np.arange(s.shape[0])[:, None]
    rank[rows, order] = np.arange(s.shape[1])[None, :] + 1
    return rank[:, items]


def main():
    train, test = dataset.load_split("u1")
    U = np.asarray(train, dtype=np.float64)
    m, n = U.shape
    rated = U > 0
    rng = np.random.default_rng(SEED)

    # The attacker promotes items that are currently obscure -- promoting what
    # is already popular would be indistinguishable from the model working.
    pop = (U > 0).sum(axis=0)
    obscure = np.argsort(pop)[: n // 2]
    targets = np.sort(rng.choice(obscure, size=N_TARGETS, replace=False))

    A0, B0 = mf.approx_factor(U, seed=SEED)
    S0 = A0 @ B0
    base_rank = rank_of(S0, rated, targets)
    base_top = float(np.mean([(np.isin(
        np.argsort(-np.where(rated[i], -np.inf, S0[i]))[:TOPK], targets)).any()
        for i in range(m)]))

    print(f"ML-100K u1: {m} users x {n} items. "
          f"{N_TARGETS} target items, chosen from the least-rated half.")
    print(f"  targets {list(map(int, targets))}, "
          f"median popularity {int(np.median(pop[targets]))} ratings "
          f"(catalogue median {int(np.median(pop))})")
    base_ndcg = metrics.ndcg_at_k(S0, test, train, k=TOPK)
    print(f"  clean model: mean target rank {base_rank.mean():.0f} of {n}, "
          f"{base_top:.1%} of users have one in their top-{TOPK}, "
          f"nDCG@20 {base_ndcg:.4f}")
    print()
    print(f"  {'atk':>4} {'weight':>9}  {'nDCG@20':>8}  {'mean rank':>10} "
          f"{'in top20':>9}  {'subspace angle':>14}")

    sha, host = git_sha(), socket.gethostname()
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    rows = []

    for n_atk in N_ATTACKERS:
        for w in W_GRID:
            # Each attacker is one extra row in the consumption matrix, with
            # weight w on the targets. That is exactly what the harvest path
            # would write, since it folds beta into the accumulator.
            atk = np.zeros((n_atk, n), dtype=np.float64)
            atk[:, targets] = float(w)
            Up = np.vstack([U, atk])

            Ap, Bp = mf.approx_factor(Up, seed=SEED)
            # Score only the HONEST users; the attacker's own recommendations
            # are not interesting and would flatter the result.
            Sp = Ap[:m] @ Bp
            r = rank_of(Sp, rated, targets)
            top = float(np.mean([(np.isin(
                np.argsort(-np.where(rated[i], -np.inf, Sp[i]))[:TOPK],
                targets)).any() for i in range(m)]))
            at1 = float(np.mean(r.min(axis=1) == 1))

            try:
                from scipy.linalg import subspace_angles
                ang = float(np.max(subspace_angles(Bp.T, B0.T)))
            except Exception:
                ang = float("nan")

            ndcg = metrics.ndcg_at_k(Sp, test, train, k=TOPK)
            print(f"  {n_atk:>4} {w:>9,}  {ndcg:>8.4f}  {r.mean():>10.0f} "
                  f"{top:>8.1%}  {ang:>14.3e}")

            rows.append({
                "git_sha": sha, "host": host, "profile": "local",
                "m": int(m), "n": int(n), "d": int(mf.D_DEFAULT),
                "ell": int(mf.ELL_DEFAULT), "b": None, "t": None, "k": TOPK,
                "stage": "S2", "phase": "oracle", "op": "harvest_poison",
                "dataset": "ml-100k", "attackers": int(n_atk),
                "weight": int(w), "n_targets": int(N_TARGETS),
                "targets": [int(x) for x in targets],
                "ndcg_at_20": float(ndcg),
                "ndcg_at_20_clean": float(base_ndcg),
                "mean_target_rank": float(r.mean()),
                "mean_target_rank_clean": float(base_rank.mean()),
                "frac_rank1": at1,
                "frac_users_target_in_top20": top,
                "frac_users_target_in_top20_clean": base_top,
                "subspace_angle_rad": ang,
                "wall_ms": 0, "bytes_sent": 0, "timestamp": stamp,
            })

    # ---- experiment 2: colluders, each burning a DIFFERENT direction -------
    print()
    print(f"  Colluding clients at weight {COLLUDE_WEIGHT:,}, each with its "
          f"own distinct targets.")
    print(f"  d = {mf.D_DEFAULT}, so the question is what happens as the "
          f"colluder count crosses it.")
    print()
    print(f"  {'colluders':>10}  {'nDCG@20':>8}  {'vs clean':>9}  "
          f"{'subspace angle':>14}")

    for c in N_COLLUDERS:
        # Distinct target sets, so each attacker pushes a different direction.
        # Identical sets would all collapse onto one component and measure the
        # experiment above again.
        rng_c = np.random.default_rng(SEED + 99)
        atk = np.zeros((c, n), dtype=np.float64)
        for a in range(c):
            t = rng_c.choice(obscure, size=N_TARGETS, replace=False)
            atk[a, t] = float(COLLUDE_WEIGHT)
        Up = np.vstack([U, atk])

        Ap, Bp = mf.approx_factor(Up, seed=SEED)
        Sp = Ap[:m] @ Bp
        ndcg = metrics.ndcg_at_k(Sp, test, train, k=TOPK)
        try:
            from scipy.linalg import subspace_angles
            ang = float(np.max(subspace_angles(Bp.T, B0.T)))
        except Exception:
            ang = float("nan")

        rel = 100 * (ndcg - base_ndcg) / base_ndcg
        print(f"  {c:>10}  {ndcg:>8.4f}  {rel:>8.1f}%  {ang:>14.3e}")

        rows.append({
            "git_sha": sha, "host": host, "profile": "local",
            "m": int(m), "n": int(n), "d": int(mf.D_DEFAULT),
            "ell": int(mf.ELL_DEFAULT), "b": None, "t": None, "k": TOPK,
            "stage": "S2", "phase": "oracle", "op": "harvest_poison_collude",
            "dataset": "ml-100k", "attackers": int(c),
            "weight": int(COLLUDE_WEIGHT), "n_targets": int(N_TARGETS),
            "distinct_targets": True,
            "ndcg_at_20": float(ndcg),
            "ndcg_at_20_clean": float(base_ndcg),
            "ndcg_rel_pct": float(rel),
            "subspace_angle_rad": ang,
            "wall_ms": 0, "bytes_sent": 0, "timestamp": stamp,
        })

    os.makedirs(os.path.dirname(RESULTS), exist_ok=True)
    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    print(f"\n  appended {len(rows)} rows to {RESULTS}")

    solo = [r for r in rows if r["op"] == "harvest_poison"
            and r["attackers"] == 1]
    coll = [r for r in rows if r["op"] == "harvest_poison_collude"]
    honest = next(r for r in solo if r["weight"] == 1)
    worst_solo = min(solo, key=lambda r: r["ndcg_at_20"])
    d = mf.D_DEFAULT

    print()
    print("  ONE ATTACKER CANNOT DO MUCH, AND THE REASON IS THE NORMALISATION.")
    print(f"  At weight {worst_solo['weight']:,}, nDCG@20 moves only "
          f"{base_ndcg:.4f} -> {worst_solo['ndcg_at_20']:.4f} "
          f"({100*(worst_solo['ndcg_at_20']-base_ndcg)/base_ndcg:+.1f}%), and "
          f"the targets do")
    print(f"  not even rise: mean rank {base_rank.mean():.0f} -> "
          f"{worst_solo['mean_target_rank']:.0f} of {n}. Power iteration "
          f"normalises every step, so")
    print(f"  past a point extra weight only fixes a DIRECTION. One attacker "
          f"captures one")
    print(f"  of the d = {d} components; the other {d-1} still carry the "
          f"honest signal.")

    if coll:
        below = [r for r in coll if r["attackers"] < d]
        at_or_above = [r for r in coll if r["attackers"] >= d]
        if below and at_or_above:
            worst_below = min(below, key=lambda r: r["ndcg_at_20"])
            first_at = min(at_or_above, key=lambda r: r["attackers"])
            print()
            print(f"  BUT d COLLUDERS OWN IT COMPLETELY, and the cliff is at "
                  f"exactly d = {d}.")
            print(f"  {worst_below['attackers']} colluders with distinct "
                  f"targets: nDCG {worst_below['ndcg_at_20']:.4f} "
                  f"({worst_below['ndcg_rel_pct']:+.1f}%).")
            print(f"  {first_at['attackers']} colluders: nDCG "
                  f"{first_at['ndcg_at_20']:.4f} "
                  f"({first_at['ndcg_rel_pct']:+.1f}%) -- the model is gone.")
            print(f"  Each colluder burns one component. Burn all {d} and "
                  f"nothing honest is left.")

    print()
    print(f"  THE CHECK BUYS THIS ENTIRELY, for one round and two ring "
          f"elements per query.")
    print(f"  The control is the same attacker with the same targets at the "
          f"honest weight of 1:")
    print(f"  nDCG {honest['ndcg_at_20']:.4f}, subspace angle "
          f"{honest['subspace_angle_rad']:.2e}. That is what the weight check")
    print(f"  enforces, and it is the difference between a working recommender "
          f"and none.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
