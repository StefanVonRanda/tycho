set -u
# Every platform this project SHIPS, and what actually executed on it.
#
# scripts/release_cross.sh builds six targets and writes UNTESTED-PLATFORM.txt
# into four of them, because four had never been run anywhere. That judgement
# was HARDCODED in a case statement: the script asserted which platforms were
# tested rather than measuring it, so a platform could start being covered (or
# stop) and the shipped disclaimer would not move. aarch64-linux is the worked
# example -- `make test` was run there by hand, once, went 1065/1065, and the
# only record was a sentence in a document.
#
# This runs the toolchain on each reachable platform and writes the verdicts to
# build/platform-matrix.tsv. release_cross.sh reads that file, so a platform
# stops carrying the disclaimer only when a run recorded here says it passed.
#
# RUNNERS -- how a platform is reached from the machine this is invoked on:
#
#   local          this host, natively
#   winkvm:<h>     a Windows x86_64 VM under KVM on host <h>. Native speed,
#                  because that box IS x86_64 -- the same guest on this Mac
#                  would be TCG emulation and take hours per run.
#   winvm          the local Windows-on-ARM VM, scripts/platform/win11arm/vm.sh.
#                  Started on demand and left running; it is a real Windows
#                  kernel on real aarch64, not wine and not an emulator.
#   podman:<h>:<img>  a container on host <h> over ssh. The host supplies the
#                  ARCHITECTURE and the image supplies a pinned userland, so a
#                  pass means "clean x86_64 Linux", not "whatever that box has
#                  installed today".
#   lima:<vm>      a Lima VM on this host. Real kernel, real libc, real arch.
#   ssh:<host>     a machine over ssh -- the x86_64 Linux box, a Windows VM.
#   wine           NOT a platform. Wine is an ABI reimplementation, so a green
#                  wine lane says the binary is well-formed, not that Windows
#                  runs it. It is recorded as evidence and never as coverage.
#
# A platform with no runner here is UNCOVERED and says so. That is the honest
# state and the point of the file: the gap is visible instead of implied.

cd "$(dirname "$0")/.." || exit 2
ROOT="$PWD"
here_win="$ROOT/scripts/platform/win11arm"
OUT="$ROOT/build/platform-matrix.tsv"

# platform<TAB>runner<TAB>what a pass proves
TARGETS="
linux-x86_64\tpodman:strix-halo\tclean x86_64 Linux (Debian trixie container on strix-halo)
linux-arm64\tlima:tycho\tnative execution on an aarch64 Linux kernel
macos-arm64\tlocal\tnative execution on this host
windows-x86_64\twinkvm:strix-halo\tnative execution on x86_64 Windows (KVM VM)
windows-arm64\twinvm\tnative execution on aarch64 Windows (UTM VM)
"

# The fixtures a platform leg runs. Small and broad on purpose: a platform
# defect is an ABI, libc, float or threading difference, and it shows up in the
# first program that touches the area, not the hundredth. `--full` runs the
# whole 1065-fixture suite instead, which is what a release wants.
FIXTURES="floats maps generics closures options results enums tuples slices
soa newtypes value_semantics bitops int_overflow or_return match_expr"

FULL=0; STRICT=0; LIST=0; SELFCHECK=0; ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --full)      FULL=1 ;;
        --strict)    STRICT=1 ;;
        --list)      LIST=1 ;;
        --only)      shift; ONLY="${1:-}" ;;
        --selfcheck) SELFCHECK=1 ;;
        *) echo "platform-matrix: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

targets() { printf '%b' "$TARGETS" | grep -v '^$'; }

