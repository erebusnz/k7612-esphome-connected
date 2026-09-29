# Uses the ProMicroUartBridge firmware; this command supplies no image or erase flag.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port
)

$ErrorActionPreference = 'Stop'
$programmerPath = Join-Path $PSScriptRoot '..\.venv\Scripts\stcgal.exe'
if (-not (Test-Path -LiteralPath $programmerPath)) {
    throw 'Install requirements-tools.txt in the repository .venv first.'
}

Write-Host 'STC identification only. Cycle keypad power when stcgal prompts.'
Write-Host 'Keep the Pro Micro connected to USB. Ctrl+C cancels waiting.'
& $programmerPath -p $Port -P auto -l 2400 -b 19200
exit $LASTEXITCODE
