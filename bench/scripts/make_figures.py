"""Regenerate every figure in the report from bench/results/*.jsonl.

ARCHITECTURE section 10 and RULES B5 both require that every figure comes from
this script and is never hand-edited. That is the whole point of it: a figure
that was touched by hand cannot be regenerated, and a number that cannot be
regenerated is a number nobody can defend in a viva.

The results files are APPEND-ONLY. A bad run is superseded by a new one and
filtered out here by git_sha, never deleted. By default each figure uses the
MOST RECENT sha present for the rows it needs, and prints which one it chose,
so a figure silently built from stale rows is visible rather than invisible.

Run:  mingw32-make figures        (which calls: py -3.13 bench/scripts/make_figures.py)
"""
import collections
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")           # no display on this box; must precede pyplot
import matplotlib.patches as mpatches   # noqa: E402
import matplotlib.pyplot as plt         # noqa: E402
import numpy as np                      # noqa: E402

RESULTS = os.path.join("bench", "results")
FIGDIR = os.path.join("report", "figures")

# One consistent visual language across every figure.
C_ATTACK = "#b2182b"        # the attack
C_ALT = "#ef8a62"           # the second estimator
C_CONTROL = "#4393c3"       # controls
C_FLOOR = "#999999"
plt.rcParams.update({
    "figure.figsize": (5.4, 3.4),
    "font.size": 9,
    "axes.grid": True,
    "grid.alpha": 0.25,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "legend.frameon": False,
})


def load(name):
    path = os.path.join(RESULTS, name)
    if not os.path.exists(path):
        return []
    out = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def newest_sha(rows, label):
    """Keep only rows from the most recent git_sha, by file order.

    File order is append order, so the last sha to appear is the newest. Using
    the timestamp would be wrong if the clock moved; using order matches how
    the file is actually written.
    """
    if not rows:
        return []
    sha = rows[-1].get("git_sha")
    kept = [r for r in rows if r.get("git_sha") == sha]
    print(f"  {label}: sha {sha}, {len(kept)}/{len(rows)} rows")
    return kept


def save(fig, name):
    os.makedirs(FIGDIR, exist_ok=True)
    path = os.path.join(FIGDIR, name)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {path}")


# --------------------------------------------------------------------------
#  F1 -- the reconstruction attack. The headline figure.
# --------------------------------------------------------------------------
def fig_attack(rows):
    if not rows:
        print("  SKIP fig_attack: no leakage rows (run py -3.13 model/attack.py)")
        return False

    by_op = collections.defaultdict(list)
    for r in rows:
        by_op[r["op"]].append(r)
    for v in by_op.values():
        v.sort(key=lambda r: r["j"])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.4))

    style = {
        "centroid":   ("attack (centroid)", C_ATTACK, "o", "-"),
        "margin":     ("attack (ranking constraints)", C_ALT, "s", "-"),
        "popularity": ("control: popularity only", C_CONTROL, "^", "--"),
        "random":     ("control: random direction", C_FLOOR, "", ":"),
    }

    for op, (label, colour, marker, ls) in style.items():
        rs = by_op.get(op)
        if not rs:
            continue
        j = [r["j"] for r in rs]
        ax1.plot(j, [r["cos_mean"] for r in rs], color=colour, marker=marker,
                 ls=ls, ms=4, label=label)
        ax2.plot(j, [100 * r["overlap_mean"] for r in rs], color=colour,
                 marker=marker, ls=ls, ms=4, label=label)

    # The IQR band on the attack itself, so the spread is visible rather than
    # hidden behind a mean.
    rs = by_op.get("centroid", [])
    if rs:
        ax1.fill_between([r["j"] for r in rs], [r["cos_q1"] for r in rs],
                         [r["cos_q3"] for r in rs], color=C_ATTACK, alpha=0.15,
                         lw=0)

    ax1.set_xscale("log")
    ax1.set_xticks([1, 2, 5, 10, 20, 50])
    ax1.set_xticklabels(["1", "2", "5", "10", "20", "50"])
    ax1.set_xlabel("observed fetches $j$")
    ax1.set_ylabel(r"$\cos(\hat{a}, a)$")
    ax1.set_title("Recovery of the user's embedding", fontsize=9)
    ax1.set_ylim(-0.1, 1.0)
    ax1.axhline(0, color="k", lw=0.6, alpha=0.4)
    ax1.legend(loc="lower right", fontsize=7.5)

    ax2.set_xscale("log")
    ax2.set_xticks([1, 2, 5, 10, 20, 50])
    ax2.set_xticklabels(["1", "2", "5", "10", "20", "50"])
    ax2.set_xlabel("observed fetches $j$")
    ax2.set_ylabel("top-20 overlap (%)")
    ax2.set_title("Prediction of the user's next 20 recommendations", fontsize=9)
    ax2.set_ylim(0, 65)

    save(fig, "fig_attack.pdf")
    return True


