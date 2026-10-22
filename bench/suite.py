#!/usr/bin/env python3
"""Tycho benchmark suite: sizes, tiers, warm-ups, and generated results.

The design mirrors the useful part of Goose's benchmark harness:

* one manifest owns workloads, sizes, tiers and source files;
* the size is substituted into a copy of each source, so every language runs
  the same algorithm at the same scale;
* Tycho and C are built by the same C backend;
* two warm-ups are discarded, then the best of three whole-process runs is
  reported;
* every implementation must print the same output for a workload/size;
* missing ports stay visible as missing, never as wins;
* `bench/RESULTS.md` is generated from saved JSON.

Run:
    python3 bench/suite.py
    python3 bench/suite.py --only latency,window --sizes large --reps 1
    python3 bench/suite.py --report-only
"""
import argparse
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from collections import OrderedDict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
GEN = HERE / "gen"
RESULTS_MD = HERE / "RESULTS.md"
RESULT_JSON = GEN / "results.json"
LANGS = ["tycho", "c", "rust", "go", "java", "swift"]
MEM_LANGS = ["c", "rust", "go"]
LABEL = {"tycho": "Tycho", "c": "C", "rust": "Rust",
         "go": "Go", "java": "Java", "swift": "Swift"}


def src(lang, path, pattern, repl):
    return {"lang": lang, "path": path, "pattern": pattern, "repl": repl}


def variant(label, tier, group, sources):
    return {"label": label, "tier": tier, "group": group, "sources": sources}


# Size values are chosen to be 0.25x / 1x / 4x around the established row
# sizes, except invindex where 900k keeps the largest run practical.
SIZES = {
    "latency": [("small", 500), ("medium", 2000), ("large", 8000)],
    "window": [("small", 500_000), ("medium", 2_000_000), ("large", 8_000_000)],
    "gcscan": [("small", 500_000), ("medium", 2_000_000), ("large", 8_000_000)],
    "winagg": [("small", 62_500), ("medium", 250_000), ("large", 1_000_000)],
    "invindex": [("small", 75_000), ("medium", 300_000), ("large", 900_000)],
    "binary": [("small", 14), ("medium", 18), ("large", 20)],
    "arr": [("small", 25_000), ("medium", 100_000), ("large", 400_000)],
    "dispatch": [("small", 500), ("medium", 2_000), ("large", 8_000)],
    "json": [("small", 12_500), ("medium", 50_000), ("large", 200_000)],
}

