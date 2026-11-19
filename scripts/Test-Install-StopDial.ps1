[CmdletBinding()]
param([Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$PackageRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    if (-not $threw) { throw $Message }
}
function Write-PackageChecksums {
    param([string]$Root)
    $rootPath = [System.IO.Path]::GetFullPath($Root)
    $sumPath = Join-Path $rootPath 'SHA256SUMS.txt'
    $files = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File | Where-Object { $_.FullName -ne $sumPath } | Sort-Object FullName)
    $lines = @()
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines += ($hash + '  ' + $relative)
    }
    Set-Content -LiteralPath $sumPath -Value $lines -Encoding ASCII
}
function Get-InstalledVersion {
    param([string]$BundlePath)
    $text = Get-Content -LiteralPath (Join-Path $BundlePath 'Contents\Resources\moduleinfo.json') -Raw
    if ($text -notmatch '"Name"\s*:\s*"StopDial"\s*,\s*"Version"\s*:\s*"([^"]+)"') { throw 'Unable to read installed StopDial version.' }
    $Matches[1]
}

$package = [System.IO.Path]::GetFullPath($PackageRoot)
$installer = Join-Path $package 'Install-StopDial.ps1'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Package is missing Install-StopDial.ps1.' }

$defaultLocalAppData = $env:LOCALAPPDATA
if ([string]::IsNullOrWhiteSpace($defaultLocalAppData)) { throw 'LOCALAPPDATA must be available for per-user installer tests.' }
$defaultVst3Target = Join-Path $defaultLocalAppData 'Programs\Common\VST3\StopDial.vst3'
$defaultPreview = & $installer -PackageRoot $package -WhatIf
Assert-True ($defaultPreview.status -eq 'what-if') 'Default-path WhatIf did not report what-if.'
Assert-True ([string]::Equals([System.IO.Path]::GetFullPath($defaultPreview.target), [System.IO.Path]::GetFullPath($defaultVst3Target), [System.StringComparison]::OrdinalIgnoreCase)) 'Default install path is not per-user LOCALAPPDATA Programs\Common\VST3.'

$originalLocalAppData = $env:LOCALAPPDATA
try {
    Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
    $missingLocalAppDataError = ''
    try { & $installer -PackageRoot $package -WhatIf | Out-Null } catch { $missingLocalAppDataError = $_.Exception.Message }
    Assert-True ($missingLocalAppDataError -like '*LOCALAPPDATA is required for the default per-user VST3 install path*') 'Installer did not fail closed when LOCALAPPDATA is unavailable.'
}
finally {
    if ([string]::IsNullOrWhiteSpace($originalLocalAppData)) {
        Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
    } else {
        $env:LOCALAPPDATA = $originalLocalAppData
    }
}
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('StopDial-Install-Test-' + [Guid]::NewGuid().ToString('N'))
$targetRoot = Join-Path $testRoot 'VST3'
$backupRoot = Join-Path $testRoot 'Backups'

