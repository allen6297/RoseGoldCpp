# Install RoseGoldC into Cursor (else VS Code).

#   powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1

$ErrorActionPreference = "Stop"

$here = $PSScriptRoot

$id = "rosegoldc.rosegoldc-0.0.11"



$targets = @()

$cursor = Join-Path $env:USERPROFILE ".cursor\extensions"

$vscode = Join-Path $env:USERPROFILE ".vscode\extensions"

if (Test-Path $cursor) { $targets += $cursor }

if (Test-Path $vscode) { $targets += $vscode }

if (-not $targets) { $targets += $cursor }



foreach ($root in $targets) {

    New-Item -ItemType Directory -Force -Path $root | Out-Null

    $old = @(

        "rosegoldc.rosegoldc-0.0.1",

        "rosegoldc.rosegoldc-0.0.2",

        "rosegoldc.rosegoldc-0.0.3",

        "rosegoldc.rosegoldc-0.0.4",

        "rosegoldc.rosegoldc-0.0.5",

        "rosegoldc.rosegoldc-0.0.6",

        "rosegoldc.rosegoldc-0.0.7",

        "rosegoldc.rosegoldc-0.0.8",
        "rosegoldc.rosegoldc-0.0.9",
        "rosegoldc.rosegoldc-0.0.10"
    )

    foreach ($name in $old) {

        $path = Join-Path $root $name

        if (Test-Path $path) {

            Remove-Item -Recurse -Force $path

        }

    }

    $dest = Join-Path $root $id

    if (Test-Path $dest) {

        Remove-Item -Recurse -Force $dest

    }

    Write-Host "Installing to $dest"

    Copy-Item -Recurse $here $dest

}



Write-Host "Installed. Reload the window: Developer: Reload Window."

Write-Host "Then run RoseGoldC: Restart Language Server if an older LSP process is still running."

