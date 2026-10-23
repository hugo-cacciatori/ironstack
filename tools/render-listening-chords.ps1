# Owner-facing chord auditions: one octave above the low-register regression.
[CmdletBinding()]
param([string]$Renderer = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Renderer) { $Renderer = Join-Path $repo 'build/windows-release/tools/render_tool/render_tool.exe' }
$Renderer = (Resolve-Path -LiteralPath $Renderer).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo "build/listening-chords-$([guid]::NewGuid().ToString('N'))" }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new directory to preserve prior listening evidence.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$fixturePath = Join-Path $repo 'docs/test-sequences/fixtures.json'
$spec = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json
$chords = @($spec.sequences | Where-Object id -eq 'power-chords')
if ($chords.Count -ne 1 -or $spec.ticks_per_quarter -ne 960 -or $spec.tempo_bpm -ne 120) { throw 'Unexpected chord fixture.' }
$events = @()
$pitches = @()
foreach ($note in $chords[0].notes) {
    $pitch = [int]$note[3] + 12
    if ($pitch -lt 0 -or $pitch -gt 127) { throw 'Octave transposition would exceed MIDI range.' }
    $pitches += $pitch
    $events += @{tick=[int]([double]$note[0]*960); order=1; bytes=[byte[]]@((0x90+([int]$note[2]-1)),$pitch,[int]$note[4])}
    $events += @{tick=[int](([double]$note[0]+[double]$note[1])*960); order=0; bytes=[byte[]]@((0x80+([int]$note[2]-1)),$pitch,0)}
}
# Write a type-0 SMF; note on/off events share the same explicit transposition.
function Write-VariableLength([int]$Value, [IO.Stream]$Stream) {
    [byte[]]$encoded = @($Value -band 127)
    while (($Value = $Value -shr 7) -gt 0) { $encoded = [byte[]]@(($Value -band 127) -bor 128) + $encoded }
    $Stream.Write($encoded, 0, $encoded.Length)
}
$track = [IO.MemoryStream]::new()
try {
    $tempo = [byte[]]@(0,0xff,0x51,3,7,0xa1,0x20) # 500000 microseconds/quarter.
    $track.Write($tempo,0,$tempo.Length)
    $at = 0
    foreach ($event in $events | Sort-Object tick,order) {
        Write-VariableLength ($event.tick-$at) $track
        $track.Write($event.bytes,0,$event.bytes.Length)
        $at = $event.tick
    }
    Write-VariableLength ([int]($chords[0].length_beats*960)-$at) $track
    $track.Write([byte[]]@(0xff,0x2f,0),0,3)
    $payload = $track.ToArray()
} finally { $track.Dispose() }
$header = [byte[]]@(0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,3,0xc0,0x4d,0x54,0x72,0x6b)
$size = [BitConverter]::GetBytes([uint32]$payload.Length)
if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($size) }
$midi = Join-Path $OutputDirectory 'chords-up-octave.mid'
[IO.File]::WriteAllBytes($midi, [byte[]]($header+$size+$payload))
$results = @()
foreach ($variant in @(
    @{name='both'; options=@()}, @{name='guitar'; options=@('--layer-mask','guitar')},
    @{name='bass'; options=@('--layer-mask','bass')},
    @{name='clean-bass'; options=@('--layer-mask','bass','--bass-component','clean')},
    @{name='grit-bass'; options=@('--layer-mask','bass','--bass-component','grit')},
    @{name='full-guitar'; options=@('--layer-mask','guitar','--destruction','1')},
    @{name='mono'; options=@('--guitar-mono','1')}
)) {
    $wav = Join-Path $OutputDirectory "chords-$($variant.name).wav"
    $json = [IO.Path]::ChangeExtension($wav,'json')
    foreach ($block in @(512,73)) {
        $out = if ($block -eq 512) { $wav } else { [IO.Path]::ChangeExtension($wav,'repeat.wav') }
        $metrics = [IO.Path]::ChangeExtension($out,'json')
        & $Renderer $midi $out $metrics --sample-rate 48000 --block-size $block --output-db 0 --tail-seconds 0.1 @($variant.options) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Renderer failed: $($variant.name)/$block" }
        $m = Get-Content -LiteralPath $metrics -Raw | ConvertFrom-Json
        if ($m.nonfinite_samples -ne 0 -or $m.clipped_samples -ne 0 -or $m.peak -le 0 -or $m.peak -ge 1 -or $m.frames -ne 244800 -or $m.midi_events -ne 40) { throw 'Invalid audition metrics.' }
    }
    $hash = (Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash
    if ($hash -ne (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash) { throw 'Audio differs between block sizes 512/73.' }
    $results += [ordered]@{name=$variant.name; wav=$wav; sha256=$hash; metrics=(Get-Content -LiteralPath $json -Raw | ConvertFrom-Json)}
}
function Read-FloatSamples([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    for ($offset = 12; $offset + 8 -le $bytes.Length;) {
        $length = [BitConverter]::ToUInt32($bytes,$offset+4)
        if ([Text.Encoding]::ASCII.GetString($bytes,$offset,4) -eq 'data') {
            $samples = [single[]]::new($length/4)
            [Buffer]::BlockCopy($bytes,$offset+8,$samples,0,$length)
            return ,$samples
        }
        $offset += 8 + $length + ($length % 2)
    }
    throw 'Missing WAV data.'
}
$bassSamples = Read-FloatSamples (Join-Path $OutputDirectory 'chords-bass.wav')
$cleanSamples = Read-FloatSamples (Join-Path $OutputDirectory 'chords-clean-bass.wav')
$gritSamples = Read-FloatSamples (Join-Path $OutputDirectory 'chords-grit-bass.wav')
$reconstructionError = 0.0
for ($i = 0; $i -lt $bassSamples.Length; $i += 2) {
    foreach ($samples in @($bassSamples,$cleanSamples,$gritSamples)) {
        if ($samples[$i] -ne $samples[$i+1]) { throw 'Bass component is not exactly centered.' }
    }
    $reconstructionError = [Math]::Max($reconstructionError,
        [Math]::Abs([double]$bassSamples[$i]-[double]$cleanSamples[$i]-[double]$gritSamples[$i]))
}
if ($reconstructionError -gt 3e-8) { throw 'Bass components do not reconstruct the actual full Bass.' }
[ordered]@{bass_reconstruction_max_error=$reconstructionError;
    transpose_semitones=12; fixture_sha256=(Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash;
    midi=$midi; midi_sha256=(Get-FileHash -LiteralPath $midi -Algorithm SHA256).Hash; note_on_count=$pitches.Count;
    minimum_note=($pitches|Measure-Object -Minimum).Minimum; maximum_note=($pitches|Measure-Object -Maximum).Maximum;
    renders=$results} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'auditions.json') -Encoding utf8NoBOM
Write-Output $OutputDirectory
