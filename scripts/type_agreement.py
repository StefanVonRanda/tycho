#!/usr/bin/env python3
"""The type-agreement gate: the checker and the C emitter each type every
expression -- the checker in compiler/types, the emitter again on its own in
compiler/emit -- and nothing compared them. FRICTION 243 and 244 were cases
where they differed in silence.

With TYCHO_TYPEAGREE=<file>, tychoc records each expression's type from both
sides (file, line, the expression's shape, the type in a shared spelling).
This compiles every accepting program in the tree to C with it on and
compares: an expression disagrees when the emitter gives it a type the checker
never did. The checker may type one expression several ways (a literal adapts
to its context; a generic body is checked per instance), so its types form a
set.

  python3 scripts/type_agreement.py            # the report
  python3 scripts/type_agreement.py --gate     # exit 1 on any disagreement, or
                                               # on a checker gap not in the baseline
  python3 scripts/type_agreement.py --record   # write the checker gaps as the baseline
"""
import collections, concurrent.futures, glob, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "compiler", "type_agreement.baseline")


def programs():
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, "tests", "*.ty"))):
        if os.path.exists(f[:-3] + ".out"):
            out.append(f)
    out += sorted(glob.glob(os.path.join(ROOT, "examples", "*.ty")))
    out += sorted(glob.glob(os.path.join(ROOT, "corelib", "test", "*", "main.ty")))
    out += sorted(glob.glob(os.path.join(ROOT, "tools", "*", "main.ty")))
    out += sorted(glob.glob(os.path.join(ROOT, "tests", "pkg", "*", "main.ty")))
    return out


def record(prog):
    fd, tsv = tempfile.mkstemp(suffix=".tsv")
    os.close(fd)
    os.unlink(tsv)
    c = tempfile.mktemp()
    env = dict(os.environ, TYCHO_TYPEAGREE=tsv, TYCHO_CORELIB=os.path.join(ROOT, "corelib"))
    r = subprocess.run([os.path.join(ROOT, "tychoc"), prog, "--emit-c", "-o", c],
                       env=env, capture_output=True, text=True, cwd=ROOT)
    rows = []
    if os.path.exists(tsv):
        with open(tsv) as f:
            rows = f.read().splitlines()
        os.unlink(tsv)
    for x in (c + ".c",):
        if os.path.exists(x):
            os.unlink(x)
    return prog, r.returncode, rows


def line_of(path, n):
    try:
        with open(path if os.path.isabs(path) else os.path.join(ROOT, path)) as f:
            ls = f.read().split("\n")
        return ls[n - 1].strip() if 0 < n <= len(ls) else ""
    except OSError:
        return ""


def shape_head(fp):
    m = re.match(r"[a-z]+", fp)
    return m.group(0) if m else fp


def general(t):
    # the pattern, with declared names folded: [S:*] not [S:Body]
    return re.sub(r"([SE]):[A-Za-z0-9_]+", r"\1:*", t)


def _parse(t, i=0):
    """A type string as (head, [args]); returns it and where parsing stopped."""
    j = i
    while j < len(t) and t[j] not in "(),":
        j += 1
    head = t[i:j]
    args = []
    if j < len(t) and t[j] == "(":
        j += 1
        while j < len(t) and t[j] != ")":
            a, j = _parse(t, j)
            args.append(a)
            if j < len(t) and t[j] == ",":
                j += 1
        j += 1
    return (head, args), j


def _show(n):
    head, args = n
    if head == "bnd" and len(args) == 2:
        return "[" + _show(args[1]) + "]"       # bounded[N]T is a dynamic array to the emitter
    if head == "fun":
        return "FN"                             # the emitter interns function types (F~N)
    if head == "soa":
        return "SOA"                            # ... and soa types (O~N)
    if head.startswith("S:") or head.startswith("E:"):
        # an instance: its base name; the emitter mangles nested arguments its own way
        head = head.replace("@", "")
        for c in "$(":
            k = head.find(c, 2)
            if k > 0:
                head = head[:k]
        return head + "<>" if args or True else head
    if head.startswith("S:") or head.startswith("E:"):
        head = head.replace("@", "")            # an instance's argument, as the emitter mangles it
    if not args and head.startswith("[") and head.endswith("]"):
        return "[" + _norm(head[1:-1]) + "]"
    return head + ("(" + ",".join(_show(a) for a in args) + ")" if args else "")


