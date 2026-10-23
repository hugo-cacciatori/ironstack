"""Rebalance existing matched Guitar/Bass stems without rerendering or replacing evidence."""
import argparse
import array
import json
import math
from pathlib import Path
import runpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--guitar-db', type=float, default=9.0)
    args = parser.parse_args()
    if not math.isfinite(args.guitar_db) or not 0 <= args.guitar_db <= 12:
        parser.error('guitar-db must be finite and within 0..12')
    helpers = runpy.run_path(str(Path(__file__).with_name('compare-guitar-unison.py')))
    read_audio, write_audio = helpers['read_audio'], helpers['write_audio']
    metrics, digest = helpers['metrics'], helpers['digest']
    source = args.comparison / 'comparison.json'
    previous = json.loads(source.read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(source_comparison_sha256=digest(source), guitar_db=args.guitar_db,
                  bass_db=0.0, master_db=0.0, sequences={})
    gain = 10 ** (args.guitar_db / 20)
    for name in previous['sequences']:
        report['sequences'][name] = {}
        for label in ('A', 'B'):
            folder = args.comparison / name
            guitar_path, bass_path = folder / f'{label}-guitar-matched.wav', folder / f'{label}-bass-512.wav'
            guitar, bass = read_audio(guitar_path), read_audio(bass_path)
            assert len(guitar) == len(bass)
            mixed = array.array('f', (gain*g+b for g, b in zip(guitar, bass)))
            result = metrics(mixed)  # Reject clipping/nonfinite samples or a lost silent tail.
            path = args.output / f'{name}-{label}-guitar-plus{args.guitar_db:g}db.wav'
            write_audio(path, mixed)
            report['sequences'][name][label] = dict(guitar_sha256=digest(guitar_path),
                bass_sha256=digest(bass_path), wav_sha256=digest(path), **result)
    (args.output / 'balance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(args.output / 'balance.json')


if __name__ == '__main__':
    main()
