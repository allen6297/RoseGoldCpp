$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
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

$cmd = "`"$vcvars`" && clang-cl /nologo /std:c++20 /EHsc /Zi /Od /W3 /I `"$root\RoseGoldC`" /Fe:`"$exe`" /Fo:`"$outDir\\`" $quoted user32.lib gdi32.lib"
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
