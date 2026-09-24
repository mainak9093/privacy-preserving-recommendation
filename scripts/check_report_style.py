"""House-style and draft-note check for everything final.tex inputs.

WHY A SCRIPT. The project's writing rule bans em dashes and semicolons in the
prose of anything submitted or read aloud (report, slides, narration script),
because both read as machine-written to a grader. Thirty-odd pages is too much
prose to hold to that by eye, and the inherited mid-term sections broke the rule
in 26 lines despite it predating them. The preamble also sets \\draftfalse, so a
forgotten \\todo{} renders as NOTHING -- invisible rather than loud -- and only a
search finds it.

WHAT IS EXEMPT, per the rule: direct quotations (``...''), table cells, TikZ,
and citation or year ranges. So tabular, tikzpicture and algorithmic
environments are skipped, text inside ``...'' is removed before checking, and
LaTeX's thin-space macro \\; is not a semicolon. An en dash between digits
(pages 11--20) is a range and is allowed. An en dash between words
(matrix--vector, Newton--Raphson) is a compound, not a clause break, and is
allowed. What is flagged is `---`, a spaced ` -- `, and the Unicode dashes.

    py -3.13 scripts/check_report_style.py            # final.tex and its inputs
    py -3.13 scripts/check_report_style.py FILE ...   # any .tex or .md files

Exits non-zero on any finding.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORT = os.path.join(REPO, "report")

SKIP_ENVS = ("tabular", "tikzpicture", "algorithmic", "align", "align*",
             "equation", "equation*")
STALE = [r"next phase", r"not yet built", r"not implemented yet",
         r"\bS1 only\b", r"covers S1 only"]
DRAFT = re.compile(r"\\(todo|verify|gap)\{")


def inputs_of(tex_path):
    """final.tex plus every file it \\inputs, in order, ignoring comments."""
    out = [tex_path]
    with open(tex_path, encoding="utf-8") as fh:
        for line in fh:
            code = strip_comment(line)
            for m in re.finditer(r"\\input\{([^}]+)\}", code):
                name = m.group(1)
                if name == "preamble":
                    continue
                path = os.path.join(REPORT, name + ".tex")
                if os.path.exists(path):
                    out.append(path)
    return out


def strip_comment(line):
    """Drop an unescaped % and everything after it."""
    return re.sub(r"(?<!\\)%.*", "", line)


def check_tex(path):
    findings = []
    depth = {e: 0 for e in SKIP_ENVS}
    with open(path, encoding="utf-8") as fh:
        lines = fh.readlines()
    for no, raw in enumerate(lines, 1):
        code = strip_comment(raw).rstrip("\n")
        for e in SKIP_ENVS:
            depth[e] += code.count("\\begin{%s}" % e)
        skipping = any(v > 0 for v in depth.values())
        text = re.sub(r"``.*?''", "", code)          # quotations exempt
        text = text.replace("\\;", "")                # thin space, not prose

        if DRAFT.search(code):
            findings.append((no, "draft note", raw.strip()))
        for pat in STALE:
            if re.search(pat, text, flags=re.I):
                findings.append((no, "stale phrase", raw.strip()))
        if not skipping:
            if "---" in text or "\u2014" in text or "\u2013" in text:
                findings.append((no, "em dash", raw.strip()))
            elif re.search(r"\s--\s", text):
                findings.append((no, "spaced en dash", raw.strip()))
            if ";" in text:
                findings.append((no, "semicolon", raw.strip()))
        for e in SKIP_ENVS:
            depth[e] -= code.count("\\end{%s}" % e)
    return findings


def check_md(path):
    """Markdown: skip code fences and table rows, keep quotations exempt."""
    findings = []
    fence = False
    with open(path, encoding="utf-8") as fh:
        for no, raw in enumerate(fh, 1):
            if raw.lstrip().startswith("```"):
                fence = not fence
                continue
            if fence or raw.lstrip().startswith("|"):
                continue
            text = re.sub(r"\u201c.*?\u201d|\".*?\"", "", raw)
            if "\u2014" in text or "---" in text.strip("-\n ") or \
                    re.search(r"\s--\s|\s\u2013\s", text):
                findings.append((no, "em dash", raw.strip()))
            if ";" in text:
                findings.append((no, "semicolon", raw.strip()))
    return findings


def main(argv):
    files = argv[1:] or inputs_of(os.path.join(REPORT, "final.tex"))
    total = 0
    for f in files:
        found = check_md(f) if f.endswith(".md") else check_tex(f)
        try:
            shown = os.path.relpath(f, REPO)
        except ValueError:          # a file on another drive, on Windows
            shown = f
        for no, kind, line in found:
            print(f"{shown}:{no}: {kind}: {line[:110]}")
        total += len(found)
    print(f"\n{len(files)} file(s) checked, {total} finding(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
