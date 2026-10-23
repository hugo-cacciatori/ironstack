# Exercise preflight rejection without staging or archiving any binaries.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify test commit.' }
$scratch = Join-Path $repoRoot ('build/provenance-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$base = @{ product = 'Ironstack'; version = '0.1.0'; system = 'Windows'; configuredCommit = $commit;
    configuredDirty = $false; standalone = (Join-Path $scratch 'not-built.exe'); vst3 = (Join-Path $scratch 'not-built.vst3') }
$cases = @(
    @{ name = 'missing'; revision = $null; dirty = $false; expected = 'provenance is missing or stale' },
    @{ name = 'stale'; revision = ('0' * 40); dirty = $false; expected = 'provenance is missing or stale' },
    @{ name = 'invalid-state'; revision = $commit; dirty = 'false'; expected = 'provenance is missing or stale' },
    @{ name = 'dirty-configuration'; revision = $commit; dirty = $true; expected = 'Build was configured from a dirty checkout' },
    @{ name = 'matching'; revision = $commit; dirty = $false; expected = 'Missing compiled artifact' }
)
foreach ($case in $cases) {
    $info = $base.Clone()
    $info.configuredCommit = $case.revision
    $info.configuredDirty = $case.dirty
    $info | ConvertTo-Json | Set-Content (Join-Path $scratch 'package-info-Release.json') -Encoding utf8NoBOM
    $failure = ''
    try { & (Join-Path $PSScriptRoot 'package.ps1') -BuildDirectory $scratch -Platform windows-x64 -AllowDirty:($case.name -ne 'dirty-configuration') }
    catch { $failure = $_.Exception.Message }
    if (-not $failure.Contains($case.expected)) { throw "Case $($case.name) failed: $failure" }
    if (Test-Path (Join-Path $scratch 'package-staging')) { throw 'Preflight unexpectedly staged files.' }
    Write-Output "PASS: $($case.name)"
}
Write-Output "Provenance preflight cases passed; fixtures retained at $scratch"