# --------------------------------------------------------------------------
#  F2 -- DPF key size against the naive one-hot query.
#
#  Sizes come from the closed form that tests/test_dpf_serialize.cpp pins and
#  asserts equal to the actual serialised length. This figure and that test
#  must agree; if the format changes, the test fails first.
# --------------------------------------------------------------------------
def fig_keysize():
    db = np.arange(8, 21)
    key64 = 21 + 18 * db + 8
    key128 = 21 + 18 * db + 16
    naive64 = (2 ** db) * 8

    fig, ax = plt.subplots()
    ax.plot(db, naive64, color=C_FLOOR, ls="--", marker="",
            label=r"naive one-hot query, $b=64$")
    ax.plot(db, key64, color=C_ATTACK, marker="o", ms=3.5,
            label=r"DPF key, $b=64$")
    ax.plot(db, key128, color=C_ALT, marker="s", ms=3.5,
            label=r"DPF key, $b=128$")

    # The operating point the demo actually runs at.
    ax.plot([11], [21 + 18 * 11 + 8], marker="*", ms=13, color=C_ATTACK,
            ls="none", zorder=5)
    ax.annotate("ML-100K demo\n2048 items, 227 B", xy=(11, 227),
                xytext=(12.2, 700), fontsize=7.5,
                arrowprops=dict(arrowstyle="->", lw=0.7))

    ax.set_yscale("log")
    ax.set_xlabel("domain bits (catalogue of $2^{\\mathrm{db}}$ records)")
    ax.set_ylabel("query bytes per server")
    ax.set_title("Query size is logarithmic, not linear, in the catalogue",
                 fontsize=9)
    ax.legend(loc="upper left", fontsize=7.5)
    save(fig, "fig_keysize.pdf")
    return True


# --------------------------------------------------------------------------
#  F4 -- ell against recommendation quality.
#
#  The evidence for keeping ell = 10: ell multiplies the only interactive cost
#  in training, and quality is flat in it.
# --------------------------------------------------------------------------
def fig_ell(rows):
    if not rows:
        print("  SKIP fig_ell: no oracle sweep rows")
        return False
    rows = sorted(rows, key=lambda r: r.get("ell", 0))
    ell = [r["ell"] for r in rows]
    ndcg = [r.get("ndcg_at_20", r.get("ndcg")) for r in rows]
    ang = [r.get("svd_subspace_angle_rad") for r in rows]
    if any(v is None for v in ndcg):
        print("  SKIP fig_ell: rows lack an nDCG field")
        return False

    fig, ax = plt.subplots()
    ax.plot(ell, ndcg, color=C_ATTACK, marker="o", ms=4, label="nDCG@20")
    ax.set_xscale("log")
    ax.set_xlabel(r"power-iteration rounds $\ell$")
    ax.set_ylabel("nDCG@20", color=C_ATTACK)
    lo, hi = min(ndcg), max(ndcg)
    pad = max((hi - lo) * 3, 0.01)
    ax.set_ylim(lo - pad, hi + pad)

    if all(v is not None for v in ang):
        ax2 = ax.twinx()
        ax2.plot(ell, ang, color=C_CONTROL, marker="^", ms=4, ls="--",
                 label="subspace angle")
        ax2.set_yscale("log")
        ax2.set_ylabel("max principal angle vs SVD (rad)", color=C_CONTROL)
        ax2.grid(False)
        ax2.spines["top"].set_visible(False)

    ax.set_title(r"Quality is flat in $\ell$ while convergence is not",
                 fontsize=9)
    save(fig, "fig_ell.pdf")
    return True


