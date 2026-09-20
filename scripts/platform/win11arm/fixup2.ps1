# sshd answers and refuses the key. Narrow it down and fix the usual causes.
$ErrorActionPreference = 'Continue'
function Say($m) { Write-Host "  $m" }
$pub = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILWt/JgbUyZK55oi5nn6V/frYsGOELDtyDGnCRHLElDZ claude-code@igzo-mac'

Write-Host "=== who am i ==="
Say "user:  $env:USERNAME"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Say "admin: $isAdmin"
# WHICH file sshd reads depends on this: an Administrators member is served by
# administrators_authorized_keys and its ~/.ssh/authorized_keys is IGNORED.
# A non-admin is the exact opposite. Getting this backwards is the usual reason
# a correct key is refused.
Say "groups: $((net localgroup Administrators | Select-String -SimpleMatch $env:USERNAME) -join ',')"

Write-Host "=== sshd_config directives ==="
Get-Content C:\ProgramData\ssh\sshd_config |
    Select-String -Pattern '^\s*(AuthorizedKeysFile|PubkeyAuthentication|Match|AllowUsers|DenyUsers)' |
    ForEach-Object { Say $_.Line.Trim() }

Write-Host "=== install key in BOTH locations ==="
$aak = 'C:\ProgramData\ssh\administrators_authorized_keys'
Set-Content -Path $aak -Value $pub -Encoding ascii -NoNewline
Add-Content -Path $aak -Value "`n" -Encoding ascii
icacls $aak /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
Say "admin file: $((Get-Item $aak).Length) bytes"

$ud = "$env:USERPROFILE\.ssh"
New-Item -ItemType Directory -Force -Path $ud | Out-Null
$uak = "$ud\authorized_keys"
Set-Content -Path $uak -Value $pub -Encoding ascii -NoNewline
Add-Content -Path $uak -Value "`n" -Encoding ascii
icacls $uak /inheritance:r /grant "$($env:USERNAME):F" /grant 'SYSTEM:F' | Out-Null
Say "user file:  $((Get-Item $uak).Length) bytes at $uak"

Write-Host "=== restart sshd and read its verdict ==="
Restart-Service sshd -Force
Start-Sleep 2
Say "status: $((Get-Service sshd).Status)"
# sshd on Windows logs auth failures to the event log; the reason string there
# names the actual cause (bad permissions, wrong file, key not found).
Get-WinEvent -FilterHashtable @{LogName='OpenSSH/Operational'} -MaxEvents 8 -ErrorAction SilentlyContinue |
    ForEach-Object { Say ("{0:HH:mm:ss}  {1}" -f $_.TimeCreated, ($_.Message -split "`n")[0]) }
