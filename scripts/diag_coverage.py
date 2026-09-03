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

Not every site is a rule. The buckets below are held out rather than counted as
gaps,
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
  DEAD      a site guarded by a condition an EARLIER site in the same arm
            already died on, so control never reaches it

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
# COARSE: `eat()`'s one format hides 105 hand-written `what` phrases, and this
# rule is scored on the FORMAT STRING ALONE. R21a, 2026-09-04. ./tychoc names
# the position ("expected ':' after field name"); ./tychoc1 derives the wanted
# token mechanically and appends what it FOUND ("expected ':', found op ','").
# Both refuse, both name the same token, and the extra half of tychoc1's line is
# information ./tychoc does not have at that point. Making them agree means
# either threading 105 phrases through `_expect`'s hundred-odd call sites -- a
# table nobody will maintain, and one where a stale phrase reads as a compiler
# defect -- or deleting the ", found X" half, which makes the diagnostic worse
# to buy a string compare. Neither is worth it, so the divergence is recorded
# here instead of being carried in [b] as though it were going to be fixed.
COARSE = ["expected %s"]
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
# DEAD: reserve's second array/map test repeats its first one verbatim --
# `if (!is_array(arrt) && !is_map(arrt))` at src/tychoc.c:7042 dies, and
# src/tychoc.c:7052 asks the identical question with `arrt` never reassigned in
# between. Nothing can reach it, so no fixture can name it.
# Two more, measured 2026-09-03 by probing both compilers. `a channel parameter
# cannot be inout` (check_inout_param_type's IS_CHAN arm) has two call sites and
# an IDENTICAL earlier guard dominates each: the direct one is refused while the
# parameter is PARSED (src/tychoc.c:4353, the only place a param's type is
# stored, and a variadic -- the one thing that rewrites it -- may not be inout),
# and the generic-instance one is refused nine lines above its call
# (src/tychoc.c:9069) on the same substituted type. `inout Channel(int)` and a
# `inout $T` instantiated at a channel both died at those earlier guards.
# `a newtype cannot wrap a channel` is dominated by the newtype's own
# underlying-type rule (src/tychoc.c:4833), which admits only
# int/float/string/bool/array/map/struct and runs at the ONE site that assigns
# `.under`; `type Cn = Channel(int)` died there. Its `[$N]T` sibling at
# src/tychoc.c:9316 is NOT dead -- `[$N]int` is an array, so it passes that rule
# -- and has a fixture.
# Eleven more, measured 2026-09-03 by probing ./tychoc with the program each
# rule names. SIX are the `void` bans in the type parser: g_void_ok is captured
# and CLEARED at parse_type_inner's entry (src/tychoc.c:2393), so the permission
# reaches Result's ok slot and nothing one level down. `Result(fn(void) -> int,
# E)`, `Result((void, int), E)`, `Result([void], E)`, `Option(void)`,
# `Channel(void)` and `Result(int, void)` were each fed to ./tychoc and each died
# on `'void' is a type only as a Result's ok payload` -- the permission check at
# src/tychoc.c:2558, which dominates all six. Two of the six say so in their own
# source comment, and tests/reject/option_void.ty and result_void_err.ty are
# fixtures for the DOMINATING message.
# FOUR are `narms`/`nfields`/`nvariants == 0` after an unconditional
# `eat(ps, TK_INDENT, ...)`: an INDENT is emitted only for a deeper non-blank,
# non-comment line, and the loop that follows parses one arm/field/variant per
# iteration or dies. `match x:`, `select:`, `struct S:` and `enum E:` with an
# empty body, and with a comment-only body, each died on the INDENT eat instead
# (`expected indented match arms`, `... select arms`, `an indented field list`,
# `an indented variant list`).
# The last is the SELF-DEFEATING GUARD shape: `expected `if` or `match`` at
# src/tychoc.c:3597 is the fall-through of parse_value_ctrl, and all FIVE of its
# call sites (src/tychoc.c:3843, :4138, :4152, :4161, :4183) are inside an
# `if (at(ps, TK_IF) || at(ps, TK_MATCH))`. It cannot be entered on any other
# token.
# Three more, measured 2026-09-03 by enumerating each site's producers rather
# than by argument. `cannot infer the type of None` (src/tychoc.c:8388) is the
# SELF-DEFEATING GUARD shape: T_NONE is produced at exactly ONE site
# (src/tychoc.c:6042, `case E_NONE`), and the untyped-decl arm eighteen lines
# above the guard (src/tychoc.c:8371) already diverts every `s->expr->kind ==
# E_NONE` into the pending-inference list -- so the guard is handed only the
# thing it exists to reject, and never receives it. `x := None` and `x := (None)`
# both died on the pending arm's own `could not infer the type of 'x'`.
# `a counting `for` needs int bounds` (src/tychoc.c:8776) has three S_FORRANGE
# producers (src/tychoc.c:4035, :4069, :4084) and no fourth: the first two are
# the `parallel for` forms, whose `s->parallel` sends them to resolve_parfor and
# breaks before this check, and the third is the foreach desugar, which writes a
# literal `0` and a `len(...)` call into the bounds itself. No user-written
# expression reaches them.
# `a spawned task must be bound and waited` (src/tychoc.c:8860) needs an
# EXPRESSION STATEMENT of task type. task_of has one call site
# (src/tychoc.c:5986, the E_SPAWN arm), and a bare `spawn f()` statement is
# refused while it is PARSED (src/tychoc.c:4229) with the rule stated in full; a
# bare task VARIABLE is refused as `a bare expression has no effect`. Both probed.
DEAD = ["reserve only supports arrays of scalars",
        "cannot infer the type of None",
        "a counting `for` needs int bounds",
        "a spawned task must be bound and waited",
        "a channel parameter cannot be inout",
        "a newtype cannot wrap a channel",
        "a function-type parameter cannot be void",
        "a tuple element cannot be void",
        "an array element type cannot be void",
        "Option(void) is not a type",
        "Channel(void) is not a type",
        # anchored on the tail: src/tychoc.c:2997's LIVE `Err() carries no value
        # -- a Result's error type cannot be void` contains the head verbatim
        "a Result's error type cannot be void -- Err always carries a value",
        "match needs at least one arm",
        "select needs at least one arm",
        "a struct needs at least one field",
        "an enum needs at least one variant",
        "expected `if` or `match`",
        # Four more, measured 2026-09-03 by DOMINANCE rather than by argument.
        # push/pop/reserve each strip E_FIELD/E_INDEX off the first argument and
        # require an E_IDENT root, then require the result to be an array/soa, and
        # ONLY then call src/tychoc.c@is_lvalue. is_lvalue returns 0 in exactly two
        # ways: a root that is not a place (the root-strip guard already refused
        # it, and E_TUPIDX is not even walked, so `t.0` dies there too), or an
        # E_INDEX whose base is not composite/soa/map. src/tychoc.c:6212 NORMALISES
        # that base off any newtype first, so the only bases left are the three
        # scalar arrays plus string/bytes -- and every one of those yields an
        # int/float/string element, which the is_array guard above refuses first.
        # Nine probes, three per builtin, and not one reached the rule.
        "cannot push through this expression",
        "cannot pop through this expression",
        "cannot reserve through this expression",
        # src/tychoc.c:6778 is dominated by the IS_TASK test one line above it.
        # `task_of` is called at exactly ONE site, src/tychoc.c:5986 (the E_SPAWN
        # arm), a Task has no type syntax so no signature, field or element can
        # carry one, and copying one is refused -- so a Task-typed expression is
        # an E_SPAWN or the E_IDENT it was bound to, and nothing else.
        "wait takes a task variable or a spawn expression"]
