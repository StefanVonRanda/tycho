#!/usr/bin/env python3
"""How many of the bootstrap's diagnostic RULES does the shipped compiler check?

Six rules present in src/tychoc.c and absent from compiler/ were found one at a
time, each by accident, because a fixture happened to exist and something
happened to compare the two compilers. Nobody had measured how many remain.
This enumerates the rules from the C SOURCE -- compiler/reject_class.tsv is
derived from fixtures and therefore cannot see a site no fixture reaches, so it
is a join target and never the source -- and splits them three ways:

  [a] COVERED   a fixture reaches the rule and both compilers refuse it
  [b] DIVERGE   a fixture reaches it and the two compilers do not agree
  [c] UNFIXTURED  no .ty anywhere in the tree reaches it

Dedup: a rule is one FORMAT STRING. `eat()` at src/tychoc.c:2077 emits
"expected %s" from every token position in the parser, and that is one rule, not
two hundred; the three affine container errors funnel through affine_err's "%s"
at src/tychoc.c:128 and are three. So the sites are collected with their format,
the pure pass-through formats (under 8 literal characters -- "%s") are dropped
as helpers rather than rules, and the literals their callers pass in are
collected instead.

Not every site is a rule. Four buckets are held out rather than counted as gaps,
because a second compiler owing you the same behaviour is what "gap" means here:
  CAPACITY  a compiler ceiling ("too many enums", "block nesting too deep") --
            reachable only by a generated program, never by hand-written source
  INTERNAL  oom, file I/O, the CLI, pkg-config, the C compiler's own failure --
            not reachable from a well-formed source file at all
  WARNING   warn_at: no verdict, so no reject fixture can cover it
  UNMATCHABLE  a format with under 8 literal characters, which cannot be matched
            back from a message even in principle
  REMOVED   a rule inside a builtin the language no longer has -- the parser
            rejects the call before the rule can run

Divergence is measured at two levels, because both have shipped:
  VERDICT   ./tychoc refuses and ./tychoc1 --typecheck accepts (R4's parallel-for
            class -- the file compiles clean under the shipped compiler)
  MESSAGE   both refuse, with different text (R16c-5's class)

Two limits this cannot see, and both make the answer OPTIMISTIC:

  * only the FIRST diagnostic of a file is scored, so a rule reachable only
    behind an earlier error in every fixture that reaches it reads as [c];
  * "expected %s" at src/tychoc.c:2077 is ONE rule here and 105 distinct `what`
    phrases in the source (`grep -o 'eat(ps, [A-Z_]*, "[^"]*"' | sort -u`).
    Agreement on one of them is not agreement on the other 104 -- and measured
    2026-09-03 they do not agree: tychoc says `expected ':' after field name`
    where tychoc1 says `expected ':', found newline ''`.

  python3 scripts/diag_coverage.py            counts
  python3 scripts/diag_coverage.py --list c   the rules with no fixture
  python3 scripts/diag_coverage.py --selfcheck  the extractor's own controls
"""
import os, re, subprocess, sys

sys.path.insert(0, "scripts")
import classify_rejects as C

SRC = "src/tycho" + "c.c"
ROOTS = ("tests", "corelib", "tools", "examples", "server", "bench")
DIE = ("die_at", "die", "warn_at", "diag_push")

# Held out of the rule set. Matched as substrings of the FORMAT string.
CAPACITY = ["too many ", "nesting too deep", "indentation too deep",
            "string too long", "declares too many locals", "a function type has at most",
            "a tuple has at most",
            # R21d-2: two more ceilings that meet this hold-out's own criterion --
            # 17 explicit type arguments and a 33-deep assignment place are
            # generated programs, and both are the same family as the two
            # `has at most` entries above. ./tychoc1 --parse accepts each.
            "at most 16 explicit type arguments", "too deeply nested"]
