# OblivRec — threat model and leakage profile

**Status:** updated 2026-09-12. Covers **S1** (serving and delivery) and, as of Phase 3,
**S2** (private training) — see §7, which is new and includes a decision still open.

This promotes `design/ARCHITECTURE-draft-v1.md §9` to a standalone document and, where §9 posed a
question, replaces it with a measurement.

---

## 1. Setting

Three servers, `P0 P1 P2`, hold 2-of-3 replicated shares of each user's embedding `a`. The item
embedding matrix `B` is **public to everyone**, which is NUDGE's design and not an accident: each
converged row of `B` is opened before the next is computed, which is what keeps `SetOrthogonal`
cheap and makes the whole power-iteration approach affordable under MPC.

Serving is `scores = a · B`, a public matrix applied to a shared vector, so each server computes
its share locally with no communication. The user reconstructs the score vector, selects the top
`k` **on their own machine**, and then fetches each recommended record by two-server DPF-PIR
against `P0` and `P1`.

## 2. Adversary classes

| | Adversary | Capability | Our guarantee |
|---|---|---|---|
| **A1** | One semi-honest server | Follows the protocol, reads its own view | **Full user privacy**, up to §3. The headline claim. Matches NUDGE's model exactly. |
| **A2** | One semi-honest server + any number of malicious users | Above, plus colluding users | Honest users' ratings stay safe. Model *quality* is protected against weight inflation as of 2026-09-24 (§9.1), **but `d` colluding clients can still destroy the model** by forging keys that redistribute rather than inflate. See §9. |
| **A3** | **Two or more colluding servers** | Pool their views | **NO GUARANTEE. The shares reconstruct.** This is the assumption the entire system rests on, and it is stated in bold wherever the system is described. |
| **A4** | A malicious (deviating) server | Arbitrary deviation | **Out of scope**, as in NUDGE §3.1. It can corrupt correctness and availability. |
| **A5** | A malicious user | Malformed DPF keys | **Measured in §9, and it was worse than this row said.** Retrieval corrupts only its own output, but the HARVEST path accepted an unbounded weight until 2026-09-24 — one query at `beta=10⁶` cast a million-weight vote in the next model. Closed by a 1-round check (§9.2). Still open: `d` colluders forging redistribution keys destroy the model (nDCG 0.4536 → 0.0021), and the DoS on `EvalFull` is **unmitigated and measured at 121× at our catalogue size** (§9.3). A Sabre-style audit closes both; we have not built it. |
| **A6** | A network observer | Message sizes and timing | Sees public parameters and timing only. All message sizes are input-independent by construction — and, as of 2026-09-11, **measured**: over 1800 recorded queries every frame is 228 bytes whatever the record index, and a classifier on the raw wire bytes scores 0.490–0.500 against a 0.5 chance line. See §5. |

## 3. Leakage profile

**Hidden:** individual ratings; which items a user rated; the user embeddings `A`; the score
vectors; the top-`k` indices; **which records a user fetches**.

**Leaked by design:** the parameters `m, n, d, ell, k, b, t`; **the item embedding matrix `B`, in
the clear**; the number of non-zero entries in a user's rating vector (eliminable by requiring a
fixed rating count, at a utility cost — NUDGE §3.1); message timing and query counts.

**Leaked, and worth being honest about:** correlation between query timing and external events
sits outside the protocol's protection. We do not claim to fix it.

---

## 4. Why the delivery layer has to be private — measured, not argued

§9.3 proposed this experiment; `model/attack.py` now runs it, and it is the strongest result in
the project. Neither PIRSONA nor NUDGE measures it, because neither implements both halves.

Because `B` is public, **an observed fetch is not one bit of information — it is a projection onto
a known basis.** An adversary who sees *which* records a user fetched, and nothing else, can
reconstruct that user's private embedding:

| observed fetches `j` | `cos(â, a)` | share of the user's **next** 20 recommendations predicted |
|---|---|---|
| 1 | **0.55** | 38% |
| 2 | 0.66 | 47% |
| 5 | 0.77 | 53% |
| 10 | **0.84** | **56%** |
| 50 | 0.92 | 46% |

