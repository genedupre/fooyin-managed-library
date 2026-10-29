param([string]$FooyinDirectory = 'C:\Program Files\fooyin')
$ErrorActionPreference = 'Stop'
if (Get-Process fooyin -ErrorAction SilentlyContinue) { throw 'Close fooyin normally before installing. Do not force-terminate it.' }
$packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$runtimeRoot = [IO.Path]::GetFullPath($FooyinDirectory)
$manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest.runtime.PSObject.Properties) {
    $runtimeFile = Join-Path $runtimeRoot $entry.Name
    if ((Get-FileHash -LiteralPath $runtimeFile -Algorithm SHA256).Hash -ne $entry.Value) {
        throw "Runtime differs from tested fooyin 0.14.1: $($entry.Name). Rebuild and retest before installing."
    }
}
$candidate = Join-Path $packageRoot 'bin\fyplugin_fileops.dll'
if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ne $manifest.pluginSha256) { throw 'Plugin checksum mismatch.' }
$destination = Join-Path $runtimeRoot 'plugins\fyplugin_fileops.dll'
$backup = Join-Path $packageRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $destination -Destination (Join-Path $backup 'fyplugin_fileops.dll')
$profileFiles = @(
    (Join-Path $env:LOCALAPPDATA 'fooyin\fooyin.conf'),
    (Join-Path $env:APPDATA 'fooyin\fooyin.db'),
    (Join-Path $env:APPDATA 'fooyin\fooyin.db-wal'),
    (Join-Path $env:APPDATA 'fooyin\fooyin.db-shm')
)
foreach ($profileFile in $profileFiles) {
    if (Test-Path -LiteralPath $profileFile) { Copy-Item -LiteralPath $profileFile -Destination $backup }
}
Copy-Item -LiteralPath $candidate -Destination $destination -Force
if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $manifest.pluginSha256) { throw 'Installed plugin verification failed.' }
@{ installed = (Get-Date -Format o); runtime = $runtimeRoot; backup = $backup; pluginSha256 = $manifest.pluginSha256 } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'install-receipt.json') -Encoding utf8
Write-Output "Installed and verified. Original plugin and profile backup: $backup"
Write-Output 'Reopen fooyin. Settings > Plugins > File Operations > Configure opens Managed library. First installs default to disabled; upgrades retain existing organizer settings.'
