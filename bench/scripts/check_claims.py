"""check_claims.py -- every headline number, recomputed, and checked where it is stated.

WHY THIS EXISTS (PHASES task 5.4). The report, the presentation script, the
README and the threat model quote numbers as text, while the figures are drawn
from the newest commit's rows in bench/results/. Re-run one benchmark and the
figures move while the prose does not. A careful read of thirty pages cannot
reliably catch that, so this does it mechanically:

  1. each claim's value is RECOMPUTED from the committed data, with the same
     newest-sha rule make_figures.py uses (its own loader is imported, so the
     two cannot disagree about which rows count);
  2. the recomputed value, formatted as the documents state it, must appear in
     a specific phrase in each document that states it.

So the check fails if the data moves or if a document's wording drifts away
from its number. Either way the fix is to re-trace that sentence.

Some claims are internal identities rather than quoted numbers (additivity to
the byte, a zero residual, a closed form a test asserts); those are asserted
directly.

    py -3.13 bench/scripts/check_claims.py
    py -3.13 bench/scripts/check_claims.py --deck path/to/deck.pptx   # also the slides

Exits non-zero on any mismatch, and prints every claim it checked.
"""
import argparse
import collections
import contextlib
import io
import json
import math
import os
import re
import sys
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
os.chdir(REPO)
with contextlib.redirect_stdout(io.StringIO()):
    import make_figures as mf          # noqa: E402  the figures' own loader


def newest(name):
    with contextlib.redirect_stdout(io.StringIO()):
        return mf.newest_sha(mf.load(name), name)


def all_rows(name):
    with contextlib.redirect_stdout(io.StringIO()):
        return mf.load(name)


# --------------------------------------------------------------------------
#  Documents, normalised so an anchor is a plain phrase.
# --------------------------------------------------------------------------
def tex_inputs():
    files, root = [], os.path.join(REPO, "report")
    with open(os.path.join(root, "final.tex"), encoding="utf-8") as fh:
        for line in fh:
            code = re.sub(r"(?<!\\)%.*", "", line)
            for m in re.finditer(r"\\input\{([^}]+)\}", code):
                if m.group(1) != "preamble":
                    files.append(os.path.join(root, m.group(1) + ".tex"))
    return files


def norm_tex(text):
    lines = [re.sub(r"(?<!\\)%.*", "", l) for l in text.splitlines()]
    t = " ".join(lines)
    t = t.replace("{,}", ",").replace("\\,", " ").replace("\\%", "%").replace("~", " ")
    for _ in range(3):
        t = re.sub(r"\\(?:textbf|emph|texttt|textit)\{([^{}]*)\}", r"\1", t)
    t = t.replace("\\times", "×").replace("\\varepsilon", "ε").replace("\\approx", "≈")
    t = t.replace("$", "").replace("\\\\", " ")
    return re.sub(r"\s+", " ", t)


def norm_md(text):
    t = text.replace("**", "").replace("`", "")
    return re.sub(r"\s+", " ", t)


def deck_text(path):
    """Paragraphs of every slide, joined with ' | ', notes excluded."""
    out = []
    with zipfile.ZipFile(path) as z:
        names = sorted((n for n in z.namelist()
                        if re.match(r"ppt/slides/slide\d+\.xml$", n)),
                       key=lambda n: int(re.findall(r"\d+", n)[0]))
        for n in names:
            xml = z.read(n).decode("utf-8")
            for p in re.findall(r"<a:p>.*?</a:p>|<a:p .*?</a:p>", xml, flags=re.S):
                txt = "".join(re.findall(r"<a:t>([^<]*)</a:t>", p))
                txt = (txt.replace("&amp;", "&").replace("&lt;", "<")
                       .replace("&gt;", ">").replace("&quot;", '"').replace("&apos;", "'"))
                if txt.strip():
                    out.append(txt.strip())
    return " | " + " | ".join(out) + " | "


def load_docs(deck):
    docs = {
        "report": norm_tex("\n".join(open(f, encoding="utf-8").read() for f in tex_inputs())),
        "script": norm_md(open("report/slides/final_presentation.md", encoding="utf-8").read()),
        "readme": norm_md(open("README.md", encoding="utf-8").read()),
        "threat": norm_md(open("docs/threat-model.md", encoding="utf-8").read()),
    }
    if deck:
        docs["deck"] = deck_text(deck)
    return docs


