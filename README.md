# OblivRec: private training and private delivery for recommendation

**CS670 · Cryptographic Techniques for Privacy Preservation · 2026-27 Semester I · IIT Kanpur**
Course project, topic (a): privacy-preserving recommendation systems. Final submission, tag
`v1.0-final`.

**Shravan Agrawal (230984) · Shrasti Dwivedi (230982) · Mainak Sarkar (230619)**

---

OblivRec composes two systems the course pointed us to. **Nudge** (Henzinger, Dauterman,
Corrigan-Gibbs and Boneh, USENIX Security 2026) trains a matrix-factorisation recommender across
three servers under 2-of-3 replicated secret sharing, publishes the item embeddings, and leaves
the fetch of the recommended item to "other means". **Pirsona** (Vadapalli, Bayatbabolghani and
Henry, PoPETs 2021) fetches privately with multi-server PIR and harvests the next round's training
data from the fetches, but trains with four parties over a Boolean circuit.

We built Nudge's power-iteration training and two-server DPF-based private retrieval, wired them
together with Pirsona's harvest, and measured what the composition costs and what it leaks. Every
protocol is written in C++17 from the papers, with no cryptographic library. The final report is
`report/final.tex` (built PDF: `report/final.pdf`), and the presentation script is
`report/slides/final_presentation.md`.

**Security is semi-honest with at most one corrupted server. Two colluding servers learn
everything.** The security argument is a real/ideal simulation sketch, not a proof.

## Results at a glance

Every number below is recomputed from committed data by `bench/scripts/check_claims.py`, and
every figure is drawn by `mingw32-make figures` from the raw rows in `bench/results/`.

| Result | Value | Data |
|---|---|---|
| Composing costs exactly the sum of the halves | FULL = B2 + B3 − B1 = 64,760,124 bytes | `bench_compose.jsonl` |
| Per session on a 30 ms, 100 Mbit/s link | private delivery +2.5 ms, private training +96 ms (one run shared by 943 users) | `bench_compose.jsonl` |
| Nudge's Theorem 4.2, measured | input matrix 4× larger, traffic +4.4%, rounds unchanged at 2,686 | `bench_sweep.jsonl` |
| Where the rounds go | truncation 68.6 to 74.4% of rounds, the FSS comparison gate 3.1% | `bench_stages.jsonl` |
| Quality cost of the normaliser that reveals nothing | 0.0018 to 0.0041 nDCG@20, for 5.3× the rounds | `train_quality.jsonl`, `bench_stages.jsonl` |
| Fetches observed in the clear | ten fetches recover a user's private embedding to cosine 0.84 (popularity control 0.44) | `leakage_attack.jsonl` |
| Decoys | 40 per query, five times the traffic: the attack only falls from 0.84 to 0.71 | `leakage_attack.jsonl` |
| Nudge's own differential privacy, ε = 1 | quality −71%, and the attack gets stronger, 0.82 to 0.92 | `dp_study.jsonl` |
| The result that goes against us | on that link, downloading the whole catalogue is 4.6× faster than our unbatched private read | `bench_compose.jsonl` |
| Malicious client | sixteen colluders with unchecked weights take nDCG@20 from 0.4536 to 0.0021. A one-round check now closes weight inflation, not redistribution | `poison_study.jsonl`, `tests/test_harvest.cpp` |
| Wire and timing | every query frame 228 B, a classifier on the bytes at chance, server-side timing bounded below 2% of a call | `distinguisher_result.jsonl`, `bench_timing.jsonl` |

## Running it

Tested on Windows 11 with MSYS2's mingw64 toolchain. The randomness (Windows CNG) and the
sockets (Winsock) are Windows APIs, so other platforms are not supported.

**You need**

- MSYS2 with the mingw64 g++ and `mingw32-make` (tested with g++ 16.1.0 and GNU Make 4.4.1).
  Put `C:\msys64\mingw64\bin` **first** on `PATH`, otherwise g++ fails silently. The build checks
  this and says so.
- Python 3.13 as `py -3.13`, with `numpy`, `matplotlib` and `scikit-learn`
  (tested with 2.2.6, 3.10.3 and 1.6.1).
- For the report only: a TeX distribution with `latexmk` (TinyTeX works).

**One command**

```bash
export PATH=/c/msys64/mingw64/bin:$PATH      # Git Bash; or set PATH in cmd/PowerShell
mingw32-make reproduce
```

This fetches MovieLens-100K, builds everything, runs all 18 test binaries, trains the 18 private
models, runs the leakage and malicious-client studies and every benchmark, then draws the 12
figures and builds the report. It takes on the order of a quarter of an hour. It is quiet for most
of that time, so `mingw32-make reproduce-bg` followed by `mingw32-make watch` runs it in the
background with a progress bar and an estimated finish time.

If the download fails (no network, a proxy, or a certificate this Python does not trust),
`scripts/fetch_data.py` says so and prints what to do: download
`https://files.grouplens.org/datasets/movielens/ml-100k.zip` (and `ml-1m.zip`) by hand, place the
zip files in `data/`, and run it again. The checksum is verified either way. The data is not
redistributed in this repository.

**Piece by piece**

