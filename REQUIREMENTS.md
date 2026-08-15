# REQUIREMENTS

**Project:** OblivRec — Private Matrix Factorization with Private Delivery
**Course:** CS670, Cryptographic Techniques for Privacy Preservation, 2026-27 Semester I, IIT Kanpur
**Instructor:** Adithya Vadapalli · **Registration TA:** Sonu Sharma
**Handout topic:** (a) Privacy-Preserving Recommendation Systems
**Group size:** 4 · **Project weight:** 30% of course grade

> ## ⚠ STATUS: §1–§3 and §6 stand · §4–§5 and §9–§10 are PROVISIONAL
>
> **The project is in its literature survey phase** (Milestone 1, 31 August). The scope,
> the instructor's steer, and the non-goals below are settled. **The deliverable list (§4)
> and the workstream split (§10) are not** — they are superseded pending two things:
>
> 1. **The survey itself.** See [`report/`](report/) and
>    [`report/notes/_synthesis.md`](report/notes/_synthesis.md).
> 2. **The instructor meeting**, which he offered on 15 August. Two questions decide the
>    architecture: whether to build on [NUDGE]'s MIT-licensed reference artifact
>    ([NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs)) or
>    reimplement its training core, and whether the serving layer needs cryptography at
>    all in our parameter regime.
>
> **Why §4/§5 are in doubt:** [NUDGE] publishes the item embedding matrix `B` in the clear
> and each user holds their own ratings, so at MovieLens scale (~800 KB) a user can simply
> download `B` and compute top-*k* locally with no cryptography. The oblivious-top-*k*
> machinery this file specifies may be solving a problem that does not exist at our scale.
> Where the crossover lies is now research question **N1** in the survey.
>
> The detailed protocol design has been demoted to
> [`design/ARCHITECTURE-draft-v1.md`](design/ARCHITECTURE-draft-v1.md) and its source tree
> parked under [`archive/scaffold-2026-08-15/`](archive/scaffold-2026-08-15/).

> This file is the contract. If something is not in here, it is out of scope until the
> team agrees to amend this file. See [PHASES.md](PHASES.md) for *when*, and
> [`design/ARCHITECTURE-draft-v1.md`](design/ARCHITECTURE-draft-v1.md) for the *how* as
> currently drafted.

---

## 1. The one-sentence pitch

A recommender that **trains on secret-shared ratings across three non-colluding servers** and
then **delivers the recommended content without any server learning which item was fetched** —
composing the training core of *Nudge* (USENIX Security 2026) with the PIR-based delivery layer
that *Nudge* explicitly leaves to "other means" and that *PIRSONA* (PoPETs 2021) built by hand
on a far more expensive 4PC factorization.

---

## 2. The instructor's steer

On 2026-08-15 the instructor replied to our topic query with two papers and an offer to meet:

> *"For this topic, you will need to implement a privacy preservation recommendation system.
> There a couple of research papers I'll suggest:*
> - *https://petsymposium.org/popets/2021/popets-2021-0059.php*
> - *https://www.usenix.org/conference/usenixsecurity26/presentation/henzinger*
>
> *We can have a detailed conversation next week if you like."*

Both PDFs are in [`references/`](references/):

| Ref | Paper | What it is |
|---|---|---|
| **[PIRSONA]** | Vadapalli, Bayatbabolghani, Henry. *You May Also Like… Privacy: Recommendation Systems Meet PIR.* PoPETs 2021(4):30–53. **The instructor's own paper.** | Collaborative filtering **on top of** PIR. Users fetch records via Hafiz–Henry computationally 1-private multiserver PIR; the servers harvest secret-shared consumption histories *directly out of the incoming PIR queries*, then periodically run a bespoke **4PC Boolean matrix factorization** to refresh the model. |
| **[NUDGE]** | Henzinger, Dauterman, Corrigan-Gibbs, Boneh. *Nudge: A Private Recommendations Engine.* USENIX Security 2026. | Private matrix factorization at scale. **3 servers**, 2-out-of-3 replicated secret sharing, semi-honest, tolerates compromise of one. Replaces gradient descent with **power iteration**, cast as a *matrix-vector program*: the matrix–vector steps are non-interactive under replicated sharing, and only the non-linear steps (truncation, normalization) need interaction — implemented with **function secret sharing**. Netflix scale (0.5M users) in 50 min on 3×192-core; nDCG@20 = 0.29 vs 0.31 for non-private neural recommenders. |

