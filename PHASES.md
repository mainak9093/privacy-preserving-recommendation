# PHASES

**Derived directly from the CS670 First Course Handout (`FCH.pdf`).**
Three graded milestones, 30% of the course, ~12 weeks from today.

```
Aug 15 (Sat) ─── today, project kickoff
Aug 31 (Mon) ─── ▲ MILESTONE 1: Literature Survey — report + 30-min video     [10%]
Sep 12 (Sat) ─── ▲ MILESTONE 2: Mid-term report, per-member contributions     [ 5%]
Nov 06 (Fri) ─── ▲ MILESTONE 3: Source code + final report + demonstration    [15%]
```

**The structural fact that drives this plan:** the gap between Milestone 1 and Milestone 2
is **12 days**. You cannot read all of August and then start coding. The survey period and
the first implementation sprint must overlap — Phase 1 and Phase 2 run concurrently from
Aug 25.

**Module timing works in our favour.** By Aug 31 the course will have covered Module 0 and
most of Module 1 (PIR). Modules 2 (MPC) and 3 (Private Memory Access) land during our core
implementation window. Our design deliberately depends on Modules 1–3 and *not* on Module 4
(ZKP), which is taught too late to build on. Lectures feed the implementation instead of
lagging it.

---

## Phase 0 — Mobilisation · Aug 15 → Aug 20

Cheap, unglamorous, and everything downstream is blocked on it.

| # | Task | Owner | Done when |
|---|---|---|---|
| 0.1 | **Email TA Sonu Sharma to register the group** (4 names, roll numbers, topic (a)) | Lead | Confirmation received. **Do this first — groups that register late get leftover topics.** |
| 0.2 | Assign the four workstreams W1–W4 (REQUIREMENTS §10) and record in MEMORY.md | All | Table filled with real names |
| 0.3 | Everyone gets the repo cloned, C++17 toolchain + CMake + Python 3.11 building | All | `cmake --build build` succeeds on 4 machines |
| 0.4 | Book a recurring 45-min weekly sync; agree the shared channel | Lead | Calendar invite out |
| 0.5 | Download MovieLens-100K and 1M via `scripts/fetch_data.py` | W2 | `data/` populated, checksums match |
| 0.6 | Get MP-SPDZ compiling in Docker (de-risks the B3 baseline early) | W4 | Tutorial example runs |
| 0.7 | **Attend office hours (Sat 12:00, KD317)** with REQUIREMENTS §11 open questions | Lead + 1 | Answers logged in MEMORY.md |

**Exit criterion:** group registered, everyone builds, questions asked.

---

## Phase 1 — Literature Survey · Aug 15 → Aug 31 · **MILESTONE 1 (10%)**

> Handout: *"a literature survey report; and a 30-minute recorded presentation
> (approximately 7–8 minutes per member) … uploaded to YouTube as an unlisted video."*

**Treat this as the cheapest 10% in the course.** It is a known, bounded deliverable with no
technical risk. The only way to lose marks here is disorganisation.

### The narrative arc

Four members, four slots, **one deck, one story.** Do not let it become four disconnected
paper summaries — that is the single most common failure mode.

```
  W1: problem + threat model + the primitive     ─┐
  W2: what has been tried for recommendation      ├─  ends at: "…and here is the gap
  W3: why the algorithm is the hard part          │      we intend to fill."
  W4: how these systems are actually evaluated   ─┘
```

