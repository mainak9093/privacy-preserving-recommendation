"""Task 3.11: the B1/B2/B3/B5 table, joined from measured rows.

REQUIREMENTS section D6 defines five baselines. This joins what has actually
been measured into the comparison the project exists to make, and says plainly
which cells are empty and why.

    B1  cleartext MF + cleartext fetch     the speed-of-light reference
    B2  cleartext MF + PIR delivery        isolates PRIVATE DELIVERY
    B3  private MF + cleartext fetch       isolates PRIVATE TRAINING
    B4  a generic MPC framework (MP-SPDZ)  the "just use a framework" strawman
    B5  full-catalogue download            trivially private delivery

D6 says B1-B3 are the important ones, and the reason is the whole thesis: the
composition is the project, so the cost of each half SEPARATELY is the result.
B2 minus B1 is what delivery costs; B3 minus B1 is what training costs; the
full system is both.

WHERE THE NUMBERS COME FROM
    delivery  bench/results/bench_baseline.jsonl   (bench_baseline.cpp)
    training  bench/results/train.jsonl            (train.exe)
Both carry a git_sha, so a row can always be traced to the commit that made it.

B4 IS NOT MEASURED, and that is a decision rather than an omission. MP-SPDZ
needs Linux or Docker, neither of which exists on this machine (verified:
`wsl --status` reports the subsystem is not installed), it needs the private
training half to exist first, and installing it would breach the standing
no-new-dependencies rule. Reported as blocked, with the reasons, rather than
left looking unattempted.

Run:  py -3.13 bench/scripts/baselines.py
"""
import json
import os
import sys

RESULTS = os.path.join("bench", "results")


def load(name):
    path = os.path.join(RESULTS, name)
    if not os.path.exists(path):
        return []
    rows = []
    for line in open(path, "r", encoding="utf-8"):
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def newest(rows, pred):
    hits = [r for r in rows if pred(r)]
    return hits[-1] if hits else None


def fmt_bytes(b):
    if b is None:
        return "--"
    if b >= 1e6:
        return f"{b/1e6:.1f} MB"
    if b >= 1e3:
        return f"{b/1e3:.1f} kB"
    return f"{int(b)} B"


def fmt_ms(ms):
    if ms is None:
        return "--"
    if ms >= 1000:
        return f"{ms/1000:.1f} s"
    if ms < 0.001:
        return f"{ms*1e6:.1f} ns"
    return f"{ms:.3f} ms"


def main():
    base = load("bench_baseline.jsonl")
    train = load("train.jsonl")

    b1 = newest(base, lambda r: r.get("op") == "b1_cleartext")
    pir = newest(base, lambda r: r.get("op") == "dpf_pir_answer")
    b5 = newest(base, lambda r: r.get("op") == "b5_fulldownload")
    tr = newest(train, lambda r: r.get("op") == "approxfactor" and r.get("ell") == 10)

    if not (b1 and pir and b5):
        print("delivery rows missing. Run: mingw32-make bench")
        return 1
    if not tr:
        print("training rows missing. Run: ./build/train.exe --d 16 --ell 10")
        return 1

    # Delivery, per fetched record. PIR talks to two servers; B1 and B5 to one.
    deliver_bytes = {"B1": b1["bytes_sent"], "B2": 2 * pir["bytes_sent"],
                     "B5": b5["bytes_sent"]}
    deliver_ms = {"B1": b1["wall_ms"], "B2": 2 * pir["wall_ms"],
                  "B5": b5["wall_ms"]}

    # Training, whole run at d=16 ell=10 on ML-100K.
    train_bytes = tr["bytes_sent"]
    train_ms = tr["wall_ms"]
    train_rounds = tr["rounds"]

    print("D6 baselines, ML-100K (d=16, ell=10), local profile")
    print(f"  training rows from git_sha {tr['git_sha']}, "
          f"delivery from {b1['git_sha']}")
    print()
    print(f"  {'':<4} {'configuration':<34} {'training':<14} "
          f"{'delivery/record':<16} {'private?'}")

    rows = [
        ("B1", "cleartext MF + cleartext fetch", None, "B1", "neither"),
        ("B2", "cleartext MF + PIR delivery", None, "B2", "delivery only"),
        ("B3", "private MF + cleartext fetch", "priv", "B1", "training only"),
        ("--", "FULL SYSTEM: private MF + PIR", "priv", "B2", "both"),
        ("B5", "cleartext MF + full download", None, "B5", "delivery, trivially"),
    ]
    for tag, desc, tr_kind, dl, priv in rows:
        t_cell = (f"{fmt_ms(train_ms)} / {fmt_bytes(train_bytes)}"
                  if tr_kind else "cleartext")
        d_cell = f"{fmt_bytes(deliver_bytes[dl])} / {fmt_ms(deliver_ms[dl])}"
        print(f"  {tag:<4} {desc:<34} {t_cell:<14} {d_cell:<16} {priv}")

    print()
    print("  B4  generic MPC framework (MP-SPDZ)      BLOCKED: needs Linux or")
    print("      Docker (neither present; wsl --status reports WSL is not")
    print("      installed), needs S2 to exist first, and installing it would")
    print("      breach the no-new-dependencies rule.")
    print()
    print("  The isolation, which is the point of the table:")
    print(f"    private DELIVERY costs {deliver_bytes['B2']/deliver_bytes['B1']:.1f}x "
          f"the bytes of a cleartext fetch and "
          f"{deliver_ms['B2']/deliver_ms['B1']:.0f}x the server time,")
    print(f"    but {deliver_bytes['B5']/deliver_bytes['B2']:.0f}x FEWER bytes "
          f"than downloading the catalogue.")
    print(f"    private TRAINING costs {train_rounds} rounds and "
          f"{fmt_bytes(train_bytes)} for a whole run, which is")
    print("    round-bound on every WAN profile -- see the train.exe output.")
    print()
    print("  Read together: delivery privacy is cheap and bounded per query;")
    print("  training privacy is a fixed cost per model, paid once. Neither")
    print("  half dominates the other, which is why the composition is")
    print("  worth building rather than picking one.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
