# PHASES

**Derived from the CS670 First Course Handout (`FCH.pdf`), revised 2026-08-15 after the
instructor recommended [PIRSONA] and [NUDGE] (see [REQUIREMENTS.md §2](REQUIREMENTS.md)).**

```
Aug 15 (Sat) ─── today · kickoff · instructor's paper recommendations received
Aug 31 (Mon) ─── ▲ MILESTONE 1: Literature Survey — report + 30-min video     [10%]
Sep 12 (Sat) ─── ▲ MILESTONE 2: Mid-term report, per-member contributions     [ 5%]
Nov 06 (Fri) ─── ▲ MILESTONE 3: Source code + final report + demonstration    [15%]
```

**The structural fact that drives this plan:** the gap between Milestone 1 and Milestone 2 is
**12 days**. You cannot read all of August and then start coding. Phases 1 and 2 run concurrently
from Aug 25.

**The second structural fact:** the project is now two halves — private *training* and private
*delivery* — and 12 weeks is tight for both. [REQUIREMENTS.md §5](REQUIREMENTS.md) defines the
staging **S1 (serving + delivery) → S2 (training) → S3 (composition)** and the fallback if S2
stalls. Build in that order; S1 gives a demo early and its DPF is a prerequisite for S2 anyway.

> **On ownership.** `T1`–`T4` are the four *survey tracks* (live now); `W1`–`W4` are the four
> *implementation streams* (provisional, Phase 2 onward). **Neither is assigned to a person
> yet** — that happens at the kickoff, and the implementation streams may not survive the
> instructor meeting intact. Labels exist so the work can be discussed, not to allocate it.

**Module timing works in our favour.** By Aug 31 the course will have covered Module 0 and most of
Module 1 (PIR). Module 2 (MPC) and Module 3 (Private Memory Access) land during our core
implementation window. The design depends on Modules 1–3 and **not** on Module 4 (ZKP), which is
taught too late to build on.

---

## Phase 0 — Mobilisation · Aug 15 → Aug 21

> **No code in this phase.** The project is in its survey phase; the implementation scaffold
> has been parked under [`archive/scaffold-2026-08-15/`](archive/scaffold-2026-08-15/) and the
> protocol design demoted to [`design/ARCHITECTURE-draft-v1.md`](design/ARCHITECTURE-draft-v1.md)
> until the survey and the instructor meeting settle the architecture.

| # | Task | Done when |
|---|---|---|
| 0.1 | ~~Register the group with TA Sonu Sharma~~ | ✅ **done 2026-08-15** |
| 0.2 | **Reply to the instructor and book the meeting he offered.** Take [REQUIREMENTS.md §11](REQUIREMENTS.md) — Q1 (composition framing) and Q2 (build on the Nudge artifact or reimplement) are the two that unblock everything | Meeting scheduled |
| 0.3 | **Everyone reads [PIRSONA] and [NUDGE] end to end.** Not skimmed — the viva is individual | Discussed at the kickoff |
| 0.4 | Assign the four survey tracks T1–T4 ([`report/README.md`](report/README.md)) | Agreed at kickoff |
| 0.5 | Weekly 45-min sync booked; shared channel agreed | Invite out |
| 0.6 | `report/` builds on everyone's machine — `latexmk -pdf survey.tex` | 4 machines, 4 PDFs |
| 0.7 | MovieLens-100K and 1M fetched (`python scripts/fetch_data.py`) | `data/` populated, checksums match |

**Exit criterion:** meeting booked, both base papers read by everyone, tracks assigned, the
report skeleton building.

---

## Phase 1 — Literature Survey · Aug 15 → Aug 31 · **MILESTONE 1 (10%)**

> Handout: *"a literature survey report; and a 30-minute recorded presentation (approximately
> 7–8 minutes per member) … uploaded to YouTube as an unlisted video."*

**The cheapest 10% in the course.** Bounded, no technical risk. The only way to lose marks is
disorganisation.

### The narrative arc

Four members, four slots, **one deck, one story.** Do not let it become four disconnected paper
summaries — that is the most common failure mode. The arc writes itself from our framing:

```
  W1: the primitive — function secret sharing, and why one DPF serves both halves
  W2: the substrate — replicated sharing, and why matrix-vector work is free
  W3: the algorithm — why power iteration beats gradient descent under MPC
  W4: the gap      — what neither paper does: the composition, and its leakage
                     └─► "…and that is what we intend to build."
```

