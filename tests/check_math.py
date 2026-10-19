"""Dependency-free mathematical regression checks for StopDial's timing/curve rules.
Run: python tests/check_math.py (not a JUCE/audio-thread integration test).
"""
import math

BEATS = (4, 2, 1, 0.5, 0.25, 6, 3, 1.5, 0.75, 0.375)
LOOP_BEATS = (1, 0.5, 0.75, 0.25, 0.375, 0.125, 0.1875, 2, 3)

def speed(t, curve):
    return 1 - t ** (2 ** (curve / 50.0))

for curve in (-100, -54, 0, 54, 100):
    samples = [speed(i / 1000.0, curve) for i in range(1001)]
    assert abs(samples[0] - 1) < 1e-10
    assert abs(samples[-1]) < 1e-10
    assert all(-1e-6 <= x <= 1 + 1e-6 for x in samples)
    assert all(a >= b for a, b in zip(samples, samples[1:])), curve
for bpm in (20, 60, 90, 120, 240, 400):
    for idx, beats in enumerate(BEATS):
        samples = round(48000 * (60 / bpm) * beats)
        assert samples > 0
    assert math.isclose(48000 * 60 / bpm * BEATS[2], 48000 * 60 / bpm)
assert LOOP_BEATS[2] == .75
# Loop window must stay bounded by history even at the lowest supported tempo.
for sample_rate in (44100, 48000, 96000):
    history = math.ceil(sample_rate * 8) + 8
    for bpm in (20, 120, 400):
        for beats in LOOP_BEATS:
            source = min(history - 8, max(32, int(sample_rate * 60 / bpm * beats)))
            for start, end in ((0, 1), (.25, .75), (.85, .92)):
                size = max(16, int(source * (end - start)))
                assert 16 <= size <= history - 8
print("StopDial tempo, curve, marker-boundary math checks PASSED")