# REMOVED: the map_set/map_get/map_has/map_del builtins were taken OUT of the
# language -- src/tychoc.c:3041-3044 turns every call into `map_set was removed;
# use `m[k] = v`` (and its three siblings) at PARSE time, so nothing downstream
# of that can ever run. Measured 2026-09-03 by probing all four in both
# compilers: each died at the removal message, never at the rules below. Listed
# one format at a time rather than by the bare builtin name, so the four live
# removal diagnostics themselves stay rules.
REMOVED = ["map_set(m, key, value)", "map_set's first argument", "map_set key must be",
           "map_set value must be", "map_get(m, key, default)", "map_get's first argument",
           "map_get key must be", "map_get default must be", "map_has(m, key) takes",
           "map_has's first argument", "map_has key must be", "map_del(m, key) takes",
           "map_del's first argument", "map_del key must be",
           "map keys must be string or int"]
INTERNAL = ["oom", "cannot open", "cannot write", "read error", "unknown flag",
            "pkg-config", "C compilation failed", "already exists and was not written",
            "-g line info", "contains a NUL byte", "import cycle",
            "has no .ty files", "empty %s name", "illegal character in %s name",
            "usage:", "  %4d | ", "       | ", "%s:%d: warning: ",
            "%s:%d: note: ", "required from here"]

LIT = lambda f: len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))


