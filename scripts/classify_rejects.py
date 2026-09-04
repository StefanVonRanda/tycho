#!/usr/bin/env python3
"""Classify tests/reject/*.ty as SYNTAX, NAME or SEMANTIC rejections.

Grounded in src/tychoc.c, not in tychoc1's behaviour: every die_at/die/warn
format string in that file is extracted with its LINE NUMBER, the fixture's
actual tychoc message is matched back to the site that emitted it, and the site
decides the class.

  SYNTAX   = the site sits in the lexer or in parse_program's reach
             (line < PARSE_END) and does not need a symbol table.
  NAME     = the site is a NAME-resolution rule: it needs the symbol table and
             the scope stack, and nothing else. A resolver without a type
             checker must refuse these. Split out of SEMANTIC in Phase 5, the
             same way Phase 4 moved eight rules the other way -- the class is
             decided by the SITE, never by what tychoc1 happens to do, or the
             leg would pass by construction. NAME splits SEMANTIC only: a rule
             the PARSER already decides stays SYNTAX, which is why
             `upper_bind_param_used` (src line 2942, the five builtin
             constructor names, no table) is not NAME even though its message is
             word-for-word a binding rule.
  SEMANTIC = anything else -- including the parse-region sites that DO need a
             symbol table (an unknown type, a duplicate name, a where-predicate
             checked against the typaram list). A parser without a resolver must
             ACCEPT those, and that is the leg that stops "reject everything"
             from scoring full marks.
"""
import re, subprocess, sys, os

SRC = "src/tycho" + "c.c"          # assembled: a literal path:line in a string is a citation
PARSE_FN = "static ProcVec parse_program("


def find_parse_end(path=SRC):
    """Line of parse_program's closing brace -- the end of the parse region.

    DERIVED, never typed. It was a hard-coded 5245 until 2026-09-04; an edit
    inserting 23 lines above it moved three const-folding sites across the
    boundary and reclassified them SYNTAX -> SEMANTIC in silence, and no gate in
    the tree could see it (a bare Python integer is not a `path:line`, so the
    citation gate never looks at it). Bumping the literal by 23 restored those
    three and left the boundary 81 lines short of the function it names.
    """
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    starts = [i for i, ln in enumerate(lines) if ln.startswith(PARSE_FN)]
    if len(starts) != 1:
        raise LookupError("%d definitions of parse_program in %s" % (len(starts), path))
    for j in range(starts[0] + 1, len(lines)):
        if lines[j] == "}":            # column 0: the function's own closing brace
            return j + 1
    raise LookupError("parse_program has no closing brace in %s" % path)


try:
    PARSE_END = find_parse_end()
except LookupError as e:
    sys.exit("classify_rejects: " + str(e))

# Parse-region sites that consult a table tychoc1 has not built yet. Matched as
# substrings of the FORMAT string, so each names a rule rather than a line.
#
# Eight rules were dropped from this list in Phase 4, having been misjudged: the
# `where` predicate set is a fixed five names, the type-parameter list is what
# the signature just read, the subscript place rules are structural, and a
# `const` is folded at parse time by src/tychoc.c@const_fold. None of the four
# reaches a symbol table, so a parser must refuse them and they are SYNTAX.
# Name-resolution sites: the symbol table and the scope stack decide these, and
# no type is consulted. Matched as substrings of the FORMAT string, so each names
# a rule rather than a line. Checked BEFORE the SYNTAX/SEMANTIC split.
NAME_SITES = [
    "unknown variable '%s'",
    "assignment to unknown variable",
    "unknown procedure '%s'",
    "'%s' is already defined",
    "variant name '%s' is already used in this package",
    "cannot name a binding",
    "is not a discard",
    "duplicate parameter",
    "cannot assign to constant",
    "`pass` is a statement and produces no value",
    "is package-private",
    "has no symbol",
    "this file does not `import` it",
    "has no variant, const or function",
    "is already declared in this scope",
    "is a variant of enum %s, not a package member",
    "is a builtin, not a member of package",
    "imported and not used",
    "declared and not used",
]

