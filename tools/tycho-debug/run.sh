#!/bin/sh
# Gate for tycho-debug, the gdb adapter in tools/tycho-debug/. Runs the tool
# against fixtures the runner writes itself and asserts on stable transcript
# lines. In `make ci` as step [3p/22].
#
# WHY NO GOLDEN. The tool's own formatting is deterministic, but the session
# transcribes gdb's output too (breakpoint addresses, `=library-loaded` noise,
# the thread/pid lines, where an interrupt lands), and that drifts across gdb
# versions. A byte golden would redden for a gdb upgrade, not a tool change --
# so the lane asserts behavior: breakpoint set + hit on the right source line,
# locals under their stripped C names, print, step, clean quit, normal exit,
# Ctrl-C interrupt of a running program, and fail-closed refusals.
#
# It is the only lane that RUNS tycho-debug, exactly as ar-check is for
# tycho-ar: scripts/tools_check.sh `--emit-c`s every .ty in the tree, so a
# syntax error already reddens step [9], but [3b] entrypoints never looks
# under tools/, so nothing else executes the binary. A debugger that stopped
# breaking, stopped stepping, or hung on Ctrl-C would keep every other gate
# green.
#
# gdb is an external dependency like sqlite3/libpng: when it is absent the
# lane SKIPS loudly instead of reddening (the tool itself fails closed with
# "gdb not found on PATH").
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
D=tools/tycho-debug
T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "interpreter=mi2 --args /tmp/.tycho_dbg_" 2>/dev/null' EXIT
fail=0
skipped=0

command -v gdb >/dev/null 2>&1 || { echo "SKIP: gdb not on PATH -- tycho-debug lane skipped"; exit 0; }

"$TYCHOC" "$D/main.ty" --shim "$D/debug_shim.c" -o "$T/td" >"$T/build.log" 2>&1 \
    || { echo "FAIL (tool build)"; cat "$T/build.log"; exit 2; }
TD="$T/td"

# the fixture: assignments with known values, then a loop, then output
FIX="$T/fix"
mkdir -p "$FIX"
cat > "$FIX/prog.ty" <<'EOF'
fn main():
    x := 41
    y := x + 1
    total := 0
    i := 0
    for i < 3:
        total = total + i
        i = i + 1
    println(str(y))
    println(str(total))
EOF
# an infinite loop, for the Ctrl-C leg
cat > "$FIX/slow.ty" <<'EOF'
fn main():
    i := 0
    for true:
        i = i + 1
EOF
# a package build: -g line info is single-file only, so it must be refused
cat > "$FIX/pkg.ty" <<'EOF'
package main
fn main():
    println("x")
EOF

# [1] the core loop: breakpoint set + hit on the right source line, locals
# under stripped C names (h_x -> x), print, step, clean quit (exit 0)
out=$(printf 'b 4\nr\nloc\np h_x\nn\nq\n' | TYCHOC="$PWD/tychoc" "$TD" "$FIX/prog.ty" 2>&1); rc=$?
ok=1
echo "$out" | grep -q 'breakpoint 1 at prog.ty:4'   || { echo "  no breakpoint set"; ok=0; }
echo "$out" | grep -q '\[breakpoint 1 hit\]'        || { echo "  breakpoint not hit"; ok=0; }
echo "$out" | grep -q 'at prog.ty:4  (main)'        || { echo "  wrong stop location"; ok=0; }
echo "$out" | grep -q '  x = 41'                    || { echo "  local x missing/wrong"; ok=0; }
echo "$out" | grep -q '  y = 42'                    || { echo "  local y missing/wrong"; ok=0; }
echo "$out" | grep -q '^tycho-debug> 41$'                        || { echo "  print h_x wrong"; ok=0; }
echo "$out" | grep -q '\[stepped\]'                 || { echo "  step not reported"; ok=0; }
echo "$out" | grep -q 'at prog.ty:5  (main)'        || { echo "  step landed wrong"; ok=0; }
[ "$rc" -eq 0 ] || { echo "  session exit $rc"; ok=0; }
if [ "$ok" -eq 1 ]; then echo "  [1] scripted session: break/run/locals/print/step/quit ok"
else echo "FAIL [1] scripted session"; echo "$out" | sed 's/^/    /' | head -25; fail=1; fi

# [2] -b pre-set breakpoints do the same without typing `b`
out=$(printf 'r\nq\n' | TYCHOC="$PWD/tychoc" "$TD" -b 4 "$FIX/prog.ty" 2>&1); rc=$?
if echo "$out" | grep -q 'breakpoint 1 at prog.ty:4' && echo "$out" | grep -q '\[breakpoint 1 hit\]' && [ "$rc" -eq 0 ]; then
    echo "  [2] -b flag: pre-set breakpoint ok"
