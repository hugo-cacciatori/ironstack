# Check every repository Markdown file, including untracked/unignored new docs, and the private
# docs (AGENTS.md, CLAUDE.md, docs/), which git ignores (owner, 2026-10-23), whenever they are present.
# Use byte and line limits: short minified lines must not evade the context budget.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$limits = @{
    'AGENTS.md' = @(8192, 100)
    'CLAUDE.md' = @(1024, 20)
    'docs/progress.md' = @(3072, 45)
    'docs/roadmap.md' = @(10240, 130)
    'docs/sound-spec.md' = @(12288, 170)
    'docs/testing.md' = @(8192, 110)
    'docs/releases.md' = @(6144, 85)
}
$paths = @(& git -C $repoRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- '*.md')
if ($LASTEXITCODE -ne 0) { throw 'Could not list repository documentation.' }
$paths += @('AGENTS.md', 'CLAUDE.md')
$docsRoot = Join-Path $repoRoot 'docs'
if (Test-Path -LiteralPath $docsRoot -PathType Container) {
    $paths += @(Get-ChildItem -LiteralPath $docsRoot -Recurse -File -Filter '*.md' | ForEach-Object {
        $_.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
    })
}
$failures = 0
foreach ($relativePath in ($paths | Sort-Object -Unique)) {
    $fullPath = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { continue }
    $budget = if ($limits.ContainsKey($relativePath)) { $limits[$relativePath] } else { @(8192, 120) }
    $byteCount = (Get-Item -LiteralPath $fullPath).Length
    $lineCount = [System.IO.File]::ReadAllLines($fullPath).Length
    $withinBudget = $byteCount -le $budget[0] -and $lineCount -le $budget[1]
    $label = if ($withinBudget) { 'OK' } else { 'OVER LIMIT' }
    Write-Output ("{0}: {1} ({2}/{3} bytes, {4}/{5} lines)" -f $label, $relativePath, $byteCount, $budget[0], $lineCount, $budget[1])
    if (-not $withinBudget) { $failures++ }
}
if ($failures -gt 0) {
    Write-Error "$failures documentation file(s) exceed their budget. Condense or split by topic without losing requirements."
    exit 1
}