# Monomorphic TYPE sites: Phase 6a's scope, read off src/tychoc.c's type-resolve
# section. A rule lands here when its subject is one of the eleven the phase
# names -- scalars, string/bytes, arrays, maps, structs, enums, tuples,
# Option/Result, function signatures, operators, indexing, field access, match
# exhaustiveness, assignment and return compatibility -- and NOT a generic, a
# newtype, an affine type (handle/task/channel), `bounded`, a `where` predicate,
# a subscript, the extern C-ABI list, the sink consume analysis, or one of the
# "cannot infer ..." diagnostics, each of which is a rule family of its own and
# stays SEMANTIC. Matched as substrings of the FORMAT string, so each entry names
# a rule rather than a line. Checked BEFORE the SYNTAX/SEMANTIC split and AFTER
# NAME, like every other class here.
# Rules whose message reads like a monomorphic one but whose SITE is inside
# generic instantiation -- src/tychoc.c@instantiate_generic. Phase 6b owns them.
# Checked before TYPE_SITES, since the argument-compatibility entry there would
# otherwise swallow this one.
TYPE_EXCLUDE = [
    "which does not fit the parameter pattern",
]

TYPE_SITES = [
    "unknown type",
    "'void' is a type only as a Result",
    "an array element cannot be void",
    "an array element type cannot be void",
    "a tuple element cannot be void",
    "Option(void) is not a type",
    "a function-type parameter cannot be void",
    "map keys must be",
    "a fixed-size array length must be an integer literal",
    "a fixed-size array element cannot be",
    "infinite type",
    "if condition must be bool",
    "for condition must be bool",
    "declared type",
    "returning %s but proc returns",
    "argument %d of",
    "takes %d argument(s), got",
    "cannot compare",
    "ordering compares two ints",
    "arithmetic requires two ints or two floats",
    "is not defined element-wise",
    "element-wise `%s` requires",
    "element-wise `%s` is defined for",
    "cannot mix a fixed array and a growable array",
    "cannot concatenate",
    "on a fixed array requires the same static length",
    "array elements must all have the same type",
    "element %d of a",
    "a fixed-size array of length",
    "field '%s' of %s is",
    "map key must be",
    "a map is not directly iterable",
    "tuple index %lld out of range",
    "a tuple element is named",
    "`in` tests membership in a map",
    "`in` key must be",
    "`is` asks an enum, Option or Result value",
    "is not a variant of",
    "non-exhaustive match",
    "match on a Result must cover",
    "match on an Option must cover",
    "a match on a bool must cover",
    "must carry a `_` arm",
    "is refused: nothing in the tree dispatches on it",
    "duplicate arm for",
    "duplicate or overlapping match arm",
    "wildcard must be the last match arm",
    "a range starts at",
    "a match on an int takes int literal arms",
    "is not an int constant",
    "carries no value here",
    "carries no ok value",
    "cannot bind a void value",
    "branches produce different types",
    "cannot assign %s to",
    "can only index-assign an array or map",
    "cannot index-assign an element of",
    "cannot assign to a field of a temporary",
    "'main' takes no parameters",
    "'main' returns nothing or Result(void, string)",
    "too many parameters (max",
    "len(...) takes",
    "push's first argument",
    "push's value must be",
    "pop(arr) takes one argument",
    "to_int(x)",
    "to_float(n)",
    "takes a numeric value",
    "can't hash a",
    "'&' is only valid as the argument to an inout parameter",
    "is inout; pass it as",
    "a function value can't be inout",
    "which this statement discards",
    "spread `...` is only valid as the argument to a variadic parameter",
    "must be the only variadic argument",
    "functions are not comparable",
]

