# Grotto — Screaming fast (2+1)-PC for ℤ_{2ⁿ} via (2,2)-DPFs

**Kyle Storrier (Calgary) · Adithya Vadapalli (Waterloo) · Allan Lyons (Calgary) · Ryan Henry (Calgary)** ·
CCS 2023 · PDF: `references/Grotto.pdf` · BibTeX key: `grotto2023`
**Read by:** Mainak · **Track:** T1 · **Date:** 2026-08-22 · **Status: [VERIFIED]** — pp. 1–8, 13–16

> ⚠ **Instructor's paper #3.** Note the affiliation drift: *Waterloo* here (CCS'23), *IIT Kanpur*
> by PRAC (PoPETs'24). Same person. That makes Grotto, Duoram, PRAC and PIRSONA all his.

> **One-sentence summary.** A C++ library for evaluating **piecewise-polynomial (spline)
> approximations of non-linear functions** on additively-shared secrets, whose central trick is a
> structural observation about the DPF tree plus a **prefix-parity algorithm** that lets **one DPF**
> do the work that state-of-the-art approaches need **many distributed comparison functions** for.

---

## 1. Problem

MPC is good at linear algebra and bad at everything else. The recurring pain is **evaluating
non-linear functions inside a linear secret-sharing scheme** — activation functions in private
neural networks, `sqrt`, `1/√x`, logarithms, sign tests. Early systems (SecureML) dodged it with
"MPC-friendly" knock-offs of the real functions, trading accuracy for tractability.

The standard modern answer is a **distributed comparison function (DCF)** — a DPF-adjacent
primitive specialised for comparisons. Evaluating a `P`-piece spline costs `O(P·lg N)` in both
communication and computation that way. **Grotto gets communication down to `O(lg N)` and
computation to `o(P·lg N)`**, with smaller hidden constants.

## 2. Threat model

- **`(2+1)`-party**, a.k.a. **server-aided 2PC**. `P0` and `P1` hold the additive shares and do the
  computation; **`P2` is a semi-trusted third party that supplies correlated randomness in a
  preprocessing phase and does not participate online.**
- **Semi-honest.** (Grotto §9 notes Wagh's *Pika* generalised the predecessor technique to the
  fully malicious setting; Grotto itself does not.)
- Sharings: 64-bit `(2,2)` additive over `ℤ_{2^64}`, `(2,2)`-XOR, Beaver triples for multiplication.
  In `(2+1)`-party computation, well-formed Beaver triples are provided **"for free"** by the
  semi-honest third party.

> ⚠ **This is a THIRD distinct party structure, and it matters for our composition:**
> | System | `P0`, `P1` | `P2` |
> |---|---|---|
> | **Nudge** | hold input shares | **also holds input shares** |
> | **PRAC** | hold data shares | no input, **participates online** |
> | **Grotto** | hold shares, compute | no input, **offline dealer only** |
>
> Three papers, three roles for the third party. **This sharpens the open question already flagged
> in `doram.md` §7.3** — it is not just "does PRAC's `P2` clash with Nudge's third server", it is
> "which of three incompatible `P2` conventions does our system adopt". **Ask him.**

## 3. Technique

### 3.1 Selection vectors and the rotation trick (§3)
A **selection vector** `ê_i` is length-`N`, all zeros but a single 1 at position `i`.

> **Observation 1.** *All selection vectors of a given length are equivalent up to cyclic
> rotation:* `ê_j = ê_i ≫ (j − i)`.

That yields a `(2+1)`-party **scalar-to-selection-vector conversion** (Fig. 1): in preprocessing
`P2` picks a uniform `i` and hands `P0`, `P1` shares of `[i]` and `[ê_i]`. Online, the two parties
reconstruct `(j − i) mod N` using linearity alone —

```
(j − i) mod N  =  ([j]₀ − [i]₀)  +  ([j]₁ − [i]₁)   mod N
```

— then each **locally cyclically rotates** its share of `ê_i` by that amount. Since `i` is uniform,
`(j − i)` **perfectly hides** `j`.

> **This is the same shape as Duoram's online phase** — precompute at a random index, exchange one
> offset, rotate locally. Learning it once pays for both papers.

### 3.2 PIR from selection vectors — directly relevant to us
Encode a lookup table as a length-`N` vector `P⃗`; then `⟨ê_j, P⃗⟩ = P_j`. The paper says the quiet
part out loud:

> *"Astute readers may recognize this procedure as a variant of 2-server **private information
> retrieval** (PIR) over `P⃗` in which the 'client' `P2` pre-distributes random queries to 'servers'
> `P0` and `P1` in an offline phase."*

Two equivalent implementations (Fig. 2): rotate the **selection vector right**, or rotate the
**lookup table left** by the same distance — inner products are invariant under cyclic reordering.

**Optimisation worth stealing:** where `f` is *constant on an interval*, apply the distributive law.
A step function over `N = 8` collapses from `N−1` additions and `N` scalar multiplications to
**`N−1` additions and just 2 scalar multiplications**.

**Binary selection vectors** shave a factor of `⌈lg N⌉` off share size, but the bits must be
*lifted* into `ℤ_N`. Grotto's trick (Eq. 3): lift bit `b` to `0` or `(−1)^b`, so the inner product
yields `±P_j` — **correct up to sign** — and defer sign correction to post-processing. This works
*only because* a selection vector has exactly one non-zero entry.

### 3.3 Parity-segment trees (§4) — the actual contribution
A **parity-segment tree** `T(x)` answers **parity queries over substrings** of a binary string in
**`O(lg N)`** worst case: for length-`N` bitstring `x`, `parity(x[a..b]) = ⊕_{i=a}^{b−1} x_i`.

- Layout: `N = 2^{n+k}`; each **leaf holds `λ = 2^k` consecutive bits**, storing that substring's
  parity; each internal node stores the XOR of its two children.
- Build cost: **`O(N)` bit operations**; the tree itself occupies `2^{n+1} − 1 = O(N/λ)` bits.

The **prefix-parity algorithm** computes segment parities via **prefix** parities sharing the same
right endpoint, then recovers segments by XOR's nilpotency (`parity(seg) = parity(pre_end) ⊕
parity(pre_start)`). Traversal:

1. running parity `:= 0`
2. walk root → leaf containing the prefix's rightmost bit
3. **whenever the path changes direction, XOR in the parity stored at that node**
4. XOR in any prefix bits inside the leaf's own substring

Plus **memoization** (each node visited at most once across all queries) and an
**early-termination** rule.

> **Theorem 1.** With height `n` and `S` sorted distinct prefix endpoints, the algorithm traverses
> at most `S·n − Σ_{i=2}^{S} ⌊lg(i−1)⌋` edges — i.e. **`o(S·n)`**. As `S → N`, amortised cost per
> prefix tends to `2^{k+1}`.

> **Theorem 2.** For a uniform random endpoint, early termination saves `(2 − 2^{−n})/λ`
> traversals in expectation.

### 3.4 Point functions (§5)
A binary point function's truth table *is* a selection vector. Represented as a height-`n` binary
tree whose `2^n` leaves partition `ê_i` into `λ`-bit segments. Grotto extends the **Sabre**
(Vadapalli–Storrier–Henry) node taxonomy:

- **1-leaf** holds a `λ`-bit selection vector; **0-leaf** holds a `λ`-bit zero vector. Exactly one
  1-leaf exists.
- **Observation 2:** parity of the 1-leaf's vector is `1`; every 0-leaf's is `0`.

That is the bridge: **parity structure over the DPF tree is what lets one DPF replace many DCFs.**

## 4. Results

**Implementation:** **C++ library**. Uses `dpf++` for `(2,2)`-DPFs, **GMP 6.2.1** for multi-limb
ABY2.0-style multiplication, **ALGLIB 3.19.0 (C++)** for curve fitting in LUT generation.
**65 gadgets** out of the box — trig/hyperbolic and inverses, logarithms, roots, reciprocals,
reciprocal roots, sign testing, bit counting, 24+ deep-learning activations.

**Benchmarks:** single workstation, 16 GiB RAM, Intel Core i7-9700K, Ubuntu 18.04, 100 trials,
single-threaded. **Network time excluded deliberately** — Grotto's runtimes are 4–5 orders of
magnitude below typical Internet latency, so it could not be separated from network variance.

Table 2, vs **LLAMA** (Gupta et al., PoPETs 2022) — selected rows:

| Function | Scheme | bits | Preproc comp | Preproc comm | Online comp | Online comm | Rounds |
|---|---|---|---|---|---|---|---|
| isqrt | LLAMA | 16 | 28 ± 4 µs | 11.37 KiB | 60 ± 10 µs | 36 B | 3 |
| isqrt | **Grotto** | 16 | **2.65 µs** | **0.38 KiB** | **3.1 µs** | 74 B | 3 |
| isqrt | **Grotto** | **64** | 4.61 µs | 1.32 KiB | 78 µs | 152 B | 3 |
| tanh | LLAMA | 16 | 31 ± 6 µs | 13.22 KiB | 60 ± 10 µs | 36 B | 3 |
| tanh | **Grotto** | **64** | 4.55 µs | 1.32 KiB | **17 µs** | 152 B | 3 |
| sigmoid | LLAMA | 16 | 60 ± 10 µs | 33.05 KiB | 260 ± 60 µs | 36 B | 3 |
| sigmoid | **Grotto** | **64** | 4.61 µs | 1.32 KiB | **21 µs** | 152 B | 3 |

- Preprocessing time and space are **a fraction** of LLAMA's (≈30× less preprocessing communication).
- Online compute is consistently lower — **Grotto at 64-bit beats LLAMA at 16-bit** on two of three
  common gadgets.
- **Honest trade-off, stated by the authors:** Grotto's **online communication is higher**
  (74 B / 152 B vs 36 B) because it uses cubic rather than quadratic polynomials and lifts shares
  to a larger ring. What that buys is accuracy — **Grotto's max error is below what the fixed-point
  representation can express, whereas LLAMA tolerates up to 4 ULPs**.
- Round complexity **ties** LLAMA at 3, and **cannot beat DCFs' non-interactive evaluation**.

## 5. Stated limitations

- **Semi-honest `(2+1)`-party only.** Malicious security is Wagh's *Pika*, not this.
- **Higher online communication than DCF approaches** — acknowledged, and traded for accuracy.
- **Cannot match DCFs' non-interactive evaluation** on round complexity.
- LUT generation from a genuine black-box `f` is impossible to fully automate; their utility needs
  "hints", making it *graybox* rather than blackbox LUT generation.

## 6. Relation to our project

### What we take
1. **The `(2+1)` PIR-from-selection-vectors construction (§3.2)** is a working blueprint for our
   delivery layer, from the instructor's own group, in C++.
2. **The rotation trick (Observation 1)** — precompute at a random index, exchange one offset,
   rotate locally — is the shared spine of Grotto's PIR *and* Duoram's online phase. One idea,
   two papers.
3. **Comparison / sign-testing gadgets** for the oblivious tournament in Layer 2 (safe path).
   PRAC already uses "the oblivious compare due to Storrier, Vadapalli, Lyons, and Henry" — i.e.
   this paper. So Grotto sits *underneath* PRAC in our stack, not beside it.
4. **The constant-interval optimisation** — if our score vector is bucketed or quantised, the
   distributive-law shortcut applies directly.

### The PIRSONA link, from Grotto's own related work (§9)
> *"Vadapalli, Bayatbabolghani, and Henry [31] used the spline evaluation via selection vectors
> approach … with the selection vector shares compressed as DPF shares, to implement both
> piecewise-linear approximations for the reciprocal square root (isqrt) function and a
> piecewise-constant exact comparison (leq) for 16-bit fixed-point numbers in the semi-honest
> (2+1)-party setting."*

**Reference [31] is PIRSONA.** So Grotto is the direct modern successor to a technique PIRSONA
already used — and Grotto notes the older approach was capped at `ℤ_N` for small `N`, with "typical
sizes … around 20–25 bits". **That is a concrete, citable statement of what PIRSONA's machinery
could not do and Grotto can** — useful for §4/§6 of the survey.

### What we question
- **The main body is about spline evaluation, which we do not obviously need.** Our pipeline needs
  *comparison* and *PIR*, not `tanh`. Be honest in the report: we use Grotto's **substrate**
  (selection vectors, PIR, comparison), not its headline contribution.
- **One exception worth noting:** Grotto's flagship gadget is `isqrt` — and inverse square root is
  exactly what **Nudge's normalization** needs. We vendor Nudge, so we do not implement it; but
  "the instructor's library has a fast gadget for the operation the state-of-the-art private
  recommender spends its rounds on" is a genuinely interesting observation for the report, and
  possibly a question for him.
- **Network time is excluded from every number in Table 2.** Our evaluation must not inherit that
  convention — we report with the network profile attached.

### Which of our sections it belongs in
§3 (primitives — the DPF/selection-vector substrate), §5 (private retrieval — the PIR construction),
§6 (systems — the PIRSONA→Grotto lineage).

## 7. Open questions

1. **Which `P2` convention do we adopt?** Three papers, three roles (see §2). This is now the
   sharpest architectural question in the project and it blocks Layers 2(b) and 3. **Top of the
   list for the meeting.**
2. **Is the Grotto C++ library public and does it build?** It would give us comparison and a PIR
   substrate for free, in our language. **Check alongside the Duoram repo.**
3. **Do we need Grotto at all, or only via PRAC?** PRAC already uses Grotto's compare internally.
   If we take PRAC, we may get Grotto transitively and never call it directly — which changes what
   we claim to have built. Decide before writing any bullet that says "used Grotto".
4. **Does the `o(S·n)` amortisation help us?** Theorem 1's savings grow as the number of prefix
   endpoints `S` grows. Our top-`k` does `k` comparisons over `n` items — is that a regime where
   the memoization pays, or are our queries too sparse to amortise?
