param(
    [Parameter(Mandatory)][string]$QtDirectory,
    [Parameter(Mandatory)][string]$FooyinSource,
    [string]$FooyinDirectory = 'C:\Program Files\fooyin',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
# Run from an x64 Native Tools Command Prompt for VS 2022.
foreach ($tool in @('cl.exe', 'lib.exe', 'python.exe', 'cmake.exe', 'ninja.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
}
$QtDirectory = (Resolve-Path -LiteralPath $QtDirectory).Path
$FooyinSource = (Resolve-Path -LiteralPath $FooyinSource).Path
$FooyinDirectory = (Resolve-Path -LiteralPath $FooyinDirectory).Path
$oldPath = $env:PATH
try {
    $env:PATH = "$QtDirectory\bin;$FooyinDirectory;$oldPath"
    python (Join-Path $PSScriptRoot 'make_imports.py') (Join-Path $PSScriptRoot 'imports') $FooyinDirectory
    if ($LASTEXITCODE) { throw 'Import-library generation failed.' }
    cmake -S $PSScriptRoot -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtDirectory" "-DFOOYIN_SOURCE=$FooyinSource" "-DFOOYIN_RUNTIME=$FooyinDirectory"
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    cmake --build $BuildDirectory
    if ($LASTEXITCODE) { throw 'Compilation failed.' }
    ctest --test-dir $BuildDirectory --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed.' }
} finally { $env:PATH = $oldPath }