NEEDS_SYMBOLS = [
    "unknown type",
    "is already defined",
    "a fixed-size array length must be an integer literal",
    "a bounded capacity must be an integer literal",
    "a vector count must be an integer literal",
    "a vector element must be",
    "soa requires a struct element type",
    "map keys must be",
    "a newtype's underlying type must be",
    "variant name '%s' is already used in this package",
    "collides with the builtin",
    "this file does not `import` it",
    "a subscript receiver may not be generic",
    "is package-private",                              # is_imported_pkg
    "but a variant of another package's enum",         # the variant table
    "extern fn '%s'",
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
    "was instantiated at one",
    "is not a variant of",
    "a bounded capacity must be positive",
]


# Messages assembled by a helper (the affine container errors) or carrying a
# backslash the format-string extractor cannot reproduce. Classified by hand,
# each with the reason: a lexer message is SYNTAX, a type rule is SEMANTIC.
FALLBACK = [
    ("cannot be stored in a container", "SEMANTIC"),   # affine_slot_check, needs types
    ("a function value cannot", "SEMANTIC"),           # fn-type affine rule, needs types
    ("is already used in this package", "NAME"),       # variant table
    ("unsupported escape", "SYNTAX"),                  # the lexer's escape set
    ("unsupported char escape", "SYNTAX"),             # same defect, the char lexer
    ("raw control byte in string literal", "SYNTAX"),  # same defect, the string lexer
    ("literal needs", "SYNTAX"),                       # the lexer's 0x / 0b scanners
    ("no 'main' procedure", "NAME"),                   # whole-program, but the Sig table alone decides it
    ("unclosed '(' or '['", "SYNTAX"),                 # the lexer, message built by snprintf
    ("must declare its own package first", "NAME"),    # the package header, an fprintf after parsing
    # report_unused_locals (src/tychoc.c:5668) is an fprintf loop, not a die_at, so
    # load_sites cannot see it. SEMANTIC, not NAME: it is a whole-program pass run
    # AFTER name resolution in both compilers, and the NAME class means "the
    # resolver refuses it" (compiler/verdict_diff.py:161).
    ("declared and not used", "SEMANTIC"),
    # report_unused_imports (src/tychoc.c@report_unused_imports) is the sibling of
    # the line above and invisible to load_sites for the same reason: an fprintf
    # loop, not a die_at. Measured 2026-09-03 on
    # tests/reject/pkg/import_unused/main.ty -- ./tychoc1 --parse rc=0,
    # --resolve rc=0, --typecheck rc=1, which is exactly the SEMANTIC contract.
    ("imported and not used in this file", "SEMANTIC"),
    # merge_pkg's two package-header checks (src/tychoc.c:14417, :14234) are
    # fprintf+exit in the package LOADER, so load_sites cannot see them either.
    # NAME, on the same measurement as "must declare its own package first"
    # above: ./tychoc1 --parse rc=0, --resolve rc=1, which is the NAME contract
    # at compiler/verdict_diff.py@main. Measured 2026-09-03 on both fixtures.
    ("but has no `package` declaration", "NAME"),
    ("but is in package `", "NAME"),
]


def fallback(msg):
    for k, c in FALLBACK:
        if k in msg:
            return c
    return None


def classify_site(line, f):
    """The class a diagnostic SITE decides: its line and its format string.

    The single copy of the split. compiler/verdict_diff.py and
    scripts/check_reject_sites.py call this rather than restating it, so a
    boundary or a rule list cannot mean two things in one tree.
    """
    if line < PARSE_END and not any(k in f for k in NEEDS_SYMBOLS):
        return "SYNTAX"
    if any(k in f for k in NAME_SITES):
        return "NAME"
    if any(k in f for k in TYPE_SITES) and not any(k in f for k in TYPE_EXCLUDE):
        return "TYPE"
    return "SEMANTIC"


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
        out.append((p, classify_site(line, f), line, msg))
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
