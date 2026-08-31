# Narration Script — Privacy-Preserving Recommendation Systems
### CS670 Literature Survey · 30-minute recorded presentation · 4 speakers

**How to use this:** Each of the four parts is one speaker (~7–8 min). The text under each slide is written to be spoken aloud — read it naturally, paraphrase where it helps, and don't rush. Rough per-slide timings are in brackets. Total target ≈ 28–30 minutes at a normal speaking pace (~140 words/min). The **[TRANSITION]** lines are where you hand off to the next presenter.

---

## PART I — The Problem and the Map
**Speaker 1 · target ~8.5 min at a normal pace (Mainak speaks medium-to-fast, so real time should land closer to 7–7.5 min — still inside the 7–8 min per-member guideline)**

### Slide 1 — Title [0:20]
Hi everyone. Our literature survey is on *privacy-preserving recommendation systems*, and specifically the **cryptographic** approaches to building them. I'm Mainak , and I'll be presenting the first part along with my group members. The four of us split the talk into four sections, which I'll outline in a moment.

### Slide 2 — Outline [0:35]
Here's the roadmap. I'll start with the problem — why recommendation is a privacy risk in the first place — and set up the framework the whole survey is organized around: a four-stage pipeline, and a taxonomy for placing systems on it. Shrasti goes next, covering the cryptographic toolkit and private *training*. Our third and fourth speakers will then take private *retrieval and delivery*, and finally how this field evaluates itself and what remains open. Let's start with the problem.

### Slide 3 — Part I divider [0:05]
*(Pause on the section slide, then continue.)*

### Slide 4 — Why this is a problem [1:35]
So, why is this hard, and why does it matter? Recommender systems are built from the most revealing data users produce — what you watched, what you read, what you bought, what you clicked. That this data is *sensitive* is obvious. What's less obvious, and what really motivates this whole field, is that it's **identifying**.

The classic result here is Narayanan and Shmatikov, in 2008. They took the Netflix Prize dataset — which was released as *anonymized* movie ratings — and, using publicly available IMDb profiles as background knowledge, they re-identified specific users and uncovered what they called "apparent political preferences and other potentially sensitive information." And this isn't a fragile, one-off trick that only works under lab conditions: the authors themselves show the attack is "robust to perturbation in the data" and tolerates "some mistakes in the adversary's background knowledge" — so an attacker doesn't need clean data or perfect outside information, just *some*. So anonymizing the data by stripping names simply wasn't enough.

That result launched a large body of work asking a single question: can we build a recommender whose operator *never sees the underlying data*? And the answers come from several different research communities — secure multi-party computation, private information retrieval, oblivious memory, federated learning, and differential privacy. The problem with that, and the reason a survey is useful, is that these communities use different vocabulary, assume different threat models, and — crucially — protect *different parts of the system*.

### Slide 5 — The recommendation pipeline [1:20]
This is the single most important slide for understanding our whole survey, so let me spend a minute on it. The organizing idea is that a deployed recommender isn't one monolithic thing — it splits into **four stages**.

**Collection** is where ratings and clicks leave the user and reach the service. **Training** is where a model is fitted to that collected data. **Serving** is where, for a given user, the system computes scores and produces a ranking. And **delivery** is where the user actually fetches the recommended item — the film, the article, the product page.

Now here's the key insight. Different lines of research protect different *subsets* of these four stages. And that leads to a consequence that sounds simple but is easy to forget: **a pipeline is only as private as its weakest stage.** If you train a model under heavy cryptographic protection, but then the user fetches the recommended movie in the clear, you've leaked the recommendation anyway — the fetch itself reveals what the protocol was trying to hide. So privacy has to be reasoned about across the *whole* pipeline, not one stage at a time. Every system we survey takes a position on which of these stages it protects.

### Slide 6 — Utility constrains the cryptography [1:15]
There's a second constraint that shapes everything, and it's about *quality*. Recommendation quality is measured with ranking metrics — nDCG and recall at k. And the point is simple: a private recommender that recommends *badly* has solved nothing. Nobody will use it.

