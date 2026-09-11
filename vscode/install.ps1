# Install RoseGoldC into Cursor (else VS Code).
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$id = "rosegoldc.rosegoldc-0.0.1"

$targets = @()
$cursor = Join-Path $env:USERPROFILE ".cursor\extensions"
$vscode = Join-Path $env:USERPROFILE ".vscode\extensions"
if (Test-Path $cursor) { $targets += $cursor }
if (Test-Path $vscode) { $targets += $vscode }
if (-not $targets) { $targets += $cursor }

foreach ($root in $targets) {
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $dest = Join-Path $root $id
    if (Test-Path $dest) {
        Remove-Item -Recurse -Force $dest
    }
    Write-Host "Installing to $dest"
    Copy-Item -Recurse $here $dest
}

Write-Host "Installed. Reload the window: Developer: Reload Window."
