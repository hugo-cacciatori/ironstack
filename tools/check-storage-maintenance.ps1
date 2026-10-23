# Disposable safety fixtures are removed in finally; pass/fail text is retained.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'maintain-storage.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$scratch = Join-Path $repo ('build/storage-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
$package = 'Ironstack-0.4.0-windows-x64-test'
$source = Join-Path $scratch 'source'
$inside = Join-Path $source $package
New-Item -ItemType Directory -Path (Join-Path $inside 'regression'), (Join-Path $inside 'bass-regression') | Out-Null
foreach ($file in @('program.exe', 'regression/low-riff.wav', 'regression/low-riff.json',
                    'bass-regression/low-riff.wav', 'bass-regression/low-riff.json')) {
    [IO.File]::WriteAllText((Join-Path $inside $file), "fixture:$file")
}
$archive = Join-Path $scratch "$package.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($source, $archive)
$copy = Join-Path $scratch 'copy'
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $copy)
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    Write-Output "PASS: $Message"
}
Assert (Test-ArchiveCopy $copy $archive) 'Exact extracted archive is disposable'
$partial = Join-Path $scratch 'partial'
New-Item -ItemType Directory -Path (Join-Path $partial $package) | Out-Null
Copy-Item (Join-Path $inside 'program.exe') (Join-Path $partial "$package/program.exe")
Assert (-not (Test-ArchiveCopy $partial $archive)) 'Incomplete archive copy is retained'
$extra = Join-Path $scratch 'extra-directory'
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $extra)
New-Item -ItemType Directory -Path (Join-Path $extra 'owner-folder') | Out-Null
Assert (-not (Test-ArchiveCopy $extra $archive)) 'Unexpected empty directory is retained'
$program = Join-Path $copy "$package/program.exe"
[IO.File]::WriteAllText($program, 'modified')
Assert (-not (Test-ArchiveCopy $copy $archive)) 'Modified data is retained'
[IO.File]::WriteAllText($program, 'fixture:program.exe')
$rerender = Join-Path $copy 'rerender'
New-Item -ItemType Directory -Path $rerender | Out-Null
foreach ($pair in @(@('both', 'regression'), @('bass', 'bass-regression'))) {
    foreach ($extension in @('wav', 'json')) {
        Copy-Item (Join-Path $inside "$($pair[1])/low-riff.$extension") (Join-Path $rerender "$($pair[0])-low-riff.$extension")
    }
}
Assert (Test-ArchiveCopy $copy $archive) 'Identical regenerated WAVs/metrics are archive-backed too'
[IO.File]::WriteAllText((Join-Path $rerender 'both-low-riff.wav'), 'changed')
Assert (-not (Test-ArchiveCopy $copy $archive)) 'Changed render evidence is retained'
Copy-Item (Join-Path $inside 'regression/low-riff.wav') (Join-Path $rerender 'both-low-riff.wav')
[IO.File]::WriteAllText((Join-Path $copy 'owner-notes.txt'), 'keep')
Assert (-not (Test-ArchiveCopy $copy $archive)) 'Unexpected owner file prevents cleanup'
foreach ($path in @($scratch, (Join-Path $scratch '../outside'))) {
    $rejected = $false
    try { $null = Assert-StoragePath $path $scratch } catch { $rejected = $true }
    Assert $rejected 'Root and parent-traversal targets rejected'
}
$null = Assert-StoragePath $program $scratch
Assert (Test-Path -LiteralPath (Join-Path $copy 'owner-notes.txt')) 'Selection checks preserve unexpected owner data'
Write-Output "Safety fixtures verified: $scratch"

$selection = Join-Path $scratch 'audio-selection'
foreach ($name in @('current/accepted.wav', 'old/repeat.WAV', 'old/audition.flac',
                    'current/summary.json', 'current/sequence.mid', 'current/program.exe',
                    'vendor/JUCE/example.wav', 'tools/example.mp3', '_deps/example.ogg', 'vendor/JUCE/notification.caf')) {
    $target = Join-Path $selection $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    [IO.File]::WriteAllText($target, 'fixture')
}
$audio = @(Get-GeneratedAudioFiles $selection)
Assert ($audio.Count -eq 7) 'Audio selected across current, accepted, dependency and tool folders'
Assert (@($audio | Where-Object Extension -eq '.flac').Count -eq 1) 'Non-WAV audio selected too'
Assert (@($audio | Where-Object Extension -eq '.caf').Count -eq 1) 'CAF dependency notification audio selected'
Assert ((Get-ChildItem $selection -File -Recurse).Count -eq 10) 'Preview leaves audio and non-audio fixtures intact'
$archives = @(Get-OptionalAudioArchives $scratch)
Assert ($archives.Count -eq 1 -and $archives[0].FullName -eq $archive) 'Only audio-bearing known package archives selected'
$evidence = Join-Path $scratch 'retained-evidence'
Save-ArchiveTextEvidence $archive $evidence
Assert (@(Get-ChildItem $evidence -Filter '*.json').Count -eq 3) 'Archived metrics and member index preserved'
Assert (@(Get-ChildItem $evidence -Filter '*.wav').Count -eq 0) 'Archive evidence contains no audio'
} finally {
    $null = Assert-StoragePath $scratch (Join-Path $repo 'build')
    Write-Output "Remove-Item -LiteralPath '$scratch' -Recurse -Force"
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
