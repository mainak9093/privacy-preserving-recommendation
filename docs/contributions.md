# Contribution Log

> **Why this file exists.** The mid-term report (5%, due **12 September 2026**) is explicitly
> *"a two-page summary describing the contributions of each group member"*, and a viva may be
> called at any stage. Reconstructing this from memory on 11 September is how groups lose these
> marks.
>
> **Append one line after every work session.** Newest at the bottom of your own section.
> Keep it factual: what you did, what landed, what is still open.

---

> **Ownership is not yet assigned.** The four survey tracks (T1–T4,
> [`report/README.md`](../report/README.md)) are assigned at the Phase 0 kickoff; the
> implementation streams (W1–W4) after the instructor meeting. Log your work under whichever
> heading fits — **the log matters, the label does not.** Anything that does not fit goes in
> *Shared / joint work*.

## Survey tracks (live now, Milestone 1)

### T1 — Primitives · §3

*Owner: unassigned* — secret sharing, FSS/DPFs, PIR, ORAM, fixed-point over rings.

| Date | Hours | What was done | Artefact (note / section / commit) |
|---|---|---|---|
| | | | |

### T2 — Private training · §4

*Owner: unassigned* — Nikolaenko → PIRSONA → Nudge; the federated and FHE alternatives.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### T3 — Private retrieval · §5

*Owner: unassigned* — SANNS → Tiptoe → Pacmann/Compass → Wally/Panther/MESS; private top-*k*.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### T4 — Threat models, systems, the gap · §2, §6, §7, §8

*Owner: unassigned* — privacy notions, PIRSONA as an end-to-end system, evaluation norms,
the gap statement and the two research questions.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

---

## Implementation streams (provisional, Phase 2 onward)

These may not survive the instructor meeting intact — see
[REQUIREMENTS.md §10](../REQUIREMENTS.md).

### W1 — FSS core and private delivery

*Owner: unassigned* — DPF (AES-NI GGM tree), FSS comparison/zero-test gates, DPF-PIR read layer,
consumption harvesting.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W2 — 3PC substrate and non-linear protocols

*Owner: unassigned* — replicated 2-of-3 sharing, PRF setup, matrix-vector programs, `Trunc_t`,
`ApproxNormalize`, the ring-width study.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W3 — Factorization and serving

*Owner: unassigned* — power iteration / `ApproxFactor`, `SetOrthogonal`, score computation,
seen-item masking, cleartext quality oracle.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

### W4 — Evaluation and security analysis

*Owner: unassigned* — benchmark harness, `tc netem` profiles, baselines, threat model,
leakage profile, the public-`B` reconstruction analysis, reproducibility.

| Date | Hours | What was done | Artefact |
|---|---|---|---|
| | | | |

---

## Shared / joint work

Meetings, deck building, report writing, dry runs — anything not attributable to one track.

