# Diagnose and repair the ssh path on the windows-arm64 agent.
# provision.ps1 withheld its marker, which means one of clang/git/make is
# missing -- but the FIRST thing to establish is whether the host can get in at
# all, because everything after this is scripted over ssh.
$ErrorActionPreference = 'Continue'
function Say($m) { Write-Host "  $m" }

Write-Host "=== provision log (tail) ==="
if (Test-Path C:\provision.log) { Get-Content C:\provision.log -Tail 12 } else { Say 'no log' }

Write-Host "=== sshd ==="
$cap = (Get-WindowsCapability -Online -Name OpenSSH.Server* | Select-Object -First 1)
Say "capability: $($cap.State)"
if ($cap.State -ne 'Installed') {
    Say 'installing OpenSSH.Server'
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null
}
Set-Service -Name sshd -StartupType Automatic -ErrorAction SilentlyContinue
Start-Service sshd -ErrorAction SilentlyContinue
Say "service:    $((Get-Service sshd -ErrorAction SilentlyContinue).Status)"
Say "listening:  $((Get-NetTCPConnection -LocalPort 22 -State Listen -ErrorAction SilentlyContinue | Measure-Object).Count) socket(s)"

Write-Host "=== firewall ==="
# The capability normally adds this rule, but not when the capability install
# is the thing that failed. A DROP looks like a timeout from the host, which is
# indistinguishable from "no sshd" -- so open it explicitly and say so.
if (-not (Get-NetFirewallRule -Name 'tycho-sshd' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'tycho-sshd' -DisplayName 'tycho sshd' -Enabled True `
        -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow -Profile Any | Out-Null
    Say 'added inbound allow on 22'
} else { Say 'rule already present' }

Write-Host "=== key ==="
$aak = 'C:\ProgramData\ssh\administrators_authorized_keys'
$pub = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILWt/JgbUyZK55oi5nn6V/frYsGOELDtyDGnCRHLElDZ claude-code@igzo-mac'
New-Item -ItemType Directory -Force -Path 'C:\ProgramData\ssh' | Out-Null
Set-Content -Path $aak -Value $pub -Encoding ascii
icacls $aak /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
Say "authorized_keys: $((Get-Content $aak).Length) byte(s)"

Write-Host "=== toolchain ==="
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine')
foreach ($t in @('clang','git','make','cc')) {
    $c = Get-Command $t -ErrorAction SilentlyContinue
    Say ("{0,-6} {1}" -f $t, $(if ($c) { $c.Source } else { 'MISSING' }))
}
Write-Host "=== ip ==="
(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' }).IPAddress | ForEach-Object { Say $_ }