# --------------------------------------------------------------------------
#  F3 -- the private read against its two baselines, at the operating point.
#
#  Deliberately NOT a curve against domain size. Catalogue::LoadMovieLens reads
#  the real 1682-item file; there is no synthetic catalogue, and plotting a
#  sweep would mean inventing one. The bytes-against-domain-size story is
#  already carried analytically by F2.
#
#  The two panels must be read together, and the second is the unflattering
#  one. Showing only bytes would be advocacy rather than evaluation.
# --------------------------------------------------------------------------
def fig_pir_cost(base_rows):
    if not base_rows:
        print("  SKIP fig_pir_cost: no baseline rows (run mingw32-make bench)")
        return False

    by_op = collections.defaultdict(list)
    for r in base_rows:
        by_op[r.get("op")].append(r)

    order = ["b1_cleartext", "dpf_pir_answer", "b5_fulldownload"]
    label = {"b1_cleartext": "B1\ncleartext\n(no privacy)",
             "dpf_pir_answer": "DPF-PIR\n(this work)",
             "b5_fulldownload": "B5\nfull download\n(trivially private)"}
    colour = {"b1_cleartext": C_FLOOR,
              "dpf_pir_answer": C_ATTACK,
              "b5_fulldownload": C_CONTROL}

    present = [o for o in order if by_op.get(o)]
    if not present:
        print("  SKIP fig_pir_cost: baseline rows carry no recognised op")
        return False

    byts = [by_op[o][0]["bytes_sent"] for o in present]
    tms = [float(np.median([r["wall_ms"] for r in by_op[o]])) for o in present]
    x = np.arange(len(present))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.4))
    for ax, vals, ylab, title in (
            (ax1, byts, "bytes per query, one server",
             "Communication"),
            (ax2, tms, "median server time per query (ms)",
             "Server computation")):
        ax.bar(x, vals, color=[colour[o] for o in present], width=0.6)
        ax.set_yscale("log")
        ax.set_xticks(x)
        ax.set_xticklabels([label[o] for o in present], fontsize=7.5)
        ax.set_ylabel(ylab)
        ax.set_title(title, fontsize=9)
        ax.grid(axis="x", visible=False)
        for xi, v in zip(x, vals):
            ax.annotate(f"{v:,.0f}" if v >= 1 else f"{v:.2g}",
                        xy=(xi, v), xytext=(0, 3), textcoords="offset points",
                        ha="center", fontsize=7.5)
        ax.set_ylim(top=max(vals) * 6)

    save(fig, "fig_pir_cost.pdf")

    # State the trade in the console too, so a run reports its own conclusion.
    d = dict(zip(present, zip(byts, tms)))
    if "dpf_pir_answer" in d and "b5_fulldownload" in d:
        pb, pt = d["dpf_pir_answer"]
        bb, bt = d["b5_fulldownload"]
        # PIR talks to two servers; B5 to one.
        extra_ms = 2 * pt - bt
        saved_bytes = bb - 2 * pb
        if extra_ms > 0 and saved_bytes > 0:
            bw = saved_bytes / (extra_ms / 1000.0)      # bytes per second
            print(f"    trade: {bb / (2 * pb):.0f}x fewer bytes, "
                  f"{extra_ms:.3f} ms extra CPU")
            print(f"    -> PIR wins below {bw * 8 / 1e9:.1f} Gbit/s, "
                  f"i.e. on any real network")
    return True


