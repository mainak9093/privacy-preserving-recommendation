#!/usr/bin/env python3
"""Download the MovieLens datasets used by OblivRec into ``data/``.

Datasets are never committed (see .gitignore). Run once after cloning::

    python scripts/fetch_data.py            # ml-100k and ml-1m
    python scripts/fetch_data.py --all      # also ml-25m (250 MB, scale sweep only)
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent.parent / "data"

# name -> (url, md5 of the zip as published by GroupLens)
# ml-100k and ml-1m were verified against a live download on 2026-08-15.
# ml-25m is taken from the published checksum and has NOT been verified here;
# if it mismatches on first use, check it against GroupLens before editing.
DATASETS: dict[str, tuple[str, str]] = {
    "ml-100k": (
        "https://files.grouplens.org/datasets/movielens/ml-100k.zip",
        "0e33842e24a9c977be4e0107933c0723",
    ),
    "ml-1m": (
        "https://files.grouplens.org/datasets/movielens/ml-1m.zip",
        "c4d9eecfca2ab87c1945afe126590906",
    ),
    "ml-25m": (
        "https://files.grouplens.org/datasets/movielens/ml-25m.zip",
        "6b51fb2759a8657d3bfcbfc42b592ada",
    ),
}

DEFAULT = ["ml-100k", "ml-1m"]


def md5(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.md5()
    with path.open("rb") as fh:
        for block in iter(lambda: fh.read(chunk), b""):
            h.update(block)
    return h.hexdigest()


def fetch(name: str, force: bool = False) -> None:
    url, expected = DATASETS[name]
    target = DATA_DIR / name

    if target.exists() and not force:
        print(f"[skip]  {name} already present at {target}")
        return

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    archive = DATA_DIR / f"{name}.zip"

    print(f"[get]   {name} <- {url}")
    with urllib.request.urlopen(url) as response, archive.open("wb") as out:
        shutil.copyfileobj(response, out)

    digest = md5(archive)
    if digest != expected:
        archive.unlink(missing_ok=True)
        raise SystemExit(
            f"[fail]  {name}: md5 mismatch\n"
            f"        expected {expected}\n"
            f"        got      {digest}\n"
            f"        Refusing to unpack. The model artefacts must be byte-identical\n"
            f"        on both servers, so a corrupt download is not recoverable later."
        )
    print(f"[ok]    {name}: md5 {digest}")

    if target.exists():
        shutil.rmtree(target)
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(DATA_DIR)
    archive.unlink()
    print(f"[done]  {name} -> {target}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="also fetch ml-25m (~250 MB)")
    parser.add_argument("--force", action="store_true", help="re-download even if present")
    parser.add_argument("names", nargs="*", choices=[*DATASETS, []], help="specific datasets")
    args = parser.parse_args(argv)

    names = args.names or (list(DATASETS) if args.all else DEFAULT)
    for name in names:
        fetch(name, force=args.force)

    print(f"\nData directory: {DATA_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
