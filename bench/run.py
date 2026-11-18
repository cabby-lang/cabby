#!/usr/bin/env python3
"""Cabby benchmarks: speed and memory against Python, C and Rust.

    python3 bench/run.py                 # everything (a few minutes)
    python3 bench/run.py --quick         # one run each, for a quick look
    python3 bench/run.py --tier interp   # only the interpreter against Python
    python3 bench/run.py --tier compiled # only compiled Cabby against C and Rust
    python3 bench/run.py fib sort        # only some programs

Each program in bench/programs/ is written once per language, with the same
algorithm, plainly. (The Python versions keep their work inside a main()
function, as real Python programs do: Python runs code in functions faster.) @N@ in the source is replaced by the problem size (and
@N1@ by size + 1), so every language gets the same fixed size. Before
anything is timed, every language's output is checked to be identical.

Time is wall-clock time, the best of several runs (the least disturbed by
whatever else the computer is doing). Memory is the peak resident memory:
the most RAM the program held at any moment, including the runtime itself
(Python's interpreter, Cabby's interpreter).

Results are printed, and a full run (all programs, both tiers, without
--quick) writes them to bench/results.md.
"""

import argparse
import os
import platform
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROGRAMS = os.path.join(HERE, "programs")
BUILD = os.path.join(HERE, "build")

CABBY = os.environ.get("CABBY", os.path.join(ROOT, "cabby"))
CC = os.environ.get("CC", "gcc")
PYTHON = os.environ.get("PYTHON", sys.executable)
RUSTC = os.environ.get("RUSTC", "rustc")

# name: (what it measures, size for the interpreter tier, size for the compiled tier)
BENCHMARKS = {
    "hello":      ("start-up and baseline memory",        1,         1),
    "fib":        ("function calls (recursive fib)",      30,        38),
    "sieve":      ("loops and arrays (primes)",           2_000_000, 50_000_000),
    "mandelbrot": ("floating point",                      250,       2_500),
    "sort":       ("quicksort, arrays passed by borrow",  300_000,   10_000_000),
    "strings":    ("building and reading text",           300_000,   10_000_000),
    "objects":    ("making objects, virtual calls",       300_000,   50_000_000),
    "text":       ("string tools: trim, find, slice, replace", 100_000, 3_000_000),
    "lists":      ("lists: add, go through, find, split and join", 300_000, 10_000_000),
    "lookups":    ("lookups: count by key, go through, remove", 300_000, 10_000_000),
}

TIERS = {
    "interp":   ("Interpreted: Cabby against Python", ["cabby", "python"]),
    "compiled": ("Compiled: Cabby against C and Rust", ["cabby -c", "c", "rust"]),
}


def tool_version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True).stdout.strip()
        return out.splitlines()[0] if out else "?"
    except OSError:
        return "not found"


def source_for(prog, ext, size):
    with open(os.path.join(PROGRAMS, f"{prog}.{ext}")) as f:
        text = f.read()
    return text.replace("@N1@", str(size + 1)).replace("@N@", str(size))


