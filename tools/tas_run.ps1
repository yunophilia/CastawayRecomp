# Runs a TAS script (docs/tas.md) with an empty save folder and collects the log, telemetry
# and screenshots in out/tas/<Tag>.log / .csv / shots/<Tag>/. The window starts minimized;
# runs are low impact (muted, 2x internal resolution, below-normal priority) unless -Full.
#   powershell -ExecutionPolicy Bypass -File tools/tas_run.ps1 -Script tools/tas/e2e.tas [-Tag name] [-Seconds 900] [-Full]
# Prints "<Tag> : OK" when the script ran to its end with every check passed; exit code 0 then,
# else the game's (1 a check failed, 3 an "until" timed out) or 2 if the run timed out.
param(
    [Parameter(Mandatory)][string]$Script,
    [string]$Tag = "",
    [int]$Seconds = 900,
    [switch]$Full,
    [int]$Scale = 2,
    [string]$Exe = "",
    [string]$Data = "",               # game data (default: game/R42E69)
    [string]$ExtraArgs = ""
)
$root = Split-Path $PSScriptRoot
$out = Join-Path $root "out"
if (-not $Exe) { $Exe = Join-Path $out "CastawayRecomp.exe" }
if (-not $Data) { $Data = Join-Path $root "game\R42E69" }
if (-not (Test-Path $Script)) { throw "TAS script not found: $Script" }
if (-not $Tag) { $Tag = [IO.Path]::GetFileNameWithoutExtension($Script) }
$exeDir = Split-Path (Resolve-Path $Exe).Path
$tasDir = Join-Path $out "tas"
New-Item -ItemType Directory -Force $tasDir | Out-Null
$nand = Join-Path $out "tasnand\run_$Tag"
if (Test-Path $nand) { Remove-Item -Recurse -Force $nand }
New-Item -ItemType Directory -Force $nand | Out-Null
$csv = Join-Path $tasDir "$Tag.csv"
$shotDir = Join-Path $exeDir "shots"   # the game writes shots/ in its working directory
if (Test-Path $shotDir) { Remove-Item -Recurse -Force $shotDir }
$scriptPath = (Resolve-Path $Script).Path
$gameArgs = "--nobox --skip-launcher --tas `"$scriptPath`" --telemetry `"$csv`" --nand `"$nand`" --data `"$Data`""
if (-not $Full) { $gameArgs += " --mute --scale $Scale" }
if ($ExtraArgs) { $gameArgs += " $ExtraArgs" }

Remove-Item (Join-Path $exeDir "CastawayRecomp.log") -ErrorAction SilentlyContinue   # a run that never starts must not report an old log
$mine = { Get-Process CastawayRecomp* -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Resolve-Path $Exe).Path } }
& $mine | Stop-Process -Force
$p = Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $exeDir -PassThru -WindowStyle Minimized
if (-not $Full) { try { $p.PriorityClass = "BelowNormal" } catch {} }
$code = $null
if ($p.WaitForExit($Seconds * 1000)) { $code = $p.ExitCode } else { Stop-Process -Id $p.Id -Force }
& $mine | Stop-Process -Force
Start-Sleep -Milliseconds 300
$gameLog = Join-Path $exeDir "CastawayRecomp.log"
if (Test-Path $gameLog) { Copy-Item $gameLog (Join-Path $tasDir "$Tag.log") -Force } else { Set-Content (Join-Path $tasDir "$Tag.log") "" }
if (Test-Path $shotDir) {
    $dst = Join-Path $tasDir "shots\$Tag"
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Move-Item $shotDir $dst
}
$log = Get-Content (Join-Path $tasDir "$Tag.log")
$fails = $log | Where-Object { $_ -match "TAS: FAIL|^\[E |FATAL" }
$summary = $log | Where-Object { $_ -match "TAS: \d+ passed" } | Select-Object -Last 1
$result = if ($code -eq 0 -and -not $fails) { "OK" } elseif ($code -eq $null) { "TIMEOUT after $Seconds s" } else { "FAILED (exit $code)" }
"$Tag : $result"
if ($summary) { ($summary -replace '^.*TAS: ', '  ') }
$fails | Select-Object -First 5 | ForEach-Object { "  $_" }
exit $(if ($result -eq "OK") { 0 } elseif ($code -eq $null) { 2 } elseif ($code) { $code } else { 1 })