**The control is what makes this a result rather than an artefact.** Popular items sit near the
top of nearly everyone's ranking, so an estimator can look strong having learned only what is
public. So the identical estimator is run on the `j` globally most-popular items, ignoring the
user entirely — it reaches only 0.26 to 0.53, and **the attack's margin above it is a stable
+0.39 to +0.42 at every `j`**. That margin is the part that is genuinely about the individual. A
random-direction control sits at 0.003.

Two caveats we state ourselves:

- **A single user's cosine is not evidence.** The random control's spread is roughly ±0.18 (about
  `1/√d` at `d = 16`), so one user scoring 0.2 is inside guessing noise. Only population means and
  the margin over the popularity control carry a claim.
- **The overlap column is non-monotone** — it peaks near `j = 10` and falls by `j = 50`. That is
  the metric, not the attack: the observed items are excluded from the candidate pool, so the
  easy head of the ranking is progressively removed. `cos` rises monotonically throughout, which
  is the check that the estimate itself does not degrade.

**Conclusion.** Ten unprotected fetches are enough to recover a user's taste vector well enough to
predict over half their next twenty recommendations. This is the quantitative justification for
the PIR layer, and it is precisely the gap NUDGE leaves open when it delegates fetching to "other
means".

### What it costs to close that gap

| | bytes/query (one server) | median server time | privacy |
|---|---|---|---|
| B1, cleartext lookup | 260 | 0.0000047 ms | none |
| **DPF-PIR (this work)** | **483** | 0.242 ms | index hidden from each server |
| B5, full download | 430,592 | 0.045 ms | trivially total |

Read honestly, **DPF-PIR is the slowest of the three in server CPU** — 5× slower than B5's memcpy
and about 50,000× slower than a cleartext lookup. Its win is entirely in communication: 446×
fewer bytes than B5 counting both servers. Setting the extra CPU against the saved bytes, PIR is
the better choice on any link slower than **8.0 Gbit/s** — that is, on every real network, but we
would rather state the crossover than imply there isn't one.

### Two defences short of making the read private, and both fail

Before concluding the read must be private, it is worth asking whether anything
cheaper works. Two candidates were measured. **Neither reaches the popularity
control**, which is the line a defence would have to reach to have worked at
all — below it the adversary has learned nothing about *this* user that is not
already public.

**Decoys.** Fetch the `k` real recommendations plus `r` decoys drawn in
proportion to popularity. At `r=40` — five times the real traffic — the attack
falls only from `cos 0.843` to `0.705`, against a floor of `0.442`. The
estimator is a centroid, and diluting it with popular items moves it toward
what the control already knows.

**Differential privacy on the model (task 4.6, D9.2).** NUDGE §9 specifies
Gaussian noise on the Gram matrix `UᵀU`, injected as `E·v` inside power
iteration, with row normalisation bounding each user's contribution. Measured on
ML-100K at ε=1, δ=2⁻⁴⁰:

| | nDCG@20 | attack `cos` | margin over control |
|---|---|---|---|
| no defence | 0.4255 | 0.8231 | +0.4324 |
| DP, ε=1 | **0.1239** | **0.9212** | **+0.4898** |

**Both axes move the wrong way.** Utility falls 71% and the attack gets
*stronger*. The second is not a paradox: DP protects the **training data** —
what `B` reveals about other users' ratings — whereas §4's attack recovers
*this* user's embedding using `B` as a known basis. Adversary and user work
against the same published matrix, so noising it does not disturb the attack's
geometry; it only destroys the shared popularity structure that the control was
exploiting, which *widens* the attack's margin.

