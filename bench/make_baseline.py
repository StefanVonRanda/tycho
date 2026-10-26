#!/usr/bin/env python3
"""Snapshot the current generated results as the self-regression baseline."""
import json
from pathlib import Path

FILES = {
    "cc": Path("bench/gen/results.json"),
    "clang": Path("bench/gen/results-clang.json"),
}
OUT = Path("bench/gen/baseline.json")


def main():
    out = {}
    for name, path in FILES.items():
        data = json.loads(path.read_text())
        rows = {}
        for wn, w in data["workloads"].items():
            for vn, v in w["variants"].items():
                for sz, row in v["rows"].items():
                    t = row.get("tycho")
                    if t and t.get("ok"):
                        rows[f"{wn}/{vn}/{sz}"] = {"ms": t["ms"], "rss": t["rss"]}
        out[name] = {"rows": rows, "generated": data.get("generated")}
    OUT.write_text(json.dumps(out, indent=1, sort_keys=True) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
