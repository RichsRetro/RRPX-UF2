$ErrorActionPreference = 'Stop'
$XboxAddress = Read-Host 'Xbox IP address shown in RRXD'
$parsedAddress = $null
if (-not [System.Net.IPAddress]::TryParse($XboxAddress, [ref]$parsedAddress)) { throw 'Enter a valid IP address.' }
$BackupNumberText = Read-Host 'Backup number (for Backup-000 enter 0)'
$BackupNumber = 0
if (-not [int]::TryParse($BackupNumberText,[ref]$BackupNumber) -or $BackupNumber -lt 0 -or $BackupNumber -gt 999) { throw 'Enter a number from 0 to 999.' }
$BackupName = 'Backup-{0:D3}' -f $BackupNumber
$ExportParent = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'RRXD Backups'
$ExportDestination = Join-Path $ExportParent ($BackupName + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$PythonCommand = Get-Command py -ErrorAction SilentlyContinue
if ($PythonCommand) {
    & $PythonCommand.Source -3 (Join-Path $PSScriptRoot 'export_backup.py') --host $XboxAddress --backup $BackupName --output $ExportDestination
} else {
    $PythonCommand = Get-Command python -ErrorAction Stop
    & $PythonCommand.Source (Join-Path $PSScriptRoot 'export_backup.py') --host $XboxAddress --backup $BackupName --output $ExportDestination
}
if ($LASTEXITCODE -ne 0) { throw 'Backup export did not verify. Partial files have been preserved.' }
Write-Host "Verified backup and ZIP saved under $ExportParent"
Read-Host 'Press Enter to close'
