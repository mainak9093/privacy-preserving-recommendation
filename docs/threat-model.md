# OblivRec: threat model and leakage profile

What OblivRec protects, against whom, and how far the argument for it goes. Measurements come from the
committed data under [`bench/results/`](../bench/results/), and
[`bench/scripts/check_claims.py`](../bench/scripts/check_claims.py) checks the numbers here against it.
Section 2 of the [final report](../report/final.pdf) is the polished version of this document, and this
one keeps more of the working detail.

**The short version.** One semi-honest server learns nothing beyond the leakage in §3, argued by a
simulation sketch rather than a proof. **Two colluding servers learn everything.** A server that deviates
from the protocol is out of scope.

## 1. Setting

Three servers, `P0 P1 P2`, hold 2-of-3 replicated shares of each user's embedding `a`. The item embedding
matrix `B` is **public to everyone**. That is Nudge's design and not an accident: each converged row of `B`
is opened before the next is computed, which keeps `SetOrthogonal` cheap and makes the whole
power-iteration approach affordable under secure computation.

Serving is `scores = a · B`, a public matrix applied to a shared vector, so each server computes its share
locally with no communication. The user reconstructs the score vector, selects the top `k` **on their own
machine**, and then fetches each recommended record by two-server DPF-PIR against `P0` and `P1`.

## 2. Adversary classes

| | Adversary | Capability | Our guarantee |
|---|---|---|---|
| **A1** | One semi-honest server | Follows the protocol, reads its own view | **Full user privacy, up to §3.** The headline claim, and exactly Nudge's model. |
| **A2** | One semi-honest server and any number of malicious users | Above, plus colluding users | Honest users' ratings stay safe. Model *quality* is protected against weight inflation (§9.2), but colluding clients can still distort the model by forging keys that redistribute weight. |
| **A3** | **Two or more colluding servers** | Pool their views | **No guarantee. The shares reconstruct.** The entire system rests on this assumption. |
| **A4** | A malicious, deviating server | Arbitrary deviation | **Out of scope**, as in Nudge §3.1. It can corrupt correctness and availability. |
| **A5** | A malicious user | Malformed or weighted DPF keys | Measured in §9. Retrieval corrupts only that user's own output. The harvest path accepted an unbounded weight until we closed it with a one-round check. Still open: forged redistribution keys, and the denial of service on `EvalFull`, unmitigated and measured at 121× and 142× at our catalogue size in two runs (§9.3). |
| **A6** | A network observer | Message sizes and timing | Sees public parameters and timing only. Over 1800 recorded queries every query frame is 228 bytes and every reply 265 bytes whatever the record index, and a classifier on the raw wire bytes scores 0.490 to 0.500 against a 0.5 chance line. |

## 3. Leakage profile

**Hidden:** individual ratings, which items a user rated, the user embeddings `A`, the score vectors, the
top-`k` indices, and **which records a user fetches**.

**Leaked by design:** the parameters `m, n, d, ell, k, b, t`. **The item embedding matrix `B`, in the
clear.** The number of non-zero entries in a user's rating vector, which could be removed by requiring a
fixed rating count at a cost in quality (Nudge §3.1). Message timing and query counts.

**Outside the protocol's protection:** correlation between query timing and external events. We do not
claim to fix it.

## 4. Why the delivery layer has to be private

`model/attack.py` runs this experiment. Neither Pirsona nor Nudge measures it, because neither implements
both halves.

Because `B` is public, **an observed fetch is not one bit of information. It is a projection onto a known
basis.** An adversary who sees *which* records a user fetched, and nothing else, can reconstruct that
user's private embedding:

| Observed fetches `j` | `cos(â, a)` | Share of the user's **next** 20 recommendations predicted |
|---|---|---|
| 1 | **0.55** | 39% |
| 2 | 0.66 | 47% |
| 5 | 0.77 | 53% |
| 10 | **0.84** | **56%** |
| 50 | 0.91 | 46% |

**The control is what makes this a result and not an artefact.** Popular items sit near the top of nearly
everyone's ranking, so an estimator can look strong having learned only what is public. So the identical
estimator is run on the `j` globally most popular items, ignoring the user entirely. It reaches only 0.26 to
0.53, and **the attack's margin above it is +0.38 to +0.42 for every `j` from 2 to 50, and +0.28 at `j = 1`**.
That margin is the part that is genuinely about the individual. A random-direction control sits at 0.003.

