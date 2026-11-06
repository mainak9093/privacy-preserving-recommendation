"""Draw the report's figures as PNGs for the README and docs/results.md.

GitHub shows PNG inline but not the PDF figures `mingw32-make figures` writes for
the report. This reuses make_figures.py's own plotting functions and the same
newest-commit rows, and changes nothing but the output format, so a PNG here and
the matching figure in the report always come from the same data.

    mingw32-make readme-figures
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "bench", "scripts"))
os.chdir(REPO)

import make_figures as mf           # noqa: E402

OUT = os.path.join("docs", "img")


def save_png(fig, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, os.path.splitext(name)[0] + ".png")
    fig.savefig(path, dpi=130, bbox_inches="tight", facecolor="white",
                pil_kwargs={"optimize": True})
    mf.plt.close(fig)
    print(f"  wrote {path}")


mf.FIGDIR = OUT
mf.save = save_png
sys.exit(mf.main())