# ---- selfcheck ----------------------------------------------------------
# A matrix that cannot report a failure is a green light with no lamp. These
# legs run the verdict classifier over synthetic runner output, because that
# classifier is the whole gate: every runner funnels into it, and if it reads
# "failed: 3" as a pass then six platforms report green forever.
if [ "$SELFCHECK" = 1 ]; then
    ok=1
    leg() {  # name expected actual
        if [ "$2" = "$3" ]; then printf '  %-52s ok\n' "$1"
        else printf '  %-52s FAILED (want %s, got %s)\n' "$1" "$2" "$3"; ok=0; fi
    }
    classify() {
        case "$1" in
            *"failed: 0"*|*"all green"*) echo PASS ;;
            NO-LIMACTL|NO-VM|VM-WONT-START|NO-HOST|NO-PODMAN|NO-CONTAINERFILE|NO-WINVM|WINVM-NO-IP|WINVM-NO-SSH|WINVM-UPLOAD-FAILED|NO-WINKVM|WINKVM-NO-SSH|WINKVM-UPLOAD-FAILED|NO-TMP) echo SKIP ;;
            UNCOVERED) echo UNCOVERED ;;
            *) echo FAIL ;;
        esac
    }
    leg "[1] a clean suite run is a PASS"        PASS      "$(classify 'passed: 1065   failed: 0')"
    leg "[2] a FAILING fixture is a FAIL"        FAIL      "$(classify 'passed: 1062   failed: 3')"
    leg "[3] a build failure is a FAIL"          FAIL      "$(classify 'BUILD FAILED')"
    leg "[4] an absent VM is a SKIP, not a pass" SKIP      "$(classify NO-VM)"
    leg "[5] an absent host is a SKIP"           SKIP      "$(classify NO-HOST)"
    leg "[6] no runner is UNCOVERED"             UNCOVERED "$(classify UNCOVERED)"
    leg "[7] empty output is a FAIL, never a pass" FAIL    "$(classify '')"
    leg "[8] a container with no toolchain is a FAIL" FAIL "$(classify 'BUILD FAILED')"
    # Wine must never be spelled as a platform runner.
    if targets | grep -q '	wine	'; then
        leg "[9] wine is not counted as platform coverage" yes no
    else
        leg "[9] wine is not counted as platform coverage" yes yes
    fi
    [ "$ok" = 1 ] && { echo "platform-matrix selfcheck: ok"; exit 0; }
    echo "platform-matrix selfcheck: FAILED"; exit 1
fi

if [ "$LIST" = 1 ]; then
    printf '%-16s %-18s %s\n' PLATFORM RUNNER 'A PASS PROVES'
    targets | while IFS="$(printf '\t')" read -r plat runner proves; do
        printf '%-16s %-18s %s\n' "$plat" "$runner" "$proves"
    done
    exit 0
fi

# ---- fixture set sanity -------------------------------------------------
# `tests/strings.out` has no `tests/strings.ty` -- several goldens are produced
# by other lanes. Naming one here would make a platform leg fail for a reason
# that has nothing to do with the platform, so the list is checked first.
missing=""
for f in $FIXTURES; do
    [ -f "tests/$f.ty" ] && [ -f "tests/$f.out" ] || missing="$missing $f"
done
[ -z "$missing" ] || {
    echo "platform-matrix: fixture(s) with no .ty/.out pair:$missing" >&2
    echo "  A platform leg must fail for a PLATFORM reason. Fix the list." >&2
    exit 2
}
# One line, always: the list is written across two for readability and a raw
# newline inside the payload's `for x in ...` truncates it mid-list.
FIXTURES=$(echo $FIXTURES | tr -s '[:space:]' ' ')
nfix=$(echo $FIXTURES | wc -w | tr -d ' ')

mkdir -p "$ROOT/build"
: > "$OUT.tmp"
rc=0; npass=0; nskip=0; nfail=0

record() {  # platform verdict detail
    printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$(date -u +%Y-%m-%dT%H:%MZ)" "$3" >> "$OUT.tmp"
}

# The payload, as a shell program run on the far side. Kept as one string so
# every runner sends the SAME work -- a per-runner variant is how two lanes end
# up proving different things under one column heading.
payload() {
    if [ "$FULL" = 1 ]; then
        echo 'make -s tychoc >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
              make -s test 2>&1 | tail -3'
    else
        echo 'make -s tychoc >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
              p=0; f=0
              for x in '"$FIXTURES"'; do
                  if ./tychoc "tests/$x.ty" -o "/tmp/pm-$x" >/dev/null 2>&1 \
                     && "/tmp/pm-$x" 2>/dev/null | diff -q - "tests/$x.out" >/dev/null 2>&1
                  then p=$((p+1)); else f=$((f+1)); echo "  FAIL $x"; fi
                  rm -f "/tmp/pm-$x"
              done
              echo "passed: $p   failed: $f"'
    fi
}

