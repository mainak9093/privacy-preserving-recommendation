"""The distinguisher experiment: show an adversary FAILING.

This is the evidence half of the Phase 2 exit criterion. The original wording
asked for `tcpdump` on the server links showing "nothing but pseudorandom
bytes"; the 2026-09-06 Decisions Log row replaced that with a channel-boundary
transcript plus this experiment, for a reason worth restating:

    Bytes *looking* random is not evidence. Any fixed byte string looks random
    if you do not know what to compare it against. The claim that matters is
    that an adversary given the wire bytes CANNOT DO BETTER THAN GUESSING at
    saying which record was fetched -- and the way to show that is to build the
    adversary and measure it losing.

WHAT IS TESTED. src/apps/probe.cpp records many independent PIR queries for
each of several fixed record indices. Each query calls Gen afresh, so the keys
are independently sampled -- an adversary fed one key repeated N times would be
measuring nothing.

Two separate claims, tested separately because they can fail separately:

  1. LENGTH. Every frame must be the same size regardless of the index. This
     is true by construction -- DPF key size depends only on domain_bits -- but
     it is checked in the data rather than asserted, because a framing change
     could break it silently.

  2. CONTENT. A classifier trained on the raw wire bytes must not beat chance
     on held-out data.

HOW A RESULT IS READ. Accuracy near chance is the expected outcome. It is
reported with a Wilson 95% interval, and the honest reading is "chance is
inside the interval", not "we got exactly 50%". If the interval ever excludes
chance, that is a real finding and it gets reported rather than re-run until it
behaves.

A NEGATIVE CONTROL runs alongside: the same classifier on deliberately
shuffled labels. If the real accuracy is at chance but the shuffled control is
too, the experiment is at least measuring what it claims to.

Run:  py -3.13 bench/scripts/distinguisher.py
"""
import base64
import collections
import json
import math
import os
import socket
import subprocess
import sys
import time

import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.model_selection import train_test_split

TRANSCRIPT = os.path.join("bench", "results", "distinguisher.jsonl")
RESULTS = os.path.join("bench", "results", "distinguisher_result.jsonl")
SEED = 0


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


def wilson(k, n, z=1.96):
    """Wilson score interval. Used rather than the normal approximation
    because near p=0.5 with a few hundred trials the difference matters, and
    because it cannot produce an interval outside [0, 1]."""
    if n == 0:
        return (0.0, 1.0)
    p = k / n
    d = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / d
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (centre - half, centre + half)


