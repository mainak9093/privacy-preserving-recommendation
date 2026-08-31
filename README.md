# OblivRec — Private Matrix Factorization with Private Delivery

**CS670 · Cryptographic Techniques for Privacy Preservation · 2026-27 Semester I · IIT Kanpur**
Course project, topic (a): *Privacy-Preserving Recommendation Systems*

**Mainak Sarkar · Shrasti Dwivedi · Aditya Anand · Shravan Agrawal**

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

## Where we are: the literature survey

**No implementation yet, deliberately.** Milestone 1 (31 August) is a literature survey report
plus a 30-minute recorded presentation. The protocol design drafted before the survey has been
demoted to [`design/ARCHITECTURE-draft-v1.md`](design/ARCHITECTURE-draft-v1.md) and its empty
source tree parked under [`archive/scaffold-2026-08-15/`](archive/scaffold-2026-08-15/), because
two findings put it in doubt:

- **Nudge ships a complete MIT-licensed reference implementation**
  ([NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs)) — so
  reimplementing its training core may be wasted effort.
- **`B` is public and each user holds their own ratings**, so at MovieLens scale (~800 KB) a
  user can download `B` and compute top-*k* locally with no cryptography. What genuinely
  remains open is the *fetch* — exactly what Nudge delegates to "other means".

Both are settled at the instructor meeting. Until then, we read.

## Where to start

| Read this | For |
|---|---|
| **[report/](report/)** | **The live work.** The survey's LaTeX skeleton, the reading tracks, the bibliography, and per-paper notes. Start at [`report/README.md`](report/README.md). |
| **[report/notes/_synthesis.md](report/notes/_synthesis.md)** | The argument the survey is built to make — and the list of things that would kill it. |
| **[REQUIREMENTS.md](REQUIREMENTS.md)** | Scope, the instructor's steer, the gap, and the explicit non-goals. Note the status banner: the deliverable list is provisional. |
| **[PHASES.md](PHASES.md)** | Schedule, the three graded milestones, and the risk register. |
| **[references/](references/)** | The two base papers. **Read both in full before anything else.** |
| [design/ARCHITECTURE-draft-v1.md](design/ARCHITECTURE-draft-v1.md) | The drafted protocol design. **Superseded pending the survey** — do not implement from it. |

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

<!-- ---

## Workstreams

| Stream | Owner | Scope |
|---|---|---|
| **W1** | Mainak Sarkar | FSS core and private delivery: the DPF (AES-NI GGM tree), the FSS comparison gates, the DPF-PIR read layer. |
| **W2** | Aditya Anand | 3PC substrate and non-linear protocols: replicated sharing, the PRF model, `Trunc_t`, `ApproxNormalize`. |
| **W3** | Shrasti Dwivedi | Factorization and serving: power iteration, `SetOrthogonal`, score computation, oblivious top-*k*, model quality. |
| **W4** | Shravan Agrawal | Evaluation and security analysis: benchmark harness, WAN emulation, baselines, threat model, leakage analysis. |

Workstreams touch each other only through `include/oblivrec/`.
`dpf.hpp` and `nonlinear.hpp` are frozen in week one — they are everyone's critical path.

--- -->

## Staging

The system is two halves and 12 weeks is tight, so they are built to be separable:

**S1** serving + delivery (on a cleartext-trained model) → **S2** private training →
**S3** the composition and its evaluation.

S1 ships regardless and demos early; its DPF is a prerequisite for S2's non-linear gates anyway.

---

## Layout

```
report/             THE LIVE WORK — survey.tex, midterm.tex, final.tex,
                    one shared preamble.tex and references.bib,
                    sections/, notes/, figures/, slides/
references/         the two instructor-recommended papers
design/             the drafted protocol design (superseded, see above)
archive/            parked scaffolding, nothing deleted
docs/               contribution log (required by the mid-term report)
scripts/            fetch_data.py — MovieLens downloader, checksum-verified
data/               downloaded datasets (gitignored)
```

---

## Build

The survey needs only a LaTeX toolchain (TinyTeX is enough):

```bash
git clone <repo-url> && cd privacy-preserving-recommendation/report
latexmk -pdf survey.tex
```

Data, for later:

```bash
python scripts/fetch_data.py    # MovieLens-100K and 1M; data/ is not committed
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

**Phase 0/1 — mobilisation and literature survey.** Next actions, in order:

1. **Book the instructor meeting** he offered. Questions Q1 and Q2 in
   [REQUIREMENTS.md §11](REQUIREMENTS.md) unblock the architecture.
2. **Everyone reads both base papers end to end**, then tracks are assigned at the kickoff.
3. **Run the forward-citation sweep** (PHASES task 1.4) before drafting — the survey's central
   claim depends on it.

See [PHASES.md](PHASES.md).
