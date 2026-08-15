# `report/` — the LaTeX write-ups

All three graded documents for CS670 live here and **share one preamble and one
bibliography**, so notation and citations cannot drift apart across the semester.

| Build | Produces | Due | Weight |
|---|---|---|---|
| `latexmk -pdf survey.tex` | `survey.pdf` — **the literature survey** | **31 Aug 2026** | 10% |
| `latexmk -pdf midterm.tex` | `midterm.pdf` — two-page per-member contributions | 12 Sep 2026 | 5% |
| `latexmk -pdf final.tex` | `final.pdf` — the final project report | 6 Nov 2026 | 15% |

Only `survey.tex` is live. The other two are stubs with their structure sketched, so the
shape of the semester is visible from day one.

Without latexmk: `pdflatex survey && bibtex survey && pdflatex survey && pdflatex survey`.

---

## The narrative arc

**The section order is the argument.** Do not reorder without raising it at the sync.

```
 §1-2  frame the problem, and establish that "private" is not one thing
 §3    the primitive that makes both halves tractable        — FSS
 §4    how far private TRAINING has come                     — ends at Nudge
 §5    how far private RETRIEVAL has come                    — a parallel literature
 §6    who has tried to do both                              — essentially only PIRSONA
 §7    how this field measures itself                        — what we will be held to
 §8    THE GAP, and what we propose to build
```

Four members, four ~7–8 minute video slots, one deck. The four reading tracks below map
one-to-one onto those slots. **Do not let the video become four disconnected paper
summaries** — that is the most common way to lose marks on this milestone.

---

## Reading tracks

**Ownership is not yet assigned** — that happens at the kickoff. The tracks are sized to be
roughly equal.

| Track | Sections | Core reading |
|---|---|---|
| **T1 — Primitives** | §3 | BGI'15, BGI'16, the July 2026 DPF/FSS survey (arXiv 2607.27696), Grotto, Araki et al., Hafiz–Henry |
| **T2 — Private training** | §4 | Nikolaenko CCS'13, PIRSONA §3–4, Nudge §4–5, Secure Federated MF, FHE 2509.03024 |
| **T3 — Private retrieval** | §5 | SANNS, Tiptoe, Pacmann, Compass, Wally, Panther, P²RAG |
| **T4 — Threat models, systems, the gap** | §2, §6, §7, §8 | SimplePIR, Spiral, PIRSONA §1–2, Nudge §3 and §9, PrivateRec, PICS, Asharov et al. |

**Everyone reads [PIRSONA] and [NUDGE] in full regardless of track.** The viva is individual
and can cover any part of the project.

---

## Layout

| Path | Holds |
|---|---|
| `survey.tex` | Milestone 1 driver. Structure only — title block and `\input` lines. |
| `midterm.tex`, `final.tex` | Milestone 2 and 3 drivers. Stubs. |
| `preamble.tex` | Packages, theorem environments, **all notation macros**, the `\ifdraft` switch. |
| `references.bib` | One bibliography for the whole semester. Read its header — entries carry a verification status. |
| `sections/*.tex` | One file per section. Currently skeletons: headings, subsection structure, and a `\todo{}` saying what each must argue. |
| `notes/` | Per-paper reading notes, markdown, **not compiled**. `_template.md` is the shape; `_synthesis.md` is the argument the survey builds toward. |
| `figures/` | Figures, referenced by filename alone (`\graphicspath` is set). |
| `slides/` | The 30-minute deck. |

---

## Conventions

- **Notation is fixed in `preamble.tex`.** If a symbol is missing, add it there and mention it
  at the sync — never define a competing symbol inside a section file. `\shr{x}` is a secret
  sharing, `\itemembed` / `\userembed` are the embedding matrices, `\pirsona` and `\nudge` name
  *and* cite the base papers.
- **Citation verification is tracked in the bib.** Every entry is marked `[VERIFIED]`,
  `[LISTING]`, or `[UNVERIFIED]`. Most start `[UNVERIFIED]` — they were seeded from a search,
  so author lists and page numbers may be wrong. **Promote your track's entries to `[VERIFIED]`
  as you read.** Do not submit while citing an `[UNVERIFIED]` entry.
- **Draft notes** render orange and are stripped by setting `\draftfalse` in `preamble.tex`.
  Three flavours: `\todo{}` (still to write), `\verify{}` (claim to check against the paper),
  `\gap{}` (a place where the survey must argue the gap). Find outstanding work with:
  ```bash
  grep -rn '\\todo{\|\\verify{\|\\gap{' sections/ *.tex
  ```
- **`\Cref` is unsafe for theorem-like environments** — they share one counter, so cleveref
  calls every one of them "Theorem". Write `Remark~\ref{...}` explicitly. `\Cref` is fine for
  sections, figures and tables.
- **Numbers carry their setting.** "50 minutes" is not a result; "50 minutes on three 192-core
  servers over a LAN" is. This applies in the survey as much as in the final report.

---

## The two figures that matter

The video will be remembered for its diagrams, not its prose. Two are load-bearing:

1. **`fig:prim:ggm`** (§3) — the GGM tree, showing the two parties' paths diverging only on the
   path to α. This is *why* a DPF key is 260 bytes instead of 32 KB.
2. **`fig:gap:timelines`** (§8) — the two literature timelines side by side, with the single
   arrow between them (PIRSONA, 2021) and Nudge's dangling "other means" arrow. **This figure
   is the survey's entire argument in one picture.**

`fig:both:pirsona-loop` (§6) is third in priority.

---

## Before submitting

- [ ] `grep` for `\todo` / `\verify` / `\gap` returns nothing
- [ ] `\draftfalse` set in `preamble.tex`
- [ ] No `[UNVERIFIED]` entry is cited
- [ ] `bibtex` clean; no `??` anywhere in the PDF
- [ ] Timed dry run done and the video is **under** 30 minutes
- [ ] YouTube link is **unlisted** and verified in a private browser window
