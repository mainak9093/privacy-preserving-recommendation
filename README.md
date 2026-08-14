# OblivRec — Privacy-Preserving Recommendation

**CS670 · Cryptographic Techniques for Privacy Preservation · 2026-27 Semester I · IIT Kanpur**
Course project, topic (a): *Privacy-Preserving Recommendation Systems*

---

A recommendation service that returns your personalised top-*k* items **without any server
learning your consumption history, your query, or which items it returned.**

The client's profile is encoded as **(2,2)-Distributed Point Function** keys and split across
two non-colluding servers. Each server sees only a pseudorandom key — but the two servers'
outputs *add up* to exactly the recommendation scores the client wanted. A DPF key for a
catalogue of 4,096 items is about **260 bytes**, against the 32 KB a naive one-hot query would
cost. That gap is the whole reason the system is practical.

```
   client profile                two servers, non-colluding
   {Toy Story, Matrix, …}   ──►  each sees pseudorandom bytes  ──►  ⟨scores⟩
                                                                        │
                                                    oblivious top-k  ◄──┘
                                                            │
                                            top-k titles  ◄─┘
```

---

## Where to start

| Read this | For |
|---|---|
| **[REQUIREMENTS.md](REQUIREMENTS.md)** | What we are building, what we are deliberately *not* building, and how "done" is defined. **Start here.** |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | The technical design: DPF construction, the three protocol stages, the threat model, the repository layout. The source of truth — if code disagrees with it, one of the two is a bug. |
| **[PHASES.md](PHASES.md)** | The schedule, the three graded milestones, who owns what, and the risk register. |

---

## Milestones

| Milestone | Weight | Deadline |
|---|---|---|
| Literature survey — report + 30-minute recorded presentation | 10% | **31 August 2026** |
| Mid-term report — two pages, per-member contributions | 5% | **12 September 2026** |
| Final — complete source code, report, and demonstration | 15% | **6 November 2026** |

> **Every member must log their work in [`docs/contributions.md`](docs/contributions.md) after
> each session.** The mid-term report is explicitly a per-member contribution summary, and a
> viva may be called at any stage on any part of the project.

---

## Design in three stages

| Stage | What happens | Cost |
|---|---|---|
| **A — Private scoring** | Client sends DPF keys for its profile. Each server computes shares of `score = Σ r·S[i,·]` over the item–item similarity matrix. | 1 round |
| **B — Oblivious top-*k*** | The servers select the *k* highest scores from the secret-shared vector, with an access pattern that does not depend on the values. | interactive, batched |
| **C — Private delivery** | Client reconstructs the indices locally, then fetches the item metadata by DPF-PIR. | 1 round |

The model itself — an item–item similarity matrix over MovieLens — is trained **offline, in the
clear**. This project is about private *retrieval*, not private *training*; the reasoning is in
[REQUIREMENTS.md §3](REQUIREMENTS.md).

---

## Workstreams

| Stream | Scope |
|---|---|
| **W1** | Function-secret-sharing core: the DPF (AES-NI GGM tree, `Gen`/`Eval`/`EvalFull`) and the two-server PIR read layer. |
| **W2** | Offline model pipeline, fixed-point encoding, secure scoring, recommendation-quality evaluation. |
| **W3** | Oblivious top-*k* selection, comparators, oblivious swap, private metadata delivery. |
| **W4** | Benchmark harness, WAN emulation, baselines, threat model and leakage analysis. |

Members are assigned in Phase 0. Workstreams touch each other only through
`include/oblivrec/` — see [ARCHITECTURE.md §9](ARCHITECTURE.md).

---

## Layout

```
include/oblivrec/   public headers — the contract between workstreams
src/dpf/            W1  DPF: GGM tree, AES-NI PRG
src/pir/            W1  two-server read layer, Stage A aggregation
src/topk/           W3  comparators, selectors, oblivious swap
src/net/            transport framing and batching
src/apps/           client, server0, server1, demo CLI
model/              W2  Python offline pipeline and quality evaluation
bench/              W4  sweeps, netem profiles, figure generation
tests/              unit and end-to-end; the plaintext oracle
docs/               report sources, threat model, survey notes
```

---

## Build

Requires a C++17 compiler with AES-NI, CMake ≥ 3.20, and Python 3.11.
Benchmarks require Docker (network shaping via `tc netem` is not available natively on Windows).

```bash
git clone <repo-url> && cd privacy-preserving-recommendation
python scripts/fetch_data.py            # downloads MovieLens; data/ is not committed
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build
```

---

## Ground rules

- **The DPF is written by us**, from the Boyle–Gilboa–Ishai papers. Third-party crypto appears
  only under `third_party/`, and only as a benchmark baseline.
- **Security claims are stated exactly as implemented.** The base system is semi-honest and
  assumes the two servers do not collude. Every claim says so — including in the abstract.
- **No benchmark number ships without its network profile.** "40 ms" is meaningless;
  "40 ms on `wan_a` (30 ms RTT, 100 Mbps)" is a result.
- **Raw benchmark data is committed; figures are generated.** Every figure in the report comes
  out of `make figures`, never a hand edit.
- **Branch per workstream** (`w1/…`, `w2/…`), never commit to `main` directly. Changes to
  `include/oblivrec/` change the contract between members — flag them before merging.

---

## Status

Phase 0 — mobilisation. See [PHASES.md](PHASES.md).
