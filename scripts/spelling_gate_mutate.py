#!/usr/bin/env python3
"""Patch src_in_corelib back to the strstr SPELLING test.

The negative control for scripts/spelling_gate_corelib.sh. Lives in its own
file because the gate is /bin/sh and nesting a python heredoc inside a sh
heredoc inside the gate is how a control silently stops being applied.
Exits non-zero if the function is not found, so a rename cannot leave the
control quietly patching nothing.
"""
import sys

path = sys.argv[1]
src = open(path).read()
sig = 'static int src_in_corelib(void) {'
i = src.index(sig)                      # IndexError -> non-zero exit, by design
j = src.index('\n}\n', i) + 3
body = (sig + '\n'
        '    if (!g_srcname) return 0;\n'
        '    return strstr(g_srcname, "corelib/") != NULL;   /* the regression */\n'
        '}\n')
open(path, 'w').write(src[:i] + body + src[j:])
print("spelling_gate_mutate: src_in_corelib -> strstr", file=sys.stderr)
