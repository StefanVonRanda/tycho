#!/usr/bin/env python3
"""Every tracked .py and .sh must parse.

WHY THIS LANE EXISTS. On 2026-08-17 a sweep that deleted every line MENTIONING
`tychoc0` deleted two lines of CODE in `fuzz/gen.py`, because each carried a
trailing comment about it. One was a syntax error and `make fuzz-quick` caught
it at push time by accident -- that file happens to be one fuzz-quick imports.

The other is the reason this exists. Deleting a branch HEADER
(`if k == "soa_use":`) left its body attached to the previous branch, after that
branch's `return`. **Python parses unreachable code without complaint.** No
syntax error, no gate red, and the entire SOA fuzz class stopped being generated
while `soa_use` stayed in the `kinds` list. Measured: 0 of 60 seeds emitted an
`soa` where 5 should.

So this lane is deliberately TWO legs, and the second is the one that matters:

  [1] every tracked .py parses (ast) and every .sh parses (sh -n). Cheap, and it
      catches the loud half.
  [2] no function contains a statement that is UNREACHABLE because it follows a
      `return`/`raise`/`continue`/`break` in the same block. That is the shape a
      deleted branch header leaves behind, and leg [1] is blind to it by
      construction -- the file is valid Python.

Leg [2] is scoped to unreachable code specifically rather than to general dead
code: this is a defect signature, not a style rule.
"""
import ast
import os
import subprocess
import sys

TERMINAL = (ast.Return, ast.Raise, ast.Continue, ast.Break)


def unreachable(tree):
    """[(line, after)] for each statement that cannot run."""
    out = []
    for node in ast.walk(tree):
        for field in ("body", "orelse", "finalbody"):
            body = getattr(node, field, None)
            if not isinstance(body, list):
                continue
            for i, stmt in enumerate(body[:-1]):
                if isinstance(stmt, TERMINAL):
                    out.append((body[i + 1].lineno, type(stmt).__name__.lower()))
                    break
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    fails, npy, nsh, nstmt = [], 0, 0, 0

    def tracked(pat):
        r = subprocess.run(["git", "ls-files", pat], capture_output=True, text=True,
                           cwd=root)
        return [f for f in r.stdout.split() if os.path.exists(os.path.join(root, f))]

    for f in tracked("*.py"):
        npy += 1
        src = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
        try:
            tree = ast.parse(src)
        except SyntaxError as e:
            fails.append("%s:%s does not parse: %s" % (f, e.lineno, e.msg))
            continue
        for line, after in unreachable(tree):
            nstmt += 1
            fails.append("%s:%d is UNREACHABLE -- it follows a `%s` in the same "
                         "block. A deleted `if`/`elif` header leaves exactly this, "
                         "and python runs it silently" % (f, line, after))

    for f in tracked("*.sh") + [".githooks/pre-push"]:
        p = os.path.join(root, f)
        if not os.path.exists(p):
            continue
        nsh += 1
        r = subprocess.run(["sh", "-n", p], capture_output=True, text=True)
        if r.returncode != 0:
            fails.append("%s does not parse: %s"
                         % (f, (r.stderr.strip().splitlines() or [""])[0][:70]))

    if "--selfcheck" in sys.argv:
        # Each leg must redden on a mutation aimed at it, or it is decoration.
        bad = 0
        for name, src, want in (
                ("[c1] a syntax error is caught", "def f(:\n    pass\n", True),
                ("[c2] a statement after `return` is caught",
                 "def f():\n    return 1\n    x = 2\n", True),
                ("[c3] a `return` at the END of a block is NOT flagged",
                 "def f():\n    x = 1\n    return x\n", False),
                ("[c4] a `return` in an if-arm is NOT flagged",
                 "def f():\n    if x:\n        return 1\n    return 2\n", False)):
            try:
                got = bool(unreachable(ast.parse(src)))
            except SyntaxError:
                got = True
            ok = got == want
            print("  %-52s %s" % (name, "ok" if ok else "BROKEN"))
            bad += 0 if ok else 1
        print("selfcheck: %s" % ("ok (every leg reddens on its own mutation)" if not bad
                                 else "FAILED (%d leg(s) do not behave)" % bad))
        return 1 if bad else 0

    if fails:
        print("script check: FAILED (%d)" % len(fails))
        for x in fails:
            print("  " + x)
        return 1
    print("script check: ok (%d .py parse with no unreachable statement, %d .sh parse)"
          % (npy, nsh))
    return 0


if __name__ == "__main__":
    sys.exit(main())