**Why 71% here and 17% for NUDGE.** They report 0.29 → 0.24 on Netflix. The
mechanism is identical; the dataset is not. Analyze Gauss noise grows as
`σ√n` while the signal grows as `m`, so the usable regime is set by `m/√n` —
**157× more favourable for Netflix (480k × 17.7k) than for ML-100K (943 ×
1682)**. Measured directly: `‖UᵀU‖₂ = 144.6` against `‖E‖₂ ≈ 613` at ε=1, so
the noise dominates the signal 4.2×. DP on the Gram matrix is a large-scale
mechanism, and our scale is the wrong one for it. That is a statement about the
dataset, not a contradiction of the paper.

This is the argument for making the read private rather than patching around it.

---

## 5. What we do **not** claim

- **A6 is now demonstrated, not merely argued** — this bullet previously said the opposite and is
  retracted as of 2026-09-11. The three servers are three separate OS processes talking over TCP,
  and the transcript and distinguisher experiment the exit criterion asks for have both been run:
  over 1800 recorded queries across three record indices, every frame is 228 bytes regardless of
  the index, and an adversary trained on the raw wire bytes scores 0.490–0.500 with chance inside
  every 95% confidence interval. What we still do **not** fully claim is the *timing*
  channel — §9.4 now bounds the server-side half of it at under 2% of a call, with
  the wire half left to the OS and unanalysed — or anything about an adversary who can see both server links at once — with both keys the record
  reconstructs by design, and no two-server PIR scheme claims otherwise.
- **~~No claim about private training. The model is factorised in the clear.~~** Retracted
  2026-09-12: private training is built and runs end to end, and §7 is its leakage analysis. The
  bullet was true when written, during S1.
- **Semi-honest only.** A deviating server is out of scope.
- **No formal proof, and §8 is not one.** §8 adds a real/ideal simulation **sketch**: a
  construction-level walk of every message with the simulator named at each step. It is not a
  proof — no security parameter, no reduction, no written-out hybrid chain — and §8.4 lists the
  three places the argument does not close, the sharpest being that the *revealing* normaliser
  is not simulable against `F_train` at all and needs a weaker functionality to be honest about.

## 7. Private training (S2) — added Phase 3

Training now runs under secret sharing. Its leakage is **not** the same as
serving's, and the differences are worth stating one at a time.

### 7.1 What the substrate itself reveals

Nothing beyond the shape of the computation. Multiplication is Araki-style:
each party sends one ring element per product, re-randomised by a zero-share
drawn from a pairwise PRF. A single party sees only uniformly random elements.

**Message sizes depend only on public parameters** — `m, n, d, ell, t, b` —
never on a value. A matrix–vector product communicates `rows` elements
regardless of the matrix contents, which is NUDGE Thm 4.2 and is asserted by
`tests/test_mpc.cpp` against the byte counter rather than argued in prose.

### 7.2 What `B` being opened reveals — unchanged, and already measured

Each converged row of `B` is revealed, exactly as in S1. That is NUDGE's
design, it is what makes `SetOrthogonal` local, and §4 already measures what
it costs: ten observed fetches recover a user to `cos = 0.84`. Private training
does not change this, because it produces the same public `B`.

### 7.3 The truncation protocol

`Trunc_t` opens `x - r` to two of the three parties, where the third generated
`r`. A single corrupted party holds either `r` or `x - r`, never both, so it
learns nothing about `x`. The helper role is passed explicitly at every call
site precisely because giving it to a party that also sees the opening would be
a total break and an easy mistake.

### 7.4 RESOLVED: the spec-faithful normaliser is built

This section previously recorded an OPEN question — training ran on a
normaliser that opened `‖v‖²` because `ApproxNormalize` needs an FSS
comparison gate that did not exist. **The gate is now built** (`dcf.hpp`,
`msnzb.hpp`), so there are two paths and the choice is a measured trade rather
than a compromise:

| normaliser | reveals | rounds per (d×ℓ) | ring |
|---|---|---|---|
| `FssNormalizer` (spec) | **nothing** | ~80 | **b=128 only** |
| `RevealNormNormalizer` | one scalar per call | ~16 | b=64 or b=128 |

**The spec-faithful path costs about 5× the rounds and requires a 128-bit
ring.** That second constraint is not arithmetic and is worth stating on its
own, because it is a *new* answer to D9.1:

