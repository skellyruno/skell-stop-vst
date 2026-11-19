[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium', DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')][ValidateNotNullOrEmpty()][string]$PackageRoot,
    [Parameter(Mandatory = $true, ParameterSetName = 'Rollback')][ValidateNotNullOrEmpty()][string]$RollbackFrom,
    [string]$Vst3Directory = '',
    [string]$BackupRoot = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Vst3Directory)) {
    $localAppData = $env:LOCALAPPDATA
    if ([string]::IsNullOrWhiteSpace($localAppData)) {
        throw 'LOCALAPPDATA is required for the default per-user VST3 install path. Pass -Vst3Directory for a custom location.'
    }
    $Vst3Directory = Join-Path $localAppData 'Programs\Common\VST3'
}
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    $localData = $env:LOCALAPPDATA
    if ([string]::IsNullOrWhiteSpace($localData)) { $localData = [System.IO.Path]::GetTempPath() }
    $BackupRoot = Join-Path $localData 'Skelly\StopDial\Backups'
}

function Get-FullPath { param([string]$Path) [System.IO.Path]::GetFullPath($Path) }

function Remove-SteinbergTrailingCommas {
    param([string]$Source)
    $builder = New-Object System.Text.StringBuilder
    $inString = $false
    $escaped = $false
    $i = 0
    while ($i -lt $Source.Length) {
        $c = $Source[$i]
        if ($inString) {
            [void]$builder.Append($c)
            if ($escaped) { $escaped = $false }
            elseif ($c -eq '\') { $escaped = $true }
            elseif ($c -eq '"') { $inString = $false }
            $i++
            continue
        }
        if ($c -eq '"') {
            $inString = $true
            [void]$builder.Append($c)
            $i++
            continue
        }
        if ($c -eq ',') {
            $next = $i + 1
            while ($next -lt $Source.Length -and [char]::IsWhiteSpace($Source[$next])) { $next++ }
            if ($next -lt $Source.Length -and ($Source[$next] -eq '}' -or $Source[$next] -eq ']')) { $i++; continue }
        }
        [void]$builder.Append($c)
        $i++
    }
    $builder.ToString()
}

function Assert-NoDuplicateJsonProperties {
    param([string]$Source)
    $stack = New-Object 'System.Collections.Generic.List[object]'
    $index = 0
    while ($index -lt $Source.Length) {
        $c = $Source[$index]
        if ($c -eq '"') {
            $start = $index
            $index++
            $escaped = $false
            $closed = $false
            while ($index -lt $Source.Length) {
                $current = $Source[$index]
                if ($escaped) { $escaped = $false }
                elseif ($current -eq '\') { $escaped = $true }
                elseif ($current -eq '"') { $closed = $true; break }
                $index++
            }
            if (-not $closed) { throw 'Unterminated JSON string.' }
            $raw = $Source.Substring($start, $index - $start + 1)
            $after = $index + 1
            while ($after -lt $Source.Length -and [char]::IsWhiteSpace($Source[$after])) { $after++ }
            if ($after -lt $Source.Length -and $Source[$after] -eq ':' -and $stack.Count -gt 0) {
                $scope = $stack[$stack.Count - 1]
                if ($null -ne $scope) {
                    $key = [string](ConvertFrom-Json -InputObject $raw -ErrorAction Stop)
                    if (-not $scope.Add($key)) { throw "Duplicate or case-colliding JSON object key: $key" }
                }
            }
            $index++
            continue
        }
        if ($c -eq '{') {
            $keys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
            [void]$stack.Add($keys)
        }
        elseif ($c -eq '[') { [void]$stack.Add($null) }
        elseif ($c -eq '}' -or $c -eq ']') {
            if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
        }
        $index++
    }
}

function Assert-FiniteJsonNumbers {
    param([object]$Value)
    if ($null -eq $Value) { return }
    if ($Value -is [double]) {
        if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value)) {
            throw 'JSON contains a non-finite or overflowing numeric value.'
        }
        return
    }
    if ($Value -is [single]) {
        if ([single]::IsNaN($Value) -or [single]::IsInfinity($Value)) {
            throw 'JSON contains a non-finite or overflowing numeric value.'
        }
        return
    }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($key in $Value.Keys) { Assert-FiniteJsonNumbers -Value $Value[$key] }
        return
    }
    if ($Value -is [System.Array]) {
        foreach ($item in $Value) { Assert-FiniteJsonNumbers -Value $item }
        return
    }
    if ($Value -is [pscustomobject]) {
        foreach ($property in $Value.PSObject.Properties) {
            Assert-FiniteJsonNumbers -Value $property.Value
        }
    }
}

