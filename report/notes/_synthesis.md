# Synthesis — the argument the survey is built to make

**Status:** draft, written 2026-08-15 before the reading was done.
**This document is decided at the synthesis meeting on 23 August, not before.** Everything
below is a hypothesis for the group to attack. If the reading kills it, that is the reading
doing its job — say so at the meeting rather than writing around it.

---

## The thesis in one paragraph

Private recommendation has two halves — **training** a model on users' private ratings, and
**delivering** the recommended item without revealing what was delivered. These are two
mature literatures that have developed almost entirely in parallel. [PIRSONA] (2021) is close
to the only system that spans both, and it predates the entire modern private-retrieval line.
[NUDGE] (2026) is the state of the art on training and **explicitly delegates the fetch to
"other means"**. Nobody has plugged current private-retrieval machinery into a privately
trained recommender. That is the gap.

## The two literatures

```
  TRAINING                                    RETRIEVAL
  Nikolaenko et al. CCS'13  (garbled)         SANNS         USENIX Sec'20
        │                                     Tiptoe        SOSP'23   (linear scan)
  PIRSONA  PoPETs'21  (4PC + PIR) ◄───────┐   Pacmann       2024      (sublinear, 614MB client)
        │                                 │   Compass       NSDI'24   (ORAM + HNSW)
  NUDGE    USENIX'26  (3PC power iter)    │   Wally         2024      (DP breaks linear barrier)
        │                                 │   Panther       CCS'25    (single-server)
        └── "relies on other means" ──────┘   MESS          Jul 2026  (multi-graph HNSW)
                     ▲                        P²RAG         2026      (arbitrary top-k)
                     └──────── THE GAP ───────────────────────────────┘
```

The right-hand column barely existed when [PIRSONA] was written. The left-hand column's best
system points at the right-hand column and walks away.

## Three claims to defend, in decreasing confidence

**C1 — The literatures are disconnected.** *Evidence so far:* the only prior work naming both
training and serving is `PrivateRec` (arXiv 2204.08146), and it is DP + federated, not MPC +
PIR. **To verify:** a forward-citation sweep of [NUDGE] and Tiptoe. If someone has already
done this, we need to know by 23 Aug, not in October.

**C2 — The obvious architecture is wrong for our parameter regime, and that is interesting.**
[NUDGE] publishes `B` in the clear and the user holds its own ratings, so scoring is a *local*
computation once `B` is downloaded — ~800 KB at MovieLens scale. No cryptography needed. But
`B` is `d×n`, so at Criteo scale (hundreds of millions of items) downloading it is absurd and
the private-ANN machinery becomes necessary. **The crossover is unmapped.** Drawing that
boundary — local download vs private ANN vs PIR fetch, as a function of `n`, `d`, and client
bandwidth — is cheap, honest, and unclaimed. → **N1**

**C3 — Public `B` is a quantifiable leakage surface.** Every server holds a complete
latent-factor model of the catalogue. So an observed fetch is not one bit of information about
a user; it is a projection onto a known basis, and a handful of fetches pins down the taste
vector. Concretely: *given public `B` and `j` observed fetches, reconstruct `â⁽ⁱ⁾` by least
squares and measure `cos(â⁽ⁱ⁾, a⁽ⁱ⁾)` as a function of `j`.* This is the quantitative argument
for why the delivery layer must be private — it turns our contribution from an engineering
choice into a demonstrated necessity. Neither base paper does it. → **N2**

## Why this is safe ground

- We are **not attempting to beat either paper**. [NUDGE] is a 2026 USENIX Security paper from
  MIT/Stanford; [PIRSONA] is the instructor's. Composing them and measuring the composition is
  defensible in a way that a marginal speedup claim is not.
- **C2 and C3 are analysis, not systems engineering.** They are cheap enough to complete even
  if the implementation half runs late, and they are the parts most likely to read as
  research-grade.
- The gap we target is one the authors **stated themselves**. An author-acknowledged
  limitation is a far safer target than one we think we spotted.

## What would kill this thesis

Note these honestly — a survey that only looks for confirming evidence is worthless.

- Someone has already composed a private-ANN backend with a private-MF recommender. *(Check:
  forward citations of [NUDGE], Tiptoe, Pacmann.)*
- The crossover in C2 turns out to be trivially far away — i.e. `B` is downloadable at *every*
  realistic catalogue size — which would make N1 a one-line observation rather than a result.
- The C3 reconstruction turns out to be obvious or already folded into the standard
  embedding-inversion literature.
- The instructor wants a straight reimplementation of one paper rather than a composition.
  **This is a real possibility — it is question Q2 for the meeting.**

## What the survey must therefore establish, section by section

| § | Must establish |
|---|---|
| 2 | That "private" means several incomparable things (MPC / DP / federated / TEE), so claims across papers cannot be compared naively |
| 3 | That FSS is the primitive making both halves tractable — the same DPF is a comparison gate *and* a PIR read |
| 4 | The training line, ending at [NUDGE] as state of the art, and why power iteration is a *cryptographic* choice |
| 5 | The retrieval line, and how much it has moved since 2021 |
| 6 | That [PIRSONA] is nearly alone in spanning both, and is now dated on the training side |
| 7 | That this literature's evaluation norms (WAN, baselines, microbenchmarks) are what we will be held to |
| 8 | **The gap, C1–C3, and N1/N2 as what we will deliver** |
