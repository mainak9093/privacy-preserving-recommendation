# Third-party code

Code under `third_party/` is not ours. Each entry keeps its original `LICENSE` file, untouched.

## `nudge/`: the Nudge reference implementation

**Upstream:** <https://github.com/NudgeArtifact/private-recs>, snapshot of `private-recs-main`, taken 2026-08-20.
**Paper:** Henzinger, Dauterman, Corrigan-Gibbs, Boneh. *Nudge: A Private Recommendations Engine.* USENIX Security 2026.
Full version: <https://eprint.iacr.org/2026/179>.
**Licence:** MIT, see [`nudge/LICENSE`](nudge/LICENSE).
**Size:** about 10.7K lines of Go, plus CGo and assembly.

**It is here as reading material, and it is not part of OblivRec.** No build target compiles or links anything under
`third_party/`. OblivRec's private training is our own C++ under [`src/mpc/`](../src/mpc/) and [`src/mf/`](../src/mf/), written
from the paper. The snapshot was kept to read alongside the paper, and so the team read the same version.

### What is in it

| Package | Lines | What it does |
|---|---|---|
| `protocol/` | 2,189 | The three-party protocol: server and client for all three phases |
| `share/` | 4,636 | Additive and replicated secret sharing, matrix operations |
| `dcf/` | 1,521 | Distributed comparison function, which drives truncation and normalisation |
| `multdpf/` | 985 | A multiplicative DPF, used for data collection |
| `net/` | 432 | Multi-threaded messaging between the three servers |
| `dmsb/` | 351 | Distributed most-significant-bit, which seeds Newton-Raphson in normalisation |
| `uint128/` | 242 | 128-bit arithmetic via CGo |
| `rand/` | 203 | AES-CTR buffered generator pool |
| `aes/` | 32 | Assembly AES-128, used as the PRF |

### Where it stops, and where OblivRec begins

Nudge's three phases are data collection, private factorisation, and serving, in which the servers send each user shares of
the scores and the user picks the top k locally. The user then holds item IDs in the clear and must still fetch the content,
and that fetch is unprotected. Nudge says so in its non-goals (§3.1):

> *"Nudge's goal is to map user ratings into personalized recommendations; it relies on other means (e.g., Apple's private
> relay, Tor, or cryptographic private information retrieval) to let users fetch data items in a private way."*

### Building it, if you want to read it running

Needs Go 1.22 or later and a C compiler with AVX2 and AES-NI.

```bash
cd third_party/nudge
go build ./...
go test ./...
```

Upstream notes one flake: `TestPowerIt` in `share/` occasionally fails on a random sparse matrix, so rerun it before
investigating. `TestS3` needs AWS credentials.
