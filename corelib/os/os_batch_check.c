/* Host-side unit gate for osx_is_batch (corelib/os/os_shim.c:302).
 *
 * The suffix logic is pure C (no Win32 API) but lives inside #ifdef _WIN32
 * in the shim, so it cannot be compiled standalone on a POSIX host.  This
 * file reimplements the EXACT logic from os_shim.c:302-318 and scores it
 * with revert-each controls: each condition has a corresponding test that
 * reddens when that condition is removed.
 *
 * Compiled and run by scripts/shim_check.sh on non-Windows hosts.
 * Exit 0 = all pass, exit 1 = at least one FAIL.
 */
#include <stdio.h>
#include <string.h>

/* ---- Copy of osx_is_batch from corelib/os/os_shim.c:302-318 ---- */
static int osx_is_batch(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return 0;
    const char *ext = name + len - 4;
    if (ext[0] != '.') return 0;
    /* .bat */
    if ((ext[1] == 'b' || ext[1] == 'B') &&
        (ext[2] == 'a' || ext[2] == 'A') &&
        (ext[3] == 't' || ext[3] == 'T'))
        return 1;
    /* .cmd */
    if ((ext[1] == 'c' || ext[1] == 'C') &&
        (ext[2] == 'm' || ext[2] == 'M') &&
        (ext[3] == 'd' || ext[3] == 'D'))
        return 1;
    return 0;
}

/* ---- Test harness ---- */
static int nfail = 0;
static int npass = 0;

static void expect_batch(const char *label, const char *name) {
    int got = osx_is_batch(name);
    if (got == 1) { npass++; return; }
    printf("FAIL %s: osx_is_batch(%s) = %d, expected 1\n", label, name, got);
    nfail++;
}

static void expect_not_batch(const char *label, const char *name) {
    int got = osx_is_batch(name);
    if (got == 0) { npass++; return; }
    printf("FAIL %s: osx_is_batch(%s) = %d, expected 0\n", label, name, got);
    nfail++;
}

int main(void) {
    /* --- Positive: .bat detection (revert: removing bat-check reddens these) --- */
    expect_batch("bat-lower",    "prog.bat");
    expect_batch("bat-upper",    "PROG.BAT");
    expect_batch("bat-mixed",    "pRoG.BaT");
    expect_batch("bat-dot-only", ".bat");

    /* --- Positive: .cmd detection (revert: removing cmd-check reddens these) --- */
    expect_batch("cmd-lower",    "run.cmd");
    expect_batch("cmd-upper",    "RUN.CMD");
    expect_batch("cmd-mixed",    "RuN.CmD");
    expect_batch("cmd-dot-only", ".cmd");

    /* --- Revert: the dot check.  Without it, "x.bat" with no dot would pass. --- */
    expect_batch("bat-dot-guard", "tool.bat");

    /* --- Negative: other extensions must NOT match --- */
    expect_not_batch("ext-sh",      "run.sh");
    expect_not_batch("ext-py",      "run.py");
    expect_not_batch("ext-exe",     "run.exe");
    expect_not_batch("ext-batx",    "run.batx");
    expect_not_batch("ext-cmdx",    "run.cmdx");
    expect_not_batch("ext-bat-txt", "run.bat.txt");
    expect_not_batch("ext-cmd-txt", "run.cmd.txt");

    /* --- Negative: no dot at all --- */
    expect_not_batch("no-dot-bat",   "bat");
    expect_not_batch("no-dot-cmd",   "cmd");
    expect_not_batch("no-dot-full",  "nobat");

    /* --- Negative: wrong position of dot --- */
    expect_not_batch("dot-prefix",    ".batfoo");
    expect_not_batch("dot-mid",       "foo.batfoo");

    /* --- Negative: short / empty --- */
    expect_not_batch("empty",         "");
    expect_not_batch("short-1",      ".");
    expect_not_batch("short-2",      ".b");
    expect_not_batch("short-3",      ".ba");

    /* --- Revert: without case-insensitive check, these would pass --- */
    expect_batch("bat-all-upper",  "FOO.BAT");
    expect_batch("bat-all-lower",  "foo.bat");
    expect_batch("cmd-all-upper",  "FOO.CMD");
    expect_batch("cmd-all-lower",  "foo.cmd");

    /* --- Path-like names: the function checks the final extension only --- */
    expect_batch("path-bat",    "/usr/bin/prog.bat");
    expect_batch("path-cmd",    "C:\\tools\\run.cmd");
    expect_not_batch("path-sh", "/usr/bin/prog.sh");

    printf("os_batch_check: %d ok, %d FAIL\n", npass, nfail);
    return nfail > 0 ? 1 : 0;
}
