# Offline A/B: resynthesize the bass +12; reuse the exact original guitar stem.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$AuditionDirectory, [string]$Renderer='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$AuditionDirectory=(Resolve-Path -LiteralPath $AuditionDirectory).Path
$receipt=Get-Content -LiteralPath (Join-Path $AuditionDirectory 'auditions.json') -Raw|ConvertFrom-Json
if(-not $Renderer){$Renderer=Join-Path $repo 'build/windows-release/tools/render_tool/render_tool.exe'}
$Renderer=(Resolve-Path -LiteralPath $Renderer).Path
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo "build/bass-octave-$([guid]::NewGuid().ToString('N'))"}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new directory to preserve previous evidence.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$guitar=Join-Path $AuditionDirectory 'chords-guitar.wav'
$original=Join-Path $AuditionDirectory 'chords-both.wav'
$midi=Join-Path $AuditionDirectory 'chords-up-octave.mid'
foreach($name in @('guitar','both')){
    $row=@($receipt.renders|Where-Object name -eq $name)
    if($row.Count -ne 1 -or $row[0].sha256 -ne (Get-FileHash -LiteralPath (Join-Path $AuditionDirectory "chords-$name.wav")).Hash){throw 'Audition hash mismatch.'}
    $m=$row[0].metrics
    if($m.output_db -ne 0 -or $m.sample_rate -ne 48000 -or $m.guitar_mono -or -not $m.guitar_enabled -or -not $m.bass_enabled){throw 'Expected default Both-routing audition.'}
}
if($receipt.midi_sha256 -ne (Get-FileHash -LiteralPath $midi).Hash){throw 'MIDI hash mismatch.'}
# This accepts only the explicit-status type-0 SMF emitted by our chord tool.
[byte[]]$lifted=[IO.File]::ReadAllBytes($midi)
if([Text.Encoding]::ASCII.GetString($lifted,0,4) -ne 'MThd' -or $lifted[9] -ne 0 -or $lifted[11] -ne 1){throw 'Expected type-0 listening MIDI.'}
$at=22; $changed=0
while($at -lt $lifted.Length){
    do{$b=$lifted[$at++]}while($b -band 128)
    $status=$lifted[$at++]
    if($status -eq 255){
        $at++; $length=0
        do{$b=$lifted[$at++];$length=($length -shl 7) -bor ($b -band 127)}while($b -band 128)
        $at+=$length
    }elseif(($status -band 240) -in @(128,144)){
        if($lifted[$at] -gt 115){throw 'Pitch exceeds MIDI range after lifting.'}
        $lifted[$at]+=12; $at+=2; $changed++
    }else{throw 'Unexpected event in listening MIDI.'}
}
if($changed -ne 40){throw 'Expected 20 paired chord notes.'}
$raisedMidi=Join-Path $OutputDirectory 'bass-input-plus12.mid'
[IO.File]::WriteAllBytes($raisedMidi,$lifted)
function Read-FloatWave([string]$Path){
    $bytes=[IO.File]::ReadAllBytes($Path);$position=12;$offset=-1;$size=0;$valid=$false
    if([Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'RIFF' -or [Text.Encoding]::ASCII.GetString($bytes,8,4) -ne 'WAVE'){throw 'Expected RIFF WAV.'}
    while($position+8 -le $bytes.Length){
        $id=[Text.Encoding]::ASCII.GetString($bytes,$position,4);$length=[BitConverter]::ToUInt32($bytes,$position+4);$position+=8
        if($id -eq 'fmt '){$valid=[BitConverter]::ToUInt16($bytes,$position) -eq 3 -and [BitConverter]::ToUInt16($bytes,$position+2) -eq 2 -and [BitConverter]::ToUInt32($bytes,$position+4) -eq 48000 -and [BitConverter]::ToUInt16($bytes,$position+14) -eq 32}
        if($id -eq 'data'){$offset=$position;$size=$length;break}
        $position+=$length+($length%2)
    }
    if(-not $valid -or $offset -lt 0 -or $size -ne 244800*8 -or $offset+$size -gt $bytes.Length){throw 'Expected 48k stereo float32 chord WAV.'}
    $samples=[float[]]::new($size/4);[Buffer]::BlockCopy($bytes,$offset,$samples,0,$size)
    return @{bytes=$bytes;offset=$offset;size=$size;samples=$samples}
}
$g=Read-FloatWave $guitar;$reference=Read-FloatWave $original
$rows=@();$candidate=$null
foreach($variant in @(@{name='original';midi=$midi},@{name='raised';midi=$raisedMidi})){
    $stem=Join-Path $OutputDirectory "bass-$($variant.name).wav"
    foreach($block in @(512,73)){
        $out=if($block -eq 512){$stem}else{[IO.Path]::ChangeExtension($stem,'repeat.wav')}
        $json=[IO.Path]::ChangeExtension($out,'json')
        & $Renderer $variant.midi $out $json --layer-mask bass --block-size $block --output-db 0|Out-Null
        if($LASTEXITCODE -ne 0){throw 'Bass renderer failed.'}
        $m=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
        $source=($receipt.renders|Where-Object name -eq guitar).metrics
        if($m.source_commit -ne $source.source_commit -or $m.source_dirty -ne $source.source_dirty -or $m.clipped_samples -or $m.nonfinite_samples -or $m.left_minus_right_rms -ne 0 -or -not $m.guitar_enabled -or -not $m.bass_enabled){throw 'Bass provenance/routing/safety mismatch.'}
    }
    if((Get-FileHash -LiteralPath $stem).Hash -ne (Get-FileHash -LiteralPath $out).Hash){throw 'Bass differs across blocks.'}
    $bass=Read-FloatWave $stem;$mix=[float[]]::new($g.samples.Length);$peak=0.0;$power=0.0;$last=-1
    for($i=0;$i -lt $mix.Length;$i++){
        if($bass.samples[$i] -ne $bass.samples[$i -bxor 1]){throw 'Bass is not centered.'}
        $mix[$i]=[float]([double]$g.samples[$i]+[double]$bass.samples[$i])
        if(-not [float]::IsFinite($mix[$i]) -or [Math]::Abs($mix[$i]) -ge 1){throw 'Mix is non-finite/clipped.'}
        if($variant.name -eq 'original' -and $mix[$i] -ne $reference.samples[$i]){throw 'Stems do not reconstruct original Both exactly.'}
        $peak=[Math]::Max($peak,[Math]::Abs($mix[$i]));$power+=[double]$mix[$i]*$mix[$i]
        if($mix[$i] -ne 0){$last=[int][Math]::Floor($i/2)}
    }
    if($last -ge 244800-4800){throw 'Final 100 ms is not silent.'}
    if($variant.name -eq 'raised'){$candidate=$mix}
    $rows+=@{name=$variant.name;peak=$peak;rms=[Math]::Sqrt($power/$mix.Length);last_nonzero_frame=$last;stem_sha256=(Get-FileHash -LiteralPath $stem).Hash}
}
$output=Join-Path $OutputDirectory 'both-bass-up-octave.wav'
[Buffer]::BlockCopy($candidate,0,$g.bytes,$g.offset,$g.size)
[IO.File]::WriteAllBytes($output,$g.bytes)
[ordered]@{source_commit=$source.source_commit;source_dirty=$source.source_dirty;guitar_sha256=(Get-FileHash -LiteralPath $guitar).Hash;
    original_mix_sha256=(Get-FileHash -LiteralPath $original).Hash;raised_mix_sha256=(Get-FileHash -LiteralPath $output).Hash;
    bass_relative_semitones=12;guitar_relative_semitones=0;gain_db=0;normalized=$false;original_reconstruction_exact=$true;
    changed_midi_events=$changed;blocks_identical=@(512,73);measurements=$rows}|
    ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'comparison.json') -Encoding utf8NoBOM
Write-Output $output
