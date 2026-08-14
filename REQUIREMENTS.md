# REQUIREMENTS

**Project:** OblivRec — Private Top-*k* Recommendation over Secret-Shared Similarity Models
**Course:** CS670, Cryptographic Techniques for Privacy Preservation, 2026-27 Semester I, IIT Kanpur
**Instructor:** Adithya Vadapalli · **Registration TA:** Sonu Sharma
**Handout topic:** (a) Privacy-Preserving Recommendation Systems
**Group size:** 4 · **Project weight:** 30% of course grade

> This file is the contract. If something is not in here, it is out of scope until the
> team agrees to amend this file. See [PHASES.md](PHASES.md) for *when*, and
> [ARCHITECTURE.md](ARCHITECTURE.md) for *how*.

---

## 1. The one-sentence pitch

A recommendation service that returns your personalised top-*k* items **without any
server ever learning your consumption history, your query, or which items it returned**
— built on (2,2)-Distributed Point Functions, benchmarked honestly against plaintext,
generic MPC, and single-server PIR baselines, over emulated WAN links.

---

## 2. What the handout actually demands

Extracted from `FCH.pdf`, verbatim where it matters:

| Requirement | Handout wording | Consequence for us |
|---|---|---|
| Mandate | "design and **implement** a secure system" | Running code is mandatory. A survey + slides fails the bar. |
| Group | "groups of four members", register with TA by email | Work must split into 4 individually-defensible workstreams. |
| Lit survey (10%, Aug 31) | report **plus** 30-min recorded presentation, ~7–8 min/member, unlisted YouTube | 4 sub-topics, one narrative arc, one shared deck. |
| Mid-term (5%, Sep 12) | "two-page summary describing the contributions of each group member" | Individual accountability is graded. Keep a contribution log from day one. |
| Final (15%, Nov 6) | "the complete source code; the final project report; and a final presentation" | Repo must be self-contained and reproducible by a stranger. |
| Viva | "Any group may be asked to appear for an in-person viva during any stage" | Every member must be able to defend *every* layer, not just their own. |

**Module alignment.** The course runs Intro → PIR → MPC → Private Memory Access → ZKP →
Secure Systems. Our primitives (PIR, secret sharing, DPFs, oblivious selection) sit in
Modules 1–3, which are taught *before* our implementation deadlines. We deliberately
avoid a SNARK-centric design, since ZKPs (Module 4) arrive too late to be safely relied on.

---

## 3. Scope decision: retrieval, not training

The obvious framing of "privacy-preserving recommendation" is *private model training* —
running matrix factorisation under MPC. **We explicitly reject that scope.**

**Why we reject it.**
- It is the core of PIRSONA (Vadapalli et al., PETS 2021), i.e. the instructor's own paper.
  A half-finished 4PC Boolean matrix factorisation invites a direct, unflattering comparison.
- Fixed-point arithmetic under MPC (truncation, overflow, probabilistic truncation error)
  reliably consumes a week of debugging that produces nothing demonstrable.
- It drifts into being an ML project with a thin cryptographic veneer, which reads badly
  in a cryptography course.

**What we build instead.** The model is trained **offline, in the clear**, on public
MovieLens data. The project is the *private inference and private delivery* pipeline on
top of a fixed item–item similarity model. This is precisely the **private nearest-neighbour
search** problem, and it lands on the central lesson of Module 3: *an index is useful
because traversal is data-dependent, and data-dependent traversal is what oblivious
computation forbids.*

**This is a scope reduction, not a difficulty reduction.** The hard, interesting, gradeable
parts — DPF construction, oblivious top-*k*, leakage analysis, WAN behaviour — are all
retained.

---

## 4. Functionality specification

### 4.1 Parties

| Party | Holds | Trust |
|---|---|---|
| **Client** | private profile `P = {(i₁,r₁),…,(i_m,r_m)}` | The party whose privacy we protect. |
| **Server 0**, **Server 1** | replicated copy of the item–item similarity matrix `S` and the item metadata table `M` | Semi-honest, **assumed non-colluding**. |
| *(baseline only)* **Single server** | `S`, `M` | Semi-honest, no non-collusion assumption. Route A2. |

### 4.2 Ideal functionality `F_rec`

```
F_rec( client: P = {(i,r)} , servers: S ∈ Z^{n×n}, M ∈ ({0,1}^L)^n , k )
    score  ←  Σ_{(i,r) ∈ P}  r · S[i, ·]              ∈ Z^n
    score[j] ← −∞   for all j with (j,·) ∈ P          # never re-recommend seen items
    T      ←  indices of the k largest entries of score
    → Client:  T,  M[T]
    → Servers: ⊥
```

### 4.3 What the client learns
The *k* recommended item IDs and their metadata. Nothing about `S` beyond what those
`k` rows imply. (We do **not** attempt to hide the model from the client — that is a
separate problem and is explicitly out of scope, §6.)