That constraint pushes right down into the cryptography. To make these protocols efficient, designers use fixed-point arithmetic, they approximate non-linear functions, they reduce precision — and all of that has to happen *without* messing up the ranking. So there's a real tension: cryptographic primitives naturally compute over rings and finite fields, but recommendation fundamentally computes over the *real numbers*. Bridging that gap is where a huge fraction of the practical cost lives, and my teammate will come back to this in detail. The takeaway for now: in this field, you always report *quality alongside cost*, because a fast protocol that ruins recommendations is not a solution.

### Slide 7 — A taxonomy along four axes [1:15]
So how do we compare systems that all claim to do "private recommendation" but protect different things? The survey proposes four axes.

Axis one is the **trust model** — who has to be trusted? A single server, several non-colluding servers, a federated setup, or trusted hardware. Axis two is the **privacy notion** — what does "private" even mean formally? A cryptographic simulation-based guarantee, differential privacy, or just anonymity. Axis three is the **pipeline stage** we just saw — which of collection, training, serving, delivery does the system actually protect. And axis four is the **adversary** — semi-honest versus malicious, and how many parties can be corrupted.

And the crucial point — the reason we use four separate axes instead of one score — is that these axes are **not orderable by strength**. A system that needs two non-colluding servers isn't simply "weaker" than one that needs none; it's a *different assumption*, better in some deployments and worse in others. That's the mindset the whole survey adopts.

### Slide 8 — Placing the systems (condensed) [1:00]
This is a condensed version of the big table from our report. You don't need to read every row — the point is the *shape*. We group systems into recommendation systems, private-retrieval and nearest-neighbour systems, pure private information retrieval, and oblivious-memory primitives. And for each, we record its trust model, its privacy notion, which pipeline stage it covers — collection, training, serving, delivery — and its adversary model.

Notice a couple of things even at a glance. Almost everything in the "adversary" column says *semi-honest*. And the "stage" column is very uneven — which is exactly what the next slide is about.

### Slide 9 — Three observations from the table [1:05]
Reading that table carefully, three observations fall out, and each one gets developed later in the talk.

First, **the stage column is uneven.** Training and serving are well covered, but *delivery* is addressed almost entirely by the private-information-retrieval line, in isolation. In fact only one system, Pirsona, spans collection, training, and delivery together.

Second, **cost is bought with assumptions, consistently.** The cheapest protocol in each group is almost always the one assuming non-collusion. The moment you remove that assumption and go single-server, you pay for it.

And third, **semi-honest is the norm at scale.** Every recommendation system evaluated at realistic scale assumes a passive adversary. The malicious-security exceptions exist, but they're narrow, and none of them is a full pipeline.

That framing — the pipeline and these four axes — is the lens for everything that follows. I'll hand over to Shrasti, who'll take us into the cryptographic building blocks and private training.

**[TRANSITION → Speaker 2]**

---

## PART II — Building Blocks and Private Training
**Speaker 2 · target ~7.5 min**

### Slide 10 — Part II divider [0:10]
Thanks, Mainak. I'm going to cover the cryptographic toolkit these systems are built from, and then the first technical half of the pipeline: private *training*.

### Slide 11 — Cryptographic building blocks [1:40]
Let me quickly introduce the primitives, because the rest of the talk is really about how systems combine these. The thing to pay attention to for each one is its *cost profile* — does it cost communication, computation, or rounds of interaction?

**Secret sharing** splits a value across parties. Addition is essentially free, but multiplication needs either precomputed randomness or extra rounds of communication. This is the workhorse of modern MPC.

**Garbled circuits** let two parties evaluate a Boolean circuit in a constant number of rounds. But the cost is bandwidth proportional to the circuit *size* — which becomes the binding constraint when your algorithm has to be data-oblivious.

**Homomorphic encryption** lets you compute directly on ciphertexts. Its importance here is structural: it's what enables *single-server* constructions, removing the non-collusion assumption — but at a computational price.

