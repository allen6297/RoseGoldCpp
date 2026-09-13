$ErrorActionPreference = "Stop"

$pf86 = ${env:ProgramFiles(x86)}
if (-not $pf86) {
    $pf86 = [Environment]::GetFolderPath("ProgramFilesX86")
}
if (-not $pf86) {
    $pf86 = "C:\Program Files (x86)"
}

$vswhereCandidates = @(
    (Join-Path $pf86 "Microsoft Visual Studio\Installer\vswhere.exe"),
    "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
)
$vswhere = $vswhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vswhere) {
    throw "vswhere.exe not found. Install Visual Studio Build Tools or Community."
}

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw "MSVC build tools not found."
}

$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$sources = @(
    "RoseGoldC\RoseGoldC.cpp",
    "RoseGoldC\lexer.cpp",
    "RoseGoldC\parser.cpp",
    "RoseGoldC\value.cpp",
    "RoseGoldC\modules.cpp",
    "RoseGoldC\constexpr.cpp",
    "RoseGoldC\typecheck.cpp",
    "RoseGoldC\eval.cpp",
    "RoseGoldC\harness.cpp",
    "RoseGoldC\lsp.cpp",
    "RoseGoldC\host_ui.cpp"
)

$quoted = ($sources | ForEach-Object { "`"$root\$_`"" }) -join " "
$exe = Join-Path $outDir "RoseGoldC.exe"
# Link to a staging name first so a locked RoseGoldC.exe (running demo/tests)
# does not block the build; then replace the canonical binary.
$stage = Join-Path $outDir "RoseGoldC.stage.exe"

$cmd = "`"$vcvars`" && clang-cl /nologo /std:c++20 /EHsc /Zi /Od /W3 /I `"$root\RoseGoldC`" /Fe:`"$stage`" /Fo:`"$outDir\\`" $quoted user32.lib gdi32.lib"
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

function Install-RoseGoldC([string]$from, [string]$to) {
    if (Test-Path $to) {
        try {
            Remove-Item -LiteralPath $to -Force -ErrorAction Stop
        } catch {
            Write-Warning "Could not replace $to (file may be locked). New build is at $from"
            Write-Warning "Close RoseGoldC.exe and re-run .\build.ps1, or copy manually."
            return $false
        }
    }
    Move-Item -LiteralPath $from -Destination $to -Force
    return $true
}

if (-not (Install-RoseGoldC $stage $exe)) {
    exit 0
}
Write-Host "Built $exe"