# --------------------------------------------------------------------------
#  F5 -- the distinguisher. An adversary FAILING is the evidence.
#
#  Plotted as accuracy with its 95% Wilson interval against the chance line.
#  The reading that matters is whether chance falls INSIDE each interval, so
#  the chance line and the intervals are the two things the eye must catch.
# --------------------------------------------------------------------------
def fig_distinguisher(rows):
    if not rows:
        print("  SKIP fig_distinguisher: no rows (run mingw32-make distinguisher)")
        return False

    real = [r for r in rows if not r.get("shuffled")]
    ctrl = [r for r in rows if r.get("shuffled")]
    if not real:
        print("  SKIP fig_distinguisher: no unshuffled rows")
        return False

    labels = [r["pair"].replace("alpha=", "").replace("|", " vs ") for r in real]
    acc = [r["accuracy"] for r in real]
    lo = [r["accuracy"] - r["ci_lo"] for r in real]
    hi = [r["ci_hi"] - r["accuracy"] for r in real]

    if ctrl:
        labels.append("shuffled\ncontrol")
        acc.append(ctrl[-1]["accuracy"])
        lo.append(ctrl[-1]["accuracy"] - ctrl[-1]["ci_lo"])
        hi.append(ctrl[-1]["ci_hi"] - ctrl[-1]["accuracy"])

    x = np.arange(len(labels))
    colours = [C_ATTACK] * len(real) + ([C_FLOOR] if ctrl else [])

    fig, ax = plt.subplots(figsize=(6.2, 3.4))
    ax.errorbar(x, acc, yerr=[lo, hi], fmt="o", ms=6, capsize=5, lw=1.4,
                ecolor="#555555", ls="none",
                mfc=colours[0], mec=colours[0])
    for xi, (a, c) in enumerate(zip(acc, colours)):
        ax.plot([xi], [a], "o", ms=6, color=c)

    ax.axhline(0.5, color="k", ls="--", lw=1.0)
    ax.annotate("chance", xy=(len(labels) - 0.5, 0.5), xytext=(3, 4),
                textcoords="offset points", fontsize=8, ha="right")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=8)
    ax.set_ylabel("distinguisher accuracy")
    ax.set_ylim(0.30, 0.70)
    ax.set_title("An observer cannot tell which record was fetched", fontsize=9)
    nq = real[0].get("total_queries")
    fb = real[0].get("frame_bytes")
    if nq and fb:
        ax.annotate(f"{nq} recorded queries, every frame {fb} B",
                    xy=(0.5, 0.02), xycoords="axes fraction", ha="center",
                    fontsize=7.5, color="#555555")
    save(fig, "fig_distinguisher.pdf")
    return True


# --------------------------------------------------------------------------
#  F6 -- the decoy defence, and why it does not work.
#
#  The point of this figure is the POPULARITY FLOOR. Decoys must be drawn
#  popularity-weighted or they are separable by inspection, which means they
#  point roughly along the popularity direction -- and the popularity direction
#  is exactly what the control already recovers. So decoys drag the estimate
#  down towards the control's level and no further.
#
#  Plotting the control as a floor line is what makes that visible, and it is
#  the difference between "decoys help a bit" and "decoys cannot fix this".
# --------------------------------------------------------------------------
def fig_decoy(rows):
    dec = sorted([r for r in rows if r.get("op") == "decoy_defence"],
                 key=lambda r: r.get("r", 0))
    if not dec:
        print("  SKIP fig_decoy: no decoy rows (run py -3.13 model/attack.py)")
        return False

    # The popularity control at the matching number of real fetches (k=10).
    pop = [r for r in rows if r.get("op") == "popularity" and r.get("j") == 10]
    floor = pop[0]["cos_mean"] if pop else None

    r = [x["r"] for x in dec]
    cos = [x["cos_mean"] for x in dec]
    overlap = [100 * x["overlap_mean"] for x in dec]
    kbytes = [x.get("bytes_per_query", 0) / 1000.0 for x in dec]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.4))

    ax1.plot(r, cos, color=C_ATTACK, marker="o", ms=4,
             label="attack, with decoys")
    if floor is not None:
        ax1.axhline(floor, color=C_CONTROL, ls="--", lw=1.2,
                    label="popularity control (the floor)")
        ax1.fill_between([min(r), max(r)], 0, floor, color=C_CONTROL,
                         alpha=0.08, lw=0)
    ax1.set_xlabel("decoys per query, $r$")
    ax1.set_ylabel(r"$\cos(\hat{a}, a)$")
    ax1.set_ylim(0.0, 1.0)
    ax1.set_title("Decoys cannot push below the popularity floor", fontsize=9)
    ax1.legend(loc="lower left", fontsize=7.5)

    ax2.plot(kbytes, cos, color=C_ATTACK, marker="o", ms=4)
    for xi, yi, ri in zip(kbytes, cos, r):
        if ri in (0, 10, 40):
            ax2.annotate(f"r={ri}", xy=(xi, yi), xytext=(4, 5),
                         textcoords="offset points", fontsize=7.5)
    if floor is not None:
        ax2.axhline(floor, color=C_CONTROL, ls="--", lw=1.2)
    ax2.set_xlabel("bandwidth per query (KB)")
    ax2.set_ylabel(r"$\cos(\hat{a}, a)$")
    ax2.set_ylim(0.0, 1.0)
    ax2.set_title("What the protection costs", fontsize=9)

    save(fig, "fig_decoy.pdf")
    if floor is not None:
        print(f"    decoys: {cos[0]:.3f} -> {cos[-1]:.3f} at r={r[-1]} "
              f"({kbytes[-1]:.1f} KB/query); floor is {floor:.3f}")
    return True


