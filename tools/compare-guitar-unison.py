"""Dry Guitar A/B evidence; uses only Python's standard library and built renderers.

Raw files remain untouched. Matched Guitar copies use one whole-file RMS gain;
matched Both copies combine that Guitar with the unchanged Bass at unity gain.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def vlq(value):
    result = [value & 127]
    while value >> 7:
        value >>= 7
        result.insert(0, (value & 127) | 128)
    return bytes(result)


def midi(sequence, path):
    events = []
    for start, length, channel, note, velocity in sequence['notes']:
        events += [(round(start * 960), 1, bytes([0x90 + channel - 1, note, velocity])),
                   (round((start + length) * 960), 0, bytes([0x80 + channel - 1, note, 0]))]
    for beat, channel, wheel in sequence.get('bends', []):
        events.append((round(beat * 960), 2, bytes([0xe0 + channel - 1, wheel & 127, wheel >> 7])))
    for beat, channel, cc, value in sequence.get('controllers', []):
        events.append((round(beat * 960), 3, bytes([0xb0 + channel - 1, cc, value])))
    track, at = bytearray(b'\0\xff\x51\x03\x07\xa1\x20'), 0
    # Preserve fixture note order at simultaneous timestamps: summation/voice
    # allocation order is part of the owner's existing raised-chord reference.
    for tick, _, message in sorted(events, key=lambda event: event[:2]):
        track += vlq(tick - at) + message
        at = tick
    track += vlq(round(sequence['length_beats'] * 960) - at) + b'\xff\x2f\0'
    path.write_bytes(b'MThd' + struct.pack('>IHHH', 6, 0, 1, 960)
                     + b'MTrk' + struct.pack('>I', len(track)) + track)


def read_audio(path):
    raw = path.read_bytes()
    assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE'
    offset, fmt, samples = 12, None, None
    while offset + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, offset)
        data = raw[offset + 8:offset + 8 + size]
        if tag == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', data)
            assert fmt[0] == 3 or (fmt[0] == 65534 and data[24:28] == b'\x03\0\0\0')
            assert fmt[1] == 2 and fmt[2] == 48000 and fmt[5] == 32
        if tag == b'data':
            samples = array.array('f')
            samples.frombytes(data)
            if sys.byteorder != 'little':
                samples.byteswap()
        offset += 8 + size + (size & 1)
    assert fmt and samples is not None
    return samples


def write_audio(path, samples):
    samples = array.array('f', samples)
    if sys.byteorder != 'little':
        samples.byteswap()
    payload = samples.tobytes()
    path.write_bytes(b'RIFF' + struct.pack('<I', 36 + len(payload)) + b'WAVEfmt '
                     + struct.pack('<IHHIIHH', 16, 3, 2, 48000, 384000, 8, 32)
                     + b'data' + struct.pack('<I', len(payload)) + payload)


def metrics(samples):
    assert all(math.isfinite(x) and abs(x) < 1 for x in samples)
    assert all(x == 0 for x in samples[-4800:])
    ll = sum(x*x for x in samples[::2])
    rr = sum(x*x for x in samples[1::2])
    lr = sum(l*r for l, r in zip(samples[::2], samples[1::2]))
    windows = []
    for at in range(0, len(samples), 24000):  # 250 ms at 48 kHz, stereo.
        window = samples[at:at+24000]
        a = sum(x*x for x in window[::2])
        b = sum(x*x for x in window[1::2])
        if min(a, b) > 1e-8:
            windows.append(sum(l*r for l, r in zip(window[::2], window[1::2])) / math.sqrt(a*b))
    return dict(peak=max(map(abs, samples)), rms=math.sqrt((ll+rr)/len(samples)),
                correlation=lr/math.sqrt(ll*rr),
                correlation_250ms_min=min(windows), correlation_250ms_max=max(windows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline_renderer', type=Path)
    parser.add_argument('candidate_renderer', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--raised-midi', type=Path,
                        help='Reuse the exact established raised-chord MIDI, including simultaneous-event order.')
    parser.add_argument('--guitar-balance-db', type=float, default=0.0,
                        help='Additional Guitar-only gain in separately labeled listening mixes (0..12 dB).')
    args = parser.parse_args()
    if not math.isfinite(args.guitar_balance_db) or not 0 <= args.guitar_balance_db <= 12:
        parser.error('guitar-balance-db must be finite and within 0..12')
    args.output.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parent.parent
    fixture = repo / 'docs/test-sequences/fixtures.json'
    spec = json.loads(fixture.read_text(encoding='utf-8-sig'))
    assert spec['ticks_per_quarter'] == 960 and spec['tempo_bpm'] == 120
    sequences = spec['sequences']
    raised = json.loads(json.dumps(next(s for s in sequences if s['id'] == 'power-chords')))
    raised['id'] = 'raised-chords'
    for note in raised['notes']:
        note[3] += 12
    sequences += [raised, dict(id='held-69', length_beats=9, notes=[[0, 8, 1, 69, 127]])]
    report = dict(fixture_sha256=digest(fixture), guitar_balance_db=args.guitar_balance_db,
                  renderers={}, sequences={})
    variants = {'guitar': ['--layer-mask', 'guitar'], 'bass': ['--layer-mask', 'bass'],
                'both': [], 'full-guitar': ['--layer-mask', 'guitar', '--destruction', '1']}
    for label, renderer in [('A', args.baseline_renderer), ('B', args.candidate_renderer)]:
        renderer = renderer.resolve()
        report['renderers'][label] = dict(path=str(renderer), sha256=digest(renderer))
        for sequence in sequences:
            name = sequence['id']
            folder = args.output / name
            folder.mkdir(exist_ok=True)
            midi_path = folder / 'input.mid'
            if not midi_path.exists():
                if name == 'raised-chords' and args.raised_midi:
                    midi_path.write_bytes(args.raised_midi.read_bytes())
                else:
                    midi(sequence, midi_path)
            entry = report['sequences'].setdefault(name, dict(midi_sha256=digest(midi_path)))
            entry[label] = {}
            for variant, options in variants.items():
                stem = folder / f'{label}-{variant}'
                for block in (512, 73):
                    wav = Path(f'{stem}-{block}.wav')
                    metadata = wav.with_suffix('.json')
                    subprocess.run([str(renderer), str(midi_path), str(wav), str(metadata),
                                    '--block-size', str(block), *options], check=True, capture_output=True)
                assert digest(Path(f'{stem}-512.wav')) == digest(Path(f'{stem}-73.wav'))
                audio = read_audio(Path(f'{stem}-512.wav'))
                entry[label][variant] = dict(sha256=digest(Path(f'{stem}-512.wav')), **metrics(audio),
                    renderer_metrics=json.loads(Path(f'{stem}-512.json').read_text()))
    for name, entry in report['sequences'].items():
        folder = args.output / name
        assert entry['A']['bass']['sha256'] == entry['B']['bass']['sha256']
        for label in ('A', 'B'):
            guitar = read_audio(folder / f'{label}-guitar-512.wav')
            bass = read_audio(folder / f'{label}-bass-512.wav')
            both = read_audio(folder / f'{label}-both-512.wav')
            summed = array.array('f', (g+b for g, b in zip(guitar, bass)))
            assert summed == both
            assert bass[::2] == bass[1::2]
        entry['matched'] = {}
        for variant in ('guitar', 'full-guitar'):
            gain = entry['A'][variant]['rms'] / entry['B'][variant]['rms']
            for label in ('A', 'B'):
                samples = read_audio(folder / f'{label}-{variant}-512.wav')
                matched = array.array('f', (x*(gain if label == 'B' else 1) for x in samples))
                path = folder / f'{label}-{variant}-matched.wav'
                write_audio(path, matched)
                entry['matched'][f'{label}-{variant}'] = dict(gain_db=20*math.log10(gain) if label == 'B' else 0,
                                                              sha256=digest(path), **metrics(matched))
                if variant == 'guitar':
                    bass = read_audio(folder / f'{label}-bass-512.wav')
                    path = folder / f'{label}-both-guitar-matched.wav'
                    combined = array.array('f', (g+b for g, b in zip(matched, bass)))
                    write_audio(path, combined)
                    entry['matched'][f'{label}-both'] = dict(sha256=digest(path), **metrics(combined))
                    if args.guitar_balance_db:
                        balance = 10 ** (args.guitar_balance_db / 20)
                        balanced = array.array('f', (balance*g+b for g, b in zip(matched, bass)))
                        path = folder / f'{label}-both-guitar-plus{args.guitar_balance_db:g}db.wav'
                        write_audio(path, balanced)
                        entry['matched'][f'{label}-balanced-both'] = dict(
                            guitar_gain_db=args.guitar_balance_db, bass_gain_db=0,
                            sha256=digest(path), **metrics(balanced))
    (args.output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(args.output / 'comparison.json')


if __name__ == '__main__':
    main()