Two caveats:

- **A single user's cosine is not evidence.** The random control's spread is roughly ±0.18, the interquartile
  range of a random direction's cosine in 16 dimensions, so one user scoring 0.2 is inside guessing noise.
  Only population means and the margin over the popularity control carry a claim.
- **The overlap column is non-monotone.** It peaks near `j = 10` and falls by `j = 50`. That is the metric and
  not the attack: observed items are excluded from the candidates, so the easy head of the ranking is
  progressively removed. `cos` rises throughout, which is the check that the estimate does not degrade.

**Conclusion.** Ten unprotected fetches recover a user's taste vector well enough to predict over half their
next twenty recommendations. This is the quantitative case for the PIR layer, and it is the gap Nudge leaves
open when it delegates fetching to "other means".

### What it costs to close that gap

| | Bytes per query (one server) | Median server time | Privacy |
|---|---|---|---|
| B1, cleartext lookup | 260 | 4 to 5 ns | none |
| **DPF-PIR (this work)** | **483** | 0.238 ms and 0.082 ms in two committed runs | index hidden from each server |
| B5, full download | 430,592 | 0.045 ms and 0.015 ms in the same runs | trivially total |

**DPF-PIR is the slowest of the three in server CPU**, about five times B5's copy within a run (5.3 and 5.6) and
four orders of magnitude slower than a cleartext lookup. Its win is entirely in communication: 446× fewer bytes
than B5 counting both servers. Setting the extra CPU against the saved bytes, PIR is the better choice on any
link slower than **8.0 Gbit/s in one committed run and 22.9 Gbit/s in the other**. The crossover comes from
absolute times, which drift with the laptop's thermal state. That covers every real network, and we would
rather state the crossover than imply there is none.

### Two defences short of making the read private, and both fail

Two cheaper candidates were measured. **Neither reaches the popularity control**, which is the line a defence
would have to reach to have worked at all, since below it the adversary has learned nothing about *this* user
that is not already public.

**Decoys.** Fetch the `k` real recommendations plus `r` decoys drawn in proportion to popularity. At `r = 40`,
five times the real traffic, the attack falls only from `cos 0.843` to `0.705`, against a floor of `0.442`. The
estimator is a centroid, and diluting it with popular items moves it towards what the control already knows.

**Differential privacy on the model.** Nudge §9 specifies Gaussian noise on the Gram matrix `UᵀU`, injected as
`E·v` inside power iteration, with row normalisation bounding each user's contribution. Measured on ML-100K at
ε = 1, δ = 2⁻⁴⁰:

| | nDCG@20 | Attack `cos` | Margin over control |
|---|---|---|---|
| No defence | 0.4255 | 0.8231 | +0.4324 |
| DP, ε = 1 | **0.1239** | **0.9212** | **+0.4898** |

**Both axes move the wrong way.** Utility falls 71% and the attack gets *stronger*. That is not a paradox. DP
protects the **training data**, what `B` reveals about other users' ratings, whereas this attack recovers *this*
user's embedding using `B` as a known basis. Adversary and user work against the same published matrix, so
noising it leaves the attack's geometry alone. It destroys only the shared popularity structure the control was
exploiting, which *widens* the attack's margin.

**Why 71% here and 17% for Nudge.** They report 0.29 falling to 0.24 on Netflix. The mechanism is identical and
the dataset is not. Analyze Gauss noise grows as `σ√n` while the signal grows as `m`, so the usable regime is set
by `m/√n`, **157× more favourable for Netflix (480k × 17.7k) than for ML-100K (943 × 1682)**. Measured directly,
`‖UᵀU‖₂ = 144.6` against `‖E‖₂ ≈ 613` at ε = 1, so noise dominates signal by 4.2×. DP on the Gram matrix is a
large-scale mechanism and our scale is the wrong one for it. That is a statement about the dataset and not a
contradiction of the paper.

## 5. What we do not claim

- **A wire-timing result.** Frame sizes and contents are measured. §9.4 bounds the server-side half of timing,
  and timing on the wire is left to the operating system and not analysed.
- **Anything about an adversary who can see both PIR links at once.** With both keys the record reconstructs, by
  design, and no two-server PIR scheme claims otherwise.
