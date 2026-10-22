#!/usr/bin/env python3
"""Render Tycho-relative time and memory ratios from benchmark TSV rows.

Input is a tab-separated file with:
    workload <TAB> language <TAB> peak_rss_mb <TAB> wall_ms

Ratios are Tycho / competitor, so values below 1.0 mean Tycho is faster or
smaller. Missing measurements stay `-`; they are never treated as wins.
"""
import math
import sys
from collections import OrderedDict


LANGS = [("c", "C"), ("rust", "Rust"), ("go", "Go"),
         ("java", "Java"), ("swift", "Swift")]
# Java/Swift peak RSS carries the runtime baseline and is not a comparable
# memory-model number. Keep their time ratios, not their memory ratios.
MEM_LANGS = [("c", "C"), ("rust", "Rust"), ("go", "Go")]
BASE = "tycho"


def read_rows(path):
    rows = OrderedDict()
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) != 4:
                raise SystemExit(f"{path}:{lineno}: expected 4 TSV fields")
            workload, lang, rss, ms = parts
            try:
                rss = float(rss)
                ms = float(ms)
            except ValueError:
                raise SystemExit(f"{path}:{lineno}: non-numeric measurement")
            rows.setdefault(workload, {})[lang] = (rss, ms)
    return rows


def ratio(base, other):
    if base is None or other is None or other <= 0:
        return None
    return base / other


def geomean(values):
    values = [v for v in values if v is not None and v > 0]
    if not values:
        return None
    return math.exp(sum(math.log(v) for v in values) / len(values))


def fmt_ratio(value):
    return "-" if value is None else f"{value:.2f}x"


def render(rows, metric, langs):
    idx = 0 if metric == "memory" else 1
    print(f"Tycho-relative {metric} (Tycho / competitor; below 1.00 is better)")
    print()
    print("| workload | " + " | ".join(label for _, label in langs) + " |")
    print("|---|" + "---:|" * len(langs))
    all_ratios = {lang: [] for lang, _ in langs}
    for workload, values in rows.items():
        tycho = values.get(BASE)
        cells = []
        for lang, _ in langs:
            other = values.get(lang)
            r = ratio(tycho[idx] if tycho else None,
                      other[idx] if other else None)
            cells.append(fmt_ratio(r))
            if r is not None:
                all_ratios[lang].append(r)
        print(f"| {workload} | " + " | ".join(cells) + " |")
    means = [geomean(all_ratios[lang]) for lang, _ in langs]
    print("| **geometric mean** | " +
          " | ".join(fmt_ratio(v) for v in means) + " |")
    counts = [len(all_ratios[lang]) for lang, _ in langs]
    print()
    print("Comparable rows: " +
          ", ".join(f"{label}={n}" for (_, label), n in zip(langs, counts)))
    print()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: score.py results.tsv")
    rows = read_rows(sys.argv[1])
    render(rows, "time", LANGS)
    render(rows, "memory", MEM_LANGS)
    print("Sub-10 ms rows are start-up dominated; treat their ratios as noise.")
    print("Java and Swift peak RSS includes their runtime baseline; only their "
          "time ratios are cross-language claims.")


if __name__ == "__main__":
    main()
