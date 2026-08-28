# third_party/ — vendored external code

Code here is **not ours**. It is vendored (committed into this repo) rather than submoduled so the
whole team reads and builds against an identical copy, and so the project stays reproducible if an
upstream repo moves. Each entry keeps its original `LICENSE` file untouched.

**Rule:** do not edit vendored code in place. If a change is needed, record it as a patch under
`third_party/patches/` with a note saying why — otherwise the next person cannot tell our code from
theirs, and neither can a viva examiner.

---

## `nudge/` — the Nudge reference implementation

**Upstream:** <https://github.com/NudgeArtifact/private-recs> (snapshot `private-recs-main`,
vendored 2026-08-20)
**Paper:** Henzinger, Dauterman, Corrigan-Gibbs, Boneh. *Nudge: A Private Recommendations Engine.*
USENIX Security 2026. Full version: <https://eprint.iacr.org/2026/179>
**License:** MIT (see `nudge/LICENSE`) — permits use and redistribution with the notice retained.
**Size:** ~658 KB, ~10.7 K lines of Go plus CGo/assembly.

This is the artifact-evaluated implementation of the private-training half of our system. **We use
it; we do not claim it.** Every report, README, and resume bullet must say so.

### What is in it

| Package | Lines | What it does |
|---|---|---|
| `protocol/` | 2,189 | The 3-party protocol: server + client for all three phases |
| `share/` | 4,636 | Additive / replicated secret sharing, matrix ops; `matrix.c` is a SIMD matrix core over shares |
| `dcf/` | 1,521 | Distributed Comparison Function — 2-party keys for `f(x) = [x ≥ α]`, 64/128-bit outputs. Drives truncation and normalization |
| `multdpf/` | 985 | **Multiplicative DPF** — a DPF whose output is already in RSS form. Used for *data collection* (writing ratings in) |
| `net/` | 432 | Multi-threaded messaging between the three servers |
| `dmsb/` | 351 | Distributed Most-Significant-Bit, built on `dcf/`; seeds Newton–Raphson in normalization |
| `uint128/` | 242 | 128-bit arithmetic via CGo around GCC `__int128` (`-O3 -march=native -maes -mavx2`) |
| `rand/` | 203 | AES-CTR buffered PRG pool |
| `aes/` | 32 | AMD64 assembly AES-128, used as the PRF throughout |

### The three phases, and where ours begins

1. **Data collection** — each user secret-shares their rating vector via `multdpf`; servers verify
   all ratings are in `{0,1}` by exchanging `O(λ)` bits.
2. **Private matrix factorization** — power iteration over RSS shares; `A` (user embeddings) stays
   secret-shared, **`B` (item embeddings) is revealed in cleartext to the servers**.
3. **Recommendation serving** — servers send each user RSS shares of `a⁽ⁱ⁾·B`; **the user
   reconstructs the score vector and picks top-*k* locally.** `O(n)` bits per query.

**Phase 3 is where Nudge stops.** The user now holds item IDs in the clear and must still fetch the
actual content — and that fetch is unprotected. Nudge says so in its own non-goals (§3.1):

> *"Nudge's goal is to map user ratings into personalized recommendations; it relies on **other
> means** (e.g., Apple's private relay, Tor, or cryptographic private information retrieval) to let
> users fetch data items in a private way."*

**That sentence is our project.** Everything we write starts after their phase 3.

### Build requirements

Go ≥ 1.22 and a C compiler with **AVX2 + AES-NI** (three packages use CGo). Builds without those
instructions at reduced performance.

```bash
cd third_party/nudge
go build ./...
go test ./...
```

Known upstream flake: `TestPowerIt` in `share/` occasionally fails on a random sparse matrix —
re-run before investigating. `TestS3` needs AWS credentials and is not relevant to us.

### Useful entry points for us

- `main.go` — CLI for all three phases (`data-server`, `server`, `recs-server`, and `*-client` /
  `*-bench` counterparts).
- `params/*.json` — power-iteration hyperparameters. `movielens_tiny_params.json` (K=8) and
  `movielens_params.json` (K=10) are our scale; `tiny_params.json` (K=2) for local smoke tests.
- `recs-server` / `recs-client` — **the integration seam.** Its `nclusters` /
  `nitemsPerCluster` arguments are worth reading closely: they suggest upstream already considered
  partitioning the item space to control the `O(n)` serving cost, which is exactly the axis our
  bandwidth analysis measures.
- `multdpf/` and `dcf/` — reference implementations to read before writing our own DPF. Note they
  solve a *different* problem from ours: `multdpf` **writes** shares in, our PIR layer **reads**
  records out.
