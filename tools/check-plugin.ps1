# Validate the built VST3 with the pinned external validator, including GUI tests.
[CmdletBinding()]
param([string]$PluginPath = '')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pins = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows-toolchain.json') -Raw | ConvertFrom-Json
$validator = Join-Path $repoRoot "build/tools/pluginval-$($pins.pluginval.version)/pluginval.exe"
$plugin = if ($PluginPath) { (Resolve-Path -LiteralPath $PluginPath).Path } else {
    Join-Path $repoRoot 'build/windows-release/Ironstack_artefacts/Release/VST3/Ironstack.vst3'
}
$logs = Join-Path $repoRoot 'build/pluginval'
if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) { throw 'Provision pluginval first; see docs/build.md.' }
if (-not (Test-Path -LiteralPath $plugin -PathType Container)) { throw 'Build the Release VST3 first.' }
New-Item -ItemType Directory -Path $logs -Force | Out-Null
# Start-Process joins these arguments into a command line; quote paths explicitly.
foreach ($argumentPath in @($plugin, $logs)) {
    if ($argumentPath.Contains('"')) { throw 'Unsupported quote in validation path.' }
}
$arguments = @('--strictness-level', '5', '--random-seed', '12345',
    '--output-dir', ('"{0}"' -f $logs), '--output-filename', 'ironstack-strictness5.txt',
    '--validate', ('"{0}"' -f $plugin))
$process = Start-Process -FilePath $validator -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $logs 'stdout.txt') -RedirectStandardError (Join-Path $logs 'stderr.txt')
Get-Content -LiteralPath (Join-Path $logs 'stdout.txt')
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $logs 'stderr.txt')
    throw "pluginval failed (exit $($process.ExitCode)); logs: $logs"
}
$binary = Get-ChildItem -LiteralPath (Join-Path $plugin 'Contents/x86_64-win') -File -Filter '*.vst3' | Select-Object -First 1
if (-not $binary) { throw 'Could not identify the validated Windows binary.' }
[ordered]@{ passed = $true; validatorVersion = $pins.pluginval.version; strictness = 5; seed = 12345;
    pluginSha256 = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logs 'result.json') -Encoding utf8NoBOM
Write-Output "pluginval strictness 5 passed; logs: $logs"