| Command | What it does |
|---|---|
| `py -3.13 scripts/fetch_data.py` | MovieLens-100K and 1M into `data/`, checksum-verified |
| `mingw32-make test` | builds and runs the 18 test binaries |
| `mingw32-make check` | the same tests under a hardened build: undefined-behaviour traps, `_GLIBCXX_DEBUG`, bounds-checked spans |
| `mingw32-make test-exhaustive` | the full DPF sweep, every index against every input up to 16 domain bits (slow) |
| `py -3.13 model/export.py` | the cleartext oracle and the fixed-point model the demo serves |
| `./build/train.exe --d 16 --ell 10 --b 128 --normalizer fss --out model/out/B_private_fss_b128_ell10.bin` | private training with the normaliser that reveals nothing |
| `py -3.13 model/score_private.py` | scores the trained models and writes the private model files the demo reads |
| `./build/demo.exe --user 42 --k 10 --a model/out/A_private_fss_b128_ell10.bin --b model/out/B_private_fss_b128_ell10.bin --expect model/out/top10_u42_private_fss_b128_ell10.txt` | serves the privately trained model and fetches ten records by DPF-PIR, each checked byte for byte |
| `mingw32-make demo-net` | the same protocol across three separate server processes over TCP |
| `mingw32-make distinguisher` | records 1,800 queries and trains a classifier on the wire bytes |
| `mingw32-make bench` | every benchmark, appending rows to `bench/results/` (about 12 minutes) |
| `mingw32-make figures` | redraws all 12 figures into `report/figures/` from the committed rows |
| `py -3.13 bench/scripts/check_claims.py` | recomputes the report's headline numbers from the data and checks the documents state them |
| `mingw32-make report` | builds `report/final.pdf` |

Benchmarks append rows tagged with the current commit, and the figures use the newest commit's
rows. Absolute times on a laptop drift with its thermal state, so a rerun will show different
milliseconds. Rounds, bytes and quality will not change.

## Where things are

```
include/oblivrec/   the public headers, one per component
src/dpf/            the distributed point and comparison functions (written from the papers)
src/mpc/            replicated sharing, zero shares, truncation, the comparison gate, inverse square root
src/mf/             power iteration under sharing, both normalisers, the truncation schedule
src/serve/  src/pir/  scoring, the catalogue, the PIR server and client, the harvest
src/net/            TCP framing, the transcript recorder, channel-level network emulation
src/apps/           the three servers, the training driver, the demo, the distinguisher's probe
tests/              18 test binaries, each checking a component against a cleartext twin
bench/              7 benchmarks, raw results in bench/results/, figures from bench/scripts/
model/              the cleartext oracle, model export, the attack, the DP and poisoning studies
report/             final.tex and its sections, the figures, the presentation script
docs/               threat model, contribution log, handover notes
design/             the original design draft, with its decisions log
third_party/nudge/  Nudge's Go reference implementation, vendored for reading only (see below)
references/         reading list (the paper PDFs are not part of the submission zip)
```

**Report section to code**

| Report | Code |
|---|---|
| §5.1 The DPF | `src/dpf/`, `tests/test_dpf_*.cpp` |
| §5.2 Serving and delivery | `src/serve/`, `src/pir/`, `src/net/`, `tests/test_pir.cpp`, `tests/test_serve.cpp` |
| §5.3 The training substrate | `src/mpc/mpc.cpp`, `tests/test_mpc.cpp` |
| §5.4 Truncation | `src/mpc/nonlinear/trunc.cpp`, `tests/test_trunc.cpp` |
| §5.5 The comparison gate and normaliser | `src/dpf/dcf.cpp`, `src/mpc/nonlinear/msnzb.cpp`, `tests/test_dcf.cpp`, `tests/test_msnzb.cpp` |
| §5.6 Power iteration | `src/mf/factor.cpp`, `src/apps/train.cpp`, `tests/test_factor.cpp` |
| §6 Evaluation | `bench/*.cpp`, `bench/scripts/make_figures.py` |
| §7 Leakage and the malicious client | `model/attack.py`, `model/dp_study.py`, `model/poison_study.py`, `bench/bench_dos.cpp`, `src/pir/harvest.cpp` |
| Appendix A, protocols | the files named in each algorithm's caption |

## What we claim, and what we do not

- **One semi-honest server learns nothing beyond the stated leakage.** The item embeddings are
  public by design, as in Nudge, and the report measures what that costs.
- **Two colluding servers learn everything.** A deviating server is out of scope.
- **MovieLens-100K only.** Larger scales are analysed from public parameters, not run.
- **Wide-area numbers come from channel-level emulation** on one laptop, not real networks.
- **The harvest is built and tested but not driven by the running servers.** Every model in
  the report was trained on ratings read from disk.
- **A malicious client can still redistribute vote weight with a forged key**, and full-domain
  evaluation remains a denial-of-service lever. Closing both needs an audit we did not build.

## Provenance

- **The cryptography is ours.** The DPF, DCF, truncation, comparison gate and power iteration
  are written in C++ from the papers. `third_party/nudge/` holds Nudge's MIT-licensed Go
  reference implementation, vendored early on to read alongside the paper. No build target
  compiles or links it. `third_party/NOTICE.md` records what it is and how it was used.
- **Figures are generated, never edited.** Raw results are committed, append-only.
- **MovieLens data is fetched, not redistributed** (`data/` is gitignored).
- Who did what up to the mid-term is logged session by session in `docs/contributions.md`.
