# Evidence file — what we may assert about each paper, and why

**This file is the anti-hallucination substrate for the survey.** Sections in `sections/*.tex`
are assembled *from this file*, not from memory. If a sentence in the survey makes a claim about
a paper, that claim must appear below with its source. If it does not appear here, it does not
ship.

Written 2026-08-29, after the bibliography was rebuilt from DBLP and the downloaded PDFs were
audited (see [`../../references/INDEX.md`](../../references/INDEX.md) — an earlier download pass
silently fetched 11 wrong papers, all now removed).

---

## The tier system

| Tier | Evidence held | May assert | May **not** assert |
|---|---|---|---|
| **A** | Full PDF read | Mechanism, direct quotes, specific numbers, stated limitations | — |
| **B** | Abstract extracted from the verified PDF | Problem addressed, setting (parties, trust model), headline claim **as the abstract states it**, author-stated limitations | Internal mechanism detail; any number not in the abstract |
| **C** | DBLP metadata only, no PDF | Placement in the taxonomy table; title-level categorisation | Any prose claim about content **except** via a Tier A/B source that describes it (see below) |

**Tier C with secondary attribution.** Several Tier C papers are important enough that the survey
must discuss them (Nikolaenko et al., Yao, Shamir, Paillier). Where a Tier A/B paper we *did* read
describes them, we may state what that source reports, **attributed**: *"Nudge reports that
Nikolaenko et al.\ require 29 hours on a subset of MovieLens-100K \cite{nudge2026}"* — not
*"Nikolaenko et al.\ require 29 hours"*. This is standard practice for classics and is honest
because the attribution is visible.

**Numbers rule.** Every performance figure carries its setting (hardware, network, dataset) or it
is not cited.

---

## Tier A — read in full (5)

Detailed notes already exist; do not duplicate them here.