function ConvertFrom-StrictJson {
    param([string]$Source, [switch]$AllowTrailingCommas)
    $json = $Source
    if ($AllowTrailingCommas) { $json = Remove-SteinbergTrailingCommas -Source $json }
    Assert-NoDuplicateJsonProperties -Source $json
    $value = ConvertFrom-Json -InputObject $json -ErrorAction Stop
    Assert-FiniteJsonNumbers -Value $value
    $value
}
function Get-StopDialTreeHash {
    param([string]$Path)
    $root = Get-FullPath $Path
    $files = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File | Sort-Object FullName)
    if ($files.Count -eq 0) { throw "Cannot hash empty bundle: $root" }
    $records = @()
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $records += ($relative + '|' + $hash)
    }
    $payload = [string]::Join([Environment]::NewLine, $records)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        [BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($payload))).Replace('-', '').ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

function Get-StopDialBundleEvidence {
    param([string]$BundlePath)
    $bundle = Get-FullPath $BundlePath
    if (-not (Test-Path -LiteralPath $bundle -PathType Container) -or (Split-Path -Leaf $bundle) -ne 'StopDial.vst3') {
        throw "Expected StopDial.vst3 bundle: $bundle"
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $bundle -Recurse -Force)) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point not allowed: $($item.FullName)" }
    }
    if (@(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.exe').Count -ne 0) { throw 'Standalone EXE found inside VST3 bundle.' }
    if (@(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.zip').Count -ne 0) { throw 'Nested ZIP found inside VST3 bundle.' }

    $infoPath = Join-Path $bundle 'Contents\Resources\moduleinfo.json'
    $binaryPath = Join-Path $bundle 'Contents\x86_64-win\StopDial.vst3'
    if (-not (Test-Path -LiteralPath $infoPath -PathType Leaf)) { throw 'Missing moduleinfo.json.' }
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) { throw 'Missing Windows x64 VST3 module.' }
    $modules = @(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.vst3')
    if ($modules.Count -ne 1 -or $modules[0].FullName -ne $binaryPath) { throw "Expected one x86_64-win VST3 module; found $($modules.Count)." }

    $metadata = ConvertFrom-StrictJson -Source (Get-Content -LiteralPath $infoPath -Raw) -AllowTrailingCommas
    if ($metadata.Name -ne 'StopDial') { throw "Unexpected product name: $($metadata.Name)" }
    $version = [string]$metadata.Version
    if ($version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') { throw "Invalid version: $version" }
    $classes = @($metadata.Classes)
    $audio = @($classes | Where-Object { $_.Category -eq 'Audio Module Class' -and $_.Name -eq 'StopDial' })
    $controller = @($classes | Where-Object { $_.Category -eq 'Component Controller Class' -and $_.Name -eq 'StopDial' })
    $ids = @($classes | ForEach-Object { [string]$_.CID })
    if ($audio.Count -ne 1 -or $controller.Count -ne 1 -or $ids.Count -ne 2) { throw 'Expected one processor and one controller class.' }
    if (@($ids | Select-Object -Unique).Count -ne 2) { throw 'VST3 class IDs must be unique.' }
    foreach ($class in $classes) {
        if ([string]$class.Version -ne $version) { throw 'VST3 class version does not match bundle version.' }
    }
    foreach ($id in $ids) { if ($id -notmatch '^[0-9A-Fa-f]{32}$') { throw "Invalid VST3 class ID: $id" } }

    $bytes = [System.IO.File]::ReadAllBytes($binaryPath)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw 'Missing PE MZ header.' }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0x40 -or ($pe + 26) -gt $bytes.Length) { throw 'Invalid PE header offset.' }
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) { throw 'Missing PE signature.' }
    $machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
    $magic = [BitConverter]::ToUInt16($bytes, $pe + 24)
    if ($machine -ne 0x8664) { throw ('Expected AMD64 PE 0x8664, got 0x{0:X4}.' -f $machine) }
    if ($magic -ne 0x020B) { throw ('Expected PE32+ 0x020B, got 0x{0:X4}.' -f $magic) }

    [pscustomobject]@{
        version = $version
        class_id = [string]$audio[0].CID
        binary_sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
        binary_size = [int64](Get-Item -LiteralPath $binaryPath).Length
        tree_sha256 = Get-StopDialTreeHash -Path $bundle
    }
}

function Test-StopDialPackageChecksums {
    param([string]$Root)
    $rootPath = Get-FullPath $Root
    $sumPath = Join-Path $rootPath 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $sumPath -PathType Leaf)) { throw 'Missing SHA256SUMS.txt.' }
    $expected = @{}
    foreach ($line in @(Get-Content -LiteralPath $sumPath)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Malformed checksum line: $line" }
        $relative = $Matches[2].Replace('\', '/')
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative -match '(^|/)\.\.(/|$)') { throw "Unsafe checksum path: $relative" }
        $key = $relative.ToLowerInvariant()
        if ($expected.ContainsKey($key)) { throw "Duplicate checksum path: $relative" }
        $expected[$key] = $Matches[1].ToLowerInvariant()
    }
    $seen = @{}
    $files = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File | Where-Object { $_.FullName -ne $sumPath })
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $key = $relative.ToLowerInvariant()
        if (-not $expected.ContainsKey($key)) { throw "File missing from SHA256SUMS.txt: $relative" }
        $actual = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected[$key]) { throw "Checksum mismatch: $relative" }
        $seen[$key] = $true
    }
    foreach ($key in $expected.Keys) { if (-not $seen.ContainsKey($key)) { throw "Checksum references missing file: $key" } }
}

