"""Optional offline filter design/oracle; never imported by the instrument/build.
Run with NumPy 2.1.3 and SciPy 1.15.1. No files, samples or network access.
"""
import argparse
import importlib.metadata
import json
import numpy as np
from scipy import signal

for package, pin in (("numpy", "2.1.3"), ("scipy", "1.15.1")):
    if importlib.metadata.version(package) != pin:
        raise SystemExit(f"Expected {package} {pin}; do not silently change the oracle")
parser = argparse.ArgumentParser()
parser.add_argument("--header", action="store_true")
args = parser.parse_args()
order, ripple, rejection = 7, 0.1, 60.0
zeros, poles, _ = signal.ellipap(order, ripple, rejection)
references = []
for rate in (44100, 48000, 88200, 96000, 176400, 192000):
    sos = signal.ellip(order, ripple, rejection, 350, btype="highpass", fs=rate, output="sos")
    frequencies = (1, 50, 80, 140, 250, 300, 350, 400, 1000, 1800, 6000, 9000, rate * 0.49)
    _, response = signal.sosfreqz(sos, worN=frequencies, fs=rate)
    references.extend((rate, hz, float(h.real), float(h.imag)) for hz, h in zip(frequencies, response))
if args.header:
    print("#pragma once\n#include <array>\nnamespace dsp_core::test {\n"
          "// Independent SciPy 1.15.1 oracle; tools/design_separation_guard.py --header.\n"
          "struct GuardReference { double rate, hz, real, imag; };\n"
          f"inline constexpr std::array<GuardReference, {len(references)}> guardReferences{{{{")
    for rate, hz, real, imag in references:
        print(f"    {{{rate}, {hz:.17g}, {real:.17g}, {imag:.17g}}},")
    print("}};\n}")
else:
    print(json.dumps({"order": order, "ripple_db": ripple, "stop_db": rejection,
                      "zeros_imag": [float(z.imag) for z in zeros if z.imag > 0],
                      "poles": [[float(p.real), float(p.imag)] for p in poles if p.imag >= 0],
                      "reference": references}, indent=2))
