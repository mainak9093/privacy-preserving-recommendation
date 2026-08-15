# NUDGE — Nudge: A Private Recommendations Engine

**Alexandra Henzinger, Emma Dauterman, Henry Corrigan-Gibbs, Dan Boneh** ·
*35th USENIX Security Symposium*, 12–14 Aug 2026, Baltimore ·
[usenix.org](https://www.usenix.org/conference/usenixsecurity26/presentation/henzinger) ·
full version [eprint 2026/179](https://eprint.iacr.org/2026/179) ·
artifact [NudgeArtifact/private-recs](https://github.com/NudgeArtifact/private-recs) (MIT) ·
PDF: `references/Nudge A Private Recommendations Engine.pdf` · BibTeX key: `nudge2026`
**Read by:** *(all — mandatory)* · **Date:** 2026-08-15

> **One-sentence summary.** Because a shared-matrix × shared-vector product is *free* under
> 2-of-3 replicated secret sharing, recasting matrix factorization as **power iteration**
> rather than gradient descent makes nearly the whole of private recommender training
> non-interactive — leaving only truncation and normalization to pay for, both handled with
> function secret sharing.

---

## 1. Problem

Recommender data — clicks, likes, views, browsing — is resold, breached, and de-anonymised.
Existing answers all fall short at scale: federated recommenders leak through their logs or
intermediate model versions; trusted-hardware designs are vulnerable to side channels; and
prior *cryptographically* private recommenders "run into a problem of scale: they operate on
at most thousands of users each fetching thousands of items."

## 2. Threat model

- **Exactly 3 servers**, run by independent parties. Real-world precedent cited: the Prio
  deployment for exposure notification (CDC and ISRG as the non-colluding entities).
- **2-out-of-3 replicated secret sharing** over `R = Z_{2^b}`; `x = x₀+x₁+x₂`, party `i` holds
  the pair `(x_i, x_{i+1})`.
- **Semi-honest, honest majority.** Security holds against an adversary compromising the
  **entire secret state of one server**, plus arbitrarily many malicious users.
- **"3PC with PRF" model:** each *pair* of parties shares a PRF key, so correlated randomness
  (zero-shares) is generated with **no communication**.
- **Public by design:** the item embedding matrix `B`. Also leaked: how many items each user
  rated (removable by requiring a constant number of ratings, at a utility cost).

## 3. Technique

**The central fact (Thm 4.2).** For a *matrix-vector program*
`P(v, M₁…M_ℓ) := f_ℓ(M_ℓ · … · f₂(M₂ · f₁(M₁·v)))`, three parties holding replicated shares can
evaluate it with communication scaling **only with the largest intermediate vector, not with
the size of the input matrices**. When the matrices dominate, per-party work is only 3× that
of the cleartext product. Rounds scale with the number of *non-linear* stages.

**Why power iteration.** It alternates exactly two things: (a) apply a secret-shared matrix to
a secret-shared vector — non-interactive under replicated sharing — and (b) evaluate simple
non-linear functions on the intermediate result — cheap under FSS. Gradient descent has no
such structure. This is a *cryptographic* argument for an *algorithmic* choice, and it is the
single most quotable idea in the paper.

**The algorithm** (Fig. 4). `ApproxFactor(U) → (A, B)`:
```
for i in 1..d:
    v := Normalize(SetOrthogonal(random, B))
    for j in 1..ℓ:
        v := Mul(Uᵀ, Mul(U, v))     # free
        v := SetOrthogonal(v, B)    # Gram-Schmidt against PUBLIC rows
        v := Normalize(v)           # interactive
    B[i] := v                       # ← OPENED IN THE CLEAR
A := U · Bᵀ
```
Opening each converged row of `B` before computing the next is the design's load-bearing
trick: `SetOrthogonal` becomes Gram–Schmidt against *public* vectors. The authors note this
"leverages power iteration's structure… aiding the computation while incurring no additional
leakage" — because `B` is an output anyway.

**The two non-linear protocols** (§4.3), which are the entire interactive cost:
- **`Trunc_t`** — deterministic truncation with sign extension. Divide by `2^t`, then fix
  low-order carries with an integer comparison (Escudero et al.) and prevent high-order
  carries with one bit of slack. **3 rounds, `2dt(λ+4) + 10db` bits.** The leading term is
  `λt`, not `λb` as in prior work — a `b/t` ≈ **6× communication improvement** at `b=λ=128, t=20`.
- **`ApproxNormalize`** — shares of `1/‖v‖`. `‖v‖²` is degree-two hence free; the hard part is
  inverse square root. Seeded via the **most-significant-non-zero-bit** of `‖v‖²`, obtained
  with `b+1` *simultaneous* integer comparisons in a single round — avoiding both the `O(b)`-round
  and the exponential-lookup-table routes — then refined by Newton–Raphson.
  **`O(1)` rounds, `O(dbλ + λb² + b³)` bits.**

**Protocol flow** (§3.2): (1) users secret-share their rating vectors to the three servers;
(2) the servers run the 3PC factorization; (3) recommendation scores for user `i` are
`a⁽ⁱ⁾·B`, computed as secret shares and forwarded to the user, who reconstructs in plaintext.

## 4. Results

- **Netflix**, 0.5M users, tens of thousands of items, `d=20`, `b=λ=128`, `t=20`,
  3 × 192-core servers on a **LAN**: **50 min**, **40 GB** server-to-server.
- Quality: **nDCG@20 = 0.29**, equal to non-private matrix factorization, against **0.31** for
  non-private deep neural recommenders.
- Serving: hundreds of KB per user (**298 KB** on Netflix), **0.38 s**.
- Beats garbled-circuit 2PC approaches by **four orders of magnitude** in communication and
  compute; faster than the bespoke 4PC (i.e. [PIRSONA]) with **8× less communication and one
  fewer non-colluding party**.
- Larger: Criteo 1.5 h / 124 GB; Yelp 8 h / 250 GB.

## 5. Stated limitations (§1, §3.1) — read these twice

1. **Requires three servers** and tolerates semi-honest compromise of one.
2. **The fetch gap.** Verbatim: *"Nudge's goal is to map user ratings into personalized
   recommendations; it relies on **other means** (e.g., Apple's private relay, Tor, or
   cryptographic private information retrieval) to let users fetch data items in a private way."*
3. **Metadata is not hidden** — servers learn when each user sends messages.
4. **No malicious-server security.** A deviating server can break correctness and availability.
5. Inherent to collaborative filtering: the model `B` reveals *something* about user behaviour,
   since users' inputs influence others' outputs. Mitigated (imperfectly) by DP in §9.

## 6. Relation to our project

- **What we take:** the training core, and the framing that algorithm choice is a cryptographic
  decision.
- **What we question — and this is our project:**
  - Limitation 2 is an **explicit, author-acknowledged gap**. It is the safest possible target.
  - **Public `B` is a leakage surface nobody has quantified.** Every server holds a complete
    latent-factor model of the catalogue, so one observed fetch is not one bit — it is a
    projection onto a known basis. → novelty candidate **N2**.
  - **`B` being public also undercuts the obvious architecture.** If `B` is public *and* the
    user holds its own `u⁽ⁱ⁾`, the user can download `B` (~800 KB at MovieLens scale) and
    compute top-*k* locally, with no cryptography. The paper says as much: for users who join
    after training, "it suffices to gather their ratings and use the already-computed item
    embeddings `B`." So elaborate oblivious top-*k* machinery is unnecessary **in this
    regime** — but not at Criteo scale. **Where is the crossover?** → novelty candidate **N1**.
- **Belongs in:** §4 (private training), §7 (evaluation methodology), §8 (the gap).

## 7. Open questions

- The artifact is **Go with AVX2/AES-NI assembly** and implements `dcf/`, `dmsb/`, `multdpf/`,
  the 3PC protocol and phase benchmarks. **Do we build on it or reimplement?** Deferred to the
  instructor meeting — it is the single biggest open decision in the project.
- `b = 128` is used because Netflix-scale `m` overflows 64 bits. At MovieLens scale, does
  `b = 64` suffice? Roughly 2× cheaper if so. → novelty candidate **N1(b)**.
- What exactly does `multdpf/` (multiplicative DPF for data collection) do, and how close is it
  to [PIRSONA]'s harvest loop?
- §9's DP mechanism protects `B`. What does it cost in nDCG, and does it also blunt the N2
  reconstruction attack?