**Function secret sharing** and distributed point functions share a *function* rather than a value, and their key property is *compression* — keys are logarithmic in the domain size. That's what makes private reads and writes practical at scale.

**Private information retrieval**, or PIR, lets a client fetch record j without the server learning j. And **oblivious RAM** hides *access patterns* — not the contents, but which locations you touched. In the MPC setting that becomes distributed ORAM, used by systems like Floram and Duoram.

### Slide 12 — Fixed-point arithmetic dominates the cost [1:10]
Now, my teammate mentioned the tension between rings and real numbers — let me make it concrete, because this is where a lot of the cost actually goes. A real number v gets stored as v times two-to-the-t, rounded — that's fixed-point representation.

Addition of these is free. But when you *multiply* two of them, the result is scaled by two-to-the-*two*-t, and you have to **truncate** it back down. And if you keep multiplying, values grow without bound, so you need **normalization**. Both truncation and normalization are *non-linear* operations, and non-linear operations are exactly what's expensive under secret sharing.

So the recurring theme across all of private training is this: the *linear algebra is cheap, and the non-linear steps are not*. SecureML, for instance, is remembered less for its linear algebra and more for its "MPC-friendly alternatives to non-linear functions like sigmoid and softmax." And whole frameworks like ABY and MP-SPDZ exist precisely because different operations are cheapest under different sharing schemes.

### Slide 13 — Private training (1): the garbled-circuit era [1:10]
Okay — private training. Every system in this section is computing the same underlying object: a low-rank factorization of a rating matrix that nobody is allowed to see.

The first practical system was Nikolaenko and co-authors, in 2013. They ran gradient descent *inside a garbled circuit*, across two non-colluding servers. And the cost profile is exactly what you'd predict from the building-blocks slide: because the computation has to be data-oblivious, the circuit is sized by the *entire* rating matrix, and communication follows. To put a number on it, the later Nudge system reports that this whole family needs over *four orders of magnitude* more communication than secret-sharing approaches.

The broader lesson is architectural, not about any one paper: a circuit-based approach pays for every single operation in circuit size. Secret sharing inverts that relationship — and that's what the systems after this one exploit.

### Slide 14 — Private training (2): HE and the secret-sharing line [1:10]
Two threads follow. The first is **homomorphic encryption**. It's attractive because it removes the non-collusion assumption — you keep the data encrypted at a single server. But fully homomorphic evaluation of a whole training loop is expensive, so in practice the literature applies it *surgically*. FedMF is the good example: it uses homomorphic encryption only on the one channel that actually leaks — the gradient uploads — rather than encrypting the entire computation.

The second thread, and the one that's advanced furthest, is **secret-sharing MPC**. SecureML established the two-server pattern, and told us where the difficulty really lies — again, the non-linearities, not the linear algebra. SecureNN then moved to three parties and gave concrete protocols for things like ReLU and maxpool, reporting inference six to over a hundred times faster than prior work. Notice the pattern: every one of these papers' headline contributions is about the *non-linear* layers.

### Slide 15 — Private training (3): Nudge [1:20]
This brings us to Nudge, which is the current reference point for the field, and its key idea is really elegant. Instead of just optimizing an existing algorithm, Nudge **changes the algorithm** to fit the cryptography.

Here's the reasoning. Under two-out-of-three replicated sharing, a matrix–vector product is extremely cheap. So Nudge throws out gradient descent and replaces it with **power iteration** — an algorithm that's mostly matrix–vector products with only a few non-linear steps. It keeps the user embeddings secret-shared, while revealing the item embeddings in the clear — and that last choice matters later in the talk.

And the headline result is the strongest evidence in the whole survey that privacy need not cost accuracy. On the full Netflix dataset — half a million users, ten thousand items — running on three 192-core servers, Nudge trains a private recommender in about 50 minutes and scores **nDCG@20 of 0.29**. That's on par with *non-private* matrix factorization, and just shy of non-private neural recommenders at 0.31. And it holds that guarantee even against an adversary that compromises the *entire* secret state of one server. So the cost of privacy here is paid in compute and communication — *not* in the quality of what users get recommended.

