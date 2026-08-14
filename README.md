# OblivRec — Private Matrix Factorization with Private Delivery

**CS670 · Cryptographic Techniques for Privacy Preservation · 2026-27 Semester I · IIT Kanpur**
Course project, topic (a): *Privacy-Preserving Recommendation Systems*

**Mainak Sarkar · Shrasti Dwivedi · Anushka Gupta · Aditya Anand**

---

A recommender that **trains on secret-shared ratings across three non-colluding servers**, and
then **delivers the recommended content without any server learning which item was fetched.**

```
   users' ratings ──► ⟦secret-shared⟧ ──► 3 servers, at most one compromised
                                               │
                            power iteration under MPC (matrix-vector work is FREE)
                                               │
                      item embeddings B (public)  +  user embeddings ⟦A⟧ (shared)
                                               │
                            ⟦scores⟧ = ⟦a⟧·B ──► oblivious top-k ──► ⟦T⟧
                                               │
                                  DPF-PIR fetch of the actual content
                                               │
                          the server never learns what you watched
```

## Why this shape

The project sits on two papers the instructor recommended, both in [`references/`](references/):

- **[NUDGE]** *(Henzinger, Dauterman, Corrigan-Gibbs, Boneh — USENIX Security 2026)* trains a
  recommender privately at scale. Its trick: under 2-of-3 replicated secret sharing, multiplying a
  shared matrix by a shared vector is **non-interactive**, so recasting matrix factorization as
  *power iteration* makes nearly all of the work free — only truncation and normalization cost
  rounds. But Nudge states plainly that it *"relies on other means … to let users fetch data items
  in a private way."* It stops at the scores.
- **[PIRSONA]** *(Vadapalli, Bayatbabolghani, Henry — PoPETs 2021)* closes that loop with PIR, and
  even harvests the next round's training data straight out of the users' PIR queries — but its
  training core is a 4PC Boolean matrix factorization that Nudge now outperforms with one fewer
  non-colluding party.

**Neither paper does both halves well. We compose them and measure the composition** — which
nobody has done, because nobody has built both. See [REQUIREMENTS.md §2](REQUIREMENTS.md).

Function secret sharing is the load-bearing primitive on *both* sides: it is the comparison gate
inside Nudge's truncation and normalization, and it is the PIR read layer for delivery. One DPF
implementation, written from scratch, serves the whole system.

---

## Where to start

| Read this | For |
|---|---|
| **[REQUIREMENTS.md](REQUIREMENTS.md)** | What we build, the instructor's steer, the gap we fill, deliverables, and the explicit non-goals. **Start here.** |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | The technical design: replicated sharing, truncation and normalization, power iteration, the DPF, the PIR delivery layer, the threat model. The source of truth. |
| **[PHASES.md](PHASES.md)** | Schedule, the three graded milestones, who owns what, and the risk register. |
| **[references/](references/)** | The two papers. **Read both in full before writing any code.** |

---

## Milestones

| Milestone | Weight | Deadline |
|---|---|---|
| Literature survey — report + 30-minute recorded presentation | 10% | **31 August 2026** |
| Mid-term report — two pages, per-member contributions | 5% | **12 September 2026** |
| Final — complete source code, report, and demonstration | 15% | **6 November 2026** |

> **Log your work in [`docs/contributions.md`](docs/contributions.md) after every session.** The
> mid-term report is explicitly a per-member contribution summary, and a viva may be called at any
> stage on any part of the project.

---

## Workstreams

| Stream | Owner | Scope |
|---|---|---|
| **W1** | Mainak Sarkar | FSS core and private delivery: the DPF (AES-NI GGM tree), the FSS comparison gates, the DPF-PIR read layer. |
| **W2** | Aditya Anand | 3PC substrate and non-linear protocols: replicated sharing, the PRF model, `Trunc_t`, `ApproxNormalize`. |
| **W3** | Shrasti Dwivedi | Factorization and serving: power iteration, `SetOrthogonal`, score computation, oblivious top-*k*, model quality. |
| **W4** | Anushka Gupta | Evaluation and security analysis: benchmark harness, WAN emulation, baselines, threat model, leakage analysis. |

Workstreams touch each other only through `include/oblivrec/`.
`dpf.hpp` and `nonlinear.hpp` are frozen in week one — they are everyone's critical path.

---

## Staging

The system is two halves and 12 weeks is tight, so they are built to be separable:

**S1** serving + delivery (on a cleartext-trained model) → **S2** private training →
**S3** the composition and its evaluation.

S1 ships regardless and demos early; its DPF is a prerequisite for S2's non-linear gates anyway.

---

## Layout

```
include/oblivrec/   public headers — the contract between workstreams
src/dpf/            W1  DPF: GGM tree, AES-NI          ── used by BOTH halves
src/pir/            W1  DPF-PIR delivery, consumption harvesting
src/mpc/            W2  replicated sharing, PRF setup, matrix-vector programs
src/mpc/nonlinear/  W2  Trunc_t, ApproxNormalize, FSS compare / zero-test
src/mf/             W3  power iteration, SetOrthogonal, ApproxFactor
src/serve/          W3  scores, seen-item masking, oblivious top-k
src/apps/           server0/1/2, user client, demo CLI
model/              cleartext oracle and quality evaluation (Python)
bench/              W4  sweeps, netem profiles, figure generation
tests/              unit and end-to-end; cleartext oracles
references/         the two instructor-recommended papers
```

---

## Build

Requires a C++17 compiler with AES-NI, CMake ≥ 3.20, and Python 3.11.
Benchmarks require Docker — `tc netem` is not available natively on Windows.

```bash
git clone <repo-url> && cd privacy-preserving-recommendation
python scripts/fetch_data.py            # downloads MovieLens; data/ is not committed
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build
```

---

## Ground rules

- **The DPF is written by us**, from the Boyle–Gilboa–Ishai papers. Third-party crypto appears only
  under `third_party/`, and only as a benchmark baseline.
- **Security claims are stated exactly as implemented.** The system is semi-honest with an honest
  majority and tolerates **at most one** compromised server. Two colluding servers break it. That
  appears in the abstract, not buried in a limitations section.
- **Approximate protocols ship with measured error bounds.** `Trunc_t` and `ApproxNormalize` are
  tested against cleartext oracles; an unmeasured approximation is not finished.
- **No benchmark number ships without its network profile.** "40 ms" is meaningless;
  "40 ms on `wan_a` (30 ms RTT, 100 Mbps)" is a result.
- **Raw benchmark data is committed; figures are generated.** Every figure comes out of
  `make figures`, never a hand edit.
- **Branch per workstream** (`w1/…`, `w2/…`), never commit to `main` directly. Changes to
  `include/oblivrec/` change the contract between members — flag them before merging.

---

## Status

Phase 0 — mobilisation. See [PHASES.md](PHASES.md).