- **Security against a deviating server.** Semi-honest only.
- **A formal proof.** §8 is a real/ideal simulation **sketch**: a construction-level walk of every message with
  the simulator named at each step. There is no security parameter, no reduction and no written-out hybrid chain,
  and §8.4 lists the three places the argument does not close.

## 6. Open items

| Item | State |
|---|---|
| Malicious-client audit (Sabre style) | Not built. It would close weight redistribution and the DoS on `EvalFull`. |
| Constant rating count, to close the non-zero-count leak | Not evaluated against its cost in quality. |
| Inter-frame timing on the wire | Not analysed. It is dominated by the OS and loopback stack, not the protocol. |
| What many rounds of opening `B` reveal about one user | Open. See §8.4. |

## 7. Private training

Training runs under secret sharing, and its leakage differs from serving's in ways worth stating one at a time.

### 7.1 What the substrate reveals

Nothing beyond the shape of the computation. Multiplication follows Araki et al.: each party sends one ring element
per product, re-randomised by a zero share drawn from a pairwise PRF, so a single party sees only uniformly random
elements.

**Message sizes depend only on public parameters**, `m, n, d, ell, t, b`, and never on a value. A matrix-vector
product communicates `rows` elements whatever the matrix contains. That is Nudge Thm 4.2, and `tests/test_mpc.cpp`
asserts it against the byte counter.

### 7.2 What opening `B` reveals

Each converged row of `B` is revealed, exactly as in serving. That is Nudge's design, it is what makes `SetOrthogonal`
local, and §4 measures what it costs. Private training does not change this, because it produces the same public `B`.

### 7.3 The truncation protocol

`Trunc_t` opens `x - r` to two of the three parties, and the third generated `r`. A single corrupted party holds
either `r` or `x - r`, never both, so it learns nothing about `x`. The helper role is passed explicitly at every call
site, because giving it to a party that also sees the opening would be a total break and an easy mistake.

### 7.4 Two normalisers

| Normaliser | Reveals | Rounds per call | Ring |
|---|---|---|---|
| `FssNormalizer`, per the paper | **nothing** | 58 | **`b = 128` only** |
| `RevealNormNormalizer` | one scalar per call | 5 | `b = 64` or `b = 128` |

**The spec-faithful path needs a 128-bit ring**, and that constraint is not arithmetic. Opening the masked value
`S + r` hides `S` only if `r` is drawn from a range `κ` bits wider than `S`'s. With `κ = 40` and the values power
iteration produces, the gate's domain needs at least 81 bits, so **`b = 64` has no room for the mask at all**.
`FssNormalizer`'s constructor refuses at `b = 64` rather than use a short mask, because a gate that masked with too
few bits would look like privacy while providing none.

With `b = 128` and `FssNormalizer`, private training reveals nothing beyond `B` itself, and the leakage profile
matches the specification.

### 7.5 Harvesting adds no new leak

The harvest accumulates the DPF expansion each PIR server already computes, giving shared consumption counts for the
next training round with no upload step. Each server's accumulator is a share and reveals nothing on its own. It does
expose **how many** queries a user made, since it is updated once per query, but query counts are already leaked by
design, so harvesting reveals nothing the PIR layer did not.

### 7.6 What the no-leak path costs in quality

Measured on ML-100K `u1` at `d = 16`, nDCG@20:

| `ell` | Reveal `b = 64` | Reveal `b = 128` | **FSS `b = 128`** | Oracle |
|---|---|---|---|---|
| 1 | 0.3729 | 0.3727 | **0.1651** | 0.3664 |
| 2 | 0.4251 | 0.4248 | **0.4230** | 0.4220 |
| 5 | 0.4485 | 0.4490 | **0.4448** | 0.4455 |
| 10 | 0.4511 | 0.4513 | **0.4493** | 0.4536 |
| 25 | 0.4532 | 0.4531 | **0.4499** | 0.4545 |
| 50 | 0.4531 | 0.4532 | **0.4502** | 0.4525 |

**For every `ell` of 2 or more, revealing nothing costs between 0.0018 and 0.0041 of nDCG@20.** Leaking the
singular-value trajectory buys essentially no accuracy. What it saves is rounds: 2191 against 11519 at `ell = 10`.

