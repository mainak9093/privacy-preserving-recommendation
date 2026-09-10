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
| 2026-09-11 (day 11) | Mainak | **Wrote the graded artifact and closed Phase 2.** `report/midterm.tex` was a 62-line stub of `\todo` blocks; it is now the two-page per-member summary the handout actually asks for, and it builds to **exactly 2 pages** with no warnings. **A trap caught while writing it:** `preamble.tex` sets `\draftfalse`, so `	odo{...}` renders as *nothing* -- the placeholder subsections for the other three members would have been silently EMPTY in the submitted PDF, which is precisely the failure a per-member-graded rubric punishes. They are written as visible italic body text instead, stating factually that those contributions are not recorded in the repository rather than inventing them. Deliberately kept to two pages and pointed at the 17-page technical report as an appendix rather than smuggling depth into the graded file. Verification pass: `make test` green (9 binaries, 1.8 s), `make check` clean under UBSan trap mode + `_GLIBCXX_DEBUG` + stack protector + checked `Span`, `make demo` still ten byte-exact titles agreeing with the oracle, `make figures` regenerating all four. **Also simulated a fresh clone properly**: an earlier attempt reported a false failure because it ran from a directory without the *committed* `tests/data/`; with the committed fixture present as a real clone would have it, every suite passes, the model- and data-dependent tests skip cleanly, and the 25 cross-language contract vectors still run -- which is exactly what committing them was for. |
| 2026-09-11 (day 11) | Mainak | **Built the networking and closed the Phase 2 exit criterion in full, reversing the deferral I recorded the day before.** `include/oblivrec/channel.hpp` + `src/net/tcp.cpp`: length-prefixed frames over plain TCP per REQUIREMENTS section 5, with `server.cpp` running **one OS process per party** (not threads -- separate processes are the stronger claim, and they have a structural payoff: each server serves one client to completion, so **no threading appears anywhere in this project**). `make demo-net` runs the identical protocol across three processes and **asserts it returns the same ten titles in the same order** as the in-process path, every record still byte-exact. **The distinguisher, which is what actually closes the criterion.** The original wording wanted `tcpdump` showing 'pseudorandom bytes'; bytes *looking* random is not evidence, so instead an adversary is built and measured losing. Over **1800 recorded queries across three record indices: every frame is 228 B whatever the index**, and a logistic regression on the raw wire bytes scores 0.490-0.500 with **chance inside every 95% interval**, plus a shuffled-label control at 0.483 proving the harness measures what it claims. **Two bugs of my own, both instructive.** (1) The split-delivery test **deadlocked**: with one thread and blocking sockets, sending 200 KB blocks waiting for a reader that cannot run -- it *hung* rather than failed, which is the worse mode. Fixed by forcing the split from the receiver side with a tiny `SO_RCVBUF`. (2) A dangling pointer in `server.cpp` from calling `EncodeWords` twice inside one `Send()`, taking `data()` from an already-destroyed temporary. **Also found a real provenance bug:** under memory pressure the `git` subprocess could not spawn and `except Exception` silently stamped every result row `"unknown"`, violating RULES D5 invisibly; `git_sha()` now warns, in all three producers. **Decoy defence measured and REJECTED** -- decoys must be popularity-weighted or they are separable by inspection, so they point where the popularity control already points and the defence has a floor at cos 0.44; 40 decoys (5x the traffic, 48 KB/query) still leave the adversary at 0.71. A negative result about the alternative, and the strongest argument for the PIR layer. **Retracted every stale caveat in the same commit** -- PHASES 2.3/2.8 and the exit criterion, threat model A6 and section 5, `tech_scope.tex`, HANDOVER Task A, and midterm 'next steps'. The 2026-09-09 Decisions Log row is left standing with a superseding row above it, since the log is append-only and a reversed decision is more informative than a rewritten one. Suite now **10 binaries**, all clean under hardening; technical report **20 pages**; six figures. |
| 2026-09-10 (day 10) | Mainak | **Priced the private read against what it replaces, and the honest answer is not flattering -- which is why it is worth reporting.** `bench/bench_baseline.cpp` closes task 2.10 with B1 (cleartext lookup, no privacy) and B5 (download the whole 430 KB catalogue and filter at home, trivially private). Measured at the real operating point: **B1 260 B / 4.7 ns, DPF-PIR 483 B / 0.242 ms, B5 430,592 B / 0.045 ms.** So DPF-PIR sends **446x fewer bytes** than B5 counting both servers, but is **the SLOWEST of the three in server CPU** -- 5x slower than B5's local copy and ~50,000x slower than a cleartext lookup. Rather than bury that, the report states the **crossover**: setting the extra CPU against the saved bytes, the private read wins on any link below **8.0 Gbit/s**, i.e. every real network -- but there IS a crossover and we name it. A schema note: `bench.hpp` **aborts** on a phase outside the ARCHITECTURE section 10 enum, and "baseline" is not in it; adding one would mean amending section 10 first per RULES. It would also be the wrong model, since all three rows measure the same operation by three methods -- which is exactly what the `op` extra is for. So phase stays `pir` and the methods are three ops, which is also what makes them joinable. Wrote **`docs/threat-model.md`** (task 2.11), the D7 deliverable: adversary classes A1-A6, the leakage profile, and **ARCHITECTURE section 9.3 promoted from a proposal to a measurement**. Report sections `tech_leakage.tex` (the attack, its controls, the cost of closing the gap) plus figures wired into `tech_oracle` and `tech_dpf`; **technical report now 17 pages**, no undefined references. **Recorded the deferral honestly rather than letting it pass:** PHASES 2.3 is now marked PARTIAL (sharing done, process wiring deferred) and 2.8 DEFERRED, and the phase exit criterion is split -- the demo half MET, the **wire half NOT met**, since nothing in this phase has been observed on a socket. That appears in `PHASES.md`, `tech_scope.tex` and section 5 of the threat model, in the same words. |
| 2026-09-09 (day 9) | Mainak | **Carried out the leakage analysis the architecture had only proposed, and it is the strongest result in the project.** `design/ARCHITECTURE-draft-v1.md §9.3` calls this *"the analysis that only we can do"* -- neither PIRSONA nor NUDGE measures it, because neither implements both halves. The setting: NUDGE publishes the item matrix `B` in the clear by design, so **an observed fetch is not one bit, it is a projection onto a known basis**. `model/attack.py` reconstructs a user's private embedding from nothing but public `B` and the *indices* of the records they fetched. **Result: one single observed fetch gives cos(a-hat, a) = 0.55; ten give 0.84 and predict 56% of that user's next twenty recommendations.** **The control is the load-bearing part and it is why the result is defensible.** A random-vector control flatters any attack of this shape, because popular items sit near the top of everyone's ranking -- an estimator can look strong having learned only what is public. So the honest control runs the *identical* estimator on the globally most-popular items, ignoring the user: it reaches only 0.26-0.53, and **the attack's gap above it is a stable +0.39 to +0.42 at every j**, so the leakage is genuinely about the individual. Random floor sits at 0.003 as theory says. **An honest negative result, kept rather than dropped:** the more sophisticated ranking-constraint estimator uses strictly more information and is *worse* at every j>=2 (0.81 vs 0.92 at j=50). That strengthens the case for PIR -- the cheap obvious attack is already sufficient, so an adversary needs no sophistication. **Also corrected a wrong claim in my own docstring**, caught by the measurement: I had asserted the random control's mean cosine would sit near 1/sqrt(d)=0.25; the mean is ~0, and it is the *spread* (IQR +/-0.18) that is near 1/sqrt(d). The practical consequence is real -- one user's cos of 0.2 is inside guessing noise, so only population means carry a claim. Built `bench/scripts/make_figures.py`, which `mingw32-make figures` had been deliberately failing without since Day 6, closing task 2.9: three figures, all regenerated from JSONL, **none hand-edited** (RULES B5). F1 the attack curve, F2 key size vs naive one-hot (agreeing with the sizes `test_dpf_serialize.cpp` pins), F4 ell vs nDCG showing quality flat at 0.453 while the subspace angle falls five orders of magnitude. Networking (2.3, 2.8) **deferred to Phase 3** -- recorded with its consequence stated, that the amended exit criterion is unmet and "three servers" is a property of the code rather than something shown over a socket. |
| 2026-09-08 (day 4c) | Mainak | **The serving layer and the end-to-end demo. `demo --user 42 --k 10` now returns ten real film titles, every one of them fetched privately, which closes the Phase 2 exit criterion.** Wrote `share.hpp` (2-of-3 replicated sharing, `x0+x1+x2 = x` with party `i` holding `(x_i, x_{i+1})`) and the serving path. The key structural point for the report: **`B` is public, so `scores = a.B` is a linear map applied to a shared vector by a public matrix**, and each server computes its whole score share locally -- no communication, no correlated randomness, no multiplication protocol, no rounds. Scores stay at `2t` scale because `Trunc_t` is interactive and belongs to S2; decoding at `t` instead would scale everything by 2^20, which is *monotone* and so invisible in a ranking. **Decided and recorded: the seen-item mask is an additive shared vector carrying `-(2^55)`**, which sits ~3700x below any real score and ~256x above the ring floor, both margins asserted by a test rather than argued in prose; and top-k comparison is **signed**, since unsigned would sort masked items *first* and a smoke test looking only at the head of the list would miss it. **BUG 3, found by the exact-equality test:** `export.py` was encoding the float product, but the protocol multiplies *encoded* factors -- quantise-then-multiply is not multiply-then-quantise. 1650 of 1682 scores disagreed, by up to 2.1e7 out of 4.3e12. That is ~5e-6 relative and irrelevant to the ranking, which is exactly the danger: **any tolerance-based test would have passed** and the oracle would have been quietly wrong for every later exact comparison. Reference now computes what the protocol computes, in arbitrary-precision ints. All 1682 scores then matched **bit-for-bit**. The demo checks itself twice -- each record byte-exact against a cleartext lookup, and the ranking position-for-position against the Python oracle -- and both agree. Wrote `report/sections/tech_serving.tex`, which also resolves the dangling `sec:tech:pir` reference the DPF section had been carrying; **technical report now 13 pages**. Eight Decisions Log rows added. Full suite 1.8 s, green, and clean under UBSan trap mode + `_GLIBCXX_DEBUG` + checked `Span`. |
| 2026-09-08 (day 4b) | Mainak | **The catalogue and the DPF-PIR read layer. A private fetch of "Toy Story (1995)" now reconstructs byte-exactly, which is the core of the Phase 2 exit criterion.** `Catalogue::LoadMovieLens` packs u.item into fixed 256 B records using explicit length prefixes rather than a delimiter (a delimiter could collide with a byte inside a Latin-1 title). Drops the IMDb URL and the video-date field, which is empty on all 1682 lines, taking the longest record from 265 B to 117 B of content. The three awkward records are handled explicitly rather than by a silent fallback: id 267 (`unknown`, empty date), id 1242 (the width driver), and the single malformed `4-Feb-1971` date, which never mattered because dates are carried as opaque text. 1682 items pad to 2048, domain_bits 11. `PirServer::Answer` does one `EvalFull` then an unconditional inner product; `PirClient` uses beta=1 so reconstruction is a clean difference. **Decided and recorded: P0 and P1 hold the PIR keys while all three servers hold the catalogue**, since the DPF is (2,2) but the system has three servers and the catalogue is public anyway. Tests: 60 random private fetches at b=64 and 20 at b=128, all byte-exact, plus boundary indices, padding-slot fetches, and rejection of a key with mismatched domain_bits. **One test was wrong and it took real investigation to establish that rather than assume it.** A naive "no word of one server's share equals the plaintext" assertion failed with 18 of 32 words matching. That is not a leak: records pad to 256 B while content never exceeds 117 B and the trailing genre flags are almost always zero, so the upper words are identically zero across the *entire* catalogue, and a linear combination of zeros is zero. It is public structure, constant across every alpha. The test now computes which words actually vary from the data and asserts only on those: 14 of 32 at b=64, 7 of 16 at b=128, which agree at 112 bytes and cross-check each other. |
| 2026-09-08 (day 4a) | Mainak | **Graphify finally fixed, then the float-to-ring boundary, which found two real bugs.** Graphify: re-ran on a fresh day in two passes (15/23 chunks, then the remaining 8 from cache). `.graphifyignore` worked, **`third_party`+`references` went from 86% of nodes to 0%**; graph is now 467 nodes/555 edges composed 30% `report`, 22% `include`, 8% `tests`, 6% `src`, and the god nodes are ours (`DpfKey`, `Block`, `Span`, `CorrectionWord`). `graphify query` is reliable again. Fixed three documentation defects: `dpf.hpp`'s wire-format comment said `16+db*17+ring` when `SizeBytes()` is `21+db*18+ring` (211 vs the real 227 at db=11, so anyone sizing a buffer from it would be 16 bytes short); ARCHITECTURE §7.2 said PIR shares "sum" when the implementation uses the difference convention; and REQUIREMENTS D5.1 + PHASES 2.6 still mandated oblivious top-k after the 2026-09-06 row cut it. Wrote `model/export.py`, the only place a float becomes a ring element. **BUG 1, found by the new contract test on its first run:** `numpy.rint` uses banker's rounding (half-to-even) while C++ `std::round` rounds half away from zero, so the two disagreed at every exact half-quantum. Python now matches C++, which is the reference since it ships. **BUG 2:** `.gitignore` line 17 was `data/` **unanchored**, matching any directory of that name at any depth, so the committed contract vectors in `tests/data/` were silently ignored and would never have reached the repo, breaking the boundary test on a fresh clone. Anchored to `/data/` plus an explicit `!tests/data/**`. Verified `test_ring` passes with `model/out/` absent, i.e. the fresh-clone case. Headroom measured, not assumed: worst-case d=16 accumulation is 47.8 bits of 63, so b=64 is safe at MovieLens scale with ~32,000x margin, which is an empirical D9.1 answer. |
| 2026-09-07 (exhaustive sweep) | Mainak | **Ran the full DPF sweep and it PASSED.** `make test-exhaustive` at `bce4cb1`: every alpha against every x for every domain to **d=16 on u64 (5.73e9 leaf checks) and d=15 on u128 (1.43e9)**, 7.16e9 total, in **27.0 min** at ~4.4M checks/s. Recorded in `MEMORY.md` section 7 with the instruction to re-run after any change to `Gen`/`Eval`/`EvalFull`/`Traverse`. **My pre-run estimate of ~15 min was low by 80%**, and the report said "about a quarter of an hour" and "sixteen bits on both rings", both of which were wrong; corrected to the measured 27 min and to the actual per-ring bounds. u128 stops one bit lower by design, since its comparisons cost roughly twice as much and the extra bit buys no new structural coverage for another ~20 min. |
| 2026-09-06 (day 3) | Mainak | **S1 day 3: coverage tiering and the benchmark harness.** **Fixed a false statement I had committed:** `report/sections/tech_dpf.tex` claimed the key size was "measured by a test that prints it" when no test printed it. A test now prints the table (209/227/245/317 B for u64 at db 10/11/12/16, and the u128 row), pins those literals, and checks the closed form across db 1..31, so the report copies the test output rather than the reverse. **Split "exhaustive to d<=16" into two claims**, which is why it had looked infeasible: the difference invariant is carried by `EvalFull` (how the spec itself states it, and 8x cheaper at d=16 than the same coverage via `Eval`), and `Eval` is bridged to it by an index-by-index agreement check. Default `make test` now covers every alpha x every x to **db=11 on BOTH rings** (up from 10/9) in 1.7 s, including the db=11 the demo actually runs since 1682 items pad to 2048; the full db=16 sweep is opt-in via `make test-exhaustive`. **Also found the sweep used one fixed beta per ring** across billions of leaf checks, leaving the subtlest line in `Gen` (the `(-1)^t1` sign on the final correction word) tested at a single value; beta is now derived per alpha. Built `include/oblivrec/bench.hpp` and `bench/bench_dpf.cpp`: JSONL to stdout with the 16 mandated fields in order, provenance filled by the emitter, phase validated on the way out, one row per repetition. 345 rows validate clean against the schema. Makefile gained `bench` (with a `FORCE` prerequisite, without which rows silently carry the previous commit's SHA), `test-exhaustive`, a `figures` guard instead of a traceback, and `check` now captures output and explains exit 127. **Measured:** at db=11 one server answers a query in ~0.28 ms with a 227 B key, `EvalFull` beats 2^d `Eval`s by 3.4-4.4x, and the CSPRNG is <10% of `Gen`. **Caveat recorded honestly:** absolute timings vary up to 4x between back-to-back runs (thermal), while within-run spread is 1.1-1.7x, so the report quotes ratios and orders of magnitude, not precise absolutes. Report now 9 pages. |
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

> **Filled 2026-09-11 from the session table above, which was logged after each work session
> rather than reconstructed at the end.** The three blank rows are **not a claim that nothing was
> done** — they are a statement that nothing was recorded in this repository. Each member should
> fill their own row before submission; Milestone 2 is graded specifically on per-member
> contribution.

| Member | Components delivered | Lines of evidence (commits, tests, results) |
|---|---|---|
| Mainak Sarkar | Whole S1 slice: DPF (BGI), catalogue, DPF-PIR read layer, 2-of-3 replicated sharing, private scoring + seen-item mask, `demo --user 42`, the §9.3 leakage analysis, B1/B5 baselines, threat model, figures pipeline, and all report writing | 9 test binaries green in 1.8 s and clean under UBSan + `_GLIBCXX_DEBUG` + checked `Span`; **7.16e9 exhaustive DPF leaf checks** (27 min); all 1682 scores bit-exact vs the Python oracle; 10/10 records byte-exact through PIR with the ranking matching the oracle in order; `bench/results/*.jsonl` (690 dpf + 60 baseline + 40 leakage rows); 4 real bugs found by tests; every commit in `git log` |
| Shrasti Dwivedi | *to be filled in by Shrasti* — Milestone 1: presentation Part II, slides 10–16 | |
| Aditya Anand | *to be filled in by Aditya* | |
| Shravan Agrawal | *to be filled in by Shravan* | |

### Milestone 3 — Final (6 Nov 2026)

| Member | Components delivered | Report sections | Evaluation owned |
|---|---|---|---|
| Mainak Sarkar | | | |
| Shrasti Dwivedi | | | |
| Aditya Anand | | | |
| Shravan Agrawal | | | |
