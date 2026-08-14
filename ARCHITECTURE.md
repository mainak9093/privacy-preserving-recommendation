# ARCHITECTURE

**System:** OblivRec — Private Top-*k* Recommendation over Secret-Shared Similarity Models

> **Purpose of this file.** This is the single source of truth for *how the system is built*.
> Any code that contradicts this document is a bug in one of the two — resolve it by
> amending this file first, then the code. Never the other way round. See
> [RULES.md](RULES.md) for why this is enforced.
>
> **Status:** design frozen for Phase 2. Amendments require a PR that edits this file and
> a note in the changelog at the bottom.

---

## 1. System overview

```
                  ┌──────────────────────────────────────────────┐
                  │  OFFLINE, IN THE CLEAR  (Python, one-time)   │
                  │                                              │
                  │  MovieLens ratings                           │
                  │       │                                      │
                  │       ▼                                      │
                  │  item–item similarity  S_float ∈ R^{n×n}     │
                  │       │  quantise, scale 2^f                 │
                  │       ▼                                      │
                  │  S ∈ Z_{2^ℓ}^{n×n}   +   metadata M          │
                  └───────────────┬──────────────────────────────┘
                                  │  replicated, identical, public
                  ┌───────────────┴───────────────┐
                  ▼                               ▼
        ┌───────────────────┐          ┌───────────────────┐
        │    SERVER 0       │          │    SERVER 1       │
        │    holds S, M     │          │    holds S, M     │
        └─────────┬─────────┘          └─────────┬─────────┘
                  │      assumed NON-COLLUDING   │
                  │                              │
    ── Stage A ───┤  m DPF key shares  ──────────┤   (1 round)
                  │  ◄── ⟨score⟩₀          ⟨score⟩₁ ──►
                  │                              │
    ── Stage B ───┤   oblivious top-k over ⟨score⟩   (interactive)
                  │  ◄── ⟨T⟩₀              ⟨T⟩₁ ──►
                  │                              │
    ── Stage C ───┤  k DPF key shares over M ────┤   (1 round)
                  │  ◄── ⟨M[T]⟩₀        ⟨M[T]⟩₁ ──►
                  └──────────────┬───────────────┘
                                 ▼
                          ┌─────────────┐
                          │   CLIENT    │  holds private profile P
                          │  reconstructs T and M[T]
                          └─────────────┘
```

**The whole design in one sentence:** the client's profile is encoded as Distributed Point
Function keys, so each server sees a pseudorandom key that reveals nothing, yet the two
servers' outputs *add up* to exactly the scores the client wanted.

---

## 2. Notation and parameters

| Symbol | Meaning | Default |
|---|---|---|
| `n` | number of items in the catalogue | 3,706 (ML-1M) |
| `m` | number of items in the client's profile | 10–200 |
| `m̄` | **padded** profile size, a public constant | 256 |
| `k` | number of recommendations returned | 10 |
| `ℓ` | secret-sharing ring width, shares live in `Z_{2^ℓ}` | 64 |
| `f` | fixed-point fractional bits | 20 |
| `λ` | DPF security parameter / PRG block width | 128 |
| `⟨x⟩_b` | server `b`'s additive share of `x`, so `⟨x⟩₀ + ⟨x⟩₁ = x mod 2^ℓ` | |

**Why `Z_{2^64}` and not a prime field.** Native machine arithmetic, free modular reduction,
and it is what Grotto and Duoram target. Truncation after fixed-point multiplication is the
one place this costs us; see §4.3.

**Why `m̄` is public and padded.** Profile size is metadata that a real deployment leaks
anyway through timing, so we make it explicit and constant rather than pretending otherwise.
Clients with `m < m̄` pad with DPF keys for point functions with value `0`. This is recorded
in the leakage profile (§7).

---

## 3. Offline pipeline (Python, `model/`)

Runs once. Produces artefacts that both servers load identically.

| Step | Output | Notes |
|---|---|---|
| 1. Load ratings | `R ∈ R^{u×n}` sparse | MovieLens, downloaded by `scripts/fetch_data.py`. |
| 2. Mean-centre per user | `R̃` | Removes user rating bias. |
| 3. Item–item similarity | `S_float = shrunk cosine(R̃ᵀ R̃)` | Shrinkage `λ_s = 100` against low-support pairs. |
| 4. Sparsify (optional) | top-`s` neighbours per row | Only for the DORAM variant (D7.3); the dense matrix is the default. |
| 5. Quantise | `S = round(S_float · 2^f) mod 2^ℓ` | Signed two's-complement in the ring. |
| 6. Metadata table | `M ∈ ({0,1}^L)^n`, `L = 256` bytes | Fixed-width records, zero-padded. Fixed width is a *security* requirement, not a convenience. |
| 7. Emit | `model/out/S.bin`, `model/out/M.bin`, `model/out/params.json` | Byte-identical on both servers; verified by SHA-256 at startup. |

