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
    history = math.ceil(sample_rate * 16) + 8
    for bpm in (20, 120, 400):
        for beats in LOOP_BEATS:
            source = min(history - 8, max(32, int(sample_rate * 60 / bpm * beats)))
            for start, end in ((0, 1), (.25, .75), (.85, .92)):
                size = max(16, int(source * (end - start)))
                assert 16 <= size <= history - 8
print("StopDial tempo, curve, marker-boundary math checks PASSED")

# The zero-latency end-fade and loop-enter fade are bounded, smooth and finite.
for rate in (44100, 48000, 96000, 192000):
    fade = max(1, int(rate * 0.006))
    enter = [math.sin(min(1, i/fade) * math.pi/2) for i in range(fade + 1)]
    exit_ = list(reversed(enter))
    assert abs(enter[0]) < 1e-9 and abs(enter[-1] - 1) < 1e-9
    assert all(0 <= x <= 1.000001 for x in enter + exit_)
    assert all(b >= a for a,b in zip(enter, enter[1:]))
    assert all(b <= a for a,b in zip(exit_, exit_[1:]))
print("StopDial transition-envelope math checks PASSED")

# Minimum-16-sample slices near the live head must never read future samples.
for total in (32, 64, 128, 600, 800):
    for a, b in ((.95, 1.), (.9, 1.), (0, .05), (.5, .55), (.25, .75)):
        length = min(total, max(16, int(total * (b-a))))
        first = min(total-length, max(0, int(total*a)))
        assert 0 <= first and first + length <= total
print("StopDial minimum-slice guard checks PASSED")
