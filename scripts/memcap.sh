#!/bin/sh
# Cap a command tree at MEMCAP_MB (default 8192, 0 disables) in a cgroup: a
# soft RLIMIT_AS would break ASan's shadow mapping. Every make recipe runs
# through it, and ci.sh calls it again to scope each forked lane.
set -u

MB=${MEMCAP_MB:-8192}
case $MB in
    ''|*[!0-9]*) echo "memcap: MEMCAP_MB must be a non-negative integer (got '$MB')" >&2; exit 2 ;;
esac

capped=0
if [ "$MB" -gt 0 ] && [ -z "${MEMCAP_APPLIED:-}" ] &&
   [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -S "$XDG_RUNTIME_DIR/systemd/private" ] &&
   command -v systemd-run >/dev/null 2>&1; then
    capped=1
fi

# make invokes its SHELL as `sh -c 'command'`.
if [ "${1:-}" = "-c" ]; then
    shift
    set -- /bin/sh -c "${1:-}"
fi

if [ "$capped" = 1 ]; then
    # Named explicitly: systemd-run's generated scope name is derived from the
    # caller's pid, so an inner scope (ci.sh lane inside a make recipe scope)
    # collides with the outer one and fails to start. $$ alone is not enough --
    # a shell may exec the wrapper and keep the pid -- so add randomness. This
    # costs nothing on the recipe path, which inherits MEMCAP_APPLIED.
    unit="tycho-memcap-$$-$(od -An -N4 -tu4 /dev/urandom 2>/dev/null | tr -d '[:space:]')"
    exec systemd-run --user --scope --quiet --collect --unit="$unit" \
        -p "MemoryMax=${MB}M" -p MemorySwapMax=0 \
        -- env MEMCAP_APPLIED=1 "$@"
fi

MEMCAP_APPLIED=1; export MEMCAP_APPLIED
exec "$@"