> Opening the masked value `S + r` hides `S` only if `r` is drawn from a range
> `κ` bits wider than `S`'s. With `κ = 40` and the value ranges power
> iteration produces, the gate's domain needs ~81 bits. **`b = 64` has no room
> for the mask at all.** The arithmetic headroom study (§ D9.1) said `b=64`
> survives to ML-1M but loses the deferred schedule; this says that the moment
> you want a normaliser that reveals nothing, `b=64` stops being an option.
> The two constraints bind for different reasons and the tighter one wins.

`FssNormalizer`'s constructor refuses at `b=64` rather than using a short mask,
because a gate that silently masked with too few bits would *look* like privacy
while providing none.

**What is claimed now.** With `b=128` and `FssNormalizer`, private training
reveals nothing beyond `B` itself — which is public by design and whose cost is
measured in §4. The leakage profile matches the specification.

**What was still measured only at `b=64`** — this paragraph previously said the
quality study had been run only on the revealing path, that re-running it at
`b=128` was Phase 4 work, and that matching quality was "an expectation and not
yet a measurement". That expectation has since been tested. It was **nearly
right and wrong in an instructive way**; see §7.6.

### 7.5 Harvesting adds no new leak

The PIRSONA loop (§7.3 of the architecture) accumulates the DPF expansion each
PIR server already computes, giving shared consumption counts for the next
training round with no upload step. Each server's accumulator is a share and
reveals nothing on its own.

It does expose **how many** queries a user made, since the accumulator is
updated once per query — but the threat model already lists query counts as
leaked by design, so harvesting reveals nothing that running the PIR layer did
not. That is exactly why it is free.

### 7.6 What the no-leak path costs in quality — measured 2026-09-13

§7.4 predicted that `FssNormalizer` at `b=128` would match the revealing path's
recommendation quality. Measured, on ML-100K `u1` at `d=16`, nDCG@20:

| `ell` | reveal `b=64` | reveal `b=128` | **fss `b=128`** | oracle |
|---|---|---|---|---|
| 1 | 0.3729 | 0.3727 | **0.1651** | 0.3664 |
| 2 | 0.4251 | 0.4248 | **0.4230** | 0.4220 |
| 5 | 0.4485 | 0.4490 | **0.4448** | 0.4455 |
| 10 | 0.4511 | 0.4513 | **0.4493** | 0.4536 |
| 25 | 0.4532 | 0.4531 | **0.4499** | 0.4545 |
| 50 | 0.4531 | 0.4532 | **0.4502** | 0.4525 |

**The prediction holds for every `ell >= 2`**: revealing nothing costs between
0.002 and 0.005 of nDCG@20. The leakage of the singular-value trajectory buys
essentially no accuracy. What it costs instead is rounds — 11519 against 2191 at
`ell=10`.

The `b=128` reveal-norm column is the **control that makes that claim
defensible**. Without it the comparison confounds three changes at once —
normaliser, ring width, and the deferred truncation schedule `b=128` takes — and
it shows ring and schedule together contribute ±0.0005, so the normaliser owns
the whole difference.

**`ell=1` is the exception and is reported, not buried.** 0.1651 against 0.3729,
with the subspace angle still near-orthogonal at 1.552 rad. A single power
iteration has nothing downstream to recover from the first normalisation — the
one acting on the raw random start — and from `ell=2` the gap closes to 0.0022.
`ell=1` is not an operating point anyone would choose, but the cliff is sharp
and belongs in the record.