else echo "FAIL [2] -b flag"; echo "$out" | sed 's/^/    /' | head -15; fail=1; fi

# [3] a run to completion (no breakpoint) reports the exit and the program's
# own output survives (@ records: "42" and "3")
out=$(printf 'r\nq\n' | TYCHOC="$PWD/tychoc" "$TD" "$FIX/prog.ty" 2>&1); rc=$?
if echo "$out" | grep -q '\[program exited normally\]' && echo "$out" | grep -q '^42$' \
   && echo "$out" | grep -q '^3$' && [ "$rc" -eq 0 ]; then
    echo "  [3] run to completion: exit + program output ok"
else echo "FAIL [3] run to completion"; echo "$out" | sed 's/^/    /' | head -15; fail=1; fi

# [4] Ctrl-C interrupts a RUNNING program: SIGINT to the tool must become a
# stop (the shim forwards it to the inferior; gdb reports *stopped SIGINT),
# after which the session still answers commands and quits cleanly
if [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" -ne 0 ]; then
  skipped=1
  echo "  [4] SKIP Ctrl-C interrupt on Windows: no SIGINT-to-inferior (tools/tycho-debug/debug_shim.c@dbgx_kill)"
  echo "      -- the console handler runs on a new thread and a blocked pipe read has no EINTR to wake it;"
  echo "      the flag is set but unread until the next command. \`q\` still quits. Measured: gdb reports the"
  echo "      SIGINT itself, the tool never prints [stopped by SIGINT]."
else
printf 'r\nq\n' | TYCHOC="$PWD/tychoc" "$TD" "$FIX/slow.ty" >"$T/int.out" 2>&1 &
TPID=$!
sleep 2
kill -INT "$TPID" 2>/dev/null
i=0
while kill -0 "$TPID" 2>/dev/null && [ "$i" -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
if kill -0 "$TPID" 2>/dev/null; then
    echo "FAIL [4] interrupt: debugger still alive after Ctrl-C"; kill -9 "$TPID" 2>/dev/null; fail=1
else
    wait "$TPID"; rc=$?
    if grep -q '\[stopped by SIGINT\]' "$T/int.out" && [ "$rc" -eq 0 ]; then
        echo "  [4] Ctrl-C interrupt: stop reported, clean exit"
    else echo "FAIL [4] interrupt"; grep -v "libthread\|host lib" "$T/int.out" | sed 's/^/    /' | head -15; fail=1; fi
fi
fi

# [5] fail-closed refusals: missing file, a package build (no -g line info),
# and a breakpoint on a non-code line (error, session continues)
"$TD" "$FIX/nope.ty" >"$T/e1.out" 2>&1; rc1=$?
"$TD" "$FIX/pkg.ty" >"$T/e2.out" 2>&1; rc2=$?
out=$(printf 'b 999\nr\nq\n' | TYCHOC="$PWD/tychoc" "$TD" "$FIX/prog.ty" 2>&1); rc3=$?
if [ "$rc1" -ne 0 ] && grep -q 'no such file' "$T/e1.out" \
   && [ "$rc2" -ne 0 ] && grep -q 'single-file' "$T/e2.out" \
   && echo "$out" | grep -q 'No compiled code for line 999' && [ "$rc3" -eq 0 ]; then
    echo "  [5] fail-closed: missing file / package build / bad line ok"
else echo "FAIL [5] fail-closed"; cat "$T/e1.out" "$T/e2.out" | sed 's/^/    /'; echo "$out" | sed 's/^/    /' | head -8; fail=1; fi

# [6] the `tycho debug` dispatcher command: build the driver, hand a session
# through it (TYCHODEBUG points at the lane's own tool binary)
"$TYCHOC" tools/tycho.ty --shim tools/tycho_shim.c -o "$T/tycho" >"$T/tycho.log" 2>&1 \
    || { echo "FAIL [6] tycho driver build"; cat "$T/tycho.log"; fail=1; }
out=$(printf 'b 4\nr\nq\n' | TYCHODEBUG="$TD" TYCHOC="$PWD/tychoc" "$T/tycho" debug "$FIX/prog.ty" 2>&1); rc=$?
if echo "$out" | grep -q 'breakpoint 1 at prog.ty:4' && echo "$out" | grep -q '\[breakpoint 1 hit\]' \
   && [ "$rc" -eq 0 ]; then
    echo "  [6] tycho debug wrapper ok"
else echo "FAIL [6] tycho debug wrapper"; echo "$out" | sed 's/^/    /' | head -15; fail=1; fi

[ "$fail" -ne 0 ] && { echo "tycho-debug: FAIL"; exit 1; }
[ "$skipped" -eq 0 ] && echo "tycho-debug: all green (6 legs)" \
                     || echo "tycho-debug: green ($((6 - skipped)) legs, $skipped skipped -- see the SKIP line above)"
exit 0