run_local() { ( cd "$ROOT" && eval "$(payload)" ) 2>&1; }

run_lima() {  # $1 = vm name
    vm="$1"
    command -v limactl >/dev/null 2>&1 || { echo "NO-LIMACTL"; return; }
    limactl list -q 2>/dev/null | grep -qx "$vm" || { echo "NO-VM"; return; }
    [ "$(limactl list --format '{{.Status}}' "$vm" 2>/dev/null)" = Running ] \
        || limactl start "$vm" >/dev/null 2>&1 \
        || { echo "VM-WONT-START"; return; }
    # Out of the mounted tree, into the guest's own filesystem: a `make` in the
    # shared mount overwrites ./tychoc with a guest-arch binary and breaks the
    # host. `git archive` also means the leg tests a clean checkout, not the
    # working tree's build leftovers.
    limactl shell "$vm" -- bash -c '
        set -e
        rm -rf ~/.tycho-platform && mkdir -p ~/.tycho-platform
        cd '"$ROOT"' && git archive --format=tar HEAD | tar -x -C ~/.tycho-platform
        cd ~/.tycho-platform
        '"$(payload)" 2>&1 </dev/null
}

run_podman() {  # $1 = ssh host that has podman; the image is built from
                #      scripts/platform/Containerfile.linux-x86_64
    h="$1"; img=tycho-linux-x86_64; cf=Containerfile.linux-x86_64
    ssh -o BatchMode=yes -o ConnectTimeout=5 "$h" true </dev/null 2>/dev/null \
        || { echo "NO-HOST"; return; }
    ssh -o BatchMode=yes "$h" 'command -v podman >/dev/null' </dev/null 2>/dev/null \
        || { echo "NO-PODMAN"; return; }
    [ -f "scripts/platform/$cf" ] || { echo "NO-CONTAINERFILE"; return; }
    ssh -o BatchMode=yes "$h" 'mkdir -p ~/.tycho-platform' </dev/null 2>/dev/null \
        || { echo "NO-HOST"; return; }
    # The payload goes over as a FILE. Inlining it meant a shell string nested
    # three deep -- local sh, then ssh's remote sh, then the container's sh --
    # and the middle layer ate it ("payload: not found"). A file has no quoting
    # depth, and it is byte-identical to what every other runner executes.
    pf=$(mktemp) || { echo "NO-TMP"; return; }
    payload > "$pf"
    scp -q "scripts/platform/$cf" "$h:.tycho-platform/$cf" 2>/dev/null || { rm -f "$pf"; echo "NO-CONTAINERFILE"; return; }
    scp -q "$pf" "$h:.tycho-platform/payload.sh" 2>/dev/null || { rm -f "$pf"; echo "NO-HOST"; return; }
    rm -f "$pf"
    # Rebuilt only when the Containerfile changes; podman's layer cache makes a
    # repeat run a no-op. A build failure is a REAL failure, not a skip: the
    # image IS the platform definition, and testing against a stale one is how a
    # lane reports a green describing last month's userland.
    ssh -o BatchMode=yes "$h" \
        "cd ~/.tycho-platform && podman build -q -t $img -f $cf ." </dev/null >/dev/null 2>&1 \
        || { echo "IMAGE BUILD FAILED"; return; }
    # HEAD, not the working tree: the same source the lima leg gets, so the two
    # Linux rows are comparable.
    git archive --format=tar HEAD | ssh -o BatchMode=yes "$h" \
        "rm -rf ~/.tycho-platform/src && mkdir -p ~/.tycho-platform/src && tar -x -C ~/.tycho-platform/src" \
        2>/dev/null || { echo "NO-HOST"; return; }
    ssh -o BatchMode=yes "$h" \
        "podman run --rm -v ~/.tycho-platform/src:/src:Z -v ~/.tycho-platform/payload.sh:/payload.sh:ro,Z -w /src $img sh /payload.sh" \
        </dev/null 2>&1
}

