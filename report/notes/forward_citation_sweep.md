# Forward-citation sweep (PHASES task 1.4)

**Run:** 2026-09-03, after the survey was drafted rather than before it, which is a deviation from
the order `PHASES.md` intended. Recorded here honestly for that reason.
**Run by:** Mainak.
**Why it matters:** `PHASES.md:100` flags this as "the one that can sink the survey". The survey's
central structural claim in Section 7 (`systems_both.tex`) and Section 9
(`open_problems.tex`) is that the private-training literature and the private-retrieval literature
occupy the two halves of the pipeline separately, and that nobody has composed them.

---

## The claim under test

Stated precisely, as it appears in `systems_both.tex`:

> The private-retrieval systems are, with the exception of Pirsona, not connected to privately
> trained models. They take a corpus and embeddings as given. The private-training systems produce
> models and scores and stop. Pirsona spans both, but predates the private-nearest-neighbour line
> almost in its entirety.

A counterexample would be any published system that both trains a recommender under cryptographic
privacy and privately delivers the recommended item, published after Pirsona (2021).

## Method, and its limits

Five searches from different framings, covering the composition directly, forward citations of
Nudge, the private-ANN line, the IACR eprint archive for 2026, and forward citations of Pirsona.

**Limitation, stated plainly:** this was a web-search sweep, not an exhaustive citation-graph
traversal. The Semantic Scholar API returned HTTP 429 and then 404 on the two attempts made, so
the citation lists of Nudge and Pirsona were not enumerated mechanically. A reviewer asking "did
you check every paper citing Nudge" should be told no, we checked the search-visible ones. Nudge
is a 2026 paper, so its citation count is small and this is less damaging than it would be for an
older work, but the gap is real and should be closed before the final report by enumerating
citations directly from Semantic Scholar or Google Scholar.

## Result: no counterexample found

Nothing surfaced that composes cryptographic private training with private retrieval or delivery.
What did surface falls into families the survey already classifies:

| What was found | Family | Already in the survey? |
|---|---|---|
| Panther, Pacmann, Tiptoe and the private-ANN line | Private retrieval, corpus and embeddings taken as given | Yes, Section 6 |
| ReuseKNN, DP-based KNN recommendation | Differential privacy | Yes, taxonomy Axis 2 |
| Federated recommenders (DGREC, NCF/BPR variants, POI work) | Federated, architecture not a privacy notion | Yes, Section 5.4 |
| Nudge itself and its reference implementation | Private training, delegates delivery by its own admission | Yes, Sections 5 and 7 |

The Pirsona search returned its PETS 2021 record and its author's own page, and surfaced no
successor system spanning training and delivery.

**Verdict: the Section 7 and Section 9 claims stand as written.** The hedged phrasing already in
the report ("this survey does not claim that composing these lines is straightforward, nor that no
unpublished work has done so") is exactly the right strength for the evidence we have, and this
sweep does not license strengthening it.

## Two items worth citing that this sweep newly surfaced

Neither is a counterexample, but both are relevant and neither is currently in `references.bib`.
Flagged for the final report rather than retrofitted into the submitted survey:

1. **"Private Information Retrieval: A Tutorial and Survey", IACR eprint 2026/1135.** A 2026 PIR
   survey. Section 1.2 of our survey makes claims about what existing surveys do and do not cover,
   and an examiner could reasonably ask whether we checked for a recent PIR survey. We should read
   at least its scope section before the final report and adjust Section 1.2 if it overlaps more
   than we assumed.
2. **"Retrieve-Compute PIR and Its Applications", IACR eprint 2026/1372.** Extends PIR toward
   computing on the retrieved record, which is adjacent to the delivery-stage composition our
   project is about.

## What this changes

Nothing in the submitted survey needs correcting. The value is that the claim is now checked
rather than assumed, and the check is on record for a viva. If asked "how do you know nobody has
done this", the answer is this file, including its stated limitation.
