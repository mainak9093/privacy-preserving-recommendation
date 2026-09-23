# ARCHITECTURE — DRAFT v1 (superseded pending the literature survey)

**System:** OblivRec — Private Matrix Factorization with Private Delivery

> ## ⚠ STATUS: NOT THE SOURCE OF TRUTH
>
> **This document predates the literature survey and parts of it are known to be wrong.**
> It is kept because the reasoning is real and most of it will survive, but **do not
> implement from it** and do not cite it as settled. It moves back to the repo root, as the
> authoritative architecture, only after the survey's §8 and the instructor meeting.
>
> **Two things found on 2026-08-15, after this was written:**
>
> 1. **§5–§6 (oblivious top-*k* selection and serving) are probably unnecessary.** `B` is
>    published in the clear (§5, and it is [NUDGE]'s own design), and each user holds their own
>    ratings `u⁽ⁱ⁾`. At MovieLens scale `B` is `d×n` ≈ **800 KB** — the user downloads it once
>    and computes top-*k* locally with no cryptography at all. The entire `Comparator` /
>    `SEL_SORT` / `SEL_TOURN` apparatus in §5 solves a problem that may not exist in our
>    parameter regime. **The open question is where the crossover lies** as the catalogue grows
>    — that is now novelty candidate N1 in the survey plan, not a settled design.
> 2. **[NUDGE] ships a complete MIT-licensed reference implementation**
>    ([NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs): Go + AVX2/AES-NI,
>    `dcf/`, `dmsb/`, `multdpf/`, full 3PC protocol, phase benchmarks). Whether §3–§5 get
>    reimplemented by us or built upon is **deferred to the instructor meeting**.
>
> **What still stands:** §7 (private delivery via DPF-PIR) is the part [NUDGE] explicitly
> delegates to "other means", and is the clearest candidate for our actual contribution.
> §9 (threat model and leakage), especially **§9.3 (public-`B` reconstruction)**, stands and
> is now novelty candidate N2.
>
> Original purpose statement, for the record: *the single source of truth for how the system
> is built; any code that contradicts it is a bug in one of the two.* That role is suspended
> until this document is promoted back.

**References**, both in [`references/`](references/):
- **[PIRSONA]** Vadapalli, Bayatbabolghani, Henry. *You May Also Like… Privacy: Recommendation Systems Meet PIR.* PoPETs 2021(4):30–53.
- **[NUDGE]** Henzinger, Dauterman, Corrigan-Gibbs, Boneh. *Nudge: A Private Recommendations Engine.* USENIX Security 2026.

---

## 1. System overview

Three servers, run by independent parties, semi-honest, at most one compromised.

```
 ┌── TRAINING (periodic, all users) ────────────────────────────────────────┐
 │                                                                          │
 │  users ──⟦u⁽ⁱ⁾⟧──►  P₀   P₁   P₂     2-of-3 replicated shares of U       │
 │                      │    │    │                                         │
 │                      └────┴────┘                                         │
 │              power iteration, ℓ rounds × d components                    │
 │                           │                                              │
 │        matvec: FREE (non-interactive under replicated sharing)           │
 │        Trunc_t + ApproxNormalize: the ONLY interactive cost, via FSS     │
 │                           │                                              │
 │                           ▼                                              │
 │            B ∈ R^{d×n}  IN THE CLEAR   (item embeddings)                 │
 │            ⟦A⟧ ∈ R^{m×d}  secret-shared (user embeddings)                │
 └──────────────────────────────────────────────────────────────────────────┘
                             │
 ┌── SERVING + DELIVERY (per user, online) ─────────────────────────────────┐
 │                                                                          │
 │   ⟦scores⟧ = ⟦a⁽ⁱ⁾⟧ · B      ──►  mask seen items  ──►  oblivious top-k  │
 │                                                              │           │
 │                                              ⟦T⟧ shares  ────┘           │
 │                                                    │                     │
 │                                                    ▼                     │
 │                                            user reconstructs T           │
 │                                                    │                     │
 │            ◄── DPF-PIR fetch of the CONTENT D[T] ──┘   ◄── OUR ADDITION  │
 │                                                                          │
 │      servers harvest next round's ⟦consumption⟧ from these queries       │
 │      ([PIRSONA]'s loop) ──────────────────────────────────► back to top  │
 └──────────────────────────────────────────────────────────────────────────┘
```

**The design in three sentences.** Under 2-of-3 replicated secret sharing, multiplying a shared
matrix by a shared vector is *non-interactive* — so if you express your algorithm as a sequence of
matrix–vector products separated by a few cheap non-linear steps, almost all of it is free. Power
iteration has exactly that shape, which is why [NUDGE] uses it instead of gradient descent. We
take that training core and bolt on the PIR delivery layer that [NUDGE] explicitly leaves to
"other means", closing the loop the way [PIRSONA] does.

---

## 2. Notation and parameters

| Symbol | Meaning | Default |
|---|---|---|
| `m` | users | 6,040 (ML-1M) |
| `n` | items | 3,706 (ML-1M) |
| `d` | embedding dimension | 16 (dev) → 32 (target); [NUDGE] uses 20 |
| `ℓ` | power-iteration inner rounds | 10 |
| `b` | ring bit-width, `R = Z_{2^b}` | **64** dev, **128** target — see D9.1 |
| `t` | fixed-point fractional bits | 20 (as [NUDGE]) |
| `λ` | PRF seed / DPF security parameter | 128 |
| `k` | recommendations returned | 10 |
| `⟦x⟧` | 2-of-3 replicated sharing: `(x₀,x₁),(x₁,x₂),(x₂,x₀)` with `x₀+x₁+x₂ = x` | |

**Ring width is a live research question, not a settled constant.** [NUDGE] uses `b = 128`
specifically because Netflix-scale `m` makes the accumulated sums overflow at 64 bits. At
MovieLens scale `b = 64` may be sufficient and is roughly 2× cheaper in communication. **Find
the crossover empirically** (D9.1) rather than assuming. `b = 128` uses `__int128`; the ring
type is a template parameter from day one so this study costs nothing later.

---

## 3. The 3PC substrate (W2, `src/mpc/`)

### 3.1 2-out-of-3 replicated secret sharing

`x ∈ R` is split as `x₀ + x₁ + x₂ = x`; party `i` holds the pair `(x_i, x_{i+1})`. Any two parties
reconstruct; any one learns nothing.

**"3PC with PRF" model.** Each *pair* of parties holds a shared PRF key, so they can generate
correlated randomness — in particular zero-shares `(r_i − r_{i+1})` — with **no communication**.
This is what makes the multiplication below cheap. Set up once at startup.

### 3.2 Why matrix–vector is free

For a degree-two function, each party can compute a share of the product *locally* from its two
shares, then re-randomize with a PRF zero-share and send **one** ring element to one neighbour.
The consequence stated in [NUDGE] Thm 4.2: for a matrix–vector program, **communication scales
only with the largest intermediate vector, not with the size of the input matrices.** Multiplying
an `n×n` shared matrix by a shared `n`-vector costs `O(n)` communication, not `O(n²)`.

This is the single most important fact in the system. Everything else in the design follows from
arranging the computation so that the expensive objects stay on the non-interactive side.

### 3.3 Matrix-vector program abstraction (`include/oblivrec/mvp.hpp`)

Per [NUDGE] Def 4.1, a program is `P(v, M₁…M_ℓ) := f_ℓ(M_ℓ · … · f₂(M₂ · f₁(M₁·v)))`.
We implement this abstraction directly, because it cleanly separates the free part from the
interactive part and makes the round count obvious by inspection:

```cpp
template <typename Ring>
class MatVecProgram {
  void push(const SharedMatrix<Ring>&, std::unique_ptr<NonLinear<Ring>>);
  SharedVec<Ring> run(const SharedVec<Ring>& v);   // rounds == number of NonLinear stages
};
```

---

## 4. Non-linear protocols (W2, `src/mpc/nonlinear/`)

The **entire** interactive cost of training lives here. Both are built on function secret sharing,
which evaluates zero-test and integer-comparison gates on shared inputs in a **single round**
([NUDGE] Table 3) — the same DPF machinery W1 builds for the PIR layer.

### 4.1 `Trunc_t` — fixed-point truncation

After every multiplication of two `t`-scaled fixed-point values the result is `2t`-scaled and must
be shifted back by `t`. Naive local shifting corrupts shares in two places, and both are fixed
cheaply:

1. **Low-order carries** — corrected with an integer comparison ([NUDGE] cites Escudero et al.).
2. **High-order carries** — prevented by one bit of slack, requiring input `v ∈ [−2^{b−2}, 2^{b−2}]`.

Cost: **3 rounds, `2dt·(λ+4) + 10db` bits** for a `d`-vector. Note the leading term is `λt`, not
`λb` — this is the [NUDGE] improvement (≈ `b/t` = 6× less communication than prior truncation at
`b=128, t=20`). Implement the improved version; benchmark it against the naive `2b·(λ+6)` variant,
because that comparison is a clean, self-contained result for the report.

### 4.2 `ApproxNormalize` — L2 normalization

Power iteration must renormalize `v` every step or it overflows. This needs shares of `1/‖v‖`,
which is the awkward one: inverse square root under MPC.

- `‖v‖²` is a degree-two function → **free**, one round.
- Seed: the most-significant-non-zero-bit of `‖v‖²` gives `2^{−⌊log‖v‖²⌋/2} ≈ 1/‖v‖`. Obtained via
  `b+1` **simultaneous** integer comparisons using FSS — one round, no extra leakage. This is the
  trick that avoids the `O(b)`-round or giant-lookup-table approaches.
- Refine: standard Newton–Raphson, a constant number of steps (each doubles the correct digits).

Cost: `O(1)` rounds, `O(db·λ + λb² + b³)` bits.

**Correctness discipline.** Both protocols are tested against a cleartext fixed-point oracle over
randomised inputs, with the observed error bound recorded. An approximate protocol whose error is
not measured is not finished.

---

## 5. Private matrix factorization (W3, `src/mf/`)

`ApproxFactor(U) → (A, B)`, per [NUDGE] Fig. 4:

```
B := 0 ∈ R^{d×n}
for i in 1..d:
    v := random n-vector
    v := Normalize(SetOrthogonal(v, B))
    for j in 1..ℓ:
        v := Mul(Uᵀ, Mul(U, v))       # free: two matrix-vector products
        v := SetOrthogonal(v, B)      # Gram-Schmidt against already-found rows
        v := Normalize(v)             # interactive
    B[i] := v                         # ← REVEALED IN THE CLEAR
A := U · Bᵀ
```

**The load-bearing design decision, and it is not ours.** Each converged row of `B` is *opened*
before the next component is computed. That is what keeps `SetOrthogonal` cheap — it is a
Gram–Schmidt step against *public* vectors — and it is why the whole thing is tractable. It also
means the item embedding model is public to all three servers. See §9.3.

**Deferred truncation.** Truncations are performed as "add-then-truncate" rather than
"truncate-then-add", and where `b` has slack we truncate by `2t` after every *other* multiplication
instead of `t` after each. Halves the truncation count; costs headroom. The safe schedule depends
on `b`, so it is derived, asserted at startup, and re-checked by the `b=64` vs `b=128` study.

**Convergence.** `ℓ = O(log(n/ε)/γ)` where `γ` is the eigenvalue gap. Do not assume a fixed `ℓ` is
enough — measure the residual against the cleartext oracle and report `ℓ` vs quality.

---

## 6. Serving (W3, `src/serve/`)

1. `⟦scores⟧ := ⟦a⁽ⁱ⁾⟧ · B`. `B` is public, so this is a **local** linear map — free.
2. **Seen-item masking.** `scores[j] := −∞` for items `i` already rated. The user supplies this as
   a shared mask so no server learns which items those are.
3. **Oblivious top-*k*** over `⟦scores⟧ ∈ R^n`. Data-independent instruction and network trace, by
   construction. Comparator is the same FSS integer comparison as §4.

| ID | Selector | Comparisons | Role |
|---|---|---|---|
| `SEL_SORT` | Batcher bitonic, take first `k` | `O(n log²n)` | Baseline, obviously oblivious. Ship first. |
| `SEL_TOURN` | Oblivious tournament | `O(n + k log n)` | Default. Small `k` is our regime. |

All comparisons at one level of the network are independent and **must be issued as a single
batch** — round count dominates on WAN. The `flush()` in the comparator interface exists for this.

Output: shares `⟦T⟧` of the top-*k* indices, sent to the user.

---

## 7. Private delivery (W1, `src/pir/`) — our addition over [NUDGE]

[NUDGE] stops at step 6. The user now holds `T` and must actually *fetch* the films — and a
cleartext fetch discards everything the previous two stages bought.

### 7.1 The (2,2)-DPF (`src/dpf/`, W1)

Boyle–Gilboa–Ishai GGM-tree construction, written by us.

- **PRG:** AES-128 fixed-key Davies–Meyer (`π(x) ⊕ x`) with AES-NI; one seed → two children plus
  two control bits.
- **Key size:** ~~`d·(λ+2) + b` bits. For `n = 4096`: **≈ 260 bytes**~~ **CORRECTED 2026-09-06
  against the implementation.** The old figure was wrong twice over: the formula omitted the
  initial seed and the self-describing header, and the 260-byte evaluation was done at 16 domain
  bits rather than the 12 that `n = 4096` implies. It also overloaded `d`, which §2 already uses
  for the embedding dimension, to mean tree depth.

  The packed format is `1 + 4 + 16 + 18·domain_bits + ⌈b/8⌉` bytes. **Measured** by
  `DpfKey::SizeBytes()`, which a test asserts equals the real serialised length:

  | domain bits | `n` | key, `b=64` | key, `b=128` | naive one-hot (`b=64`) | compression |
  |---:|---:|---:|---:|---:|---:|
  | 10 | 1,024 | 209 B | 217 B | 8,192 B | 39× |
  | 11 | 2,048 | 227 B | 235 B | 16,384 B | 72× |
  | 12 | 4,096 | **245 B** | 253 B | 32,768 B | **134×** |
  | 16 | 65,536 | 317 B | 325 B | 524,288 B | 1654× |

  The `32 KB` naive figure was correct. The compression factor is still the headline number, it is
  just 134× at `n = 4096` rather than the ~126× the old pair of numbers implied. The ML-100K PIR
  domain is `domain_bits = 11` (1682 items padded to 2048), so **227 bytes** is the figure that
  applies to this project's actual demo.
- **API** (`include/oblivrec/dpf.hpp`):
  ```cpp
  std::pair<DpfKey,DpfKey> Gen(uint32_t alpha, Ring beta, uint32_t domain_bits);
  Ring                     Eval(const DpfKey&, uint32_t x);
  void                     EvalFull(const DpfKey&, std::span<Ring> out);   // O(2^d), one pass
  ```
- **Invariant, tested exhaustively for `d ≤ 16`:**
  `EvalFull(k0)[x] − EvalFull(k1)[x] == (x == alpha ? beta : 0)`.

**This same DPF is the FSS gate in §4.** One implementation, two consumers. Do not fork it.

### 7.2 The read

The user reconstructs `T` locally, then issues `k` DPF-PIR reads against the replicated catalogue
`D`. Each server does one `EvalFull` and an inner product against `D`; the two answers
**differ** by `D[T_j]`.

> **Corrected 2026-09-08.** This line previously said the shares "sum to" the record. That is the
> textbook BGI convention, where `Eval_0 + Eval_1 = f(x)`. This project uses the **difference**
> convention throughout, fixed by §7.1's stated invariant and implemented in `dpf.hpp`, which omits
> the `(-1)^party` factor. The implementation and the exhaustive tests are right; the prose here had
> drifted. Reconstruction is
> `Σ_j (EvalFull(k0)[j] − EvalFull(k1)[j]) · D[j][w] = D[alpha][w]` for each ring word `w`.

**Records are fixed width** (`L = 256 B` for metadata; content records padded to a fixed block
count). Variable width leaks through response size — this is a *security* requirement, not a
convenience, and it goes in the report.

### 7.3 Closing the [PIRSONA] loop

[PIRSONA]'s idea: the servers extract secret-shared consumption histories **directly from the
incoming PIR queries** — the query already encodes which item, in shared form, so the ratings for
the next training round come for free with no separate upload step. We implement this, and it is
what makes the system a genuine cycle rather than two bolted-together halves.

---

## 8. Repository layout

```
include/oblivrec/   public headers — the contract between workstreams
src/
  common/           ring arithmetic (Z_{2^64}, Z_{2^128}), fixed-point, serialisation, PRG
  dpf/              W1  DPF: GGM tree, AES-NI, Gen/Eval/EvalFull      ── used by BOTH halves
  mpc/              W2  replicated sharing, PRF setup, matvec program
  mpc/nonlinear/    W2  Trunc_t, ApproxNormalize, FSS compare/zero-test
  mf/               W3  power iteration, SetOrthogonal, ApproxFactor
  serve/            W3  score computation, masking, oblivious top-k
  pir/              W1  DPF-PIR read layer, catalogue, consumption harvesting
  net/              framing, batching, flush()
  apps/             server0/1/2, user client, demo CLI
model/              W2/W3  cleartext oracle + quality evaluation (Python)
bench/              W4  sweeps, netem, figures
tests/              unit + end-to-end; cleartext oracles
references/         the two instructor-recommended papers
```

**Interface discipline.** Workstreams touch each other only through `include/oblivrec/`. W3 must be
able to build against stub non-linear protocols before W2 finishes them; W2 must be able to build
against a stub FSS gate before W1 finishes the DPF. **Fix those two headers in week one** — they
are the critical path for everyone.

---

## 9. Threat model and leakage profile

> A **graded deliverable** (D7), not documentation. Most groups will not write one. It costs one
> page and it is the strongest available signal of maturity.

### 9.1 Adversary classes

| Adversary | Capability | Our guarantee |
|---|---|---|
| **A1. One semi-honest server** | Follows the protocol, reads its own view | **Full user privacy**, up to the leakage in §9.2. The headline claim. Matches [NUDGE]'s model exactly. |
| **A2. One semi-honest server + arbitrarily many malicious users** | Above, plus colluding users | Still safe for honest users' ratings. Model *quality* is not protected without D9.3 input validation. |
| **A3. Two or more colluding servers** | Pool views | **No guarantee.** Replicated shares reconstruct. Stated in bold, in the abstract. |
| **A4. Malicious server** | Deviates from the protocol | **Out of scope**, as in [NUDGE] §3.1. It can corrupt correctness and availability. |
| **A5. Malicious user** | Malformed DPF keys | Corrupts only its own output; but a flood of invalid keys is a **DoS** on `EvalFull`. Mitigation is a Sabre-style logarithmic audit — D9.4, stretch. Unmitigated in the base system, and we say so. |
| **A6. Network observer** | Sizes and timing | Sees the public parameters and message timing only; all sizes are input-independent by construction. |

### 9.2 Leakage profile

**Hidden:** individual ratings; which items a user rated; the user embeddings `A`; the score
vectors; the top-*k* indices `T`; which content records a user fetches.

**Leaked by design:** `m, n, d, ℓ, k, b, t`; **the item embedding matrix `B`, in the clear**; the
*number* of non-zero entries in each user's rating vector (eliminable by requiring a constant
number of ratings, at a utility cost — [NUDGE] §3.1); message timing and query counts.