run_winvm() {
    # Windows gets its OWN payload, and that is deliberate. Every other runner
    # executes the shared POSIX payload because every other platform has a
    # POSIX userland; Windows does not. Insisting on parity there means
    # installing git-for-windows for bash/awk/diff and building GNU make, a
    # dependency chain whose failures arrive dressed as platform failures --
    # two hours of exactly that preceded this comment. The guest only has to
    # COMPILE and RUN, and it ships tar.exe and PowerShell inbox, so
    # scripts/platform/win11arm/payload.ps1 does the loop and the only thing
    # installed is a native aarch64 clang.
    command -v utmctl >/dev/null 2>&1 || { echo "NO-WINVM"; return; }
    name="${TYCHO_UTM_VM:-}"
    [ -n "$name" ] || name=$(utmctl list 2>/dev/null | tail -n +2 \
        | sed -E 's/^[0-9A-Fa-f-]+ +[a-z]+ +//' | grep -iE 'win' | head -1)
    [ -n "$name" ] || { echo "NO-WINVM"; return; }
    cold=0
    utmctl status "$name" 2>/dev/null | grep -qi started || {
        cold=1
        utmctl start "$name" >/dev/null 2>&1 || { echo "NO-WINVM"; return; }
    }
    # A COLD Windows boot needs minutes, not seconds: the old 120s window made
    # `platform-check` report WINVM-NO-IP on any stopped VM, which reads as "no
    # Windows here" when the truth is "it was still booting". 6 minutes warm,
    # 10 cold.
    tries=36; [ "$cold" = 1 ] && tries=60
    ip=""; i=0
    while [ $i -lt $tries ]; do
        ip=$(utmctl ip-address "$name" 2>/dev/null \
             | grep -Eo '^[0-9]+(\.[0-9]+){3}$' | grep -v '^127' | head -1)
        [ -n "$ip" ] && break
        i=$((i + 1)); sleep 10
    done
    # utmctl reads the address from the guest agent, which can be absent or
    # slow even once sshd is listening. The last address that worked is cached,
    # so fall back to probing it directly rather than declaring the VM
    # unreachable because one reporting channel is quiet.
    cache="$ROOT/build/winvm-last-ip"
    if [ -z "$ip" ] && [ -f "$cache" ]; then
        last=$(cat "$cache")
        ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
            -o LogLevel=ERROR -o BatchMode=yes -o ConnectTimeout=5 \
            "tycho@$last" 'echo ok' >/dev/null 2>&1 && ip="$last"
    fi
    [ -n "$ip" ] || { echo "WINVM-NO-IP"; return; }
    mkdir -p "$ROOT/build" && printf '%s' "$ip" > "$cache"
    SSHO="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o BatchMode=yes"
    # shellcheck disable=SC2086
    ssh $SSHO "tycho@$ip" 'echo ok' >/dev/null 2>&1 || { echo "WINVM-NO-SSH"; return; }

    W="$(mktemp -d)" || { echo "NO-TMP"; return; }
    # build/tycho_rt_embed.h is generated by the Makefile and is NOT in git, so
    # `git archive` alone ships a tree that cannot compile. The guest has no
    # make to regenerate it, so it goes in the tarball.
    make -s build/tycho_rt_embed.h >/dev/null 2>&1
    git archive --format=tar HEAD -o "$W/tree.tar"
    tar -rf "$W/tree.tar" build/tycho_rt_embed.h 2>/dev/null
    cp "$here_win/payload.ps1" "$W/payload.ps1"
    ( cd "$W" && tar -rf tree.tar payload.ps1 ) 2>/dev/null
    gzip -c "$W/tree.tar" > "$W/tree.tgz"
    # shellcheck disable=SC2086
    ssh $SSHO "tycho@$ip" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\tycho -EA SilentlyContinue; New-Item -ItemType Directory -Force C:\tycho|Out-Null"' >/dev/null 2>&1
    # shellcheck disable=SC2086
    scp -q $SSHO "$W/tree.tgz" "tycho@$ip:C:/tycho/tree.tgz" 2>/dev/null \
        || { rm -rf "$W"; echo "WINVM-UPLOAD-FAILED"; return; }
    rm -rf "$W"
    # shellcheck disable=SC2086
    ssh $SSHO "tycho@$ip" 'cd C:\tycho && tar -xzf tree.tgz' >/dev/null 2>&1
    # The fixture list goes over as a FILE. As an environment variable it had to
    # survive sh -> ssh -> cmd -> PowerShell quoting and did not: the value
    # collapsed, the subset was ignored and the lane quietly ran EVERY fixture,
    # reporting a pass for different work than the row beside it.
    if [ "$FULL" = 1 ]; then
        # shellcheck disable=SC2086
        ssh $SSHO "tycho@$ip" 'powershell -NoProfile -Command "Remove-Item C:\tycho\fixtures.txt -EA SilentlyContinue"' >/dev/null 2>&1
    else
        printf '%s' "$FIXTURES" | ssh $SSHO "tycho@$ip" 'powershell -NoProfile -Command "$input | Set-Content C:\tycho\fixtures.txt"' >/dev/null 2>&1
    fi
    # No `&` either: cmd.exe rejects a bare ampersand before PowerShell sees it.
    # shellcheck disable=SC2086
    ssh $SSHO "tycho@$ip" \
        'powershell -NoProfile -ExecutionPolicy Bypass -Command "cd C:\tycho; .\payload.ps1"' 2>&1
}

