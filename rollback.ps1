$ErrorActionPreference = 'Stop'
if (Get-Process fooyin -ErrorAction SilentlyContinue) { throw 'Close fooyin normally before restoring its plugin.' }
$receipt = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'install-receipt.json') -Raw | ConvertFrom-Json
$backupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'backups')) + [IO.Path]::DirectorySeparatorChar
$backupFile = [IO.Path]::GetFullPath((Join-Path $receipt.backup 'fyplugin_fileops.dll'))
if (-not $backupFile.StartsWith($backupRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected backup location.' }
$destination = Join-Path ([IO.Path]::GetFullPath($receipt.runtime)) 'plugins\fyplugin_fileops.dll'
Copy-Item -LiteralPath $backupFile -Destination $destination -Force
if ((Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $backupFile).Hash) { throw 'Restore verification failed.' }
Write-Output 'Original File Operations plugin restored. Library files and database were not rolled back.'
