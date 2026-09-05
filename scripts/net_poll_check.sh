set -eu

# The only lane that runs net.wait_readable, and the only one that can see the
# 1024-descriptor ceiling. A readiness call that reports EVERY fd ready, or NONE,
# passes any test that merely asks whether the program still works: both give a
# program that runs and prints. So every leg here names WHICH descriptor came
# back, and three controls defeat the shim one way at a time.
#
# scripts/net_poll_probe/main.ty opens 700 loopback pairs, so its highest
# connected fd is past 1024 -- an fd_set cannot represent that fd at all, which
# is why the shim is poll(2). A probe that never opens 1024 descriptors cannot
# see the ceiling, so it opens them.

cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM

[ -x ./tychoc ] || make tychoc >/dev/null

legs=0
fail=0
say_ok()   { legs=$((legs + 1)); printf '  ok   %s\n' "$1"; }
say_fail() { legs=$((legs + 1)); fail=$((fail + 1)); printf '  FAIL %s\n' "$1"; }

# Build the probe against a copy of corelib, optionally with one substitution
# applied to the shim. $1 = tag, $2 = python patch program ("" for pristine).
build() {
    tag=$1; prog=$2
    cp -R corelib "$T/cl-$tag"
    if [ -n "$prog" ]; then
        python3 - "$T/cl-$tag/net/net_shim.c" "$prog" <<'PY' || return 1
import pathlib, sys
p = pathlib.Path(sys.argv[1])
old, new = sys.argv[2].split("|||")
s = p.read_text()
# Assert the substitution APPLIED. A control that silently does not patch
# reports the UNMODIFIED shim as if it were the broken one.
n = s.count(old)
assert n == 1, f"control pattern matched {n} times, expected 1"
p.write_text(s.replace(old, new, 1))
PY
    fi
    TYCHO_CORELIB="$T/cl-$tag" ./tychoc scripts/net_poll_probe/main.ty -o "$T/p-$tag" \
        >"$T/b-$tag" 2>&1 || { sed 's/^/        /' "$T/b-$tag"; return 1; }
    rc=0
    "$T/p-$tag" >"$T/o-$tag" 2>&1 || rc=$?
    return 0
}

# ---------------------------- the real shim ----------------------------
build live "" || { echo "net-poll-check: FAILED (the probe does not build)"; exit 1; }
O=$T/o-live

for want in pairs=700 above_1024=true idle=Timeout idle_waited=true nready=1 ready_is_top=true; do
    if grep -qx "$want" "$O"; then say_ok "live: $want"
    else say_fail "live: expected '$want', probe said: $(grep "^${want%%=*}=" "$O" || echo '(nothing)')"; fi
done

# ---------------------------- negative controls ----------------------------
# Each defeats ONE property and must move the leg that names it; the pristine
# rebuild afterwards must put it back, or the control proved nothing.
ctl() {
    tag=$1; label=$2; prog=$3; leg=$4
    if ! build "$tag" "$prog"; then
        say_fail "$label: the defeated copy did not build (patch or compile)"; return
    fi
    if grep -qx "$leg" "$T/o-$tag"; then
        say_fail "$label: '$leg' still holds with the property defeated -- the leg is decoration"
    else
        say_ok "$label -> '$leg' gone (probe said: $(grep "^${leg%%=*}=" "$T/o-$tag" || echo '(nothing)'))"
    fi
}

READY_LINE='        if (pf[i].revents & (TY_POLL_IN | POLLHUP | POLLERR)) res[k++] = fds[i];'
ctl all   '[c1] report EVERY fd ready' \
    "$READY_LINE|||        res[k++] = fds[i];" 'nready=1'
ctl none  '[c2] report NO fd ready' \
    "$READY_LINE|||        if (0) res[k++] = fds[i];" 'ready_is_top=true'

# [c3] reintroduce select's ceiling. If the probe's descriptors were all below
# 1024 this control would change nothing and the >1024 leg would be decoration.
CEIL='    for (tycho_int i = 0; i < nfds; i++) if (fds[i] < 0) return;'
ctl ceil  '[c3] refuse fds >= 1024 (select FD_SETSIZE)' \
    "$CEIL|||$CEIL
    for (tycho_int i = 0; i < nfds; i++) if (fds[i] >= 1024) return;" 'ready_is_top=true'

# ---------------------------- revert ----------------------------
if build rev "" && grep -qx 'ready_is_top=true' "$T/o-rev" && grep -qx 'nready=1' "$T/o-rev"; then
    say_ok "[rev] a pristine copy restores every leg"
else
    say_fail "[rev] the revert did not restore the legs"
fi

if [ "$fail" -ne 0 ]; then
    echo "net-poll-check: FAILED ($fail of $legs legs)"
    exit 1
fi
echo "net-poll-check: all green ($legs legs)"