The `b = 128` reveal column is the **control that makes that claim defensible**. Without it the comparison confounds
the normaliser, the ring width and the deferred truncation schedule. It shows ring and schedule together contribute
±0.0005, so the normaliser owns the whole difference.

**`ell = 1` is the exception.** 0.1651 against 0.3729. The first normalisation acts on the raw random start, and a
single iteration has nothing downstream to recover with. From `ell = 2` the gap closes. Nobody would choose `ell = 1`,
but the cliff is sharp and belongs in the record.

**This measurement also found a bug a cost-only benchmark could not.** The first run showed 0.3428 against 0.4511, a
24% relative loss that would have been reported as "what privacy costs". The cause was the gate's range, `[t-8, t+8]`,
copied from the parameter sweep. `MsnzbGate::Apply` builds its answer as a telescoping sum of `1[S >= 2^k]` over
`[lo, hi]`, so a value outside it clamps silently and Newton-Raphson refines from the wrong power of two. Rounds and
bytes are **identical** either way, because widening the range buys DCF keys offline and `Apply` costs the same. The
buggy run's rows were not kept: its value is recorded in commit `d2138ad` and at the call site in `src/apps/train.cpp`.

### 7.7 The gate cannot check its own range

`MsnzbGate::Apply` sees only the masked opening `S + r`. Learning `msnzb(S)` at run time is what the gate exists to
avoid, so **it cannot validate its own input range**, and an out-of-range value is a silent clamp rather than an error.
The obligation falls on the caller, which must size `[lo, hi]` from public parameters with room to spare.
`src/apps/train.cpp` uses `[1, 2t+8]`. This is a correctness obligation with no diagnostic when violated, the same class
as the truncation and counter-synchronisation invariants.

## 8. A real/ideal simulation sketch for the composed system

Read this as what it is called: a **sketch**. It walks every message the system sends and says what a simulator would
produce instead. It is not a proof.

### 8.1 What is being claimed

The ideal functionalities are `F_train` and `F_serve`, defined in Section 2.2 of the [report](../report/final.pdf). The
claim, for adversary class **A1**, exactly one semi-honest server:

> For each corrupted party `P_c`, there is a simulator `S_c` that, given only `P_c`'s input shares and the declared
> output of the functionality, produces a transcript computationally indistinguishable from `P_c`'s real view.

`P_c`'s real view is its input shares, its PRF keys, its local randomness, and every ring element it receives. Each step
below either sends nothing or sends elements that are uniform from `P_c`'s standpoint. **The three servers are not
symmetric.** `P0` and `P1` answer PIR queries and hold DPF keys, `P2` does not, and truncation rotates a helper role. So
"by symmetry" is not used.

### 8.2 The substrate: why one share reveals nothing

`U` is shared 2-of-3 replicated: `x = x0 + x1 + x2`, and party `i` holds `(x_i, x_{i+1})`. Any two of three shares of a
3-out-of-3 additive sharing are uniform and independent of `x`, so `S_c` samples two uniform ring elements per value, and
the simulator never needs `U`.

Correlated randomness is pairwise-PRF zero shares, `alpha_i = F(k_i, ctr) - F(k_{i-1}, ctr)`, summing to zero with **no
communication**. `P_c` can compute its own `alpha_c` and no other party's, because each needs a key it does not hold. That
asymmetry makes the incoming elements below uniform, and it is why the counter must stay in lockstep across all three
generators. `Mpc3::Exchange` asserts this, after a desynchronisation bug that silently produced well-formed shares of the
wrong value.

### 8.3 Step by step

**Matrix-vector products (`Mpc3::MatVec`, via `Exchange`).** Party `i` computes `z_i` locally from its two shares,
re-randomises with `alpha_i`, and sends one ring element per output entry. `P_c` receives `z_{c+1}`, which carries
`alpha_{c+1}`, built from a key `P_c` lacks. `S_c` samples uniform. The **message size depends only on the public shape**,
which is Nudge Thm 4.2. We measured it: quadrupling the input matrix moved traffic 4.4%.

**Truncation (`TruncatePair`, 3 rounds).** A helper `h` supplies a correlated pair `(r, r>>t)`, and the other two open
`c = x - r` and reshare. Two simulator cases:

