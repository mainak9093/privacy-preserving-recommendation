# Contribution Log

> **Why this file exists.** The mid-term report (5%, due **12 September 2026**) is explicitly
> *"a two-page summary describing the contributions of each group member"*, and a viva may be
> called at any stage. Reconstructing this from memory on 11 September is how groups lose these
> marks.
>
> **Append one line after every work session.** Newest at the bottom of your own section.
> Keep it factual: what you did, what landed, what is still open.

---

> **Ownership is not yet assigned.** The four survey tracks (T1–T4,
> [`report/README.md`](../report/README.md)) are assigned at the Phase 0 kickoff; the
> implementation streams (W1–W4) after the instructor meeting. Log your work under whichever
> heading fits — **the log matters, the label does not.** Anything that does not fit goes in
> *Shared / joint work*.

## Survey tracks (live now, Milestone 1)

### T1 — Primitives · §3

*Owner: unassigned* — secret sharing, FSS/DPFs, PIR, ORAM, fixed-point over rings.

| Date | Hours | What was done | Artefact (note / section / commit) |
|---|---|---|---|
| | | | |

### T2 — Private training · §4

*Owner: unassigned* — Nikolaenko → PIRSONA → Nudge; the federated and FHE alternatives.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### T3 — Private retrieval · §5

*Owner: unassigned* — SANNS → Tiptoe → Pacmann/Compass → Wally/Panther/MESS; private top-*k*.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### T4 — Threat models, systems, the gap · §2, §6, §7, §8

*Owner: unassigned* — privacy notions, PIRSONA as an end-to-end system, evaluation norms,
the gap statement and the two research questions.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

---

## Implementation streams (provisional, Phase 2 onward)

These may not survive the instructor meeting intact — see
[REQUIREMENTS.md §10](../REQUIREMENTS.md).

### W1 — FSS core and private delivery

*Owner: unassigned* — DPF (AES-NI GGM tree), FSS comparison/zero-test gates, DPF-PIR read layer,
consumption harvesting.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W2 — 3PC substrate and non-linear protocols

*Owner: unassigned* — replicated 2-of-3 sharing, PRF setup, matrix-vector programs, `Trunc_t`,
`ApproxNormalize`, the ring-width study.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W3 — Factorization and serving

*Owner: unassigned* — power iteration / `ApproxFactor`, `SetOrthogonal`, score computation,
seen-item masking, cleartext quality oracle.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W4 — Evaluation and security analysis

*Owner: unassigned* — benchmark harness, `tc netem` profiles, baselines, threat model,
leakage profile, the public-`B` reconstruction analysis, reproducibility.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

---

## Shared / joint work

Meetings, deck building, report writing, dry runs — anything not attributable to one track.

| Date | Who | What |
|---|---|---|
| 2026-09-06 (day 2 work, pulled forward) | Mainak | **S1 sprint day 2, the DPF.** `include/oblivrec/dpf.hpp` frozen first as the cross-component contract, then `tests/test_dpf_invariant.cpp` written *before* `Gen` was trusted, then `src/dpf/dpf.cpp`. The invariant checker walks both parties' trees in lockstep and asserts the BGI structural property at every internal node (`s0 != s1` and `t0^t1 == 1` on the path to alpha, `s0 == s1` and `t0^t1 == 0` off it), so a control-bit bug names its own level and node instead of surfacing as "correct at 2 bits, wrong at 8". `Gen`, `Eval` and `EvalFull` all route through one shared `Traverse`, so there is one traversal to be wrong rather than three. Adopted the **difference** sign convention (no `(-1)^party` factor) to match the spec's stated invariant. **Exhaustive correctness passes on the first run**: every alpha and every x to 10 domain bits on `u64` and 9 on `u128`, all x at sampled alpha to 12 bits, plus the edge cases (alpha at either end, beta=0, beta=-1, and beta=1 reconstructing exactly, which the PIR layer depends on). **Measured key sizes correct the design draft**: 245 B at n=4096, not the ~260 B claimed, because the draft's formula omitted the seed and header and evaluated at the wrong domain size. 134x smaller than a naive one-hot query at that size. Whole suite runs in 2.1 s. Wrote `report/sections/tech_dpf.tex` (report now 5 pages). Still unverified and left for day 3: `EvalFull` to 16 bits, `Eval`-vs-`EvalFull` agreement, and serialisation round-trips. |
| 2026-09-06 | Mainak | **S1 sprint day 1, foundation.** Build system: hand-written `Makefile` for `mingw32-make` (cmake is not installed and installing it would breach the no-new-dependencies rule), with a `check-toolchain` guard that turns g++'s silent as/ld spawn failure into a loud one. Headers `span.hpp` (a C++17 stand-in for `std::span`), `ring.hpp` (`RingTraits` for `u64` and `u128`, ring width templated from day one), `fixedpoint.hpp`, `prg.hpp`. `src/common/aes.cpp`: AES-NI fixed-key MMO PRG plus a scalar AES-128 reference. `tests/test_aes.cpp` passes the FIPS-197 known-answer vector and 10k AES-NI-vs-scalar comparisons; `tests/test_ring.cpp` covers byte round-trips, two's-complement negatives and additive homomorphism on both rings. Python oracle `model/{data,mf,metrics}.py`: power iteration written to mirror the protocol structure rather than calling SVD, nDCG@20 = 0.4536 on ML-100K `u1`. **Finding: recommendation quality is flat in the iteration count** (`docs/finding-ell-vs-quality.md`, raw data in `bench/results/oracle_ell_sweep.jsonl`) — the SVD subspace angle falls five orders of magnitude from `ell=10` to `ell=400` while nDCG@20 moves 0.5%, which justifies running private training at `ell=10` for a fortyfold communication saving. Started `report/midterm_technical.tex` (4 pages so far). |

---

## Milestone contribution summaries

Filled in just before each submission, from the tables above.

### Milestone 1 — Literature survey (31 Aug 2026)

> **Backfilled 2026-09-03 from git history, not from live logging.** Only work with evidence in
> this repository is recorded below. All 16 commits to date are authored by Mainak, so that is the
> only row that can be filled from the repo. **The three blank rows are not a claim that nothing
> was done, they are a statement that nothing was recorded.** Each member should fill their own row
> before 12 September, since Milestone 2 is graded specifically on per-member contribution.

| Member | Track | Video slot | Report sections written | Other |
|---|---|---|---|---|
| Mainak Sarkar | Not formally assigned (acted across all four) | Part I, slides 1-9 | All 11 sections of `report/sections/` | Bibliography rebuilt from DBLP (66 entries, 0 unverified); 40 PDFs acquired and author-verified; `notes/evidence.md` tier system; deck source; narration script; forward-citation sweep (task 1.4) |
| Shrasti Dwivedi | *to be filled in by Shrasti* | Part II, slides 10-16 (per narration script) | | |
| Aditya Anand | *to be filled in by Aditya* | | | |
| Shravan Agrawal | *to be filled in by Shravan* | | | |

### Milestone 2 — Mid-term report (12 Sep 2026)

| Member | Components delivered | Lines of evidence (commits, tests, results) |
|---|---|---|
| Mainak Sarkar | | |
| Shrasti Dwivedi | | |
| Aditya Anand | | |
| Shravan Agrawal | | |

### Milestone 3 — Final (6 Nov 2026)

| Member | Components delivered | Report sections | Evaluation owned |
|---|---|---|---|
| Mainak Sarkar | | | |
| Shrasti Dwivedi | | | |
| Aditya Anand | | | |
| Shravan Agrawal | | | |
