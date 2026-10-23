# Standard regression MIDI fixtures

These are automated development tests, not plugin patterns or required manual tests. M2.4 includes the test files/results in developer packages for reproducibility; they never play automatically inside the instrument. The owner uses their own MIDI in REAPER; fixture audition is optional.

`fixtures.json` is the readable source of truth. `standard_sequences` CTest converts it to deterministic type 0 MIDI and calls the actual `render_tool` executable. MIDI/metrics stay in ignored `<build>/standard-sequences-*` directories; WAVs are temporary and removed per [storage](../storage.md). No samples, external music or new dependencies are used.

M3.4 adds `bass_sequences`: the same five fixtures and settings through `--layer-mask bass`, in separate `bass-sequences-*` directories/receipts. Split 140/Destruction 0.5; exact decoded L == R and matching blocks 512/73 required. Packages add `bass-regression/` and reproduce its low-riff with the extracted renderer. [Current bass measurements/listening](../bass-listening.md). Mixed WAVs remain unchanged from M3.3b.

## Listening chord register

Owner feedback on 2026-10-21: the chord example was too low. Future owner-facing chord auditions use every `power-chords` note **+12 semitones** (20 note-ons, MIDI **35-79**), preserving tempo, timing, note lengths, channels and velocities. Guitar follows the raised MIDI; Bass remains one octave below input. Existing automated low/boundary fixtures and historical listening evidence stay intact.

Run `pwsh -NoProfile -File tools/render-listening-chords.ps1` from the repository root. It creates a unique ignored build/listening-chords-* directory with the raised MIDI, Both/Guitar/full-drive Guitar/Mono WAVs, metrics and hashes. Output is unnormalized, 48 kHz/Output 0; all four variants must match bytes across blocks 512/73 and remain finite/unclipped. No DSP or preset change.

Bass-register A/B (2026-10-21): `pwsh -NoProfile -File tools/compare-listening-bass-octave.ps1 -AuditionDirectory <raised-chord-directory>` reuses the exact Guitar stem and resynthesizes only Bass one octave higher. It masks Bass while retaining Both routing; a +12 input cancels the current -12 bass offset. No audio pitch shifting, normalization or gain change. Original stems must reconstruct Both sample-exactly; Bass must remain centered, finite/unclipped, repeatable at blocks 512/73, with a silent tail. Owner preferred the original lower Bass after this A/B: retain input minus 12; Guitar and raised listening MIDI stay unchanged. Text evidence records the rejected experiment; it does not change production pitch policy.

## Fixed settings and coverage

All fixtures: 120 BPM, 960 ticks/quarter, MIDI channel 1, velocity 127, 48 kHz stereo float32, Output 0 dB, 512-sample blocks, 100 ms rendered tail. Each is rendered again with 73-sample blocks; the WAV files must match byte for byte. Musical durations exclude the tail.

| ID | Notes / duration | Purpose |
|---|---|---|
| `low-riff` | 13 notes, MIDI 22-31; 8 beats / 4 s | Short gates, repeated low roots, rests and pitch changes |
| `power-chords` | 20 notes; 10 beats / 5 s | Four explicit root/fifth/octave chords, then one eight-note chord at full velocity; no automatic chord stacking |
| `chromatic` | 128 notes, MIDI 0-127; 32.5 beats / 16.25 s | Every input pitch, one note per quarter beat; 3/16-beat gates |
| `boundary-notes` | MIDI 0, 1, 11, 12, 126, 127; 12 beats / 6 s | Longer holds at low/octave/high boundaries; future octave-down bass must retain negative transposed MIDI pitches |
| `held-sweep` | MIDI 45 for 10 beats; 12 beats / 6 s | Sustained weight, center/upper/lower/intermediate pitch-wheel positions, return to center, release while CC64 is down |

Note rows are `[startBeat, durationBeats, channel, midiNote, velocity]`; bends are `[beat, channel, wheel0to16383]`; controller rows are `[beat, channel, ccNumber, value]`. `length_beats` includes final rests. Events are ordered by beat in generated MIDI; note duration determines explicit note-off timing. All beats are exactly representable at 960 ticks/quarter.

The held fixture currently sweeps pitch bend only. Destruction/Color/Tight automation is not implemented; its long gate can host those sweeps when those controls exist. These checks do not establish future bass/guitar routing, separation, stereo width, distortion aliasing or final sound quality.

## Run and evaluate

Run the normal build/CTest, or rerun only the suite:

```powershell
build/tools/cmake-4.4.4-windows-x86_64/bin/ctest.exe --test-dir build/windows-release -R standard_sequences -V
```

To generate/render without the rest of CTest (use a new output directory for each run):

```powershell
build/windows-release/tests/ironstack_render_tests.exe C:/Code/ironstack/build/windows-release/tools/render_tool/render_tool.exe C:/Code/ironstack/build/windows-release C:/Code/ironstack/docs/test-sequences/fixtures.json
```

On another checkout, substitute its absolute executable/build/fixture paths. CMake/CTest chooses the correct configuration paths automatically in CI.

Every fixture must render successfully, have the documented frame count and some signal in every scheduled note interval, remain finite/unclipped at defaults, end in at least 50 ms of exact silence and reproduce identical WAV bytes across blocks 512/73. Held-note windows must retain nonzero RMS. The suite retains all 168 note-ons, full velocity and MIDI boundaries 0/127. Summary JSON records per-fixture renderer metrics and configured source provenance; compare like settings/builds as described in [renderer.md](../renderer.md). These are initial prototype checks, not loudness or subjective acceptance scores.

## Verified baseline

Clean Windows Release build/CTest **5/5 passed** on 2026-10-19 at source `7364072a90139271a142e13ff9da2919abeb3817`. All five sequences rendered successfully, with zero clipping/non-finite samples, exact final silence and identical WAV bytes at blocks 512/73. Held-note sustain and release while CC64 remained down passed. This chunk changes only tests/fixtures/docs; plugin/DSP/renderer sources match the preceding commit. [Exact-commit CI](https://github.com/hugo-cacciatori/ironstack/actions/runs/39265609859) passed build/CTest 5/5/package on Windows x64, Linux x64 and macOS arm64, plus Windows pluginval strictness 5 including GUI checks. This does not establish new Unix DAW support.

| Fixture | Stereo frames including tail | Peak | RMS |
|---|---:|---:|---:|
| low-riff | 196800 | 0.0799009 | 0.0313074 |
| power-chords | 244800 | 0.6297141 | 0.1051580 |
| chromatic | 784800 | 0.0799538 | 0.0375793 |
| boundary-notes | 292800 | 0.0799724 | 0.0356862 |
| held-sweep | 292800 | 0.0796737 | 0.0416814 |

These measurements describe the oscillator prototype, not future layer sound or musical quality. Raw evidence: build/m2.2-7364072/{summary.json,source.json,hashes.json,CTest.log,ci-result.json,ci-tests.txt}; source.json identifies the generated MIDI/WAV directory, hashes.json records each MIDI/WAV checksum. Renderer metrics include the exact configured commit and clean state.