# --------------------------------------------------------------------------
#  F7 -- the parameter sweep (task 4.1), and the theorem it confirms.
#
#  The left panel is the cost model: rounds rise linearly in d and in ell, and
#  are FLAT in m. The right panel is why that matters -- quadrupling the number
#  of users quadruples the input matrix while moving the byte count by a few
#  percent, because communication tracks the intermediate VECTORS and not the
#  matrix. That is NUDGE Thm 4.2, measured rather than cited.
# --------------------------------------------------------------------------
def fig_sweep(rows):
    train = [r for r in rows
             if r.get("op") == "approxfactor" and r.get("phase") == "matvec"]
    if not train:
        print("  SKIP fig_sweep: no sweep rows (run mingw32-make bench)")
        return False

    def series(axis, key):
        pts = collections.defaultdict(list)
        for r in train:
            if r.get("axis") == axis and r.get("normalizer", "").startswith("reveal"):
                pts[r[key]].append(r)
        out = []
        for v in sorted(pts):
            g = pts[v]
            out.append((v, g[0]["rounds"], g[0]["bytes_sent"],
                        float(np.median([x["wall_ms"] for x in g]))))
        return out

    d_s, ell_s, m_s = series("d", "d"), series("ell", "ell"), series("m", "m")

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.5))

    for pts, lab, colour, mark in ((d_s, "vary $d$ (components)", C_ATTACK, "o"),
                                   (ell_s, r"vary $\ell$ (iterations)", C_ALT, "s"),
                                   (m_s, "vary $m$ (users)", C_CONTROL, "^")):
        if not pts:
            continue
        x = [p[0] for p in pts]
        y = [p[1] for p in pts]
        ax1.plot(x, y, color=colour, marker=mark, ms=4, label=lab)

    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.set_xlabel("parameter value")
    ax1.set_ylabel("communication rounds")
    ax1.set_title(r"Rounds rise in $d$ and $\ell$, and are flat in $m$",
                  fontsize=9)
    ax1.legend(fontsize=7.5, loc="upper left")

    # The theorem, made visible: matrix entries against bytes.
    if m_s:
        n = 1682
        entries = [p[0] * n for p in m_s]
        mb = [p[2] / 1e6 for p in m_s]
        ax2.plot(entries, mb, color=C_CONTROL, marker="^", ms=6)
        for i, (e, y, p) in enumerate(zip(entries, mb, m_s)):
            last = (i == len(entries) - 1)   # keep the rightmost label on-axes
            ax2.annotate(f"m={p[0]}", xy=(e, y),
                         xytext=(-4 if last else 4, -10),
                         ha="right" if last else "left",
                         textcoords="offset points", fontsize=7.5)
        grow_x = entries[-1] / entries[0]
        grow_y = mb[-1] / mb[0]
        ax2.set_xlabel("input matrix entries ($m \\times n$)")
        ax2.set_ylabel("communication (MB)")
        ax2.set_ylim(0, max(mb) * 1.35)
        ax2.set_title(f"Matrix grows {grow_x:.0f}$\\times$, "
                      f"traffic grows {100*(grow_y-1):.0f}%", fontsize=9)
        ax2.annotate("communication tracks the intermediate\n"
                     "vectors, not the input matrix (Thm 4.2)",
                     xy=(0.5, 0.12), xycoords="axes fraction", ha="center",
                     fontsize=7.5, color="#555555")

    save(fig, "fig_sweep.pdf")
    # grow_x/grow_y are defined inside `if m_s:` above, so guard the report
    # rather than raising NameError on a run where the m axis is absent.
    if m_s:
        print(f"    sweep: matrix x{grow_x:.0f} -> bytes x{grow_y:.3f}, "
              f"rounds unchanged at {m_s[0][1]}")
    return True


