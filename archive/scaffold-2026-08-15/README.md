# Parked scaffold — 15 August 2026

Nothing here is lost or stale. It is **parked**, not deleted, following the convention used
in `Projects/Algorithmic_Game_Theory/report/working/`.

## What this is

An empty source-tree scaffold (`.gitkeep` files only — **no code was ever written**) for the
architecture in [`../../design/ARCHITECTURE-draft-v1.md`](../../design/ARCHITECTURE-draft-v1.md):

```
src/dpf/            DPF: GGM tree, AES-NI
src/pir/            DPF-PIR delivery, consumption harvesting
src/mpc/            replicated sharing, PRF setup, matrix-vector programs
src/mpc/nonlinear/  Trunc_t, ApproxNormalize, FSS compare / zero-test
src/mf/             power iteration, SetOrthogonal, ApproxFactor
src/serve/          scores, seen-item masking, oblivious top-k
src/apps/           server0/1/2, user client, demo CLI
src/common/         ring arithmetic, fixed-point, serialisation
src/net/            framing, batching
include/oblivrec/   public headers
model/              cleartext oracle and quality evaluation
bench/              sweeps, netem profiles, figures
tests/              unit and end-to-end
third_party/        MP-SPDZ / SimplePIR baselines
```

## Why it was parked

The project is in its **literature survey phase** (Milestone 1, 31 August). Building the
directory tree for a protocol design before the survey has validated that design is exactly
backwards. Two findings on 15 August made this concrete:

1. **Nudge ships a complete MIT-licensed reference implementation** —
   [NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs), Go + AVX2/AES-NI,
   with `dcf/`, `dmsb/`, `multdpf/`, the full 3PC protocol and phase benchmarks. Whether we
   reimplement the training core or build on theirs is **deferred to the instructor meeting**.
   `src/mpc/`, `src/mpc/nonlinear/` and `src/mf/` may never need to exist.

2. **The oblivious top-*k* serving layer is probably unnecessary.** Nudge publishes the item
   embedding matrix `B` in the clear, and each user holds their own ratings `u⁽ⁱ⁾`. At
   MovieLens scale `B` is ~800 KB — the user downloads it once and computes top-*k* locally,
   with no cryptography at all. So `src/serve/` solves a problem that may not exist in our
   parameter regime. What genuinely remains open is **private content fetch** (`src/pir/`),
   which is precisely what Nudge delegates to "other means".

Both points are recorded in the survey plan and must be settled by the survey's §8 and by the
instructor meeting before any of this is recreated.

## How to restore

The tree is empty, so restoring is a `git mv` — but do not restore it wholesale. Recreate only
the directories the post-survey architecture actually calls for:

```bash
git mv archive/scaffold-2026-08-15/src src
git mv archive/scaffold-2026-08-15/include include
# ...etc, selectively
```

Update [`../../design/ARCHITECTURE-draft-v1.md`](../../design/ARCHITECTURE-draft-v1.md) first —
promote it out of `design/` back to the repo root once the survey has confirmed or replaced it.

## What was *not* parked

- `scripts/fetch_data.py` and `data/` — working and verified (MovieLens-100K and 1M checksums
  confirmed against live downloads), needed regardless of which architecture wins.
- `references/` — the two instructor-recommended papers.
- `docs/` — the contribution log, which runs for the whole semester.
