# DORAM — Distributed Oblivious RAM: Floram → Duoram → PRAC

**A line of work, not one paper.**

| Key | Work | Venue | Status |
|---|---|---|---|
| `floram2017` | Doerner, shelat. *Scaling ORAM for Secure Computation* | CCS 2017 | [SECONDARY] — via Duoram §1.1 and PRAC §2.4 |
| `duoram2023` | **Vadapalli**, Henry, Goldberg. *Duoram: A Bandwidth-Efficient Distributed ORAM for 2- and 3-Party Computation* | USENIX Security 2023, pp. 3907–3924 | **[VERIFIED]** — `references/DUORAM.pdf`, pp. 1–3, 11–14 |
| `prac2024` | Sasy, **Vadapalli**, Goldberg. *PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures* | PoPETs 2024(3):692–714 | **[VERIFIED]** — pp. 1–5 |
| `grotto2023` | Storrier, **Vadapalli**, Lyons, Henry. *Grotto* | CCS 2023 | [SECONDARY] — PRAC's oblivious compare |

**Read by:** Mainak · **Track:** T1 / T3 · **Date:** 2026-08-20

> ⚠ **The instructor co-authored three of these four.** PRAC lists him as
> `avadapalli@cse.iitk.ac.in`, IIT Kanpur. This is his research line.

> **One-sentence summary.** A DORAM lets two or three computing parties hold an array in secret
> shares and read/write at a **secret index** with no party learning the address; Duoram makes the
> **online phase constant-bandwidth and 2 messages** by generating the DPFs at a *random* index
> during preprocessing and cyclically shifting them to the real index online, and PRAC builds
> oblivious binary search, heaps and AVL trees on top.

---

## 1. Problem

An index is useful *because* traversal is data-dependent. Obliviousness forbids exactly that. So
any algorithm with data-dependent memory access — binary search, a heap, a tree, a
nearest-neighbour walk — either collapses to a linear scan under MPC, or needs an ORAM.

Classical ORAM assumes a client with plaintext visibility talking to an untrusted server.
**DORAM** (Lu–Ostrovsky) removes that split: the computing parties *are* both client and server,
and **no party knows the contents or the access pattern**.

One useful relaxation Duoram §1 points out: in MPC **all parties know the algorithm being
executed**, so the classical requirement that reads be indistinguishable from writes is dropped.

Doerner–shelat's framing, which both later papers inherit: in DORAM the bottleneck is **bandwidth
and round complexity, not local computation**. PRAC §2.3 states the design rule outright —

> *"scaling local computation is much easier than reducing latency or increasing bandwidth between
> the (non-colluding) parties. Therefore, it is typically preferable to have a DORAM with a lower
> communication cost, particularly in terms of the number of rounds…"*

**This is the same argument our evaluation plan rests on.** Cite it there.

## 2. Threat model

**Duoram:** instantiations in **2- or 3-party** settings tolerating **a single passive
corruption** (semi-honest).

**PRAC** (§3.1, explicit): three parties `P0`, `P1`, `P2`.
- `P0`, `P1` hold the data shares; **`P2` holds no input** — it supplies correlated randomness
  (AND / multiplicative triples) in preprocessing **and actively participates in DORAM read/update
  online**. That online participation is why PRAC is genuinely 3-party, not `(2+1)`.
- Assumptions: (i) PRGs exist, (ii) a secure channel exists, (iii) **none of the three collude**.
- Sharings used: `(2,2)` additive mod `2^r` (`r = 64` typically), `r`-bit XOR, single-bit boolean.
- Terminology: a **`(2+1)`-PC** — a.k.a. **server-aided 2PC** — is a 3-party protocol where one
  party holds no secret input and only ships correlated randomness.
- PRAC footnote 2: **the DORAM determines the party count of the whole system.** Swap Duoram for a
  2-party DORAM and PRAC becomes 2-party.

> **⭐ This matters enormously for us: Nudge is 3-party, semi-honest, tolerating compromise of one
> non-colluding server. Duoram/PRAC is 3-party, semi-honest, non-colluding. The models line up, so
> bolting a DORAM onto Nudge introduces _no new trust assumption_.** That is a genuinely strong
> point for both the report and the meeting — most compositions are not this clean.