- `P_c` is one of the two openers. It sees `c = x - r` where `r` is uniform and held only by `h`. `S_c` samples `c` uniform.
- `P_c` is the helper. It sees `(r, r>>t)`, which it generated, and **not** the opening, and the reshare is masked with a
  pairwise secret it does not hold. `S_c` replays its own randomness and samples the rest.

A party that both held `r` and saw `x - r` would recover `x`, which is why the helper role is explicit in the code.

**Normalisation.** The two implementations do **not** have the same security statement (§8.4). For `FssNormalizer`, the
gate opens `S + r` where `r` is `kappa = 40` bits wider than `S`, so the opening is statistically close to uniform and
`S_c` samples it. The DCF evaluations are local, and the Newton iterations are multiplications and truncations already
covered.

**Orthogonalisation (`SetOrthogonalPublic`).** Local, because every product is share times public. The rows of `B` it
projects against are already open, so there is nothing to simulate and nothing sent.

**Opening each row of `B`.** `B` is `F_train`'s declared output, so the simulator is **given** it and replays it. This
step is free in the simulation and is also the largest leakage surface in the system. That is not a contradiction: it is
leakage by design, and §4 measures what it costs.

**Serving (`ScoreShares`).** `B` is public and `a` is shared, so scoring is a linear map on shares: each server computes
locally. The seen-item mask arrives from the user as a share, and `S_c` samples uniform shares.

**Delivery (DPF-PIR).** `P_c` in `{P0, P1}` receives one DPF key. A single key is pseudorandom and independent of the
index by the DPF's security, so `S_c` samples a key of the right length, 227 bytes at `domain_bits = 11`. The server runs
`EvalFull` across the **whole** domain and an inner product over every index, so the work does not depend on the data.
`P2` receives nothing in this step.

**Harvesting.** `h0 - h1` is a 2-of-2 additive sharing across `P0` and `P1`, converted to 2-of-3 replicated by
re-randomising with a pairwise secret `P2` does not hold, so `S_c` samples uniform. Without that re-randomisation `P2`
would receive `h0` in the clear and could subtract its way to the counts.

### 8.4 Where the argument does not close

**1. `RevealNormNormalizer` is not simulable against `F_train` as written.** It opens one scalar per normalisation. Across
a run that is `d × (ell + 1)` scalars, the trajectory of the singular values of `U`, and no simulator given only `B` can
produce them, because they are not a function of `B`. We define a weaker functionality `F_train^leaky` whose output is
`(B, {||v||})` and claim security against *that*, because it is what we ran: **every `b = 64` quality number in
`train_quality.jsonl` came from the revealing path.** Claiming the strong statement while reporting numbers from the weak
one would be the dishonest version of this section. §7.6 gives the price of the path that does close: 0.0018 to 0.0041
nDCG@20, and 5.3× the rounds.

**2. Composition across training rounds is argued, not proven.** Each round is covered above, and harvesting adds no leak
per round. What an adversary learns from many rounds of `B`, about a user whose consumption feeds it, is open.

**3. Sequential composition is assumed, not established.** The protocols run one after another with fresh correlated
randomness, the standard setting for this kind of argument, but no composition theorem is invoked and no UC claim is made.

### 8.5 The other adversary classes

- **A3, two colluding servers.** Two parties hold all three shares and reconstruct. No simulator exists and none is claimed.
- **A4, a malicious server.** Out of scope. Every step above assumes the protocol was followed.
- **A5, a malicious user.** Corrupts only its own output for *retrieval*, but see §9 for the harvest path.
- **A6, a network observer.** Sees sizes and timing only. Sizes are input-independent by construction and by measurement.

## 9. The malicious client (A5)

### 9.1 The harvest path accepted an unbounded vote

OblivRec has no rating-upload path, since the harvest loop removes it, so there is no rating vector to validate. **But there
is a submission, and it was unchecked.** An honest client goes through `PirClient::Query`, which fixes `beta = 1` so the
difference of the two expansions is exactly the selector vector. Nothing stopped a client calling `Gen(alpha, beta,
domain_bits)` directly with any `beta`. Retrieval still works, since the record returns scaled by `beta` and the attacker
divides it out, and the servers fold `beta`, not 1, into the consumption accumulator that becomes the next round's training
input. **Every existing check passed.** `tests/test_harvest.cpp` demonstrates it: one query at `beta = 10⁶` puts 1,000,000
into the consumption vector.

