# ARCHITECTURE

**System:** OblivRec — Private Matrix Factorization with Private Delivery

> **Purpose of this file.** The single source of truth for *how the system is built*. Any code
> that contradicts this document is a bug in one of the two — resolve it by amending this file
> first, then the code. Never the other way round.
>
> **Status:** rewritten 2026-08-15 after the instructor recommended [PIRSONA] and [NUDGE].
> Amendments go in the Decisions Log (§11).

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
- **Key size:** `d·(λ+2) + b` bits. For `n = 4096`: **≈ 260 bytes**, against `32 KB` for a naive
  one-hot query. That factor is why this is practical, and it is W1's headline number.
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
`D`. Each server does one `EvalFull` and an inner product against `D`; the shares sum to `D[T_j]`.

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
bytes_sent, timestamp}`. `phase ∈ {setup, matvec, truncate, normalize, fss, topk, pir, net}` so the
microbenchmark breakdown falls out for free.

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