## 3. Technique

### 3.1 Floram (the predecessor it beats)
Garbled circuits + `(2,2)`-DPFs. Keeps **two different memory layouts** — an *encrypted* copy for
reading (`D̄[i] = D[i] ⊕ F(k0,i) ⊕ F(k1,i)`) and an *XOR-shared* copy for writing — and needs a
**refresh** to convert between them, costing `O(n)` communication. A `O(√n)`-sized **stash**
amortizes refreshes down to `O(√n)`, triggered every `√n/8` interleaved operations.

### 3.2 Duoram — the core trick
Stores memory as secret shares for **both** reads and writes, so the layout switch disappears.

Each access needs **three DPFs of domain `n`** at target index `i*`. The trick:

> Generate the DPFs during preprocessing at a **random** index `ri = ri0 + ri1`. Online, the
> parties exchange only the offsets `(i*0 − ri0)` and `(i*1 − ri1)`, reconstruct a **cyclic shift
> amount `S`**, and each party *locally* shifts its precomputed DPF evaluation by `S`.

So the online phase is **one exchange of a shift offset plus a local rotation** — no DPF
evaluation online at all. (Protocol 1 = READ, Protocol 2 = UPDATE, both in the paper.)

**Also introduced:** a method for evaluating **dot products of certain secret-shared vectors with
communication only logarithmic in vector length** — this is what makes the whole thing work.

**2P-Duoram** replaces the read with **Computational Symmetric PIR** (SPIRAL, Menon–Wu), lifted
from PIR to SPIR by the Naor–Pinkas OT transform. Counter-intuitively its *online* UPDATE is
cheaper than 3P (no `RefreshBlinds`), but preprocessing uses OT and it needs only one DPF per
operation instead of three.

### 3.3 Complexity — Duoram Table 1 (grey = preprocessing)

| System | Parties | Rounds | Bandwidth | Computation |
|---|---|---|---|---|
| Floram | 2 | `O(lg n)` | `O(√n)` | `O(n)` |
| Hamlin–Varia | 2 | `O(1)` | `O(√n lg n)` | `O(√n lg n)` |
| Jarecki–Wei | 3 | `O(lg n)` | `O(lg³ n)` | `O(lg³ n)` |
| Bunn et al. | 3 | `O(√n)` | `O(√n)` | `O(lg³ n)` |
| Kushilevitz–Mour | 4 | `O(1)` | `O(lg n)` | `O(n)` |
| **Duoram** | **2 or 3** | *`O(lg n)`* **+ 1** | *`O(lg n)`* **+ `O(1)`** | *`O(n)`* + `O(n)` |

Headline: `m` interleaved reads/writes in **`O(m lg n)` words**, vs Floram's **`O(m √n)`**;
online alone is **`O(m)` words — constant per access**.

**Table 1 caption, worth knowing:** make Duoram **4-party / `(3+1)`** and rounds drop to `1+1`,
computation to `O(lg n) + O(n)`, because a fourth party can act as a **dealer** to create and
distribute the DPFs instead of running an MPC to generate them.

**A distinction that matters for us** (Duoram §6): reads are **independent** if the `k` target
indices are known in advance, **dependent** if each index is known only after the previous read
finishes (pointer chasing, tree traversal). For **`k` independent reads the message count is
`O(lg n) + 2` — independent of `k`.** Dependent reads cost `O(lg n) + 2k`.

### 3.4 PRAC — oblivious data structures on top
PRAC's thesis, and it is a good idea worth stealing:

> **If the algorithm inherently leaks something, have the protocol reveal it explicitly and buy
> efficiency with it.**

Its own example: a general DORAM read hides the index completely, but in a **binary search** it is
already public that the next probe is `CurInd ± 2^{d−2}`. Hiding that is paying for secrecy you do
not have.