### 4.4 What each server learns
Only the public parameters: `n`, `k`, the padded profile size `m̄`, the arrival time of
the query, and the number of queries. **Not** `P`, not `score`, not `T`. See
[ARCHITECTURE.md §7](ARCHITECTURE.md) for the full leakage profile — writing that profile
down honestly is itself a deliverable.

---

## 5. Deliverables

### D1 — Core system (must ship)
- **D1.1** `(2,2)`-DPF from scratch in C++ with AES-NI as the PRG. Key generation, single-point
  eval, and full-domain eval. **Written by us, not pulled from a library** — this is the
  pedagogically load-bearing component and the thing a viva will probe.
- **D1.2** Two-server DPF-PIR read layer over the similarity matrix, returning *additive
  secret shares* of the aggregated score vector in one round.
- **D1.3** Oblivious top-*k* selection over the secret-shared score vector, with the
  seen-item masking step.
- **D1.4** Private delivery: client-side reconstruction of `T`, then *k* independent
  DPF-PIR reads against the metadata table `M`.
- **D1.5** A working end-to-end demo: three processes (client, server 0, server 1), a real
  MovieLens model, recommendations that a human can eyeball as sensible.

### D2 — Offline model pipeline (must ship)
- Item–item cosine / shrunk-Pearson similarity from MovieLens-1M, in the clear, in Python.
- Fixed-point quantisation of `S` to `Z_{2^ℓ}` with a documented scale factor.
- **Quality report:** Recall@k and NDCG@k of the quantised model vs. the float model. If
  privacy costs us recommendation quality, we state by how much.

### D3 — Baselines (must ship, at least B1–B3)
| ID | Baseline | Purpose |
|---|---|---|
| B1 | Plaintext recommender | The speed-of-light reference. Report our slowdown factor without flinching. |
| B2 | Full-database download | Trivially private, absurd bandwidth. The other extreme. |
| B3 | Generic MPC circuit (MP-SPDZ) | The "just use a framework" strawman that most groups stop at. |
| B4 | Single-server PIR (SimplePIR / Spiral) | Removes the non-collusion assumption. The honest cost of doing so. |
| B5 | Naive linear-scan top-*k* under MPC | The pre-index version, for the crossover graph. |

### D4 — Evaluation (must ship)
- **Network profiles** via `tc netem`: `localhost`, `LAN (1ms/1Gbps)`, `WAN-A (30ms/100Mbps)`,
  `WAN-B (100ms/10Mbps)`. Nearly every result in this literature flips under network
  constraints because round complexity starts to dominate bandwidth. Showing this is a
  direct signal we read the papers properly.
- **Sweeps:** `n ∈ {1k, 4k, 16k, 64k}`, `m ∈ {10, 50, 200}`, `k ∈ {5, 10, 20}`.
- **Metrics:** end-to-end latency, client bandwidth ↑/↓, server CPU per query, offline
  preprocessing size, and recommendation quality.
- **Microbenchmark breakdown:** where do the milliseconds go — PRG, network, serialisation,
  selection? Offline vs online split.
- **The money graph:** latency vs `n` for linear-scan top-*k* against index-backed top-*k*,
  one line per network profile, with the crossover point marked.

### D5 — Security analysis (must ship)
- Written threat model: adversary classes (semi-honest, malicious client, malicious server,
  colluding subsets) and exactly which guarantee survives each.
- Explicit leakage profile — what is provably hidden, what is leaked (access-pattern lengths,
  set sizes, timing, round counts).
- Real/ideal simulation sketch for the semi-honest two-server case.

### D6 — Reproducibility (must ship)
- Docker image, one command to build.
- `make figures` regenerates every figure in the report from raw benchmark data.
- README a stranger can follow. This is cheap and almost nobody does it.

### D7 — Stretch, in priority order (ship if time allows)
- **D7.1 — Model-poisoning leakage attack (the novelty candidate).** A *malicious* server
  can craft `S` so that the returned top-*k* acts as a canary encoding whether the client's
  profile contains a target item; the client's subsequent click leaks that bit back. This is
  the PICS input-inconsistency idea (eprint 2025/1071) transplanted from contact discovery
  to recommendation. **Demonstrate the attack, quantify bits extracted per query, then
  implement a defence** (public Merkle commitment to `S` + client-side random audit) and
  measure its overhead. An attack we demonstrate working is worth more than a protocol we
  merely implement.
- **D7.2 — Grotto-style DPF comparison** replacing bit-decomposition in the top-*k* comparator.
- **D7.3 — DORAM-backed index** (Duoram / PRAC as the memory substrate) instead of a linear scan.
- **D7.4 — Malicious-client audit** for malformed DPF keys, Sabre-style, with the DoS
  measurement.
- **D7.5 — Private incremental updates** to the profile from user feedback.

---

## 6. Explicit non-goals

Stating these protects us in the viva and stops scope creep.

1. **No private model training.** `S` is trained in the clear. §3.
2. **No model confidentiality.** The client is allowed to learn about `S`. Protecting the
   server's model from the client is a different problem (model extraction) and is out of scope.
