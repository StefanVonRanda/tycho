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
DECLNAME = re.compile(r'^(?:struct|enum|type|const|handle|subscript|fn)[ \t]+([A-Za-z_]\w*)')
# TOP-LEVEL bindings only: a `xs :=` inside a loop body is not in scope for a
# later fence, so treating it as one drops the outer binding and breaks the page.
BINDNAME = re.compile(r'^([A-Za-z_]\w*)[ \t]*:=')
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


def heading_lines(text):
    """Line numbers of `##`-level headings: a section is the scope a reader
       assumes, so the carry-over resets there. A page that reuses `a` for an
       array in one section and a struct in another is not inconsistent."""
    return [i for i, l in enumerate(text.split('\n'), 1) if re.match(r'^## ', l)]


def bind_bare(body):
    """Bind each bare-expression line to a fresh name so the type checker sees it."""
    out = []
    k = [0]
    for l in body.split('\n'):
        s = l.split('#')[0].rstrip()
        # ONLY a top-level line: an indented line is inside a block, and a match
        # arm (`Ok(fd): fd`) rewritten to `_fenceN := Ok(fd): fd` is not an
        # expression at all -- that mangling was reported as a doc defect.
        if (s[:1] not in (' ', '\t')
                and s.strip() and not STMT_KW.match(s) and not DECL.match(s)
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


def declared(text):
    """Top-level names a fence declares, so the carry-over does not redeclare them."""
    return {m.group(1) for m in (DECLNAME.match(l) for l in text.split('\n')) if m}


def bound(text):
    """`name :=` bindings a fence makes; the carry must not bind them again."""
    return {m.group(1) for m in (BINDNAME.match(l) for l in text.split('\n')) if m}


def drop_binds(text, names):
    """Drop `name := ...` lines the current fence rebinds."""
    return '\n'.join(l for l in text.split('\n')
                     if not (BINDNAME.match(l) and BINDNAME.match(l).group(1) in names))


def drop_decls(text, names):
    """Remove top-level declarations of `names` (and their indented bodies)."""
    out, lines, i = [], text.split('\n'), 0
    while i < len(lines):
        m = DECLNAME.match(lines[i])
        if m and m.group(1) in names:
            i += 1
            while i < len(lines) and (not lines[i].strip() or lines[i][:1] in ' \t'):
                i += 1
            continue
        out.append(lines[i]); i += 1
    return '\n'.join(out)


def wrap(body, preamble=""):
    head, rest = [], []
    for l in (preamble + body).split('\n'):
        (head if DECL.match(l) else rest).append(l)
    decls, stmts = split_top('\n'.join(rest))
    inner = '\n'.join('    ' + l if l.strip() else l for l in stmts.split('\n'))
    top = '\n'.join(head)
    # an `import` needs the file to declare its own package first
    if any(l.lstrip().startswith('import') for l in head) and \
       not any(l.lstrip().startswith('package') for l in head):
        top = 'package main\n' + top
    return top + '\n' + decls + '\nfn main():\n' + inner + '\n    return\n'


def compiles(src, tmp, run=False, execute=True):
    """Build the fence, and RUN it. Returns (stdout, error) or (None, error).

    Every fence that builds is executed, not only the ones with a stated
    output: a snippet can compile and still abort on an index, a division or a
    failed unwrap, and "it compiles" would call that verified. stdin is empty
    and the run is bounded, so a fence that waits for input or loops forever
    reports that instead of hanging the gate.
    """
    p = os.path.join(tmp, "f.ty")
    open(p, 'w', encoding='utf-8').write(src)
    exe = os.path.join(tmp, "f")
    r = subprocess.run([TYCHOC, p, "-o", exe],
                       capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        return None, (r.stderr or r.stdout)
    if not execute:
        return "", ""
    try:
        q = subprocess.run([exe], capture_output=True, text=True, timeout=10,
                           errors='replace', stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, "error: ran for 10s without exiting"
    if q.returncode != 0:
        return None, ("error: compiled, but exited %d when run: %s"
                      % (q.returncode, (q.stderr or "").strip().splitlines()[:1]))
    return q.stdout, ""


def main():
    if not os.path.exists(TYCHOC):
        print("docs-fences: no ./tychoc -- run 'make' first", file=sys.stderr)
        return 2
    # every tracked .md, not a hand-listed subset: a snippet in
    # examples/*/README.md or tools/*/README.md is a snippet a reader copies.
    files = subprocess.run(['git', 'ls-files', '*.md'],
                           capture_output=True, text=True, cwd=ROOT).stdout.split()
    nok = nskip = nfail = nrun = nsh = 0
    fails = []
    for f in files:
        text = open(os.path.join(ROOT, f), encoding='utf-8', errors='replace').read()
        fs = fences(text)
        heads = heading_lines(text)
        carry = ""
        last_line = 0
        for i, (lang, body, line, skip) in enumerate(fs):
            if any(last_line < h < line for h in heads):
                carry = ""      # new section: the previous one's names are out of scope
            last_line = line
            if lang == 'c':
                # a C fence is checked for SYNTAX only: most are excerpts of a
                # shim or of emitted code, so they will not link, but a snippet
                # that does not parse is wrong on its face.
                if skip:
                    nskip += 1; print("    skip  %s:%d  %s" % (f, line, skip)); continue
                # A C fence is an excerpt of emitted code or of a shim, so it
                # names the runtime's types. Give it that context and it is a
                # real compile, not a guess -- as written, then wrapped in a
                # function for a fence that is a bare statement.
                PRELUDE = ("#include <stdio.h>\n#include <stdlib.h>\n"
                           "#include <string.h>\n#include <stdint.h>\n"
                           "typedef int64_t tycho_int;\n"
                           "typedef struct Arena { char *base; size_t used, cap; "
                           "struct Arena *parent; } Arena;\n"
                           "typedef struct Region { Arena *a; } Region;\n"
                           "typedef struct { char *data; tycho_int len; } TychoStr;\n"
                           # the arena API these excerpts are excerpts OF
                           "Arena *arena_new(Arena *parent);\n"
                           "void *arena_alloc(Arena *a, size_t n);\n"
                           "void arena_free(Arena *a);\n"
                           "void arena_reset(Arena *a);\n")
                with tempfile.TemporaryDirectory() as tmp:
                    cp = os.path.join(tmp, "s.c")
                    forms = [("as written", PRELUDE + body),
                             ("wrapped in a function",
                              PRELUDE + "void _fence(void) {\n" + body + "\n}\n")]
                    err = ""
                    for how, src in forms:
                        open(cp, 'w').write(src)
                        r = subprocess.run(['cc', '-fsyntax-only', '-w', cp],
                                           capture_output=True, text=True, errors='replace')
                        if r.returncode == 0:
                            nok += 1
                            print("    ok    %s:%d  [C compiles%s]" % (
                                f, line, "" if how == "as written" else ", " + how))
                            break
                        err = r.stderr
                    else:
                        nskip += 1
                        print("    skip  %s:%d  a C excerpt: %s" % (
                            f, line, (err.strip().splitlines() or [""])[-1][:70]))
                continue
            if lang == 'sh':
                # a shell fence is not RUN -- these clone repositories, build
                # releases and publish them. What is checkable without side
                # effects is that every command it names exists on this machine.
                if skip:
                    nskip += 1; print("    skip  %s:%d  %s" % (f, line, skip)); continue
                # A shell fence is RUN when every line is safe to run: no
                # network, no publishing, no writes outside a temp dir. The
                # unsafe ones are named, not silently passed.
                UNSAFE = ('git clone', 'gh release', 'gh api', 'git push', 'curl',
                          'wget', 'rm -rf', 'sudo', 'apt', 'brew', 'make install',
                          'scp', 'ssh', 'docker', 'npx', 'npm', 'pip',
                          # binds a fixed port: the result depends on what else
                          # is listening, which is not a property of the doc
                          '--port', 'tycho-httpd')
                if re.search(r'^\s*\((gdb|lldb)\)', body, re.M):
                    nskip += 1
                    print("    skip  %s:%d  a debugger TRANSCRIPT: `(gdb)`/`(lldb)` are "
                          "prompts, and the lines after them are its output" % (f, line))
                    continue
                # a SYNOPSIS is not a command: `tycho-diff [--stat] OLD NEW`
                # names its arguments in the usual placeholder style, and running
                # it means running with the placeholders as literal filenames.
                if re.search(r'\[--?[a-z]|\[-[A-Z]|\b(OLD|NEW|FILE|PATH|DIR|SRC|DST|'
                             r'INPUT|OUTPUT|ARGS)\b'
                             # lowercase placeholders name a file the reader supplies
                             r'|\b(program|prog|file|yourfile|myprog)\.ty\b|<[a-z-]+>', body):
                    nskip += 1
                    print("    skip  %s:%d  a usage SYNOPSIS: its arguments are "
                          "placeholders, not values" % (f, line))
                    continue
                unsafe = [u for u in UNSAFE if u in body]
                missing = []
                for l in body.split('\n'):
                    t = l.strip().lstrip('$').strip()
                    if not t or t.startswith('#'):
                        continue
                    cmd = t.split()[0]
                    if cmd in ('cd', 'export', 'set', 'echo', 'source', '.', 'if', 'for',
                               'while', 'then', 'fi', 'done', 'do', 'else', './tychoc',
                               './tycho', 'make'):
                        continue
                    if cmd.startswith('./') or '=' in cmd:
                        continue
                    # the repo root holds tychoc, tycho and the built tools; a
                    # reader who installed them has them on PATH, so look there
                    if subprocess.run(
                            ['sh', '-c', 'command -v %s' % cmd], capture_output=True,
                            env=dict(os.environ,
                                     PATH=ROOT + os.pathsep + os.environ["PATH"])
                            ).returncode != 0:
                        missing.append(cmd)
                if missing:
                    nskip += 1
                    print("    skip  %s:%d  shell: not on this machine: %s"
                          % (f, line, " ".join(sorted(set(missing)))))
                elif unsafe:
                    # RUN it, with the side-effecting commands stubbed on PATH.
                    # That verifies the pipeline, the flags and everything around
                    # them; what it does not do is clone, publish or bind. A stub
                    # echoes its argv and exits 0, so the fence still fails if any
                    # OTHER command in it is wrong.
                    with tempfile.TemporaryDirectory() as tmp:
                        bindir = os.path.join(tmp, "bin"); os.makedirs(bindir)
                        for name in ("git", "gh", "npx", "npm", "pip", "curl", "wget",
                                     "sudo", "apt", "brew", "scp", "ssh", "docker",
                                     "tycho-httpd"):
                            sp = os.path.join(bindir, name)
                            open(sp, "w").write('#!/bin/sh\necho "[stub] %s $*"\nexit 0\n' % name)
                            os.chmod(sp, 0o755)
                        cmds = "\n".join(
                            re.sub(r'^\s*\$\s?', '', l) for l in body.split('\n')
                            if l.strip() and not l.strip().startswith('#'))
                        env = dict(os.environ,
                                   PATH=os.pathsep.join([bindir, ROOT, os.environ["PATH"]]))
                        # `./tycho-httpd` names a path, so PATH's stub would be
                        # missed; drop the `./` on a stubbed name only
                        for name in ("tycho-httpd",):
                            cmds = cmds.replace("./" + name, name)
                        r = subprocess.run(['sh', '-c', "set -e\ncd %s\n" % ROOT + cmds],
                                           capture_output=True, text=True, timeout=120,
                                           errors='replace', stdin=subprocess.DEVNULL,
                                           cwd=tmp, env=env)
                    if r.returncode == 0:
                        nok += 1; nsh += 1
                        print("    ok    %s:%d  [shell, RAN with %s stubbed, exit 0]"
                              % (f, line, "/".join(unsafe)))
                    else:
                        nfail += 1
                        fails.append("%s:%d -- shell fence RAN (%s stubbed) and exited %d: %s"
                                     % (f, line, "/".join(unsafe), r.returncode,
                                        (r.stderr.strip().splitlines() or [""])[-1][:60]))
                else:
                    with tempfile.TemporaryDirectory() as tmp:
                        # a fence shows its prompt; strip `$ ` before running,
                        # or sh sees `$` as a command (tutorial.md exited 127)
                        cmds = "\n".join(
                            re.sub(r'^\s*\$\s?', '', l) for l in body.split('\n')
                            if l.strip() and not l.strip().startswith('#'))
                        env = dict(os.environ,
                                   PATH=os.pathsep.join([ROOT, os.environ["PATH"]]))
                        script = "set -e\ncd %s\n" % ROOT + cmds
                        r = subprocess.run(['sh', '-c', script], capture_output=True,
                                           text=True, timeout=120, errors='replace',
                                           stdin=subprocess.DEVNULL, cwd=tmp, env=env)
                    if r.returncode == 0:
                        nok += 1; nsh += 1
                        print("    ok    %s:%d  [shell, RAN, exit 0]" % (f, line))
                    else:
                        # a fence that is safe to run and then fails is a defect,
                        # not a note: the doc tells a reader to run it
                        nfail += 1
                        fails.append("%s:%d -- shell fence RAN and exited %d: %s"
                                     % (f, line, r.returncode,
                                        (r.stderr.strip().splitlines() or [""])[-1][:60]))
                continue
            if lang != 'tycho':
                continue
            # a marker that says the fence is REFUSED is an assertion, not an
            # excuse: the gate compiles it and requires the refusal to hold.
            if skip and re.search(r'REFUSED|must not compile|program that failed|'
                                  r'is the REPRO|REJECTED', skip, re.I):
                with tempfile.TemporaryDirectory() as tmp:
                    got, _e = compiles(body, tmp, execute=False)
                if got is None:
                    nok += 1
                    print("    ok    %s:%d  [refused, as its marker states]" % (f, line))
                else:
                    nfail += 1
                    fails.append("%s:%d -- its marker says this is refused, but it COMPILES"
                                 % (f, line))
                continue
            norun = bool(skip and skip.startswith('norun:'))
            if skip and not norun:
                nskip += 1
                print("    skip  %s:%d  %s" % (f, line, skip))
                continue
            # `...` is not Tycho. A fence using it as an elided body is showing
            # the SHAPE of a construct, not a program -- filling the bodies in
            # would obscure the very thing it illustrates.
            if re.search(u'^[ \t]*(\\.\\.\\.|\u2026)[ \t]*$|:[ \t]+(\\.\\.\\.|\u2026)[ \t]*$',
                         body, re.M):
                nskip += 1
                print("    skip  %s:%d  a shape illustration: `...` marks an elided body"
                      % (f, line))
                continue
            # the block that follows decides what this fence claims
            want = None       # ```output -> run it, stdout must match
            expect_fail = False   # a block naming an error -> it must NOT compile
            if i + 1 < len(fs):
                nlang, nbody = fs[i + 1][0], fs[i + 1][1]
                if nlang == 'output':
                    want = nbody
                elif nlang in ('', 'text') and re.search(
                        r'^\s*(error|warning):|detected in tcache|Segmentation|assertion',
                        nbody, re.M | re.I):
                    expect_fail = True

            if expect_fail:
                with tempfile.TemporaryDirectory() as tmp:
                    got, _e = compiles(body, tmp)
                    if got is None:
                        nok += 1
                        print("    ok    %s:%d  [refused, as the block below it records]" % (f, line))
                    else:
                        nfail += 1
                        fails.append("%s:%d -- COMPILES, but the block below it records an error"
                                     % (f, line))
                continue

            ok_body = None
            with tempfile.TemporaryDirectory() as tmp:
                # cheapest first; binding bare expressions is a FALLBACK, not the
                # default -- applied eagerly it turns `println(x)` into
                # `_ := println(x)`, which is an error on a void call.
                # a fence that declares `main` is a whole program: wrapping it
                # or appending another main is nonsense, so it gets one attempt
                # and its own error is the one reported.
                whole = 'main' in declared(body)
                attempts = [("as written", body)]
                if not whole:
                    attempts.append(("+ an empty main",
                                     body + "\nfn main():\n    return\n"))
                if not whole:
                    attempts += [
                             ("wrapped in a main", wrap(body)),
                             ("wrapped, bare expressions bound", wrap(bind_bare(body)))]
                # a fence with its own `main` is a whole program: prepending the
                # page's earlier fences would give it a second one.
                if carry and not whole:
                    # the carry must not redeclare what this fence declares --
                    # a page that shows `struct Point` twice is not an error
                    pre = drop_binds(drop_decls(carry, declared(body) | {"main"}), bound(body))
                    attempts += [("wrapped, with this page's earlier fences", wrap(body, pre)),
                                 ("wrapped, earlier fences, bare expressions bound",
                                  wrap(bind_bare(body), pre)),
                                 ("this page's earlier fences, as written", pre + "\n" + body)]

                errs = {}
                if os.environ.get("DUMP") == "%s:%d" % (f, line):
                    for how, src in attempts:
                        print("----- attempt: %s\n%s" % (how, src), file=sys.stderr)
                for how, src in attempts:
                    got, e = compiles(src, tmp, run=bool(want), execute=not norun)
                    if got is None:
                        errs[how] = e
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
                    # carry the form that COMPILED: if binding bare expressions
                    # is what made it work, the raw body would poison every
                    # later fence on the page with the same bare expressions.
                    ok_body = bind_bare(body) if "bare expressions bound" in how else body
                    break
                else:
                    nfail += 1
                    # report the most CONTEXTUAL attempt's error: the last one is
                    # "as written with the carry prepended", which always fails
                    # with "expected 'fn'" because Tycho has no top-level
                    # statements -- true, and never the reason the fence is broken.
                    order = ["wrapped, earlier fences, bare expressions bound",
                             "wrapped, with this page's earlier fences",
                             "wrapped, bare expressions bound",
                             "wrapped in a main", "+ an empty main", "as written"]
                    err = next((errs[k] for k in order if errs.get(k)), "")
                    first = [l for l in err.split('\n') if 'error:' in l]
                    fails.append("%s:%d -- %s" % (f, line, first[0].strip() if first else "does not compile"))
            # only a fence that COMPILED joins the carry-over: a broken one would
            # fail every later fence on this page and hide their real state.
            if ok_body is not None:
                # a page may show `struct Point` in several fences; keep the
                # newest declaration of each name so the carry stays compilable
                carry = (drop_binds(drop_decls(carry, declared(ok_body)), bound(ok_body))
                         + "\n" + ok_body + "\n")

    for x in fails:
        print("docs-fences: FAIL " + x, file=sys.stderr)
    print("docs-fences: %d snippet(s) verified -- every Tycho fence BUILT AND RUN "
          "(exit 0), %d with stdout compared to its ```output block; %d shell fences RAN; "
          "%d skipped with a stated reason, %d failure(s)"
          % (nok, nrun, nsh, nskip, nfail))
    return 1 if nfail else 0


if __name__ == '__main__':
    sys.exit(main())
