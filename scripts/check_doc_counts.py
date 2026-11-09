#!/usr/bin/env python3
"""Check the counts the status docs state against the tree.

Run by `make docs-counts-check`; `--selfcheck` proves a stale count is caught.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (path, pattern with one group per count, computed keys in group order)
CLAIMS = [
    ("docs/architecture.md",
     r"`compiler/` is ([\d,]+) lines of Tycho across (\d+) files",
     ("compiler_lines", "compiler_files")),
    ("STATUS.md", r"\| \*\*Corelib\*\* \| (\d+) packages shipped", ("packages",)),
    ("docs/internals/audit-brief.md", r"`corelib/`, (\d+) packages", ("packages",)),
]


def line_count(path):
    with open(path, encoding="utf-8") as fh:
        return len(fh.read().splitlines())


def compiler_files():
    return sorted(
        os.path.join(d, n)
        for d, _, names in os.walk(os.path.join(ROOT, "compiler"))
        for n in names if n.endswith(".ty")
    )


def computed():
    files = compiler_files()
    return {
        "c_lines": "{:,}".format(line_count(os.path.join(ROOT, "src/tychoc.c"))),
        "compiler_lines": "{:,}".format(sum(line_count(p) for p in files)),
        "compiler_files": str(len(files)),
        "packages": str(len([
            n for n in os.listdir(os.path.join(ROOT, "corelib"))
            if n != "test" and os.path.isdir(os.path.join(ROOT, "corelib", n))
        ])),
    }


def evaluate(docs, counts):
    stale = []
    for path, pattern, keys in CLAIMS:
        found = re.findall(pattern, docs.get(path, ""))
        if len(found) != 1:
            stale.append("{}: expected one match for {!r}, found {}".format(
                path, pattern, len(found)))
            continue
        groups = found[0] if isinstance(found[0], tuple) else (found[0],)
        for got, key in zip(groups, keys):
            want = counts[key]
            if got.replace(",", "") != want.replace(",", ""):
                stale.append("{}: {} is {!r}, tree has {!r}".format(
                    path, key, got, want))
    return stale


def selfcheck():
    counts = {
        "compiler_lines": "25,635",
        "compiler_files": "13",
        "packages": "48",
    }
    docs = {
        "docs/architecture.md": (
            "`compiler/` is 25,635 lines of Tycho across 13 files.\n"),
        "STATUS.md": "| **Corelib** | 48 packages shipped |\n",
        "docs/internals/audit-brief.md": "(`corelib/`, 48 packages)\n",
    }
    ok = not evaluate(docs, counts)
    print("  [1] matching counts pass: {}".format("yes" if ok else "NO"))

    stale = dict(docs)
    stale["docs/architecture.md"] = stale["docs/architecture.md"].replace(
        "25,635", "25,627")
    caught = bool(evaluate(stale, counts))
    print("  [2] stale count caught: {}".format("yes" if caught else "NO"))
    ok &= caught

    missing = dict(docs)
    missing["STATUS.md"] = "no count here\n"
    caught = bool(evaluate(missing, counts))
    print("  [3] missing claim caught: {}".format("yes" if caught else "NO"))
    ok &= caught

    print("doc-counts selfcheck: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    docs = {}
    for path, _, _ in CLAIMS:
        full = os.path.join(ROOT, path)
        with open(full, encoding="utf-8") as fh:
            docs[path] = fh.read()
    counts = computed()
    stale = evaluate(docs, counts)
    for line in stale:
        print("STALE  " + line)
    if stale:
        print("doc counts: FAILED ({} stale claim(s))".format(len(stale)))
        return 1
    print("doc counts: ok ({} claims across {} file(s))".format(
        len(CLAIMS), len({p for p, _, _ in CLAIMS})))
    return 0


if __name__ == "__main__":
    sys.exit(main())