def fig_stages(rows):
    """F9, task 4.3: where the rounds and the milliseconds actually go.

    Two panels that DISAGREE, which is the result. Rounds are what a WAN
    charges for; milliseconds are what a local run charges for; the phase that
    dominates one does not dominate the other.
    """
    attrib = [r for r in rows if r.get("op") == "attrib_analytic"]
    micro = [r for r in rows if r.get("op", "").startswith(
        ("matvec_", "truncate_", "normalize_", "msnzb_", "topk_", "score_",
         "pir_"))]
    if not attrib:
        print("  SKIP fig_stages: no attribution rows (run mingw32-make bench)")
        return False

    # ---- panel A: the rounds budget, per configuration ------------------
    configs, seen = [], set()
    for r in attrib:
        key = (r["b"], r.get("normalizer"))
        if key not in seen:
            seen.add(key)
            configs.append(key)
    configs.sort()

    order = ["matvec", "truncate", "normalize", "fss", "net"]
    label = {"matvec": "matvec", "truncate": "truncate",
             "normalize": "normalize", "fss": "FSS gate", "net": "open $B$"}
    colour = {"matvec": C_CONTROL, "truncate": C_ATTACK, "normalize": C_ALT,
              "fss": "#762a83", "net": C_FLOOR}

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.4, 3.8))
    # The right panel's op names are long and sit on its left edge, so give the
    # two panels real space between them rather than letting them collide.
    fig.subplots_adjust(wspace=0.42)

    names = []
    present = set()          # phases that actually appear in SOME config
    for i, (b, norm) in enumerate(configs):
        share = {}
        for r in attrib:
            if r["b"] == b and r.get("normalizer") == norm:
                share[r["phase"]] = r["share_rounds"]
        left = 0.0
        for ph in order:
            v = share.get(ph, 0.0)
            if v <= 0:
                continue
            ax1.barh(i, v, left=left, color=colour[ph], edgecolor="white",
                     linewidth=0.6)
            present.add(ph)
            if v >= 7:
                ax1.text(left + v / 2, i, f"{v:.0f}%", ha="center",
                         va="center", fontsize=7.5, color="white")
            left += v
        names.append(f"b={b}\n{norm}")

    ax1.set_yticks(range(len(configs)))
    ax1.set_yticklabels(names, fontsize=7.5)
    ax1.set_xlabel("share of communication rounds (%)")
    ax1.set_xlim(0, 100)
    ax1.set_title("Truncation dominates, in every configuration", fontsize=9)
    ax1.grid(axis="y", visible=False)
    # Build the legend from every phase present across ALL configurations. The
    # FSS gate only appears in the fss row, so labelling from the first bar
    # alone silently dropped it from the key.
    handles = [mpatches.Patch(color=colour[ph], label=label[ph])
               for ph in order if ph in present]
    ax1.legend(handles=handles, fontsize=7.5, ncol=5, loc="upper center",
               bbox_to_anchor=(0.5, -0.22), columnspacing=1.1,
               handlelength=1.2)

    # ---- panel B: measured per-call wall time, standalone ----------------
    if micro:
        by_op = collections.defaultdict(list)
        for r in micro:
            by_op[r["op"]].append(r["wall_ms"])
        ops = sorted(by_op, key=lambda o: -float(np.median(by_op[o])))
        vals = [float(np.median(by_op[o])) for o in ops]
        cols = []
        for o in ops:
            if o.startswith("matvec"):
                cols.append(C_CONTROL)
            elif o.startswith("truncate"):
                cols.append(C_ATTACK)
            elif o.startswith("normalize"):
                cols.append(C_ALT)
            elif o.startswith("msnzb"):
                cols.append("#762a83")
            else:
                cols.append(C_FLOOR)
        ax2.barh(range(len(ops)), vals, color=cols)
        ax2.set_yticks(range(len(ops)))
        ax2.set_yticklabels(ops, fontsize=7)
        ax2.set_xscale("log")
        ax2.set_xlabel("median wall time per call (ms), standalone")
        ax2.set_title("CPU ranks them differently from rounds", fontsize=9)
        ax2.grid(axis="y", visible=False)
        ax2.invert_yaxis()

    fig.text(0.5, -0.16,
             "Left: rounds, which is what a WAN charges for. Right: CPU, "
             "measured out of the training loop's cache context.",
             ha="center", fontsize=7.5, color="#555555")

    save(fig, "fig_stages.pdf")

    resid = {(r["b"], r.get("normalizer")): r.get("residual_rounds")
             for r in attrib}
    bad = [k for k, v in resid.items() if v not in (0, None)]
    for (b, norm) in configs:
        tr = next((r for r in attrib
                   if r["b"] == b and r.get("normalizer") == norm
                   and r["phase"] == "truncate"), None)
        fs = next((r for r in attrib
                   if r["b"] == b and r.get("normalizer") == norm
                   and r["phase"] == "fss"), None)
        if tr:
            print(f"    b={b} {norm}: truncate {tr['share_rounds']:.1f}% of "
                  f"rounds, {tr['share_bytes']:.1f}% of bytes" +
                  (f"; FSS gate {fs['share_rounds']:.1f}%" if fs and
                   fs["share_rounds"] > 0 else ""))
    print("    residual: " + ("ALL ZERO -- the breakdown is complete"
                              if not bad else f"NON-ZERO for {bad}"))
    return True

