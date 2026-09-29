# Replaces STC code flash with the selected diagnostic image.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,
    [ValidateSet('pin_watch', 'uart_smoke', 'matrix_test')]
    [string]$Environment = 'matrix_test'
)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$pioPath = Join-Path $repoPath '.venv\Scripts\pio.exe'
$stcPath = Join-Path $repoPath '.venv\Scripts\stcgal.exe'
$imagePath = Join-Path $repoPath ".pio\build\$Environment\firmware.hex"
& $pioPath run --project-dir $repoPath -e $Environment
if ($LASTEXITCODE -ne 0) { throw 'Build failed; no programming attempted.' }
if (-not (Test-Path -LiteralPath $imagePath)) { throw 'Built firmware image missing.' }
Write-Host 'Replacing STC code flash. Keep Micro USB powered; cycle keypad power when prompted.'
# This chip's 7.3.13U bootloader needs stc8d calibration, not auto/stc8.
& $stcPath -p $Port -P stc8d -l 2400 -b 19200 -t 24000 $imagePath
exit $LASTEXITCODE