Measured with `model/poison_study.py`:

| | nDCG@20 for honest users | Against clean |
|---|---|---|
| Clean model | 0.4536 | |
| 1 attacker, weight 10⁶ | 0.4493 | −1.0% |
| 8 colluders, distinct targets | 0.4351 | −4.1% |
| **16 colluders** | **0.0021** | **−99.5%** |

One attacker can do almost nothing, and **the reason is the normalisation**: power iteration normalises every step, so past
a point extra weight only fixes a *direction*. A single attacker captures one of the `d` components and the other `d−1` still
carry the honest signal. It cannot aim either: the targets it promotes do not rise (mean rank 901 → 973 of 1682 at weight 10⁶,
and 977 at weight 100).

**The collapse lies between 8 and 16 colluders.** Only 1, 2, 4, 8, 16 and 32 were run. Sixteen colluders with distinct target
sets capture all sixteen components and nothing honest is left, which fits `d = 16`, but the counts in between are untested.
This is model destruction and not promotion.

### 9.2 The check, and what it does not cover

Summation is linear, so each server can sum its own expansion locally, and

    Σⱼ e₀[j] − Σⱼ e₁[j]  =  beta

exactly, for any `alpha`. The two servers open that single scalar and require it to be 1, at **one round and two ring elements
per query**.

**Opening it leaks nothing.** The secret is `alpha`, which item was fetched. `beta` is a payload meant to be the public constant
1, and the sum does not depend on `alpha`. Each server's own sum is pseudorandom, and their difference is `beta` and nothing else.

**It closes weight inflation, not weight redistribution.** The sum bounds the total, not its distribution. A client that forged
correction words directly, rather than calling `Gen`, could produce a difference vector of `+2` at one index and `−1` at another,
which sums to 1, passes this check, and still skews two items. `tests/test_harvest.cpp` constructs exactly that forgery and shows
it passes. Proving a key encodes a genuine one-point function is what a Sabre-style audit does, and we did not build it.

### 9.3 The denial-of-service amplification

Measured by `bench/bench_dos.cpp`, in two committed runs:

| Domain bits | Key bytes | Amplification, first run | Second run |
|---|---|---|---|
| 8 | 173 | 15× | 15× |
| **11 (ours)** | **227** | **121×** | **142×** |
| 14 | 281 | 909× | 1,115× |
| 16 | 317 | 3,495× | 4,323× |

The key is `O(log N)` and the answer is `O(N)`, so **the amplification grows with the catalogue**, the opposite of what a defender
wants.

**The expensive attack is the one that looks legitimate.** A *malformed* key is cheap for the server: `DpfKey::Deserialize` rejects
it before any expansion, and `tests/test_dpf_serialize.cpp` covers the party byte, the `domain_bits` range and the exact length. So
parsing hardening does not address this, and the mitigation really is the audit we did not build.

That the server touches every index is not inefficiency. It is the privacy property, since any data-dependent shortcut would leak
which record was wanted. The exposure is **intrinsic to the design**, and it is the clearest example in this project of a privacy
mechanism paid for in availability.

### 9.4 The timing side channel, bounded (A6)

The wire's inter-frame timing is dominated by the OS scheduler and the loopback stack. What this project controls, and what a timing
attack would have to exploit, is whether **the server's work depends on `alpha`**. `bench/bench_timing.cpp` times `PirServer::Answer`
200 times at each of eight indices spanning the domain, **interleaved** so that thermal drift cannot line up with the index.

| | |
|---|---|
| Mean call | 87 µs |
| Spread between the per-`alpha` means | **1.7 µs** |
| Median within-`alpha` standard deviation | **6.0 µs** |
| Permutation test, shuffled `alpha` labels | **p = 0.60** |

The variation between indices is **smaller than the noise within a single index**, and the observed spread sits in the middle of the
null distribution.

**This is a bound, not a proof of constant time.** No finite sample shows a difference is exactly zero. It shows any `alpha`-dependent
signal is **below 2.0% of the mean call** at this sample size. That is consistent with the construction: `EvalFull` walks every node
and the answer takes an unconditional inner product over every record, so there is no data-dependent branch to find.
