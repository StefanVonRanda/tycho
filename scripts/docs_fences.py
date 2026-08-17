#!/usr/bin/env python3
"""Compile every ```tycho fence in the docs, and RUN the ones that state output.

A fence is tried in this order, stopping at the first that compiles:

  1. as written                      -- a whole program
  2. + an empty `main`               -- the fence declares only types/functions
  3. wrapped in a `main`             -- the fence is loose statements
  4. 3, with the earlier fences of the SAME document prepended

Step 4 is what makes a reference page checkable. `docs/reference/maps.md`
defines `counts` in one fence and uses it in the next three; read top to bottom
that is coherent, and read one fence at a time it is an undefined variable. The
carry-over reads the page the way a reader does.

A bare expression (`len(counts)` on its own line) is bound to `_` before
compiling. Those lines are illustrations of an EXPRESSION, and binding is what
lets the type checker see them without changing what the doc shows.

A fence followed by an ```output block is RUN, and its stdout must equal that
block. Compiling proves the snippet is legal; only running proves the output
printed beside it is true.

`<!-- fence-skip: reason -->` on the line before a fence skips it. The reason is
printed, so a skip is a stated choice rather than silence.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(ROOT, "tychoc")

FENCE = re.compile(r'^```(\w*)[ \t]*\n(.*?)^```[ \t]*$', re.S | re.M)
SKIP = re.compile(r'^[ \t]*<!--[ \t]*fence-skip:[ \t]*(.*?)[ \t]*-->[ \t]*$', re.M)
DECL = re.compile(r'^[ \t]*(package|import)[ \t]')
TOPDECL = re.compile(r'^(struct|enum|type|const|handle|fn|extern|subscript)[ \t]')
HASFN = re.compile(r'^[ \t]*(extern[ \t]+("[^"]*"[ \t]+)?)?fn[ \t]', re.M)
# a line that is only an expression: no :=, no =, not a statement keyword
STMT_KW = re.compile(r'^[ \t]*(if|for|while|match|return|push|delete|println|print|'
                     r'spawn|wait|send|recv|select|break|continue|fn|struct|enum|type|'
                     r'package|import|extern|const|handle|subscript|else|case)\b')


def fences(text):
    """[(lang, body, line, skip_reason_or_None)] for every fence in the document."""
    out = []
    for m in FENCE.finditer(text):
        line = text[:m.start()].count('\n') + 1
        before = text[:m.start()].rstrip()
        # a skip marker may sit one blank line above the fence
        tail = before.split('\n')[-1] if before else ''
        sm = SKIP.match(tail.strip())
        out.append((m.group(1), m.group(2), line, sm.group(1) if sm else None))
    return out


def bind_bare(body):
    """Bind each bare-expression line to a fresh name so the type checker sees it."""
    out = []
    k = [0]
    for l in body.split('\n'):
        s = l.split('#')[0].rstrip()
        if (s.strip() and not STMT_KW.match(s) and not DECL.match(s)
                and ':=' not in s and not re.search(r'[^=!<>]=[^=]', s)
                and not s.rstrip().endswith(':')
                and re.match(r'^[ \t]*[A-Za-z_"\[(]', s)):
            indent = l[:len(l) - len(l.lstrip())]
            expr, _, comment = l.partition('#')
            k[0] += 1
            out.append('%s_fence%d := %s%s' % (indent, k[0], expr.strip(),
                                               ('   #' + comment) if comment else ''))
            continue
        out.append(l)
    return '\n'.join(out)


def split_top(text):
    """(top-level declarations, loose statements) -- a declaration owns its
       indented block, so keep taking lines until the indentation returns."""
    decls, stmts, i = [], [], 0
    lines = text.split('\n')
    while i < len(lines):
        l = lines[i]
        if TOPDECL.match(l):
            decls.append(l); i += 1
            while i < len(lines) and (not lines[i].strip() or lines[i][:1] in ' \t'):
                decls.append(lines[i]); i += 1
            continue
        stmts.append(l); i += 1
    return '\n'.join(decls), '\n'.join(stmts)


def wrap(body, preamble=""):
    head, rest = [], []
    for l in (preamble + body).split('\n'):
        (head if DECL.match(l) else rest).append(l)
    decls, stmts = split_top('\n'.join(rest))
    inner = '\n'.join('    ' + l if l.strip() else l for l in stmts.split('\n'))
    return '\n'.join(head) + '\n' + decls + '\nfn main():\n' + inner + '\n    return\n'


def compiles(src, tmp, run=False):
    p = os.path.join(tmp, "f.ty")
    open(p, 'w', encoding='utf-8').write(src)
    exe = os.path.join(tmp, "f")
    args = [TYCHOC, p, "-o", exe] if run else [TYCHOC, p, "--emit-c", "-o", exe]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        return None, (r.stderr or r.stdout)
    if not run:
        return "", ""
    q = subprocess.run([exe], capture_output=True, text=True, timeout=20, errors='replace')
    return q.stdout, ""


def main():
    if not os.path.exists(TYCHOC):
        print("docs-fences: no ./tychoc -- run 'make' first", file=sys.stderr)
        return 2
    files = subprocess.run(['git', 'ls-files', 'docs/*.md', 'README.md', 'CONTRIBUTING.md'],
                           capture_output=True, text=True, cwd=ROOT).stdout.split()
    nok = nskip = nfail = nrun = 0
    fails = []
    for f in files:
        text = open(os.path.join(ROOT, f), encoding='utf-8', errors='replace').read()
        fs = fences(text)
        carry = ""
        for i, (lang, body, line, skip) in enumerate(fs):
            if lang != 'tycho':
                continue
            if skip:
                nskip += 1
                print("    skip  %s:%d  %s" % (f, line, skip))
                continue
            # the output block that follows, if any
            want = None
            if i + 1 < len(fs) and fs[i + 1][0] == 'output':
                want = fs[i + 1][1]

            ok_body = None
            with tempfile.TemporaryDirectory() as tmp:
                # cheapest first; binding bare expressions is a FALLBACK, not the
                # default -- applied eagerly it turns `println(x)` into
                # `_ := println(x)`, which is an error on a void call.
                attempts = [("as written", body),
                            ("+ an empty main", body + "\nfn main():\n    return\n"),
                            ("wrapped in a main", wrap(body)),
                            ("wrapped, bare expressions bound", wrap(bind_bare(body)))]
                if carry:
                    attempts += [("wrapped, with this page's earlier fences", wrap(body, carry)),
                                 ("wrapped, earlier fences, bare expressions bound",
                                  wrap(bind_bare(body), carry))]

                err = ""
                for how, src in attempts:
                    got, err = compiles(src, tmp, run=bool(want))
                    if got is None:
                        continue
                    if want is not None:
                        if got.strip() != want.strip():
                            nfail += 1
                            fails.append("%s:%d -- RAN but printed %r, the ```output block says %r"
                                         % (f, line, got.strip()[:80], want.strip()[:80]))
                            break
                        nrun += 1
                    nok += 1
                    note = "" if how == "as written" else "  (%s)" % how
                    print("    ok    %s:%d%s%s" % (f, line, note,
                                                   "  [ran, output matches]" if want else ""))
                    ok_body = body
                    break
                else:
                    nfail += 1
                    first = [l for l in err.split('\n') if 'error:' in l]
                    fails.append("%s:%d -- %s" % (f, line, first[0].strip() if first else "does not compile"))
            # only a fence that COMPILED joins the carry-over: a broken one would
            # fail every later fence on this page and hide their real state.
            if ok_body is not None:
                carry += ok_body + "\n"

    for x in fails:
        print("docs-fences: FAIL " + x, file=sys.stderr)
    print("docs-fences: %d fence(s) compiled, %d of them RUN against their stated output, "
          "%d skipped, %d failure(s)" % (nok, nrun, nskip, nfail))
    return 1 if nfail else 0


if __name__ == '__main__':
    sys.exit(main())
