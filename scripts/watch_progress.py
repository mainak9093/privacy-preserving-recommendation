"""Live progress for a long `mingw32-make reproduce` run.

WHY THIS EXISTS. `reproduce` is the D8 artefact: one command that rebuilds
every figure and table from source. It also takes tens of minutes, and most of
that time it prints nothing -- the 18 training runs redirect to /dev/null, so a
silent log is indistinguishable from a hung one. This turns the log into a bar,
a percentage and an estimated finish time.

It reads the log; it does not run anything. So it is safe to start, stop and
restart mid-run, and it adds no load to a machine that is already busy.

    py -3.13 scripts/watch_progress.py path/to/reproduce.log

    mingw32-make reproduce > run.log 2>&1 &
    py -3.13 scripts/watch_progress.py run.log

HOW THE ESTIMATE WORKS, AND WHERE IT IS WEAK. Each step carries a nominal
weight, measured on the development box. Progress is the cumulative weight of
finished steps plus the fraction of the current one. The ETA does NOT use the
nominal total directly -- it scales the remaining weight by the pace this run
has actually achieved so far, so a slower or faster machine self-corrects. It
is rough in the first minute, when there is little pace to measure, and tightens
after that. It is an estimate and is labelled as one.

The bench step gets finer granularity because it is the longest: `make bench`
prints one `--- name` line per binary, so the fraction within that step is
real rather than interpolated.
"""
import os
import re
import sys
import time

# Nominal seconds per step on the development box. Only the RATIOS matter --
# the absolute scale is re-derived from the run's own pace. Update these if the
# pipeline changes shape; a stale weight makes the bar uneven, not wrong.
STEPS = [
    (1, "fetching data", 2),
    (2, "building", 70),
    (3, "tests", 15),
    (4, "cleartext oracle + ring export", 25),
    (5, "private training, three configurations", 150),
    (6, "the composition demo", 8),
    (7, "DP and poisoning studies", 150),
    (8, "benchmarks and baselines", 640),
    (9, "figures", 25),
]
TOTAL_W = sum(w for _, _, w in STEPS)

STEP_RE = re.compile(r"^== (\d+)/(\d+) (.*)")
BENCH_RE = re.compile(r"^--- (bench_\w+)")
EXIT_RE = re.compile(r"^EXIT=(\d+)")

BAR_W = 42


# The bench step is the longest, and its binaries are nowhere near equal --
# bench_sweep alone is most of it. Counting them equally makes the bar race to
# 90% and then sit there, which is the failure mode this script exists to fix.
# Nominal seconds, same convention as STEPS: only the ratios matter.
BENCH_W = {
    "bench_baseline": 20,
    "bench_compose": 180,
    "bench_dos": 30,
    "bench_dpf": 100,
    "bench_stages": 60,
    "bench_sweep": 390,
    "bench_timing": 5,
}
BENCH_DEFAULT = 40          # for a binary this table has not met yet


def bench_names(repo_root):
    """Which bench binaries `make bench` will run, for sub-progress.

    Read from the repository this script lives in. A CLONE being watched may
    have a different set -- an older commit, say -- so unknown names seen in
    the log are folded in as they appear, and the weight below degrades to a
    default rather than guessing wrong.
    """
    d = os.path.join(repo_root, "bench")
    try:
        return sorted(f[:-4] for f in os.listdir(d)
                      if f.startswith("bench_") and f.endswith(".cpp"))
    except OSError:
        return sorted(BENCH_W)


def parse(path):
    """Current step, sub-progress, last interesting line, exit code."""
    step, label, nsteps = 0, "starting", len(STEPS)
    benches, exit_code, last = set(), None, ""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.rstrip("\n")
                m = STEP_RE.match(line)
                if m:
                    step, nsteps, label = int(m.group(1)), int(m.group(2)), \
                        m.group(3)
                    continue
                m = BENCH_RE.match(line)
                if m:
                    benches.add(m.group(1))
                    continue
                m = EXIT_RE.match(line)
                if m:
                    exit_code = int(m.group(1))
                    continue
                if line.strip():
                    last = line.strip()
    except FileNotFoundError:
        return None
    return step, nsteps, label, benches, exit_code, last