function Get-ValidatedStopDialPackage {
    param([string]$Root)
    $rootPath = Get-FullPath $Root
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) { throw "Package directory not found: $rootPath" }
    $bundles = @(Get-ChildItem -LiteralPath $rootPath -Directory -Filter '*.vst3')
    if ($bundles.Count -ne 1 -or $bundles[0].Name -ne 'StopDial.vst3') { throw "Expected one top-level StopDial.vst3 bundle; found $($bundles.Count)." }
    if (@(Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter '*.exe').Count -ne 0) { throw 'Standalone EXE not allowed in VST3 package.' }
    if (@(Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter '*.zip').Count -ne 0) { throw 'Nested ZIP not allowed in VST3 package.' }
    Test-StopDialPackageChecksums -Root $rootPath

    $manifestPath = Join-Path $rootPath 'release-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Missing release-manifest.json.' }
    $manifest = ConvertFrom-StrictJson -Source (Get-Content -LiteralPath $manifestPath -Raw)
    if ($manifest.verified -ne $true -or $manifest.product -ne 'StopDial' -or $manifest.format -ne 'VST3' -or $manifest.platform -ne 'windows-x64') {
        throw 'Release manifest does not describe a verified Windows x64 StopDial VST3.'
    }
    if ($manifest.bundle -ne 'StopDial.vst3' -or $manifest.binary -ne 'Contents/x86_64-win/StopDial.vst3') { throw 'Unexpected bundle or binary path.' }
    if ([string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Missing valid source_commit.' }
    if (-not [string]::IsNullOrWhiteSpace([string]$manifest.pr_head_commit) -and [string]$manifest.pr_head_commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Invalid pr_head_commit.' }

    $bundle = Join-Path $rootPath 'StopDial.vst3'
    $evidence = Get-StopDialBundleEvidence -BundlePath $bundle
    if ([string]$manifest.version -ne $evidence.version) { throw 'Manifest version mismatch.' }
    if ([string]$manifest.class_id -ne $evidence.class_id) { throw 'Manifest class ID mismatch.' }
    if ([string]$manifest.binary_sha256 -ne $evidence.binary_sha256) { throw 'Manifest binary hash mismatch.' }
    if ([int64]$manifest.binary_size -ne $evidence.binary_size) { throw 'Manifest binary size mismatch.' }
    [pscustomobject]@{ root = $rootPath; bundle = $bundle; evidence = $evidence }
}

function New-StopDialBackup {
    param([string]$BundlePath, [string]$Reason, [string]$Target)
    $evidence = Get-StopDialBundleEvidence -BundlePath $BundlePath
    New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $token = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $backupPath = Join-Path $BackupRoot ('StopDial-' + $stamp + '-' + $token)
    New-Item -ItemType Directory -Path $backupPath | Out-Null
    $backupBundle = Join-Path $backupPath 'StopDial.vst3'
    Copy-Item -LiteralPath $BundlePath -Destination $backupBundle -Recurse
    $copy = Get-StopDialBundleEvidence -BundlePath $backupBundle
    if ($copy.tree_sha256 -ne $evidence.tree_sha256) { throw "Backup verification failed; original was not replaced. Backup: $backupPath" }
    [ordered]@{
        schema_version = 1
        product = 'StopDial'
        created_utc = [DateTime]::UtcNow.ToString('o')
        version = $evidence.version
        binary_sha256 = $evidence.binary_sha256
        bundle_tree_sha256 = $evidence.tree_sha256
        reason = $Reason
        original_target = $Target
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupPath 'backup-manifest.json') -Encoding UTF8
    $backupPath
}

function Invoke-StopDialInstall {
    param([System.Management.Automation.PSCmdlet]$Cmdlet)
    $package = Get-ValidatedStopDialPackage -Root $PackageRoot
    $targetRoot = Get-FullPath $Vst3Directory
    $target = Join-Path $targetRoot 'StopDial.vst3'
    $current = $null
    if (Test-Path -LiteralPath $target) {
        $current = Get-StopDialBundleEvidence -BundlePath $target
        if ($current.version -eq $package.evidence.version -and $current.binary_sha256 -eq $package.evidence.binary_sha256) {
            return [pscustomobject]@{ status = 'already-installed'; version = $current.version; target = $target; backup_path = $null; binary_sha256 = $current.binary_sha256 }
        }
    }
    if (-not $Cmdlet.ShouldProcess($target, ('Install StopDial ' + $package.evidence.version + ' with verified backup'))) {
        return [pscustomobject]@{ status = 'what-if'; version = $package.evidence.version; target = $target; backup_path = $null; binary_sha256 = $package.evidence.binary_sha256 }
    }

    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $backupPath = $null
    if ($null -ne $current) { $backupPath = New-StopDialBackup -BundlePath $target -Reason 'pre-install-or-update' -Target $target }
    $token = [Guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $targetRoot ('.StopDial-stage-' + $token)
    $displaced = Join-Path $targetRoot ('.StopDial-previous-' + $token)
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
    try {
        Copy-Item -LiteralPath $package.bundle -Destination $stageRoot -Recurse
        $stagedBundle = Join-Path $stageRoot 'StopDial.vst3'
        $staged = Get-StopDialBundleEvidence -BundlePath $stagedBundle
        if ($staged.tree_sha256 -ne $package.evidence.tree_sha256) { throw 'Staged bundle differs from validated package.' }
        if (Test-Path -LiteralPath $target) { Move-Item -LiteralPath $target -Destination $displaced }
        Move-Item -LiteralPath $stagedBundle -Destination $target
        $installed = Get-StopDialBundleEvidence -BundlePath $target
        if ($installed.tree_sha256 -ne $package.evidence.tree_sha256) { throw 'Installed bundle failed verification.' }
        if (Test-Path -LiteralPath $displaced) { Remove-Item -LiteralPath $displaced -Recurse -Force -ErrorAction SilentlyContinue }
    }
    catch {
        if (Test-Path -LiteralPath $target) {
            Move-Item -LiteralPath $target -Destination (Join-Path $targetRoot ('.StopDial-install-failed-' + $token)) -ErrorAction SilentlyContinue
        }
        if ((Test-Path -LiteralPath $displaced) -and -not (Test-Path -LiteralPath $target)) {
            Move-Item -LiteralPath $displaced -Destination $target -ErrorAction SilentlyContinue
        }
        throw
    }
    finally { if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue } }
    [pscustomobject]@{ status = 'installed'; version = $installed.version; target = $target; backup_path = $backupPath; binary_sha256 = $installed.binary_sha256 }
}

function Invoke-StopDialRollback {
    param([System.Management.Automation.PSCmdlet]$Cmdlet)
    $backupPath = Get-FullPath $RollbackFrom
    if (-not (Test-Path -LiteralPath $backupPath -PathType Container)) { throw "Rollback backup not found: $backupPath" }
    $manifestPath = Join-Path $backupPath 'backup-manifest.json'
    $backupBundle = Join-Path $backupPath 'StopDial.vst3'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Rollback backup is missing backup-manifest.json.' }
    $manifest = ConvertFrom-StrictJson -Source (Get-Content -LiteralPath $manifestPath -Raw)
    if ($manifest.schema_version -ne 1 -or $manifest.product -ne 'StopDial') { throw 'Unsupported StopDial backup manifest.' }
    $backup = Get-StopDialBundleEvidence -BundlePath $backupBundle
    if ($backup.tree_sha256 -ne [string]$manifest.bundle_tree_sha256 -or $backup.binary_sha256 -ne [string]$manifest.binary_sha256 -or $backup.version -ne [string]$manifest.version) {
        throw 'Rollback backup integrity verification failed.'
    }

    $targetRoot = Get-FullPath $Vst3Directory
    $target = Join-Path $targetRoot 'StopDial.vst3'
    $current = $null
    if (Test-Path -LiteralPath $target) {
        $current = Get-StopDialBundleEvidence -BundlePath $target
        if ($current.tree_sha256 -eq $backup.tree_sha256) {
            return [pscustomobject]@{ status = 'already-restored'; version = $current.version; target = $target; backup_path = $null; binary_sha256 = $current.binary_sha256 }
        }
    }
    if (-not $Cmdlet.ShouldProcess($target, ('Restore StopDial ' + $backup.version + ' from verified backup'))) {
        return [pscustomobject]@{ status = 'what-if'; version = $backup.version; target = $target; backup_path = $null; binary_sha256 = $backup.binary_sha256 }
    }

    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $currentBackup = $null
    if ($null -ne $current) { $currentBackup = New-StopDialBackup -BundlePath $target -Reason 'pre-rollback' -Target $target }
    $token = [Guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $targetRoot ('.StopDial-rollback-stage-' + $token)
    $displaced = Join-Path $targetRoot ('.StopDial-rollback-previous-' + $token)
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
    try {
        Copy-Item -LiteralPath $backupBundle -Destination $stageRoot -Recurse
        $stagedBundle = Join-Path $stageRoot 'StopDial.vst3'
        $staged = Get-StopDialBundleEvidence -BundlePath $stagedBundle
        if ($staged.tree_sha256 -ne $backup.tree_sha256) { throw 'Staged rollback bundle differs from backup.' }
        if (Test-Path -LiteralPath $target) { Move-Item -LiteralPath $target -Destination $displaced }
        Move-Item -LiteralPath $stagedBundle -Destination $target
        $restored = Get-StopDialBundleEvidence -BundlePath $target
        if ($restored.tree_sha256 -ne $backup.tree_sha256) { throw 'Restored bundle failed verification.' }
        if (Test-Path -LiteralPath $displaced) { Remove-Item -LiteralPath $displaced -Recurse -Force -ErrorAction SilentlyContinue }
    }
    catch {
        if (Test-Path -LiteralPath $target) {
            Move-Item -LiteralPath $target -Destination (Join-Path $targetRoot ('.StopDial-rollback-failed-' + $token)) -ErrorAction SilentlyContinue
        }
        if ((Test-Path -LiteralPath $displaced) -and -not (Test-Path -LiteralPath $target)) {
            Move-Item -LiteralPath $displaced -Destination $target -ErrorAction SilentlyContinue
        }
        throw
    }
    finally { if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue } }
    [pscustomobject]@{ status = 'rolled-back'; version = $restored.version; target = $target; backup_path = $currentBackup; binary_sha256 = $restored.binary_sha256 }
}

if ($env:OS -ne 'Windows_NT') { throw 'StopDial Windows VST3 installation is supported only on Windows.' }
if ($PSCmdlet.ParameterSetName -eq 'Install') { Invoke-StopDialInstall -Cmdlet $PSCmdlet }
else { Invoke-StopDialRollback -Cmdlet $PSCmdlet }