def load_frames(path):
    """Group the client's OUTBOUND frames by the index they encode.

    Outbound only: those carry the DPF key, which is the message whose secrecy
    is the whole claim. The tag is ground truth for scoring and is NOT a
    feature -- the adversary never sees it.
    """
    if not os.path.exists(path):
        return {}
    groups = collections.defaultdict(list)
    for line in open(path, "r", encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        if r.get("dir") != "send":
            continue
        if r.get("len", 0) < 8:          # the 1-byte kBye frame
            continue
        groups[r["tag"]].append(np.frombuffer(
            base64.b64decode(r["b64"]), dtype=np.uint8))
    return groups


def check_lengths(groups):
    """Claim 1: frame length must not depend on the index."""
    lengths = {tag: sorted({len(f) for f in frames})
               for tag, frames in groups.items()}
    all_lengths = sorted({n for v in lengths.values() for n in v})
    print("  claim 1 -- LENGTH")
    for tag, v in sorted(lengths.items()):
        print(f"    {tag:<14} frame sizes observed: {v}")
    ok = len(all_lengths) == 1
    if ok:
        print(f"    PASS: every one of the "
              f"{sum(len(f) for f in groups.values())} frames is "
              f"{all_lengths[0]} bytes, whatever the index.")
    else:
        print(f"    FAIL: frame size depends on the index: {all_lengths}")
    return ok, all_lengths


def distinguish(groups, tag_a, tag_b, shuffle=False, seed=SEED):
    """Claim 2: train an adversary on raw wire bytes, score it on held-out data."""
    xa = np.stack(groups[tag_a])
    xb = np.stack(groups[tag_b])
    X = np.vstack([xa, xb]).astype(np.float64)
    y = np.concatenate([np.zeros(len(xa)), np.ones(len(xb))])

    rng = np.random.default_rng(seed)
    if shuffle:
        y = rng.permutation(y)

    Xtr, Xte, ytr, yte = train_test_split(
        X, y, test_size=0.4, random_state=seed, stratify=y)
    clf = LogisticRegression(max_iter=2000, C=1.0)
    clf.fit(Xtr, ytr)
    pred = clf.predict(Xte)
    correct = int((pred == yte).sum())
    n = len(yte)
    lo, hi = wilson(correct, n)
    return correct, n, correct / n, lo, hi


def main():
    if not os.path.exists(TRANSCRIPT):
        print(f"no transcript at {TRANSCRIPT}.")
        print("Generate one with:")
        print("  bash scripts/run_servers.sh start")
        print("  ./build/probe.exe --alpha 0    --count 600 --port 7000 \\")
        print(f"      --transcript {TRANSCRIPT}")
        print("  ./build/probe.exe --alpha 1234 --count 600 --port 7000 \\")
        print(f"      --transcript {TRANSCRIPT}")
        print("  bash scripts/run_servers.sh stop")
        return 1

    groups = load_frames(TRANSCRIPT)
    if len(groups) < 2:
        print(f"need at least two index groups in the transcript, found "
              f"{len(groups)}")
        return 1

    print("distinguisher: can an observer tell which record was fetched?")
    print(f"  transcript: {TRANSCRIPT}")
    for tag, frames in sorted(groups.items()):
        print(f"    {tag:<14} {len(frames)} queries")
    print()

    len_ok, lengths = check_lengths(groups)
    print()

    print("  claim 2 -- CONTENT (logistic regression on the raw wire bytes)")
    tags = sorted(groups.keys())
    rows = []
    all_ok = True
    for i in range(len(tags)):
        for j in range(i + 1, len(tags)):
            a, b = tags[i], tags[j]
            correct, n, acc, lo, hi = distinguish(groups, a, b)
            chance_inside = (lo <= 0.5 <= hi)
            all_ok = all_ok and chance_inside
            verdict = "at chance" if chance_inside else "ABOVE CHANCE"
            print(f"    {a} vs {b}:  {acc:.3f}  "
                  f"95% CI [{lo:.3f}, {hi:.3f}]  n={n}  -> {verdict}")
            rows.append({
                "pair": f"{a}|{b}", "accuracy": acc, "ci_lo": lo, "ci_hi": hi,
                "n_test": n, "shuffled": False,
                "chance_inside_ci": bool(chance_inside),
            })

    # Negative control: the same pipeline on shuffled labels. If this does not
    # also sit at chance, the harness is broken rather than the protocol safe.
    a, b = tags[0], tags[1]
    correct, n, acc, lo, hi = distinguish(groups, a, b, shuffle=True)
    print(f"    [control, labels shuffled]:  {acc:.3f}  "
          f"95% CI [{lo:.3f}, {hi:.3f}]")
    rows.append({"pair": f"{a}|{b}", "accuracy": acc, "ci_lo": lo, "ci_hi": hi,
                 "n_test": n, "shuffled": True,
                 "chance_inside_ci": bool(lo <= 0.5 <= hi)})

    total_queries = sum(len(v) for v in groups.values())
    print()
    if len_ok and all_ok:
        print(f"  RESULT: over {total_queries} recorded queries, frame length is")
        print(f"  constant at {lengths[0]} bytes and no classifier beat chance.")
        print("  The observer learns nothing about which record was fetched.")
    else:
        print("  RESULT: a claim FAILED above. That is a finding; report it.")

    sha, stamp = git_sha(), time.strftime("%Y-%m-%dT%H:%M:%S")
    with open(RESULTS, "a", encoding="utf-8", newline="\n") as fh:
        for r in rows:
            r.update({"git_sha": sha, "host": socket.gethostname(),
                      "profile": "local", "stage": "S1", "phase": "net",
                      "frame_bytes": lengths[0] if lengths else None,
                      "total_queries": total_queries, "timestamp": stamp})
            fh.write(json.dumps(r) + "\n")
    print(f"\n  appended {len(rows)} rows to {RESULTS}")
    return 0 if (len_ok and all_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