**Leaked and worth being honest about:** query *timing correlation* with external events sits
outside the protocol's protection. We do not claim to fix it.

### 9.3 The analysis that only we can do

Neither reference analyses the composition, because neither implements both halves.

`B` is public to every server. It is a complete latent-factor model of the catalogue. Therefore
**a single observed fetch is not a single bit — it is a projection onto a known basis**, and a
handful of observed fetches pins down a user's taste vector `a⁽ⁱ⁾` to a small region. This is the
quantitative argument for why the delivery layer must be private, and it is the strongest claim
our report can make:

> *Given public `B` and `j` observed fetches, reconstruct `â⁽ⁱ⁾` and measure
> `cos(â⁽ⁱ⁾, a⁽ⁱ⁾)` as a function of `j`.*

Cheap to run (it is a least-squares fit against a public matrix), it directly motivates our
contribution, and it is exactly the kind of result that separates a system from a demo.

---

## 10. Evaluation architecture (W4, `bench/`)

```
bench/scripts/run_sweep.py     drives the matrix, writes JSONL
bench/scripts/netem.sh         the four network profiles inside Docker
bench/scripts/make_figures.py  JSONL → every figure in the report
bench/results/*.jsonl          raw, committed, append-only
bench/figures/                 generated, gitignored
```

