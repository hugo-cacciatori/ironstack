# Preview by default. Remove generated audio and archive-backed packaging copies;
# preserve text evidence, source, tools and build products.
[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference = 'Stop'

function Assert-StoragePath([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if (-not $full.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside intended storage root: $full"
    }
    # Reject junctions/symlinks at every existing level, not just the leaf.
    for ($cursor = $full; $cursor; $cursor = [IO.Path]::GetDirectoryName($cursor)) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in storage path: $cursor"
            }
        }
        if ($cursor -eq $base) { break }
    }
    return $full
}

function Get-StreamHash($Stream) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($hash.ComputeHash($Stream)) }
    finally { $hash.Dispose() }
}

function Test-ArchiveCopy([string]$CopyRoot, [string]$Archive) {
    # Compare every byte, including manifests/logs. Unexpected files, missing
    # archive members and modified historical copies are retained, not guessed at.
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        foreach ($node in Get-ChildItem -LiteralPath $CopyRoot -Recurse -Force) {
            $null = Assert-StoragePath $node.FullName $CopyRoot
        }
        $entries = @{}
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $name = $entry.FullName.Replace('\', '/')
            if ($entries.ContainsKey($name)) { return $false }
            $entries[$name] = $entry
        }
        foreach ($directory in Get-ChildItem -LiteralPath $CopyRoot -Directory -Recurse -Force) {
            $prefix = [IO.Path]::GetRelativePath($CopyRoot, $directory.FullName).Replace('\', '/') + '/'
            if ($prefix -eq 'rerender/') { continue }
            if (-not @($entries.Keys | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) }).Count) {
                return $false
            }
        }
        $matched = @{}
        foreach ($file in Get-ChildItem -LiteralPath $CopyRoot -File -Recurse -Force) {
            $null = Assert-StoragePath $file.FullName $CopyRoot
            $relative = [IO.Path]::GetRelativePath($CopyRoot, $file.FullName).Replace('\', '/')
            $lookup = $relative
            # Extracted-renderer checks create only these additional outputs;
            # require that even these equal the archive's original regression.
            if ($relative -match '^rerender/(both|bass)-low-riff\.(wav|json)$') {
                $folder = if ($Matches[1] -eq 'both') { 'regression' } else { 'bass-regression' }
                $package = [IO.Path]::GetFileNameWithoutExtension($Archive)
                $lookup = "$package/$folder/low-riff.$($Matches[2])"
            }
            if (-not $entries.ContainsKey($lookup)) { return $false }
            $entry = $entries[$lookup]
            if ($entry.Length -ne $file.Length) { return $false }
            $stream = $entry.Open()
            try { $expected = Get-StreamHash $stream } finally { $stream.Dispose() }
            if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $expected) { return $false }
            if ($relative -eq $lookup) { $matched[$relative] = $true }
        }
        return $matched.Count -eq $entries.Count -and $entries.Count -gt 0
    } finally { $zip.Dispose() }
}

function Get-StorageSize([string]$Root) {
    $logical = 0L; $stored = 0L; $count = 0
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse -Force) {
        $logical += $file.Length; $count++
        $stored += [IronstackStorageSize]::StoredBytes($file.FullName)
    }
    [ordered]@{ logicalBytes = $logical; storedBytes = $stored; files = $count }
}

function Get-GeneratedAudioFiles([string]$BuildRoot) {
    $extensions = @('.wav', '.wave', '.flac', '.mp3', '.aif', '.aiff', '.ogg', '.oga', '.m4a', '.aac', '.wma', '.opus',
                    '.caf', '.au', '.snd', '.aifc', '.w64', '.bwf', '.rf64', '.pcm', '.raw', '.ac3', '.amr', '.mp2', '.mka', '.voc')
    foreach ($file in Get-ChildItem -LiteralPath $BuildRoot -File -Recurse -Force) {
        if ($file.Extension.ToLowerInvariant() -notin $extensions) { continue }
        $null = Assert-StoragePath $file.FullName $BuildRoot
        $file
    }
}

