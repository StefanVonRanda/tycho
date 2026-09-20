#!/usr/bin/env python3
"""Type into a QEMU guest through the monitor socket.

Needed when a Windows guest has a desktop but no sshd: the keyboard is the
only way in. Used to bootstrap a VM whose unattended provisioning did not run.

THE MAP MUST BE COMPLETE. An unmapped character was previously skipped with a
warning on stderr that nobody read, so the guest received a command missing
its pipes and ampersands and then failed for reasons that looked like
anything but a typo -- `ipconfig | Select-String` arrived as
`ipconfig Select-String` and printed usage, which reads as a broken guest
rather than a broken typist. Anything unmapped is now a hard error.

  type.py [@KEY | TEXT]...     @KEY sends a raw qemu key (e.g. @ret, @ctrl-a)
"""
import socket, sys, time

SOCK = "/home/igzo/vm/win11x64/monitor.sock"
BASE = "abcdefghijklmnopqrstuvwxyz0123456789"
SHIFTED = {  # US layout
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", "|": "backslash",
    ":": "semicolon", '"': "apostrophe", "<": "comma", ">": "dot",
    "?": "slash", "~": "grave_accent",
}
PLAIN = {
    " ": "spc", "-": "minus", "=": "equal", "[": "bracket_left",
    "]": "bracket_right", "\\": "backslash", ";": "semicolon",
    "'": "apostrophe", ",": "comma", ".": "dot", "/": "slash",
    "`": "grave_accent",
}
M = {c: c for c in BASE}
M.update({c.upper(): "shift-" + c for c in "abcdefghijklmnopqrstuvwxyz"})
M.update(PLAIN)
M.update({k: "shift-" + v for k, v in SHIFTED.items()})


def main(argv):
    if "--selfcheck" in argv:
        need = " !\"#$%&'()*+,-./0123456789:;<=>?@[\\]^_`{|}~"
        missing = [c for c in need if c not in M]
        print("type selfcheck:", "ok" if not missing else "MISSING %r" % missing)
        return 0 if not missing else 1
    s = socket.socket(socket.AF_UNIX)
    s.connect(SOCK)
    bad = []
    for tok in argv:
        if tok.startswith("@"):
            s.sendall(("sendkey %s\n" % tok[1:]).encode()); time.sleep(0.4); continue
        for ch in tok:
            if ch in M:
                s.sendall(("sendkey %s\n" % M[ch]).encode()); time.sleep(0.05)
            else:
                bad.append(ch)
    time.sleep(0.3); s.close()
    if bad:
        print("UNMAPPED, COMMAND CORRUPTED: %r" % bad, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