def bench_fraction(seen, expected):
    """Weighted fraction of the bench step that is finished.

    A name only appears in the log when its binary STARTS, so the newest one
    is running rather than done. It is credited half, which is the same
    convention the other steps use.
    """
    pool = list(dict.fromkeys(list(expected) + sorted(seen)))
    total = sum(BENCH_W.get(b, BENCH_DEFAULT) for b in pool)
    if not total:
        return 0.0
    order = [b for b in pool if b in seen]
    done = 0.0
    for i, b in enumerate(order):
        w = BENCH_W.get(b, BENCH_DEFAULT)
        done += w if i < len(order) - 1 else w * 0.5
    return min(done / total, 1.0)


def progress_fraction(step, benches, expected):
    """Cumulative weight done, in [0, 1]."""
    if step <= 0:
        return 0.0
    done = sum(w for i, _, w in STEPS if i < step)
    cur = next((w for i, _, w in STEPS if i == step), 0)
    # Inside the bench step the sub-progress is real, not interpolated.
    if step == 8:
        done += cur * bench_fraction(benches, expected)
    else:
        # No information inside other steps, so credit half of the current one.
        # Crediting zero makes the bar stall; crediting all makes it lie.
        done += cur * 0.5
    return min(done / TOTAL_W, 1.0)


def fmt_secs(s):
    if s is None or s < 0 or s != s:
        return "--:--"
    s = int(s)
    if s >= 3600:
        return f"{s//3600}h{(s%3600)//60:02d}m"
    return f"{s//60:02d}:{s%60:02d}"


def render(frac, step, nsteps, label, elapsed, eta, last, done_code,
           expected, benches):
    filled = int(round(BAR_W * frac))
    bar = "#" * filled + "." * (BAR_W - filled)

    if done_code is not None:
        head = "DONE" if done_code == 0 else f"FAILED (exit {done_code})"
        lines = [
            f"  [{bar}] {frac*100:5.1f}%",
            f"  {head}   total {fmt_secs(elapsed)}",
            "",
        ]
    else:
        finish = time.strftime("%H:%M:%S", time.localtime(time.time() + eta)) \
            if eta is not None else "--:--:--"
        sub = ""
        if step == 8:
            n = len(set(list(expected) + sorted(benches)))
            running = sorted(benches)[-1] if benches else "?"
            sub = f"  ({len(benches)}/{n}, running {running})"
        lines = [
            f"  [{bar}] {frac*100:5.1f}%",
            f"  step {step}/{nsteps}: {label}{sub}",
            f"  elapsed {fmt_secs(elapsed)}   ETA ~{fmt_secs(eta)}   "
            f"free around {finish}",
        ]
    tail = last if len(last) <= 76 else last[:73] + "..."
    lines.append(f"  last: {tail}")
    return lines


def main(argv):
    path = argv[1] if len(argv) > 1 else os.path.join("bench", "reproduce.log")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    expected = bench_names(repo_root)

    if not os.path.exists(path):
        print(f"no log at {path}")
        print("run:  mingw32-make reproduce > run.log 2>&1 &")
        return 1

    start = os.stat(path).st_ctime
    tty = sys.stdout.isatty()
    printed = 0

    print(f"watching {path}   (Ctrl-C to stop; the run is unaffected)\n")

    while True:
        got = parse(path)
        if got is None:
            time.sleep(2)
            continue
        step, nsteps, label, benches, exit_code, last = got

        elapsed = time.time() - start
        frac = progress_fraction(step, benches, expected)
        # Scale the REMAINING time by the pace this run has actually shown,
        # rather than trusting the nominal total on an unknown machine.
        eta = (elapsed / frac) * (1.0 - frac) if frac > 0.02 else None

        lines = render(frac, step, nsteps, label, elapsed, eta, last,
                       exit_code, expected, benches)

        if tty:
            if printed:
                sys.stdout.write(f"\033[{printed}A")
            for ln in lines:
                sys.stdout.write("\033[2K" + ln + "\n")
            sys.stdout.flush()
            printed = len(lines)
        else:
            # Not a terminal (piped, or a CI log): append instead of redrawing,
            # so the output stays readable rather than filling with escapes.
            # flush explicitly -- Python block-buffers a pipe, and a progress
            # line that arrives after the run finishes is not progress.
            print(" | ".join(lines[:3]), flush=True)

        if exit_code is not None:
            return 0 if exit_code == 0 else 1
        time.sleep(2)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except KeyboardInterrupt:
        print("\nstopped watching; the run itself is untouched")
        sys.exit(130)
