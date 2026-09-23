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
| **A2** | One semi-honest server + any number of malicious users | Above, plus colluding users | Honest users' ratings stay safe. Model *quality* is not protected — input validation is future work. |
| **A3** | **Two or more colluding servers** | Pool their views | **NO GUARANTEE. The shares reconstruct.** This is the assumption the entire system rests on, and it is stated in bold wherever the system is described. |
| **A4** | A malicious (deviating) server | Arbitrary deviation | **Out of scope**, as in NUDGE §3.1. It can corrupt correctness and availability. |
| **A5** | A malicious user | Malformed DPF keys | Corrupts only its own output. But a flood of invalid keys is an **unmitigated DoS** on `EvalFull`, which is linear in the domain. A Sabre-style logarithmic audit is the known mitigation; we have not built it. |
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

---

## 5. What we do **not** claim

- **A6 is now demonstrated, not merely argued** — this bullet previously said the opposite and is
  retracted as of 2026-09-11. The three servers are three separate OS processes talking over TCP,
  and the transcript and distinguisher experiment the exit criterion asks for have both been run:
  over 1800 recorded queries across three record indices, every frame is 228 bytes regardless of
  the index, and an adversary trained on the raw wire bytes scores 0.490–0.500 with chance inside
  every 95% confidence interval. What we still do **not** claim is anything about a *timing* side
  channel, or about an adversary who can see both server links at once — with both keys the record
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

## 6. Open items

| | Item | Where |
|---|---|---|
| D9.3 | Input validation against malicious users poisoning model quality | Phase 3+ |
| D9.4 | Sabre-style audit for malformed keys (A5 DoS) | stretch |
| — | ~~Channel transcript + distinguisher experiment (A6)~~ | **done 2026-09-11** |
| — | Constant rating count to close the nnz leak | evaluate against the utility cost |
| — | Timing side channel on the wire (frame *sizes* are now shown constant; inter-frame *timing* is not analysed) | Phase 4 |
| — | ~~The FSS comparison gate and the spec-faithful `ApproxNormalize`~~ | **done 2026-09-12, §7.4** |
| — | ~~Re-run the quality study at b=128 on the no-leak path~~ | **done 2026-09-13, §7.6** |
| — | ~~Real/ideal simulation sketch for the composed system~~ | **done 2026-09-24, §8** |

---

*Sources: `design/ARCHITECTURE-draft-v1.md §9`; measurements from `model/attack.py` and
`bench/bench_baseline.cpp`, raw rows in `bench/results/leakage_attack.jsonl`,
`bench/results/bench_baseline.jsonl` and `bench/results/distinguisher_result.jsonl`, figures
regenerated by `mingw32-make figures`.*