| Slot | Member | Content | Core reading |
|---|---|---|---|
| 1 (7–8 min) | **W1** | The problem, the threat model, and Function Secret Sharing as the enabling primitive. Why a `O(log n)`-size key changes what is possible. | BGI *Function Secret Sharing* (EUROCRYPT'15); BGI *Improvements and Extensions* (CCS'16); Evans–Kolesnikov–Rosulek ch. 3 |
| 2 (7–8 min) | **W2** | The recommendation landscape: PIRSONA's PIR + 4PC matrix factorisation; Nikolaenko et al.'s garbled-circuit MF; FHE/CKKS CF; federated + DP approaches. Why we scope to retrieval. | PIRSONA (PETS'21); Nikolaenko et al. (CCS'13); Shmueli–Tassa |
| 3 (7–8 min) | **W3** | *Data-dependent access is the enemy.* Private nearest-neighbour search; Path ORAM → Floram → Duoram → PRAC; why an index and obliviousness are in tension. | Path ORAM (CCS'13); Floram (CCS'17); Duoram (USENIX Sec'23); PRAC (PETS'24); Pacmann; Tiptoe (SOSP'23) |
| 4 (7–8 min) | **W4** | The PIR landscape (SimplePIR/DoublePIR, Spiral) as the alternative trust model; how this literature is evaluated; leakage beyond the protocol (PICS, Asharov et al.); **the gap we fill.** | SimplePIR (USENIX Sec'23); Spiral (S&P'22); PICS (eprint 2025/1071); Asharov et al. (PETS'18) |

### Tasks

| # | Task | Owner | Deadline |
|---|---|---|---|
| 1.1 | Read your slot's core papers, write a 1-page structured note each into `docs/survey/notes/` | Each | Aug 22 |
| 1.2 | **Implement a (2,2)-DPF from scratch, ~200 lines, one afternoon** | W1 | Aug 22 |
| 1.3 | Agree the deck outline and the single narrative arc | All | Aug 23 |
| 1.4 | Draft survey report (target 10–14 pages, IEEE two-column) | All, W4 edits | Aug 26 |
| 1.5 | Build the shared deck; consistent template, no per-member styling | W3 owns deck | Aug 27 |
| 1.6 | Dry run the full 30 min, timed, on a call. Cut anything that overruns | All | Aug 28 |
| 1.7 | Record. Good microphone, quiet room, one continuous take per member | All | Aug 29 |
| 1.8 | Upload **unlisted** to YouTube, verify the link works in a private window | Lead | Aug 30 |
| 1.9 | Submit report + link | Lead | **Aug 31** |

> **1.2 is not optional and it is not a Phase-2 task.** Implementing a DPF before you write
> the survey is the highest-leverage single action available right now: it costs one day and
> it makes every DPF-based paper in the course readable. It also means Milestone 1 ships with
> working code already in the repo, which sets the tone.

**Exit criterion:** report submitted, unlisted link verified, DPF passing its unit test.

---

## Phase 2 — Walking Skeleton · Aug 25 → Sep 12 · **MILESTONE 2 (5%)**

> Handout: *"a mid-term project report which is a two-page summary describing the
> contributions of each group member."*
>
> Individual accountability is baked into the grading. **From today, every member appends to
> their section of `docs/contributions.md` after each work session.** Reconstructing this
> from memory on Sep 11 is how groups lose these marks.

Overlaps Phase 1 by design. The goal is an **end-to-end system that is embarrassingly slow
and tiny but genuinely correct and genuinely private** — every stage present, nothing stubbed
on the critical path.

Target: `n = 1682` (MovieLens-100K), `m = 20`, `k = 10`, localhost, `SEL_SORT` + `CMP_GC`.

| # | Task | Owner | Done when |
|---|---|---|---|
| 2.1 | Harden the Phase-1 DPF: `EvalFull`, AES-NI PRG, serialisation | W1 | `tests/test_dpf.cpp` exhaustive to `d = 16` |
| 2.2 | Stage A aggregation with the fused `w_b · S` optimisation (ARCHITECTURE §4.3) | W1 | Shares of `score` reconstruct to the plaintext scores |
| 2.3 | Offline model pipeline; quantised `S.bin`, `M.bin`, `params.json` | W2 | SHA-256 identical on both servers |
| 2.4 | Quality eval: Recall@10 / NDCG@10, float vs quantised | W2 | Report written; gate passes |
| 2.5 | `Comparator` + `CMP_GC`; `SEL_SORT` bitonic selector; oblivious swap | W3 | Matches `CMP_PLAIN` oracle on random inputs |
| 2.6 | TCP framing, batching, `flush()` | W3 + W1 | Three processes talk |
| 2.7 | Stage C metadata retrieval | W3 | Client prints real movie titles |
| 2.8 | Benchmark harness skeleton, JSONL schema, `make figures` scaffold | W4 | One real figure generated end-to-end |
| 2.9 | B1 plaintext + B3 MP-SPDZ baselines running | W4 | Numbers in `bench/results/` |
| 2.10 | **First draft of the threat model** (ARCHITECTURE §7) | W4 | `docs/threat-model.md` exists |
| 2.11 | Write the two-page mid-term report | All | **Sep 12** |

**Exit criterion (the demo that defines this phase):** `./demo --profile "Toy Story, Star Wars,
Matrix"` returns sensible recommendations, and `tcpdump` on the server links shows nothing but
pseudorandom bytes. If you can show that, the project is real.

---

## Phase 3 — Core Implementation · Sep 13 → Oct 12

The long middle. Modules 2 (MPC) and 3 (Private Memory Access) are being taught right now —
use the lectures.

| # | Task | Owner |
|---|---|---|
| 3.1 | `SEL_TOURN` oblivious tournament selector, `O(n + k log n)` | W3 |
| 3.2 | AVX2 / cache-blocked matvec; multithread Stage A | W1 |
| 3.3 | Scale to MovieLens-1M (`n = 3706`) then synthetic `n = 16k` | W1 + W2 |
| 3.4 | Seen-item masking via signed DPF payloads (ARCHITECTURE §4.4) | W1 |
| 3.5 | Offline/online split: precompute multiplication triples, measure separately | W3 |
| 3.6 | `tc netem` profiles wired into Docker; full sweep runs unattended | W4 |
| 3.7 | **B4: single-server PIR baseline** (SimplePIR or Spiral) integrated | W4 |
| 3.8 | B5: naive linear-scan-under-MPC baseline for the crossover graph | W4 |
| 3.9 | **D7.2: `CMP_DPF`, Grotto-style DPF comparison** | W1 |
| 3.10 | Threat model → full leakage profile + real/ideal simulation sketch | W4 |
| 3.11 | Docker image + one-command reproducibility | W4 |

**Checkpoints.** Oct 1: all of B1–B4 measured on `local` and `wan_a`. Oct 12: feature freeze
on the base system — after this date, only stretch goals, evaluation, and writing.

**Exit criterion:** every "must ship" deliverable D1–D6 in REQUIREMENTS §5 is functional, and
the sweep runs unattended overnight.

---

## Phase 4 — Evaluation & Stretch · Oct 13 → Oct 27

Where a working system becomes a *result*.

| # | Task | Owner |
|---|---|---|
| 4.1 | Full sweep: `n × m × k × backend × selector × network profile`, repeated ≥5× | W4 |
| 4.2 | **The money graph:** latency vs `n`, linear-scan vs index-backed, per network profile, crossover marked | W4 + W3 |
| 4.3 | Microbenchmark breakdown table: PRG / matvec / network / serialisation, offline vs online | All |
| 4.4 | **D7.1: build the model-poisoning canary attack, quantify bits-per-query leaked** | W4 + W2 |
| 4.5 | **D7.1 defence:** Merkle commitment to `S` + random row audit; measure overhead | W4 + W1 |
| 4.6 | *(if time)* D7.3 DORAM-backed heap selector via Duoram/PRAC | W3 |
| 4.7 | *(if time)* D7.4 malicious-client DPF audit + DoS measurement | W1 |
| 4.8 | Reproducibility test: a member who did not build it follows the README on a clean VM | rotating |

**Priority under time pressure:** 4.1 → 4.2 → 4.3 → 4.4 → 4.5, then stop. A complete honest
evaluation beats a half-landed stretch goal. **Do not start 4.6/4.7 after Oct 22.**

**Exit criterion:** every figure in the final report is generated by `make figures` from
committed raw data.

---

## Phase 5 — Final Delivery · Oct 28 → Nov 6 · **MILESTONE 3 (15%)**

> Handout: *"the complete source code; the final project report; and a final presentation…
> may be conducted in person, online, or replaced by a 30-minute recorded presentation."*

| # | Task | Owner | Deadline |
|---|---|---|---|
| 5.1 | Final report draft, full structure (below) | All | Oct 30 |
| 5.2 | Code freeze. Only bug fixes and documentation after this point | All | **Nov 1** |
| 5.3 | README audited by a stranger-simulating teammate; Docker build from scratch verified | W4 | Nov 2 |
| 5.4 | Report internal review pass; every claim traced to a figure or a proof sketch | All | Nov 3 |
| 5.5 | Live demo rehearsed **with a recorded fallback video** | W2 + W3 | Nov 4 |
| 5.6 | **Viva prep: each member must explain every layer, not only their own** | All | Nov 4 |
| 5.7 | Submit source + report + presentation | Lead | **Nov 6** |

### Final report structure
1. Introduction and motivation
2. Threat model and security definitions ← *the section most groups omit*
3. Background: secret sharing, PIR, DPFs
4. System design (from ARCHITECTURE.md)
5. Implementation
6. Evaluation — baselines, WAN profiles, microbenchmarks, crossover
7. **Leakage beyond the protocol** — the D7.1 attack and defence
8. Related work
9. Limitations and future work ← *state the non-collusion assumption plainly, again*
10. Conclusion

**Exit criterion:** a stranger with Docker can clone the repo and reproduce Figure 1.

---

## What separates this from a median project

Restated here because it should be visible every time this file is opened. Across a large
number of course projects in this space, the median group produces: a wrapper around an
existing library on a toy dataset, a localhost-only demo, "we use AES-256 so it is secure",
benchmarks with no baseline, and a blockchain layer that adds nothing.

The five things that actually differentiate:

1. **A written threat model and an explicit leakage profile.** One page. Almost nobody does it.
2. **A real baseline, honestly measured.** "We are 340× slower than plaintext but 6× faster
   than the naive HE approach" is far more credible than a quiet omission.
3. **WAN benchmarking, not just localhost.** Results in this literature flip under network
   constraints, because round complexity starts to dominate bandwidth.
4. **A microbenchmark breakdown.** Where do the milliseconds go? This turns a demo into an
   engineering artefact.
5. **Reproducibility.** Docker, one command per figure, a README a stranger can follow.

All five are in the plan above. None of them require cryptographic novelty. They are the
cheapest marks in the project.

---

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Fixed-point / truncation errors change the top-*k* | Medium | High | Measured, not argued (ARCHITECTURE §4.3). `CMP_PLAIN` oracle in tests from Phase 2. Headroom analysis before, empirical check after. |
| Third-party research code (MP-SPDZ, Duoram, SimplePIR) fights the build | **High** | Medium | Dockerise every baseline in Phase 0/3, not Phase 4. Pin submodules. Budget a full day each. |
| Stage A `O(n²)` matvec too slow at `n = 64k` | Medium | Medium | Sparsified `S` (top-`s` neighbours) is the documented fallback; report it as a design point, not a failure. |
| Survey and skeleton sprints collide late August | High | Medium | Phase 1 and 2 are deliberately concurrent; 1.2 (the DPF) is scheduled *inside* Phase 1. |
| Member falls behind, discovered at the mid-term report | Medium | High | `docs/contributions.md` updated per session, reviewed at the weekly sync. |
| Scope drifts back toward private training | Medium | High | REQUIREMENTS §6 non-goals; any change requires editing that file first. |
| Viva exposes a member who only knows their own layer | Medium | High | Task 5.6, plus rotating "explain someone else's layer" at the weekly sync from Phase 3. |
