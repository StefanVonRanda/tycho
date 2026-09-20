# First-logon provisioning for the windows-x86_64 platform leg.
# Runs once, unattended, from autounattend.xml's FirstLogonCommands.
#
# Goal: make this VM reachable and buildable the way every other runner is --
# ssh in, run the payload, read the verdict. Nothing here is interactive, so
# rebuilding the VM is `make platform-vm-win` and a wait, not an afternoon.

$ErrorActionPreference = 'Stop'
$log = 'C:\provision.log'
function Say($m) { "$((Get-Date).ToString('s'))  $m" | Tee-Object -FilePath $log -Append }

Say 'provisioning windows-x86_64 build agent'

# ---- OpenSSH server -------------------------------------------------------
# The matrix reaches every platform over ssh. Windows ships the server as an
# optional capability; enabling it is what makes this box a runner rather than
# a desktop somebody has to remote into.
Say 'installing OpenSSH server'
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null
Set-Service -Name sshd -StartupType Automatic
Start-Service sshd

# Key auth only. The local password exists so the account is valid; it is not
# the credential, and a test VM with password ssh enabled is a liability even
# on a host-only network.
# EVERY host that will drive this VM, not just the one that built it. The
# x64 guest is reached over a loopback forward ON the Linux box, so the
# client is that box and not this Mac; a guest holding only the Mac's key
# refuses with `Permission denied (publickey)` while the file, the ACL and
# sshd_config are all provably correct.
$keys = @(
    'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILWt/JgbUyZK55oi5nn6V/frYsGOELDtyDGnCRHLElDZ claude-code@igzo-mac',
    'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIHwtsdL4+kHSwcAxRdBLJL8xYivQYyaOJUUmysY4G8gB sudotgm@gmail.com'
)
# An ADMIN user's keys live in administrators_authorized_keys, not in
# ~/.ssh/authorized_keys -- sshd_config ships a Match Group administrators
# block that redirects there, and a key in the home directory is silently
# ignored. That one line is the usual reason "ssh works for nobody".
$aak = 'C:\ProgramData\ssh\administrators_authorized_keys'
[System.IO.File]::WriteAllBytes($aak,
    [System.Text.Encoding]::ASCII.GetBytes(($keys -join "`n") + "`n"))
icacls $aak /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
Say 'ssh key installed'