**This reverses the scoping decision recorded here before the instructor's reply.** The earlier
draft scoped *away* from private training and toward retrieval only. Both recommended papers are
centrally about private **training** — so training is in scope, and it is the technical core.

**The gap the two papers leave between them, which is our project.**

- [NUDGE] §3.1 Non-goals and §1 Limitations: *"Nudge's goal is to map user ratings into
  personalized recommendations; it relies on **other means** (e.g., Apple's private relay, Tor,
  or cryptographic private information retrieval) to let users fetch data items in a private way."*
  Nudge trains privately and serves *scores* privately — but the moment the user fetches the
  recommended film, the fetch itself betrays the recommendation.
- [PIRSONA] closes exactly that loop with PIR, but its training core is a 4PC Boolean matrix
  factorization that Nudge outperforms by a wide margin (Nudge reports running faster than the
  bespoke four-party protocol "while using 8× less communication and one fewer non-colluding party").

**So: build PIRSONA's end-to-end shape on Nudge's training core.** Neither paper does both well.
We are not trying to beat either — we are *composing* them and being the first to measure and
analyse the composition honestly.

**Why this is safe ground.** Function secret sharing is the load-bearing primitive on *both*
halves — Nudge's non-linear gates and our PIR delivery layer are the same machinery. One DPF
implementation serves the whole system. This is also the spine of the course (Modules 1–3) and
the instructor's own research area.

---

## 3. Functionality specification

### 3.1 Parties

| Party | Holds | Trust |
|---|---|---|
| **Users** `i ∈ [m]` | private rating vector `u⁽ⁱ⁾ ∈ R^n` | The parties whose privacy we protect. |
| **Servers** `P₀, P₁, P₂` | 2-out-of-3 replicated shares of the rating matrix `U`; the item catalogue `D`; item metadata | Semi-honest, honest majority. **At most one may be compromised.** Run by independent parties. |

### 3.2 Ideal functionality

**Training** (periodic, batched over all users):
```
F_train( ⟦U⟧ ∈ R^{m×n},  d,  ℓ )
    (A, B) ← ApproxFactor(U)            # rank-d power iteration, ℓ iterations
    → each server:  B ∈ R^{d×n}  IN THE CLEAR   (item embeddings)
    → each server:  ⟦A⟧ ∈ R^{m×d}  secret-shared (user embeddings)
```

**Serving + delivery** (per user, online):
```
F_serve( ⟦a⁽ⁱ⁾⟧, B, k )
    scores  ←  a⁽ⁱ⁾ · B                 ∈ R^n
    scores[j] ← −∞  for j already rated by i
    T       ←  top-k indices of scores
    → user:    T,  and the CONTENT records D[T]
    → servers: ⊥                        # in particular, NOT T
```

The second line of `F_serve`'s output is our addition. Nudge stops at `scores`; the user then
has to fetch `D[T]` somehow, and that fetch is unprotected.

### 3.3 What each server learns
The public parameters `m, n, d, ℓ, k`; **the item embedding matrix `B` in the clear** (this is
by design in Nudge — it is what makes power iteration cheap, and it is a real leakage surface we
must analyse, §6.3); the number of non-zero entries in each user's rating vector, but not their
locations or values; and message timing. **Not** the ratings, not `A`, not the scores, not `T`.

---

## 4. Deliverables

