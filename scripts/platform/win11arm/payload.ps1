# The windows-arm64 payload. PowerShell, not sh, ON PURPOSE.
#
# Every other runner executes the same POSIX payload because every other
# platform has a POSIX userland. Windows does not, and making it have one means
# installing git-for-windows for bash+awk+diff and building GNU make -- a large
# dependency chain whose failures get reported as platform failures. Two hours
# went into that before the obvious point landed: the guest only has to COMPILE
# and RUN. Windows ships tar.exe and PowerShell inbox, so the orchestration
# lives here and nothing else needs installing.
#
# Reads $env:TYCHO_FIXTURES (space separated) or runs all with a .out sibling.
$ErrorActionPreference = 'Continue'
$clang = 'C:\llvm-mingw\bin\clang.exe'
if (-not (Test-Path $clang)) { Write-Output 'BUILD FAILED: no clang'; exit 1 }

Set-Location $PSScriptRoot
# tests/write_file.ty writes to /tmp/..., which Windows resolves to
# <drive>:\tmp. It does not exist by default, so the write fails and the
# fixture reads as a platform defect rather than a missing directory.
New-Item -ItemType Directory -Force -Path (Join-Path ([System.IO.Path]::GetPathRoot($PWD.Path)) 'tmp') | Out-Null
# tychoc is one translation unit plus a generated header the host already made.
& $clang -O1 -fwrapv -std=c11 -Ibuild src\tychoc.c -o tychoc.exe -lm 2>&1 | Select-Object -First 5
if (-not (Test-Path .\tychoc.exe)) { Write-Output 'BUILD FAILED: tychoc did not link'; exit 1 }

# The fixture list arrives as a FILE, never as an environment variable. Setting
# one meant a string surviving bash -> ssh -> cmd -> PowerShell intact, and it
# did not: the quoting collapsed and the lane silently ran every fixture
# instead of the named subset, which looks like a passing lane doing different
# work than the one beside it. A file has no quoting.
$fx = if (Test-Path fixtures.txt) {
          (Get-Content fixtures.txt -Raw) -split '\s+' | Where-Object { $_ }
      } else {
          Get-ChildItem tests\*.ty | Where-Object { Test-Path "tests\$($_.BaseName).out" } |
              ForEach-Object { $_.BaseName }
      }

$pass = 0; $fail = 0; $bad = @()
foreach ($x in $fx) {
    $ty = "tests\$x.ty"; $exe = "$env:TEMP\pm-$x.exe"
    # EITHER golden is a pass where both exist. float_roundtrip and
    # float_str_locale ship a `.out.win` because their rt= column needs
    # newlocale, which CLASSIC mingw lacks -- but this guest builds with
    # llvm-mingw against UCRT, which HAS it, so the output matches the POSIX
    # golden instead. Preferring .out.win unconditionally failed both fixtures
    # on a toolchain that is behaving correctly; requiring .out fails the
    # toolchain the golden was recorded for. Accepting either is the honest
    # rule: both are sanctioned answers and which applies depends on the CRT.
    $wants = @("tests\$x.out")
    if (Test-Path "tests\$x.out.win") { $wants += "tests\$x.out.win" }
    if (-not (Test-Path $ty)) { $fail++; $bad += "$x(no-fixture)"; continue }
    # --cc: tychoc shells out to `cc`, which does not exist here. Naming clang
    # explicitly keeps the emitted program native aarch64.
    & .\tychoc.exe $ty -o $exe --cc $clang 2>&1 | Out-Null
    if (-not (Test-Path $exe)) { $fail++; $bad += "$x(compile)"; continue }
    # STDIN, exactly as tests/run.sh does it: a program may supply fixture input
    # as tests/<name>.in and gets an empty stream otherwise. Without this
    # io_builtins reads EOF immediately and prints nothing, which the lane
    # reported as an x86_64 defect when it was a missing redirect.
    # Start-Process with a REDIRECTED stream, not a PowerShell pipe. Piping
    # `Get-Content -Raw` into a native exe re-encodes the text through
    # $OutputEncoding and appends a newline, so the program reads bytes that
    # are not the ones in the .in file and the golden never matches.
    $in = "tests\$x.in"
    if (Test-Path $in) {
        $so = [System.IO.Path]::GetTempFileName()
        Start-Process -FilePath $exe -RedirectStandardInput $in `
            -RedirectStandardOutput $so -NoNewWindow -Wait -ErrorAction SilentlyContinue
        $got = (Get-Content $so -Raw -ErrorAction SilentlyContinue)
        Remove-Item $so -ErrorAction SilentlyContinue
    } else {
        $got = (& $exe 2>$null | Out-String)
    }
    if ($null -eq $got) { $got = "" }
    $got = $got -replace "`r`n", "`n"
    $ok = $false
    foreach ($w in $wants) {
        $exp = (Get-Content $w -Raw) -replace "`r`n", "`n"
        if ($got.TrimEnd("`n") -eq $exp.TrimEnd("`n")) { $ok = $true; break }
    }
    # Trailing-newline differences are a text-mode artefact of the transport,
    # not a language difference, so both sides are normalised before compare.
    if ($ok) { $pass++ } else { $fail++; $bad += $x }
    Remove-Item $exe -ErrorAction SilentlyContinue
}
if ($bad.Count) { Write-Output ("  FAIL " + ($bad -join ' ')) }
Write-Output "passed: $pass   failed: $fail"
