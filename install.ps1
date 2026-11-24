# Copies wkFastDraw.dll into the Worms Armageddon folder, or removes it with -Remove. WA loads it at its next start when
# Options > Advanced > "Load WormKit modules" is on. Without -Game it looks in Steam's and GOG's usual folders.
param([switch]$Remove, [string]$Game = '')
$ErrorActionPreference = 'Stop'
if (-not $Game) {
    $Game = @('C:\Program Files (x86)\Steam\steamapps\common\Worms Armageddon',
              'C:\Program Files\GOG Galaxy\Games\Worms Armageddon',
              'C:\Program Files (x86)\GOG Galaxy\Games\Worms Armageddon',
              'C:\GOG Games\Worms Armageddon') | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'WA.exe') } | Select-Object -First 1
    if (-not $Game) { throw 'WA.exe not found in the Steam or GOG folders (use -Game <your WA folder>)' }
}
$target = Join-Path $Game 'wkFastDraw.dll'
if ($Remove) {
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target; "removed $target" } else { 'not installed' }
    return
}
if (-not (Test-Path -LiteralPath (Join-Path $Game 'WA.exe'))) { throw "WA.exe not found in $Game (use -Game <your WA folder>)" }
$dll = Join-Path $PSScriptRoot 'release\wkFastDraw.dll'
if (-not (Test-Path -LiteralPath $dll)) { $dll = Join-Path $PSScriptRoot 'wkFastDraw.dll' }
Copy-Item -LiteralPath $dll -Destination $target -Force
"installed $target (WA loads it at its next start)"
$opt = Get-ItemProperty -Path 'HKCU:\Software\Team17SoftwareLTD\WormsArmageddon\Options' -ErrorAction SilentlyContinue
if (-not $opt -or $opt.LoadWormKitModules -ne 1) { 'Turn on Options > Advanced > "Load WormKit modules" in WA (if it is not on yet)' }
