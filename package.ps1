param(
    [Parameter(Mandatory)][string]$QtDirectory,
    [Parameter(Mandatory)][string]$FooyinSource,
    [string]$FooyinDirectory = 'C:\Program Files\fooyin',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -QtDirectory $QtDirectory -FooyinSource $FooyinSource -FooyinDirectory $FooyinDirectory -BuildDirectory $BuildDirectory
$dll = Join-Path $BuildDirectory 'fyplugin_fileops.dll'
$metadata = Get-Content -LiteralPath (Join-Path $BuildDirectory 'generated\fileops.json') -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
$manifest.version = $metadata.Version
$manifest.pluginSha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$package = Join-Path $dist ('fooyin-managed-library-' + $manifest.version + '-windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path (Join-Path $package 'bin') | Out-Null
foreach ($name in @('fileops','tests','CMakeLists.txt','LICENSE','README.md','build.ps1','package.ps1','make_imports.py','install.ps1','rollback.ps1')) {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $package -Recurse
}
Copy-Item -LiteralPath $dll -Destination (Join-Path $package 'bin\fyplugin_fileops.dll')
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding utf8
Compress-Archive -LiteralPath $package -DestinationPath ($package + '.zip')
Write-Output ($package + '.zip')
