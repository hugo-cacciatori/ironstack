"""Compare actual isolated Bass branches with standard-library-only measurements.

Band energies and harmonic projections describe spectral changes, not listening
acceptance. Raw levels remain intact.
"""
import argparse
import array
import cmath
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys


def vlq(value):
    result = [value & 127]
    while value >> 7:
        value >>= 7
        result.insert(0, (value & 127) | 128)
    return bytes(result)


def read_audio(path):
    raw, offset = path.read_bytes(), 12
    assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE'
    while offset + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, offset)
        if tag == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', raw, offset + 8)
            assert fmt[0] in (3, 65534) and fmt[1:3] == (2, 48000) and fmt[5] == 32
        if tag == b'data':
            samples = array.array('f', raw[offset + 8:offset + 8 + size])
            if sys.byteorder != 'little':
                samples.byteswap()
            assert samples[::2] == samples[1::2]
            assert all(math.isfinite(x) and abs(x) < 1 for x in samples)
            return samples[::2]
        offset += 8 + size + (size & 1)
    raise ValueError('Missing float WAV data')


def filtered(samples, cutoff, highpass):
    # Independent bilinear Butterworth analysis filter, not instrument code.
    k = math.tan(math.pi * cutoff / 48000)
    norm = 1 / (1 + math.sqrt(2)*k + k*k)
    b0 = norm if highpass else k*k*norm
    b1, b2 = (-2*b0 if highpass else 2*b0), b0
    a1, a2 = 2*(k*k-1)*norm, (1-math.sqrt(2)*k+k*k)*norm
    z1 = z2 = 0.0
    result = []
    for x in samples:
        y = b0*x + z1
        z1, z2 = b1*x-a1*y+z2, b2*x-a2*y
        result.append(y)
    return result


def describe(samples, hz):
    def band(low, high):
        data = filtered(filtered(samples, low, True), high, False)[48000:]
        return sum(x*x for x in data) / len(data)
    window = [x*(0.5-0.5*math.cos(2*math.pi*i/95999)) for i, x in enumerate(samples[48000:144000])]
    partials = []
    for harmonic in range(2, 61):
        rotation = cmath.exp(-2j*math.pi*hz*harmonic/48000)
        phase, value = 1+0j, 0j
        for x in window:
            value += x*phase
            phase *= rotation
        partials.append((harmonic, abs(value)**2))
    even = sum(p for h, p in partials if h % 2 == 0)
    odd = sum(p for h, p in partials if h % 2)
    return dict(peak=max(map(abs, samples)), rms=math.sqrt(sum(x*x for x in samples)/len(samples)),
                low_mid_vs_presence_db=10*math.log10(band(80, 300)/band(800, 4000)),
                even_vs_odd_projected_db=10*math.log10(even/odd))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for note, hz in ((45, 55.0), (57, 110.0)):
        track = b'\0\xff\x51\x03\x07\xa1\x20\0' + bytes((0x90, note, 127))
        track += vlq(7680) + bytes((0x80, note, 0)) + b'\0\xff\x2f\0'
        midi = args.output / f'held-{hz:g}.mid'
        midi.write_bytes(b'MThd'+struct.pack('>IHHH', 6, 0, 1, 960)+b'MTrk'+struct.pack('>I', len(track))+track)
        clean_hashes = []
        for name, renderer in (('A', args.baseline), ('B', args.candidate)):
            for component, drive in (('clean', 0.5), ('grit', 0.5), ('grit', 1.0)):
                stem = args.output / f'{name}-{hz:g}-{component}-{drive:g}'
                # Append rather than replace suffix: drive decimals are part of the stem.
                wav, metrics = Path(str(stem)+'.wav'), Path(str(stem)+'.json')
                subprocess.run([str(renderer.resolve()), str(midi.resolve()), str(wav.resolve()), str(metrics.resolve()),
                                '--layer-mask', 'bass', '--bass-component', component, '--destruction', str(drive)],
                               check=True, stdout=subprocess.DEVNULL)
                if component == 'clean':
                    clean_hashes.append(hashlib.sha256(wav.read_bytes()).hexdigest())
                else:
                    results.append(dict(candidate=name, hz=hz, drive=drive, wav=str(wav), **describe(read_audio(wav), hz)))
        assert clean_hashes[0] == clean_hashes[1], 'Clean foundation changed'
    receipt = dict(clean_unchanged=True, measurements=results,
                   baseline_sha256=hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
                   candidate_sha256=hashlib.sha256(args.candidate.read_bytes()).hexdigest())
    (args.output/'comparison.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