### D1 — 3PC substrate (must ship) · W2
2-out-of-3 replicated secret sharing over `Z_{2^b}`, PRF-based correlated randomness (the "3PC
with PRF" model), pairwise channels, and the **non-interactive** replicated matrix–vector product.
Degree-two functions in one round with `3db` bits of communication.

### D2 — FSS / DPF core (must ship) · W1
`(2,2)`-DPF from scratch in C++ with AES-NI, per Boyle–Gilboa–Ishai. `Gen`, `Eval`, `EvalFull`.
Used **twice** in this system: as the zero-test / integer-comparison gate inside D3, and as the
PIR read layer in D5. **Written by us, not pulled from a library** — it is the pedagogically
load-bearing component and the thing a viva will probe hardest.

### D3 — 3PC non-linear protocols (must ship) · W2
- **`Trunc_t`** — deterministic fixed-point truncation with sign extension, via integer comparison
  to fix low-order carries plus one bit of slack for high-order carries. 3 rounds.
- **`ApproxNormalize`** — L2 normalization, i.e. shares of `1/‖v‖`, seeded by the
  most-significant-non-zero-bit trick and refined with Newton–Raphson. `O(1)` rounds.

These two are the entire interactive cost of training. Everything else is free.

### D4 — Private matrix factorization (must ship) · W3
`ApproxFactor` by power iteration ([NUDGE] Fig. 4): for each of `d` components, `ℓ` rounds of
`v := Mul(Uᵀ, Mul(U, v))`, `SetOrthogonal(v, B)`, `Normalize(v)`; each converged `v` becomes a row
of `B` **and is revealed in the clear**; finally `A := U · Bᵀ`.

### D5 — Private serving and private delivery (must ship) · W1 + W3
**This is our contribution over [NUDGE].**
- **D5.1** Score computation `⟦scores⟧ = ⟦a⁽ⁱ⁾⟧ · B`, seen-item masking, oblivious top-*k*
  selection over the shared score vector, shares sent to the user.
- **D5.2** **Private delivery:** the user reconstructs `T` locally and fetches the actual content
  records `D[T]` by DPF-PIR against the replicated catalogue, so no server learns which items were
  recommended or consumed. Fixed-width records; variable width leaks through response size.
- **D5.3** The [PIRSONA] loop: the servers harvest the *next* round's secret-shared consumption
  history directly from these delivery queries, feeding D4 without any separate ratings upload.

### D6 — Baselines and evaluation (must ship) · W4
| ID | Baseline | Purpose |
|---|---|---|
| B1 | Cleartext power-iteration MF + cleartext fetch | Speed-of-light reference and the **quality oracle**. |
| B2 | Cleartext MF + PIR delivery | Isolates the cost of private delivery alone. |
| B3 | Private MF + cleartext fetch (i.e. Nudge's own scope) | Isolates the cost of private training alone. |
| B4 | Generic MPC framework (MP-SPDZ) doing the same factorization | The "just use a framework" strawman most groups stop at. |
| B5 | Full-catalogue download | Trivially private delivery, absurd bandwidth. The other extreme. |

B1–B3 are the important ones: **the composition is the project, so the cost of each half
separately is the result.**

Metrics: wall-clock per training round, server-to-server bytes, per-user serving latency and
bandwidth, and **recommendation quality (nDCG@20, Recall@k)** against B1. Network profiles via
`tc netem`: `local`, `lan (1ms/1Gbps)`, `wan_a (30ms/100Mbps)`, `wan_b (100ms/10Mbps)`.
Sweeps over `m`, `n`, `d`, `ℓ`, `b`, `k`. Microbenchmark breakdown: matvec vs truncation vs
normalization vs FSS vs network.

### D7 — Security analysis (must ship) · W4
Threat model; explicit leakage profile of the **composed** system; a real/ideal simulation sketch;
and §6.3 below — the analysis of what the cleartext `B` gives an adversary. Neither reference does
this for the composition, because neither does both halves.

### D8 — Reproducibility (must ship) · W4
Docker, one command to build, `make figures` regenerates every figure from committed raw data,
README a stranger can follow.

### D9 — Stretch, in priority order
- **D9.1** Ring-width study: `b = 64` vs `b = 128`. [NUDGE] uses `b = 128` because `m` is large;
  at MovieLens scale `b = 64` may suffice and is 2× cheaper. **Find empirically where 64 breaks.**
  Cheap to do, genuinely novel, and a clean result.
- **D9.2** Differential privacy on `B` ([NUDGE] §9) and the resulting quality cost.
- **D9.3** Input validation — the well-formedness check on user submissions ([NUDGE] §3.1), so one
  malicious user cannot skew the model.
- **D9.4** Malicious-client DPF audit (Sabre-style) and the DoS measurement on `EvalFull`.
- **D9.5** Leakage of the composition across repeated rounds: with `B` public and consumption
  histories harvested from delivery queries, what does a server learn over many training rounds?

---

## 5. Staging and the fallback

This is a two-halved system and **12 weeks is tight**. The halves are deliberately separable.

| Stage | Contains | If we run out of time |
|---|---|---|
| **S1 — Serving + delivery** (D2, D5) | DPF, PIR delivery, oblivious top-*k*, on a model trained *in the clear* | **Ships regardless.** A complete, demonstrable, benchmarkable system on its own. |
| **S2 — Private training** (D1, D3, D4) | 3PC substrate, truncation, normalization, power iteration | Degrade `d`, `ℓ`, and dataset size before dropping it. Report honestly what scale we reached. |
| **S3 — The composition** (D5.3, D6 B1–B3, D7) | End-to-end loop and the comparative evaluation | The headline result. |

**Build order is S1 → S2 → S3**, because S1 is lower-risk, produces a demo early, and its DPF is a
prerequisite for S2's non-linear gates anyway. If S2 stalls, we still have a working private
delivery system plus a rigorous account of why the training half is hard — which is a respectable
project, just not a distinctive one.

---

## 6. Explicit non-goals

Stating these protects us in the viva and stops scope creep.

1. **No malicious-server security.** [NUDGE] does not claim it either. Semi-honest, honest
   majority, at most one compromised server. Stated in bold everywhere, including the abstract.
2. **No security against 2+ colluding servers.** Replicated sharing reconstructs.
3. **No metadata hiding.** Servers learn when each user sends messages.
4. **No blockchain, no SNARKs on the critical path.** Module 4 arrives too late to depend on.
5. **No attempt to beat either reference paper.** We compose them and measure. Trying to
   out-engineer a USENIX Security paper in ten weeks is a losing bet.
6. **No Netflix-scale claims.** [NUDGE] used 3×192-core machines. We will run MovieLens-1M and
   report exactly the scale we reached.
7. **No neural recommender.** Matrix factorization only, as in both references.
8. **No real user data.** MovieLens is public and licensed for research.

### 6.3 The one thing we must not hand-wave

`B` — the item embedding matrix — is **public** in Nudge's design. That is what makes power
iteration cheap. But it means every server holds a complete latent-factor model of the catalogue.
Combined with anything that leaks a user's fetches, `B` turns a single observed item into a strong
prior over that user's whole taste vector. **This is precisely why the private delivery layer (D5.2)
matters, and articulating that is the strongest argument our report can make.** Quantify it: given
public `B` and one observed fetch, how much of `a⁽ⁱ⁾` can be reconstructed?

---

## 7. Success criteria

### Minimum acceptable (a pass)
- [ ] DPF passes exhaustive correctness tests against a brute-force point function for `d ≤ 16`.
- [ ] `Trunc_t` and `ApproxNormalize` match cleartext fixed-point within a documented error bound.
- [ ] End-to-end demo on MovieLens-100K: private training completes, private delivery returns real
      film titles, and `tcpdump` on the server links shows nothing but pseudorandom bytes.
- [ ] nDCG@20 within a stated margin of the cleartext oracle B1.
- [ ] Threat model and leakage profile written.

### Target (a strong project)
- [ ] MovieLens-1M, `d ≥ 16`, private training end to end.
- [ ] B1, B2, B3 all measured on `local` and `wan_a` — the cost of each half isolated.
- [ ] Microbenchmark breakdown; the interactive cost attributed to truncation vs normalization.
- [ ] Docker + `make figures` verified on a clean machine by a member who did not build it.

### Reach (a distinctive project)
- [ ] D9.1 ring-width crossover found and explained.
- [ ] D9.5 or §6.3 quantified — a real leakage result about the composition.

---

## 8. Data

| Dataset | Scale | Use |
|---|---|---|
| MovieLens-100K | 943 × 1,682 | Development, unit tests, fast iteration. |
| MovieLens-1M | 6,040 × 3,706 | **Headline results and the demo.** |
| MovieLens-25M | 162k × 59k | Scale sweep only, if S2 is comfortable. |
| Synthetic | parameterised | Controlled sweeps and the ring-width study. |

Not committed. `scripts/fetch_data.py` downloads them; `data/` is gitignored.

---

## 9. Technology choices

| Layer | Choice | Rationale |
|---|---|---|
| Crypto core, 3PC, servers | **C++17**, CMake | AES-NI; `__int128` for the `b = 128` ring; matches the Duoram/PRAC/Grotto ecosystem. |
| Offline eval, plots, quality | **Python 3.11** | Fast iteration where performance does not matter. |
| Transport | plain TCP, length-prefixed frames | No hidden costs to explain away in benchmarks. |
| Network emulation | `tc netem` in Docker | Standard in this literature. |
| Baselines | MP-SPDZ (Docker) | Published, credible, not chosen to flatter us. |

---

## 10. Workstream split

Each stream is individually defensible in a viva and produces its own report section.
People are assigned in [MEMORY.md](MEMORY.md) §4.

> **Ownership is deliberately unassigned.** Streams are named so the work can be talked
> about; who takes which is decided at the kickoff, after the survey has clarified what the
> implementation actually involves. The implementation streams below are **provisional** for
> the same reason §4 is.

**Survey tracks** (live now — see [`report/README.md`](report/README.md)):

| Track | Survey sections | Video slot (7–8 min) |
|---|---|---|
| **T1 — Primitives** | §3 | Function secret sharing: BGI'15/'16, DPFs, PIR, and why one primitive serves both halves of the problem. |
| **T2 — Private training** | §4 | Matrix factorization under MPC: Nikolaenko → PIRSONA → Nudge, and why power iteration beats gradient descent. |
| **T3 — Private retrieval** | §5 | Private nearest-neighbour search: SANNS → Tiptoe → Pacmann/Compass → Wally/Panther. |
| **T4 — Threat models, systems, the gap** | §2, §6, §7, §8 | What "private" means, who has attempted both halves, how this field measures itself, and the gap we fill. |

**Implementation streams** (provisional, start after Milestone 1):

| Stream | Owns |
|---|---|
| **W1 — FSS core & private delivery** | D2, D5.1, D5.2, D9.4 |
| **W2 — 3PC substrate & non-linear protocols** | D1, D3, D9.1 |
| **W3 — Factorization & serving** | D4, D5.1, D5.3, D9.2 |
| **W4 — Evaluation & security analysis** | D6, D7, D8, D9.5 |

**Shared obligation:** every member reads [PIRSONA] and [NUDGE] in full and can explain the whole
pipeline. The viva is individual and can cover any layer.

---

## 11. Open questions for the instructor

He offered a detailed conversation — take it, and take these. **Q1 and Q2 are the ones that
unblock the architecture; everything else can wait.**

1. **Is the composition framing right?** Nudge's training core + PIRSONA's PIR delivery, measured
   end to end. Or does he want a straight reimplementation of one of the two?
2. **[NUDGE] ships a complete MIT-licensed reference implementation**
   ([NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs): Go, AVX2/AES-NI,
   `dcf/`, `dmsb/`, `multdpf/`, full 3PC protocol, phase benchmarks). **Build on it, or
   reimplement the training core ourselves?** Building on it removes our largest schedule risk
   and puts all of our own code on the delivery layer — the part Nudge leaves open.
   Reimplementing gives a stronger viva story at real risk to the November deadline.
3. **Does the serving layer need cryptography at all in our regime?** `B` is public and the user
   holds its own ratings, so at MovieLens scale the user can download `B` (~800 KB) and compute
   top-*k* locally. Is mapping that crossover (research question N1) a contribution he values?
4. Is the public-`B` reconstruction analysis (§6.3, research question N2) worth pursuing?
5. Is MovieLens-1M an acceptable scale given Nudge used 3×192-core machines for Netflix?
6. Is the S1→S2→S3 staging acceptable, i.e. is a strong S1 with a partial S2 a reasonable landing
   zone if the schedule bites?
7. Confirm 3PC honest-majority semi-honest is the right model to target rather than PIRSONA's 4PC.