### Slide 16 — Where each training family stops [1:00]
Let me summarize the training half in one slide — where each family hits its limit. **Garbled circuits** stop at *scale*, because circuit size grows with the data. **Homomorphic encryption** stops at *cost*, which is why it's used on specific channels rather than whole loops. **Secret-sharing MPC** stops at the *trust assumption* — it's fast now, but it needs two or three non-colluding, semi-honest parties. **Federated learning** stops at the *guarantee* — raw gradient uploads leak without an extra mechanism on top. And **differential privacy** stops at *utility and scope* — it protects the output, not the computation.

But there's one limit *none* of these families addresses: every system here ends with a *model*, or with *scores*. None of them delivers the actual recommended *item* to the user. And that's the problem [name] will pick up now.

**[TRANSITION → Speaker 3]**

---

## PART III — Private Serving, Retrieval, and Multi-Stage Systems
**Speaker 3 · target ~6.5 min**

### Slide 17 — Part III divider [0:10]
Thanks, Shrasti. So training gives us a model and some scores — but the user still has to *fetch* the recommended item. My section is about that second half of the pipeline: serving and delivery, and the handful of systems that try to do both halves at once.

### Slide 18 — The obstruction: indices are data-dependent [1:15]
After training, two problems remain: compute a ranking for a user without revealing their profile, and fetch the top item without revealing which item it was. Both of these reduce to **retrieval**, and interestingly this has mostly been studied by a *different* community, under the name *private nearest-neighbour search* rather than recommendation.

Now here's why retrieval is fundamentally hard to make private. Retrieval in the clear is fast *because of indices* — a k-d tree, an inverted file, a navigable graph. But an index is useful *precisely because* the path you take through it depends on your query. And that's exactly what obliviousness forbids, because the access pattern would leak the query.

So the field has one organizing trade-off. Either you **scan the whole corpus** on every query — which is oblivious, but costs you time linear in the corpus size. Or you **keep the index and hide the traversal** — which needs oblivious memory, and all the overhead that comes with it. Every system I'm about to show takes a position on this trade-off.

### Slide 19 — Retrieval approaches (1): linear scan [1:20]
Let's start with the linear-scan side. The origin point is Sanns, which explicitly frames secure k-nearest-neighbour search as the backbone of recommender systems. It offers an optimized linear scan plus a sublinear clustering variant, built from homomorphic encryption, distributed ORAM, and garbled circuits — and notably it treats even *top-k selection* as its own protocol-design problem.

The largest-scale system on this side is Tiptoe. It reduces private full-text search to private nearest-neighbour search using semantic embeddings, implemented with linearly-homomorphic encryption. And its trust model is the *strongest* in this whole section — the authors are explicit that the privacy guarantee is "based on cryptography alone; no hardware enclaves, no non-colluding servers." On a 45-server cluster searching 360 million web pages, it reports about 145 core-seconds of compute, 57 megabytes of communication, and 2.7 seconds end-to-end. And on quality, it ranks the best result at position 7.7 on average — worse than a non-private neural search algorithm at 2.3, but close to classical tf-idf at 6.7. That honest, side-by-side quality reporting is a model for the field.

### Slide 20 — Retrieval approaches (2): index-based and relaxed [1:30]
On the other side of the trade-off are the index-based approaches. Pacmann moves the work *to the client* — it runs a graph-based nearest-neighbour search where the client privately retrieves local graph information using a preprocessing-style PIR scheme. It reaches about 90% of the quality of a non-private algorithm, with up to 62% less computation on 100-million-vector datasets. Compass takes the opposite tack and keeps the index *server-side*, hiding the traversal with a white-box co-design of oblivious RAM — and it claims the strongest adversary model here: privacy of data, queries, *and* results, even if the server is compromised.