| Date | Who | What |
|---|---|---|
| 2026-09-06 (hardening) | Mainak | **Closed the sanitizer gap, and it turned up a real hole.** GCC on mingw ships no `libasan`/`libubsan`, so `-fsanitize=undefined` cannot link. UBSan still works in **trap mode** (`-fsanitize-undefined-trap-on-error`), which needs no runtime library, and the whole suite is clean under it. WSL turned out **not** to be installed (my earlier "wsl present" was just the launcher stub on PATH), so that route was unavailable without a large system change. **Verified both instrumentations actually fire** with deliberate-fault canaries rather than trusting a clean run, since a no-op instrumentation looks identical to a working one. **The hole:** `_GLIBCXX_DEBUG` covers `std::vector` but does nothing for `oblivrec::Span`, which is our own type and carries the deserialisation path parsing attacker-controlled bytes, so the hardened build was leaving the single most exposed path in the codebase unchecked. `Span::operator[]` and `subspan` now bounds-check under the same macros, reporting index, size and source location before aborting, at zero release cost. Wired it all into **`mingw32-make check`** so it is repeatable rather than something I ran once by hand. ASan proper remains unavailable; its main classes (heap overflow, use-after-free, leaks) do not apply here because there is no `new`/`delete`/`malloc`/`free` anywhere and every owning container is a `std::vector` (verified by grep, and recorded as the weaker claim it is). Report now 7 pages. |
| 2026-09-06 (bug hunt) | Mainak | **Audited the day-2 code paths that were written but never exercised, before starting day 3.** Found one real bug: `DpfKey::Deserialize` accepted a `party` byte outside {0,1}. Not cosmetic, because `Eval` starts with `t = key.party` and `Traverse` branches on `if (*t)`, so a party byte of 7 is truthy and the key **silently evaluates as party 1**, giving a wrong reconstruction with no error raised. Keys arrive over the wire, so it now validates on entry, and `domain_bits` is validated on the same principle before it sizes an allocation or multiplies out a length. The throwaway hunt is kept as `tests/test_dpf_serialize.cpp`. Everything else checked out: `Eval` and `EvalFull` agree at every index, both rings round-trip (including that a full key pair still reconstructs beta after crossing the wire), truncated/over-long/empty/malformed input is rejected, `EvalFull` rejects a wrongly sized span, and the `u128` decimal formatter is correct at its maximum where it has zero buffer margin. Also cross-checked **nDCG@20 against sklearn over 200 random instances, agreeing on all of them**, plus perfect/worst ranking, no-relevant-items, and train-mask exclusion, so the headline 0.4536 is trustworthy. Code is clean under `-Wconversion -Wsign-conversion` and passes a hardened build with `_GLIBCXX_ASSERTIONS`. UBSan attempted but `libubsan` is absent from this MinGW toolchain, which is a gap to close on Linux before the final report. |
| 2026-09-06 (bug fix) | Mainak | **Fixed a security bug in DPF key generation, and the documentation error it sat next to.** `Gen` was seeding a `std::mt19937_64` and using its output as DPF seed material. The initial seeds are the entire secret, and MT19937 is reconstructible from 624 consecutive outputs, so an adversary seeing enough key material could rebuild the GGM tree and recover `alpha`. Seed entropy was also ~64 bits against a claimed `lambda=128`. Replaced with the OS CSPRNG via `BCryptGenRandom` (`include/oblivrec/csprng.hpp`), **abort-on-failure rather than degrade**, plus `tests/test_csprng.cpp` covering repeat-draws, stuck output, monobit balance, byte spread, `n=0`, and buffer overrun at odd lengths. Found a latent build bug in the process: the Makefile referenced `$(LDLIBS)` but never defined it, so any library link would have failed. Also corrected the key-size error in `design/ARCHITECTURE-draft-v1.md` §7.1 that I had reported but not fixed (~260 B claimed, 245 B measured at n=4096; the ML-100K demo figure is 227 B), and wrote the seven Decisions Log entries this sprint had generated and left unrecorded. Report now 6 pages. |
| 2026-09-06 (day 2 work, pulled forward) | Mainak | **S1 sprint day 2, the DPF.** `include/oblivrec/dpf.hpp` frozen first as the cross-component contract, then `tests/test_dpf_invariant.cpp` written *before* `Gen` was trusted, then `src/dpf/dpf.cpp`. The invariant checker walks both parties' trees in lockstep and asserts the BGI structural property at every internal node (`s0 != s1` and `t0^t1 == 1` on the path to alpha, `s0 == s1` and `t0^t1 == 0` off it), so a control-bit bug names its own level and node instead of surfacing as "correct at 2 bits, wrong at 8". `Gen`, `Eval` and `EvalFull` all route through one shared `Traverse`, so there is one traversal to be wrong rather than three. Adopted the **difference** sign convention (no `(-1)^party` factor) to match the spec's stated invariant. **Exhaustive correctness passes on the first run**: every alpha and every x to 10 domain bits on `u64` and 9 on `u128`, all x at sampled alpha to 12 bits, plus the edge cases (alpha at either end, beta=0, beta=-1, and beta=1 reconstructing exactly, which the PIR layer depends on). **Measured key sizes correct the design draft**: 245 B at n=4096, not the ~260 B claimed, because the draft's formula omitted the seed and header and evaluated at the wrong domain size. 134x smaller than a naive one-hot query at that size. Whole suite runs in 2.1 s. Wrote `report/sections/tech_dpf.tex` (report now 5 pages). Still unverified and left for day 3: `EvalFull` to 16 bits, `Eval`-vs-`EvalFull` agreement, and serialisation round-trips. |
| 2026-09-06 | Mainak | **S1 sprint day 1, foundation.** Build system: hand-written `Makefile` for `mingw32-make` (cmake is not installed and installing it would breach the no-new-dependencies rule), with a `check-toolchain` guard that turns g++'s silent as/ld spawn failure into a loud one. Headers `span.hpp` (a C++17 stand-in for `std::span`), `ring.hpp` (`RingTraits` for `u64` and `u128`, ring width templated from day one), `fixedpoint.hpp`, `prg.hpp`. `src/common/aes.cpp`: AES-NI fixed-key MMO PRG plus a scalar AES-128 reference. `tests/test_aes.cpp` passes the FIPS-197 known-answer vector and 10k AES-NI-vs-scalar comparisons; `tests/test_ring.cpp` covers byte round-trips, two's-complement negatives and additive homomorphism on both rings. Python oracle `model/{data,mf,metrics}.py`: power iteration written to mirror the protocol structure rather than calling SVD, nDCG@20 = 0.4536 on ML-100K `u1`. **Finding: recommendation quality is flat in the iteration count** (`docs/finding-ell-vs-quality.md`, raw data in `bench/results/oracle_ell_sweep.jsonl`) — the SVD subspace angle falls five orders of magnitude from `ell=10` to `ell=400` while nDCG@20 moves 0.5%, which justifies running private training at `ell=10` for a fortyfold communication saving. Started `report/midterm_technical.tex` (4 pages so far). |