def _norm(t):
    if "[" in t:
        # [T] spellings: normalize the element
        if t.startswith("[") and t.endswith("]"):
            return "[" + _norm(t[1:-1]) + "]"
    try:
        n, _ = _parse(t)
        return _show(n)
    except Exception:
        return t


def _pattern(ct):
    # a checker type with `?` parts (Ok(x) before its error type is known)
    # matches any emitter type in those places; F~ and O~ are the emitter's
    # function and soa tables
    out, i = "", 0
    while i < len(ct):
        if ct[i] == "?":
            out += ".+"
        else:
            out += re.escape(ct[i])
        i += 1
    return "^" + out + "$"


def _norm_e(t):
    t = re.sub(r"F~\d+", "FN", t)
    t = re.sub(r"O~\d+", "SOA", t)
    return re.sub(r"([SE]:[A-Za-z0-9_]+)", r"\1<>", t)


def _base_eq(c, e):
    # S:pool__Slot<> (checker, arguments dropped) against S:pool__Slot__Node<> (emitter)
    ca = re.findall(r"[SE]:([A-Za-z0-9_]+)<>", c)
    ea = re.findall(r"[SE]:([A-Za-z0-9_]+)<>", e)
    if len(ca) != len(ea) or re.sub(r"[SE]:[A-Za-z0-9_]+<>", "N", c) != re.sub(r"[SE]:[A-Za-z0-9_]+<>", "N", e):
        return False
    return all(y == x or y.startswith(x + "__") for x, y in zip(ca, ea))


def matches(ct, et):
    if ct == et:
        return True
    if _base_eq(_norm(ct), _norm_e(et)):
        return True
    if ct == et:
        return True
    if et.startswith("F~") and ct.startswith("fun("):
        return True
    if et.startswith("O~") and ct.startswith("soa("):
        return True
    if et.startswith("H~") and ct.startswith("@") and (ct.endswith("__" + et[2:]) or ct == "@" + et[2:]):
        return True                       # a handle: the emitter drops its package
    if _norm(ct) == et:
        return True
    if "?" in ct and ct != "?":
        nc, ne = _norm(ct), _norm_e(et)
        pat = ""
        i = 0
        for m in re.finditer(r"([SE]):([A-Za-z0-9_]+)<>", nc):
            pat += _pattern(nc[i:m.start()])[1:-1]
            pat += m.group(1) + ":" + re.escape(m.group(2)) + r"(__[A-Za-z0-9_]+)?<>"
            i = m.end()
        pat += _pattern(nc[i:])[1:-1]
        return re.match("^" + pat + "$", ne) is not None or re.match(_pattern(ct), et) is not None
    return False


def is_literal(fp):
    # a literal, a negated one, or a named const (FRICTION 240: it adapts like one)
    return re.match(r"^(i-?\d|f-?\d|c\d|un\(-,i|un\(-,f|n:[A-Z][A-Z0-9_]*$|fld\(n:\w+\.[A-Z][A-Z0-9_]*\)$)", fp) is not None


