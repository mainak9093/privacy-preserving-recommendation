# Benchmarks and raw results

Everything OblivRec reports comes from the rows in [`results/`](results/). Each row is one measurement, written by a program,
tagged with the commit that produced it, and never edited. A bad run is superseded by a new one at a later commit and filtered out
at plotting time, not deleted, so these files only grow.

## What writes what

| Program | Run with | Data file | What it measures |
|---|---|---|---|
| `bench_compose.cpp` | `mingw32-make bench` | `bench_compose.jsonl` | The five arms B1, B2, B3, FULL and B5, and that composing costs exactly the sum of the halves |
| `bench_sweep.cpp` | `mingw32-make bench` | `bench_sweep.jsonl` | Rounds and bytes against `d`, `ell`, `m`, `b` and `k`, one factor at a time, on four network profiles |
| `bench_stages.cpp` | `mingw32-make bench` | `bench_stages.jsonl` | Every round and byte attributed to a stage, with the residual checked to be zero |
| `bench_baseline.cpp` | `mingw32-make bench` | `bench_baseline.jsonl` | One private read against a cleartext lookup and a full download |
| `bench_dpf.cpp` | `mingw32-make bench` | `bench_dpf.jsonl` | The DPF primitive: generate, evaluate, whole-domain evaluate, serialise |
| `bench_dos.cpp` | `mingw32-make bench` | `bench_dos.jsonl` | Server work bought by one key, against catalogue size |
| `bench_timing.cpp` | `mingw32-make bench` | `bench_timing.jsonl` | Server time per record, interleaved across eight records |
| `scripts/distinguisher.py` | `mingw32-make distinguisher` | `distinguisher_result.jsonl` | A classifier trained on raw wire bytes, with a shuffled-label control |
| `src/apps/train.cpp` | `./build/train.exe`, and with `--headroom-only` | `train.jsonl` | Rounds and bytes of each training run, its time projected onto each network profile, and whether a truncation schedule fits at three dataset scales |
| `model/score_private.py` | `reproduce`, step 5 | `train_quality.jsonl` | nDCG@20 of every privately trained model, against the oracle |
| `model/attack.py` | `py -3.13 model/attack.py` | `leakage_attack.jsonl` | The reconstruction attack, its controls, and decoys |
| `model/dp_study.py` | `py -3.13 model/dp_study.py` | `dp_study.jsonl` | Nudge's differential privacy: quality, attack strength, signal to noise |
| `model/poison_study.py` | `py -3.13 model/poison_study.py` | `poison_study.jsonl` | What a malicious client's weighted votes do to the model |
| A run on 2026-09-06, rechecked by `model/ell_sweep.py` | `py -3.13 model/ell_sweep.py` | `oracle_ell_sweep.jsonl` | The cleartext oracle at each `ell`. The script recomputes every row and fails on a mismatch, and appends nothing |

Commands run from the repository root. The raw wire captures behind the distinguisher (`distinguisher.jsonl`, about 1.5 MB, and
`transcript.*.jsonl`) are regenerated rather than committed, so `results/` keeps the analysis and its confidence intervals.

## The row format

One JSON object per line. The C++ benchmarks write one row per repetition, never an aggregate. The Python studies write one row per
condition, with the statistics they compute, such as a median and quartiles or a confidence interval. The common fields are:

| Field | Meaning |
|---|---|
| `git_sha`, `host`, `timestamp` | Provenance. In the seven benchmarks the emitter fills these in, so a caller cannot leave them out. `train.jsonl` has no `host` |
| `profile` | `local`, `lan` (1 ms, 1 Gbit/s), `wan_a` (30 ms, 100 Mbit/s) or `wan_b` (100 ms, 10 Mbit/s) |
| `m`, `n`, `d`, `ell`, `b`, `t`, `k` | Users, items, embedding dimension, power iterations, ring bits, fractional bits, recommendations |
| `stage`, `phase` | The build stage (`S1`, `S2`, `S3`) and the part of the pipeline, from a fixed set |
| `wall_ms`, `bytes_sent` | The measurement |

Individual benchmarks add their own fields, such as `op`, `config`, `rounds` and `rep`.

## From rows to claims

- [`scripts/make_figures.py`](scripts/make_figures.py) draws all 12 figures from the newest commit's rows in each file. The oracle sweep is the
  exception, with one row per `ell` and no commit filter.
- [`scripts/baselines.py`](scripts/baselines.py) joins measured rows into the baseline table.
- [`scripts/check_claims.py`](scripts/check_claims.py) recomputes the stated numbers with the same newest-commit rule and fails if
  the report, the slides or the documents disagree with the data. It also asserts identities: additivity to the byte, a zero
  residual, rounds flat in `m`, and the closed forms the tests check.

**Reading absolute times.** Wide-area profiles are emulated at the channel level. Delivery runs over real sockets with a delay per
frame, and training's time on a profile is computed from its exactly counted rounds and bytes. Absolute times on the development
laptop drift by up to four times with its thermal state, so ratios within a run are what to trust. Rounds, bytes and quality are exact.