Two DPF extensions:
- **Incremental DPF (IDPF)** — one DPF whose evaluation at every prefix length `j ≤ lg n` is itself
  a valid DPF: effectively `lg n` DPFs of sizes `2, 4, …, n` targeting the `j`-bit prefixes of one
  `i*`. Costs **~50% more than one plain DPF** and **replaces `lg n` of them**.
- **Wide DPF (WDPF)** — leaves `w×` wider than internal nodes via a length-`w`-stretching PRG at
  the last layer; costs `n(w−1)` extra AES evaluations. For reading/updating **related** locations.

| Structure | Class | Improvement |
|---|---|---|
| **Binary search** | static | bandwidth `O(lg²n) → O(lg n)` (one IDPF replaces `lg n` DPFs) |
| **Heap** | restrictively dynamic (insert anything, delete only min) | insert rounds `O(lg n) → O(lg lg n)`; extract-min bandwidth `O(lg²n) → O(lg n)` via WDPFs; revealing heapify's index relationships cuts cost by **3×** |
| **AVL tree** | fully dynamic | **first oblivious AVL tree for MPC**; `O(lg n)` rounds and bandwidth; *explicit-structure* pointers instead of index-determined layout |

## 4. Results

**Duoram** — C++ reference implementation, Boost.Asio, parties in **separate Docker containers**,
network shaped with **`tc qdisc … netem delay Xms rate Ymbit`**. Standard setting: **30 ms latency,
100 Mbit/s**, DB `2^16`–`2^26` 64-bit words, 128 interleaved operations.

- **3P-Duoram beats Floram at every database size tested.**
- 2P-Duoram beats Floram until roughly `2^22`–`2^24`, where its linear SPIR computation overtakes.
- **Floram needs ≈`4 lg n − 25` sequential messages per read; Duoram needs 2.**
- Constrained link (**1 Mbit/s, 100 ms**): one read on `2^20` items — 2P-Duoram **≈10 s**, Floram
  **>1.5 hours**. At `2^25` — Duoram **≈30 s**, Floram **did not finish in 10 hours**.

**PRAC** at `2^26` items:

| Protocol | Wall-clock | Bandwidth |
|---|---|---|
| Binary search | **> 27×** | **> 3×** |
| Heap extract-min | **> 31×** | **> 13×** |

⚠ **Resolve before citing:** PRAC's §1 contributions list says **>18× / >3×** and **>16× / >7×**
for the same two protocols. Abstract and contributions disagree — different baselines or network
settings. **Read PRAC §8 before either number goes in the report.**

**Artifacts:** Duoram — `https://git-crysp.uwaterloo.ca/avadapal/duoram` (**C++**). PRAC — open
source, **PETS 2024 Artifact Award** `[SECONDARY]`. **Both need checking out.**

## 5. Stated limitations

- **Semi-honest, single passive corruption, no collusion.** Same shape as Nudge — see §2.
- **Linear local computation per access**, deliberately traded for low communication. A DORAM
  access is *not* cheap locally: at `n = 2^26` every access still touches the whole array.
- Preprocessing is `O(lg n)` bandwidth and rounds **per access** — `k` accesses need `k`
  preprocessed DPF triples. Offline/online split must be measured separately.
- Duoram's 2-party read inherits SPIRAL's linear computation, which is what caps 2P at ~`2^23`.

## 6. Relation to our project

### 6.1 The quote that connects it to us
PRAC §1, unprompted:

> *"**Nearest Neighbor Search** is fundamental in many machine learning applications, including
> targeted advertising, pattern recognition, **recommendation systems** and DNA sequencing. One way
> to implement nearest neighbor search is using **priority queues**, which in turn can be
> implemented using **heaps**. Therefore, a privacy-preserving heap implementation would enable the
> implementation of the aforementioned applications while maintaining privacy."*

PRAC built the oblivious heap and named recommendation as the motivating application. Nobody has
connected it to a **privately-trained** recommender.

### 6.2 ⚠ The honest caveat — DORAM is NOT a top-*k* hammer

**Do not overclaim this.** A DORAM pays off when you touch **`o(n)`** locations out of `n`. A
one-shot top-*k* over an *unsorted* score vector must touch **all `n`** entries or it leaks — so
DORAM buys nothing there, and an oblivious tournament/sort is simpler and just as good.

