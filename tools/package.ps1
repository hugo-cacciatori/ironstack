# Package compiled bundles without installing them or deleting prior artifacts.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [ValidateSet('windows-x64', 'linux-x64', 'macos-arm64')][string]$Platform,
    [string]$Configuration = 'Release',
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
. (Join-Path $PSScriptRoot 'maintain-storage.ps1')
$null = Assert-StoragePath $buildRoot (Join-Path $repoRoot 'build')
try {
$info = Get-Content -LiteralPath (Join-Path $buildRoot "package-info-$Configuration.json") -Raw | ConvertFrom-Json
$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not identify source commit.' }
$sourceStatus = & git -C $repoRoot status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Could not identify source working-tree state.' }
$dirty = [bool]$sourceStatus
if ($info.configuredCommit -ne $commit -or $info.configuredDirty -isnot [bool]) {
    throw 'Build configuration provenance is missing or stale. Reconfigure, rebuild and retest this commit before packaging.'
}
if ($info.configuredDirty -and -not $AllowDirty) {
    throw 'Build was configured from a dirty checkout. Reconfigure, rebuild and retest a clean commit before packaging.'
}
if ($dirty -and -not $AllowDirty) { throw 'Refusing to package a dirty checkout; use -AllowDirty only for local checks.' }
# Local smoke packages retain configuration dirtiness; packaging a now-clean
# working tree must not make them appear to be clean release candidates.
$dirty = $dirty -or $info.configuredDirty
$expectedSystem = @{ 'windows-x64' = 'Windows'; 'linux-x64' = 'Linux'; 'macos-arm64' = 'Darwin' }
if (-not $Platform -or $info.system -ne $expectedSystem[$Platform]) { throw 'Platform does not match the CMake build.' }
foreach ($artifact in @($info.standalone, $info.vst3)) {
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Missing compiled artifact: $artifact" }
}
$renderer = (Get-Content -LiteralPath (Join-Path $buildRoot "render-tool-$Configuration.path") -Raw).Trim()
if (-not (Test-Path -LiteralPath $renderer -PathType Leaf)) { throw 'Missing compiled renderer.' }
$fixtures = Get-Content -LiteralPath (Join-Path $repoRoot 'docs/test-sequences/fixtures.json') -Raw | ConvertFrom-Json
$renderSuites = @(
    @{ receipt = 'standard-sequences-result.json'; mask = 'both'; guitar = $true; folder = 'regression' },
    @{ receipt = 'bass-sequences-result.json'; mask = 'bass'; guitar = $false; folder = 'bass-regression' }
)
# Build cleanup removes transient WAVs. Recreate just the two file/CLI suites
# when packaging needs their hashes; do not rerun the full DSP test matrix.
$missingAudio = $false
foreach ($suite in $renderSuites) {
    $receiptPath = Join-Path $buildRoot $suite.receipt
    if (-not (Test-Path -LiteralPath $receiptPath)) { $missingAudio = $true; continue }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    foreach ($fixture in $fixtures.sequences) {
        if (-not (Test-Path -LiteralPath (Join-Path $receipt.root "$($fixture.id).wav"))) { $missingAudio = $true }
    }
}
foreach ($suite in $renderSuites) {
    $receipt = Get-Content -LiteralPath (Join-Path $buildRoot $suite.receipt) -Raw | ConvertFrom-Json
    if ($receipt.schema_version -ne 1 -or $receipt.passed -isnot [bool] -or -not $receipt.passed) {
        throw 'Successful sequence receipt required.'
    }
    $renderRoot = [IO.Path]::GetFullPath($receipt.root)
    if (-not $renderRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Sequence artifacts must be inside the configured build directory.'
    }
    $suite.root = $renderRoot
    $summary = @(Get-Content -LiteralPath (Join-Path $renderRoot 'summary.json') -Raw | ConvertFrom-Json)
    if ($summary.Count -ne $fixtures.sequences.Count) { throw 'Incomplete sequence summary.' }
    foreach ($fixture in $fixtures.sequences) {
        $result = @($summary | Where-Object { $_.id -eq $fixture.id })
        if ($result.Count -ne 1) { throw 'Missing or duplicate sequence result.' }
        $metrics = Get-Content -LiteralPath (Join-Path $renderRoot "$($fixture.id).json") -Raw | ConvertFrom-Json
        if (($result[0].metrics | ConvertTo-Json -Depth 10 -Compress) -ne ($metrics | ConvertTo-Json -Depth 10 -Compress)) {
            throw 'Render summary differs from individual metrics.'
        }
        $expectedFrames = [long][Math]::Round($fixture.length_beats * 60.0 / $fixtures.tempo_bpm * $fixtures.sample_rate,
            [MidpointRounding]::AwayFromZero) + [long][Math]::Ceiling($fixtures.tail_seconds * $fixtures.sample_rate)
        foreach ($measured in @($result[0].metrics, $metrics)) {
            if ($measured.source_commit -ne $info.configuredCommit -or $measured.product_version -ne $info.version -or
                $measured.source_dirty -isnot [bool] -or $measured.source_dirty -ne $info.configuredDirty -or
                $measured.sample_rate -ne $fixtures.sample_rate -or $measured.block_size -ne $fixtures.block_size -or
                $measured.output_db -ne $fixtures.output_db -or $measured.tail_seconds -ne $fixtures.tail_seconds -or
                $measured.layer_mask -cne $suite.mask -or $measured.split_hz -ne 140 -or $measured.destruction -ne 0.5 -or
                $measured.bass_enabled -isnot [bool] -or $measured.bass_enabled -ne $true -or
                $measured.guitar_enabled -isnot [bool] -or $measured.guitar_enabled -ne $suite.guitar -or
                $measured.guitar_mono -isnot [bool] -or $measured.guitar_mono -ne $false -or
                $measured.latency_samples -ne 32 -or ($suite.mask -eq 'bass' -and $measured.left_minus_right_rms -ne 0) -or
                $measured.clipped_samples -ne 0 -or $measured.nonfinite_samples -ne 0 -or
                $measured.peak -le 0 -or $measured.peak -ge 1 -or $measured.frames -ne $expectedFrames) {
                throw 'Render provenance/settings/results mismatch; rebuild and rerun CTest.'
            }
        }
        foreach ($extension in @('mid')) {
            if (-not (Test-Path -LiteralPath (Join-Path $renderRoot "$($fixture.id).$extension") -PathType Leaf)) {
                throw 'Missing sequence MIDI artifact.'
            }
        }
    }
}
# Validate existing text provenance before regenerating anything: a corrupt
# receipt must fail rather than being silently replaced by a successful rerun.
if ($missingAudio) {
    $ctestLine = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') | Where-Object { $_ -match '^CMAKE_CTEST_COMMAND:INTERNAL=' }
    if (-not $ctestLine) { throw 'Cannot locate configured CTest; rerun the sequence suites before packaging.' }
    $ctest = $ctestLine.Substring($ctestLine.IndexOf('=') + 1)
    & $ctest --test-dir $buildRoot -C $Configuration --output-on-failure -R '^(standard_sequences|bass_sequences)$'
    if ($LASTEXITCODE -ne 0) { throw 'Packaging sequence verification failed.' }
    foreach ($suite in $renderSuites) {
        $receipt = Get-Content -LiteralPath (Join-Path $buildRoot $suite.receipt) -Raw | ConvertFrom-Json
        if (-not $receipt.passed) { throw 'Regenerated sequence receipt failed.' }
        $suite.root = Assert-StoragePath $receipt.root $buildRoot
        foreach ($fixture in $fixtures.sequences) {
            $metrics = Get-Content -LiteralPath (Join-Path $suite.root "$($fixture.id).json") -Raw | ConvertFrom-Json
            if ($metrics.source_commit -ne $info.configuredCommit -or $metrics.product_version -ne $info.version -or
                $metrics.source_dirty -ne $info.configuredDirty -or $metrics.layer_mask -cne $suite.mask -or
                $metrics.clipped_samples -ne 0 -or $metrics.nonfinite_samples -ne 0) {
                throw 'Regenerated render provenance/settings/results mismatch.'
            }
        }
    }
}
if ($Platform -eq 'windows-x64') {
    $vstBinary = Join-Path $info.vst3 'Contents/x86_64-win/Ironstack.vst3'
    foreach ($executable in @($info.standalone, $vstBinary)) {
        $versions = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
        if ($versions.ProductVersion -ne $info.version -or $versions.FileVersion -ne $info.version) {
            throw "Windows binary version differs from package metadata: $executable. Reconfigure and rebuild before packaging."
        }
    }
}
$packageName = "$($info.product)-$($info.version)-$Platform-$($commit.Substring(0, 8))"
$stageParent = Join-Path $buildRoot ('package-staging/' + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $stageParent $packageName
$destination = Join-Path $buildRoot 'packages'
New-Item -ItemType Directory -Path $stage, $destination -Force | Out-Null
Copy-Item -LiteralPath $info.standalone -Destination $stage -Recurse
Copy-Item -LiteralPath $info.vst3 -Destination $stage -Recurse
Copy-Item -LiteralPath $renderer -Destination $stage
foreach ($suite in $renderSuites) {
    $regression = Join-Path $stage $suite.folder
    New-Item -ItemType Directory -Path $regression | Out-Null
    foreach ($fixture in $fixtures.sequences) {
        foreach ($extension in @('mid', 'json')) {
            Copy-Item -LiteralPath (Join-Path $suite.root "$($fixture.id).$extension") -Destination $regression
        }
    }
    $hashes = [ordered]@{}
    foreach ($fixture in $fixtures.sequences) {
        $hashes[$fixture.id] = (Get-FileHash -LiteralPath (Join-Path $suite.root "$($fixture.id).wav")).Hash.ToLowerInvariant()
    }
    $hashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $regression 'audio-sha256.json') -Encoding utf8NoBOM
    Copy-Item -LiteralPath (Join-Path $suite.root 'summary.json') -Destination $regression
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/test-sequences/fixtures.json') -Destination $regression
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'packaging/RENDERING.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/notices') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE.txt') -Destination (Join-Path $stage 'PROJECT-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "packaging/$Platform.txt") -Destination (Join-Path $stage 'INSTALL.txt')
if ($Platform -ne 'windows-x64') {
    if (([IO.File]::GetUnixFileMode((Join-Path $stage (Split-Path -Leaf $renderer))) -band [IO.UnixFileMode]::UserExecute) -eq 0) {
        throw 'Packaged renderer lost its executable permission.'
    }
    $executable = if ($Platform -eq 'macos-arm64') {
        Join-Path $stage "$($info.product).app/Contents/MacOS/$($info.product)"
    } else { Join-Path $stage $info.product }
    if (([System.IO.File]::GetUnixFileMode($executable) -band [System.IO.UnixFileMode]::UserExecute) -eq 0) {
        throw 'Packaged Standalone lost its executable permission.'
    }
}
if ($Platform -eq 'linux-x64') {
    $systemNotices = Join-Path $stage 'system-notices'
    New-Item -ItemType Directory -Path $systemNotices | Out-Null
    foreach ($pattern in @('libasound2*','libfreetype*','libfontconfig*','libx11*','libxext*','libxrender*',
                           'libxrandr*','libxcursor*','libxinerama*','libxi6*','libxcomposite*','libstdc++6*','libgcc-s1*','libc6*')) {
        foreach ($entry in (Get-ChildItem /usr/share/doc -Directory -Filter $pattern)) {
            $copyright = Join-Path $entry.FullName 'copyright'
            if (Test-Path -LiteralPath $copyright) {
                Copy-Item -LiteralPath $copyright -Destination (Join-Path $systemNotices "$($entry.Name).txt")
            }
        }
    }
    foreach ($licenseName in @('LGPL-2.1', 'LGPL-3', 'GPL-2', 'GPL-3')) {
        Copy-Item -LiteralPath "/usr/share/common-licenses/$licenseName" -Destination (Join-Path $systemNotices "$licenseName.txt")
    }
}
foreach ($evidence in @('m1.4-editor.png', 'ci-environment.txt')) {
    $evidencePath = Join-Path $buildRoot $evidence
    if (Test-Path -LiteralPath $evidencePath -PathType Leaf) { Copy-Item -LiteralPath $evidencePath -Destination $stage }
}
$testLog = Join-Path $buildRoot 'Testing/Temporary/LastTest.log'
if (-not (Test-Path -LiteralPath $testLog)) { throw 'CTest evidence is required before packaging.' }
Copy-Item -LiteralPath $testLog -Destination (Join-Path $stage 'CTest.log')
$validatorLog = Join-Path $repoRoot 'build/pluginval/ironstack-strictness5.txt'
if ($Platform -eq 'windows-x64') {
    $receiptPath = Join-Path $repoRoot 'build/pluginval/result.json'
    if (-not (Test-Path -LiteralPath $validatorLog) -or -not (Test-Path -LiteralPath $receiptPath)) {
        throw 'Windows package requires pluginval log and validation receipt.'
    }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    $binary = Get-ChildItem -LiteralPath (Join-Path $info.vst3 'Contents/x86_64-win') -File -Filter '*.vst3' | Select-Object -First 1
    if (-not $binary -or -not $receipt.passed -or $receipt.strictness -ne 5 -or
        $receipt.pluginSha256 -ne (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash) {
        throw 'pluginval receipt does not match this VST3 binary/strictness.'
    }
    Copy-Item -LiteralPath $validatorLog -Destination (Join-Path $stage 'pluginval.log')
    Copy-Item -LiteralPath $receiptPath -Destination (Join-Path $stage 'pluginval-result.json')
}
$manifest = [ordered]@{ product = $info.product; version = $info.version; platform = $Platform;
    commit = $info.configuredCommit; dirty = $dirty; configuration = $Configuration;
    standalone = (Split-Path -Leaf $info.standalone); vst3 = (Split-Path -Leaf $info.vst3);
    renderer = (Split-Path -Leaf $renderer); regression = 'regression'; bassRegression = 'bass-regression';
    distribution = 'Unsigned development build; free binary use; see PROJECT-LICENSE.txt and INSTALL.txt for permissions and validation scope' }
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding utf8NoBOM
$hashLines = foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)) {
    $relative = [System.IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
$hashLines | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding utf8NoBOM
# tar preserves executable bits and app/bundle structure on Unix. Windows uses ZIP.
$extension = if ($Platform -eq 'windows-x64') { 'zip' } else { 'tar.gz' }
$archive = Join-Path $destination "$packageName.$extension"
if (Test-Path -LiteralPath $archive) { throw "Package already exists: $archive" }
if ($Platform -eq 'windows-x64') {
    Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
} else {
    & tar -czf $archive -C $stageParent $packageName
    if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed.' }
}
if ((Get-Item -LiteralPath $archive).Length -gt 50MB) { throw 'Package exceeds the 50 MiB CI artifact budget.' }
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  $(Split-Path -Leaf $archive)" | Set-Content -LiteralPath "$archive.sha256" -Encoding utf8NoBOM
Write-Output "Package: $archive"
Write-Output "SHA256: $archiveHash"
} finally {
    Clear-TemporaryAudio $buildRoot
}
