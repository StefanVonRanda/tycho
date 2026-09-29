#!/bin/sh
# D31: a large append-only procedure table must not make lookup quadratic.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$tmp/procs.ty" <<'PY'
import sys
with open(sys.argv[1], "w") as f:
    n = 40000
    for i in range(n - 1):
        f.write(f"fn proc_{i}() -> int:\n    return proc_{i + 1}()\n")
    f.write(f"fn proc_{n - 1}() -> int:\n    return 0\n")
    f.write("fn main():\n    println(str(proc_0()))\n")
PY
python3 - "$tmp" <<'PY'
import subprocess, sys
from pathlib import Path
p = Path(sys.argv[1])
try:
    subprocess.run(
        ["./tychoc", str(p / "procs.ty"), "--emit-c", "-o", str(p / "out")],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=20,
    )
except subprocess.TimeoutExpired:
    raise SystemExit("proc-signature-scale: 40,000-procedure compile exceeded 20 seconds")
if not (p / "out.c").is_file():
    raise SystemExit("proc-signature-scale: compiler did not emit C")
print("proc-signature-scale: 40,000-procedure chain compiled within 20 seconds")
PY