run_winkvm() {  # $1 = ssh host running the KVM Windows VM
    h="$1"; R="~/tycho-platform/win11x64"
    ssh -o BatchMode=yes -o ConnectTimeout=5 "$h" true </dev/null 2>/dev/null \
        || { echo "NO-HOST"; return; }
    ssh -o BatchMode=yes "$h" "sh $R/vm.sh status 2>/dev/null | grep -q 'qemu: running'" </dev/null 2>/dev/null \
        || ssh -o BatchMode=yes "$h" "sh $R/vm.sh start" </dev/null >/dev/null 2>&1 \
        || { echo "NO-WINKVM"; return; }
    i=0
    while [ $i -lt 30 ]; do
        ssh -o BatchMode=yes "$h" "sh $R/vm.sh status 2>/dev/null | grep -q 'ssh:  up'" </dev/null 2>/dev/null && break
        i=$((i + 1)); sleep 10
    done
    [ $i -lt 30 ] || { echo "WINKVM-NO-SSH"; return; }
    # Same two-hop shape as the podman runner: the tree is streamed to the
    # Linux host, then into the guest over its forwarded ssh port. The embed
    # header is generated here because the guest has no make.
    W="$(mktemp -d)" || { echo "NO-TMP"; return; }
    W2fx="$W/fixtures.txt"
    make -s build/tycho_rt_embed.h >/dev/null 2>&1
    git archive --format=tar HEAD -o "$W/tree.tar"
    tar -rf "$W/tree.tar" build/tycho_rt_embed.h 2>/dev/null
    cp "$ROOT/scripts/platform/win11x64/payload.ps1" "$W/payload.ps1"
    ( cd "$W" && tar -rf tree.tar payload.ps1 ) 2>/dev/null
    gzip -c "$W/tree.tar" > "$W/tree.tgz"
    scp -q "$W/tree.tgz" "$h:vm/win11x64/tree.tgz" 2>/dev/null \
        || { rm -rf "$W"; echo "WINKVM-UPLOAD-FAILED"; return; }
    # Written BEFORE the temp dir is removed. It was after, and the lane died on
    # "No such file or directory" for a path it had just deleted.
    [ "$FULL" = 1 ] && : > "$W2fx" || printf '%s' "$FIXTURES" > "$W2fx"
    scp -q "$W2fx" "$h:vm/win11x64/fixtures.txt" 2>/dev/null
    rm -rf "$W"
    scp -q "$ROOT/scripts/platform/win11x64/remote_run.sh" "$h:vm/win11x64/remote_run.sh" 2>/dev/null
    # bash -s, not the login shell: that box runs zsh, which does not word-split
    # an unquoted variable, so an inline `$G "cmd"` became one filename.
    ssh -o BatchMode=yes "$h" 'bash ~/vm/win11x64/remote_run.sh' </dev/null 2>&1
}

