#!/usr/bin/env python3
"""Fail-closed regression gate for the generated suite.

Checks saved results from `bench/gen/results.json` and
`bench/gen/results-clang.json`; it does not rerun benchmarks.
"""
import json
import math
import sys
from pathlib import Path

FILES = [Path("bench/gen/results.json"), Path("bench/gen/results-clang.json")]
SERVICE = {"latency", "window_string", "window_int", "gcscan", "winagg",
           "invindex_side", "invindex_map", "dbquery"}
TOL_TIME = 1.15
TOL_MEM = 1.12


def best(data, group, lang, key, tier=None):
    vals = []
    for w in data["workloads"].values():
        for v in w["variants"].values():
            if v["group"] != group or (tier and v["tier"] != tier):
                continue
            m = v["rows"].get("large", {}).get(lang)
            if m and m.get("ok"):
                vals.append(m[key])
    return min(vals) if vals else None


def gm(vals):
    vals = [v for v in vals if v and v > 0]
    return math.exp(sum(math.log(v) for v in vals) / len(vals)) if vals else None


def main():
    if not all(p.exists() for p in FILES):
        print("bench-gate: missing saved suite results; run make bench-suite-backends first")
        return 2
    failed = 0
    for path in FILES:
        d = json.loads(path.read_text())
        print(f"== {path} ({d['cc']})")
        if d.get("mismatch"):
            print("  checksum mismatch:", ", ".join(d["mismatch"])); failed = 1
        if d.get("failures"):
            print("  failures:", ", ".join(d["failures"])); failed = 1
        # Latency: compare the idiomatic tier against C and Rust at large.
        for lang in ("c", "rust"):
            t = best(d, "latency", "tycho", "ms", "idiomatic")
            o = best(d, "latency", lang, "ms", "idiomatic")
            r = t / o if t and o else None
            print(f"  latency time vs {lang}: {r:.3f}x" if r else f"  latency time vs {lang}: -")
            if r is None or r > TOL_TIME:
                print("    FAIL (limit %.2fx)" % TOL_TIME); failed = 1
        # Service memory is the best measured variant, matching the headline.
        for lang in ("c", "rust"):
            vals = []
            for g in SERVICE:
                t = best(d, g, "tycho", "rss")
                o = best(d, g, lang, "rss")
                if t and o:
                    vals.append(t / o)
            r = gm(vals)
            print(f"  service memory vs {lang}: {r:.3f}x" if r else f"  service memory vs {lang}: -")
            if r is None or r > TOL_MEM:
                print("    FAIL (limit %.2fx)" % TOL_MEM); failed = 1
        # dbquery must have all six implementations in every saved run.
        missing = [l for l in ("tycho", "c", "rust", "go", "java", "swift")
                   if best(d, "dbquery", l, "ms") is None]
        print("  dbquery missing:", missing or "none")
        if missing:
            failed = 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