WORKLOADS = [
    {
        "name": "latency",
        "what": "Allocation churn: build and discard a fixed working set each round.",
        "variants": [
            variant("default", "idiomatic", "latency", [
                src("tycho", "bench/latency/latency.ty", r"(iters := )\d+", r"\g<1>{n}"),
                src("c", "bench/latency/latency.c", r"(long iters = )\d+", r"\g<1>{n}"),
                src("rust", "bench/latency/latency.rs", r"(let iters: i64 = )\d+", r"\g<1>{n}"),
                src("go", "bench/latency/latency.go", r"(const iters, k = )\d+", r"\g<1>{n}"),
                src("java", "bench/latency/latency.java", r"(int iters = )\d+", r"\g<1>{n}"),
                src("swift", "bench/latency/latency.swift", r"(let iters = )\d+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "window",
        "what": "Sliding-window eviction: heap strings and fixed-size ints.",
        "variants": [
            variant("string", "heap", "window_string", [
                src("tycho", "bench/window/window_naive.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/window/window.c", r"(long n = )\d+", r"\g<1>{n}"),
                src("rust", "bench/window/window.rs", r"(let n: \w+ = )[\d_]+", r"\g<1>{n}"),
                src("go", "bench/window/window.go", r"(const n, w = )\d+", r"\g<1>{n}"),
                src("java", "bench/window/window.java", r"(int n = )[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/window/window.swift", r"(let n = )[\d_]+", r"\g<1>{n}"),
            ]),
            variant("int", "fixed", "window_int", [
                src("tycho", "bench/window/window_int.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/window/window_int.c", r"(long n = )\d+", r"\g<1>{n}"),
                src("rust", "bench/window/window_int.rs", r"(let n: \w+ = )[\d_]+", r"\g<1>{n}"),
                src("go", "bench/window/window_int.go", r"(const n, w = )\d+", r"\g<1>{n}"),
                src("java", "bench/window/window_int.java", r"(int n = )[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/window/window_int.swift", r"(let n = )[\d_]+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "gcscan",
        "what": "Large retained string set plus transient-array churn.",
        "variants": [
            variant("default", "retained", "gcscan", [
                src("tycho", "bench/gcscan/gcscan.ty", r"(m := )\d+", r"\g<1>{n}"),
                src("c", "bench/gcscan/gcscan.c", r"(long m = )\d+", r"\g<1>{n}"),
                src("rust", "bench/gcscan/gcscan.rs", r"(let m: usize = )[\d_]+", r"\g<1>{n}"),
                src("go", "bench/gcscan/gcscan.go", r"(const m = )\d+", r"\g<1>{n}"),
                src("java", "bench/gcscan/gcscan.java", r"(int m = )[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/gcscan/gcscan.swift", r"(let m = )[\d_]+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "winagg",
        "what": "Windowed group-by: fresh map of growing lists per window.",
        "variants": [
            variant("default", "churn", "winagg", [
                src("tycho", "bench/winagg/winagg.ty", r"(\bw := )\d+", r"\g<1>{n}"),
                src("c", "bench/winagg/winagg.c", r"(long nw=40, w=)\d+", r"\g<1>{n}"),
                src("rust", "bench/winagg/winagg.rs", r"(const W: i64 = )\d+", r"\g<1>{n}"),
                src("go", "bench/winagg/winagg.go", r"(const nw, w, g = 40, )\d+", r"\g<1>{n}"),
                src("java", "bench/winagg/winagg.java", r"(int nw = 40, w = )\d+", r"\g<1>{n}"),
                src("swift", "bench/winagg/winagg.swift", r"(let w = )\d+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "invindex",
        "what": "Build-and-hold inverted index, side-array and map-native forms.",
        "variants": [
            variant("side-growth", "growth", "invindex_side", [
                src("tycho", "bench/invindex/invindex.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/invindex/invindex.c", r"(long n=)\d+", r"\g<1>{n}"),
                src("rust", "bench/invindex/invindex.rs", r"(let n: i64 = )[\d_]+", r"\g<1>{n}"),
                src("go", "bench/invindex/invindex.go", r"(var n, w, v int64 = )\d+", r"\g<1>{n}"),
                src("java", "bench/invindex/invindex.java", r"(int n = )[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/invindex/invindex.swift", r"(let n = )[\d_]+", r"\g<1>{n}"),
            ]),
            variant("side-count-fill", "expert", "invindex_side", [
                src("tycho", "bench/invindex/invindex_exact.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/invindex/invindex_exact.c", r"(long n=)\d+", r"\g<1>{n}"),
                src("rust", "bench/invindex/invindex_exact.rs", r"(let \(n, w, v\): \(i64, i64, i64\) = \()[\d_]+", r"\g<1>{n}"),
                src("go", "bench/invindex/invindex_exact.go", r"(const n, w, v = )\d+", r"\g<1>{n}"),
                src("java", "bench/invindex/invindex_exact.java", r"(int n=)[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/invindex/invindex_exact.swift", r"(let n=)[\d_]+", r"\g<1>{n}"),
            ]),
            variant("map-growth", "growth", "invindex_map", [
                src("tycho", "bench/invindex/invindex_map.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/invindex/invindex_map.c", r"(long n=)\d+", r"\g<1>{n}"),
                src("rust", "bench/invindex/invindex_map.rs", r"(let n: i64 = )[\d_]+", r"\g<1>{n}"),
                src("go", "bench/invindex/invindex_map.go", r"(const n, w, v = )\d+", r"\g<1>{n}"),
                src("java", "bench/invindex/invindex_map.java", r"(int n = )[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/invindex/invindex_map.swift", r"(let n = )[\d_]+", r"\g<1>{n}"),
            ]),
            variant("map-count-fill", "expert", "invindex_map", [
                src("tycho", "bench/invindex/invindex_map_exact.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/invindex/invindex_map_exact.c", r"(long n=)\d+", r"\g<1>{n}"),
                src("rust", "bench/invindex/invindex_map_exact.rs", r"(let \(n, w, v\): \(i64, i64, i64\) = \()[\d_]+", r"\g<1>{n}"),
                src("go", "bench/invindex/invindex_map_exact.go", r"(const n, w, v = )\d+", r"\g<1>{n}"),
                src("java", "bench/invindex/invindex_map_exact.java", r"(int n\s*=\s*)[\d_]+", r"\g<1>{n}"),
                src("swift", "bench/invindex/invindex_map_exact.swift", r"(let n\s*=\s*)[\d_]+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "binary",
        "what": "Computer Language Benchmarks Game binary trees: build, check, discard.",
        "variants": [
            variant("default", "idiomatic", "binary", [
                src("tycho", "bench/prongB/binary_trees.ty", r"(maxd := )\d+", r"\g<1>{n}"),
                src("c", "bench/prongB/binary_trees.c", r"(int mind = 4, maxd = )\d+", r"\g<1>{n}"),
                src("rust", "bench/prongB/binary_trees.rs", r"(let \(mind, maxd\) = \(4i64, )\d+", r"\g<1>{n}"),
                src("go", "bench/prongB/binary_trees.go", r"(mind, maxd := 4, )\d+", r"\g<1>{n}"),
                src("java", "bench/prongB/binary_trees.java", r"(int mind = 4, maxd = )\d+", r"\g<1>{n}"),
                src("swift", "bench/prongB/binary_trees.swift", r"(let maxd = )\d+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "arr",
        "what": "Flat array pipeline: repeated transform and sum passes.",
        "variants": [
            variant("default", "idiomatic", "arr", [
                src("tycho", "bench/prongB/arr_pipeline.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/prongB/arr_pipeline.c", r"(long n = )\d+", r"\g<1>{n}"),
                src("rust", "bench/prongB/arr_pipeline.rs", r"(let n: i64 = )\d+", r"\g<1>{n}"),
                src("go", "bench/prongB/arr_pipeline.go", r"(var n, m, k int64 = )\d+", r"\g<1>{n}"),
                src("java", "bench/prongB/arr_pipeline.java", r"(int n = )\d+", r"\g<1>{n}"),
                src("swift", "bench/prongB/arr_pipeline.swift", r"(let n = )[\d_]+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "dispatch",
        "what": "Array of callable records rebuilt and applied each pass.",
        "variants": [
            variant("default", "idiomatic", "dispatch", [
                src("tycho", "bench/prongB/dispatch.ty", r"(n := )\d+", r"\g<1>{n}"),
                src("c", "bench/prongB/dispatch.c", r"(long n = )\d+", r"\g<1>{n}"),
                src("rust", "bench/prongB/dispatch.rs", r"(let \(n,k\)=\()\d+", r"\g<1>{n}"),
                src("go", "bench/prongB/dispatch.go", r"(var n,k,total int64 = )\d+", r"\g<1>{n}"),
                src("java", "bench/prongB/dispatch.java", r"(int n = )\d+", r"\g<1>{n}"),
                src("swift", "bench/prongB/dispatch.swift", r"(let n = )\d+", r"\g<1>{n}"),
            ]),
        ],
    },
    {
        "name": "json",
        "what": "Recursive-descent JSON parse and walk over a generated document.",
        "generator": {
            "path": "bench/prongB/json_gen.ty",
            "pattern": r"(n := )\d+",
            "repl": r"\g<1>{n}",
        },
        "variants": [
            variant("default", "idiomatic", "json", [
                src("tycho", "bench/prongB/json_parse.ty", None, None),
                src("c", "bench/prongB/json_parse.c", None, None),
                src("rust", "bench/prongB/json_parse.rs", None, None),
                src("go", "bench/prongB/json_parse.go", None, None),
                src("java", "bench/prongB/json_parse.java", None, None),
                src("swift", "bench/prongB/json_parse.swift", None, None),
            ]),
        ],
    },
]


def run(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def tool(name):
    return shutil.which(name)


def rewrite(source, n, dst):
    text = Path(ROOT / source["path"]).read_text(encoding="utf-8")
    if source["pattern"] is not None:
        repl = source["repl"].format(n=n)
        text, count = re.subn(source["pattern"], repl, text, count=1)
        if count != 1:
            raise RuntimeError(f"{source['path']}: size pattern did not match once")
    dst.write_text(text, encoding="utf-8")


def compile_source(source, n, workdir, cc, tychoc):
    """Return (executable or command prefix, source-copy path, classdir)."""
    lang = source["lang"]
    srcpath = ROOT / source["path"]
    if not srcpath.exists():
        return None, "source missing"
    dst = workdir / srcpath.name
    rewrite(source, n, dst)
    exe = workdir / ("bench" + (".exe" if os.name == "nt" else ""))
    if lang == "tycho":
        if not tool(tychoc):
            return None, "tychoc missing"
        generated = workdir / "tycho_gen"
        p = run([tychoc, str(dst), "--emit-c", "-o", str(generated)])
        if p.returncode != 0:
            return None, p.stderr.strip().splitlines()[-1] if p.stderr.strip() else "tycho build failed"
        p = run([cc, "-O3", str(generated) + ".c", "-o", str(exe), "-lm"])
        return (exe, None) if p.returncode == 0 else (None, "cc failed")
    if lang == "c":
        p = run([cc, "-O3", str(dst), "-o", str(exe), "-lm"])
        return (exe, None) if p.returncode == 0 else (None, "cc failed")
    if lang == "rust":
        if not tool("rustc"):
            return None, "rustc missing"
        p = run([tool("rustc"), "-C", "opt-level=3", str(dst), "-o", str(exe)])
        return (exe, None) if p.returncode == 0 else (None, "rustc failed")
    if lang == "go":
        if not tool("go"):
            return None, "go missing"
        p = run([tool("go"), "build", "-o", str(exe), str(dst)])
        return (exe, None) if p.returncode == 0 else (None, "go failed")
    if lang == "java":
        if not tool("javac") or not tool("java"):
            return None, "java missing"
        classes = workdir / "classes"
        classes.mkdir()
        p = run([tool("javac"), "-d", str(classes), str(dst)])
        if p.returncode != 0:
            return None, "javac failed"
        return (("java", "-cp", str(classes), srcpath.stem), None)
    if lang == "swift":
        if not tool("swiftc"):
            return None, "swiftc missing"
        p = run([tool("swiftc"), "-O", str(dst), "-o", str(exe)])
        return (exe, None) if p.returncode == 0 else (None, "swiftc failed")
    return None, "unknown language"


def measure(peakrss, command, warmups, reps, stdin_path=None):
    if isinstance(command, (str, Path)):
        command = [str(command)]
    else:
        command = list(command)

    def invoke():
        if stdin_path is None:
            return run([str(peakrss)] + command)
        with open(stdin_path, "rb") as f:
            return subprocess.run([str(peakrss)] + command, cwd=ROOT,
                                  stdin=f, capture_output=True, text=True)

    for _ in range(warmups):
        p = invoke()
        if p.returncode != 0:
            return None, p.stderr.strip() or "warm-up failed"
    best = None
    max_rss = 0
    out = ""
    for _ in range(reps):
        p = invoke()
        if p.returncode != 0:
            return None, p.stderr.strip() or "run failed"
        lines = [x for x in p.stderr.strip().splitlines() if x.split()]
        if not lines:
            return None, "no peakrss measurement"
        try:
            rss_kb, ms = lines[-1].split()[-2:]
            rss_kb = int(rss_kb)
            ms = int(ms)
        except ValueError:
            return None, "invalid peakrss output"
        if platform.system() == "Darwin":
            rss_kb //= 1024
        best = ms if best is None else min(best, ms)
        max_rss = max(max_rss, rss_kb)
        out = p.stdout.strip()
    return {"ms": best, "rss": max_rss / 1024.0, "out": out}, None


def geomean(values):
    values = [v for v in values if v is not None and v > 0]
    if not values:
        return None
    return math.exp(sum(math.log(v) for v in values) / len(values))


def fmt(v, suffix=""):
    return "-" if v is None else f"{v:.2f}{suffix}"


def ratio(a, b):
    return None if a is None or b is None or b <= 0 else a / b


def best_in_group(rows, group, size, lang):
    vals = []
    for v in rows.values():
        if v["group"] != group:
            continue
        m = v["rows"].get(size, {}).get(lang)
        if m and m.get("ok"):
            vals.append(m["ms"])
    return min(vals) if vals else None


def best_mem_in_group(rows, group, size, lang):
    vals = []
    for v in rows.values():
        if v["group"] != group:
            continue
        m = v["rows"].get(size, {}).get(lang)
        if m and m.get("ok"):
            vals.append(m["rss"])
    return min(vals) if vals else None


def render(data):
    lines = []
    add = lines.append
    env = data["environment"]
    add("# Tycho benchmark suite")
    add("")
    add("<!-- generated by bench/suite.py; do not edit by hand -->")
    add("")
    add(f"Generated {data['generated']} with `{data['cc']}` for Tycho/C. "
        f"Best of {data['reps']} after {data['warmups']} discarded warm-ups; "
        "whole-process time, peak RSS.")
    add("")
    add("| | |")
    add("|---|---|")
    add(f"| CPU | {env['cpu']} |")
    add(f"| OS | {env['os']} |")
    add(f"| C backend | {data['cc']} |")
    add(f"| rustc | {env['rustc']} |")
    add(f"| Go | {env['go']} |")
    add(f"| Java | {env['java']} |")
    add(f"| Swift | {env['swift']} |")
    add("")
    add("`Y` means the row ran at every size; `-` is explicitly missing or "
        "did not build. A missing port is not a win.")
    add("")
    add("## Coverage")
    add("")
    add("| workload / variant | Tycho | C | Rust | Go | Java | Swift |")
    add("|---|---|---|---|---|---|---|")
    for wname, w in data["workloads"].items():
        for vlabel, v in w["variants"].items():
            cells = []
            for lang in LANGS:
                ok = all(v["rows"].get(s, {}).get(lang, {}).get("ok")
                         for s, _ in data["sizes"][wname])
                cells.append("Y" if ok else "-")
            add(f"| {wname} / {vlabel} | " + " | ".join(cells) + " |")
    add("")
    all_sizes = [s for rows in data["sizes"].values() for s, _ in rows]
    report_size = "large" if "large" in all_sizes else (all_sizes[-1] if all_sizes else "large")
    add(f"## Relative time at the {report_size} size")
    add("")
    add("Tycho / competitor; below 1.00 is faster. `best` picks the fastest "
        "Tycho variant and the fastest competitor variant in each group.")
    add("")
    add("| group | vs C | vs Rust | vs Go | vs Java | vs Swift |")
    add("|---|---:|---:|---:|---:|---:|")
    group_ratios = {lang: [] for lang in LANGS[1:]}
    for group in sorted({v["group"] for w in data["workloads"].values()
                         for v in w["variants"].values()}):
        tycho = best_in_group(best_all_rows(data), group, report_size, "tycho")
        cells = []
        for lang in LANGS[1:]:
            other = best_in_group(best_all_rows(data), group, report_size, lang)
            r = ratio(tycho, other)
            cells.append(fmt(r, "x"))
            if r is not None:
                group_ratios[lang].append(r)
        add(f"| {group} | " + " | ".join(cells) + " |")
    add("| **geometric mean** | " +
        " | ".join(fmt(geomean(group_ratios[l]), "x") for l in LANGS[1:]) + " |")
    add("")
    losses = []
    for group in sorted({v["group"] for w in data["workloads"].values()
                         for v in w["variants"].values()}):
        tycho = best_in_group(best_all_rows(data), group, report_size, "tycho")
        for lang in LANGS[1:]:
            other = best_in_group(best_all_rows(data), group, report_size, lang)
            r = ratio(tycho, other)
            if r is not None and r > 1.0:
                losses.append(f"{group} vs {LABEL[lang]} ({r:.2f}x)")
    add("Explicitly slower rows (Tycho / competitor > 1.00): " +
        (", ".join(losses) if losses else "none") + ".")
    add("")
    add(f"## Relative peak RSS at the {report_size} size")
    add("")
    add("Tycho / competitor; below 1.00 is smaller. Java/Swift are omitted: "
        "their RSS includes the runtime baseline. The smallest variant in "
        "each group is used independently for Tycho and each competitor.")
    add("")
    add("| group | vs C | vs Rust | vs Go |")
    add("|---|---:|---:|---:|")
    mem_ratios = {lang: [] for lang in MEM_LANGS}
    for group in sorted({v["group"] for w in data["workloads"].values()
                         for v in w["variants"].values()}):
        tycho = best_mem_in_group(best_all_rows(data), group, report_size, "tycho")
        cells = []
        for lang in MEM_LANGS:
            other = best_mem_in_group(best_all_rows(data), group, report_size, lang)
            r = ratio(tycho, other)
            cells.append(fmt(r, "x"))
            if r is not None:
                mem_ratios[lang].append(r)
        add(f"| {group} | " + " | ".join(cells) + " |")
    add("| **geometric mean** | " +
        " | ".join(fmt(geomean(mem_ratios[l]), "x") for l in MEM_LANGS) + " |")
    add("")
    add(f"## {report_size.capitalize()}-size absolute results")
    add("")
    add("Time is ms; RSS is MB. Java/Swift RSS includes the runtime baseline.")
    add("")
    add("| workload / variant | tier | lang | time | RSS |")
    add("|---|---|---|---:|---:|")
    for wname, w in data["workloads"].items():
        for vlabel, v in w["variants"].items():
            for lang in LANGS:
                m = v["rows"].get(report_size, {}).get(lang)
                if m and m.get("ok"):
                    add(f"| {wname} / {vlabel} | {v['tier']} | {LABEL[lang]} | "
                        f"{m['ms']} | {m['rss']:.1f} |")
                else:
                    add(f"| {wname} / {vlabel} | {v['tier']} | {LABEL[lang]} | - | - |")
    add("")
    add("## Scaling")
    add("")
    add("Each cell is best time in ms at small / medium / large. The full "
        "per-language data is in `bench/gen/results.json`.")
    add("")
    add("| workload / variant | Tycho | C | Rust | Go | Java | Swift |")
    add("|---|---|---|---|---|---|---|")
    for wname, w in data["workloads"].items():
        for vlabel, v in w["variants"].items():
            cells = []
            for lang in LANGS:
                vals = []
                for s, _ in data["sizes"][wname]:
                    m = v["rows"].get(s, {}).get(lang)
                    vals.append(str(m["ms"]) if m and m.get("ok") else "-")
                cells.append(" / ".join(vals))
            add(f"| {wname} / {vlabel} | " + " | ".join(cells) + " |")
    add("")
    add("Java/Swift memory is omitted from ratios; see the absolute table. "
        "Rows around the process floor are directional, not throughput claims.")
    return "\n".join(lines) + "\n"


def best_all_rows(data):
    out = {}
    for w in data["workloads"].values():
        for label, v in w["variants"].items():
            out[f"{w['name']}/{label}"] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma-separated workload names")
    ap.add_argument("--sizes", default="small,medium,large")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--report-only", action="store_true")
    args = ap.parse_args()
    wanted_sizes = [s for s in args.sizes.split(",") if s]
    if args.report_only:
        if not RESULT_JSON.exists():
            raise SystemExit(f"no saved results at {RESULT_JSON}")
        data = json.loads(RESULT_JSON.read_text(encoding="utf-8"))
        RESULTS_MD.write_text(render(data), encoding="utf-8")
        print(f"wrote {RESULTS_MD}")
        return 0
    if not tool(args.cc):
        raise SystemExit(f"C compiler not found: {args.cc}")
    tychoc = os.environ.get("TYCHOC", "./tychoc1")
    if not (ROOT / tychoc).exists() and not tool(tychoc):
        raise SystemExit(f"Tycho compiler not found: {tychoc}")
    only = [x for x in args.only.split(",") if x]
    selected = [w for w in WORKLOADS if not only or w["name"] in only]
    if not selected:
        raise SystemExit(f"no workloads matched --only {args.only}")
    GEN.mkdir(exist_ok=True)
    td = Path(tempfile.mkdtemp(prefix="tycho-suite-"))
    peakrss = td / "peakrss"
    p = run([args.cc, "-O2", str(HERE / "peakrss.c"), "-o", str(peakrss)])
    if p.returncode != 0:
        raise SystemExit("could not build peakrss")
    env = {
        "cpu": platform.processor() or platform.machine(),
        "os": f"{platform.system()} {platform.release()}",
        "rustc": run([tool("rustc"), "--version"]).stdout.strip() if tool("rustc") else "missing",
        "go": run([tool("go"), "version"]).stdout.strip() if tool("go") else "missing",
        "java": run([tool("java"), "-version"]).stderr.strip().splitlines()[0] if tool("java") else "missing",
        "swift": run([tool("swiftc"), "--version"]).stdout.strip().splitlines()[-1] if tool("swiftc") else "missing",
    }
    data = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "cc": args.cc,
        "reps": args.reps,
        "warmups": args.warmups,
        "environment": env,
        "sizes": {w["name"]: SIZES[w["name"]] for w in selected},
        "workloads": OrderedDict(),
        "mismatch": [],
        "failures": [],
    }
    for w in selected:
        wd = {"name": w["name"], "what": w["what"], "variants": OrderedDict()}
        for v in w["variants"]:
            vr = {"group": v["group"], "tier": v["tier"], "rows": OrderedDict()}
            for sname, n in SIZES[w["name"]]:
                if sname not in wanted_sizes:
                    continue
                stdin_path = None
                if "generator" in w:
                    g = w["generator"]
                    gdir = td / w["name"] / "generator" / sname
                    gdir.mkdir(parents=True)
                    gsrc = {"lang": "tycho", "path": g["path"],
                            "pattern": g["pattern"], "repl": g["repl"]}
                    gcmd, gerr = compile_source(gsrc, n, gdir, args.cc, tychoc)
                    if gcmd is None:
                        raise SystemExit(f"generator failed for {w['name']}/{sname}: {gerr}")
                    if isinstance(gcmd, (str, Path)):
                        gcmd = [str(gcmd)]
                    with open(gdir / "input.json", "wb") as f:
                        gp = subprocess.run(list(gcmd), cwd=ROOT, stdout=f,
                                            stderr=subprocess.PIPE, text=False)
                    if gp.returncode != 0:
                        raise SystemExit(f"generator run failed for {w['name']}/{sname}")
                    stdin_path = gdir / "input.json"
                vr["rows"][sname] = OrderedDict()
                for source in v["sources"]:
                    lang = source["lang"]
                    langdir = td / w["name"] / v["label"] / sname / lang
                    langdir.mkdir(parents=True)
                    command, err = compile_source(source, n, langdir, args.cc, tychoc)
                    if command is None:
                        vr["rows"][sname][lang] = {"ok": False, "note": err}
                        print(f"  {w['name']}/{v['label']}/{sname}/{lang}: {err}")
                        if err not in ("source missing", "rustc missing", "go missing",
                                       "java missing", "swift missing", "tychoc missing"):
                            data["failures"].append(f"{w['name']}/{v['label']}/{sname}/{lang}: {err}")
                        continue
                    measured, err = measure(peakrss, command, args.warmups,
                                             args.reps, stdin_path)
                    if measured is None:
                        vr["rows"][sname][lang] = {"ok": False, "note": err}
                        print(f"  {w['name']}/{v['label']}/{sname}/{lang}: {err}")
                        data["failures"].append(f"{w['name']}/{v['label']}/{sname}/{lang}: {err}")
                    else:
                        measured["ok"] = True
                        vr["rows"][sname][lang] = measured
                        print(f"  {w['name']}/{v['label']}/{sname}/{lang}: "
                              f"{measured['ms']} ms  {measured['rss']:.1f} MB")
            wd["variants"][v["label"]] = vr
        # Checksum equality is per workload/size and per semantic group.
        for sname, _ in SIZES[w["name"]]:
            if sname not in wanted_sizes:
                continue
            groups = {}
            for vlabel, vr in wd["variants"].items():
                for lang, m in vr["rows"].get(sname, {}).items():
                    if m.get("ok"):
                        groups.setdefault(vr["group"], {})[f"{vlabel}/{lang}"] = m["out"]
            for group, outs in groups.items():
                uniq = set(outs.values())
                if len(uniq) > 1:
                    data["mismatch"].append(f"{w['name']}/{sname}/{group}")
                    print(f"  !! checksum mismatch: {w['name']}/{sname}/{group}")
        data["workloads"][w["name"]] = wd
    # Exclude rows not selected from sizes in render.
    data["sizes"] = {k: [x for x in v if x[0] in wanted_sizes]
                     for k, v in data["sizes"].items()}
    RESULT_JSON.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    RESULTS_MD.write_text(render(data), encoding="utf-8")
    shutil.rmtree(td)
    print(f"wrote {RESULTS_MD} and {RESULT_JSON}")
    if data["mismatch"]:
        print("checksum mismatches: " + ", ".join(data["mismatch"]))
        return 1
    if data["failures"]:
        print("failures: " + ", ".join(data["failures"]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