function Get-OptionalAudioArchives([string]$BuildRoot) {
    foreach ($file in Get-ChildItem -LiteralPath $BuildRoot -File -Recurse -Force) {
        # Only our packages/test packages and the redundant JUCE download qualify.
        if ($file.Name -notmatch '^(Ironstack-.+\.(zip|tar\.gz)|JUCE-[0-9a-f]{40}\.zip)$') { continue }
        if ($file.Name -like 'JUCE-*' -and -not (Test-Path -LiteralPath (Join-Path $BuildRoot "vendor/$($file.BaseName)/CMakeLists.txt"))) { continue }
        $null = Assert-StoragePath $file.FullName $BuildRoot
        if ($file.Extension -eq '.zip') {
            $zip = [IO.Compression.ZipFile]::OpenRead($file.FullName)
            try { $names = @($zip.Entries.FullName) } finally { $zip.Dispose() }
        } else {
            $names = @(& tar -tf $file.FullName)
            if ($LASTEXITCODE -ne 0) { throw "Cannot inspect archive: $($file.FullName)" }
        }
        if (@($names | Where-Object { $_ -match '(?i)\.(wav|wave|flac|mp3|aiff?|ogg|oga|m4a|aac|wma|opus|caf|au|snd|aifc|w64|bwf|rf64|pcm|raw|ac3|amr|mp2|mka|voc)$' }).Count) { $file }
    }
}

function Clear-TemporaryAudio([string]$BuildRoot) {
    $repo = Split-Path -Parent $PSScriptRoot
    $null = Assert-StoragePath $BuildRoot (Join-Path $repo 'build')
    foreach ($file in Get-GeneratedAudioFiles $BuildRoot) {
        $path = Assert-StoragePath $file.FullName $BuildRoot
        Write-Output "Remove-Item -LiteralPath '$($path.Replace("'", "''"))' -Force"
        Remove-Item -LiteralPath $path -Force
    }
}

