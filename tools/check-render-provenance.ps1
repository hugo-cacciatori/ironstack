# Negative packaging checks use only generated build evidence, restored exactly.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$platform = if ($IsWindows) { 'windows-x64' } elseif ($IsMacOS) { 'macos-arm64' } else { 'linux-x64' }
foreach ($suite in @('standard-sequences', 'bass-sequences')) {
    $receiptPath = Join-Path $buildRoot "$suite-result.json"
    $savedReceipt = [IO.File]::ReadAllBytes($receiptPath)
    $originalReceipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    $summaryPath = Join-Path $originalReceipt.root 'summary.json'
    $savedSummary = [IO.File]::ReadAllBytes($summaryPath)
    $originalSummary = @(Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json)
    $metricPath = Join-Path $originalReceipt.root "$($originalSummary[0].id).json"
    $savedMetric = [IO.File]::ReadAllBytes($metricPath)
    $cases = @(
        @{ name = 'failed-suite'; expected = 'Successful sequence receipt' },
        @{ name = 'outside-build'; expected = 'inside the configured build directory' },
        @{ name = 'stale-commit'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'invalid-dirty'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-version'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-frames'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-mask'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-split'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-destruction'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-mono'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'invalid-mono'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-bass-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-guitar-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'invalid-bass-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'invalid-guitar-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'missing-bass-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'missing-guitar-enable'; expected = 'provenance/settings/results mismatch' },
        @{ name = 'wrong-latency'; expected = 'provenance/settings/results mismatch' }
    )
    if ($suite -eq 'bass-sequences') { $cases += @{ name = 'stereo-bass'; expected = 'provenance/settings/results mismatch' } }
    foreach ($case in $cases) {
        try {
            $receipt = [Text.Encoding]::UTF8.GetString($savedReceipt) | ConvertFrom-Json
            $summary = @([Text.Encoding]::UTF8.GetString($savedSummary) | ConvertFrom-Json)
            switch ($case.name) {
                'failed-suite' { $receipt.passed = $false }
                'outside-build' { $receipt.root = Split-Path -Parent $buildRoot }
                'stale-commit' { $summary[0].metrics.source_commit = '0' * 40 }
                'invalid-dirty' { $summary[0].metrics.source_dirty = 'false' }
                'wrong-version' { $summary[0].metrics.product_version = '0.0.0' }
                'wrong-frames' { $summary[0].metrics.frames++ }
                'wrong-mask' { $summary[0].metrics.layer_mask = 'none' }
                'wrong-split' { $summary[0].metrics.split_hz = 80 }
                'wrong-destruction' { $summary[0].metrics.destruction = 0 }
                'wrong-mono' { $summary[0].metrics.guitar_mono = $true }
                'invalid-mono' { $summary[0].metrics.guitar_mono = 'false' }
                'wrong-bass-enable' { $summary[0].metrics.bass_enabled = $false }
                'wrong-guitar-enable' { $summary[0].metrics.guitar_enabled = -not $summary[0].metrics.guitar_enabled }
                'invalid-bass-enable' { $summary[0].metrics.bass_enabled = 1 }
                'invalid-guitar-enable' { $summary[0].metrics.guitar_enabled = 'false' }
                'missing-bass-enable' { $summary[0].metrics.PSObject.Properties.Remove('bass_enabled') }
                'missing-guitar-enable' { $summary[0].metrics.PSObject.Properties.Remove('guitar_enabled') }
                'wrong-latency' { $summary[0].metrics.latency_samples = 0 }
                'stereo-bass' { $summary[0].metrics.left_minus_right_rms = 0.1 }
            }
            $receipt | ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8NoBOM
            $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
            $summary[0].metrics | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $metricPath -Encoding utf8NoBOM
            try {
                & (Join-Path $PSScriptRoot 'package.ps1') -BuildDirectory $buildRoot -Platform $platform -AllowDirty
                throw "Accepted invalid render evidence: $($case.name)"
            } catch {
                if ($_.Exception.Message -notmatch [regex]::Escape($case.expected)) { throw }
            }
            Write-Output "PASS: $suite evidence rejects $($case.name)"
        } finally {
            [IO.File]::WriteAllBytes($receiptPath, $savedReceipt)
            [IO.File]::WriteAllBytes($summaryPath, $savedSummary)
            [IO.File]::WriteAllBytes($metricPath, $savedMetric)
        }
    }

}