Then there are systems that relax the *guarantee* rather than the trust model. Panther works in the single-server setting and co-designs four primitives together, answering a nearest-neighbour query on 10 million points in 18 seconds. And Wally is really interesting — it relaxes the privacy *notion* to **differential privacy** to break the linear-scan cost barrier. It batches queries from many clients and has each one add a few "fake queries" to obscure which cluster it actually wants.

And one structural point, from Asharov and co-authors, who study similar-patient search on genomic data: their setting shows that the *answer itself* can be sensitive. Protecting the computation does not protect the answer — a theme our last speaker will return to.

### Slide 21 — Systems spanning multiple stages [1:20]
Now, the systems that attempt more than one stage at once. There are really two.

Pirsona spans the most stages — collection, training, *and* delivery. It treats private information retrieval and collaborative filtering, which the authors call "seemingly antithetical primitives," together. Delivery uses a multi-server PIR, and cleverly, *collection is folded into delivery*: the servers extract consumption histories from the query traffic itself, which turns the pipeline into a *cycle* rather than a line. The price it pays is a strong assumption — an s-plus-one-server, pairwise non-colluding model.

The other is Nudge, which we saw in training — it also spans collection, training, and serving. Users secret-share their ratings, the servers train and compute scores in shared form, so no server ever learns the ranking. But Nudge is explicit that fetching the item is a *non-goal* — it relies on external tools like Apple's private relay or Tor for that last step.

And composing stages is genuinely hard, for three reasons the survey identifies: the trust models of the two halves have to *agree*; the roles assigned to each party need not match up; and the *interface* between stages itself carries information.

### Slide 22 — What the field has and has not built [1:00]
So if you read that big table by *column* instead of by row, here's the state of the field. The training stage is well served. The serving stage is well served. But **delivery** is addressed almost entirely by the PIR literature, in *isolation* from any actual recommender.

The gap in one sentence: private-retrieval systems take a corpus and embeddings *as given*, and private-training systems produce models and *stop*. The one system that spans both — Pirsona — *predates* that private-nearest-neighbour line almost in its entirety: it published in 2021, before Tiptoe in 2023 and the whole 2024-to-2025 wave — though Sanns, in 2020, actually came first. So the two halves of this problem have basically been solved *separately*. And that observation sets up our final section. Over to [name].

**[TRANSITION → Speaker 4]**

---

## PART IV — Evaluation, Open Problems, and Conclusion
**Speaker 4 · target ~6.5 min**

### Slide 23 — Part IV divider [0:10]
Thanks, [name]. I'll close by looking at how this field *measures* itself — which turns out to be surprisingly subtle — and then walk through the open problems and our conclusion.

### Slide 24 — How this literature evaluates itself [1:35]
We included a whole section on evaluation, because if you take the numbers in these papers at face value you'll draw the wrong conclusions. Let me explain why.

On **datasets**: recommendation systems evaluate on MovieLens and the Netflix Prize data, while retrieval systems evaluate on things like MS MARCO or large vector corpora. There is *no* shared benchmark that spans both halves of the pipeline — which is a direct consequence of the split my teammate just described.

On **quality metrics**: it's nDCG and recall at k, and the best practice — which the strongest papers follow — is to report quality *against a non-private baseline*. Nudge saying 0.29 versus 0.31, or Tiptoe saying rank 7.7 versus 2.3, so you can see exactly what privacy costs.

On **cost**: three quantities trade off against each other — local computation, bandwidth, and round complexity — and which one dominates depends *entirely* on the deployment. A number quoted without its network model, hardware, and dataset is simply not a result.

And that's why **cross-paper comparison is unsound**. Any two systems differ *simultaneously* along many axes — dataset, scale, hardware, network model, adversary, number of non-colluding parties, and pipeline stage. So instead of building a misleading league table, our survey deliberately reports each system in *its own authors' terms*. This was a real methodological choice on our part, not an omission.

### Slide 25 — Open problems (from the authors themselves) [1:15]
The open problems we highlight aren't a wish list — they're limitations the surveyed authors state about their *own* work. Three of them are structural.

