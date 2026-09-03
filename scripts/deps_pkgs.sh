# pkgs_of <deps-file> -- the pkg-config package names a corelib package needs.
#
# Everything from the `_WIN32:` marker onward is RAW Windows link flags, not
# pkg-config packages (corelib/tls/deps documents this, and src/tychoc.c's own
# reader skips the section on a non-Windows host). Feeding them to pkg-config
# reports them "missing" and skips a shim that builds fine: that is exactly what
# shim_check.sh did to net, regex, signal and tls until 2026-09-03.
#
# Sourced by scripts/shim_check.sh and scripts/shim_warn.sh so there is ONE
# spelling of this parse; two is how the bug above survived.
pkgs_of() {
    sed -e 's/#.*//' -e '/_WIN32:/,$d' "$1" | grep -vE '^[[:space:]]*$' || true
}