**This measurement also found a bug that a cost-only benchmark could not.** The
first run showed 0.3428 against 0.4511 — a 24% relative loss that would have
shipped as "what privacy costs". The cause was the MSNZB gate's range, `[t-8,
t+8]`, copied from the parameter sweep: `MsnzbGate::Apply` builds its answer as a
telescoping sum of `1[S >= 2^k]` over `[lo, hi]`, so a value outside silently
clamps and Newton–Raphson refines from the wrong power of two. Rounds and bytes
are **identical** either way, because widening the range buys DCF keys offline
and `Apply` stays one round — which is exactly why measuring only cost hid it.
See §7.7.

### 7.7 The gate cannot check its own range, and the caller must

`MsnzbGate::Apply` sees only the masked opening `S + r`. Learning `msnzb(S)` at
runtime is precisely what the gate exists to avoid revealing, so **it cannot
validate its own input range**, and an out-of-range value is a silent clamp
rather than an error. The header previously claimed Apply checked; it does not,
and it now says so.

The obligation is therefore the caller's: size `[lo, hi]` from public parameters
with room to spare. `src/apps/train.cpp` uses `[1, 2t+8]`. This is a
**correctness** obligation that produces no diagnostic when violated, which puts
it in the same class as the truncation and counter-synchronisation invariants
recorded elsewhere in this project.

---

## 8. A real/ideal simulation sketch for the composed system

**D7 asks for this and it is the last of its four components to land.** Read it as
what it is called: a **sketch**. It is a construction-level argument that walks
every message the system sends and says what a simulator would produce instead.
It is not a proof — there is no formal security parameter, no reduction, and no
hybrid-indistinguishability chain written out — and §5 still applies.

What it is for: the claim "one semi-honest server learns nothing beyond §3" is
asserted all over this project. This section is where that assertion is made
checkable, step by step, against code.

### 8.1 What is being claimed

The ideal functionalities are `F_train` and `F_serve` from `REQUIREMENTS.md`
§3.2. The claim, for adversary class **A1** — exactly one semi-honest server:

> For each corrupted party `P_c`, there is a simulator `S_c` that, given only
> `P_c`'s input shares and the declared output of the functionality, produces a
> transcript computationally indistinguishable from `P_c`'s real view.

`P_c`'s real view is its input shares, its PRF keys, its local randomness, and
every ring element it receives. The argument below is per-step: each step either
sends nothing, or sends elements that are uniform from `P_c`'s standpoint.

**The three servers are not symmetric and must be treated separately.** `P0` and
`P1` answer PIR queries and hold DPF keys; `P2` does not. Truncation rotates a
helper role. So "by symmetry" is not available here and is not used.

### 8.2 The substrate: why one share reveals nothing

`U` is shared 2-of-3 replicated: `x = x0 + x1 + x2`, and party `i` holds
`(x_i, x_{i+1})`. Any two of three shares of a 3-out-of-3 additive sharing are
uniform and independent of `x`, so `S_c` samples two uniform ring elements per
value. This is the base case everything else rests on, and it is why the
simulator never needs `U`.

Correlated randomness is pairwise-PRF zero shares, `alpha_i = F(k_i, ctr) -
F(k_{i-1}, ctr)`, summing to zero with **no communication**. `P_c` can compute
its own `alpha_c` and cannot compute any other party's, because each requires a
key it does not hold. That asymmetry is what makes the incoming elements below
uniform, and it is also why the counter must stay in lockstep across all three
generators — `Mpc3::Exchange` asserts it, after a desynchronisation bug that
silently produced well-formed shares of the wrong value.

### 8.3 Step by step

**Matrix–vector products (`Mpc3::MatVec`, via `Exchange`).** Party `i` computes
`z_i` locally from its two shares and re-randomises with `alpha_i`, then sends
one ring element per output entry. `P_c` receives `z_{c+1}`, which carries
`alpha_{c+1}` — built from a key `P_c` lacks. `S_c` samples uniform. The
**message size depends only on the public shape**: `rows` elements, regardless
of matrix contents. That is NUDGE Thm 4.2, and it is not argued here but
measured — task 4.1 quadrupled the input matrix and traffic moved 4.4%, with the
byte delta matching the predicted `m`-dependent term exactly.

**Truncation (`TruncatePair`, 3 rounds).** A helper `h` supplies a correlated
pair `(r, r>>t)`; the other two open `c = x + r` and reshare. Two distinct
simulator cases, and the split is load-bearing:

- `P_c` is one of the two openers — it sees `c = x + r` where `r` is uniform and
  held only by `h`. `S_c` samples `c` uniform.
- `P_c` is the helper — it sees `(r, r>>t)`, which it generated, and **not** the
  opening. `S_c` replays its own randomness.

A party that both held `r` and saw `x + r` would recover `x`. The code says so
in as many words, and the helper-role assignment exists to prevent exactly that.

**Normalisation.** Two implementations, and they do **not** have the same
security statement — this is the sharpest point in this section and §8.4 is
about it. For `FssNormalizer`: the MSNZB gate opens `S + r` where `r` is
`kappa = 40` bits wider than `S`, so the opening is statistically close to
uniform and `S_c` samples it; the DCF evaluations are local; the Newton
iterations are multiplications and truncations already covered above.

**Orthogonalisation (`SetOrthogonalPublic`).** Entirely local — every product is
share-times-public, because the rows of `B` it projects against are already
open. Nothing to simulate, and nothing sent. This is also why it is free.

**Opening each row of `B`.** `B` is `F_train`'s declared output, so in the ideal
world the simulator is **given** it and replays it. This step is free in the
simulation and is the single largest leakage surface in the system — which is
not a contradiction: it is leakage *by design*, and §4 is the measurement of
what it costs, not a claim that it costs nothing.

**Serving (`ScoreShares`).** `B` is public and `a` is shared, so scoring is a
linear map on shares: each server computes locally and sends nothing. Zero
rounds. The seen-item mask arrives from the user as a share. `S_c` samples
uniform shares.

**Delivery (DPF-PIR).** `P_c ∈ {P0, P1}` receives one DPF key. A single key is
pseudorandom and independent of `alpha` by the DPF's security, so `S_c` samples
a key of the right length — 227 bytes at `domain_bits = 11`, fixed. The server
then runs `EvalFull` across the **whole** domain and an inner product over every
index, so there is **no data-dependent work and no data-dependent timing** in the
answer path. `P2` receives nothing at all in this step.

That the frames are input-independent is not only argued: over 1800 recorded
queries every frame is 228 bytes whatever the record index, and a classifier
trained on the raw wire bytes scores 0.490–0.500 against a 0.5 chance line.

**Harvesting.** `h0 - h1` is a 2-of-2 additive sharing across `P0` and `P1`,
converted to 2-of-3 replicated by re-randomising with a pairwise secret `P2` does
not hold. `S_c` samples uniform. Without that re-randomisation the conversion
would be, in the header's own words, "a leak dressed as a type conversion".

### 8.4 Where the argument does not close

Three places, stated rather than smoothed over.

**1. `RevealNormNormalizer` is not simulable against `F_train` as written, and
this is not a technicality.** It opens one scalar per normalisation. Across a run
that is `d × (ell + 1)` scalars — the trajectory of the singular values of `U` —
and no simulator given only `B` can produce them, because they are not a function
of `B`. Two honest options, and we take the second:

- exclude it, and claim security only for the `FssNormalizer` path; or
- define a second, weaker functionality `F_train^leaky` whose output is `(B,
  {||v||})`, and claim security against *that*.

We take the second because it is what we actually ran: **every `b=64` quality
number in `train_quality.jsonl` was produced on the revealing path.** Claiming
the strong statement while reporting numbers from the weak one would be the
dishonest version of this section. §7.6 gives the price of moving to the path
that does close: 0.002–0.005 nDCG@20, and 5.3× the rounds.

**2. Composition across training rounds is argued, not proven.** Each round is
covered above, and harvesting adds no new leak per §7.5. But the *sequence* —
`B` opened repeatedly as harvested counts accumulate — is D9.5's question and is
not answered here. What an adversary learns from many rounds of `B` about a user
whose consumption is feeding it is open.

**3. Sequential composition is assumed, not established.** The protocols are
composed one after another with fresh correlated randomness, which is the
standard setting for this kind of argument, but no composition theorem is
invoked and no UC claim is made.

### 8.5 The other adversary classes, briefly

- **A3 (two colluding servers)** — two parties hold all three shares and
  reconstruct. No simulator exists and none is claimed. This is the assumption
  the system rests on.
- **A4 (malicious server)** — out of scope; nothing above survives a deviating
  party, since every step assumes the protocol was followed.
- **A5 (malicious user)** — corrupts only its own output for *retrieval*, but see
  §9: the harvest path accepts a weight the servers never check.
- **A6 (network observer)** — sees sizes and timing only; sizes are
  input-independent by construction and measured.

---

## 9. The malicious client (A5), measured — added Phase 4

Adversary class A5 had one sentence and no numbers. It now has two results,
one of which is a hole in **our** system rather than an inherited limitation.

### 9.1 The harvest path accepts an unbounded vote (D9.3)

**D9.3 as written does not apply here.** It asks for a well-formedness check on
user *submissions*, so one malicious user cannot skew the model. OblivRec has no
rating-upload path: training reads MovieLens off local disk, and §7.5's harvest
loop deliberately removed the upload. So there is no rating vector to validate.

**But there is a submission, and it was unchecked.** An honest client goes
through `PirClient::Query`, which fixes `beta = 1` so the difference of the two
expansions is the selector vector exactly. Nothing stops a client calling
`Gen(alpha, beta, domain_bits)` directly with any `beta`. Retrieval still works
— the record returns scaled by `beta`, which the attacker divides out — and the
servers fold `beta`, not 1, into the consumption accumulator that becomes the
next round's training input. **Every pre-existing check passes**: the keys
deserialise, the domain matches, the expansion is the right length.
`tests/test_harvest.cpp` demonstrates it: one query at `beta = 10⁶` puts
1,000,000 into the consumption vector.

**What it buys is not what we first assumed.** Measured with
`model/poison_study.py`:

| | nDCG@20 for honest users | vs clean |
|---|---|---|
| clean model | 0.4536 | — |
| 1 attacker, weight 10⁶ | 0.4493 | −1.0% |
| 8 colluders, distinct targets | 0.4351 | −4.1% |
| **16 colluders = `d`** | **0.0021** | **−99.5%** |

One attacker can do almost nothing, and **the reason is the normalisation**:
power iteration normalises every step, so past a point extra weight only fixes
a *direction*. A single attacker captures one of the `d` components and the
other `d−1` still carry the honest signal. It also cannot *aim* — the targets it
promotes do not rise (mean rank 901 → 977 of 1682).

**The cliff is at exactly `d`.** Sixteen colluders with distinct target sets
capture all sixteen components and nothing honest is left. This is model
destruction, not promotion, and it costs the attacker sixteen clients.

### 9.2 The check, and what it does not cover

Summation is linear, so each server can sum its own expansion locally, and

    Σⱼ e₀[j] − Σⱼ e₁[j]  =  beta

exactly, for any `alpha`. The two servers open that single scalar and require
it to be 1. **One round, two ring elements per query.**

**Opening it leaks nothing.** The secret is `alpha` — *which* item was fetched.
`beta` is a payload that is supposed to be the public constant 1, and the sum is
independent of `alpha` by construction. Each server's own sum is pseudorandom;
their difference is `beta` and nothing else.

**It closes weight inflation, not weight redistribution.** The sum bounds the
total, not its distribution. A client that forged correction words directly,
rather than calling `Gen`, could produce a difference vector of `+2` at one
index and `−1` at another — summing to 1, passing this check, still skewing two
items. `tests/test_harvest.cpp` constructs exactly that forgery and shows it
passes. Proving a key encodes a genuine one-point function is what a
Sabre-style audit does, and that is §9.3's stretch half, unbuilt.

### 9.3 The DoS amplification is now a number (D9.4)

A5 said a flood of invalid keys is an unmitigated DoS on `EvalFull`, "which is
linear in the domain". Measured, `bench/bench_dos.cpp`:

| domain bits | key bytes | amplification |
|---|---|---|
| 8 | 173 | 15× |
| **11 (ours)** | **227** | **121×** |
| 14 | 281 | 909× |
| 16 | 317 | 3495× |

The key is `O(log N)` and the answer is `O(N)`, so **the amplification grows
with the catalogue** — the opposite of the direction a defender wants. At our
operating point, 227 bytes buys 121× its own cost in server work.

**The expensive attack is the one that looks legitimate.** A *malformed* key is
cheap for the server: `DpfKey::Deserialize` rejects it before any expansion, and
`tests/test_dpf_serialize.cpp` covers the party byte, the `domain_bits` range
and the exact length. So parsing hardening does not address this, and the
mitigation really is the audit we did not build.

That the server touches every index is not inefficiency — it is the privacy
property. Any data-dependent shortcut would leak which record was wanted. The
DoS exposure is therefore **intrinsic to the design**, not a bug in it, and it
is the clearest example in this project of a privacy mechanism whose cost is
paid in availability.

### 9.4 The timing side channel, bounded (A6)

§5 has said since Phase 2 that frame *sizes* are measured constant but *timing*
is not analysed. The wire's inter-frame timing is dominated by the OS scheduler
and the loopback stack — measuring it would mostly measure Windows. What this
project controls, and what a timing attack would have to exploit, is whether
**the server's work depends on `alpha`**.

`bench/bench_timing.cpp` times `PirServer::Answer` 200 times each across eight
indices spanning the domain, **interleaved** rather than in blocks so that
thermal drift cannot align with index identity — the same reasoning as
`bench_compose.cpp`'s rep-major loop.

| | |
|---|---|
| mean call | 87 µs |
| spread between the per-`alpha` means | **1.7 µs** |
| median within-`alpha` standard deviation | **6.0 µs** |
| permutation test, shuffled `alpha` labels | **p = 0.60** |

The variation between indices is **smaller than the noise within a single
index**, and the observed spread sits in the middle of the null distribution.

**This is a bound, not a proof of constant time**, and the distinction is not
pedantry: no finite sample shows a difference is exactly zero. What it shows is
that any `alpha`-dependent signal is **below 2.0% of the mean call** at this
sample size. That is the honest claim, and it is consistent with the
construction — `EvalFull` walks every node and the answer takes an
unconditional inner product over every record, so there is no data-dependent
branch to find.

---

## 6. Open items

| | Item | Where |
|---|---|---|
| D9.3 | ~~Input validation against malicious users poisoning model quality~~ | **done 2026-09-24, §9.1-9.2** — weight inflation closed; redistribution open |
| D9.4 | Sabre-style audit for malformed keys (A5 DoS) | **DoS measured 2026-09-24, §9.3**; the audit itself remains out of scope, with reasons |
| — | ~~Channel transcript + distinguisher experiment (A6)~~ | **done 2026-09-11** |
| — | Constant rating count to close the nnz leak | evaluate against the utility cost |
| — | ~~Timing side channel (frame *sizes* shown constant; *timing* not analysed)~~ | **PARTLY done 2026-09-24, §9.4.** The half we control is measured: `Answer`'s runtime shows no dependence on `alpha` (p = 0.60), bounding any signal below 2.0% of the mean call. Inter-frame timing ON THE WIRE remains unanalysed and is dominated by the OS and loopback stack rather than by the protocol; stated as a limitation rather than claimed. |
| — | ~~The FSS comparison gate and the spec-faithful `ApproxNormalize`~~ | **done 2026-09-12, §7.4** |
| — | ~~Re-run the quality study at b=128 on the no-leak path~~ | **done 2026-09-13, §7.6** |
| — | ~~Real/ideal simulation sketch for the composed system~~ | **done 2026-09-24, §8** |

---

*Sources: `design/ARCHITECTURE-draft-v1.md §9`; measurements from `model/attack.py` and
`bench/bench_baseline.cpp`, raw rows in `bench/results/leakage_attack.jsonl`,
`bench/results/bench_baseline.jsonl` and `bench/results/distinguisher_result.jsonl`, figures
regenerated by `mingw32-make figures`.*
