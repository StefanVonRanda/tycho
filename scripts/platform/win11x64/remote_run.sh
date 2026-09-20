# Drive the Windows x64 guest from the KVM host. Run ON that host.
#
# A SHIPPED SCRIPT, not an inline ssh string. The inline version had to survive
# sh -> ssh -> the remote login shell -> cmd.exe -> PowerShell, and it lost:
# the remote shell is zsh, which does not word-split an unquoted variable, so
# `$G "cmd"` became a single filename and the lane failed with "no such file or
# directory" naming the whole ssh command line. A file has one level of
# quoting and the remote shell never parses it.
#
#   remote_run.sh          run the fixture list in fixtures.txt (all if absent)
set -u
VM=~/vm/win11x64
G="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
G="$G -o LogLevel=ERROR -o BatchMode=yes -p 2223 tycho@127.0.0.1"
S="scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
S="$S -o LogLevel=ERROR -P 2223"

$G 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\tycho -EA SilentlyContinue; New-Item -ItemType Directory -Force C:\tycho|Out-Null"' >/dev/null 2>&1
$S "$VM/tree.tgz" tycho@127.0.0.1:C:/tycho/tree.tgz || { echo "UPLOAD FAILED"; exit 1; }
$G 'cd C:\tycho && tar -xzf tree.tgz' >/dev/null 2>&1

if [ -s "$VM/fixtures.txt" ]; then
    $S "$VM/fixtures.txt" tycho@127.0.0.1:C:/tycho/fixtures.txt >/dev/null 2>&1
else
    $G 'powershell -NoProfile -Command "Remove-Item C:\tycho\fixtures.txt -EA SilentlyContinue"' >/dev/null 2>&1
fi

# No `&`: cmd.exe rejects a bare ampersand before PowerShell ever sees it.
$G 'powershell -NoProfile -ExecutionPolicy Bypass -Command "cd C:\tycho; .\payload.ps1"' 2>&1
