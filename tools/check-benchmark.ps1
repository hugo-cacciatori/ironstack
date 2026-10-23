# In-memory CPU coverage; writes JSON/text evidence only, never audio.
[CmdletBinding()]
param(
    [string]$Executable = 'build/windows-release/benchmark_tool.exe',
    [string]$OutputDirectory = 'build/m5.3b-cpu'
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$cases = @(
    @{ Rate = 48000; Block = 512; Default = $true },
    @{ Rate = 44100; Block = 64 },
    @{ Rate = 96000; Block = 73 },
    @{ Rate = 192000; Block = 512 },
    @{ Rate = 48000; Block = 1 },
    @{ Rate = 48000; Block = 8192 }
)
$results = foreach ($case in $cases) {
    $arguments = if ($case.Default) { @() } else {
        @('--sample-rate', "$($case.Rate)", '--block-size', "$($case.Block)")
    }
    $json = & $binary @arguments
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $arguments" }
    $report = $json | ConvertFrom-Json
    if ($report.sampleRate -ne $case.Rate -or $report.blockSize -ne $case.Block -or
        $report.blocksPerTrial -ne [Math]::Ceiling(10.0 * $case.Rate / $case.Block) -or
        $report.warmupBlocks -ne [Math]::Ceiling(2.0 * $case.Rate / $case.Block) -or
        $report.trialCpuPercent.Count -ne 5 -or $report.peak -le 0 -or $report.peak -ge 1 -or
        $report.energy -le 0 -or $report.guitarVoices -ne 8 -or $report.bassVoices -ne 1) {
        throw "Invalid workload/output report: $arguments"
    }
    foreach ($value in @($report.trialCpuPercent) + @($report.energy, $report.peak,
        $report.medianCpuPercent, $report.medianBlockMs, $report.worstBlockMs)) {
        if (-not [double]::IsFinite($value) -or $value -le 0) { throw 'Invalid measurement.' }
    }
    $median = @($report.trialCpuPercent | Sort-Object)[2]
    if ([Math]::Abs($median - $report.medianCpuPercent) -gt 0.0002 -or
        $report.blocksOverDeadline -lt 0 -or $report.blocksOverDeadline -gt 5 * $report.blocksPerTrial) {
        throw 'Invalid median/deadline count.'
    }
    $json | Set-Content -LiteralPath (Join-Path $OutputDirectory "$($case.Rate)-$($case.Block).json")
    Write-Host "$($case.Rate) Hz / $($case.Block): $($report.medianCpuPercent)% median; $($report.blocksOverDeadline) deadline misses"
    $report
}
# Rejection must precede processing and must not emit a partial JSON report.
$invalid = @(
    @('--sample-rate'), @('--unknown', '1'), @('--sample-rate', '0'),
    @('--sample-rate', '44099'), @('--sample-rate', '192001'),
    @('--sample-rate', '48000x'), @('--sample-rate', '48000.5'),
    @('--sample-rate', '999999999999'), @('--sample-rate', 'nan'),
    @('--block-size', '0'), @('--block-size', '-1'), @('--block-size', '8193'),
    @('--block-size', '1', '--block-size', '2'),
    @('--sample-rate', '48000', '--sample-rate', '48000')
)
foreach ($arguments in $invalid) {
    $output = & $binary @arguments 2> (Join-Path $OutputDirectory 'last-rejection.txt')
    if ($LASTEXITCODE -ne 2 -or $output) { throw "Invalid CLI accepted: $arguments" }
}
$hashes = @('tools/benchmark_tool/main.cpp', 'tools/check-benchmark.ps1', $binary,
    'build/windows-release/src/dsp/ironstack_dsp.lib') | ForEach-Object { Get-FileHash -LiteralPath $_ -Algorithm SHA256 }
@{ Reports = @($results); RejectedInputs = $invalid.Count; Hashes = @($hashes) } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'verification.json')
Write-Host "Passed $($cases.Count) workloads and $($invalid.Count) rejected inputs."
