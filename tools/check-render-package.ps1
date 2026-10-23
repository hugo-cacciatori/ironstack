# Verify the actual archive and execute its extracted headless renderer.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][ValidateSet('windows-x64', 'linux-x64', 'macos-arm64')][string]$Platform,
    [string]$Configuration = 'Release',
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'maintain-storage.ps1')
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$info = Get-Content -LiteralPath (Join-Path $buildRoot "package-info-$Configuration.json") -Raw | ConvertFrom-Json
$name = "$($info.product)-$($info.version)-$Platform-$($info.configuredCommit.Substring(0, 8))"
$extension = if ($Platform -eq 'windows-x64') { 'zip' } else { 'tar.gz' }
$archive = Join-Path $buildRoot "packages/$name.$extension"
$checksum = ((Get-Content -LiteralPath "$archive.sha256" -Raw).Trim() -split '\s+')[0]
if ((Get-FileHash -LiteralPath $archive).Hash -ne $checksum) { throw 'Archive checksum mismatch.' }
$extract = Join-Path $buildRoot ('package-check/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $extract | Out-Null
try {
if ($Platform -eq 'windows-x64') { Expand-Archive -LiteralPath $archive -DestinationPath $extract }
else {
    & tar -xzf $archive -C $extract
    if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed.' }
}
$root = Join-Path $extract $name
$manifest = Get-Content -LiteralPath (Join-Path $root 'build-info.json') -Raw | ConvertFrom-Json
if ($manifest.commit -ne $info.configuredCommit -or $manifest.version -ne $info.version -or
    $manifest.configuration -ne $Configuration -or $manifest.platform -ne $Platform -or
    $manifest.dirty -isnot [bool] -or ($manifest.dirty -and -not $AllowDirty)) { throw 'Package provenance mismatch.' }
$count = 0
foreach ($line in Get-Content -LiteralPath (Join-Path $root 'SHA256SUMS.txt')) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Malformed payload checksum.' }
    $expected = $Matches[1]
    $path = [IO.Path]::GetFullPath((Join-Path $root $Matches[2]))
    if (-not $path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Checksum path outside package.'
    }
    if ((Get-FileHash -LiteralPath $path).Hash -ne $expected) { throw "Payload checksum mismatch: $path" }
    $count++
}
$renderer = Join-Path $root $manifest.renderer
$originalRenderer = (Get-Content -LiteralPath (Join-Path $buildRoot "render-tool-$Configuration.path") -Raw).Trim()
if ((Get-FileHash -LiteralPath $renderer).Hash -ne (Get-FileHash -LiteralPath $originalRenderer).Hash) {
    throw 'Packaged renderer differs from the built executable.'
}
$rerender = Join-Path $extract 'rerender'
New-Item -ItemType Directory -Path $rerender | Out-Null
$renderHashes = @{}
foreach ($suite in @(@{ folder = $manifest.regression; mask = 'both'; guitar = $true }, @{ folder = $manifest.bassRegression; mask = 'bass'; guitar = $false })) {
    if ($suite.folder -notin @('regression', 'bass-regression')) { throw 'Unknown packaged regression directory.' }
    $wav = Join-Path $rerender "$($suite.mask)-low-riff.wav"
    $json = Join-Path $rerender "$($suite.mask)-low-riff.json"
    & $renderer (Join-Path $root "$($suite.folder)/low-riff.mid") $wav $json --layer-mask $suite.mask --guitar-enabled ([int]$suite.guitar)
    if ($LASTEXITCODE -ne 0) { throw 'Extracted renderer execution failed.' }
    $reference = Join-Path $root "$($suite.folder)/audio-sha256.json"
    $expectedHash = if (Test-Path -LiteralPath $reference) {
        (Get-Content -LiteralPath $reference -Raw | ConvertFrom-Json).'low-riff'
    } else {
        # Old published packages used a bundled WAV; retain verification support.
        (Get-FileHash -LiteralPath (Join-Path $root "$($suite.folder)/low-riff.wav")).Hash
    }
    if ((Get-FileHash -LiteralPath $wav).Hash -ne $expectedHash) {
        throw 'Packaged renderer does not reproduce the recorded WAV hash.'
    }
    $metrics = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
    if ($metrics.source_commit -ne $info.configuredCommit -or $metrics.product_version -ne $info.version -or
        $metrics.source_dirty -ne $info.configuredDirty -or $metrics.clipped_samples -ne 0 -or $metrics.nonfinite_samples -ne 0 -or
        $metrics.layer_mask -cne $suite.mask -or $metrics.split_hz -ne 140 -or $metrics.destruction -ne 0.5 -or
        $metrics.bass_enabled -isnot [bool] -or $metrics.bass_enabled -ne $true -or
        $metrics.guitar_enabled -isnot [bool] -or $metrics.guitar_enabled -ne $suite.guitar -or
        $metrics.guitar_mono -isnot [bool] -or $metrics.guitar_mono -ne $false -or
        $metrics.latency_samples -ne 32 -or ($suite.mask -eq 'bass' -and $metrics.left_minus_right_rms -ne 0)) {
        throw 'Extracted render metrics/provenance mismatch.'
    }
    $renderHashes[$suite.mask] = (Get-FileHash -LiteralPath $wav).Hash.ToLowerInvariant()
}
[ordered]@{ commit = $info.configuredCommit; version = $info.version; platform = $Platform;
    dirty = $manifest.dirty; archiveSha256 = $checksum; payloads = $count; extractedRendererPassed = $true;
    lowRiffWavSha256 = $renderHashes['both']; bassLowRiffWavSha256 = $renderHashes['bass']; bassRendererPassed = $true } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'package-verification.json') -Encoding utf8NoBOM
Write-Output "Verified $count payload hashes and byte-exact mixed/bass extracted-renderer reproduction."
} finally {
    Clear-TemporaryAudio $extract
}