try {
    & $installer -PackageRoot $package -Vst3Directory $targetRoot -BackupRoot $backupRoot -WhatIf | Out-Null
    Assert-True (-not (Test-Path -LiteralPath $targetRoot)) 'WhatIf created the VST3 destination.'
    Assert-True (-not (Test-Path -LiteralPath $backupRoot)) 'WhatIf created the backup directory.'

    $initial = & $installer -PackageRoot $package -Vst3Directory $targetRoot -BackupRoot $backupRoot
    Assert-True ($initial.status -eq 'installed') 'Fresh install did not report installed.'
    Assert-True ((Get-InstalledVersion (Join-Path $targetRoot 'StopDial.vst3')) -eq '2.3.1') 'Fresh install has the wrong version.'
    Assert-True ($null -eq $initial.backup_path) 'Fresh install unexpectedly created a backup.'

    $before = 0
    if (Test-Path -LiteralPath $backupRoot) { $before = @(Get-ChildItem -LiteralPath $backupRoot -Directory).Count }
    $same = & $installer -PackageRoot $package -Vst3Directory $targetRoot -BackupRoot $backupRoot
    $after = 0
    if (Test-Path -LiteralPath $backupRoot) { $after = @(Get-ChildItem -LiteralPath $backupRoot -Directory).Count }
    Assert-True ($same.status -eq 'already-installed') 'Repeat install was not idempotent.'
    Assert-True ($after -eq $before) 'Idempotent install created an unnecessary backup.'

    $upgradeRoot = Join-Path $testRoot 'UpgradePackage'
    New-Item -ItemType Directory -Path $upgradeRoot -Force | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $package -Force)) { Copy-Item -LiteralPath $item.FullName -Destination $upgradeRoot -Recurse }
    $upgradeManifestPath = Join-Path $upgradeRoot 'release-manifest.json'
    $upgradeManifest = Get-Content -LiteralPath $upgradeManifestPath -Raw | ConvertFrom-Json
    $upgradeManifest.version = '2.3.2'
    $upgradeManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $upgradeManifestPath -Encoding UTF8
    $upgradeInfoPath = Join-Path $upgradeRoot 'StopDial.vst3\Contents\Resources\moduleinfo.json'
    $upgradeInfo = (Get-Content -LiteralPath $upgradeInfoPath -Raw).Replace('"Version": "2.3.1"', '"Version": "2.3.2"')
    if ($upgradeInfo -notmatch '"Version"\s*:\s*"2\.3\.2"') { throw 'Synthetic upgrade fixture could not update moduleinfo.json.' }
    Set-Content -LiteralPath $upgradeInfoPath -Value $upgradeInfo -NoNewline -Encoding UTF8
    Write-PackageChecksums -Root $upgradeRoot

    $upgrade = & $installer -PackageRoot $upgradeRoot -Vst3Directory $targetRoot -BackupRoot $backupRoot
    Assert-True ($upgrade.status -eq 'installed') 'Upgrade did not report installed.'
    Assert-True ((Get-InstalledVersion (Join-Path $targetRoot 'StopDial.vst3')) -eq '2.3.2') 'Upgrade did not install the new version.'
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$upgrade.backup_path)) 'Upgrade did not return a backup path.'
    Assert-True (Test-Path -LiteralPath (Join-Path $upgrade.backup_path 'backup-manifest.json')) 'Upgrade backup manifest is missing.'

    $rollback = & $installer -RollbackFrom $upgrade.backup_path -Vst3Directory $targetRoot -BackupRoot $backupRoot
    Assert-True ($rollback.status -eq 'rolled-back') 'Rollback did not report rolled-back.'
    Assert-True ((Get-InstalledVersion (Join-Path $targetRoot 'StopDial.vst3')) -eq '2.3.1') 'Rollback did not restore the original version.'
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$rollback.backup_path)) 'Rollback did not preserve the replaced version.'

    $tamperedRoot = Join-Path $testRoot 'TamperedPackage'
    New-Item -ItemType Directory -Path $tamperedRoot -Force | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $package -Force)) { Copy-Item -LiteralPath $item.FullName -Destination $tamperedRoot -Recurse }
    Add-Content -LiteralPath (Join-Path $tamperedRoot 'INSTALL-WINDOWS.txt') -Value 'tamper'
    $tamperedTarget = Join-Path $testRoot 'TamperedVST3'
    Assert-Throws { & $installer -PackageRoot $tamperedRoot -Vst3Directory $tamperedTarget -BackupRoot (Join-Path $testRoot 'TamperedBackups') } 'Installer accepted a package file with a checksum mismatch.'
    Assert-True (-not (Test-Path -LiteralPath $tamperedTarget)) 'Tampered package changed the destination.'

    $badRoot = Join-Path $testRoot 'BadManifestPackage'
    New-Item -ItemType Directory -Path $badRoot -Force | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $package -Force)) { Copy-Item -LiteralPath $item.FullName -Destination $badRoot -Recurse }
    $badManifestPath = Join-Path $badRoot 'release-manifest.json'
    $badManifest = Get-Content -LiteralPath $badManifestPath -Raw | ConvertFrom-Json
    $badManifest.binary_sha256 = ('0' * 64)
    $badManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $badManifestPath -Encoding UTF8
    Write-PackageChecksums -Root $badRoot
    $badTarget = Join-Path $testRoot 'BadManifestVST3'
    Assert-Throws { & $installer -PackageRoot $badRoot -Vst3Directory $badTarget -BackupRoot (Join-Path $testRoot 'BadManifestBackups') } 'Installer accepted a manifest with an incorrect binary hash.'
    Assert-True (-not (Test-Path -LiteralPath $badTarget)) 'Bad manifest changed the destination.'

    function Copy-PackageFixture {
        param([string]$Name)
        $root = Join-Path $testRoot $Name
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        foreach ($item in @(Get-ChildItem -LiteralPath $package -Force)) {
            Copy-Item -LiteralPath $item.FullName -Destination $root -Recurse
        }
        $root
    }
    function Assert-PackageRejectedWithoutWrite {
        param([string]$Root, [string]$Name)
        $target = Join-Path $testRoot ($Name + '-VST3')
        $backup = Join-Path $testRoot ($Name + '-Backups')
        Assert-Throws {
            & $installer -PackageRoot $Root -Vst3Directory $target -BackupRoot $backup
        } "$Name package was not rejected."
        Assert-True (-not (Test-Path -LiteralPath $target)) "$Name package changed the VST3 destination."
        Assert-True (-not (Test-Path -LiteralPath $backup)) "$Name package created a backup before validation."
    }

    $duplicateInfoRoot = Copy-PackageFixture -Name 'DuplicateModuleinfoPackage'
    $duplicateInfoPath = Join-Path $duplicateInfoRoot 'StopDial.vst3\Contents\Resources\moduleinfo.json'
    $duplicateInfo = Get-Content -LiteralPath $duplicateInfoPath -Raw
    $nameMatch = [regex]::Match($duplicateInfo, '"Name"\s*:\s*"StopDial"')
    if (-not $nameMatch.Success) { throw 'Duplicate moduleinfo fixture could not find the product name.' }
    $duplicateInfo = $duplicateInfo.Insert($nameMatch.Index + $nameMatch.Length, ', "Name": "StopDial"')
    Set-Content -LiteralPath $duplicateInfoPath -Value $duplicateInfo -NoNewline -Encoding UTF8
    Write-PackageChecksums -Root $duplicateInfoRoot
    Assert-PackageRejectedWithoutWrite -Root $duplicateInfoRoot -Name 'DuplicateModuleinfo'

    $duplicateManifestRoot = Copy-PackageFixture -Name 'DuplicateManifestPackage'
    $duplicateManifestPath = Join-Path $duplicateManifestRoot 'release-manifest.json'
    $duplicateManifest = Get-Content -LiteralPath $duplicateManifestPath -Raw
    $productMatch = [regex]::Match($duplicateManifest, '"product"\s*:\s*"StopDial"')
    if (-not $productMatch.Success) { throw 'Duplicate manifest fixture could not find the product field.' }
    $duplicateManifest = $duplicateManifest.Insert($productMatch.Index + $productMatch.Length, ', "product": "StopDial"')
    Set-Content -LiteralPath $duplicateManifestPath -Value $duplicateManifest -NoNewline -Encoding UTF8
    Write-PackageChecksums -Root $duplicateManifestRoot
    Assert-PackageRejectedWithoutWrite -Root $duplicateManifestRoot -Name 'DuplicateManifest'

    $overflowInfoRoot = Copy-PackageFixture -Name 'OverflowModuleinfoPackage'
    $overflowInfoPath = Join-Path $overflowInfoRoot 'StopDial.vst3\Contents\Resources\moduleinfo.json'
    $overflowInfo = Get-Content -LiteralPath $overflowInfoPath -Raw
    $versionMatch = [regex]::Match($overflowInfo, '"Version"\s*:\s*"2\.3\.1"')
    if (-not $versionMatch.Success) { throw 'Overflow moduleinfo fixture could not find the version field.' }
    $overflowInfo = $overflowInfo.Insert($versionMatch.Index + $versionMatch.Length, ', "NumericProbe": 1e999')
    Set-Content -LiteralPath $overflowInfoPath -Value $overflowInfo -NoNewline -Encoding UTF8
    Write-PackageChecksums -Root $overflowInfoRoot
    Assert-PackageRejectedWithoutWrite -Root $overflowInfoRoot -Name 'OverflowModuleinfo'

    Write-Host 'StopDial install/upgrade/rollback and strict-metadata tests passed.'
}
finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