3. **No blockchain.** It would add engineering and zero cryptographic content.
4. **No SNARKs / ZKPs on the critical path.** Module 4 arrives too late. D7.1's Merkle
   commitment is a hash commitment, not a SNARK.
5. **No differential privacy as the headline mechanism.** DP may appear as a discussion point
   in D5, not as the protocol.
6. **No mobile client.** Desktop processes only.
7. **No malicious-security claim we have not implemented.** If we only have semi-honest, we
   say semi-honest, everywhere, in bold.
8. **No real user data.** MovieLens is public and licensed for research; nothing else is used.

---

## 7. Success criteria

### Minimum acceptable (this is a pass)
- [ ] DPF passes correctness tests against a brute-force point function on `n ≤ 2^16`.
- [ ] End-to-end demo produces the **same top-*k* as the plaintext recommender** on
      MovieLens-1M for ≥ 99% of test profiles (mismatches explained by quantisation ties).
- [ ] Servers demonstrably see no profile information (logged transcript inspection).
- [ ] Benchmarks against B1 and B3 on at least two network profiles.
- [ ] Threat model and leakage profile written.

### Target (this is a strong project)
- [ ] All of the above, plus B4 (single-server PIR) measured.
- [ ] Crossover graph produced and explained.
- [ ] Sub-second online latency for `n = 4k, m = 50, k = 10` on the LAN profile.
- [ ] Microbenchmark breakdown table.
- [ ] Docker + `make figures` reproducibility verified on a clean machine by a team member
      who did not build it.

### Reach (this is a distinctive project)
- [ ] D7.1 attack demonstrated with a quantified leakage rate, plus a working defence.
- [ ] D7.2 or D7.3 landed with a measured speedup.

---

## 8. Data

| Dataset | Size | Use |
|---|---|---|
| MovieLens-100K | 943 × 1,682 | Development, unit tests, fast iteration. |
| MovieLens-1M | 6,040 × 3,706 | Headline results and the demo. |
| MovieLens-25M | 162k × 59k | Scale sweep only (`n = 64k` point). |
| Synthetic | parameterised | Controlled `n`-sweeps and the crossover graph. |

Datasets are **not committed**. `scripts/fetch_data.py` downloads them; `data/` is gitignored.

---

## 9. Technology choices

| Layer | Choice | Rationale |
|---|---|---|
| Crypto core, protocol, servers | **C++17**, CMake | AES-NI intrinsics; matches the Duoram / PRAC / Grotto ecosystem we build on. |
| Offline model, evaluation, plots | **Python 3.11** (numpy, pandas, matplotlib) | Fast iteration where performance does not matter. |
| Transport | plain TCP, length-prefixed frames | No hidden costs to explain away in benchmarks. |
| Network emulation | `tc netem` in Docker | Standard in this literature. |
| Baselines | MP-SPDZ (Docker), SimplePIR (Rust) | Published, credible, not chosen to flatter us. |
| Repro | Docker + Make | One command per figure. |

---

## 10. Workstream split (4 members)

Each stream is individually defensible in a viva and produces its own section of the report.
Names are assigned in [MEMORY.md](MEMORY.md); this file defines the *streams*, not the people.

| Stream | Owns | Lit-survey slot (7–8 min) |
|---|---|---|
| **W1 — FSS core** | D1.1, D1.2, D7.2. The DPF, AES-NI PRG, GGM tree, full-domain eval, PIR read layer. | DPFs and Function Secret Sharing: BGI'15, BGI'16, and why logarithmic keys change everything. |
| **W2 — Model & secure scoring** | D2, the fixed-point encoding, the secret-shared aggregation, quality evaluation. | Collaborative filtering under privacy: PIRSONA, Nikolaenko et al. CCS'13, and the fixed-point problem. |
| **W3 — Oblivious selection & delivery** | D1.3, D1.4, D7.3. Sorting networks, oblivious heaps, masking, metadata retrieval. | Data-dependent access is the enemy: ORAM, Floram, Duoram, PRAC, and private nearest-neighbour search. |
| **W4 — Evaluation & security analysis** | D3, D4, D5, D6, D7.1. Harness, WAN emulation, baselines, threat model, the attack. | Threat models, leakage, and what "secure" actually buys you: the PIR landscape and the limits of the ideal functionality. |

**Shared obligation:** every member reads the DPF papers and can explain the top-*k* protocol.
The viva is individual and can cover any layer.

---

## 11. Open questions to resolve at office hours

Log answers in [MEMORY.md](MEMORY.md) as they arrive.

1. Is the retrieval-only scope (§3) acceptable, or is private training expected?
2. How much weight does the report carry for algorithmic / mechanism-design framing versus
   pure cryptographic content? (Assume crypto is the spine until told otherwise.)
3. Is the two-server non-collusion assumption acceptable as the headline model, with
   single-server PIR as a measured baseline?
4. Would a demonstrated attack + defence (D7.1) count as a stronger contribution than a
   marginal speedup? (We believe yes; confirm.)