def scan(text):
    """Offsets of real code, with string/char literals and comments blanked out."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            q, j = c, i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (j - i + 1)); i = j + 1; continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j])); i = j; continue
        out.append(c); i += 1
    return "".join(out)


def unescape(f):
    out, i = [], 0
    m = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}
    while i < len(f):
        if f[i] == "\\" and i + 1 < len(f):
            out.append(m.get(f[i + 1], f[i:i + 2])); i += 2; continue
        out.append(f[i]); i += 1
    return "".join(out)


# fprintf writes its own location prefix; die_at is handed one. Stripped so both
# families are matched against the same thing the user sees.
PREFIX = ("%s:%d: error: ", "%s: error: ", "%s:%d: warning: ", "tychoc: ")


def literals(text, start):
    """Format strings in the argument list opening at start.

    C concatenates ADJACENT string literals, so a run separated only by
    whitespace is one format -- but a ternary (`cond ? "a" : "b"`, which is how
    the hex and binary literal rules are written at src/tychoc.c:619) puts two
    RULES in one call, and joining them invents a message no user ever sees.
    """
    i, depth, out, cur, gap = start, 1, [], [], ""
    while i < len(text) and depth:
        ch = text[i]
        if ch == '"':
            if cur and gap.strip():
                out.append("".join(cur)); cur = []
            j, buf = i + 1, []
            while j < len(text) and text[j] != '"':
                if text[j] == "\\":
                    buf.append(text[j:j + 2]); j += 2; continue
                buf.append(text[j]); j += 1
            cur.append("".join(buf)); gap = ""; i = j + 1
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 1 and cur:
            break
        gap += ch
        i += 1
    if cur:
        out.append("".join(cur))
    res = []
    for f in out:
        f = unescape(f).rstrip("\n")
        for p in PREFIX:
            if f.startswith(p):
                f = f[len(p):]
        if f:
            res.append(f)
    return res


def sites():
    """(line, kind, format) for every diagnostic emitted by the compiler itself."""
    text = open(SRC, encoding="utf-8", errors="replace").read()
    code = scan(text)                      # so a `fprintf(stderr, ...)` inside an
    out = []                               # EMITTED C string is not a diagnostic
    pat = r"\b(%s|fprintf|affine_err)\s*\(" % "|".join(DIE)
    for m in re.finditer(pat, code):
        name = m.group(1)
        if name == "fprintf" and not re.match(r"\s*stderr\s*,", code[m.end():]):
            continue                       # fprintf(o, ...) writes the output C
        line = code.count("\n", 0, m.start()) + 1
        for f in literals(text, m.end()):
            out.append((line, name, f))
    # One rule the extractors cannot see: built by snprintf into a heap buffer and
    # handed to diag_push, from two identical sites (src/tychoc.c:538, :563).
    out.append((538, "snprintf", "unclosed '(' or '[' opened on line %d -- `%s` here can only "
                                 "start a declaration, so the bracket was never closed"))
    return sorted(out)


def rules():
    """Deduplicated by format string. Returns {format: (line, kind, bucket)}."""
    by = {}
    for line, kind, f in sites():
        if f in by:
            continue
        if kind == "warn_at" or f.startswith("%s:%d: warning: "):
            b = "WARNING"
        elif any(k in f for k in CAPACITY):
            b = "CAPACITY"
        elif any(k in f for k in INTERNAL):
            b = "INTERNAL"
        elif any(k in f for k in REMOVED):
            b = "REMOVED"
        elif LIT(f) < 8:
            b = "UNMATCHABLE"
        else:
            b = "RULE"
        by[f] = (line, kind, b)
    return by


def msg_of(stderr):
    for ln in stderr.split("\n"):
        m = re.match(r"^(?:tychoc: )?(?:(\S+?(?::\d+)?): )?error: (.*)$", ln)
        if m:
            return m.group(2).strip()
        if ln.startswith("tychoc: ") and ": error:" not in ln:
            return ln[8:].strip()
    return ""


def corpus(extra=()):
    return list(extra) + sorted(os.path.join(d, n)
                  for root in ROOTS for d, _, ns in os.walk(root)
                  for n in ns if n.endswith(".ty"))


def match(msg, rx):
    hits = [f for f, r in rx if r.match(msg) and LIT(f) >= 8]
    return max(hits, key=LIT) if hits else None


def measure(rs, allfmt, extra=()):
    # Matched against EVERY format, then filtered to the rule set: a fixture that
    # reaches a held-out ceiling ("too many parameters (max 16)") is not an
    # unmatched message, it is a fixture for a site this measurement excludes.
    rx = [(f, C.fmt_to_re(f)) for f in allfmt]
    keep = set(rs)
    hit = {}                                   # format -> [(file, how)]
    unmatched = []
    for f in corpus(extra):
        r = subprocess.run(["./tychoc", f, "--emit-c", "-o", "/tmp/_dc_out"],
                           capture_output=True, text=True)
        if r.returncode == 0:
            continue
        m = msg_of(r.stderr)
        k = match(m, rx)
        if k is None:
            unmatched.append((f, m)); continue
        if k not in keep:
            continue
        t = subprocess.run(["./tychoc1", f, "--typecheck"], capture_output=True, text=True)
        if t.returncode == 0:
            how = "VERDICT"                    # tychoc1 compiles what tychoc refuses
        elif msg_of(t.stderr) != m:
            how = "MESSAGE"
        else:
            how = "AGREE"
        hit.setdefault(k, []).append((f, how))
    return hit, unmatched


def main():
    rs = rules()
    if "--selfcheck" in sys.argv:
        mine = {(l, f) for l, k, f in sites() if k in DIE}
        theirs = {(l, f) for l, f, _ in C.load_sites()}
        rc = 0
        # The extraction differs from load_sites() at eight lines, and every
        # difference is one of three defects in THAT extractor, which is why the
        # control names the lines rather than asserting equality:
        #   619, 4795  two rules in one ternary, glued into a message no user sees
        #   731, 740, 784, 815, 7136  `\\n` in a message about escapes, unescaped
        #              once too often, so the format cannot match its own output
        #   3608       `die("cannot bind")` inside a C COMMENT -- a Tycho example
        KNOWN_DIFF = {619, 731, 740, 784, 815, 3608, 4795, 7136}
        diff = {l for l, _ in mine ^ theirs}
        if diff == KNOWN_DIFF:
            print("  selfcheck [c1] the die-family extraction matches "
                  "classify_rejects.load_sites() at every line but its eight known defects")
        else:
            print("  selfcheck [c1] FAILED: differs at %s" % sorted(diff ^ KNOWN_DIFF)); rc = 1
        if any(b == "RULE" and "too many enums" in f for f, (_, _, b) in rs.items()):
            print("  selfcheck [c2] FAILED: a capacity ceiling was counted as a rule"); rc = 1
        else:
            print("  selfcheck [c2] a capacity ceiling is not counted as a rule")
        # [c3] the positive control, and the one that stops [c] being a list of
        # phantoms: a rule with no fixture must LEAVE [c] the moment one reaches
        # it. `division by zero` is in [c] as measured 2026-09-03, and the probe
        # below is a fixture for it -- ./tychoc refuses this and ./tychoc1 does not.
        import tempfile
        d = tempfile.mkdtemp()
        f = os.path.join(d, "m.ty")
        open(f, "w").write("fn main():\n    x := 1 / 0\n    println(str(x))\n")
        names = [k for k, v in rs.items() if v[2] == "RULE"]
        hit, _ = measure(names, list(rs), extra=[f])
        if "division by zero" in hit:
            print("  selfcheck [c3] an unfixtured rule leaves [c] when a fixture reaches it")
        else:
            print("  selfcheck [c3] FAILED: the probe did not move `division by zero`"); rc = 1
        return rc
    names = sorted(f for f, v in rs.items() if v[2] == "RULE")
    hit, unmatched = measure(names, list(rs))
    a = sorted(f for f in names if f in hit and all(h == "AGREE" for _, h in hit[f]))
    b = sorted(f for f in names if f in hit and any(h != "AGREE" for _, h in hit[f]))
    c = sorted(f for f in names if f not in hit)
    held = {}
    for f, (_, _, bu) in rs.items():
        if bu != "RULE":
            held[bu] = held.get(bu, 0) + 1
    if "--families" in sys.argv:
        # Grouped by classify_rejects' own SYNTAX/NAME/TYPE/SEMANTIC split, so the
        # families a follow-up phase is sized against are the ones this tree
        # already reasons in, not a fresh taxonomy invented here.
        def cls(f, line):
            if line < C.PARSE_END and not any(k in f for k in C.NEEDS_SYMBOLS):
                return "SYNTAX"
            if any(k in f for k in C.NAME_SITES):
                return "NAME"
            if any(k in f for k in C.TYPE_SITES) and not any(k in f for k in C.TYPE_EXCLUDE):
                return "TYPE"
            return "SEMANTIC"
        for tag, group in (("b/verdict", [f for f in b if any(h == "VERDICT" for _, h in hit[f])]),
                           ("b/message", [f for f in b if all(h != "VERDICT" for _, h in hit[f])]),
                           ("c", c)):
            n = {}
            for f in group:
                k = cls(f, rs[f][0])
                n[k] = n.get(k, 0) + 1
            print("%-10s %d  %s" % (tag, len(group),
                                    " ".join("%s=%d" % kv for kv in sorted(n.items()))))
        return 0
    want = sys.argv[2] if "--list" in sys.argv else None
    if want:
        for f in {"a": a, "b": b, "c": c}[want]:
            line, kind, _ = rs[f]
            extra = ""
            if f in hit:
                extra = "  " + ",".join("%s=%s" % (h, p) for p, h in hit[f][:2])
            print("%s:%d\t%s%s" % (SRC, line, f.replace("\n", " ")[:110], extra))
        return 0
    print("diagnostic sites in %s: %d, deduplicated to %d messages" % (SRC, len(sites()), len(rs)))
    print("held out: " + ", ".join("%s %d" % (k, held[k]) for k in sorted(held)))
    print("rules: %d" % len(names))
    print("  [a] covered, both compilers agree : %d" % len(a))
    print("  [b] covered, the two DIVERGE      : %d  (verdict %d, message %d)"
          % (len(b),
             sum(1 for f in b if any(h == "VERDICT" for _, h in hit[f])),
             sum(1 for f in b if all(h != "VERDICT" for _, h in hit[f]))))
    print("  [c] NO fixture anywhere in the tree: %d" % len(c))
    for f, m in unmatched:
        print("  UNMATCHED %s :: %s" % (f, m[:90]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