function Save-ArchiveTextEvidence([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    # Read members into safe numbered files: archive paths never become targets.
    $index = @(); $number = 0
    if ([IO.Path]::GetFileName($Archive) -like 'JUCE-*') { return }
    if ($Archive.EndsWith('.zip')) {
        $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
        try {
            foreach ($entry in $zip.Entries) {
                if ($entry.FullName -notmatch '(?i)\.(json|log|txt|mid|midi)$') { continue }
                $name = '{0:D4}{1}' -f $number++, [IO.Path]::GetExtension($entry.FullName)
                $source = $entry.Open(); $target = [IO.File]::Create((Join-Path $Destination $name))
                try { $source.CopyTo($target) } finally { $source.Dispose(); $target.Dispose() }
                $index += [ordered]@{ member = $entry.FullName; savedAs = $name }
            }
        } finally { $zip.Dispose() }
    } else {
        $names = @(& tar -tf $Archive)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot list evidence archive.' }
        foreach ($member in $names | Where-Object { $_ -match '(?i)\.(json|log|txt)$' }) {
            $text = @(& tar -xOf $Archive -- $member)
            if ($LASTEXITCODE -ne 0) { throw 'Cannot read archived text evidence.' }
            $name = '{0:D4}{1}' -f $number++, [IO.Path]::GetExtension($member)
            $text | Set-Content -LiteralPath (Join-Path $Destination $name) -Encoding utf8NoBOM
            $index += [ordered]@{ member = $member; savedAs = $name }
        }
    }
    ConvertTo-Json -InputObject @($index) | Set-Content -LiteralPath (Join-Path $Destination 'index.json') -Encoding utf8NoBOM
}

function Invoke-StorageMaintenance {
    if (-not $IsWindows) { throw 'This maintenance script currently supports Windows/NTFS only.' }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class IronstackStorageSize {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern uint GetCompressedFileSizeW(string name, out uint high);
    public static long StoredBytes(string name) {
        uint high;
        uint low = GetCompressedFileSizeW(name, out high);
        int error = Marshal.GetLastWin32Error();
        if (low == uint.MaxValue && error != 0) throw new Win32Exception(error);
        return (long)(((ulong)high << 32) | low);
    }
}
'@
    $repo = Split-Path -Parent $PSScriptRoot
    $build = Join-Path $repo 'build'
    $windows = Join-Path $build 'windows-release'
    $null = Assert-StoragePath $build $repo
    $null = Assert-StoragePath $windows $build
    if ($Apply) {
        $busy = @(Get-Process | Where-Object ProcessName -match '^(cmake|ctest|nmake|cl|link|pluginval|render_tool|ironstack_.*tests)$')
        if ($busy.Count) { throw "Finish local builds/tests before maintenance: $($busy.ProcessName -join ', ')" }
        $tracked = @(& git -C $repo ls-files -- build)
        if ($LASTEXITCODE -ne 0 -or $tracked.Count) { throw 'Cannot clean a build tree containing tracked files.' }
    }
    $before = Get-StorageSize $repo
    $candidates = @(); $skipped = @(); $audio = @(); $archives = @()
    foreach ($category in @('package-staging', 'package-check')) {
        $parent = Join-Path $windows $category
        if (-not (Test-Path -LiteralPath $parent)) { continue }
        $null = Assert-StoragePath $parent $windows
        foreach ($copy in Get-ChildItem -LiteralPath $parent -Directory -Force) {
            if ($copy.Name -notmatch '^[0-9a-f]{32}$') { continue }
            $copyRoot = Assert-StoragePath $copy.FullName $parent
            $folders = @(Get-ChildItem -LiteralPath $copyRoot -Directory | Where-Object Name -ne 'rerender')
            if ($folders.Count -ne 1) { $skipped += $copyRoot; continue }
            $archive = Join-Path $windows "packages/$($folders[0].Name).zip"
            $checksum = "$archive.sha256"
            if (-not (Test-Path -LiteralPath $archive) -or -not (Test-Path -LiteralPath $checksum)) {
                $skipped += $copyRoot; continue
            }
            $null = Assert-StoragePath $archive $windows
            $expected = ((Get-Content -LiteralPath $checksum -Raw).Trim() -split '\s+')[0]
            if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash -LiteralPath $archive).Hash -ne $expected -or
                -not (Test-ArchiveCopy $copyRoot $archive)) { $skipped += $copyRoot; continue }
            $bytes = ((Get-ChildItem -LiteralPath $copyRoot -File -Recurse -Force) | Measure-Object Length -Sum).Sum
            $command = "Remove-Item -LiteralPath '$($copyRoot.Replace("'", "''"))' -Recurse -Force"
            $candidates += [ordered]@{ path = $copyRoot; archive = $archive; bytes = $bytes;
                archiveSha256 = $expected; command = $command;
                restore = "Expand-Archive -LiteralPath '$archive' -DestinationPath '$copyRoot'" }
            Write-Output $command
            if ($Apply) {
                # Revalidate the absolute boundary immediately before removal.
                $null = Assert-StoragePath $copyRoot $parent
                Remove-Item -LiteralPath $copyRoot -Recurse -Force
            }
        }
    }
    foreach ($file in Get-GeneratedAudioFiles $build) {
        $path = Assert-StoragePath $file.FullName $build
        $command = "Remove-Item -LiteralPath '$($path.Replace("'", "''"))' -Force"
        $audio += [ordered]@{ path = $path; bytes = $file.Length;
            sha256 = (Get-FileHash -LiteralPath $path).Hash; command = $command }
        Write-Output $command
        if ($Apply) {
            $null = Assert-StoragePath $path $build
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($file in Get-OptionalAudioArchives $build) {
        $path = Assert-StoragePath $file.FullName $build
        $hash = (Get-FileHash -LiteralPath $path).Hash
        $evidence = Join-Path $build "storage/archive-evidence/$hash"
        $command = "Remove-Item -LiteralPath '$($path.Replace("'", "''"))' -Force"
        $archives += [ordered]@{ path = $path; bytes = $file.Length; sha256 = $hash; evidence = $evidence; command = $command }
        Write-Output $command
        if ($Apply) {
            $null = Assert-StoragePath $evidence $build
            Save-ArchiveTextEvidence $path $evidence
            $null = Assert-StoragePath $path $build
            Remove-Item -LiteralPath $path -Force
            if (Test-Path -LiteralPath "$path.sha256") {
                $null = Assert-StoragePath "$path.sha256" $build
                Write-Output "Remove-Item -LiteralPath '$path.sha256' -Force"
                Remove-Item -LiteralPath "$path.sha256" -Force
            }
        }
    }
    $report = [ordered]@{ applied = [bool]$Apply;
        thresholdBytes = 3GB; before = $before; after = (Get-StorageSize $repo);
        archiveBackedCopies = $candidates; retainedUnmatchedCopies = $skipped; generatedAudio = $audio; optionalAudioArchives = $archives }
    $reports = Join-Path $build 'storage'
    New-Item -ItemType Directory -Path $reports -Force | Out-Null
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reports 'latest.json') -Encoding utf8NoBOM
    [pscustomobject]@{ Applied = [bool]$Apply; ProjectGiB = [math]::Round($report.after.logicalBytes / 1GB, 3);
        StoredGiB = [math]::Round($report.after.storedBytes / 1GB, 3); ArchiveBackedCopies = $candidates.Count;
        CopyMiB = [math]::Round(($candidates.bytes | Measure-Object -Sum).Sum / 1MB, 2);
        RetainedUnmatchedCopies = $skipped.Count; AudioFiles = $audio.Count; AudioArchives = $archives.Count;
        AboveThreshold = $report.after.storedBytes -gt 3GB }
}

# Dot-source only the read-only helpers for fixture tests.
if ($MyInvocation.InvocationName -ne '.') { Invoke-StorageMaintenance }