First, **malicious security at scale.** Nearly every system that runs at realistic scale is semi-honest — it assumes the adversary follows the protocol. The exceptions that achieve malicious security are all narrower than a full recommendation pipeline.

Second, **reducing the non-collusion assumption.** The fastest protocols in every family assume non-colluding parties, and that's an assumption about *organizations*, not about mathematics. Whether you can build a full pipeline in the single-server model at competitive cost is open.

Third, **the delivery stage** — the gap we keep coming back to. It's handled by PIR in isolation, and composing it with a privately trained recommender is essentially unanswered in the literature.

### Slide 26 — Open problems — the deepest one [1:20]
But the deepest problem is a different kind, and I want to be clear that it is *not* a protocol failure. It's this: **the recommendation itself leaks.**

Cryptographic privacy guarantees that nothing leaks *beyond the agreed output*. But it says nothing about what the *output itself* reveals. And a recommendation *is* a statement about the user — delivered to a system that gets to observe whether the user acts on it.

Nudge makes this concrete: it reveals the item embeddings and the recommendation scores by design. That's not a bug — it's what the system is *for* — but it's information that no amount of extra cryptography *inside* the protocol would remove. The tool that actually addresses this class of leak is **differential privacy**, at some cost to utility. And that's the real point: this leak survives *any* protocol, because it's carried by the recommendation itself. No improvement in protocol design fixes it.

### Slide 27 — The direction we intend to pursue [0:50]
That leads directly into where our own course project is headed. We want to engage with the composition problem — specifically, connecting a privately *trained* recommender with a private *delivery* mechanism, which is the gap most visible in our table.

The two concrete questions we take away from this survey are: first, what does it actually *cost* to compose a modern private-retrieval backend with a privately trained recommender, given all the composition obstacles we discussed? And second, does Pirsona's clever collection-from-delivery trick — harvesting consumption histories from PIR query traffic — transfer to a *modern* training substrate?

### Slide 28 — Conclusion [1:10]
To conclude. The single message of our survey is that privacy-preserving recommendation is **not one problem but four** — one for each stage of the pipeline. Reading the literature through that lens is what lets you see why systems that *look* like competitors often aren't.

Two of those stages are in good shape: private training has reached the point where cryptographic privacy costs compute and communication rather than recommendation quality — Nudge's nDCG on par with plain matrix factorization is the proof. Delivery is served by a mature PIR literature, but — with one exception — it isn't connected to any recommender. That exception, Pirsona, is exactly why the gap is worth naming: it shows the stages *can* be addressed together, but its training core has since been superseded.

And the deepest issue — the leakage carried by the recommendation itself — survives *any* protocol, which makes it the most fundamental open problem in the area.

### Slide 29 — Thank you [0:15]
That's our survey. Thank you all for watching, and we're happy to take any questions.

---

### Timing summary
| Part | Speaker | Slides | Target (read at ~140 wpm) |
|------|---------|--------|--------|
| I — Problem & Map | 1 (Mainak) | 1–9 | ~8.5 min (Mainak's own pace is faster — expect ~7–7.5 min) |
| II — Building Blocks & Training | 2 (Shrasti) | 10–16 | ~7.5 min |
| III — Serving, Retrieval & Multi-stage | 3 | 17–22 | ~6.5 min |
| IV — Evaluation & Open Problems | 4 | 23–29 | ~6.5 min |
| **Total** | | **29** | **~29.5 min at 140 wpm; less in practice** |

Per-slide bracket sums were re-derived directly from the `[m:ss]` tags after Part I was elaborated, rather than eyeballed — Part III and IV's old labels (~7.5 / ~7.0 min) had drifted about 30–55 seconds above their own bracket sums even before this edit; those are now corrected too, not just Part I's.

**Delivery tips:** Practice once with a timer — if a part runs long, the easiest cuts are the per-system detail numbers (you can say "roughly" instead of exact figures). Don't read the tables aloud row by row; point to the shape and say what it means. And rehearse the four transition lines so the handoffs feel smooth on camera.