Every record: `{git_sha, host, profile, m, n, d, ell, b, t, k, stage, phase, wall_ms, cpu_ms,
bytes_sent, timestamp}`. `phase ∈ {setup, matvec, truncate, normalize, fss, topk, pir, net,
oracle}` so the microbenchmark breakdown falls out for free.

**Amended 2026-09-06, once there were two producers rather than zero.** Four clarifications, each
forced by real data:

1. **`oracle` added to the enum.** It names a *cleartext baseline* measurement. The eight original
   values are all protocol phases, and the Python quality oracle is not one of them. Labelling its
   rows `matvec` would be actively false, because `matvec` means the secret-shared phase whose cost
   S2 is measured against. Filtering to the eight protocol phases still yields the breakdown this
   section exists for.

2. **The sixteen mandated keys are always present, always in the order above, always with the
   meanings above. A producer may append its own keys after `bytes_sent` and before `timestamp`,
   documented here beside the producer.** Currently:
   - `model/` oracle rows add `ndcg_at_20`, `recall_at_20`, `svd_subspace_angle_rad`.
   - `bench/bench_dpf` rows add `domain_bits`, `op`, `ring`, `iters`, `rep`, `batch_ms`.

3. **`wall_ms` is per operation, not per batch.** The batch size travels as an `iters` extra, so
   batch time is recoverable by multiplying. One row is emitted per repetition and nothing is
   aggregated inside a producer, because aggregation would hide the distribution and the raw file
   is meant to be raw.

4. **`d` means the embedding dimension and nothing else.** DPF rows leave it null and carry
   `domain_bits` as an extra instead. `domain_bits` is deliberately **not** derived from `n`:
   MovieLens-100K has 1682 items in a 2048-entry domain, so a real PIR row will carry `n = 1682`
   with `domain_bits = 11` and the two genuinely differ. `n` is the item count, `2^domain_bits` is
   the padded domain.

**Measurement caveat, recorded because it is large.** Absolute timings from `bench_dpf` on the
development laptop vary by up to roughly 4x between back-to-back runs, consistent with thermal
throttling: within a single run the spread across repetitions is only 1.1x to 1.7x. Ratios between
operations in the same run are therefore far more trustworthy than absolute figures, and any
absolute number quoted from `profile: local` should be read as an order of magnitude. Every
repetition is retained in the JSONL with its own timestamp, so the drift is visible rather than
averaged away.

**Figures are never hand-edited and never produced outside `make figures`.** Raw JSONL is committed
so results survive a machine change; figures are not.

Network profiles: `local`, `lan` (1 ms / 1 Gbps), `wan_a` (30 ms / 100 Mbps), `wan_b`
(100 ms / 10 Mbps). Every headline number is reported on `wan_a` as well as `local`, because the
ranking of approaches is expected to change between them and **that change is the result**.

---

## 11. Decisions log