**Quality gate.** Step 5 must not degrade recommendations. `model/eval_quality.py` reports
Recall@k and NDCG@k for `S_float` vs `S` and fails the build if Recall@10 drops by more than
0.5% absolute. If we cannot hold that, `f` goes up and we re-check overflow headroom (§4.3).

---

## 4. Stage A — private scoring

### 4.1 The insight

The client wants `score = Σ_{(i,r) ∈ P} r · S[i, ·]`. This is a **weighted multi-point read**
of `S`'s rows. A DPF gives us exactly that: a pair of short keys whose full-domain evaluations
differ only at one hidden index.

### 4.2 The (2,2)-DPF (`src/dpf/`)

We implement Boyle–Gilboa–Ishai (EUROCRYPT 2015, CCS 2016), the standard GGM-tree construction.

- **Domain:** `[0, n)`, padded to `2^d` with `d = ⌈log₂ n⌉`.
- **PRG:** AES-128 in fixed-key Davies–Meyer mode (`π(x) ⊕ x`) using AES-NI, expanding one
  128-bit seed to two children plus two control bits. One `AESENC` chain per node.
- **Key size:** `d · (λ + 2) + ℓ` bits. For `n = 4096, ℓ = 64`: **≈ 260 bytes**. Compare to
  the `n · ℓ / 8 = 32 KB` a naive one-hot vector would cost. This factor is the entire reason
  the system is practical, and it is the headline number for the W1 lit-survey slot.
- **Output group:** `Z_{2^64}`, so the payload correction word carries the client's rating `r`
  directly. The client does *not* send `r` separately — it is folded into the DPF payload,
  so a server cannot even learn the rating distribution.
- **API** (`include/oblivrec/dpf.hpp`):
  ```cpp
  struct DpfKey { uint8_t party; Block seed; std::vector<CorrectionWord> cw; uint64_t cw_out; };

  std::pair<DpfKey, DpfKey> Gen(uint32_t alpha, uint64_t beta, uint32_t domain_bits);
  uint64_t                  Eval(const DpfKey&, uint32_t x);
  void                      EvalFull(const DpfKey&, std::span<uint64_t> out);  // O(2^d), one pass
  ```
- **Invariant, tested:** `EvalFull(k0)[x] + EvalFull(k1)[x] == (x == alpha ? beta : 0)` in `Z_{2^64}`,
  for every `x`, for random `(alpha, beta)`. `tests/test_dpf.cpp` checks this exhaustively for
  `d ≤ 16` and by sampling above.

**Non-negotiable:** this is written by us. Not `libdpf`, not lifted from Duoram. See
[RULES.md](RULES.md) R3.

### 4.3 Server-side aggregation

Client sends `m̄` key pairs (one key of each pair to each server). Server `b` computes:

```
⟨score⟩_b [j]  =  Σ_{t=0}^{m̄-1}  Σ_{i=0}^{n-1}  EvalFull(key_t^b)[i] · S[i][j]      (mod 2^ℓ)
```

Naively `O(m̄ · n²)`. Two optimisations, both mandatory:

1. **Fuse the sum over `t` first.** Compute `w_b = Σ_t EvalFull(key_t^b) ∈ Z_{2^ℓ}^n` — cost
   `O(m̄ · n)` — then a single vector–matrix product `w_b · S`, cost `O(n²)`. Total
   `O(m̄·n + n²)` instead of `O(m̄·n²)`. This is the difference between minutes and milliseconds.
2. **SIMD the matvec.** `n²` 64-bit multiply-accumulates, AVX2, row-major, cache-blocked.
   For `n = 4096` that is 16.8M MACs ≈ 5 ms single-threaded.

**Fixed-point truncation.** `w · S` produces a `2f`-scaled result. We truncate by `f` bits.
Because both servers hold shares, local truncation introduces the standard 1-bit error with
probability `2^{-(ℓ - 2f - log n)}`. With `ℓ=64, f=20, n=2^16` the headroom is 8 bits — safe,
but **`model/eval_quality.py` must confirm empirically that truncation error does not change
the top-*k***, not merely argue it. Document the measured mismatch rate.

**Round complexity: 1.** Client → servers, servers → client. This matters more than bandwidth
on the WAN profiles, which is the point §8 exists to demonstrate.

### 4.4 Seen-item masking

`score[j]` must be `−∞` for `j` already in the profile. The client folds this in for free:
it sends `m̄` *additional* DPF keys with payload `−2^{ℓ-2}` targeting its own profile indices,
into a separate accumulator that is added to `score`. No extra round, no server knowledge.

---

## 5. Stage B — oblivious top-*k*

