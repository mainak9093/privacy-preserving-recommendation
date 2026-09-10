# OblivRec — threat model and leakage profile

**Status:** updated 2026-09-11 (networking landed; see §5). Covers **S1 only** (private serving and delivery). Private
training is S2 and no claim below applies to it.

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
- **No claim about private training.** The model is factorised in the clear.
- **Semi-honest only.** A deviating server is out of scope.
- **No formal proof.** The argument is a construction-level one, not a simulation-based proof.

## 6. Open items

| | Item | Where |
|---|---|---|
| D9.3 | Input validation against malicious users poisoning model quality | Phase 3+ |
| D9.4 | Sabre-style audit for malformed keys (A5 DoS) | stretch |
| — | ~~Channel transcript + distinguisher experiment (A6)~~ | **done 2026-09-11** |
| — | Constant rating count to close the nnz leak | evaluate against the utility cost |
| — | Timing side channel on the wire (frame *sizes* are now shown constant; inter-frame *timing* is not analysed) | Phase 3 |

---

*Sources: `design/ARCHITECTURE-draft-v1.md §9`; measurements from `model/attack.py` and
`bench/bench_baseline.cpp`, raw rows in `bench/results/leakage_attack.jsonl`,
`bench/results/bench_baseline.jsonl` and `bench/results/distinguisher_result.jsonl`, figures
regenerated by `mingw32-make figures`.*