| Slot | Track | Content | Core reading |
|---|---|---|---|
| 1 | **T1** | Problem, threat model, and FSS/DPFs as the enabling primitive. `O(log n)`-size keys — 260 bytes against 32 KB. The same DPF is a comparison gate *and* a PIR read layer. | BGI (EUROCRYPT'15); BGI (CCS'16); **the July 2026 DPF/FSS survey, arXiv 2607.27696**; Araki et al. (CCS'16) |
| 2 | **T2** | Private training: matrix factorization, Nikolaenko et al.'s garbled circuits, [PIRSONA]'s 4PC Boolean MF, [NUDGE]'s power iteration. **Why the algorithm choice is a cryptographic decision.** | [PIRSONA] §3–4; [NUDGE] §4–5; Nikolaenko et al. (CCS'13) |
| 3 | **T3** | Private retrieval — the literature that grew up in parallel and has barely been cited by the recommendation line. Linear scan → sublinear → relaxed guarantees. | SANNS; Tiptoe (SOSP'23); **Pacmann**; Compass (NSDI'24); **Wally**; Panther (CCS'25); P²RAG |
| 4 | **T4** | What "private" means; how this literature is evaluated; **the gap we fill** — [NUDGE] delegates fetching to "other means", [PIRSONA]'s training core is superseded, and nobody has connected the two. | [NUDGE] §3.1 non-goals and §9; [PIRSONA] §1–2; SimplePIR; Spiral; PrivateRec; PICS; Asharov et al. |

### Tasks

| # | Task | Deadline |
|---|---|---|
| 1.1 | Seed [`report/references.bib`](report/references.bib); scaffold `report/` | ✅ Aug 15 |
| 1.2 | 1-page structured note per paper into [`report/notes/`](report/notes/), using `_template.md` | Aug 22 |
| 1.3 | **Synthesis meeting** — agree the gap statement in `notes/_synthesis.md` and the single narrative arc. **Nothing gets written before this** | Aug 23 |
| 1.4 | **Forward-citation sweep of [NUDGE], Tiptoe and Pacmann.** If someone has already composed a private-ANN backend with a private-MF recommender, we must know now | Aug 23 |
| 1.5 | Draft sections into `report/sections/` | Aug 26 |
| 1.6 | Assemble full draft; one person edits the whole thing for a single voice | Aug 27 |
| 1.7 | Build the shared deck; one template, no per-slot styling | Aug 28 |
| 1.8 | Timed dry run of the full 30 min. Cut anything that overruns | Aug 29 |
| 1.9 | Record — good microphone, quiet room, one take per member | Aug 29 |
| 1.10 | Upload **unlisted** to YouTube; verify in a private window | Aug 30 |
| 1.11 | Submit report + link | **Aug 31** |

> **Task 1.4 is the one that can sink the survey.** The central claim of §6 — that the
> intersection of the two literatures is nearly empty — is falsifiable, and finding a
> counterexample in October rather than August would be expensive. Do it before drafting.

> **No DPF implementation in this phase.** An earlier version of this plan scheduled one here.
> It is deferred: whether we write our own FSS core at all depends on the Nudge-artifact
> decision (REQUIREMENTS §11 Q2), which the instructor meeting settles.

**Exit criterion:** report submitted, unlisted link verified, no `[UNVERIFIED]` bib entry cited.

---

## Phase 2 — S1: Serving and Delivery · Aug 25 → Sep 12 · **MILESTONE 2 (5%)**

> Handout: *"a mid-term project report which is a two-page summary describing the contributions of
> each group member."*
>
> **From today, every member appends to their section of `docs/contributions.md` after each work
> session.** Reconstructing this on Sep 11 is how groups lose these marks.

Overlaps Phase 1 by design. Goal: **the serving half working end to end against a model trained in
the clear.** Embarrassingly small, genuinely correct, genuinely private.

Target: MovieLens-100K, `n = 1682`, `d = 16`, `k = 10`, localhost, `SEL_SORT`.

| # | Task | Owner | Done when |
|---|---|---|---|
| 2.1 | Harden the Phase-1 DPF: `EvalFull`, AES-NI PRG, serialisation, ring template | W1 | `tests/test_dpf.cpp` exhaustive to `d = 16` |
| 2.2 | FSS zero-test and integer-comparison gates on top of the DPF | W1 | Matches cleartext oracle |
| 2.3 | Replicated 2-of-3 sharing, PRF setup, three-process wiring | W2 | Three servers reconstruct a shared value |
| 2.4 | Cleartext power-iteration MF in Python — **the quality oracle** | W3 | nDCG@20 reported on ML-100K |
| 2.5 | Score computation `⟦a⟧·B`, seen-item masking | W3 | Shares reconstruct to oracle scores |
| 2.6 | `SEL_SORT` oblivious top-*k* + oblivious swap | W3 | Matches plaintext top-*k* on random inputs |
| 2.7 | DPF-PIR read layer, fixed-width records | W1 | Client prints real film titles |
| 2.8 | TCP framing, batching, `flush()` | W1 + W2 | Three processes talk under load |
| 2.9 | Benchmark harness, JSONL schema, `make figures` scaffold | W4 | One real figure end to end |
| 2.10 | B1 cleartext + B5 full-download baselines | W4 | Numbers in `bench/results/` |
| 2.11 | **First draft of the threat model** (ARCHITECTURE §9) | W4 | `docs/threat-model.md` exists |
| 2.12 | Two-page mid-term report | All | **Sep 12** |

**Exit criterion — the demo that defines this phase:** `./demo --user 42` returns sensible film
recommendations *and fetches the records*, while `tcpdump` on the server links shows nothing but
pseudorandom bytes.

---

## Phase 3 — S2: Private Training · Sep 13 → Oct 12

The long middle, and the hard half. Modules 2 (MPC) and 3 are being taught right now — use them.

| # | Task | Owner |
|---|---|---|
| 3.1 | Non-interactive replicated matrix–vector product; the `MatVecProgram` abstraction | W2 |
| 3.2 | **`Trunc_t`** — the improved 3-round protocol; benchmark against the naive variant | W2 |
| 3.3 | **`ApproxNormalize`** — MSNZB seeding via simultaneous FSS comparisons + Newton–Raphson | W2 |
| 3.4 | `SetOrthogonal` (Gram–Schmidt against the public rows of `B`) | W3 |
| 3.5 | **`ApproxFactor`** — full power iteration, `d` components × `ℓ` rounds | W3 |
| 3.6 | Deferred-truncation schedule; derive and assert the headroom bound at startup | W2 + W3 |
| 3.7 | Convergence study: `ℓ` vs quality against the cleartext oracle | W3 |
| 3.8 | Scale to MovieLens-1M (`m = 6040, n = 3706, d = 32`) | All |
| 3.9 | **[PIRSONA] loop:** harvest shared consumption histories from the delivery queries | W1 + W3 |
| 3.10 | `tc netem` profiles wired into Docker; sweeps run unattended | W4 |
| 3.11 | B3 (private MF, cleartext fetch) and B4 (MP-SPDZ) baselines | W4 |
| 3.12 | Threat model → full leakage profile + real/ideal simulation sketch | W4 |
| 3.13 | Docker image + one-command reproducibility | W4 |

**Checkpoints.** Oct 1: `ApproxFactor` completes on ML-100K and quality is within a stated margin of
the oracle. Oct 12: **feature freeze** on the base system — after this, only stretch, evaluation,
and writing.

**Escalation rule.** If `ApproxFactor` is not converging on ML-100K by **Oct 1**, stop adding scope:
drop to `d = 8`, shorten `ℓ`, and spend the remaining time on evaluation and the §9.3 leakage
analysis. A rigorous partial S2 with an honest account beats a broken full one.

---

## Phase 4 — S3: Composition & Evaluation · Oct 13 → Oct 27

Where two working halves become a *result*.

| # | Task | Owner |
|---|---|---|
| 4.1 | Full sweep: `m × n × d × ℓ × b × k × network profile`, repeated ≥5× | W4 |
| 4.2 | **The headline comparison: B1 vs B2 vs B3 vs full system** — the cost of each half, isolated | W4 |
| 4.3 | Microbenchmark breakdown: matvec / truncate / normalize / FSS / topk / PIR / network | All |
| 4.4 | **D9.1 ring-width study: where does `b = 64` break?** Cheap, novel, clean result | W2 |
| 4.5 | **§9.3 leakage analysis: reconstruct `â⁽ⁱ⁾` from public `B` + `j` observed fetches** | W4 |
| 4.6 | *(if time)* D9.2 differential privacy on `B` and its quality cost | W3 |
| 4.7 | *(if time)* D9.3 input validation; D9.4 malicious-client DPF audit | W1 |
| 4.8 | Reproducibility test: a member who did not build it follows the README on a clean VM | rotating |

**Priority under time pressure:** 4.1 → 4.2 → 4.3 → 4.5 → 4.4, then stop. A complete honest
evaluation beats a half-landed stretch goal. **Do not start 4.6/4.7 after Oct 22.**

**Exit criterion:** every figure in the report is generated by `make figures` from committed raw data.

---

## Phase 5 — Final Delivery · Oct 28 → Nov 6 · **MILESTONE 3 (15%)**

> Handout: *"the complete source code; the final project report; and a final presentation… may be
> conducted in person, online, or replaced by a 30-minute recorded presentation."*

| # | Task | Owner | Deadline |
|---|---|---|---|
| 5.1 | Final report draft, full structure (below) | All | Oct 30 |
| 5.2 | **Code freeze.** Bug fixes and documentation only after this | All | **Nov 1** |
| 5.3 | README audited by a stranger-simulating teammate; Docker build from scratch verified | W4 | Nov 2 |
| 5.4 | Review pass: every claim traced to a figure, a measurement, or a proof sketch | All | Nov 3 |
| 5.5 | Live demo rehearsed **with a recorded fallback video** | W1 + W3 | Nov 4 |
| 5.6 | **Viva prep: each member explains every layer, not only their own** | All | Nov 4 |
| 5.7 | Submit source + report + presentation | Mainak | **Nov 6** |

### Final report structure
1. Introduction and motivation
2. Threat model and security definitions ← *the section most groups omit*
3. Background: replicated secret sharing, FSS/DPFs, PIR, matrix factorization
4. System design (from ARCHITECTURE.md)
5. Implementation
6. Evaluation — B1/B2/B3 isolation, WAN profiles, microbenchmarks, ring-width study
7. **Leakage of the composition** — the public-`B` reconstruction result
8. Related work — [PIRSONA], [NUDGE], and what sits between them
9. Limitations ← *semi-honest, one compromised server, our actual scale, plainly*
10. Conclusion

**Exit criterion:** a stranger with Docker can clone the repo and reproduce Figure 1.

---

## What separates this from a median project

Restated because it should be visible every time this file is opened. The median group produces: a
wrapper around a library on a toy dataset, a localhost-only demo, "we use AES-256 so it is secure",
benchmarks with no baseline, and a blockchain layer that adds nothing.

1. **A written threat model and an explicit leakage profile.** One page. Almost nobody does it.
2. **Real baselines, honestly measured.** Here the baselines *are* the result: B1/B2/B3 isolate the
   cost of each half of the composition.
3. **WAN benchmarking, not just localhost.** Results in this literature flip under network
   constraints, because round complexity starts to dominate bandwidth.
4. **A microbenchmark breakdown.** Where do the milliseconds go? This turns a demo into an artefact.
5. **Reproducibility.** Docker, one command per figure, a README a stranger can follow.

None require cryptographic novelty. They are the cheapest marks in the project.

---

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **S2 (private training) does not converge in time** | **High** | High | This is the top risk. S1 ships independently ([REQUIREMENTS.md §5](REQUIREMENTS.md)); hard escalation checkpoint on **Oct 1**; degrade `d`, `ℓ`, dataset before dropping the half. |
| `ApproxNormalize` is the hardest protocol and blocks all of power iteration | High | High | W2 starts it in Phase 2, not Phase 3. Cleartext oracle first, then the FSS version. Newton–Raphson step count is a tunable, not a constant. |
| Fixed-point overflow at `b = 64` corrupts training silently | Medium | High | Derive and **assert** the headroom bound at startup; the `b=64` vs `b=128` study (D9.1) turns this risk into a result. |
| Scale expectations set by [NUDGE]'s 3×192-core Netflix run | Medium | Medium | State our hardware and scale up front, in the abstract. We are not claiming to match it. |
| Third-party code (MP-SPDZ) fights the build | High | Low | Dockerised in Phase 0, not Phase 4. Budget a day. |
| Survey and S1 sprints collide in late August | High | Medium | Phases 1 and 2 are deliberately concurrent; the DPF (1.2) is scheduled *inside* Phase 1. |
| Header churn between workstreams | Medium | High | Task 0.8 freezes `dpf.hpp` and `nonlinear.hpp` in week one. Changes need a heads-up before merge. |
| Member falls behind, discovered at the mid-term | Medium | High | `docs/contributions.md` per session, reviewed at the weekly sync. |
| Viva exposes a member who only knows their own layer | Medium | High | Task 5.6, plus a rotating "explain someone else's layer" slot at the weekly sync from Phase 3. |