Input: `⟨score⟩ ∈ Z_{2^ℓ}^n` held as shares. Output: `⟨T⟩`, shares of `k` indices.

Everything here must be **data-independent**: the same instruction sequence and the same
network trace regardless of the values. This is the layer where the project's intellectual
content lives.

### 5.1 Comparison primitive (`src/topk/compare.hpp`)

A single swappable interface, three implementations, benchmarked against each other:

| ID | Implementation | Cost | Status |
|---|---|---|---|
| `CMP_GC` | Garbled-circuit / GMW bit-decomposition comparison | `O(ℓ)` rounds or `O(ℓ)` AND gates | Baseline. Ship first. |
| `CMP_DPF` | Grotto-style (2,2)-DPF comparison over `Z_{2^n}` | 1 round, small keys | D7.2. The "speaks the course's language" version. |
| `CMP_PLAIN` | Plaintext, no privacy | free | Correctness oracle in tests only. |

The interface is fixed now so W3 can build selection while W1 builds `CMP_DPF`:
```cpp
class Comparator {         // returns ⟨1⟩ if a > b else ⟨0⟩, as a shared bit
  virtual SharedBit gt(SharedVal a, SharedVal b) = 0;
  virtual void      flush() = 0;   // batches, for round efficiency
};
```
**`flush()` exists because round count dominates on WAN.** All comparisons at one level of a
sorting network are independent and must be issued as a single batch.

### 5.2 Selection algorithms

| ID | Algorithm | Comparisons | When it wins |
|---|---|---|---|
| `SEL_SORT` | Batcher bitonic sort, take first `k` | `O(n log²n)` | Never the best, but simple and obviously oblivious. The baseline. |
| `SEL_TOURN` | Oblivious tournament / bitonic top-*k* | `O(n + k log n)` amortised | The default. Small `k` is our regime. |
| `SEL_HEAP` | PRAC-style oblivious heap over a DORAM | `O(k log n)` after `O(n)` build | D7.3. Only pays off at large `n`; **the crossover graph exists to find out where.** |

`SEL_SORT` and `SEL_TOURN` both scan all `n` entries — they are *index-free*. `SEL_HEAP` keeps
an index and therefore needs a DORAM. That contrast is the thesis of the whole report:
*this algorithm is easy in the clear and hard under privacy, and the reason is data-dependent
memory access.*

### 5.3 Oblivious swap

Everything reduces to: given shared bit `c` and shared values `a, b`, produce
`(c ? b : a, c ? a : b)` without branching. One multiplication triple per swap, precomputed
offline. The offline/online split is measured separately (§8).

---

## 6. Stage C — private delivery

The top-*k* indices come out of Stage B as shares `⟨T⟩`. Two options were considered:

- **Rejected:** have the servers perform a shared-index DORAM read into `M`. Correct, but it
  requires Duoram-style shared-input reads and buys nothing, because the client is allowed
  to know `T`.
- **Chosen:** servers send `⟨T⟩_b` to the client; the client reconstructs `T` locally, then
  issues `k` fresh DPF-PIR reads against the metadata table `M`. Simple, one extra round,
  and the servers still never see `T`.

Metadata records are **fixed width `L = 256` bytes**, zero-padded. Variable-width records would
leak title length through the response size. This is the kind of detail that separates a
system from a demo; it goes in the report.

---

## 7. Threat model and leakage profile

> This section is a **graded deliverable** (D5), not documentation. Most groups will not write
> one. It costs one page and it is the strongest available signal of maturity.

### 7.1 Adversary classes

| Adversary | Capability | Our guarantee |
|---|---|---|
| **A1. Semi-honest server (one of two)** | Follows the protocol, reads everything it sees | **Full profile privacy.** DPF key pseudorandomness ⇒ the transcript is simulatable from `(n, m̄, k)` alone. This is the headline claim. |
| **A2. Both servers colluding** | Pool transcripts | **No guarantee.** Shares reconstruct. This assumption is the price of DPF efficiency and we state it in bold, everywhere. Route A2 (single-server PIR baseline) exists precisely to price this assumption. |
| **A3. Malicious client** | Sends malformed DPF keys | Correctness of *its own* output is lost (it only hurts itself); but a flood of invalid keys is a **DoS vector** on full-domain evaluation. Mitigation is a Sabre-style logarithmic audit — D7.4, stretch. Unmitigated in the base system, and we say so. |
| **A4. Malicious server** | Deviates, e.g. supplies a poisoned `S` | **Broken, interestingly.** See §7.3. This is D7.1 and our novelty candidate. |
| **A5. Network observer** | Sees ciphertext sizes and timing | Sees `m̄`, `k`, `n`, query count, query times. All are query-independent constants by construction — *except* query timing. |