| Key | Paper | Notes file |
|---|---|---|
| `nudge2026` | Nudge: A Private Recommendations Engine (USENIX Sec'26) | [`nudge.md`](nudge.md) |
| `pirsona2021` | PIRSONA: Recommendation Systems Meet PIR (PoPETs'21) | [`pirsona.md`](pirsona.md) |
| `duoram2023` | Duoram (USENIX Sec'23) | [`doram.md`](doram.md) |
| `prac2024` | PRAC (PoPETs'24) | [`doram.md`](doram.md) |
| `grotto2023` | Grotto (CCS'23) | [`grotto.md`](grotto.md) |

`nudge2026eprint` is the same paper as `nudge2026`; cite the USENIX version.

---

## Tier B — abstract extracted and verified (35)

Every entry below is grounded in the abstract of the verified PDF in `references/`. Quoted phrases
are the authors' own words.

### Recommendation background — survey §2

**`liang2018`** — *Variational Autoencoders for Collaborative Filtering* (WWW 2018).
Extends VAEs to CF for implicit feedback; a "non-linear probabilistic model" going "beyond the
limited modeling capacity of linear factor models which still largely dominate collaborative
filtering research". Abstract claims it "significantly outperforms several state-of-the-art
baselines, including two recently-proposed neural network approaches". *Use for:* establishing
that neural CF exists and claims superiority — then immediately contrast with `dacrema2019`.

**`dacrema2019`** — *Are We Really Making Much Progress? A Worrying Analysis of Recent Neural
Recommendation Approaches* (RecSys 2019). Systematic analysis of algorithmic proposals for top-*n*
recommendation, motivated by "problems in today's research practice… in terms of the
reproducibility of the results or the choice of the baselines". *Use for:* justifying why the
cryptographic literature targets matrix factorisation rather than deep models — the reproducibility
critique is why MF remains the sensible target. **Do not state the paper's specific findings** —
the abstract text extracted was interleaved with the introduction and is not clean enough to quote.

**`ammadudin2019`** — *Federated Collaborative Filtering* (arXiv 2019).
Claims "the first federated implementation of a Collaborative Filter", updates "based on a
stochastic gradient approach", evaluated on MovieLens and an in-house dataset. Author claim:
"a collaborative filter can be federated without a loss of accuracy compared to a standard
implementation". *Use for:* the federated family in the taxonomy (§3, §5).

**`narayanan2008`** — *Robust De-anonymization of Large Sparse Datasets* (IEEE S&P 2008).
Presents "a new class of statistical de-anonymization attacks against high-dimensional micro-data,
such as individual preferences, recommendations, transaction records". Applied to the **Netflix
Prize dataset** — "anonymous movie ratings of 500,000 subscribers" — and using IMDb as background
knowledge, the authors "successfully identified the Netflix records of known users, uncovering
their apparent political preferences and other potentially sensitive information". They note the
attack is "robust to perturbation in the data and tolerate[s] some mistakes in the adversary's
background knowledge". *Use for:* §2 — the primary evidence that recommendation data is
identifying, not merely sensitive. **Note:** the local PDF is the arXiv preprint (cs/0610105),
whose title reads *"Robust De-anonymization of Large Datasets (How to Break Anonymity of the
Netflix Prize Dataset)"*; cite the published S&P 2008 version.

### Private training — survey §5

**`chai2021`** — *Secure Federated Matrix Factorization* (IEEE Intell. Syst. 2021).
Proposes **FedMF**. Two-part claim worth quoting: users upload only gradients, but the authors
"prove that it could still leak users' raw data", and they therefore add homomorphic encryption.
*Use for:* the strongest citation that **federated ≠ private** — the authors of a federated system
proving their own gradient channel leaks. This is a load-bearing point for §3 (privacy notions)
and §5.

**`secureml2017`** — *SecureML* (IEEE S&P 2017).
Privacy-preserving linear regression, logistic regression and neural-network training via SGD.
**Two-server model**, data owners "distribute their private data among two non-colluding servers".
Contributes "new techniques to support secure arithmetic operations on shared decimal numbers" and
"MPC-friendly alternatives to non-linear functions such as sigmoid and softmax". *Use for:* the
fixed-point / non-linear-function problem, and the two-server trust model.

**`securenn2019`** — *SecureNN* (PoPETs 2019).
**Three-party** protocols for NN building blocks (matrix multiplication, convolutions, ReLU,
Maxpool, normalization) "such that no single party learns any information about the data".
Claims secure inference outperforming prior 2- and 3-server work by 6×–113×, on Amazon EC2.
*Use for:* the 3PC honest-majority family; the recurring pattern that non-linear layers are the
expensive part.

**`aby22021`** — *ABY2.0* (USENIX Sec 2021).
Mixed-protocol **semi-honest 2PC over rings**, focused on online-phase efficiency. Notable claim:
"The online communication of our scalar product is two ring elements irrespective of the vector
dimension, which is a feature achieved for the first time in the 2PC literature." Benchmarks
training and inference of logistic regression and NNs **over LAN and WAN**. *Use for:* §4
(mixed-protocol frameworks) and §8 (WAN evaluation is standard practice in this literature).

**`mpspdz2020`** — *MP-SPDZ* (CCS 2020).
A framework implementing **34 MPC protocol variants** behind one Python interface, covering
"honest/dishonest majority and semi-honest/malicious corruption" and binary and arithmetic
circuits. *Use for:* §8 — MP-SPDZ is the standard general-purpose baseline, and its breadth is
precisely what makes it a fair-but-slow comparison point.

**`gentry2009`** — *Fully Homomorphic Encryption Using Ideal Lattices* (STOC 2009).
The bootstrapping result: it suffices to build a scheme that can evaluate its own (augmented)
decryption circuit. *Use for:* §4, one paragraph establishing FHE exists and why it is expensive.
**Do not go further** — we are not qualified to summarise its internals from the abstract alone.

### Cryptographic primitives — survey §4

**`bgi2016`** — *Function Secret Sharing: Improvements and Extensions* (CCS 2016).
Defines FSS: an *m*-party scheme splits `f: {0,1}^n → G` into keys such that `f = f_1 + … + f_m`
and every strict subset of keys hides `f`. A **DPF** is the special case where `F` is the family
of point functions. States the applications directly: "privately reading from or writing to
distributed databases while minimizing the amount of communication… different flavors of private
information retrieval (PIR), as well as a recent application of DPF for large-scale anonymous
messaging." Reduces the PRG-based DPF key size of Boyle et al. "roughly by a factor of 4".
*Use for:* the definition of FSS/DPF in §4 — this is the citation to lean on, since `bgi2015` is
Tier C.

**`boyle2021`** — *FSS for Mixed-Mode and Fixed-Point Secure Computation* (EUROCRYPT 2021).
Builds on the offset-family approach: a gate `g` is evaluated using an FSS scheme for
`g_r(x) = g(x+r)`. Provides FSS for "zero test, integer comparison, ReLU, and spline functions",
and states the benefit plainly: "significant savings in online communication and round complexity
compared to alternative techniques based on garbled circuits or secret sharing". Reports "roughly
4× reduction in key size for Distributed Comparison Function (DCF)". *Use for:* §4 — how FSS
becomes a general secure-computation technique, and the DCF definition Grotto and LLAMA build on.

**`escudero2020`** — *Improved Primitives for MPC over Mixed Arithmetic-Binary Circuits*
(CRYPTO 2020). Introduces **edaBits** — "shared integers in the arithmetic domain whose bit
decomposition is shared in the binary domain" — used to "considerably increase the efficiency of
non-linear operations such as truncation, secure comparison and bit-decomposition". Best suited to
dishonest-majority protocols such as SPDZ. *Use for:* §4/§5, the arithmetic↔binary conversion
problem that makes truncation expensive. Nudge cites this line for its low-order carry correction.

**`lindell2017`** — *How To Simulate It* (tutorial, 2017).
A guide to writing simulators and proving security via the simulation paradigm. *Use for:* §3, the
definition of cryptographic (simulation-based) privacy.

### PIR — survey §4 and §6

**`hafiz2019`** — *A Bit More Than a Bit Is More Than a Bit Better* (PoPETs 2019).
Multi-server PIR "wherein several untrusted servers work to obliviously service remote clients'
requests… and yet no pair of servers colludes". Claims efficiency "with respect to every cost
metric — download, upload, computation, and round complexity". Extends Shah–Rashmi–Ramchandran,
whose property is that fetching a *b*-bit record needs only *b*+1 bits of download; allowing
"a bit more" download yields a family of tradeoffs that includes as special cases the 2-server
instances of Chor et al. (FOCS 1995) and the DPF-based protocol of Boyle et al. (CCS 2016).
*Use for:* §4 (this is the scheme PIRSONA builds on) **and** as the secondary source for
`chor1995` (Tier C).

**`simplepir2023`** — *One Server for the Price of Two* (USENIX Sec 2023).
**SimplePIR**, "the fastest single-server private information retrieval scheme known to date",
security under LWE. Reports "10 GB/s/core server throughput", approaching "the performance of the
fastest two-server private-information-retrieval schemes (which require non-colluding servers)".
The honest cost, stated by the authors: "relatively large communication costs: to make queries to
a 1 GB database, the client must download a 121 MB 'hint'"; thereafter each query needs 242 KB.
**DoublePIR** shrinks the hint to 16 MB at 345 KB/query and 7.4 GB/s/core.
*Use for:* §4/§6 — the single-server line, and the cleanest illustration of the
non-collusion-vs-computation trade in the whole survey.

**`spiral2022`** — *Spiral* (IEEE S&P 2022).
Single-server PIR composing "the Regev encryption scheme and the Gentry-Sahai-Waters encryption
scheme", with "new ciphertext translation techniques to convert between these two schemes".
Claims "at least a 4.5× reduction in query size, 1.5× reduction in response size, and 2× increase
in server throughput compared to previous systems"; a streaming variant reaches 1.9 GB/s.
*Use for:* §4, the FHE-based single-server line.

**`cgk2020`** — *PIR with Sublinear Online Time* (EUROCRYPT 2020).
Single-server PIR with "sublinear amortized server time… sublinear additional storage", allowing
adaptive queries, under standard assumptions (DDH, QR, LWE). Mechanism named in the abstract: the
client "first fetch[es] a small 'hint' about the database contents"; generating it is linear, but
thereafter queries are answered in sublinear time. Includes lower bounds showing their most
efficient scheme is optimal for the trade-off it achieves. *Use for:* §4/§6 — the preprocessing
paradigm Pacmann depends on.

**`sealpir2018`** — *PIR with Compressed Queries and Amortized Query Processing* (IEEE S&P 2018).
Two techniques: query compression "achieving size reductions of up to 274×", and **probabilistic
batch codes** for multi-query PIR giving "up to 40× speedup over processing queries one at a
time". *Use for:* §4 — batching, which matters for any recommender serving many users.

### ORAM and DORAM — survey §4 and §6

**`pathoram2013`** — *Path ORAM* (CCS 2013).
"An extremely simple Oblivious RAM protocol with a small amount of client storage"; proves
`O(log N)` bandwidth cost for blocks of size `B = Ω(log² N)` bits, "asymptotically better than the
best known ORAM schemes with small client storage" in that regime. *Use for:* §4, the canonical
tree-based ORAM.

**`lu2013`** — *Distributed Oblivious RAM for Secure Two-Party Computation* (TCC 2013).
Frames the alternative to circuit-unrolling: two players simulate the CPU of an oblivious RAM
machine using secure two-party computation, per the Ostrovsky–Shoup compiler. *Use for:* §4 — the
origin of the DORAM idea that Floram and Duoram inherit.

**`floram2017`** — *Scaling ORAM for Secure Computation* (CCS 2017).
Floram. Improves prior constructions' "access time by a factor of up to ten, their memory overhead
by a factor of one hundred or more, and their initialization time by a factor of thousands";
instantiates ORAMs holding `2^34` bytes. Key architectural point, in the authors' words: it "is
derived from the new Function Secret Sharing scheme introduced by Boyle, Gilboa and Ishai", which
"significantly reduces the amount of secure computation required… albeit at the cost of `O(n)`
efficient local memory operations". *Use for:* §4/§6 — the FSS-based DORAM line, and the explicit
statement of the local-computation-for-communication trade.

**`wang2014`** — *Oblivious Data Structures* (CCS 2014).
Two techniques — pointer-based and locality-based — applied to "maps, sets, priority-queues,
stacks, deques" and algorithms including max-flow and shortest paths, claiming to outperform "the
best known ORAM scheme both asymptotically and in practice" for data with predictable access
patterns. *Use for:* §4/§6 — the predecessor line to PRAC's oblivious data structures.

**`jarecki2018`** — *3PC ORAM with Low Latency, Low Bandwidth, and Fast Batch Retrieval*
(ACNS 2018). Names the gap directly: "there is an efficiency gap between known MPC ORAM's and
ORAM's". Presents a **3-party, 1-fault-tolerant** ORAM using only symmetric ciphers that
"asymptotically matches client-server Path-ORAM in round complexity and for large records also in
bandwidth". *Use for:* §6, the 3PC DORAM comparison — Duoram's Table 1 places this work.

**`hamlin2021`** — *Two-Server Distributed ORAM with Sublinear Computation and Constant Rounds*
(PKC 2021). States the prior limitation: "All prior DORAM constructions either involve linear work
per server (e.g., Floram) or logarithmic rounds of communication". Constructs "the first DORAM
schemes in the 2-server, semi-honest setting that simultaneously achieve sublinear server
computation and constant rounds". Notes one construction "allows the servers to distinguish
between reads and writes". *Use for:* §6 and the Duoram Table 1 comparison.

### Private retrieval and nearest-neighbour search — survey §6

**`sanns2020`** — *SANNS* (USENIX Sec 2020).
Secure *k*-NN search keeping "client's query and the search result confidential", motivated
explicitly by "cloud-based services such as **recommender systems**, face recognition, and
database search". Two protocols: an optimized linear scan and one based on "a novel sublinear time
clustering-based algorithm", proven secure in the **standard semi-honest model**. Built on
"lattice-based additively homomorphic encryption, distributed oblivious RAM, and garbled circuits".
Contributes "a new circuit for the approximate top-*k* selection from *n* numbers that is built
from `O(n + k²)` comparators". *Use for:* §6 — the origin point, and a direct link from private
ANN to recommendation.

**`tiptoe2023`** — *Private Web Search with Tiptoe* (SOSP 2023).
Private web search over "hundreds of millions of documents… revealing no information about their
search query". Crucially for our taxonomy: "Tiptoe's privacy guarantee is based on cryptography
alone; **it does not require hardware enclaves or non-colluding servers**." Method: semantic
embeddings reduce private full-text search to **private nearest-neighbor search**, implemented with
linearly homomorphic encryption. On a 45-server cluster, 360 M web pages: "145 core-seconds of
server compute, 56.9 MiB of client-server communication (74% of which occurs before the client
enters its search query), and 2.7 seconds of end-to-end latency". Quality, stated honestly by the
authors: average rank 7.7 on MS MARCO, "worse than a state-of-the-art, non-private neural search
algorithm (average rank: 2.3), but… close to the classical tf-idf algorithm (average rank: 6.7)".
*Use for:* §6 as the linear-scan benchmark, §8 as a model of honest quality reporting.

**`pacmann2025`** — *Pacmann* (ICLR 2025).
Private ANN "without revealing the query vector to the server". Distinguishing design: "Unlike
prior constructions that run encrypted search on the server side, Pacmann carefully offloads
limited computation and storage to the client, no longer requiring computationally-intensive
cryptographic techniques." Client runs a graph-based ANN search, "where in each hop on the graph,
the client privately retrieves local graph information from the server", combining a graph ANN
algorithm with "a recent class of PIR schemes that trade offline preprocessing for online
computational efficiency" (i.e. `cgk2020`). Claims "up to 2.5× better search accuracy on
real-world datasets than prior work", "90% quality of a state-of-the-art non-private ANN
algorithm", and on datasets up to 100 M vectors "up to 62% reduction in computation time and 22%
reduction in overall latency". *Use for:* §6 — the sublinear/indexed approach.

**`compass2025`** — *Compass* (OSDI 2025). **Note the venue: OSDI 2025, not NSDI.**
Semantic search over encrypted data "matching state-of-the-art plaintext search quality, while
ensuring the privacy of data, queries, and results, **even if the server is compromised**".
Method: "a novel way to traverse a state-of-the-art graph-based semantic search index and a
white-box co-design with Oblivious RAM". Named techniques: Directional Neighbor Filtering,
Speculative Neighbor Prefetch, Graph-Traversal Tailored ORAM. Claims "user-perceived latencies
within or around a second" and "orders of magnitude faster than baselines under various network
conditions". *Use for:* §6 — the ORAM-based branch, and the clearest example of co-designing an
index with an oblivious primitive.

**`panther2025`** — *Panther* (CCS 2025).
Private ANNS "under the single server setting". States the prior trade-off precisely: earlier work
"either suffer[s] from high communication cost (Chen et al., USENIX Security 2020) or work[s] under
a stronger security assumption of two non-colluding servers (Servan-Schreiber et al., SP 2022)."
Achieves its performance "via several novel co-designs of private information retrieval,
secret-sharing, garbled circuits, and homomorphic encryption". Reports answering "an ANNS query on
10 million points in 18 seconds with 284 MB of communication… more than 7.8× faster and 20× more
compact than Chen et al." *Use for:* §6, and as the **secondary source for
`servanschreiber2022`** (Tier C) — Panther characterises it as the two-non-colluding-server point
in the design space.

**`wally2024`** — *Wally: Batched Private Nearest Neighbor Search at Scale* (arXiv 2024).
Uses **differential privacy** "to break the linear computation barrier of fully-oblivious schemes".
Its framing of the prior art is directly citable: "In prior systems like Tiptoe, the server must
process the entire database per query to hide the access pattern, resulting in low throughput
(909 QPS) and high communication (17.4 MB) on a 3.2-million-entry database. Sublinear alternatives
like Pacmann avoid full scans but require 614 MB of client storage and an offline phase where
clients stream the entire database." Key insight: batch queries from many non-coordinating clients,
each adding "a few fake queries to hide which cluster" it wants. *Use for:* §6 (relaxing the
guarantee) and §3 (a system that is *not* purely cryptographic — it sits between the crypto and DP
families, which is exactly why the taxonomy needs two separate axes).

**`asharov2018`** — *Privacy-Preserving Search of Similar Patients in Genomic Data* (PoPETs 2018).
Secure approximation of the Similar Patient Query. Reports the approximation "returns the exact
closest records in 98% of the queries and very good approximation otherwise", with a two-party
protocol taking "just a few seconds… on databases with thousands of records". *Use for:* §6 and
§9 — a domain where the *functionality itself* is privacy-sensitive, structurally parallel to
recommendation.

### FSS-based function evaluation — survey §4

**`llama2022`** — *LLAMA: A Low Latency Math Library for Secure Inference* (PoPETs 2022).
FSS-based 2PC in the **trusted dealer model** ("a dealer provides input independent correlated
randomness to both parties"), giving "a very lightweight online phase". States what prior FSS-based
2PC lacked: "support for math functions (e.g., sigmoid, and reciprocal square root)" and the
restriction that "all values in the computation [be] of the same bitwidth". *Use for:* §4 — the
comparison baseline Grotto measures itself against.

**`pika2022`** — *Pika* (PoPETs 2022).
MPC protocols for non-linear functions (division, exponentiation, logarithm, tanh) via FSS,
claiming "constant round complexity (3 for semi-honest, 4 for malicious), an order of magnitude
lower communication (54–121× lower than prior art), and high concrete efficiency (2–1163× faster
runtime)". Its stated main contribution is extending the protocol "to be secure in the presence of
**malicious adversaries in the honest majority setting**". *Use for:* §4 and §9 — one of the few
points in this survey where malicious security is actually achieved.

---

## Tier C — metadata verified, no PDF (26)

**No prose claims about these except via an attributed Tier A/B source.** They may appear in the
taxonomy table and the reference list.

`aby2015` · `aby32018` · `araki2016` · `beaver1991` · `bgi2015` · `bunn2020` · `calandrino2011` ·
`chor1995` · `dpfsurvey2026` · `dwork2014` · `evans2018` · `gilboa2014` · `goldreich1996` ·
`hu2008` · `ishai2006` · `koren2009` · `kushilevitz1997` · `mcsherry2009` · `movielens2016` ·
`nikolaenko2013` · `paillier1999` · `sabre2022` · `servanschreiber2022` ·
`shamir1979` · `shmueli2017` · `yao1986`

### Secondary attributions available for the important ones

| Tier C paper | What we may say, and on whose authority |
|---|---|
| `nikolaenko2013` | Nudge's Table 6 reports it as garbled-circuit gradient descent over 2 non-colluding servers, d=8, ~1,330 GB communication, 29 hr on 32 cores for a MovieLens-100K subset \cite{nudge2026}. **Attribute to Nudge.** |
| `chor1995` | Hafiz–Henry describe it as the seminal multiserver PIR protocol and recover its 2-server instance as a special case of their family \cite{hafiz2019}. |
| `bgi2015` | `bgi2016` is the follow-up by the same authors and restates the FSS definition; cite `bgi2016` for the definition instead. |
| `araki2016` | Nudge and PRAC both use 2-of-3 replicated secret sharing and cite this as its source \cite{nudge2026,prac2024}. |
| `goldreich1996` | Duoram and PRAC both cite it as the origin of ORAM \cite{duoram2023,prac2024}. |
| `servanschreiber2022` | Panther characterises it as private ANNS "under a stronger security assumption of two non-colluding servers" \cite{panther2025}. |
| `sabre2022` | Grotto builds on its DPF-tree node taxonomy \cite{grotto2023}. |
| `calandrino2011` | **We have no source we read that describes it.** It is a load-bearing motivation paper (privacy risks of CF outputs). **Either obtain the PDF or restrict it to a one-line citation of its title claim.** ⚠ resolve before writing §2. |

---

## Open items before writing

1. **`narayanan2008` obtained 2026-08-29** and promoted to Tier B — §2's motivation now rests on a
   paper we have actually read.
   ⚠ **`calandrino2011` remains Tier C with no secondary source.** It is the stronger motivation
   (it attacks the recommender's *intended output*, not a released dataset), but we could not
   obtain it: IEEE Xplore paywalled, and three author-page/CiteSeerX URLs returned 404. **Cite it
   for its title claim only — do not paraphrase its method or findings.** Build §2's argument on
   `narayanan2008`, which we have.
2. `dacrema2019`'s extracted abstract was interleaved with its introduction — re-extract or restrict
   to the general claim.
3. The taxonomy table (§3) must place all 66 references. Tier C placement from title + venue is
   permitted; anything finer is not.