---

## Milestone contribution summaries

Filled in just before each submission, from the tables above.

### Milestone 1 — Literature survey (31 Aug 2026)

> **Backfilled 2026-09-03 from git history, not from live logging.** Only work with evidence in
> this repository is recorded below. All 16 commits to date are authored by Mainak, so that is the
> only row that can be filled from the repo. **The three blank rows are not a claim that nothing
> was done, they are a statement that nothing was recorded.** Each member should fill their own row
> before 12 September, since Milestone 2 is graded specifically on per-member contribution.

| Member | Track | Video slot | Report sections written | Other |
|---|---|---|---|---|
| Mainak Sarkar | Not formally assigned (acted across all four) | Part I, slides 1-9 | All 11 sections of `report/sections/` | Bibliography rebuilt from DBLP (66 entries, 0 unverified); 40 PDFs acquired and author-verified; `notes/evidence.md` tier system; deck source; narration script; forward-citation sweep (task 1.4) |
| Shrasti Dwivedi | *to be filled in by Shrasti* | Part II, slides 10-16 (per narration script) | | |
| Aditya Anand | *to be filled in by Aditya* | | | |
| Shravan Agrawal | *to be filled in by Shravan* | | | |

### Milestone 2 — Mid-term report (12 Sep 2026)

| Member | Components delivered | Lines of evidence (commits, tests, results) |
|---|---|---|
| Mainak Sarkar | | |
| Shrasti Dwivedi | | |
| Aditya Anand | | |
| Shravan Agrawal | | |

### Milestone 3 — Final (6 Nov 2026)

| Member | Components delivered | Report sections | Evaluation owned |
|---|---|---|---|
| Mainak Sarkar | | | |
| Shrasti Dwivedi | | | |
| Aditya Anand | | | |
| Shravan Agrawal | | | |