def fig_compose(rows, quality):
    """F8, task 4.2: the cost of each half of the composition, isolated.

    Two panels, and the second one is deliberately unflattering. Showing only
    the first would be advocacy: it says private delivery is nearly free, which
    is true per byte and false per round trip.
    """
    net = [r for r in rows if r.get("phase") == "net"
           and r.get("op") in ("compose", "compose_batched")]
    if not net:
        print("  SKIP fig_compose: no composed rows (run mingw32-make bench)")
        return False

    arms = ["B1", "B2", "B3", "FULL", "B5"]
    pretty = {"B1": "B1\ncleartext\n+ fetch",
              "B2": "B2\ncleartext\n+ PIR",
              "B3": "B3\nprivate\n+ fetch",
              "FULL": "FULL\nprivate\n+ PIR",
              "B5": "B5\ncleartext\n+ download"}
    colour = {"B1": C_FLOOR, "B2": C_ALT, "B3": C_ALT, "FULL": C_ATTACK,
              "B5": C_CONTROL}

    def pick(arm, prof, op):
        for r in net:
            if (r.get("config") == arm and r.get("profile") == prof
                    and r.get("op") == op):
                return r
        return None

    # nDCG@20 for the two distinct models: oracle (B1/B2/B5), private (B3/FULL).
    q = {"oracle": None, "private": None}
    for r in quality:
        if r.get("ell") == 10 and r.get("d") == 16 and r.get("b") == 64:
            q["oracle"] = r.get("ndcg_at_20_oracle")
            q["private"] = r.get("ndcg_at_20_private")

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.6, 3.9))
    fig.subplots_adjust(wspace=0.28)

    # ---- panel A: the isolation -- what each half ADDS over B1 ----------
    #
    # The task's own words are "the cost of each half, isolated", so plot the
    # increment over B1 rather than five totals a reader has to subtract by
    # eye. B1 is the floor and sits at zero by construction.
    x = np.arange(len(arms))
    width = 0.38
    base = {}
    for prof in ("local", "wan_a"):
        r = pick("B1", prof, "compose")
        base[prof] = r["wall_ms"] if r else 0.0

    for off, prof, alpha, lab in ((-width / 2, "local", 1.0, "local"),
                                  (width / 2, "wan_a", 0.55, "wan_a")):
        vals = []
        for a in arms:
            r = pick(a, prof, "compose")
            vals.append((r["wall_ms"] - base[prof]) if r else 0.0)
        bars = ax1.bar(x + off, vals, width,
                       color=[colour[a] for a in arms], alpha=alpha,
                       edgecolor="white", linewidth=0.5, label=lab)
        for b, v in zip(bars, vals):
            if abs(v) < 0.05:
                continue          # rounding noise; "-0.0" is not a measurement
            ax1.annotate(f"{v:+.0f}" if abs(v) >= 10 else f"{v:+.1f}",
                         xy=(b.get_x() + b.get_width() / 2, v),
                         xytext=(0, 3 if v >= 0 else -11),
                         textcoords="offset points", ha="center", fontsize=6.5)

    ax1.axhline(0, color="k", lw=0.8)
    ax1.set_xticks(x)
    ax1.set_xticklabels([pretty[a] for a in arms], fontsize=7)
    ax1.set_ylabel("ms per session, relative to B1")
    ax1.set_title("What each half costs, isolated", fontsize=9)
    ax1.grid(axis="x", visible=False)
    ax1.legend(fontsize=7.5, loc="upper left", title="profile",
               title_fontsize=7.5)

    # Quality belongs on this panel: without it a reader concludes B5 is the
    # right answer, when B5 has B1's exact quality AND the lowest latency and
    # is rejected on bandwidth alone.
    qline = []
    for a in arms:
        src = "private" if a in ("B3", "FULL") else "oracle"
        qline.append("--" if q.get(src) is None else f"{q[src]:.3f}")
    ax1.set_xlabel("nDCG@20:   " + "      ".join(qline), fontsize=7,
                   color="#555555")

    # ---- panel B: wan_a, as built against what batching would give ------
    built, batched = [], []
    for a in arms:
        r = pick(a, "wan_a", "compose")
        b = pick(a, "wan_a", "compose_batched")
        built.append(r["wall_ms"] if r else 0.0)
        batched.append(b["wall_ms"] if b else (r["wall_ms"] if r else 0.0))

    ax2.bar(x - width / 2, built, width, color=[colour[a] for a in arms],
            label="as built: $k$ sequential round trips")
    ax2.bar(x + width / 2, batched, width, color=[colour[a] for a in arms],
            alpha=0.45, hatch="//", edgecolor="white",
            label="batched into 1 round trip (not implemented)")

    b5 = built[arms.index("B5")]
    ax2.axhline(b5, color=C_CONTROL, ls="--", lw=1.0)
    ax2.text(len(arms) - 0.4, b5 * 1.12, "B5 full download", fontsize=7,
             color=C_CONTROL, ha="right")

    ax2.set_xticks(x)
    ax2.set_xticklabels(arms, fontsize=8)
    ax2.set_ylabel("ms per user-session on wan_a")
    ax2.set_title("On a WAN, round trips decide it -- not bytes", fontsize=9)
    ax2.grid(axis="x", visible=False)
    ax2.legend(fontsize=7, loc="upper left")

    fig.text(0.5, -0.10,
             "wan_a = 30 ms RTT, 100 Mbit/s. Channel-level emulation, not "
             "netem. Training amortised over m = 943 users per refresh.",
             ha="center", fontsize=7.5, color="#555555")

    save(fig, "fig_compose.pdf")

    b1 = pick("B1", "wan_a", "compose")
    b2 = pick("B2", "wan_a", "compose")
    b3 = pick("B3", "wan_a", "compose")
    fu = pick("FULL", "wan_a", "compose")
    if b1 and b2 and b3 and fu:
        print(f"    wan_a: private delivery costs {b2['wall_ms']-b1['wall_ms']:+.2f} ms, "
              f"private training {b3['wall_ms']-b1['wall_ms']:+.2f} ms, "
              f"both {fu['wall_ms']-b1['wall_ms']:+.2f} ms")
        print(f"    B5 (full download, 1 round trip) = {b5:.2f} ms, "
              f"i.e. {b2['wall_ms']/b5:.1f}x FASTER than DPF-PIR as built")
    return True


