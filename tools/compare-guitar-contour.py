"""Create one RMS-matched Guitar A/B and a production-level candidate mix.

Uses existing float-WAV/MIDI helpers and Python's standard library. Scratch WAVs
remain available for the project's explicit storage-maintenance command.
"""
import argparse
import array
import json
import math
from pathlib import Path
import runpy
import subprocess

helpers = runpy.run_path(str(Path(__file__).with_name('compare-guitar-unison.py')))
digest, metrics = helpers['digest'], helpers['metrics']
read_audio, write_audio = helpers['read_audio'], helpers['write_audio']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline_renderer', type=Path)
    parser.add_argument('candidate_renderer', type=Path)
    parser.add_argument('midi', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('scratch', type=Path)
    parser.add_argument('--bass-midi', type=Path, required=True)
    parser.add_argument('--bass-sha256', required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    args.scratch.mkdir(parents=True, exist_ok=False)
    report = dict(midi_sha256=digest(args.midi), renders=[],
                  baseline_renderer_sha256=digest(args.baseline_renderer),
                  candidate_renderer_sha256=digest(args.candidate_renderer))

    def render(name, binary, midi, mask, folder):
        wav = folder / (name + '.wav')
        evidence = args.scratch / (name + '.json')
        subprocess.run([str(binary.resolve()), str(midi.resolve()), str(wav.resolve()),
                        str(evidence.resolve()), '--sample-rate', '48000', '--block-size', '512',
                        '--output-db', '0', '--tail-seconds', '0.1', '--layer-mask', mask,
                        '--split-hz', '140', '--destruction', '0.5'], check=True,
                       stdout=subprocess.DEVNULL)
        rendered = json.loads(evidence.read_text(encoding='utf-8-sig'))
        assert rendered['nonfinite_samples'] == rendered['clipped_samples'] == 0
        samples = read_audio(wav)
        measured = metrics(samples)
        report['renders'].append(dict(name=name, mask=mask, wav_sha256=digest(wav),
                                      renderer_metrics=rendered, measured=measured))
        return samples

    a = render('A-baseline-guitar', args.baseline_renderer, args.midi, 'guitar', args.output)
    b = render('B-raw-guitar', args.candidate_renderer, args.midi, 'guitar', args.scratch)
    render('C-candidate-full-mix', args.candidate_renderer, args.midi, 'both', args.output)
    bass = render('bass-proof', args.candidate_renderer, args.bass_midi, 'bass', args.scratch)
    assert bass[::2] == bass[1::2]
    assert digest(args.scratch / 'bass-proof.wav').upper() == args.bass_sha256.upper()
    assert len(a) == len(b)
    gain = metrics(a)['rms'] / metrics(b)['rms']
    matched = array.array('f', (x * gain for x in b))
    matched_metrics = metrics(matched)
    assert abs(20 * math.log10(matched_metrics['rms'] / metrics(a)['rms'])) < 1.0e-6
    output = args.output / 'B-contour-guitar-matched.wav'
    write_audio(output, matched)
    report.update(matched_gain_db=20 * math.log10(gain), matched_metrics=matched_metrics,
                  matched_sha256=digest(output), bass_matches_accepted=True,
                  bass_midi_sha256=digest(args.bass_midi))
    (args.scratch / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"B RMS-match gain: {report['matched_gain_db']:.6f} dB; accepted Bass hash exact")


if __name__ == '__main__':
    main()
