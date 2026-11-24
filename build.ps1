# Builds wkFastDraw.dll (a 32-bit WormKit module for Worms Armageddon 3.8.1) with MinGW (32-bit gcc), after running the
# drawing test (also against WA's own code, when WA.exe is found: WA_EXE, else Steam's usual folder) and the WA.exe check's
# test. Also makes tests\fastdraw-check.dll, the same module comparing every 16th blit with plain loops (for testing only;
# its name keeps WA, which loads wk*.dll, from picking it up by accident).
#   .\build.ps1 [-Gcc C:\MinGW\bin\gcc.exe]
param([string]$Gcc = 'C:\MinGW\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$opt = @('-O2', '-Wall', '-Wextra', '-std=gnu99', '-msse2', '-mstackrealign')
foreach ($t in @('blit_test', 'guard_test')) {
    $exe = Join-Path $env:TEMP "wkfastdraw_$t.exe"
    $ErrorActionPreference = 'Continue'     # (compiler warnings on stderr are not build failures)
    & $Gcc @opt (Join-Path $here "tests\$t.c") -o $exe
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "$t build failed" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$t failed" }
}
foreach ($b in @(@('wkFastDraw.dll', @()), @('tests\fastdraw-check.dll', @('-DFB_CHECK')))) {
    $ErrorActionPreference = 'Continue'
    & $Gcc -shared @opt @($b[1]) -static-libgcc -s (Join-Path $here 'src\wkFastDraw.c') -o (Join-Path $here $b[0])
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "$($b[0]) build failed" }
    Get-FileHash -LiteralPath (Join-Path $here $b[0]) -Algorithm SHA256 | Select-Object Hash, Path
}