### 7.2 Leakage profile (semi-honest, non-colluding)

**Provably hidden:** profile item IDs, profile ratings, actual profile size `m`, the score
vector, the top-*k* indices, the retrieved metadata.

**Leaked, by design:** `n` (public catalogue size), `k` (public), `m̄` (public padding
constant), the number of queries a client makes, the wall-clock time of each query, and the
fact that a query occurred at all.

**Leaked, and we should be honest about it:** query *timing correlation*. If a client queries
immediately after a public event, that correlation is outside the protocol's protection.
We do not claim to fix this.

### 7.3 The interesting failure: leakage in the ideal functionality

`F_rec` (REQUIREMENTS §4.2) is defined over a similarity matrix `S` **supplied by the servers**.
A malicious server can choose `S` adversarially. Construct `S` so that item `x`'s row
recommends canary item `c`, and no other row does. Then:

> `c ∈ T` ⟺ the client's profile contains `x`.

The protocol leaks nothing — but the client *acts* on the recommendation, and that action
(a click, a watch) is observable. **No amount of better cryptography fixes this, because the
leakage is in the functionality, not in the protocol.** This is structurally the same point
Asharov et al. make about genomic search and PICS makes about contact discovery, arrived at
independently. It is a real recurring theme, not a contrived framing.

**Defence (D7.1):** publish a Merkle commitment to `S`, have clients verify a random subset of
rows via a separate audit query, and bound the number of adversarial rows a server can plant
before detection. Measure the audit cost. This is the strongest single contribution available
to us.

---

## 8. Evaluation architecture (`bench/`)

```
bench/
  scripts/run_sweep.py       # drives the whole matrix, writes JSONL
  scripts/netem.sh           # applies the four network profiles inside Docker
  scripts/make_figures.py    # JSONL → every figure in the report
  results/*.jsonl            # raw, committed, append-only
  figures/*.pdf              # generated, gitignored
```

**Rule:** figures are never hand-edited and never produced outside `make figures`. Raw JSONL is
committed so results survive a machine change; figures are not.

Every measurement records: `{git_sha, host, profile, n, m, k, backend, selector, comparator,
phase, wall_ms, cpu_ms, bytes_up, bytes_down, timestamp}`. Phase is one of
`{offline, stage_a, stage_b, stage_c}` so the microbenchmark breakdown falls out for free.

**Network profiles** (`netem.sh`): `local` (no shaping), `lan` (1 ms, 1 Gbps),
`wan_a` (30 ms, 100 Mbps), `wan_b` (100 ms, 10 Mbps). Every headline number is reported on
`wan_a` as well as `local`, because the ranking of the backends is expected to change between
them, and that change *is* the result.

---

## 9. Repository layout

```
include/oblivrec/     public headers, the API surface between workstreams
src/
  common/             ring arithmetic, fixed-point, serialisation, PRNG
  dpf/                W1 — GGM tree, AES-NI PRG, Gen/Eval/EvalFull
  pir/                W1 — two-server read layer, Stage A aggregation
  topk/               W3 — comparators, selectors, oblivious swap
  net/                framing, batching, the flush() machinery
  apps/               client, server0, server1, demo CLI
model/                W2 — Python offline pipeline + quality eval
bench/                W4 — sweeps, netem, figures
tests/                unit + end-to-end; CMP_PLAIN is the oracle
docs/                 report sources, threat model, lit survey
scripts/              fetch_data.py, dev setup
third_party/          MP-SPDZ / SimplePIR pinned as submodules (baselines only)
data/                 gitignored, downloaded
```

**Interface discipline.** Workstreams touch each other only through `include/oblivrec/`.
W3 must be able to build against a stub `Comparator` before W1 finishes `CMP_DPF`. Header
changes require a heads-up in the team channel — this is the main source of merge pain in a
4-person, 12-week project.

---

## 10. Decisions log

Amendments go here with a date and a reason. Do not silently change the body of this file.

| Date | Decision | Reason |
|---|---|---|
| 2026-08-15 | Scope = private retrieval, not private training | Avoids direct competition with PIRSONA; avoids the MPC fixed-point time sink. REQUIREMENTS §3. |
| 2026-08-15 | Two-server DPF model as headline; single-server PIR as baseline | DPFs are the spine of the course; the baseline prices the non-collusion assumption honestly. |
| 2026-08-15 | `Z_{2^64}` ring, `f = 20` fixed-point bits | Native arithmetic; matches Grotto/Duoram. Headroom verified in §4.3. |
| 2026-08-15 | Fixed-width 256-byte metadata records | Variable width leaks title length via response size. |
| 2026-08-15 | Client reconstructs `T`, then re-queries for metadata | Simpler than shared-index DORAM reads and loses nothing, since the client may learn `T`. |
