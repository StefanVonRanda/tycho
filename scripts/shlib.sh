# Shared shell helpers for the gates. Source it AFTER the `cd` to the repo
# root, which every caller already does:
#
#     cd "$(dirname "$0")/../.." || exit 2
#     . ./scripts/shlib.sh
#
# Nothing here runs anything or prints anything; it only defines what is
# missing on the host.

# ---------------------------------------------------------------------------
# `timeout`
#
# GNU coreutils' timeout is not in the macOS base system, and nine gates called
# `timeout 10 ...` unguarded. Every one of those calls exited 127 (command not
# found) on a Mac, so the command under test NEVER RAN -- and the legs that
# then read its output scored an empty file or a missing one. tycho-snap is
# what surfaced it: "empty archive is  bytes", "an unknown option exited 127,
# expected 2". A gate that cannot run the program it is gating must not report
# on it, so this defines the name rather than leaving each call to fail.
#
# Order of preference:
#   1. a real GNU timeout (the --version probe rejects cmd.exe's timeout on
#      Windows, which takes a different argument entirely)
#   2. gtimeout, which is what Homebrew's coreutils installs it as
#   3. perl's alarm -- present on every macOS, and it keeps the BOUND, which
#      is the whole reason these calls have a timeout: a gate that hangs is
#      worse than one that fails
#   4. no bound at all, as the last resort
if command -v timeout > /dev/null 2>&1 && timeout --version > /dev/null 2>&1; then
    :
elif command -v gtimeout > /dev/null 2>&1; then
    timeout() { gtimeout "$@"; }
elif command -v perl > /dev/null 2>&1; then
    # exec keeps the child's exit status; alarm makes SIGALRM kill it at the
    # bound, which surfaces as 142 rather than GNU timeout's 124. Callers here
    # assert "not zero", never the specific 124.
    timeout() { _tmo_d=$1; shift; perl -e 'alarm shift; exec @ARGV or exit 127' "$_tmo_d" "$@"; }
else
    timeout() { shift; "$@"; }
fi

# ---------------------------------------------------------------------------
# `openssl`
#
# macOS ships LibreSSL under the name `openssl`, and LibreSSL's s_server has no
# -naccept. Three gates start a test server with it, so on a Mac the server
# never came up and tls-verify, http-verify and handle-guard's tls leg all
# SKIPPED -- on a box where a real OpenSSL 3 was installed the whole time and
# pkg-config was already finding its libraries. Resolve the CLI that matches
# what the shims link against instead of taking the first name on PATH.
#
# Sets OPENSSL to a binary whose s_server understands -naccept, or leaves it
# empty for the caller to skip on.
_ossl_ok() { [ -x "$1" ] && "$1" s_server -help 2>&1 | grep -q naccept; }
ossl_cli() {
    for _c in \
        "$(pkg-config --variable=exec_prefix openssl 2>/dev/null)/bin/openssl" \
        "$(brew --prefix openssl@3 2>/dev/null)/bin/openssl" \
        /opt/homebrew/opt/openssl@3/bin/openssl \
        /usr/local/opt/openssl@3/bin/openssl \
        "$(command -v openssl 2>/dev/null)"
    do
        if _ossl_ok "$_c"; then printf '%s\n' "$_c"; return 0; fi
    done
    return 1
}