def main():
    print("regenerating figures from bench/results/")
    leak = newest_sha(load("leakage_attack.jsonl"), "leakage_attack")
    dpf = newest_sha(load("bench_dpf.jsonl"), "bench_dpf")
    base = newest_sha(load("bench_baseline.jsonl"), "bench_baseline")
    dist = newest_sha(load("distinguisher_result.jsonl"), "distinguisher")
    sweep = newest_sha(load("bench_sweep.jsonl"), "bench_sweep")
    stages = newest_sha(load("bench_stages.jsonl"), "bench_stages")
    comp = newest_sha(load("bench_compose.jsonl"), "bench_compose")
    qual = newest_sha(load("train_quality.jsonl"), "train_quality")
    ell = load("oracle_ell_sweep.jsonl")     # one row per ell, no sha filter

    made = 0
    made += bool(fig_attack(leak))
    made += bool(fig_keysize())
    made += bool(fig_ell(ell))
    made += bool(fig_pir_cost(base))
    made += bool(fig_distinguisher(dist))
    made += bool(fig_decoy(leak))
    made += bool(fig_sweep(sweep))
    made += bool(fig_stages(stages))
    made += bool(fig_compose(comp, qual))

    print(f"\n{made} figure(s) written to {FIGDIR}/")
    if made == 0:
        print("no figures produced; that is a failure, not an empty run")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