| Date | Decision | Reason |
|---|---|---|
| 2026-08-15 | ~~Scope = private retrieval, not private training~~ **REVERSED** | The instructor recommended [PIRSONA] and [NUDGE]; both are centrally about private training. |
| 2026-08-15 | Architecture = [NUDGE]'s 3PC power-iteration training core + [PIRSONA]'s PIR delivery loop | Fills the gap each paper leaves: [NUDGE] delegates private fetching to "other means"; [PIRSONA]'s 4PC training core is superseded. Composing is defensible; beating either is not. |
| 2026-08-15 | 3 servers, 2-of-3 replicated sharing, semi-honest honest-majority | Matches [NUDGE] exactly. One fewer non-colluding party than [PIRSONA]'s 4PC. |
| 2026-08-15 | Power iteration, not gradient descent | Matrix–vector products are non-interactive under replicated sharing; only truncation and normalization cost rounds. This is [NUDGE]'s core insight. |
| 2026-08-15 | Ring width `b` is a template parameter; `b=64` dev, `b=128` target | [NUDGE] needs 128 at Netflix scale. Whether 64 suffices at MovieLens scale is an open, cheap, publishable question (D9.1). |
| 2026-08-15 | One DPF implementation serves both the FSS gates and the PIR layer | Same primitive, two consumers. Forking it would double the work and the bug surface. |
| 2026-08-15 | Build order S1 (serving+delivery) → S2 (training) → S3 (composition) | S1 is lower-risk, demos early, and its DPF is a prerequisite for S2's non-linear gates. |
| 2026-08-15 | Fixed-width records throughout | Variable width leaks through response size. |
| 2026-09-06 | **Key material comes from the OS CSPRNG (`BCryptGenRandom`), never from `std::mt19937_64`** | The first implementation of `Gen` seeded a Mersenne Twister and used its output as DPF seeds. That is a real break, not a style point: the initial seeds *are* the secret, and MT19937 is fully reconstructible from 624 consecutive outputs, so an adversary seeing enough key material could rebuild the GGM tree and recover `alpha`. It also gave ~64 bits of seed entropy against a claimed `lambda = 128`. `bcrypt` is a Windows system library, so this is not a new third-party dependency. **Failure policy is abort, never degrade**, because a silent fallback makes a broken guarantee indistinguishable from a working one. |
| 2026-09-06 | Hand-written `Makefile` driven by `mingw32-make`, not CMake | CMake is not installed on the dev box and installing it would breach RULES A7. Supersedes REQUIREMENTS §9's toolchain line. The first target is `check-toolchain`, because g++ here fails **silently** (no binary, no error) when `/c/msys64/mingw64/bin` is off `PATH`. |
| 2026-09-06 | `-std=gnu++17`, and a local `oblivrec::Span<T>` instead of `std::span` | `unsigned __int128` is a GNU extension, so strict `-std=c++17` loses the `numeric_limits` specialisation. §7.1's API sketch uses `std::span`, which is C++20 while REQUIREMENTS §9 mandates C++17, so we supply the small part of it this project uses rather than bumping the standard. |
| 2026-09-06 | **DPF uses the difference convention**, i.e. no `(-1)^party` factor | Textbook BGI applies the factor and gives `Eval_0 + Eval_1 = f(x)`. §7.1 states the invariant as a *difference*, so the factor is omitted. Algebraically identical. Recorded because getting it backwards makes every test fail in a way that looks like a tree-traversal bug rather than a sign error. |
| 2026-09-06 | **No DCF and no oblivious top-*k* in S1** | The comparison gate's only consumers are `Trunc_t` and `ApproxNormalize`, both of which are S2. And per the 2026-08-20 correction the user selects top-*k* locally from the reconstructed score vector, so the servers never learn `T` regardless. §6's `SEL_SORT`/`SEL_TOURN` apparatus and §7.1's claim that a DPF and a DCF are the same primitive are both superseded. [NUDGE]'s own repository keeps `dcf/` and `multdpf/` separate, which corroborates the distinction. |
| 2026-09-06 | §2's `ell = 10` is retained, now on evidence rather than by default | Measured: nDCG@20 stays within 0.5% across `ell` from 10 to 400 while the SVD subspace angle falls five orders of magnitude. Since `ell` multiplies the only interactive cost in training, this makes the 40× communication saving free. See `docs/finding-ell-vs-quality.md`. |
| 2026-09-06 | Phase 2's exit criterion drops `tcpdump` for a channel-boundary transcript plus a distinguisher experiment | There is no `tcpdump` on Windows. A transcript recorded at the channel boundary is better provenance than a packet capture, and a distinguisher run over many trials is stronger evidence than one capture looking random, because it demonstrates an adversary *failing* rather than bytes *appearing* random. |
| 2026-09-06 | **"Exhaustive to `d ≤ 16`" is split into two claims with two bounds** | REQUIREMENTS §7 asks for exhaustive correctness and §7.1 states the invariant over `EvalFull`, so the headline claim is carried by `EvalFull` (every alpha against every x) and `Eval` is bridged to it by a separate index-by-index agreement check. Conflating them is why the target looked infeasible: every-alpha-every-x via `EvalFull` costs `4^D · 68 ns` against `4^D · 34D ns` via `Eval`, i.e. 8× less at `d=16`. The cheap path is also the one matching the spec's own wording. |
| 2026-09-06 | Default `make test` covers `d ≤ 11` exhaustively; the full `d ≤ 16` sweep is opt-in via `make test-exhaustive` | The full sweep is ~15 minutes. A suite that takes fifteen minutes stops being run, and then it stops catching anything. `d = 11` stays in the default tier because 1682 ML-100K items pad to 2048, so it is the domain the demo actually runs. |
| 2026-09-06 | Exhaustive-sweep `beta` is derived per alpha rather than fixed | The subtlest line in `Gen` is the `(-1)^{t1}` factor on the final correction word, and a single fixed `beta` tested it at exactly one value per ring across billions of leaf checks. A `splitmix64` of `(domain_bits, alpha)` costs nothing and keeps failures reproducible. |
| 2026-09-06 | Benchmarks write JSONL to stdout; the Makefile does the redirect | Keeps the binary a pure producer with no file I/O and no cwd assumption, and makes append-only-ness visible in the recipe rather than buried in C++. The bench link rule carries a `FORCE` prerequisite because make does not track `CXXFLAGS`, so an unchanged bench source at a new HEAD would otherwise emit rows stamped with the previous commit's SHA. |
| 2026-09-08 | **`model/export.py` is the only place a float becomes a ring element**, and its contract vectors are committed | Everything downstream of the Python-to-C++ boundary trusts it, so the boundary is tested rather than assumed. `tests/data/fixedpoint_vectors.txt` holds 25 `<double> <int64>` pairs covering zero, both signs, the quantum and half the quantum, the observed extremes of `A`, `B` and the scores, and the two's-complement edge. It is plain text so the C++ side needs no JSON dependency, and it is committed so a fresh clone tests the boundary with no export run. |
| 2026-09-08 | **Python matches C++ rounding, not the other way round: half away from zero** | Found by the contract test on its first run. `numpy.rint` and Python's `round` use banker's rounding (half to even, `rint(0.5) == 0`); C++ `std::round` rounds half away from zero (`round(0.5) == 1`). They disagree at every exact half-quantum. C++ is the reference because it is what ships, so `export.py` uses `trunc(x + copysign(0.5, x))`. |
| 2026-09-08 | **The reference scores are computed from the *encoded* factors, not by encoding the float product** | Quantise-then-multiply is a different operation from multiply-then-quantise, and the servers only ever hold encoded values. Encoding the float product disagreed with C++ on 1650 of 1682 scores, by up to `2.1e7` out of `4.3e12`. That is ~`5e-6` relative and irrelevant to the ranking, so a tolerance-based test would have accepted it and the oracle would have been quietly wrong for every later exact comparison. |
| 2026-09-08 | **S1 leaves scores at `2t` scale** | `Enc(a) · Enc(B)` carries `2t` fractional bits and truncating back to `t` needs `Trunc_t`, which is interactive and belongs to S2. Decoding with `t` instead of `2t` scales every score by `2^20`, which is *monotone* and therefore invisible in a ranking — hence recording it rather than leaving it to be rediscovered. |
| 2026-09-08 | **Catalogue records carry `id \| title \| date \| 19 genre flags`, with explicit length prefixes, padded to `L = 256`** | The IMDb URL is the largest field and nothing downstream uses it; field 3 (video release date) is empty on all 1682 lines, confirmed by measurement. Dropping both takes the longest record from 265 B to 117 B of content. Lengths rather than delimiters because nine titles contain Latin-1 bytes and any delimiter choice risks colliding with one. 1682 items pad to 2048, so `domain_bits = 11`. |
| 2026-09-08 | **P0 and P1 hold the PIR keys; all three servers hold the catalogue** | The DPF is (2,2) but the system has three servers, and §7 never said which two. The catalogue is public data, so replicating it costs nothing in privacy and keeps any two servers able to serve a read. |
| 2026-09-08 | **The seen-item mask is an additive shared vector carrying `-(2^55)` at seen positions** | §6 says the user supplies the mask as a share but never says how to represent minus infinity in a finite ring. Encoded scores peak near `9.7e12` at `2t = 40` and the ring floor is near `-9.2e18`, so `-(2^55) ≈ -3.6e16` sits ~3700× below any real score and ~256× above the floor, giving both margins. Both are checked by a test rather than asserted in prose. **Stated plainly because the report must not overclaim: in S1 the user reconstructs the score vector anyway, so server-side masking is *equivalent* to client-side masking.** It is done server-side because that is what the ideal functionality specifies and what S3 will need, not because S1 requires it. |
| 2026-09-08 | Top-`k` comparison is **signed**, over two's-complement ring elements | Unsigned comparison would sort masked items *first*, which is the exact opposite of the intent, and a smoke test that only inspects the top of the list would not see it. |
| 2026-09-09 | **The §9.3 reconstruction attack is measured against a POPULARITY control, not only a random one** | A random-direction control flatters the attack. Popular items sit near the top of nearly everyone's ranking, so an estimator can score well having learned only what is popular — which is public. The control therefore runs the identical estimator on the `j` globally most-popular items, ignoring the user entirely, and **the attack may only claim the gap above that line**. Measured: the gap in `cos` is a stable **+0.39 to +0.42** across every `j`, so the leakage is genuinely about the individual. The random control is kept as the floor. |
| 2026-09-09 | The attack's top-20 overlap metric **excludes the `j` items already observed** | Otherwise the metric rewards regurgitating the fetches the adversary was handed. Excluding them makes it a measure of *prediction*: the share of the user's **next** twenty recommendations an observer can name. It also explains the curve's shape — overlap peaks near `j = 10` (56%) and declines by `j = 50` (46%) because the easy head of the ranking has been excluded from the candidate pool, not because the estimate degrades. `cos` rises monotonically throughout, which is the check that the estimate does not in fact degrade. |
| 2026-09-09 | **The centroid estimator is reported as the headline, ahead of the ranking-constraint one** | The ranking estimator uses strictly more information (every unfetched item is a negative) and is nonetheless **worse** for every `j ≥ 2` — 0.81 against 0.92 at `j = 50`. Reported rather than dropped: it establishes that the obvious, cheap attack is already sufficient, which strengthens the argument for PIR rather than weakening it. An adversary needs no sophistication here. |
| 2026-09-09 | **Networking (PHASES 2.3, 2.8) is deferred to Phase 3** | Decided with the time remaining before Milestone 2 in view. The consequence is stated rather than buried: the amended Phase 2 exit criterion (a channel-boundary transcript plus a distinguisher experiment) is **not met**, and "three servers" remains a structural property of the code — shares are split, held and combined exactly as the protocol specifies — rather than something demonstrated across a socket. `src/net/` stays empty and the Makefile's `-lws2_32` is currently unused. |
| 2026-09-11 | **Networking is BUILT. This SUPERSEDES the 2026-09-09 deferral row above** | The row above is left standing rather than edited, because this log is append-only and a decision that was reversed is more informative than one quietly rewritten. `include/oblivrec/channel.hpp` + `src/net/tcp.cpp` + `src/apps/server.cpp`: three separate OS processes over loopback TCP, length-prefixed frames per REQUIREMENTS §5. **The amended Phase 2 exit criterion is now MET in full** — `make demo-net` returns the same ten titles as the in-process run, and `make distinguisher` shows an adversary failing. Every claim of the form "not met", "deferred" or "not demonstrated on a wire" elsewhere in the repository was retracted in the same commit; a stale caveat left standing after the fact would be exactly the silent divergence RULES forbids. |
| 2026-09-11 | **Separate processes, not threads** | Considered running the three servers as threads for a single-command demo. Rejected: separate processes are the stronger claim, and they have a structural payoff — each server serves one client to completion, so nothing is concurrent and **no threading appears anywhere in this project**. The platform risk that made this task look expensive came from the thread design, not from the process design. |
| 2026-09-11 | **Batching is deliberately NOT implemented**, though §8 names it | S1 exchanges a handful of frames per query, so there is nothing to batch. `flush()` exists and is documented as the no-op it honestly is under blocking sockets with no user-space buffer. Building an unused optimisation and calling §8 satisfied would be worse than stating the absence. |
| 2026-09-11 | The frame length prefix is **validated before it sizes an allocation** | It arrives over a socket, so a peer chooses it: four bytes claiming 4 GB is a four-byte denial of service. This is the third piece of code in the project parsing attacker-shaped input, after `DpfKey::Deserialize` and `Span`, and like them it is covered by `make check`. |
| 2026-09-11 | The split-delivery test forces the split **from the receiver's side**, with a tiny `SO_RCVBUF` | The first version sent a 200 KB frame to provoke fragmentation and **deadlocked**: with one thread and blocking sockets the sender and receiver are the same thread, so `send()` blocked waiting for a reader that could not run. Shrinking the receive buffer and keeping the payload inside the send buffer forces many `recv()` calls with no possibility of deadlock. Recorded because the naive version of this test hangs rather than fails, which is the worse failure mode. |
| 2026-09-11 | **Decoys are rejected as a defence, on measurement** | Fetching `k` real records plus `r` popularity-weighted decoys was measured as an alternative to a private read. It does not work, and the reason is structural: decoys must be popularity-weighted or they are separable by inspection, so they point along the popularity direction — which is exactly what the popularity control already recovers. The defence therefore has a floor at the control's level (cos 0.44) and cannot go below it. 40 decoys, five times the traffic, still leave the adversary at 0.71. Reported as a negative result about the alternative, which is the strongest available argument for the mechanism we did build. |
| 2026-09-11 | `git_sha()` **warns instead of returning "unknown" silently** | Bit for real: under memory pressure the `git` subprocess could not spawn, the `except Exception` swallowed it, and every distinguisher row was stamped `"unknown"` — which RULES D5 forbids and which is invisible in the data. Fixed in all three producers (`export.py`, `attack.py`, `distinguisher.py`). A swallowed provenance failure is worse than a loud one because it surfaces days later, in a figure nobody can reproduce. |
| 2026-09-12 | **§3.3's "rounds == number of NonLinear stages" is WRONG and is corrected in `mvp.hpp`** | A stage whose matrix is SHARED costs one round by itself, before any non-linear stage runs. The truth is `rounds == (shared matrices) + (rounds of each NonLinear)`. The draft's claim holds only when every matrix is public — which is S1's serving path, and very likely where the sentence came from. `DeclaredRounds()` computes it, `Run()` is measured, and a test asserts they agree, so the two cannot drift again. |
| 2026-09-12 | **`U` is kept at scale 0, not `t`** | Ratings are small integers (1..5). Encoding them at `t` fractional bits would spend 20 bits of headroom representing a value with no fractional part. At scale 0, `U v` and `U^T(U v)` are both at scale `t` and **need no truncation at all** — which removes truncation from the hot loop entirely, since the two matrix products are the expensive part. |
| 2026-09-12 | The growth bound uses **`nnz`, not `m·n`** | `\|U^T U v\| ≤ σ₁² ≤ ‖U‖_F² ≤ nnz · max_rating²`. The crude `m·n·max_rating²` bound assumes a fully dense matrix of maximum ratings and costs 26 bits at ML-100K where the truth needs 22. `nnz` is already listed as leaked by design in the threat model, so using it in a public bound reveals nothing new. |
| 2026-09-12 | **`‖v‖²` must be computed on a RESCALED `v`**, not the raw output of the matrix products | Squaring an un-normalised `v` needs `2·(t + growth)` bits — 84 at ML-100K — and overflows `b=64` before a single truncation runs. Fixed by shifting by the public growth bound immediately after the matrix products, which is data-independent and therefore leaks nothing. **Found by the subspace test failing, not by reading the algorithm**, which is the argument for having had that test. |
| 2026-09-12 | A truncation schedule is accepted only with a **margin of 8 spare bits**, not merely a positive number | The growth bound is worst-case over public parameters, not a guarantee about a particular matrix. Three spare bits is technically inside the ring and one unlucky dataset away from silent corruption. The margin required to *choose* the deferred schedule and the margin required to *accept* any schedule were inconsistent at first; a test caught it. |
| 2026-09-12 | **Every zero-share draw must advance ALL THREE generators** | `TruncatePair` drew a pairwise mask for two parties and not the helper. The three counters drifted apart, after which zero-shares stopped summing to zero and **every subsequent multiplication was silently wrong** — well-formed shares of the wrong value, no error raised anywhere. `Mpc3::Exchange` now asserts the three counters agree, because it is the one place every communicating operation passes through. This cost a broken factorisation to find. |
| 2026-09-12 | **The normaliser is an interface, and the working one CHANGES THE LEAKAGE PROFILE** | `ApproxNormalize` needs the FSS comparison gate, which is not built. Rather than block all of `ApproxFactor` or quietly substitute something weaker: `FssNormalizer` throws with the reason, and `RevealNormNormalizer` runs today at 2 rounds instead of ~57 by opening `‖v‖²`. §5 normalises *before* revealing `B[i]` precisely so the norm stays hidden, so this is a departure, not a detail. It is not the default, its name says what it does, and every result records how many scalars it revealed. **Recorded as OPEN in `docs/threat-model.md` §7.4** rather than settled. |
| 2026-09-12 | **`tc netem` and Docker are SUBSTITUTED, not skipped** (tasks 3.10, 3.13) | Verified absent: no `tc`, no Docker, and `wsl --status` reports the subsystem is not installed. WAN behaviour is modelled at the **channel boundary** — a `DelayChannel` decorator for S1's real sockets, and a cost model over S2's measured round/byte counters — and every number so produced is labelled channel-level emulation, never netem. It models no queueing, loss or slow-start; it models round count and bytes exactly, and on a WAN those dominate. Docker's reproducibility role is taken by `make reproduce`. **This amends the Phase 5 exit criterion, which is a graded line**, and is recorded here rather than quietly reinterpreted. |
| 2026-09-12 | **B4 (MP-SPDZ) is out of scope, with reasons** | It needs Linux or Docker (neither present), it needs the private-training half to exist first, and installing it would breach the standing no-new-dependencies rule (RULES A7). Stated as blocked in `bench/scripts/baselines.py` output rather than left looking unattempted. |
| 2026-09-12 | **A DCF is NOT the DPF with a different payload**, contradicting §7.1's draft claim | §7.1 asserts the two are the same primitive. They are not: a DPF is non-zero at one point, a DCF on a whole prefix, and the construction needs three things the DPF lacks — four PRG outputs per node (hence `Expand4`), a per-level VALUE correction word, and a running accumulator `V_alpha` in `Gen` whose value at each level depends on everything below it. NUDGE's own repository keeps `dcf/` and `multdpf/` separate, which is what prompted checking. Keys are 263 B at db=9 against the DPF's 209 B at db=10. |
| 2026-09-12 | The DCF uses the **sum convention internally and converts at the boundary** | The published construction carries a `(-1)^party` factor and yields `Eval_0 + Eval_1 = f(x)`; this project uses the DIFFERENCE convention everywhere else. Rather than rewrite the construction and risk a sign error that would look like a tree bug, `EvalDcf` negates party 1's output so `EvalDcf(k0,x) - EvalDcf(k1,x) == f(x)` and a caller never has to remember which primitive it holds. |
| 2026-09-12 | **The DCF domain index is `u128`, not `u64`** | The FSS gate masks a value with `kappa` extra bits before opening it, so its domain is `value_bits + kappa + 1` — over 64 for any realistic range. A 64-bit index would have silently capped the achievable statistical security at whatever fits, which is the wrong thing to trade away by accident. Found when the gate refused to build at b=128 for a reason that had nothing to do with the ring. |
| 2026-09-12 | **`b = 64` cannot support a spec-faithful `ApproxNormalize` AT ALL** — a sharper D9.1 answer than the headroom study gave | Opening `S + r` hides `S` only if `r` is `kappa` bits wider than `S`. At `kappa = 40` and the ranges power iteration produces, the gate's domain needs ~81 bits, and a 64-bit ring has no room for the mask. The arithmetic headroom study said `b=64` survives to ML-1M but loses the deferred schedule; this says that the moment you want a normaliser revealing nothing, `b=64` stops being an option. **The two constraints bind for different reasons and the tighter one wins.** `FssNormalizer`'s constructor refuses at b=64 rather than using a short mask, because a gate masking with too few bits would look like privacy while providing none. |
| 2026-09-12 | The Newton halving is folded into the **truncation**, not done on the shares | `y(3 - Sy²)/2` needs a division by two. Halving each share locally is wrong for exactly the reason `TruncateLocal` is wrong — the shares are uniform, their sum wraps, and the carry is lost. Truncating by `t+1` instead of `t` does the rescale and the halving in one protocol call. The local version would have looked like slow Newton convergence rather than like a bug. |
| 2026-09-12 | **The price of the no-leak path is measured, not estimated**: ~5× the rounds | `RevealNormNormalizer` costs ~16 rounds per `d×ℓ`; `FssNormalizer` ~80. That is what removing the singular-value leak costs, and it is now a number rather than an argument. Both paths remain available and every result records which one produced it. |
| 2026-09-13 | The parameter sweep is **one factor at a time around a baseline**, not the Cartesian product | REQUIREMENTS D6 asks for sweeps over `m, n, d, ell, b, k` and the network profiles. Crossing all six is thousands of configurations and hours per run, and most cells answer nothing — nobody needs the cost at `d=32, ell=1, b=128, k=20` simultaneously. A benchmark that takes hours stops being re-run, and a benchmark that stops being re-run stops being true. So the sweep fixes `ml-100k, d=16, ell=10, b=64, k=10` and moves one axis at a time. **Interaction effects are therefore not measured, and that is stated in the file header rather than left for a reader to discover.** |
| 2026-09-13 | Network profiles in the sweep are **derived from the measured counters, not re-run** | Training never transmits: the substrate simulates three parties in one process and counts what it would have sent. Re-running the whole sweep once per profile would measure the same round and byte counters four times and add nothing but hours. Each row instead carries its measured counters and `netprofile.hpp` projects them. Every projected row is labelled `emulation: channel-level, not netem`, because that is what it is. |
| 2026-09-13 | **NUDGE Thm 4.2 is now measured, not quoted** | Holding everything else fixed and taking `m` from 235 to 943 — a 4x larger input matrix — moved traffic 62.03 -> 64.75 MB (+4.4%) and left the round count **unchanged at 2686**. The 2.72 MB delta matches the predicted `m`-dependent component exactly. Communication tracks the largest intermediate *vector*, not the input matrix. This is the single result that most justifies choosing NUDGE's formulation over PIRSONA's 4PC training core, and until now the project had only asserted it. |
| 2026-09-13 | **`stage = "S3"` is now a real value**, not just vocabulary in REQUIREMENTS | Section 10 fixes the `phase` enum but leaves `stage` free-form, and every row ever written has carried `S1` (serving/delivery) or `S2` (private training). REQUIREMENTS section 5 has always named the composition `S3`; task 4.2 is the first producer whose rows belong to neither half alone, so it stamps `S3` and distinguishes its five arms by a `config` extra. Ratifying the existing name beats inventing a sixth. |
| 2026-09-13 | The 4.2 arms are measured **in one process with interleaved repetitions** | The measurement caveat recorded above is large: absolute timings on this laptop drift up to 4x between back-to-back runs while within-run spread is 1.1-1.7x. Five configurations timed in five processes would be measuring the thermal state of the machine, not the protocols. One process, and the report quotes ratios. |
| 2026-09-13 | Task 4.3 attributes cost by **decorating an existing interface**, not by editing protocol code | `Normalizer<Ring>` is already a pure-virtual interface and `ApproxFactorShared` already takes it by reference, so a metering wrapper defined inside the bench binary yields normalisation's true share of a real training run with **zero** edits to `src/`. The remaining stages are attributed analytically -- `FactorResult.truncations` x 3 rounds per `TruncatePair`, `d*ell*2` matvec rounds, `d` opening rounds -- and the binary emits the **residual** against the measured total. A residual of zero is the claim worth making: the breakdown accounts for every round, rather than sampling some of them. Instrumenting the protocols themselves would have meant editing code three weeks before a freeze, for a worse answer. |
| 2026-09-13 | The cleartext arms of 4.2 use **`ApproxFactorClear`, not the Python oracle** | B1/B2/B5 need a cleartext factorisation timed in the same process as the private one. `model/mf.py` is the real oracle but is Python and cannot be timed against C++ in one run. `ApproxFactorClear` is the fixed-point twin that takes the SAME truncation points, so the comparison isolates the cost of secret sharing rather than conflating it with a change of numerics. It is instantiated for `u64` only, which is why 4.2's cleartext arm stays at `b=64`. |
| 2026-09-13 | **The MSNZB gate's range was silently wrong, and it cost 0.108 of nDCG@20** | `FssNormalizer` was constructed with `[t-8, t+8]` and 30 value bits, a figure copied from `bench_sweep.cpp`. `MsnzbGate::Apply` builds its answer as a telescoping sum of `1[S >= 2^k]` over `k` in `[lo, hi]`, so a value below `2^lo` silently receives `table[0]` and one at or above `2^hi` receives `table[hi-lo]`. Newton-Raphson then refines from the wrong power of two. Measured at `ell=10`: 0.3428 against 0.4513 for the same run with the revealing normaliser, with the subspace angle pinned at 1.571 rad -- exactly orthogonal to the oracle's subspace, which is what a power iteration that never converges looks like. Widening to `[1, 2t+8]` recovers 0.4493, a gap of -0.0020. |
| 2026-09-13 | **`MsnzbGate::Apply` cannot check its own range, and the header claiming it did was wrong** | The header read "a value outside is a programming error, not a silent wrong answer, so Apply checks." It does not check, and it must not: `Apply` sees only the masked opening `S + r`, and learning `msnzb(S)` at runtime is exactly what the gate exists to avoid revealing. A runtime check would defeat the protocol. The obligation is therefore the CALLER's, to size the range from public parameters, and the header now says so. |
| 2026-09-13 | Task 4.1's published FSS cost numbers are **unaffected** by the range correction | Rounds and bytes are byte-identical between the narrow and the corrected range at every `ell` -- 11519 rounds and 143,941,376 bytes at `ell=10` either way -- because widening buys more DCF keys OFFLINE and `Apply` remains one round. The correction changes the model's quality, not its cost, so the sweep's numbers stand as published. |
| 2026-09-13 | **The no-leak path costs ~0.002 nDCG@20, not ~0.11** -- and the difference was found only by measuring quality | Until this task the spec-faithful normaliser had its cost measured (task 4.1) but had never produced a `B`, so its quality was unmeasured. The first measurement showed a large gap, which a control run isolated: `b=128` with the REVEALING normaliser scores 0.4513 against `b=64`'s 0.4511, so ring width and the deferred truncation schedule contribute +-0.0005 and the entire gap belonged to the normaliser -- and then to its gate range rather than to privacy. A cost-only benchmark could not have surfaced this, which is the argument for measuring both. |
| 2026-09-13 | **Task 4.3's headline: truncation is ~70% of every round, and the FSS gate is 3%** | The breakdown reconciles exactly -- residual zero rounds and zero bytes in all three configurations -- so these are shares of a COMPLETE budget rather than of a sample. `truncate` takes 74.4% of rounds at b=64 reveal-norm, 68.6% at b=128 reveal-norm and 72.6% at b=128 FSS, and 75-83% of bytes throughout. `matvec` is 2.8-14.6% of rounds, which is NUDGE's free-matrix-vector claim made visible. The `fss` gate -- the primitive the entire D9.1 ring-width argument is about -- is **3.1%**. The actionable conclusion is to optimise truncation; the gate is not the problem. |
| 2026-09-13 | The 4.3 breakdown is checked **twice, by different means** | The residual check alone would still pass if two phases were mis-attributed between each other, so the in-situ `CountingNormalizer` provides a second, independent constraint: everything it sees must equal the model's own normalize+fss rounds plus the `TruncatePair` calls the normaliser makes internally (3 per call for reveal-norm, 3*(2+3*newton) for FSS). Both checks agree in all three configurations, and a disagreement in either exits non-zero rather than publishing a breakdown that does not add up. |
| 2026-09-13 | **The composition costs EXACTLY the sum of its halves** | Task 4.2's central question is whether composing private training with private delivery costs more than either alone. It does not: `FULL = B2 + B3 - B1 = 64,760,124 bytes`, exactly, because the two halves are independent -- training bytes are server-to-server, delivery bytes are client-to-server. `bench_compose.cpp` asserts this before emitting a composed row, so a double-count fails the run rather than being laundered into a figure. On `wan_a` and amortised over 943 users, private delivery costs **+2.5 ms** per session and private training **+96 ms**. |
| 2026-09-13 | **On a WAN the full-catalogue download BEATS DPF-PIR, and `fig_pir_cost`'s conclusion is local-only** | Delivery is issued as `k` sequential round trips (`demo.cpp` fetches one record at a time), so at `k=10` on `wan_a` DPF-PIR pays 10 x 30 ms of latency while B5 pays one round trip: 303.8 ms against 65.6 ms, a **4.6x loss despite sending ~45x fewer bytes**. The existing `fig_pir_cost` claim that PIR wins below 8 Gbit/s is a statement about bytes and CPU at the `local` profile and does not survive criterion 276 -- which is precisely why that criterion asks for `wan_a`. The client knows all `k` indices at once, so batching them into one round trip gives 33.8 ms and reverses the result; that variant is emitted as `compose_batched` with `implemented=false`. What is built and what is possible are both labelled, and neither is quietly substituted for the other. |
| 2026-09-13 | Training and delivery are combined by **amortising training over `m` users per model refresh** | A per-model cost and a per-session cost cannot share a bar without a denominator, and the denominator is a modelling choice rather than a measurement. So every composed row carries `amort_users`, `amort_basis`, and both halves separately (`train_ms`/`train_bytes`, `deliver_ms`/`deliver_bytes`), letting a reader re-amortise at whatever refresh rate they think realistic. The crossover between the two halves IS the refresh rate, and that framing is the result. |
| 2026-09-24 | **The real/ideal simulation sketch is written (D7), and it is labelled a SKETCH deliberately** | `docs/threat-model.md` section 8. It walks every message the system sends and names the simulator at each step: the replicated substrate, Araki MatVec, `TruncatePair`'s two distinct helper/opener cases, both normalisers, the local `SetOrthogonal`, opening `B`, serving, DPF-PIR delivery and the harvest conversion. It is NOT a proof -- no security parameter, no reduction, no written-out hybrid chain -- and section 8.4 lists the three places the argument does not close. Upgrading a sketch to a proof by vocabulary would be the exact failure this project keeps catching in other people's work. |
| 2026-09-24 | **`RevealNormNormalizer` is not simulable against `F_train`, so the honest statement needs a SECOND, weaker functionality** | It opens one scalar per normalisation; across a run that is `d*(ell+1)` scalars -- the singular-value trajectory of `U` -- and no simulator given only `B` can produce them, because they are not a function of `B`. Two options: exclude the path and claim security only for FSS, or define `F_train^leaky` whose output is `(B, {||v||})`. We take the second, because it is what we actually ran: **every b=64 quality number in `train_quality.jsonl` came off the revealing path.** Claiming the strong statement while reporting numbers obtained under the weak one is the dishonest version of this section. Section 7.6 prices the move to the path that does close: 0.002-0.005 nDCG@20, and 5.3x the rounds. |
| 2026-09-24 | **The transcript writer produced invalid JSON, and it was a concurrency bug rather than a formatting one** | `demo.cpp` wraps THREE channels -- one per party -- at the same path in one process, and `run_servers.sh` had the three server processes appending to that same file. Each wrapper owned its own `std::ofstream`. A record is the base64 of a whole frame, up to ~36 KB, far larger than a stream buffer, so one record spanned several flushes and the flushes interleaved: seven lines read `"b64": "b64": ...`. Fixed in two halves, because the two cases are different: in-process, a registry keyed by path gives every wrapper one shared stream and one mutex, and a record is built into a string then written under the lock; across processes there is no lock to share, so `run_servers.sh` now gives each process its own file. Verified: 98 rows across four files, zero malformed, against 55 rows with 7 malformed before. |
| 2026-09-24 | The distinguisher's data was **never** affected by that bug, and this was checked rather than assumed | `probe.cpp` wraps ONE channel per process and `make distinguisher` runs its three alphas sequentially, so there is only ever one writer. `distinguisher.jsonl` parses clean at 3603 rows. The graded A6 claim -- 1800 queries, every frame 228 bytes, classifier at 0.490-0.500 -- stands unchanged. Worth recording because a corrupt transcript file next to a security result is exactly the kind of thing that should be run down rather than waved at. |
| 2026-09-24 | **D9.2 implements NUDGE section 9's ACTUAL mechanism, which is noise on the Gram matrix, not on `B`** | The repo had three one-line mentions of DP and no mechanism, so the paper was read before any code. Section 9 specifies: row-normalise `U`, then have each pair of servers PRF-share a Gaussian matrix `E`, and at every power-iteration step add `Mul(<<E>>, v)` to `Mul(<<U^T>>, Mul(<<U>>, v))`. Since power iteration computes `(U^T U) v`, this releases `U^T U + E` -- Dwork et al.'s Analyze Gauss applied to the covariance. It costs **no communication**, because `E` is shared once by PRF and the extra term is a matvec the protocol already performs. Implementing what we guessed rather than what they wrote would have measured the wrong thing. |
| 2026-09-24 | The **noise scale is not in the paper**, so it comes from the Analyze Gauss calibration the paper cites | `sigma = sqrt(2 ln(1.25/delta)) / epsilon` for rows with `||u||_2 <= 1`, where the sensitivity is exact because changing one user's row changes `U^T U` by `u u^T` with Frobenius norm `||u||_2^2`. Recorded because a reader checking our epsilon against the paper will not find it there. |
| 2026-09-24 | We **L2-normalise rows where NUDGE says to divide by the rating count**, and the deviation is deliberate | Count-normalisation gives `||u||_2 <= 5/sqrt(c_i)`, which exceeds 1 whenever a user has fewer than 25 ratings -- and **200 of ML-100K's 943 users do**. The Analyze Gauss bound would simply not apply to them, so the epsilon would be decorative. L2 normalisation makes the sensitivity exactly 1 and the claim meaningful. Both arms are run and both are reported, so the deviation is visible rather than asserted. |
| 2026-09-24 | **DP does not defend against the section 9.3 attack, and it makes it slightly WORSE** | At `eps=1`: nDCG 0.4255 -> 0.1239 (-71%) while the attack's cosine rises 0.8231 -> 0.9212 and its margin over the popularity control widens from +0.4324 to +0.4898. Not a paradox: DP protects the TRAINING DATA -- what `B` leaks about other users' ratings -- while section 9.3 recovers THIS user's embedding using `B` as a known basis. Adversary and user work against the same published matrix, so noising it leaves the attack's geometry intact and destroys the shared popularity structure the control was exploiting. Two defences have now been measured against this attack and **both fail to reach the control**, which is the argument for making the read private rather than patching around it. |
| 2026-09-24 | **Our -71% and NUDGE's -17% are consistent, and the difference is scale** | Analyze Gauss noise grows as `sigma*sqrt(n)`; the signal grows as `m`. So the usable regime is set by `m/sqrt(n)`, which is **157x more favourable for Netflix (480189 x 17770) than for ML-100K (943 x 1682)**. Measured rather than argued: `||U^T U||_2 = 144.6` against `||E||_2 ~ 613` at `eps=1`, i.e. the noise dominates by 4.2x. DP on the Gram matrix is a large-scale mechanism and ML-100K is the wrong scale for it. Stating that is a claim about the dataset, not a contradiction of the paper -- and it is the kind of result only available because we ran their mechanism rather than one of our own. |
| 2026-09-24 | **D9.3's target does not exist in OblivRec, so we built the finding that does** | The requirement asks for a well-formedness check on user *submissions*. There is no rating-upload path here: training reads MovieLens off local disk and task 3.9 deliberately removed the upload, harvesting consumption from the PIR queries instead. Implementing a check for a path we do not have would have been box-ticking. The real unchecked submission is the DPF query: `PirClient::Query` fixes `beta=1`, but nothing stops a client calling `Gen(alpha, beta, db)` directly, and `AnswerAndHarvest` folds whatever it gets into the accumulator that becomes the next round's training input. One query at `beta=10^6` casts a million-weight vote with every pre-existing check passing. |
| 2026-09-24 | **One malicious client can barely move the model, and the normalisation is why** | Measured: at weight 10^6 a single attacker costs 1.0% of nDCG@20 and cannot even aim -- its target items move DOWN, mean rank 901 -> 977 of 1682. Power iteration normalises every step, so past a point extra weight only fixes a DIRECTION rather than growing. One attacker therefore captures one of the `d` components and the other `d-1` still carry the honest signal. The effect saturates at weight 100 for exactly this reason. |
| 2026-09-24 | **`d` colluding clients destroy the model completely, and the cliff is at exactly `d`** | With distinct target sets so each burns a different direction: 8 colluders cost 4.1% of nDCG, **16 colluders (= d) cost 99.5%** -- 0.4536 to 0.0021. Burn all `d` components and nothing honest is left. This is model DESTRUCTION, not promotion, and it is what makes the weight check worth its round: the honest-weight control leaves nDCG at 0.4536 with a subspace angle of 2.5e-05. |
| 2026-09-24 | The weight check **opens a scalar, and that is safe rather than lazy** | Summation is linear, so each server sums its own expansion locally and `sum(e0) - sum(e1) = beta` exactly, for any alpha. Opening that one scalar costs **one round and two ring elements per query** and leaks nothing: the secret is WHICH item was fetched, `beta` is a payload that is supposed to be the public constant 1, and the sum is independent of alpha by construction. A DCF range gate was considered and is unnecessary -- there is no value to hide. |
| 2026-09-24 | The weight check closes **inflation, not redistribution**, and the test constructs the gap | The sum bounds the total weight, not its distribution. A client forging correction words directly could produce +2 at one index and -1 at another, summing to 1 and passing. `tests/test_harvest.cpp` builds exactly that forgery and asserts it passes, so the boundary is demonstrated rather than described. Proving a key encodes a genuine one-point function is a Sabre-style audit, which is D9.4's stretch half and remains unbuilt. |
| 2026-09-24 | **The DoS on `EvalFull` is 121x at our catalogue and grows with it** | A5 said "unmitigated" with no number. Measured: the key is O(log N) -- 227 bytes at db=11 -- and the answer is O(N), so amplification runs 15x at db=8, **121x at db=11**, and 3495x at db=16. It gets WORSE as the catalogue grows. The expensive attack is the one that looks LEGITIMATE: a malformed key is cheap because `Deserialize` rejects it before any expansion, so parsing hardening does not address this. That the server touches every index is the privacy property, not an inefficiency, which makes this exposure intrinsic to the design rather than a bug in it. |
| 2026-09-24 | **The timing side channel is bounded at 2% of a call, and a bound is the honest form of this claim** | `bench/bench_timing.cpp` times `PirServer::Answer` 200x across eight indices spanning the domain. Spread between the per-alpha means is **1.7 us** against a within-alpha standard deviation of **6.0 us**, and a permutation test on shuffled alpha labels puts the observed spread at **p = 0.60** -- squarely inside the null. No finite sample shows a difference is exactly zero, so the claim is that any alpha-dependent signal sits **below 2.0% of the 87 us mean call**, not that the code is constant-time. Consistent with the construction: `EvalFull` walks every node and the answer is an unconditional inner product, so there is no data-dependent branch to find. |
| 2026-09-24 | The timing samples are **interleaved across alphas, not blocked** | Measuring all of alpha=0 then all of alpha=1 would let thermal drift -- documented at up to 4x between runs on this box -- align with alpha identity and manufacture exactly the signal the experiment is testing for. Same reasoning as `bench_compose.cpp`'s rep-major loop. |
| 2026-09-24 | **Only the server-side half of the timing question is answerable here**, and the other half is stated rather than faked | Inter-frame timing ON THE WIRE is dominated by the OS scheduler and the loopback stack; measuring it would mostly measure Windows. What the protocol controls is whether the server's work depends on the index, and that is what was measured. The wire half stays an open limitation in section 5 rather than being quietly folded into the result. |
| 2026-09-24 | **Task 4.8's clean-checkout run passed, and it found a real defect** | A fresh `git clone` into a scratch directory ran `mingw32-make reproduce` to exit 0 in 762 s, regenerating all 11 figures from nothing but the checkout and the documented toolchain. It also died the first time at step 1/9 with a raw urllib `CERTIFICATE_VERIFY_FAILED` traceback on a network-restricted box -- which is exactly the kind of thing only a clean run surfaces. `scripts/fetch_data.py` now honours a pre-placed archive and names the URL, the target path and the expected md5. The DoS amplification also read 147x in the clean clone against 121x here, which is the documented between-run thermal variance and the reason this project quotes ratios rather than absolutes. |