# --------------------------------------------------------------------------
#  The claims.
# --------------------------------------------------------------------------
CLAIMS = []      # (id, value, [(doc, regex-with-{v})])
CHECKS = []      # (id, ok, detail) for identities asserted directly


def claim(cid, value, *anchors):
    CLAIMS.append((cid, str(value), list(anchors)))


def check(cid, ok, detail):
    CHECKS.append((cid, bool(ok), detail))


def f2(x):
    return f"{x:.2f}"


def build():
    # ---- the reconstruction attack and its control ----------------------
    leak = newest("leakage_attack.jsonl")
    by = {(r["op"], r.get("j")): r for r in leak if r["op"] in ("centroid", "margin", "popularity", "random")}
    dec = {r["r"]: r for r in leak if r["op"] == "decoy_defence"}
    cos = lambda j: by[("centroid", j)]["cos_mean"]
    pop = lambda j: by[("popularity", j)]["cos_mean"]
    js = sorted(j for (op, j) in by if op == "centroid")
    claim("attack.cos.j1", f2(cos(1)),
          ("report", r"One observed fetch gives a cosine of {v}"),
          ("script", r"the guess already has a cosine similarity of {v}"),
          ("deck", r"\| {v} \| cosine with your true vector after one fetch"))
    claim("attack.cos.j10", f2(cos(10)),
          ("report", r"Ten give {v}"),
          ("script", r"After ten fetches it reaches {v}"),
          ("readme", r"to cosine {v}"),
          ("deck", r"\| {v} \| after ten fetches"))
    claim("attack.next20.j10", f"{by[('centroid', 10)]['overlap_mean'] * 100:.0f}",
          ("report", r"name {v}% of the user's next twenty"),
          ("script", r"predicts {v} percent of your next twenty"),
          ("deck", r"\| {v}% \| of your next twenty"))
    claim("attack.control.j10", f2(pop(10)),
          ("script", r"It reaches a cosine of {v}"),
          ("readme", r"\(popularity control {v}\)"),
          ("deck", r"\| {v} \| the same method on the ten most popular items"))
    claim("attack.margin.j1", f2(cos(1) - pop(1)),
          ("report", r"The margin over the control is {v} at one fetch"),
          ("deck", r"The margin is {v} after one fetch"),
          ("threat", r"\+{v} at j = 1"))
    margins = [cos(j) - pop(j) for j in js if j >= 2]
    claim("attack.margin.min.j2+", f2(min(margins)),
          ("report", r"between {v} and \d\.\d\d from two fetches on"),
          ("deck", r"{v} to \d\.\d\d from two to fifty"))
    claim("attack.margin.max.j2+", f2(max(margins)),
          ("report", r"between \d\.\d\d and {v} from two fetches on"),
          ("deck", r"\d\.\d\d to {v} from two to fifty"))
    claim("attack.random", f"{by[('random', 1)]['cos_mean']:.3f}",
          ("report", r"The random direction averages {v}"),
          ("deck", r"A random direction scores {v}"))
    claim("attack.cos.j50", f2(cos(50)),
          ("report", r"\d\.\d\d against {v} at fifty"))
    claim("attack.constraints.j50", f2(by[("margin", 50)]["cos_mean"]),
          ("report", r"{v} against \d\.\d\d at fifty"))
    claim("decoys.r0", f"{dec[0]['cos_mean']:.3f}",
          ("report", r"only from {v} to"))
    claim("decoys.r40", f"{dec[40]['cos_mean']:.3f}",
          ("report", r"to {v}, against a popularity control at"))
    claim("decoys.r40.2dp", f2(dec[40]["cos_mean"]),
          ("script", r"the attack only falls from 0\.84 to {v}"),
          ("readme", r"only falls from 0\.84 to {v}"),
          ("deck", r"\| {v} \| the attack after them, from 0\.84"))
    claim("decoys.floor", f"{pop(10):.3f}",
          ("report", r"against a popularity control at {v}"))

    # ---- the wire ---------------------------------------------------------
    dist = newest("distinguisher_result.jsonl")
    frame = {r["frame_bytes"] for r in dist}
    check("wire.frame.single", len(frame) == 1, f"frame sizes in the result rows: {sorted(frame)}")
    claim("wire.frame", str(frame.pop()),
          ("report", r"Every query frame is {v} bytes"),
          ("script", r"every query frame is {v} bytes"),
          ("readme", r"every query frame {v} B"),
          ("deck", r"{v} B / 265 B"))
    acc = sorted(r["accuracy"] for r in dist if not r["shuffled"])
    claim("wire.acc.min", f"{acc[0]:.3f}",
          ("script", r"scores between {v} and"),
          ("deck", r"\| {v} to \d\.\d\d\d \|"))
    claim("wire.acc.max", f"{acc[-1]:.3f}",
          ("script", r"scores between \d\.\d\d\d and {v}"),
          ("deck", r"\| \d\.\d\d\d to {v} \|"))
    sh = [r["accuracy"] for r in dist if r["shuffled"]]
    claim("wire.shuffled", f"{sh[0]:.3f}",
          ("report", r"shuffled labels & {v}"),
          ("deck", r"\| {v} \| the same pipeline on shuffled labels"))

    # ---- differential privacy ---------------------------------------------
    dp = [r for r in newest("dp_study.jsonl") if r.get("normalisation") == "l2"]
    base = next(r for r in dp if r["epsilon"] is None)
    e1 = next(r for r in dp if r["epsilon"] == 1.0)
    e8 = next(r for r in dp if r["epsilon"] == 8.0)
    claim("dp.ndcg.base", f"{base['ndcg_at_20']:.4f}",
          ("report", r"no noise \(rows normalised\) & {v}"),
          ("script", r"nDCG@20 {v} to"),
          ("deck", r"nDCG@20 from {v} to"))
    claim("dp.ndcg.e1", f"{e1['ndcg_at_20']:.4f}",
          ("report", r"ε = 1 & {v}"),
          ("script", r"to {v}, −71%"),
          ("deck", r"from 0\.4255 to {v}"))
    drop = round(100 * (base["ndcg_at_20"] - e1["ndcg_at_20"]) / base["ndcg_at_20"])
    claim("dp.drop.pct", str(drop),
          ("report", r"quality falls by {v}%"),
          ("script", r"recommendation quality falls by {v} percent"),
          ("readme", r"quality −{v}%"),
          ("deck", r"\| −{v}% \|"))
    claim("dp.attack.base", f2(base["cos_attack"]),
          ("script", r"It gets stronger, from {v} to"),
          ("readme", r"stronger, {v} to"),
          ("deck", r"{v} → 0\.92"))
    claim("dp.attack.e1", f2(e1["cos_attack"]),
          ("script", r"It gets stronger, from 0\.82 to {v}"),
          ("readme", r"stronger, 0\.82 to {v}"),
          ("deck", r"0\.82 → {v}"))
    claim("dp.ndcg.e8", f"{e8['ndcg_at_20']:.3f}",
          ("report", r"Even at ε = 8 quality falls to {v}"))
    claim("dp.signal", f"{e1['signal_spectral']:.1f}",
          ("report", r"= {v} against"))
    claim("dp.noise", f"{e1['noise_spectral']:.0f}",
          ("report", r"≈ {v} at ε = 1"))
    claim("dp.noise_over_signal", f"{e1['noise_spectral'] / e1['signal_spectral']:.1f}",
          ("report", r"by a factor of {v}"),
          ("script", r"Noise against signal \| about {v}×"),
          ("deck", r"\| {v}× \| noise over signal"))
    # m / sqrt(n), Netflix (480,189 x 17,770) against ML-100K (943 x 1,682)
    scale = (480189 / math.sqrt(17770)) / (943 / math.sqrt(1682))
    claim("dp.scale", f"{scale:.0f}",
          ("report", r"which is {v} times larger for Netflix"),
          ("script", r"Netflix {v} times further"),
          ("deck", r"\| {v}× \| larger m / √n for Netflix"))

    # Users with fewer than 25 ratings in the training split: needs the data.
    if os.path.exists(os.path.join(REPO, "data", "ml-100k", "u1.base")):
        sys.path.insert(0, os.path.join(REPO, "model"))
        with contextlib.redirect_stdout(io.StringIO()):
            import data as dataset
            train, _ = dataset.load_split("u1")
        under = int(((train > 0).sum(axis=1) < 25).sum())
        claim("dp.users.under25", str(under),
              ("report", r"{v} of the 943 users have fewer than 25 ratings"),
              ("script", r"{v} of our users are in that range"),
              ("deck", r"\| {v} of 943 \|"))
    else:
        check("dp.users.under25", True, "SKIPPED: data/ml-100k not present (run scripts/fetch_data.py)")

    # ---- composition (task 4.2) ---------------------------------------------
    comp = newest("bench_compose.jsonl")
    net = {(r["config"], r["profile"], r["op"]): r for r in comp if r.get("phase") == "net"}
    W = lambda a: net[(a, "wan_a", "compose")]
    B = lambda a: net[(a, "wan_a", "compose")]["bytes_sent"]
    check("compose.additive.session", B("FULL") == B("B2") + B("B3") - B("B1"),
          f"FULL {B('FULL')} vs B2+B3-B1 {B('B2') + B('B3') - B('B1')}")
    tp = next(r for r in comp if r.get("op") == "train_private" and r.get("config") == "FULL")
    dl = next(r for r in comp if r.get("op") == "deliver_pir" and r.get("config") == "FULL")
    claim("compose.full.bytes", f"{tp['bytes_sent'] + dl['bytes_sent']:,}",
          ("report", r"it is {v} bytes"),
          ("script", r"FULL = B2 \+ B3 − B1 = {v} B"),
          ("readme", r"FULL = B2 \+ B3 − B1 = {v} bytes"),
          ("deck", r"{v} bytes, asserted"))
    claim("compose.delivery.ms", f"{W('B2')['wall_ms'] - W('B1')['wall_ms']:.1f}",
          ("report", r"private delivery adds {v} ms per session"),
          ("readme", r"private delivery \+{v} ms"),
          ("deck", r"\| \+{v} ms \| private delivery"))
    claim("compose.training.ms", f"{W('B3')['wall_ms'] - W('B1')['wall_ms']:.0f}",
          ("report", r"private training adds {v} ms"),
          ("readme", r"private training \+{v} ms"),
          ("deck", r"\| \+{v} ms \| private training"))
    claim("compose.b5.faster", f"{W('B2')['wall_ms'] / W('B5')['wall_ms']:.1f}",
          ("report", r"B5 is {v} times"),
          ("readme", r"{v}× faster than our unbatched private read"),
          ("deck", r"\| {v}× \| faster to download"))
    claim("compose.b5.bytes", f"{B('B5') / B('B2'):.0f}",
          ("report", r"sends {v} times as many bytes"),
          ("deck", r"\| {v}× \| more bytes"))
    claim("compose.batched.ms", f"{net[('B2', 'wan_a', 'compose_batched')]['wall_ms']:.1f}",
          ("report", r"batched B2 at {v} ms"),
          ("script", r"would take {v} ms"),
          ("deck", r"\| {v} ms \| batched private retrieval"))
    claim("compose.b5.ms", f"{W('B5')['wall_ms']:.1f}",
          ("report", r"against B5's {v} ms"),
          ("script", r"against the download's {v} ms"),
          ("deck", r"{v} ms against 303\.9 ms"))
    qual = newest("train_quality.jsonl")
    q10 = next(r for r in qual if r["ell"] == 10 and r["b"] == 64)
    claim("compose.ndcg.oracle", f"{q10['ndcg_at_20_oracle']:.3f}",
          ("report", r"quality moves from {v} to"),
          ("script", r"barely moves, from {v} to"),
          ("deck", r"\| {v} → 0\.451 \|"))
    claim("compose.ndcg.private", f"{q10['ndcg_at_20_private']:.3f}",
          ("report", r"quality moves from 0\.454 to {v}"),
          ("script", r"barely moves, from 0\.454 to {v}"),
          ("deck", r"\| 0\.454 → {v} \|"))

    # ---- the sweep and Theorem 4.2 (task 4.1) -------------------------------
    sw = [r for r in newest("bench_sweep.jsonl") if r.get("op") == "approxfactor" and r.get("profile") == "local"]
    pt = {(r["m"], r["d"], r["ell"], r["b"]): r for r in sw}
    rnd = lambda m, d, l, b=64: pt[(m, d, l, b)]["rounds"]
    claim("sweep.rounds.d", f"{rnd(943, 8, 10):,}, {rnd(943, 16, 10):,} and {rnd(943, 32, 10):,}",
          ("report", r"\({v} at \\dime = 8, 16, 32\)"))
    claim("sweep.rounds.ell1", f"{rnd(943, 16, 1):,}",
          ("report", r"\({v} at \\iters = 1,"))
    claim("sweep.rounds.ell25", f"{rnd(943, 16, 25):,}",
          ("report", r", {v} at \\iters = 25\)"))
    flat = {rnd(m, 16, 10) for m in (235, 471, 943)}
    check("sweep.rounds.flat_in_m", len(flat) == 1, f"rounds at m = 235, 471, 943: {sorted(flat)}")
    by_m = {m: pt[(m, 16, 10, 64)]["bytes_sent"] for m in (235, 471, 943)}
    claim("sweep.mb.235", f"{by_m[235] / 1e6:.2f}",
          ("report", r"traffic from {v} to"),
          ("deck", r"from {v} to 64\.75 MB"))
    claim("sweep.mb.943", f"{by_m[943] / 1e6:.2f}",
          ("report", r"traffic from 62\.03 to {v} MB"),
          ("deck", r"from 62\.03 to {v} MB"))
    claim("sweep.growth.pct", f"{100 * (by_m[943] / by_m[235] - 1):.1f}",
          ("report", r"an increase of {v}%"),
          ("script", r"traffic grew by only {v} percent"),
          ("readme", r"traffic \+{v}%"),
          ("deck", r"\| \+{v}% \|"))
    per_user = (by_m[943] - by_m[235]) / (943 - 235)
    check("sweep.per_user.linear", per_user == (by_m[471] - by_m[235]) / (471 - 235),
          f"bytes per added user: {per_user} (235->943) vs {(by_m[471] - by_m[235]) / (471 - 235)} (235->471)")
    check("sweep.per_user.closed_form", per_user == 16 * 10 * 3 * 8,
          f"{per_user} bytes per user against d*ell*3*8 = {16 * 10 * 3 * 8}")
    claim("sweep.per_user", f"{per_user:,.0f}",
          ("report", r"exactly linear at {v} bytes per added user"))
    wan = next(r for r in newest("bench_sweep.jsonl") if r.get("op") == "approxfactor" and r.get("profile") == "wan_a"
               and r["m"] == 943 and r["d"] == 16 and r["ell"] == 10 and r["b"] == 64)
    lat = wan["rounds"] * 30.0 / 1000.0
    xfer = wan["bytes_sent"] * 8 / 100e6
    check("sweep.wan_a.model", abs(wan["wall_ms"] / 1000.0 - (lat + xfer)) < 0.01,
          f"wan_a time {wan['wall_ms'] / 1000:.2f} s = {lat:.2f} s latency + {xfer:.2f} s transfer")
    claim("sweep.wan_a.total", f"{wan['wall_ms'] / 1000.0:.1f}",
          ("report", r"one training run takes {v} s, of which"))
    claim("sweep.wan_a.latency", f"{lat:.1f}",
          ("report", r"of which {v} s is round-trip latency"))

    # ---- where the rounds go (task 4.3) ---------------------------------------
    st = newest("bench_stages.jsonl")
    an = [r for r in st if r.get("op") == "attrib_analytic"]
    tot = {(r["b"], r["normalizer"]): r for r in st if r.get("op") == "attrib_total" and r.get("wall_ms") == 0}
    cfgs = sorted({(r["b"], r["normalizer"]) for r in an})
    for c in cfgs:
        parts = [r for r in an if (r["b"], r["normalizer"]) == c]
        rr, bb = sum(r["rounds"] for r in parts), sum(r["bytes_sent"] for r in parts)
        check(f"stages.residual.{c[0]}.{c[1]}", rr == tot[c]["rounds"] and bb == tot[c]["bytes_sent"],
              f"phases sum to {rr} rounds / {bb} B, total {tot[c]['rounds']} / {tot[c]['bytes_sent']}")
    tr = [r for r in an if r["phase"] == "truncate"]
    claim("stages.trunc.rounds.min", f"{min(r['share_rounds'] for r in tr):.1f}",
          ("report", r"It is {v} to \d\d\.\d% of rounds"),
          ("readme", r"truncation {v} to \d\d\.\d% of rounds"))
    claim("stages.trunc.rounds.max", f"{max(r['share_rounds'] for r in tr):.1f}",
          ("report", r"It is \d\d\.\d to {v}% of rounds"),
          ("readme", r"truncation \d\d\.\d to {v}% of rounds"))
    claim("stages.trunc.rounds.rounded", f"{min(r['share_rounds'] for r in tr):.0f} to {max(r['share_rounds'] for r in tr):.0f}",
          ("deck", r"\| {v}% \| of all rounds are truncation"))
    claim("stages.trunc.bytes", f"{min(r['share_bytes'] for r in tr):.1f} to {max(r['share_bytes'] for r in tr):.1f}",
          ("report", r"and {v}% of bytes"))
    gate = next(r for r in an if r["phase"] == "fss" and r["rounds"] > 0)
    claim("stages.gate", f"{gate['share_rounds']:.1f}",
          ("report", r"is {v}% of rounds"),
          ("readme", r"the FSS comparison gate {v}%"),
          ("deck", r"\| {v}% \| the FSS comparison gate"))
    claim("stages.total.fss", f"{tot[(128, 'fss')]['rounds']:,}",
          ("report", r"What revealing nothing costs instead is rounds: {v} against"),
          ("deck", r"{v} rounds, 143,941,376 bytes"))
    claim("stages.ratio", f"{tot[(128, 'fss')]['rounds'] / tot[(128, 'reveal')]['rounds']:.1f}",
          ("report", r"a factor of {v}\."),
          ("readme", r"for {v}× the rounds"))
    insitu = {(r["b"], r["normalizer"]): r["rounds"] for r in st if r.get("op") == "attrib_insitu"}
    claim("stages.insitu.fss", f"{insitu[(128, 'fss')]:,}",
          ("report", r"880 rounds for each revealing path and {v} for"))
    mv = [r for r in an if r["phase"] == "matvec"]
    claim("stages.matvec", f"{mv[0]['rounds']}",
          ("report", r"cost {v} rounds, one per product"))

    # ---- quality (task 3.7 and the no-leak study) -----------------------------
    q = {(r["ell"], r["b"], r["normalizer"].split()[0]): r["ndcg_at_20_private"] for r in qual}
    oracle = {r["ell"]: r["ndcg_at_20_oracle"] for r in qual}
    ells = sorted(oracle)
    gaps = [max(q[(l, 64, "reveal-norm")], q[(l, 128, "reveal-norm")]) - q[(l, 128, "fss")] for l in ells if l >= 2]
    gaps += [min(q[(l, 64, "reveal-norm")], q[(l, 128, "reveal-norm")]) - q[(l, 128, "fss")] for l in ells if l >= 2]
    claim("quality.noleak.min", f"{min(gaps):.4f}",
          ("report", r"between {v} and 0\.0041 of nDCG@20"),
          ("readme", r"{v} to 0\.0041 nDCG@20"),
          ("threat", r"between {v} and 0\.0041"))
    claim("quality.noleak.max", f"{max(gaps):.4f}",
          ("report", r"between 0\.0018 and {v} of nDCG@20"),
          ("readme", r"0\.0018 to {v} nDCG@20"),
          ("script", r"0\.0018 to {v} of nDCG@20"))
    ctl = max(abs(q[(l, 64, "reveal-norm")] - q[(l, 128, "reveal-norm")]) for l in ells)
    claim("quality.control.diff", f"{ctl:.4f}",
          ("report", r"differ by at most {v}"))
    within = max(abs(q[(l, 64, "reveal-norm")] - oracle[l]) for l in ells)
    check("quality.within.oracle", within <= 0.007, f"revealing path vs oracle, worst {within:.4f}")
    claim("quality.ell1.fss", f"{q[(1, 128, 'fss')]:.4f}",
          ("report", r"scores {v} against 0\.3729"))
    claim("quality.ell10.gap", f"{q[(10, 128, 'reveal-norm')] - q[(10, 128, 'fss')]:.4f}",
          ("report", r"the gap is {v}"),
          ("deck", r"\| {v} \| the real cost of revealing nothing"))

    # ---- the oracle's ell sweep ------------------------------------------------
    ell = all_rows("oracle_ell_sweep.jsonl")
    nd = [r["ndcg_at_20"] for r in ell]
    claim("oracle.ndcg.range", f"{min(nd):.4f} and {max(nd):.4f}",
          ("report", r"nDCG@20 stays between {v}"))
    ang = {r["ell"]: r["svd_subspace_angle_rad"] for r in ell}
    claim("oracle.angle.400", f"{ang[400]:.1e}".replace("e-06", " × 10^{-6}"),
          ("report", r"to {v}"))

    # ---- the malicious client (task 4.7) ---------------------------------------
    po = newest("poison_study.jsonl")
    coll = {r["attackers"]: r for r in po if r["op"] == "harvest_poison_collude"}
    solo = {r["weight"]: r for r in po if r["op"] == "harvest_poison" and r["attackers"] == 1}
    claim("poison.8", f"{-coll[8]['ndcg_rel_pct']:.1f}",
          ("report", r"Eight cost {v}%"),
          ("script", r"Eight cost {v} percent"))
    claim("poison.16", f"{coll[16]['ndcg_at_20_clean']:.4f} to {coll[16]['ndcg_at_20']:.4f}",
          ("report", r"sixteen take nDCG@20 from {v}"),
          ("readme", r"take nDCG@20 from {v}"),
          ("deck", r"take nDCG@20 from {v}"))
    one = sorted([-100 * (solo[10 ** 6]["ndcg_at_20"] - solo[10 ** 6]["ndcg_at_20_clean"]) / solo[10 ** 6]["ndcg_at_20_clean"],
                  -coll[1]["ndcg_rel_pct"]])
    claim("poison.single", f"{one[0]:.1f} and {one[1]:.1f}",
          ("report", r"by between {v}%"))
    claim("poison.rank", f"{solo[10 ** 6]['mean_target_rank_clean']:.0f} to {solo[10 ** 6]['mean_target_rank']:.0f}",
          ("report", r"from {v} of 1,682"))
    tested = sorted(coll)
    check("poison.only_powers_of_two", tested == [1, 2, 4, 8, 16, 32],
          f"colluder counts run: {tested} (so the collapse is only located between 8 and 16)")

    # ---- the DoS amplification --------------------------------------------------
    dos_all = all_rows("bench_dos.jsonl")
    shas = list(dict.fromkeys(r["git_sha"] for r in dos_all))
    at11 = [next(r["amplification_time"] for r in dos_all if r["git_sha"] == s and r.get("op") == "dos_amplification"
                 and r["domain_bits"] == 11) for s in shas]
    claim("dos.11.newest", f"{at11[-1]:.0f}",
          ("report", r"buys {v} times its own cost in server time in the run the figure shows"),
          ("script", r"buys {v} times its own cost in server work in the run on screen"),
          ("deck", r"One 227-byte key buys {v}×"))
    claim("dos.11.earlier", f"{at11[0]:.0f}",
          ("report", r"and {v} times in an earlier committed run"),
          ("threat", r"measured at {v}× and"))
    d16 = next(r["amplification_time"] for r in newest("bench_dos.jsonl") if r.get("op") == "dos_amplification" and r["domain_bits"] == 16)
    claim("dos.16.newest", f"{d16:,.0f}",
          ("report", r"At 16 bits the same run measures {v} times"))

    # ---- the timing bound ---------------------------------------------------------
    tm = [r for r in newest("bench_timing.jsonl") if r.get("op") == "answer_timing"]
    byal = collections.defaultdict(list)
    for r in tm:
        byal[r["alpha"]].append(r["wall_ms"])
    samples = [np.asarray(byal[a], dtype=float) for a in sorted(byal)]
    means = np.array([s.mean() for s in samples])
    grand = float(np.concatenate(samples).mean())
    between = float(means.max() - means.min())
    within_sd = float(np.median([s.std(ddof=1) for s in samples]))
    allv, sizes = np.concatenate(samples), [len(s) for s in samples]
    rng, null = np.random.default_rng(0), []
    for _ in range(2000):                      # identical to make_figures.fig_timing
        perm, idx, gm = rng.permutation(allv), 0, []
        for sz in sizes:
            gm.append(perm[idx:idx + sz].mean()); idx += sz
        null.append(max(gm) - min(gm))
    pval = float((np.asarray(null) >= between).mean())
    claim("timing.mean.us", f"{grand * 1000:.0f}",
          ("report", r"The mean call takes {v} μs|The mean call takes {v} \\mu"),
          ("deck", r"of an {v} µs call"))
    claim("timing.spread.us", f"{between * 1000:.1f}",
          ("report", r"is {v} \\mu|is {v} μs"),
          ("deck", r"between records, {v} µs"))
    claim("timing.within.us", f"{within_sd * 1000:.1f}",
          ("report", r"deviation of {v} \\mu|deviation of {v} μs"),
          ("deck", r"within one record, {v} µs"))
    claim("timing.p", f"{pval:.2f}",
          ("report", r"p = {v}"),
          ("deck", r"\| p = {v} \|"))
    claim("timing.bound.pct", f"{100 * between / grand:.1f}",
          ("report", r"below {v}% of the mean call"))

    # ---- the private read against its baselines -----------------------------------
    base_all = all_rows("bench_baseline.jsonl")
    ratios, cross = [], []
    for s in dict.fromkeys(r["git_sha"] for r in base_all):
        b = collections.defaultdict(list)
        for r in base_all:
            if r["git_sha"] == s:
                b[r["op"]].append(r)
        pir_t = float(np.median([r["wall_ms"] for r in b["dpf_pir_answer"]]))
        b5_t = float(np.median([r["wall_ms"] for r in b["b5_fulldownload"]]))
        pir_b, b5_b = b["dpf_pir_answer"][0]["bytes_sent"], b["b5_fulldownload"][0]["bytes_sent"]
        ratios.append(pir_t / b5_t)
        cross.append((b5_b - 2 * pir_b) / ((2 * pir_t - b5_t) / 1000.0) * 8 / 1e9)
    newest_base = newest("bench_baseline.jsonl")
    pb = next(r for r in newest_base if r["op"] == "dpf_pir_answer")["bytes_sent"]
    bb5 = next(r for r in newest_base if r["op"] == "b5_fulldownload")["bytes_sent"]
    claim("pir.bytes", str(pb), ("report", r"DPF-PIR sends {v} bytes to each server"))
    claim("pir.fewer", f"{bb5 / (2 * pb):.0f}",
          ("report", r"it moves {v} times fewer bytes"),
          ("deck", r"\| {v}× \| fewer bytes"))
    claim("pir.cpu.ratios", " and ".join(f"{x:.1f}" for x in ratios),
          ("report", r"in both committed runs \({v}\)"))
    claim("pir.crossover", " to ".join(f"{x:.1f}" for x in sorted(cross)),
          ("deck", r"\| {v} \| Gbit/s"))
    claim("pir.crossover.report", f"{min(cross):.1f}",
          ("report", r"{v} Gbit/s in one committed run"))

    # ---- ring width (D9.1) ----------------------------------------------------------
    hr = [r for r in newest("train.jsonl") if r.get("op") == "headroom"]
    h = {(r["dataset"], r["b"], r["t"]): r for r in hr}
    claim("ring.b64.t20", ", ".join(str(h[(d, 64, 20)]["headroom_bits"]) for d in ("ml-100k", "ml-1m")) +
          f" and {h[('netflix-ish', 64, 20)]['headroom_bits']}",
          ("report", r"with {v} bits to spare"))
    check("ring.b64.t20.immediate", not any(h[(d, 64, 20)]["deferred"] for d in ("ml-100k", "ml-1m", "netflix-ish")),
          "the deferred schedule must not fit at b=64, t=20 at any scale")
    check("ring.netflix.t24", not h[("netflix-ish", 64, 24)]["feasible"], "no safe schedule at Netflix size, b=64, t=24")
    claim("ring.b128.min", str(min(r["headroom_bits"] for r in hr if r["b"] == 128)),
          ("report", r"at least {v} spare bits"))
    claim("ring.b128.ml100k", str(h[("ml-100k", 128, 20)]["headroom_bits"]),
          ("report", r"deferred schedule fits with {v}"))

    # ---- closed forms the tests assert ---------------------------------------------
    key = 1 + 4 + 16 + 18 * 11 + 64 // 8
    claim("dpf.key.11", str(key),
          ("report", r"a key is {v} bytes against 16,384 bytes"),
          ("script", r"{v} bytes for our catalogue"),
          ("deck", r"\| {v} B \| the key each server receives"))
    leaves = sum(4 ** d for d in range(1, 17)) + sum(4 ** d for d in range(1, 16))
    claim("dpf.exhaustive", f"{leaves / 1e9:.2f}",
          ("report", r"which is {v} × 10\^9 leaf comparisons"),
          ("script", r"That is {v} billion checks"),
          ("deck", r"\| {v} × 109 \|"))


# --------------------------------------------------------------------------
def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--deck", help="also check the slides of this .pptx")
    args = ap.parse_args(argv[1:])
    build()
    docs = load_docs(args.deck)
    fails = 0
    for cid, value, anchors in CLAIMS:
        missing = []
        for doc, pat in anchors:
            if doc not in docs:
                continue
            rx = pat.replace("{v}", re.escape(value))
            if not re.search(rx, docs[doc]):
                missing.append(doc)
        n = len([a for a in anchors if a[0] in docs])
        if missing:
            fails += 1
            print(f"FAIL  {cid:28s} {value!r} not found as stated in: {', '.join(missing)}")
        else:
            print(f"ok    {cid:28s} {value!r} ({n} place{'s' if n != 1 else ''})")
    for cid, ok, detail in CHECKS:
        print(f"{'ok  ' if ok else 'FAIL'}  {cid:28s} {detail}")
        fails += (not ok)
    print(f"\n{len(CLAIMS)} claims, {len(CHECKS)} identities, {fails} failure(s)"
          + ("" if args.deck else "  (slides not checked: pass --deck)"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