# One more, measured 2026-09-03 by moving the BINARY rather than by argument.
# `cannot find the corelib for import` (src/tychoc.c:5022) reports on the
# INSTALLATION, not on the program: TYCHO_CORELIB is taken unchecked when set,
# and with it unset the lookup finds `<exe_dir>/corelib`, which exists for every
# compiler in this tree. Copied to a bare directory, ./tychoc emits it for the
# same `import "core:strings"` that compiles here -- so no .ty file can reach it.
# It is an fprintf+exit like every other entry below, not a die_at.
# One more, measured 2026-09-03: `internal: spread ... reached codegen`
# (src/tychoc.c:10938) is a compiler-bug assertion, not a rule about a program.
# E_SPREAD has exactly two dispositions -- the variadic call arm UNWRAPS it
# (src/tychoc.c:7083 takes args[nfixed]->lhs, so no E_SPREAD node survives), and
# every other position dies at src/tychoc.c:5963. Four spread positions probed
# (a decl rhs, an array literal, a len() argument, and a second variadic
# argument beside a spread); all four were refused before codegen.
INTERNAL = ["oom", "cannot open", "cannot write", "read error", "unknown flag",
            "cannot find the corelib for import",
            "internal: spread",
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
        elif f in COARSE:
            b = "COARSE"
        elif any(k in f for k in CAPACITY):
            b = "CAPACITY"
        elif any(k in f for k in INTERNAL):
            b = "INTERNAL"
        elif any(k in f for k in REMOVED):
            b = "REMOVED"
        elif any(k in f for k in DEAD):
            b = "DEAD"
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
