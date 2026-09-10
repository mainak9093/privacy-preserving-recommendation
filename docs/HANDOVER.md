# OblivRec — handover, status, and what is left before 12 September

**Written 2026-09-09, updated 2026-09-11. Milestone 2 is due Saturday 12 September.**

> **Update 2026-09-11: Task A below is DONE.** The networking landed, so the Phase 2 exit
> criterion is met in full. The remaining unclaimed work is Tasks B–E in §4.

This is the single document to read if you are picking the project up. It says what exists, how
to check that it works, exactly what is still unclaimed, and how to write your own contribution
section. Everything below is checkable against something in this repository; where it is not,
that is said explicitly.

---

## 0. Status at a glance

| | |
|---|---|
| **Phase 2 (S1: serving + delivery)** | **functionally complete and demonstrated** |
| Phase 3 (S2: private training) | not started — this is the next month's work |
| Test suite | 10 binaries, green in ~2 s, clean under UBSan + `_GLIBCXX_DEBUG` + checked `Span` |
| Graded artifact | `report/midterm.tex` → 2 pages, **three sections still blank** |
| Technical report | `report/midterm_technical.tex` → 17 pages |
| Networking | **done 2026-09-11** — three separate processes over TCP; exit criterion met in full |

**The one-line summary of the system.** Three servers hold 2-of-3 replicated shares of a user's
embedding. Because the item matrix `B` is public (that is NUDGE's design), each server computes
its share of `scores = a·B` locally — no communication, no multiplication protocol. The user
reconstructs, picks the top `k` on their own machine, and then fetches each recommended record by
two-server DPF-PIR, so neither server learns which records were read.

---

## 1. Start here — reading order

If you read nothing else, read the four starred items.

### 1.1 Orientation (about 30 minutes)

| # | File | Why |
|---|---|---|
| 1 | ★ `README.md` | What the project is |
| 2 | ★ **this file** | What is done, what is left |
| 3 | `REQUIREMENTS.md` §§1–3, §10 | The deliverable IDs (D1–D9.5) and the W1–W4 stream split everything else refers to |
| 4 | `PHASES.md` — Phase 2 table | Per-task status, including what is **cut** and what is **deferred**, each with its reason |
| 5 | ★ `design/ARCHITECTURE-draft-v1.md` §11 | **The Decisions Log.** Every non-obvious choice and why. Read this before disagreeing with any of them — most objections are already answered here |
| 6 | `docs/threat-model.md` | What we claim, and §5, what we explicitly do **not** |

> `design/ARCHITECTURE-draft-v1.md` is a **draft**: several of its statements were superseded and
> the corrections live in §11 and in `PHASES.md`. Where the draft and the Decisions Log disagree,
> **the Decisions Log is newer and wins.**

### 1.2 The code, in dependency order (about 1 hour)

Read the headers first — every one opens with a comment block explaining *why* the file is the way
it is, and those comments are the real documentation.

| # | File | What it is |
|---|---|---|
| 7 | `include/oblivrec/ring.hpp`, `span.hpp`, `fixedpoint.hpp` | `u64`/`u128` ring traits, a C++17 stand-in for `std::span`, fixed-point encoding at `t=20` |
| 8 | ★ `include/oblivrec/dpf.hpp` → `src/dpf/dpf.cpp` | The distributed point function. **The difference convention** (no `(-1)^party` factor) is stated here and is load-bearing everywhere downstream |
| 9 | `include/oblivrec/catalogue.hpp` → `src/pir/catalogue.cpp` | MovieLens packed into fixed 256 B records |
| 10 | ★ `include/oblivrec/pir.hpp` → `src/pir/pir.cpp` | The private read. The two obliviousness properties are stated in the header |
| 11 | `include/oblivrec/share.hpp`, `serve.hpp` → `src/serve/serve.cpp` | 2-of-3 replicated sharing and private scoring |
| 12 | `src/apps/demo.cpp` | The end-to-end demo; the clearest single read of how the pieces fit |

### 1.3 The Python side

| # | File | What it is |
|---|---|---|
| 13 | `model/mf.py` | Power-iteration factorisation, written to mirror the protocol rather than call `numpy.linalg.svd`, because S2 gets checked against its *structure* |
| 14 | `model/export.py` | **The only place a float becomes a ring element.** Read its docstring before touching anything numeric |
| 15 | `model/attack.py` | The leakage experiment (§2.5 below) |
| 16 | `bench/scripts/make_figures.py` | Every figure in the report comes from here and is never hand-edited |

### 1.4 Prove it works on your machine

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"   # REQUIRED; see section 6
mingw32-make test                           # ~1.8 s, must be green
py -3.13 scripts/fetch_data.py              # only if data/ is missing
py -3.13 model/export.py                    # writes model/out/ (gitignored)
mingw32-make demo                           # ten real film titles
```

---

## 2. What is built and verified

Everything in this section was written by **Mainak Sarkar (230619)** and is corroborated by the
git history. Each claim names how to reproduce it.

### 2.1 The distributed point function (D2)

Boyle–Gilboa–Ishai, written rather than imported, templated on the ring from the first commit.

- **Exhaustively verified**: every `alpha` against every `x` to 16 domain bits at `b=64` and 15 at
  `b=128` — **7.16 × 10⁹ leaf checks in 27 minutes**. Reproduce: `mingw32-make test-exhaustive`
  (opt-in precisely because a 27-minute suite stops being run).
- Default `make test` still covers every `alpha` × every `x` to **11 domain bits on both rings** in
  1.8 s — 11 because 1682 items pad to 2048, so it is the domain the demo actually runs.
- Key size **227 B** at that operating point against 16 KB for a naive one-hot query. The sizes are
  printed by `tests/test_dpf_serialize.cpp` and copied into the report, never the reverse.
- **A real security bug, found and fixed**: `Gen` seeded a `std::mt19937_64` and used its output as
  DPF seed material. The seeds *are* the secret and MT19937 is reconstructible from 624 consecutive
  outputs, so an adversary with enough key material could rebuild the GGM tree and recover `alpha`;
  entropy was also ~64 bits against a claimed `lambda = 128`. Now `BCryptGenRandom`,
  **abort-on-failure rather than degrade**.

### 2.2 The catalogue and the private read (D5.2)

- Fixed-width **256 B** records, `id | title | date | 19 genre flags`, with explicit length
  prefixes rather than a delimiter — nine titles contain Latin-1 bytes, so any delimiter risks
  colliding with content. 1682 items pad to a 2048 domain (`domain_bits = 11`).
- A server answer is one `EvalFull` plus an **unconditional** inner product. Two distinct
  properties carry the claim: there is no data-dependent branch, *and* there is no data pattern
  either, because every expanded coefficient is a pseudorandom share rather than zero at all but
  one index. The second is the stronger statement.
- Verified byte-exact against a cleartext lookup: 60 random indices at `b=64`, 20 at `b=128`, plus
  both domain endpoints, padding slots, and rejection of a key whose domain width disagrees.
- **P0 and P1 hold the PIR keys**; all three servers hold the catalogue, which is public anyway.

### 2.3 Serving under secret sharing (D5.1)

- `include/oblivrec/share.hpp`: `x = x0+x1+x2`, party `i` holds `(x_i, x_{i+1})`.
- `scores = a·B` is a **public matrix applied to a shared vector**, so it is a local linear map:
  no communication, no correlated randomness, no rounds. This is the largest single saving in the
  serving path and it follows directly from `B` being public.
- **All 1682 scores match the Python oracle bit for bit** — exact equality, not a tolerance. A
  linear map over a ring has no tolerance to spend, and a tolerance would hide exactly the
  endianness / sign-extension / scale bugs this boundary is prone to.
- Seen-item mask: an additive shared vector carrying `-(2^55)`, about 3700× below any real score
  and 256× above the ring floor. Both margins are asserted by a test, not argued in prose.
- **Oblivious top-k was cut** (2026-09-06): the user reconstructs the score vector anyway and
  selects locally, so the servers never learn the selection regardless. The apparatus would have
  protected something already protected.

### 2.4 The demo — the Phase 2 exit criterion

`mingw32-make demo` runs `demo --user 42 --k 10`. It checks itself twice rather than merely
printing something plausible: every fetched record is compared byte-for-byte against a cleartext
lookup, **and** the ranking is compared position-for-position against the independent Python
reference. Both agree.

### 2.5 The leakage result (D9.5) — the strongest thing we have

`ARCHITECTURE §9.3` proposed this and called it *"the analysis that only we can do"*. `model/attack.py`
runs it. Neither PIRSONA nor NUDGE measures it, because neither implements both halves.

Because `B` is public, an observed fetch is not one bit of information — it is a projection onto a
known basis. Given only `B` and the *indices* a user fetched:

| observed fetches `j` | `cos(â, a)` | popularity control | predicts next-20 |
|---|---|---|---|
| 1 | **0.55** | 0.26 | 38% |
| 10 | **0.84** | 0.44 | **56%** |
| 50 | 0.92 | 0.53 | 46% |

**The control is the load-bearing part.** A random-vector control would flatter any attack of this
shape, because popular items sit near everyone's top — an estimator can look strong having learned
only what is public. So the *identical* estimator is run on the globally most-popular items,
ignoring the user; the attack's margin over it is a stable **+0.39 to +0.42** at every `j`. That
margin is the part genuinely about the individual.

Two caveats we raise ourselves: an **individual** user's cosine is not evidence (the random
control's spread is ≈ ±0.18, about `1/√d`), and the overlap column is non-monotone because the
metric excludes already-observed items, so the candidate pool gets harder as `j` grows — `cos`
rises monotonically throughout, which is the check that the estimate itself does not degrade.

An **honest negative result** is kept rather than dropped: a second, more sophisticated estimator
using strictly more information (every unfetched item is a negative) is *worse* at every `j ≥ 2`.
That strengthens the case for PIR — the cheap obvious attack already suffices.

### 2.6 What privacy costs (D6, partial — B1 and B5)

| | bytes/query (one server) | median server time | privacy |
|---|---|---|---|
| B1 cleartext lookup | 260 | 0.0000047 ms | none |
| **DPF-PIR (ours)** | **483** | 0.242 ms | index hidden from each server |
| B5 full download | 430,592 | 0.045 ms | trivially total |

DPF-PIR sends **446× fewer bytes** than B5 counting both servers, and is the **slowest of the
three in server CPU**. The report shows both panels and states the crossover — the private read
wins on any link below **8.0 Gbit/s** — rather than showing only the flattering one.

### 2.7 Four bugs found by tests rather than by inspection

Worth knowing, because each is a trap you could re-introduce:

1. **Rounding mode.** `numpy.rint` uses banker's rounding (half to even); C++ `std::round` rounds
   half away from zero. They disagree at every exact half-quantum. Python was changed to match C++,
   since C++ is what ships.
2. **Quantise-then-multiply ≠ multiply-then-quantise.** The reference encoded the *float product*;
   the protocol multiplies *encoded factors*. 1650 of 1682 scores disagreed — but by only ~5e-6
   relative, so **any tolerance-based test would have passed** and the oracle would have been
   quietly wrong for every later comparison.
3. **Unanchored `.gitignore` pattern.** `data/` matched a directory of that name at *any* depth and
   silently swallowed the committed `tests/data/` fixtures. Fixed to `/data/` plus `!tests/data/**`.
4. **`DpfKey::Deserialize` accepted a `party` byte outside {0,1}**, which made a malformed key
   silently evaluate as party 1 and reconstruct wrongly with no error.

---

## 3. The report files, and how to fill your section

| File | What it is |
|---|---|
| `report/midterm.tex` → `midterm.pdf` | **THE GRADED ARTIFACT.** Two pages, per-member contributions |
| `report/midterm_technical.tex` | 17-page technical companion, offered as an appendix |
| `report/sections/*.tex` | The technical report's sections |
| `report/preamble.tex` | Shared by all three documents. Add notation **here**, not in a section |
| `report/figures/*.pdf` | **Generated. Never hand-edit.** `mingw32-make figures` |

### 3.1 Filling your subsection — read this before you type

Open `report/midterm.tex`, find `\subsection*{Your Name}`, and replace the placeholder
`\vspace{3.2em}` under it with an `\begin{itemize}` list.

**Three rules, in order of how much they will cost you if ignored:**

1. **`\todo{...}` renders as NOTHING.** `preamble.tex` sets `\draftfalse`, so anything you wrap in
   `\todo` will be **silently empty** in the built PDF. This nearly happened and is exactly the
   failure a per-member-graded rubric punishes. **Write plain body text.**
2. **Two pages is a hard limit.** After editing, run `latexmk -pdf midterm.tex` and *check the page
   count*. If you go over, trim your own prose — do not shrink the margins.
3. **Name concrete artefacts, not effort.** Files written, protocols implemented, experiments run,
   sections authored, slides delivered. Every claim should be checkable against a commit, a test, a
   results file, or a section file. The grader can read the git history.

Also add a dated row to `docs/contributions.md` (newest first, in the session table) and fill your
row in its **Milestone 2** table. That file is the source the report is written from.

---

## 4. Unclaimed work you can pick up before 12 September

**Three days.** These are ordered by value, and each is scoped to be genuinely finishable. Each
gives you a real, defensible contributions entry. Pick one and say so on the group chat first, so
two people do not do the same thing.

> **None of these are blocked on anything.** They touch files nobody else is editing.

### ~~Task A — Three-process networking~~ · **DONE 2026-09-11, no longer available**

> Completed on 11 September. `include/oblivrec/channel.hpp`, `src/net/tcp.cpp`,
> `src/net/transcript.cpp`, `src/apps/server.cpp`, `src/apps/probe.cpp` and `tests/test_channel.cpp`.
> `make demo-net` runs three separate OS processes and returns the same ten titles as the
> in-process demo; `make distinguisher` records 1800 queries and shows an adversary at chance.
> **The Phase 2 exit criterion is now met in full.** The description below is kept for context.

#### Original description

This is the largest deferred item and the most obvious question in a viva: *"you say three servers
— do they actually talk?"* Right now they do not; the three parties are three sets of shares in one
process, and `src/net/` was empty. *(Both were true when this was written on 09-11; the work is now done.)*

**Build:**
- `include/oblivrec/channel.hpp` — a `Channel` interface, `Send(Span<const uint8_t>)` / `Recv(...)`,
  with a length-prefixed frame format.
- `src/net/inproc.cpp` — an in-process transport, so tests stay deterministic.
- `src/net/tcp.cpp` — a real loopback TCP transport. **The Makefile already links `-lws2_32` for
  exactly this and it is currently unused**, so there is nothing to add to the build.
- `src/net/transcript.cpp` — tee every frame to `bench/results/transcript.jsonl`.
- A `--net` flag on `src/apps/demo.cpp` so P0 and P1 answer over `127.0.0.1`.
- `tests/test_channel.cpp`.

**Then the experiment that actually closes the exit criterion:** a **distinguisher**. Record many
transcripts for known-different query indices and show an adversary cannot separate them better
than chance. That is stronger evidence than a packet capture *looking* random, because it
demonstrates an adversary **failing**.

**Done when:** `demo --net` returns the same ten titles as the in-process demo; the transcript is
recorded at the channel boundary; the distinguisher reports ≈50% accuracy over ≥1000 trials.

**This closed the half of the Phase 2 exit criterion that was then marked NOT MET**, which was
`PHASES.md`'s most visible open item.

### Task B — The ring-width crossover (W2 · D9.1)

`REQUIREMENTS.md` line 281 has this as an open checklist item: *"D9.1 ring-width crossover found
and explained."* We currently have only one point on that curve — at MovieLens-100K the worst-case
`d=16` accumulation is **47.8 bits of the 63 available**, so `b=64` is safe with ~32,000× margin.

**The open question is where it stops being safe.** `data/ml-1m/` is **already fetched**, so the
scale-up needs no new data.

**Build:** a sweep in `model/` reporting the worst-case accumulation in bits against `m`, `n`, `d`
and `t`, at ML-100K and ML-1M, and the point where it crosses 63. Emit JSONL to `bench/results/`
using the existing schema, add a figure to `bench/scripts/make_figures.py`.

**Done when:** there is a defensible sentence of the form *"`b=64` suffices up to X; beyond that
`b=128` is required, because Y"*, backed by a figure. NUDGE uses `b=128` because `m` is large —
showing *where* that becomes necessary is a genuine, publishable-in-a-course-report finding.

### Task C — Strengthen or break the leakage result (W4 · D7, D9.5)

The attack in §2.5 is one estimator, at one `d`, on one dataset. Any of these is a real result:

- Does it hold at `d = 8` or `d = 32`? Leakage should scale with `d` — **show the curve**.
- Does it hold on **ML-1M** (already fetched)?
- **A defence:** if the client fetches `k` real records plus `r` decoys, how does `cos` degrade with
  `r`? That converts the attack into a *design recommendation*, which is the most valuable version.
- Does it work from **partial** observation — the adversary sees a random subset of fetches, not the
  top-`j`?

**Reuse, do not rewrite:** `model/attack.py` already has the estimators, the popularity and random
controls, the metrics and the JSONL emitter. Most of these are a new loop around existing functions.

**Done when:** new rows in `bench/results/leakage_attack.jsonl`, a figure, and a paragraph in
`report/sections/tech_leakage.tex`.

### Task D — The remaining baselines (W4 · D6)

`REQUIREMENTS.md` §D6 names B1–B5; only **B1 and B5** exist (`bench/bench_baseline.cpp`). B2–B4 are
unimplemented. Read §D6, pick the ones that are meaningful for S1, and add them to that file — the
harness, the JSONL schema and the figure are all already there, so this is mostly measurement.

**Careful:** `bench.hpp` **aborts** on a `phase` outside the ARCHITECTURE §10 enum. Use the existing
`phase="pir"` with a new `op` extra, as `bench_baseline.cpp` does and explains at the top.

### Task E — S2 groundwork: cleartext `Trunc_t` and `ApproxNormalize` (W3 · D3)

Private training needs exactly two interactive protocols, and neither exists even in cleartext. A
Python reference for both — matching `model/mf.py`'s structure the way `mf.py` matches the protocol
— makes Phase 3 dramatically cheaper and is self-contained.

**Done when:** `model/` has reference implementations with tests, and the quality cost of
truncation is measured (does nDCG@20 move from 0.4536?).

---

## 5. After the midterm — Phase 3 (S2: private training)

Starts 13 September, due 6 November. Scope, from `PHASES.md`:

1. `Trunc_t` and `ApproxNormalize` as FSS gates on top of the existing DPF — **this is what the DPF
   was always for**; the PIR layer is its other consumer.
2. Power iteration running under replicated sharing. The matrix–vector products are free; only
   truncation and normalisation cost rounds, which is NUDGE's core insight and why `ell` matters.
3. `ell = 10` is already justified on evidence: nDCG@20 moves 0.5% while the SVD subspace angle
   falls five orders of magnitude (`docs/finding-ell-vs-quality.md`), so the 40× communication
   saving is free.
4. Then S3: compose the two halves and run the comparative evaluation.

---

## 6. Commands, and the one that will waste your afternoon

```bash
# THIS FIRST, EVERY SHELL. Without it g++ cannot spawn as/ld and fails SILENTLY --
# no binary, no error message. `make check-toolchain` turns that into a loud failure.
export PATH="/c/msys64/mingw64/bin:$PATH"

mingw32-make test              # 9 binaries, ~1.8 s
mingw32-make check             # UBSan trap mode + _GLIBCXX_DEBUG + checked Span
mingw32-make test-exhaustive   # the full DPF sweep, ~27 min, opt-in
mingw32-make demo              # the end-to-end demo
mingw32-make bench             # appends JSONL to bench/results/
mingw32-make figures           # regenerates every figure from that JSONL

py -3.13 scripts/fetch_data.py # MovieLens (gitignored, not in the repo)
py -3.13 model/export.py       # model/out/ — needed by test_serve and demo
py -3.13 model/attack.py       # the leakage experiment
```

If a hardened binary exits **127**, that is the loader, not a test failure: Git for Windows ships an
incompatible `libwinpthread-1.dll` and MSYS2's must win the search order. The `check` recipe detects
this and says so.

---

## 7. House rules that will trip you up

1. **Amend the design before the code.** If you contradict `design/ARCHITECTURE-draft-v1.md`, add a
   Decisions Log row in §11 *first*, then change the code. Never silently diverge — most of that log
   exists because someone (me) did it the other way round once.
2. **`bench/results/*.jsonl` is append-only.** A bad run is superseded by a new one and filtered by
   `git_sha` at plot time, never deleted or edited.
3. **Figures are generated.** Never hand-edit one; change `make_figures.py` and re-run.
4. **No new third-party dependencies.** The build is a hand-written Makefile because CMake is not
   installed and installing it would breach that rule. `bcrypt`/`ws2_32` are Windows system
   libraries, not dependencies.
5. **Tests that need generated data must SKIP, not fail** — `model/out/` and `data/` are gitignored,
   so a fresh clone must still go green. `tests/data/fixedpoint_vectors.txt` is the exception: it is
   **committed on purpose**, and its absence is a hard failure.
6. **Some files are deliberately local-only** and are gitignored. If `git status` ever shows them,
   stop and fix the ignore rule rather than committing them.
