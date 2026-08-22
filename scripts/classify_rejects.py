#!/usr/bin/env python3
"""Classify tests/reject/*.ty as SYNTAX or SEMANTIC rejections.

Grounded in src/tychoc.c, not in tychoc1's behaviour: every die_at/die/warn
format string in that file is extracted with its LINE NUMBER, the fixture's
actual tychoc message is matched back to the site that emitted it, and the site
decides the class.

  SYNTAX   = the site sits in the lexer or in parse_program's reach
             (line < PARSE_END) and does not need a symbol table.
  SEMANTIC = anything else -- including the parse-region sites that DO need a
             symbol table (an unknown type, a duplicate name, a where-predicate
             checked against the typaram list). A parser without a resolver must
             ACCEPT those, and that is the leg that stops "reject everything"
             from scoring full marks.
"""
import re, subprocess, sys, os

SRC = "src/tycho" + "c.c"          # assembled: a literal path:line in a string is a citation
PARSE_END = 5245                   # end of parse_program

# Parse-region sites that consult a table tychoc1 has not built yet. Matched as
# substrings of the FORMAT string, so each names a rule rather than a line.
NEEDS_SYMBOLS = [
    "unknown type",
    "is already defined",
    "is not a type parameter of this function",
    "which is not a type parameter of this function",
    "a fixed-size array length must be an integer literal",
    "a bounded capacity must be an integer literal",
    "soa requires a struct element type",
    "map keys must be",
    "a newtype's underlying type must be",
    "variant name '%s' is already used in this package",
    "collides with the builtin",
    "this file does not `import` it",
    "a subscript receiver may not be generic",
    "extern fn '%s'",
    "a `const` must be a single scalar literal",
    "a bounded element cannot be",
    "a fixed-size array element cannot be",
    "an array element type cannot be void",
    "'void' is a type only as a Result",
    "Option(void) is not a type",
    "Channel(void) is not a type",
    "a tuple element cannot be void",
    "a function-type parameter cannot be void",
    "a Tycho fn cannot return a handle",
    "which an affine type cannot be",
    "generic struct",
    "generic enum",
    "too many type parameters",
    "too many size parameters",
    "a struct type parameter must be written",
    "an enum type parameter must be written",
    "`where` constraints require a generic function",
    "unknown `where` predicate",
    "was instantiated at one",
    "is not a variant of",
    "const expression divides by zero",
    "a bounded capacity must be positive",
    "a subscript must yield a place rooted in",
    "is used more than once in the yielded place",
]


# Messages assembled by a helper (the affine container errors) or carrying a
# backslash the format-string extractor cannot reproduce. Classified by hand,
# each with the reason: a lexer message is SYNTAX, a type rule is SEMANTIC.
FALLBACK = [
    ("cannot be stored in a container", "SEMANTIC"),   # affine_slot_check, needs types
    ("a function value cannot", "SEMANTIC"),           # fn-type affine rule, needs types
    ("is already used in this package", "SEMANTIC"),   # variant table
    ("unsupported escape", "SYNTAX"),                  # the lexer's escape set
    ("literal needs", "SYNTAX"),                       # the lexer's 0x / 0b scanners
    ("no 'main' procedure", "SEMANTIC"),               # whole-program, after parsing
]


def fallback(msg):
    for k, c in FALLBACK:
        if k in msg:
            return c
    return None


def fmt_to_re(f):
    out, i = [], 0
    while i < len(f):
        c = f[i]
        if c == '%':
            m = re.match(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", f[i:])
            if m:
                out.append(".*")
                i += m.end()
                continue
        out.append(re.escape(c))
        i += 1
    return re.compile("^" + "".join(out) + "$", re.S)


def load_sites():
    """(line, format string, compiled regex) for every diagnostic in the C source."""
    src = open(SRC, encoding="utf-8", errors="replace").read().split("\n")
    sites = []
    joined = "\n".join(src)
    for m in re.finditer(r'\b(die_at|die|warn_at|diag_push)\s*\(', joined):
        line = joined.count("\n", 0, m.start()) + 1
        # walk the argument list, collecting the first run of adjacent string literals
        i, depth, lits, seen = m.end(), 1, [], False
        while i < len(joined) and depth:
            ch = joined[i]
            if ch == '"':
                j, buf = i + 1, []
                while j < len(joined) and joined[j] != '"':
                    if joined[j] == '\\':
                        buf.append(joined[j:j + 2]); j += 2; continue
                    buf.append(joined[j]); j += 1
                lits.append("".join(buf)); seen = True
                i = j + 1
                continue
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 1 and seen:
                break
            i += 1
        if not lits:
            continue
        f = "".join(lits)
        f = f.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")
        sites.append((line, f, fmt_to_re(f)))
    return sites


def main():
    sites = load_sites()
    files = sorted(f for f in os.listdir("tests/reject") if f.endswith(".ty"))
    out = []
    unmatched = []
    for name in files:
        p = "tests/reject/" + name
        r = subprocess.run(["./tychoc", p, "--emit-c", "-o", "/tmp/_cls_out"],
                           capture_output=True, text=True)
        msg = ""
        for ln in r.stderr.split("\n"):
            m = re.match(r"^(?:tychoc: )?(?:\S+?(?::\d+)?: )?error: (.*)$", ln)
            if m:
                msg = m.group(1).strip()
                break
            if ln.startswith("tychoc: ") and ": error:" not in ln:
                msg = ln[8:].strip()
                break
        # most SPECIFIC match wins: the format with the most literal (non-%)
        # characters. Taking the earliest line instead let a bare "%s" format
        # swallow every fixture and score all 337 as one class.
        hits = [s for s in sites if s[2].match(msg)]
        hits = [h for h in hits if len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", h[1])) >= 8]
        if not hits:
            fb = fallback(msg)
            if fb is None:
                unmatched.append((p, msg))
                out.append((p, "UNMATCHED", -1, msg))
            else:
                out.append((p, fb, 0, msg))
            continue
        best = max(hits, key=lambda h: len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", h[1])))
        line, f = best[0], best[1]
        needs = any(k in f for k in NEEDS_SYMBOLS)
        cls = "SYNTAX" if (line < PARSE_END and not needs) else "SEMANTIC"
        out.append((p, cls, line, msg))
    with open(sys.argv[1], "w") as fh:
        for p, cls, line, msg in out:
            fh.write("%s\t%s\t%d\t%s\n" % (p, cls, line, msg))
    n = {}
    for _, cls, _, _ in out:
        n[cls] = n.get(cls, 0) + 1
    print("classified", len(out), n)
    for p, m in unmatched:
        print("UNMATCHED", p, "|", m[:90])


if __name__ == "__main__":
    main()
