# Plan for the stretch before the mid-term (Sep 3 to Sep 12)

**Status: PROPOSED.** This is written to be discussed and adjusted at our kickoff, not a plan
already locked in. Nothing here should be started until we have agreed on it together.

---

## 1. Where we actually are

Milestone 1 (Literature Survey) is submitted: report and slides done, 10% of the grade. That
work is behind us.

We are now starting Milestone 2 (Mid-term report), due **12 September**, worth 5%. As of today
(3 September) we have **9 days left and no implementation code written yet**. This document is
about closing that gap honestly rather than pretending we are further along than we are.

## 2. What Milestone 2 actually requires

The handout's own wording for this milestone is: *"a mid-term project report which is a two-page
summary describing the contributions of each group member."* That is the graded deliverable. It
does not, by itself, require a working prototype.

Our own `PHASES.md` set a more ambitious internal target for this same window, a first working
slice of the system (private serving and delivery, against a model trained in the clear). That
target is still worth aiming for, because it gives us something real to write the two-page report
about, but it is a stretch goal for this plan, not the bar we are graded against.

## 3. What the professor has told us, and why it matters here

The professor has confirmed there are no rigid deliverables for this project. It depends on how
much each of us learns and can integrate, we are free to skip novelties that turn out to be too
hard to implement or understand, and he specifically advised against over-complicating things
unless we are confident in what we are building. That guidance is the reason this plan is
deliberately smaller than the full task list in `PHASES.md`'s Phase 2. We would rather show four
small, genuinely understood, correctly working pieces than one ambitious pipeline that nobody can
defend in a viva.

## 4. The technical target, scoped down

The full system has two halves: private training and private delivery. This stretch is about the
delivery half only, running against a model trained without any cryptography, on MovieLens-100K.
Private training itself does not start until Phase 3, from 13 September.

### Must have by Sep 12 (realistic for 9 days, starting from zero)

1. A basic two-party distributed point function (DPF), correctness-tested against a brute-force
   point function on a small domain.
2. A cleartext power-iteration recommender in Python on MovieLens-100K, reporting nDCG@20. This
   is the quality oracle every later privacy-preserving version gets compared against. No
   cryptography involved, so it is the fastest thing on this list to get right.
3. A minimal three-process skeleton that can secret-share a single value across three processes
   and reconstruct it. This is the seed of the replicated-sharing substrate the rest of the
   project builds on.
4. `docs/contributions.md` kept up to date after every session, so the two-page report writes
   itself from real logged work rather than being reconstructed from memory on Sep 11.

None of these four depends on any of the others being finished first, so nobody is blocked
waiting on a teammate.

### Worth attempting if the must-haves land early, but not required

5. Using the DPF from item 1 to fetch an actual film record from a small catalogue (the private
   delivery step proper).
6. Oblivious top-k selection over secret-shared scores.
7. A first rough paragraph of the threat model, stating our trust assumptions in one page.

### Explicitly out of scope for this stretch

Anything from Phase 3 onward: `Trunc_t`, `ApproxNormalize`, power iteration actually running under
secret sharing, WAN benchmarking, the B1 through B5 baseline comparisons, Docker packaging. These
are real work for the following month, not this window.

## 5. Proposed ownership

Nobody has been formally assigned an implementation stream yet, this document is meant to settle
that. Based on the workstream descriptions already in `REQUIREMENTS.md §10`:

| Stream | Proposed owner | Task for this stretch |
|---|---|---|
| W1, FSS core and delivery | Mainak | Item 1, the DPF and its correctness tests |
| W2, 3PC substrate | Aditya | Item 3, the three-process share and reconstruct skeleton |
| W3, factorization and serving | Shrasti | Item 2, the cleartext quality oracle |
| W4, evaluation and security | Shravan | Item 4 plus item 7, the contribution log and the first threat-model paragraph |

Open exactly for discussion: whether this split feels right to everyone, and whether Shravan is
in fact taking W4, since that was only ever proposed, never confirmed with him directly.

## 6. Suggested timeline

| When | What |
|---|---|
| Sep 3 to 4 | Kickoff sync: agree or adjust this plan, confirm ownership, everyone sets up their toolchain (CMake and C++17 for the DPF and sharing work, a Python environment for the oracle) |
| Sep 5 to 8 | Independent work on the four must-have items |
| Sep 9 | Checkpoint sync: what is actually working, and whether any of the nice-to-have items are realistic in the time left |
| Sep 10 | Start drafting the two-page report from `docs/contributions.md` |
| Sep 11 | Finalize and review the report |
| Sep 12 | Submit |

## 7. What counts as success on Sep 12

An honest two-page report describing real work, even if modest, in line with what the professor
told us directly. It is a genuine bonus, not a requirement, if the DPF-based fetch from item 5
works end to end by then.