Where a DORAM genuinely earns its place in our system:

| Use | Accesses | DORAM helps? |
|---|---|---|
| Top-*k* over a full unsorted score vector | `n` | **No** — use an oblivious tournament |
| Traversing an **index** (sorted array, tree, ANN graph) | `O(lg n)` – polylog | **Yes** — PRAC binary search / AVL |
| **Fetching `k` content records out of `n`** | `k` | **Yes** — this is the delivery layer |
| Heap maintained **across** queries | amortized | **Yes** — PRAC heap |

### 6.3 Where the project now stands — four layers

```
 1. TRAINING          Nudge, vendored as-is (Go)                        not ours
        │                 outputs: ⟦A⟧ shared, B cleartext
        ▼
 2. SELECTION         top-k moved SERVER-SIDE                           OURS  ← new
        │                 replaces Nudge's O(n·b) score dump
        │                 user link: 298 KB  ──►  O(k·b), a few hundred bytes
        ▼
 3. DELIVERY          fetch the k content records obliviously           OURS
        │                 k reads out of n  →  DPF-PIR or Duoram READ
        ▼
 4. HARVEST           delivery queries feed the next training round     OURS
                          PIRSONA's loop, on Nudge's substrate
```

**The systems argument for layer 2, and it is a good one:** the `O(n)` cost does not vanish, it
**moves from the user link — mobile, high-latency, metered — to the server–server link —
datacentre, cheap, high-bandwidth.** Nobody has measured that trade, and a WAN-profile evaluation
is exactly the instrument for exposing it.

**Layer 2 has a safe version and a stretch version. Ship the safe one first:**
- **(a) Safe:** oblivious tournament / bitonic selection over the shared score vector. `O(n)` local
  work, `O(k·b)` to the user. **No DORAM needed.** Simple, shippable, and already the headline win.
- **(b) Stretch:** *sublinear* selection over an **index** — cluster centroids or an ANN graph —
  traversed obliviously with Duoram/PRAC. This is where the DORAM genuinely belongs, it is the
  Pacmann/Compass idea rebuilt with the instructor's own tools, and it is the research contribution.

### 6.4 Why this composes unusually cleanly
- **Trust models match exactly** (§2) — no new assumption.
- **Duoram is C++** with a public repo → fits our stack, unlike Nudge's Go.
- **Duoram benchmarks with `tc netem` in Docker** → identical methodology to our plan; our numbers
  will be directly comparable to theirs.
- Their 3 parties can be *the same 3 servers* Nudge already runs.

### 6.5 Sections
§3 (primitives — DORAM beside FSS/PIR), §5 (private retrieval), **§8 (the gap — this is now the
spine of N1)**.

## 7. Open questions

1. **Does the Duoram artifact build?** `git-crysp.uwaterloo.ca/avadapal/duoram`, C++ + Boost.Asio.
   **Try this early** — if it builds, layers 2(b) and 3 get much cheaper. **Blocking for scope.**
2. **Is layer 2(a) enough?** An oblivious tournament needs no DORAM at all. Decide honestly whether
   2(b) is a real contribution or scope creep, *before* building it.
3. **Party-role conflict.** Nudge's three servers **all hold input shares**; PRAC's `P2` holds
   **none**. Are these composable on the same three machines, or does `P2`'s role clash? **Ask him
   — this is the sharpest technical question we have.**
4. **`O(n)` local computation per DORAM access.** For a heapify of `n` items that is `O(n²)` local
   work. Rough it out on paper at MovieLens scale (`n = 3,706`) *before* committing.
5. **Independent vs dependent reads.** Our `k` content fetches are *independent* (all indices known
   once the top-*k* is out), so message count is `O(lg n) + 2` regardless of `k`. **Confirm this
   applies to our access pattern** — if so it is a strong result for layer 3.
6. **Would a 4th party be acceptable?** Table 1 says `(3+1)` drops rounds to `1+1` with a dealer.
   PIRSONA already used 4 parties. Worth asking whether he considers that a reasonable trade.