# The capability is SUPPOSED to add this rule and did not on either Windows
# guest. A firewall DROP presents to the host as a connection timeout, which
# looks exactly like "sshd is not running" -- hours went into that confusion on
# the arm64 box. Adding it explicitly costs nothing when it is already there.
if (-not (Get-NetFirewallRule -Name 'tycho-sshd' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'tycho-sshd' -DisplayName 'tycho sshd' -Enabled True `
        -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow -Profile Any | Out-Null
    Say 'firewall: inbound 22 allowed'
}

# Default shell: PowerShell would need every payload quoted twice. The payload
# is plain POSIX sh, so point sshd at the bash that ships with git -- but ONLY
# IF IT EXISTS. Setting this unconditionally bricks ssh in a way that looks
# like an auth problem: the login succeeds, the shell fails to start, and the
# client sees an empty session with no error. That cost an hour of chasing key
# permissions while the keys were fine all along. cmd.exe is always present, so
# an absent bash means leave sshd's default alone.

$bash = 'C:\Program Files\Git\bin\bash.exe'
if (Test-Path $bash) {
    New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell `
        -Value $bash -PropertyType String -Force | Out-Null
    Say 'sshd default shell -> git bash'
} else {
    Remove-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell -ErrorAction SilentlyContinue
    Say 'git bash absent -- leaving sshd on cmd.exe'
}

# ---- toolchain ------------------------------------------------------------
# llvm-mingw, not MSYS2: MSYS2 is x86_64 and would run under Windows' x64
# emulation, which would make this lane measure the emulator rather than
# x86_64 Windows. llvm-mingw ships a NATIVE aarch64 clang that targets
# x86_64 Windows, so the binaries under test are the ones a user would get.
$ErrorActionPreference = 'Continue'
# Git for Windows, ARM64 build, installed DIRECTLY rather than through winget.
# winget exited 0 and installed nothing on this image (no MSStore source in a
# fresh unattended install, and no interactive session to repair it), which is
# the worst failure shape: a silent success. The release asset is named, so a
# 404 is a loud failure instead.
Say 'installing git (x64)'
$gitver = '2.55.0.5'
$gitexe = "$env:TEMP\git-arm64.exe"
$giturl = "https://github.com/git-for-windows/git/releases/download/" +
          "v$gitver.windows.5/Git-$gitver-64-bit.exe"
try {
    Invoke-WebRequest -Uri $giturl -OutFile $gitexe -UseBasicParsing
    # /VERYSILENT is the Inno Setup switch; without /NORESTART it can reboot
    # the agent out from under the ssh session that started it.
    Start-Process -FilePath $gitexe -Wait -ArgumentList `
        '/VERYSILENT','/NORESTART','/NOCANCEL','/SP-','/SUPPRESSMSGBOXES',
        '/COMPONENTS=gitlfs,assoc_sh'
    Say 'git installed'
} catch { Say "git install FAILED: $_" }

Say 'installing llvm-mingw'

$tc  = 'llvm-mingw-20250910-ucrt-x86_64'
$url = "https://github.com/mstorsjo/llvm-mingw/releases/download/20250910/$tc.zip"
try {
    Invoke-WebRequest -Uri $url -OutFile "C:\$tc.zip" -UseBasicParsing
    Expand-Archive -Path "C:\$tc.zip" -DestinationPath 'C:\' -Force
    Rename-Item "C:\$tc" 'C:\llvm-mingw' -ErrorAction SilentlyContinue
    Say 'llvm-mingw installed'
} catch { Say "llvm-mingw download FAILED: $_" }

# PATH for every future non-interactive ssh session. `cc` is what the tree
# invokes; llvm-mingw names it clang, so a shim supplies the name.
$paths = 'C:\llvm-mingw\bin;C:\Program Files\Git\bin;C:\Program Files\Git\usr\bin'
$cur = [Environment]::GetEnvironmentVariable('Path','Machine')
[Environment]::SetEnvironmentVariable('Path', "$cur;$paths", 'Machine')
New-Item -ItemType Directory -Force -Path 'C:\bin' | Out-Null
# An ARRAY, not a string with `r`n in it: PowerShell single quotes are LITERAL,
# so '@echo off`r`nclang %*' writes those six characters and produces a cc.cmd
# that does nothing. Set-Content joins array elements with a real newline.
Set-Content -Path 'C:\bin\cc.cmd' -Value @('@echo off', 'clang %*') -Encoding ascii
[Environment]::SetEnvironmentVariable('Path',
    [Environment]::GetEnvironmentVariable('Path','Machine') + ';C:\bin', 'Machine')

# ---- prove it before claiming it -----------------------------------------
# The marker is what the platform matrix waits on, so it must mean "this box
# can build", not "the script reached the end". Writing it unconditionally is
# how a lane starts reporting a missing compiler as a language failure.
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine')
# CLANG ONLY. git and make were required back when this guest ran the same
# POSIX payload as the Linux runners; scripts/platform/*/payload.ps1 replaced
# that and needs nothing but a compiler and the inbox tar.exe. Keeping git in
# the gate meant a flaky 63 MB download could withhold the marker from a box
# that was perfectly able to build.
$ok = $true
if (-not (Get-Command clang -ErrorAction SilentlyContinue)) { Say 'MISSING: clang'; $ok = $false }
foreach ($t in @('git', 'make')) {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { Say "absent (not required): $t" }
}
if ($ok) {
    Set-Content -Path 'C:\provisioned' -Value 'ok' -Encoding ascii
    Say 'provisioning complete -- toolchain verified'
} else {
    Say 'provisioning INCOMPLETE -- marker withheld on purpose'
}