def main():
    gate = "--gate" in sys.argv
    rec = "--record" in sys.argv
    progs = programs()
    cs = collections.defaultdict(set)
    es = collections.defaultdict(set)
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        for prog, rc, rows in ex.map(record, progs):
            if rc != 0:
                failed.append(prog)
            for row in rows:
                p = row.split("\t")
                if len(p) != 5:
                    continue
                side, f, ln, fp, t = p
                f = os.path.relpath(os.path.join(ROOT, f)) if not os.path.isabs(f) else os.path.relpath(f, ROOT)
                key = (f, int(ln), fp)
                (cs if side == "C" else es)[key].add(t)
    both = [k for k in es if k in cs]
    agree = 0
    unknown = []      # the checker could not type it; the emitter did
    differ = []       # both typed it, differently
    literal = []      # a literal the checker adapted to its context (an f32, a u8); the emitter records it unadapted
    for k in both:
        extra = [e for e in es[k] if not any(matches(c, e) for c in cs[k])]
        if not extra:
            agree += 1
            continue
        known = cs[k] - {"?"}
        if not known or "?" in cs[k]:
            unknown.append((k, sorted(cs[k]), sorted(es[k])))
        elif is_literal(k[2]):
            literal.append((k, sorted(cs[k]), sorted(es[k])))
        else:
            differ.append((k, sorted(cs[k]), sorted(es[k])))
    only_e = len([k for k in es if k not in cs])
    print("type agreement over %d programs (%d did not compile)" % (len(progs), len(failed)))
    print("  expressions both sides typed: %d -- agree %d, checker unknown %d, adapted literals %d, differ %d" %
          (len(both), agree, len(unknown), len(literal), len(differ)))
    print("  typed by the emitter only (no checker record at that shape): %d" % only_e)
    lines = []
    for title, rows in (("THE CHECKER COULD NOT TYPE IT, THE EMITTER DID", unknown),
                        ("BOTH TYPED IT, DIFFERENTLY", differ),
                        ("A LITERAL THE CHECKER ADAPTED, RECORDED UNADAPTED BY THE EMITTER", literal)):
        groups = collections.defaultdict(list)
        for k, c, e in rows:
            groups[(shape_head(k[2]), general(" | ".join(c)), general(" | ".join(e)))].append((k, c, e))
        lines.append("")
        lines.append("== %s: %d expressions in %d patterns" % (title, len(rows), len(groups)))
        for (head, c, e), items in sorted(groups.items(), key=lambda x: -len(x[1])):
            files = sorted(set(k[0] for k, _, _ in items))
            lines.append("  %4d  %-8s checker %-28s emitter %-24s in %d file(s)" % (len(items), head, c, e, len(files)))
            for k, c1, e1 in items[:2]:
                lines.append("          %s:%d  %s   [%s]" % (k[0], k[1], line_of(k[0], k[1])[:80], k[2][:60]))
    print("\n".join(lines))
    if rec:
        with open(BASELINE, "w") as f:
            f.write("# The checker gaps scripts/type_agreement.py --gate tolerates: expressions the\n")
            f.write("# checker typed `?` and the emitter typed. Each is a place nothing type-checks;\n")
            f.write("# closing one removes its line (--record). A disagreement is never tolerated.\n")
            for k, c, e in sorted(unknown):
                f.write("%s:%d\t%s\t%s\t%s\n" % (k[0], k[1], k[2], "|".join(c), "|".join(e)))
        print("\ntype-agreement: recorded %d checker gaps in %s" % (len(unknown), os.path.relpath(BASELINE, ROOT)))
    if gate:
        if differ:
            print("\ntype-agreement: %d expression(s) typed differently by the checker and the emitter" % len(differ))
            sys.exit(1)
        cur = sorted("%s:%d\t%s\t%s\t%s" % (k[0], k[1], k[2], "|".join(c), "|".join(e)) for k, c, e in unknown)
        base = []
        if os.path.exists(BASELINE):
            with open(BASELINE) as f:
                base = [l for l in f.read().splitlines() if l and not l.startswith("#")]
        new = sorted(set(cur) - set(base))
        if new:
            print("\ntype-agreement: %d disagreement(s) not in %s:" % (len(new), os.path.relpath(BASELINE, ROOT)))
            for n in new[:20]:
                print("  " + n)
            sys.exit(1)
        print("\ntype-agreement: no disagreement beyond the baseline (%d recorded)" % len(base))


if __name__ == "__main__":
    main()
