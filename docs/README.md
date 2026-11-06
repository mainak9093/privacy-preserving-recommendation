# Documentation

Everything in this repository, by what you want to know.

## Start here

| If you want | Read |
|---|---|
| The project in two minutes | [`README.md`](../README.md) |
| Every result, with its figure and data file | [`results.md`](results.md) |
| How the system works, and where each piece is in the code | [`architecture.md`](architecture.md) |
| What is protected, against whom, and what is not | [`threat-model.md`](threat-model.md) |
| The whole argument, written up | [Final report, 40 pages (PDF)](../report/final.pdf) |
| The talk | [Slides (PDF)](../report/slides/OblivRec_final_presentation.pdf) · [PowerPoint with speaker notes](../report/slides/OblivRec_final_presentation.pptx) · [Speaker script (PDF)](../report/slides/OblivRec_presentation_script.pdf) |

## Deeper

| Document | What it is |
|---|---|
| [`../bench/README.md`](../bench/README.md) | Which program writes which results file, the row format, and how rows become figures and checked claims |
| [`finding-ell-vs-quality.md`](finding-ell-vs-quality.md) | Why ten power iterations are enough, the measurement behind the operating point |
| [`../report/survey.pdf`](../report/survey.pdf) | The literature survey that came first: cryptographic approaches to private recommendation |
| [`../report/README.md`](../report/README.md) | Every document under `report/`, and how to build it |
| [`../references/INDEX.md`](../references/INDEX.md) | Every work cited, with links |
| [`../third_party/NOTICE.md`](../third_party/NOTICE.md) | What the vendored Nudge code is, and that it is not built |

## The project's working history

[`history/`](history/README.md) keeps the planning documents as the project used them: the requirements, the phase-by-phase
schedule with task status, the mid-term plan and handover, and the working log. They were right for their dates and are not
the current description of the system. The design draft and its log of 88 dated decisions, including the ones later
reversed, is in [`../design/ARCHITECTURE-draft-v1.md`](../design/ARCHITECTURE-draft-v1.md).

## Reading the code comments

The code explains its decisions in comments, using the project's working labels.

| Label | Means | Where it is defined |
|---|---|---|
| `ARCHITECTURE section N` | A section of the design draft | [`design/ARCHITECTURE-draft-v1.md`](../design/ARCHITECTURE-draft-v1.md) |
| `REQUIREMENTS section N`, `D1` to `D9` | A section or deliverable of the requirements | [`history/REQUIREMENTS.md`](history/REQUIREMENTS.md) |
| `task 4.2`, `Phase 3` | A task or phase in the schedule | [`history/PHASES.md`](history/PHASES.md) |
| `S1`, `S2`, `S3` | The build stages: serving and delivery, private training, composition | [`history/PHASES.md`](history/PHASES.md) |
| `B1` to `B5` | The baselines: B1 clear training and fetch, B2 clear training with PIR, B3 private training with clear fetch, B4 a generic MPC framework (not built), B5 clear training with full download | [`results.md`](results.md#what-the-composition-costs) |
| `F1` to `F12` | The report's figures | [`results.md`](results.md), and Appendix B of the [report](../report/final.pdf) |
| `A1` to `A6` | The adversary classes | [`threat-model.md`](threat-model.md#2-adversary-classes) |
| `b`, `t`, `d`, `ell`, `k` | Ring bits, fractional bits, embedding dimension, power iterations, recommendations returned | [`architecture.md`](architecture.md) |