run_ssh() {  # $1 = host alias
    h="$1"
    ssh -o BatchMode=yes -o ConnectTimeout=5 "$h" true </dev/null 2>/dev/null || { echo "NO-HOST"; return; }
    tar czf - --exclude=.git --exclude=build --exclude=dist . 2>/dev/null \
      | ssh "$h" 'rm -rf ~/.tycho-platform && mkdir -p ~/.tycho-platform \
                  && tar xzf - -C ~/.tycho-platform \
                  && cd ~/.tycho-platform && '"$(payload)" 2>&1
}

echo "=============================================================="
echo " tycho platform matrix   ($(uname -s)/$(uname -m), $(date -u +%Y-%m-%d))"
if [ "$FULL" = 1 ]; then echo " payload: make test (the whole suite)"
else echo " payload: build tychoc + $nfix fixtures against their goldens"; fi
echo "=============================================================="

targets | while IFS="$(printf '\t')" read -r plat runner proves; do
    [ -z "$ONLY" ] || [ "$ONLY" = "$plat" ] || continue
    printf '\n>>> %s   [%s]\n' "$plat" "$runner"
    case "$runner" in
        local)   out=$(run_local) ;;
        lima:*)  out=$(run_lima "${runner#lima:}") ;;
        podman:*) out=$(run_podman "${runner#podman:}") ;;
        winvm)   out=$(run_winvm) ;;
        winkvm:*) out=$(run_winkvm "${runner#winkvm:}") ;;
        ssh:*)   out=$(run_ssh "${runner#ssh:}") ;;
        none)    out="UNCOVERED" ;;
        *)       out="BAD-RUNNER" ;;
    esac
    echo "$out" | sed 's/^/    /'
    case "$out" in
        *"failed: 0"*|*"all green"*)
            echo "    PASS -- $proves"; record "$plat" PASS "$proves" ;;
        NO-LIMACTL|NO-VM|VM-WONT-START|NO-HOST|NO-PODMAN|NO-CONTAINERFILE|NO-WINVM|WINVM-NO-IP|WINVM-NO-SSH|WINVM-UPLOAD-FAILED|NO-WINKVM|WINKVM-NO-SSH|WINKVM-UPLOAD-FAILED|NO-TMP)
            echo "    SKIP -- runner unreachable from this machine ($out)"
            record "$plat" SKIP "runner unreachable: $out" ;;
        UNCOVERED)
            echo "    UNCOVERED -- $proves"
            record "$plat" UNCOVERED "$proves" ;;
        *)  echo "    FAIL"; record "$plat" FAIL "see output above" ;;
    esac
done

mv "$OUT.tmp" "$OUT"
npass=$(grep -c "	PASS	" "$OUT" 2>/dev/null | head -1 | tr -d " \n"); : "${npass:=0}"
nfail=$(grep -c "	FAIL	" "$OUT" 2>/dev/null | head -1 | tr -d " \n"); : "${nfail:=0}"
nskip=$(grep -c "	SKIP	" "$OUT" 2>/dev/null | head -1 | tr -d " \n"); : "${nskip:=0}"
nunc=$(grep -c "	UNCOVERED	" "$OUT" 2>/dev/null | head -1 | tr -d " \n"); : "${nunc:=0}"
ntot=$(targets | wc -l | tr -d ' ')

echo
echo "=============================================================="
printf ' %d of %d shipped platforms EXECUTED here: %d pass, %d fail, %d skip, %d uncovered\n' \
    "$npass" "$ntot" "$npass" "$nfail" "$nskip" "$nunc"
echo " verdicts -> build/platform-matrix.tsv (release_cross.sh reads this)"
echo "=============================================================="

[ "$nfail" = 0 ] || exit 1
if [ "$STRICT" = 1 ] && [ "$((nskip + nunc))" != 0 ]; then
    echo "platform-matrix: --strict and $((nskip + nunc)) platform(s) were not executed" >&2
    exit 1
fi
exit 0
