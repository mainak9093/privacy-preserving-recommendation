# PIRSONA — You May Also Like… Privacy: Recommendation Systems Meet PIR

**Adithya Vadapalli, Fattaneh Bayatbabolghani, Ryan Henry** ·
*Proceedings on Privacy Enhancing Technologies* 2021(4):30–53 · DOI 10.2478/popets-2021-0059 ·
[petsymposium.org](https://petsymposium.org/popets/2021/popets-2021-0059.php) ·
PDF: `references/Recommendation System PIR.pdf` · BibTeX key: `pirsona2021`
**Read by:** *(all — mandatory)* · **Date:** 2026-08-15

> **One-sentence summary.** A content-delivery system in which users fetch items by
> multiserver PIR, the servers harvest secret-shared consumption histories *directly out of
> those PIR queries*, and a bespoke 4PC Boolean matrix factorization periodically turns those
> histories into a collaborative-filtering model — so recommendations improve without anyone
> learning what anyone watched.

> ⚠ **This is the instructor's own paper.** He sent it first. Read it properly; a shallow
> reading will be visible in the viva.

---

## 1. Problem

Digital content distributors (Netflix, Kindle, app stores) accumulate fine-grained
consumption-pattern data. That data is genuinely valuable for recommendations *and* genuinely
dangerous — the paper is blunt that it is invaluable to "unscrupulous marketers, identity
thieves… and hostile foreign agents".

The tension the paper names in its own words: PIR lets users fetch items while hiding *which*
items, whereas collaborative filtering works by making predictions about a user from the
interests of like-minded users. The two goals appear **fundamentally at odds**. PIRSONA's
contribution is to show they are not.

## 2. Threat model

- **`s+1` servers**, each holding a complete replica of the database `D` (an `r × s` matrix
  over `GF(2^w)`; `r` records of `s` words of `w` bits).
- **Pairwise non-colluding** — privacy requires that no two servers collude.
- Semi-honest for the CF part; the PIR is **computationally 1-private**, i.e. it protects
  against a single *malicious* server.
- Users maintain **no local state** and need not disclose anything to third parties or
  participate actively in training. That last point is a real design achievement and worth
  contrasting with federated learning.

## 3. Technique

Three components, and the paper's own framing is that it uses MPC to "glue" the first two
seemingly antithetical primitives together.

**(a) Hafiz–Henry PIR** (PoPETs 2019.4). Two variants:
- *Perfectly 1-private*: the user samples a template vector `q̄ ∈ [0..s]^r` uniformly subject to
  `Σ q̄[j] = 0`, picks a uniform permutation `σ: [0..s] → [0..s]`, and sends
  `query_k = q̄ + σ(k)·ē_j` to server `P_k`. Each server replies with a **single scalar** in
  `GF(2^w)`. The permutation forces independence between the query and the index.
  Meets known lower bounds for download and server-side computation.
- *Computationally 1-private*: replaces the explicitly-sent query vectors with
  **(2,2)-DPFs**, cutting per-server upload from `Θ(r lg s)` to `Θ((lg r)(lg s))` with no
  change to download cost. Cleanest when `s+1 = 2^L`; the user sends an `L`-tuple of DPF seeds
  and each server runs `EvalFull` to reconstruct its query vector.

**(b) The harvest loop — the idea worth stealing.** The servers do not need a separate ratings
upload. The incoming PIR query *already encodes* which record the user wants, in secret-shared
form, so the servers extract per-user secret-shared consumption histories straight from the
fetch traffic. Training data arrives for free as a by-product of delivery.

**(c) 4PC Boolean matrix factorization**, run periodically over those shared histories to
produce still-secret-shared item and user profiles, which then drive oblivious personalised
recommendations.

**New primitives contributed along the way** (§1.2), several of which we may want:
4PC fixed-selection wire MUXes and DeMUXes; fast 3PC vector normalization for secret-shared
fixed-point vectors; **4PC well-formedness tests for (2,2)-DPFs**; and one-round 3PC integer
comparison.

## 4. Results

*(To fill in on a close read of §6 — record dataset, party count, network, and wall-clock per
training round. Do not carry numbers over from memory.)*

## 5. Stated limitations

- The paper is explicit that it opted for "the most performant primitives available **(at the
  price of rather strong non-collusion assumptions)**". Four parties, pairwise non-colluding,
  is a heavy assumption and the paper does not hide it.
- 4PC Boolean matrix factorization is expensive. [NUDGE] reports beating a bespoke four-party
  protocol "while using 8× less communication and one fewer non-colluding party" — that is
  this paper.

## 6. Relation to our project

- **What we take:** the **end-to-end shape** — private delivery *and* private training in one
  loop — and specifically the **harvest idea** (§3b), which no other system does and which
  makes the cycle self-sustaining. Also the DPF-compressed PIR query structure.
- **What we question:** the 4PC training core, superseded by [NUDGE]'s 3PC power iteration;
  and the strength of the pairwise non-collusion assumption.
- **Belongs in:** §4 (private training) and §6 (systems spanning both halves). It is close to
  the *only* entry in §6.
- **Also read:** Hafiz & Henry (PoPETs 2019.4); Boyle–Gilboa–Ishai FSS papers; Du–Attalah 3PC
  multiplication.

## 7. Open questions

- The harvest loop assumes a fetch implies a positive rating. How is an explicit *negative*
  rating expressed? Does the model conflate "watched" with "liked"?
- The well-formedness test for (2,2)-DPFs — how does its cost compare to the Sabre-style
  logarithmic audit, and would we need it at all in a 3PC setting?
- **For the instructor meeting:** is the harvest loop the part he would most want to see
  carried forward onto a modern substrate?