def prepare(prog, lang, size, tier):
    """Write and build one program; return the command that runs it."""
    d = os.path.join(BUILD, tier, prog)
    os.makedirs(d, exist_ok=True)
    if lang == "python":
        path = os.path.join(d, f"{prog}.py")
        open(path, "w").write(source_for(prog, "py", size))
        return [PYTHON, path]
    if lang in ("cabby", "cabby -c"):
        path = os.path.join(d, f"{prog}.cby")
        open(path, "w").write(source_for(prog, "cby", size))
        if lang == "cabby":
            return [CABBY, path]
        exe = os.path.join(d, f"{prog}-cabby")
        subprocess.run([CABBY, "-c", path, "-o", exe], check=True)
        return [exe]
    if lang == "c":
        path = os.path.join(d, f"{prog}.c")
        open(path, "w").write(source_for(prog, "c", size))
        exe = os.path.join(d, f"{prog}-c")
        subprocess.run([CC, "-O2", "-o", exe, path], check=True)
        return [exe]
    if lang == "rust":
        path = os.path.join(d, f"{prog}.rs")
        open(path, "w").write(source_for(prog, "rs", size))
        exe = os.path.join(d, f"{prog}-rust")
        subprocess.run([RUSTC, "-O", "-o", exe, path], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return [exe]
    raise ValueError(lang)


MEMTIME = os.path.join(BUILD, "memtime")


def build_memtime():
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run([CC, "-O2", "-o", MEMTIME, os.path.join(HERE, "memtime.c")], check=True)


def measure(cmd):
    """Run once: (seconds, peak memory in KB, output). The small memtime
    launcher starts the program, so none of this script's own memory is
    counted as the program's."""
    with tempfile.NamedTemporaryFile() as out:
        p = subprocess.run([MEMTIME, out.name] + cmd, capture_output=True, text=True)
        lines = p.stderr.strip().splitlines()
        if p.returncode != 0 or not lines:
            raise RuntimeError(f"{' '.join(cmd)} failed:\n{p.stderr}")
        secs, kb = lines[-1].split()
        with open(out.name) as f:
            return float(secs), int(kb), f.read().strip()


def fmt_time(s):
    if s < 0.01:
        return f"{s * 1000:.1f} ms"
    if s < 1:
        return f"{s * 1000:.0f} ms"
    return f"{s:.2f} s"


def fmt_mem(kb):
    return f"{kb / 1024:.1f} MB"


def fmt_ratio(r):
    if r >= 10:
        return f"{r:.0f}×"
    return f"{r:.2f}×"


def main():
    ap = argparse.ArgumentParser(description="Cabby speed and memory benchmarks")
    ap.add_argument("programs", nargs="*", help="only these programs")
    ap.add_argument("--tier", choices=["interp", "compiled", "all"], default="all")
    ap.add_argument("--runs", type=int, default=5, help="runs per measurement (best is kept)")
    ap.add_argument("--quick", action="store_true", help="one run each")
    args = ap.parse_args()
    runs = 1 if args.quick else args.runs
    progs = args.programs or list(BENCHMARKS)
    for p in progs:
        if p not in BENCHMARKS:
            sys.exit(f"unknown program '{p}'; choose from {', '.join(BENCHMARKS)}")
    tiers = ["interp", "compiled"] if args.tier == "all" else [args.tier]

    if not os.path.exists(CABBY):
        sys.exit(f"cabby not found at {CABBY}: run `make` first, or set CABBY=")
    if os.path.isdir(BUILD):
        shutil.rmtree(BUILD)
    build_memtime()

    machine = [
        f"* Computer: {platform.processor() or platform.machine()}, "
        f"{os.cpu_count()} cores, {platform.system()} {platform.release()}",
        f"* Python: {tool_version([PYTHON, '--version'])}",
        f"* C: {tool_version([CC, '--version'])}, `-O2`",
        f"* Rust: {tool_version([RUSTC, '--version'])}, `-O`",
        f"* Cabby: compiled with `cabby -c` (`-O2`), "
        f"{tool_version([os.environ.get('CABBY_CC', 'clang'), '--version'])}",
        f"* Best of {runs} run{'s' if runs > 1 else ''}; memory is peak resident memory.",
    ]
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    machine[0] = (f"* Computer: {line.split(':', 1)[1].strip()}, "
                                  f"{os.cpu_count()} cores, {platform.system()} {platform.release()}")
                    break
    except OSError:
        pass

    report = ["# Cabby benchmark results", "", *machine, ""]
    print("\n".join(machine))

    missing = []
    if not shutil.which(RUSTC):
        missing.append("rust")
        print("(rustc not found: Rust is left out. Install it with `sudo pacman -S rust`.)")
    for tier in tiers:
        title, langs = TIERS[tier]
        langs = [l for l in langs if l not in missing]
        idx = 1 if tier == "interp" else 2
        print(f"\n== {title}")
        report += [f"## {title}", ""]
        head = ["Program", "Size"] + [f"{l} time" for l in langs] + [f"{l} memory" for l in langs]
        rows = []
        for prog in progs:
            what, *sizes = BENCHMARKS[prog]
            size = sizes[idx - 1]
            cmds = {l: prepare(prog, l, size, tier) for l in langs}
            results = {}
            for l in langs:
                best = None
                for _ in range(runs):
                    t, mem, out = measure(cmds[l])
                    if best is None or t < best[0]:
                        best = (t, mem, out)
                results[l] = best
            outs = {results[l][2] for l in langs}
            if len(outs) != 1:
                sys.exit(f"{prog}: the languages disagree:\n" +
                         "\n".join(f"  {l}: {results[l][2]}" for l in langs))
            size_text = "" if prog == "hello" else f"{size:,}"
            row = [prog, size_text] + [fmt_time(results[l][0]) for l in langs] \
                + [fmt_mem(results[l][1]) for l in langs]
            rows.append((prog, what, results, row))
            print(f"  {prog:<11} {size_text:>11}  " +
                  "  ".join(f"{l}: {fmt_time(results[l][0]):>8} {fmt_mem(results[l][1]):>9}" for l in langs))

        # Times and memory side by side, then ratios against the reference.
        report.append("| " + " | ".join(head) + " |")
        report.append("|" + "---|" * len(head))
        for _, _, _, row in rows:
            report.append("| " + " | ".join(row) + " |")
        report.append("")
        ref = "python" if tier == "interp" else "c"
        others = [l for l in langs if l != ref]
        report.append(f"Relative to {'Python' if ref == 'python' else 'C'} "
                      "(below 1× is faster or smaller, above is slower or bigger):")
        report.append("")
        rh = ["Program", "What it measures"] + [f"{l} time" for l in others] + [f"{l} memory" for l in others]
        report.append("| " + " | ".join(rh) + " |")
        report.append("|" + "---|" * len(rh))
        for prog, what, res, _ in rows:
            cells = [prog, what]
            cells += [fmt_ratio(res[l][0] / res[ref][0]) for l in others]
            cells += [fmt_ratio(res[l][1] / res[ref][1]) for l in others]
            report.append("| " + " | ".join(cells) + " |")
        report.append("")

    # Only a full run replaces the saved results.
    if args.quick or args.programs or args.tier != "all":
        print("\n(a partial run: bench/results.md is kept as it was)")
        return
    with open(os.path.join(HERE, "results.md"), "w") as f:
        f.write("\n".join(report))
    print(f"\nwritten to {os.path.relpath(os.path.join(HERE, 'results.md'))}")


if __name__ == "__main__":
    main()
