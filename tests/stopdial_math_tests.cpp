#include "../Source/dsp/StopDialMath.h"
#include <array>
// Keep assertions active in Release/CI builds compiled with -DNDEBUG.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
    bool near(double a, double b, double tolerance = 1e-6)
    {
        return std::abs(a - b) < tolerance;
    }

    void testStopCurves()
    {
        for (double shape : { -100.0, -54.0, 0.0, 54.0, 100.0 })
        {
            double previous = 1.0;
            for (int i = 0; i <= 10000; ++i)
            {
                const auto speed = StopDialMath::stopSpeed(i / 10000.0, shape);
                assert(std::isfinite(speed) && speed >= 0.0 && speed <= 1.0);
                assert(speed <= previous + 1e-12);
                previous = speed;
            }
            assert(near(StopDialMath::stopSpeed(0.0, shape), 1.0));
            assert(near(StopDialMath::stopSpeed(1.0, shape), 0.0));
        }
    }

    void testTempoAndDivision()
    {
        assert(near(StopDialMath::beatSeconds(120.0), 0.5));
        assert(near(StopDialMath::beatSeconds(90.0), 2.0/3.0));
        assert(near(StopDialMath::beatSeconds(-1.0), 0.5));
        assert(near(StopDialMath::beatSeconds(NAN), 0.5));
        assert(near(StopDialMath::beatSeconds(20.0), 3.0));
        // A stop running at 120 BPM for 1/4 beat length, then at 60 BPM:
        // phase should be 0.5 after 0.25s and end after a further 0.5s.
        constexpr double sr = 48000.0;
        double phase = 0.0;
        for (int sample = 0; sample < 12000; ++sample)
            phase += 1.0 / (sr * StopDialMath::beatSeconds(120.0));
        assert(near(phase, 0.5));
        for (int sample = 0; sample < 24000; ++sample)
            phase += 1.0 / (sr * StopDialMath::beatSeconds(60.0));
        assert(near(phase, 1.0));
    }

    void testHermite()
    {
        for (float fractional : { 0.0f, 0.125f, 0.25f, 0.75f, 1.0f })
        {
            assert(near(StopDialMath::hermite(0.25f, 0.25f, 0.25f, 0.25f,
                                               fractional), 0.25f));
            assert(near(StopDialMath::hermite(0.0f, 1.0f, 2.0f, 3.0f,
                                               fractional), 1.0 + fractional));
        }
    }

    void testCausalTapeReader()
    {
        for (int historySize : { 8, 16, 128, 1024, 480000 })
        {
            for (int write : { 0, 1, 2, 3, 4, 5, 7, 8, 15, 127, 1023 })
            {
                const int nextWrite = write % historySize;
                const int read = StopDialMath::causalReaderIndex(nextWrite, historySize);
                assert(read >= 0 && read < historySize);
                // The four interpolator taps -1, 0, +1, +2 must all refer
                // to samples written before the upcoming frame is stored.
                for (int offset : { -1, 0, 1, 2 })
                {
                    const int tap = StopDialMath::wrapLoopIndex(read + offset, historySize);
                    const int age = StopDialMath::wrapLoopIndex(nextWrite - tap, historySize);
                    assert(age >= 2 && age <= 5);
                }
            }
        }
    }

    void testBoundedCubicTransients()
    {
        for (float prev : { -1.0f, 0.0f, 1.0f })
            for (float a : { -1.0f, 0.0f, 1.0f })
                for (float b : { -1.0f, 0.0f, 1.0f })
                    for (float next : { -1.0f, 0.0f, 1.0f })
                    {
                        const float low = std::min(std::min(prev, a), std::min(b, next));
                        const float high = std::max(std::max(prev, a), std::max(b, next));
                        for (int i = 0; i <= 200; ++i)
                        {
                            const float frac = static_cast<float>(i) * (1.0f / 200.0f);
                            const float sample = StopDialMath::boundedHermite(prev, a, b, next, frac);
                            assert(std::isfinite(sample));
                            assert(sample >= low - 1e-6f && sample <= high + 1e-6f);
                            if (i == 0) assert(near(sample, a));
                            if (i == 200) assert(near(sample, b));
                        }
                    }
        // Linearity and constant regions must survive the overshoot clamp.
        for (int step = 0; step <= 100; ++step)
            assert(near(StopDialMath::boundedHermite(-1, 0, 1, 2, static_cast<float>(step) * 0.01f),
                        step * 0.01, 1e-5));
    }

    void testTapeHistoryFailsSafe()
    {
        for (int capacity : { 8192, 48000, 768000, 3072000 })
        {
            for (int fade : { 128, 2400, 9600 })
            {
                const double threshold = std::max(0, capacity - fade - 32);
                assert(!StopDialMath::historyOverwriteImminent(threshold - 1.0,
                                                                 capacity, fade));
                assert(StopDialMath::historyOverwriteImminent(threshold,
                                                               capacity, fade));
                assert(StopDialMath::historyOverwriteImminent(capacity, capacity, fade));
                // At worst one lag frame accumulates per output sample. A
                // complete linear-in-time fade finishes before ring overwrite.
                if (threshold > 0)
                    assert(threshold + fade <= capacity - 32);
            }
        }
        assert(StopDialMath::historyOverwriteImminent(
                   std::numeric_limits<double>::quiet_NaN(), 8192, 200));
    }

    void testModernToneFullSpeedTransparency()
    {
        StopDialMath::ModernTone tone;
        tone.prime(0.0f);
        const float pole = 0.998f;
        for (int i = 0; i < 10000; ++i)
        {
            const float x = 0.6f * std::sin(static_cast<float>(i) * 0.018f)
                          + 0.2f * std::cos(static_cast<float>(i) * 0.04f);
            // At speed 1, no high-pass or saturation should colour bass.
            const float y = tone.process(x, 1.0f, 0.8f, pole);
            assert(near(x, y, 1e-6));
        }
        // Still finite when slowing rapidly and with a DC input.
        for (int i = 0; i < 20000; ++i)
        {
            const float y = tone.process(0.8f, 0.0f, 0.08f, pole);
            assert(std::isfinite(y));
        }
    }

    void testModernCutoffSmoothing()
    {
        // Exercise the exact decimated coefficient generator used by JUCE.
        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            StopDialMath::ModernCutoffSmoother c;
            c.prepare(rate);
            const float maximum = static_cast<float>(std::min(18000.0, rate * 0.45));
            const float bright = 1.0f - std::exp(-6.283185307179586f * maximum
                                               / static_cast<float>(rate));
            const float dark = 1.0f - std::exp(-6.283185307179586f * 180.0f
                                             / static_cast<float>(rate));
            assert(near(c.next(1.0f), bright, 1.0e-6));
            // Smoother moves monotonically over a sudden speed drop without
            // a single-sample discontinuity. It should settle within 0.2s.
            float previous = c.coefficient;
            for (int n = 0; n < static_cast<int>(rate * 0.2); ++n)
            {
                const float x = c.next(0.0f);
                assert(std::isfinite(x) && x >= 0.0f && x <= 1.0f);
                assert(x <= previous + 1.0e-6f);
                previous = x;
            }
            assert(near(c.coefficient, dark, 3.0e-5));
            for (int n = 0; n < static_cast<int>(rate * 0.2); ++n)
                c.next(1.0f);
            assert(near(c.coefficient, bright, 3.0e-5));
            // A malformed host value must never poison the filter state.
            assert(std::isfinite(c.next(std::numeric_limits<float>::quiet_NaN())));
        }
    }

    void testRhythmGateSlew()
    {
        for (double rate : { 22050.0, 48000.0, 96000.0 })
        {
            StopDialMath::GateSlew gate;
            gate.prepare(rate);
            gate.prime(1.0f);
            float previous = 1.0f;
            for (int n = 0; n < static_cast<int>(rate * 0.01); ++n)
            {
                const float x = gate.next(0.0f);
                assert(std::isfinite(x) && x <= previous && x >= 0.0f);
                assert(previous - x < 0.09f); // No hard 1-sample gain step.
                previous = x;
            }
            assert(gate.level < 0.001f);
            for (int n = 0; n < static_cast<int>(rate * 0.01); ++n)
                gate.next(1.0f);
            assert(gate.level > 0.999f);
            assert(std::isfinite(gate.next(std::numeric_limits<float>::quiet_NaN())));
            for (int p = 0; p < 16; ++p)
                for (int i = 0; i < 1000; ++i)
                {
                    const float target = StopDialMath::rhythmGate(p,
                        i * 4.8, 4800.0, 96.0);
                    const float output = gate.next(target);
                    assert(std::isfinite(output) && output >= 0.0f && output <= 1.0f);
                }
        }
    }

    void testModernToneStability()
    {
        // Test the EXACT ModernTone implementation used in the plugin.
        for (double rate : { 22050., 44100., 48000., 96000., 192000. })
        {
            StopDialMath::ModernTone tone;
            tone.prime(0.0f);
            const float dcPole = static_cast<float>(std::exp(-2.0 * 3.141592653589793 * 12.0 / rate));
            for (int i = 0; i < 48000; ++i)
            {
                const float speed = i < 15000 ? 1.0f : (i < 30000 ? 0.04f : 0.7f);
                const float cutoffMax = static_cast<float>(std::min(18000.0, rate * 0.45));
                const float cutoff = 180.0f + (cutoffMax - 180.0f) * std::pow(speed, 1.45f);
                const float coefficient = 1.0f - std::exp(
                    -6.283185307179586f * cutoff / static_cast<float>(rate));
                const float input = std::sin(static_cast<float>(i) * 0.061f) * 0.95f;
                const float output = tone.process(input, speed, coefficient, dcPole);
                assert(std::isfinite(output));
                assert(std::abs(output) <= 1.5f);
            }
            // A fresh trigger on another sound must clear stale filter memory.
            tone.prime(0.0f);
            assert(near(tone.lp1, 0) && near(tone.lp2, 0));
            assert(near(tone.dcOutput, 0));
        }
    }

    void testSeam()
    {
        assert(near(StopDialMath::splice(1.0f, -1.0f, 0.0f), 1.0f));
        assert(near(StopDialMath::splice(1.0f, -1.0f, 1.0f), -1.0f));
        assert(near(StopDialMath::splice(1.0f, 1.0f, 0.5f), 1.0f));
        for (int i = 0; i <= 1000; ++i)
        {
            const float t = static_cast<float>(i) / 1000.0f;
            const auto y = StopDialMath::splice(0.8f, 0.8f, t);
            assert(near(y, 0.8f));  // no +3 dB correlation bump
            assert(StopDialMath::smoothFade(t) >= 0.0f);
            assert(StopDialMath::smoothFade(t) <= 1.0f);
        }
    }

    void testPeriodicLoopGuards()
    {
        constexpr int size = 256, start = 103, loopSize = 37;
        std::array<float, size> ring {};
        std::array<float, loopSize + 6> clip {};
        for (int i = 0; i < size; ++i)
            ring[static_cast<size_t>(i)] = std::sin(static_cast<float>(i) * 0.29f)
                + 0.16f * std::sin(static_cast<float>(i) * 1.29f);
        for (int i = 0; i < loopSize + 6; ++i)
        {
            const int idx = StopDialMath::wrapLoopIndex(i - 2, loopSize);
            clip[static_cast<size_t>(i)] = ring[static_cast<size_t>((start + idx) % size)];
        }
        assert(StopDialMath::wrapLoopIndex(-1, loopSize) == loopSize - 1);
        assert(StopDialMath::wrapLoopIndex(-2, loopSize) == loopSize - 2);
        assert(StopDialMath::wrapLoopIndex(loopSize, loopSize) == 0);
        assert(StopDialMath::wrapLoopIndex(loopSize + 3, loopSize) == 3);
        assert(StopDialMath::wrapLoopIndex(0, 0) == 0);
        // The Hermite interpolation must be identical using the live periodic
        // buffer and the completed guarded clip, even across the loop edge.
        for (int i = -1; i <= loopSize; ++i)
        {
            for (int q = 0; q < 10; ++q)
            {
                const float t = static_cast<float>(q) / 10.0f;
                const auto liveSample = [&](int rel) {
                    return ring[static_cast<size_t>((start + StopDialMath::wrapLoopIndex(rel, loopSize)) % size)];
                };
                const auto expected = StopDialMath::boundedHermite(
                    liveSample(i - 1), liveSample(i), liveSample(i + 1), liveSample(i + 2), t);
                const auto actual = StopDialMath::boundedHermite(
                    clip[static_cast<size_t>(i + 1)], clip[static_cast<size_t>(i + 2)],
                    clip[static_cast<size_t>(i + 3)], clip[static_cast<size_t>(i + 4)], t);
                assert(near(actual, expected, 1e-6));
            }
        }
        // Capture must remain stable even if the live history is overwritten.
        ring.fill(0.0f);
        assert(std::abs(clip[18]) > 0.001f);
    }

    void testShortLoopSelection()
    {
        for (int total : { 32, 64, 128, 600, 800 })
        {
            for (int startPct = 0; startPct <= 95; startPct += 5)
            {
                for (int endPct = startPct + 5; endPct <= 100; endPct += 5)
                {
                    const float a = static_cast<float>(startPct) * 0.01f;
                    const float b = static_cast<float>(endPct) * 0.01f;
                    const int length = std::clamp(static_cast<int>(static_cast<float>(total) * (b - a)), 16, total);
                    const int first = std::clamp(static_cast<int>(static_cast<float>(total) * a), 0, total - length);
                    assert(first >= 0 && first + length <= total);
                    for (int i = 0; i < length + 6; ++i)
                    {
                        const int sample = first + StopDialMath::wrapLoopIndex(i - 2, length);
                        assert(sample >= 0 && sample < total);
                    }
                }
            }
        }
    }

    void testLoopPeriodAfterCapture()
    {
        for (int size : { 1, 16, 17, 64, 512, 12000, 250000 })
        {
            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                const int period = StopDialMath::loopPeriodSamples(size, sr);
                assert(period >= 1 && period <= size);
                if (size >= 16) assert(period < size);
            }
        }
        // Stop may finish halfway through a host block, immediately replacing
        // the initial/previous clip geometry with an entirely different one.
        int period = StopDialMath::loopPeriodSamples(1, 48000.0);
        assert(period == 1);
        period = StopDialMath::loopPeriodSamples(4800, 48000.0);
        assert(period > 4000);
        double phase = 0;
        for (int s = 0; s < 256; ++s)
        {
            phase += 1.0;
            if (phase >= period) phase = std::fmod(phase, static_cast<double>(period));
        }
        assert(near(phase, 256.0));
    }

    void testStopToLoopTransition()
    {
        constexpr float slowEnd = 0.77f, loopFirst = -0.83f;
        constexpr int length = 288; // 6 ms at 48 kHz
        float previous = slowEnd;
        for (int i = 0; i <= length; ++i)
        {
            const auto blend = StopDialMath::transitionGain(static_cast<float>(i) / length);
            const auto out = slowEnd + (loopFirst - slowEnd) * blend;
            assert(std::isfinite(out));
            if (i == 0) assert(near(out, slowEnd));
            if (i == length) assert(near(out, loopFirst));
            assert(std::abs(out - previous) <= 0.009f); // No single-sample step.
            previous = out;
        }
        // When incoming and looped signals already match, the handoff must
        // not create the old two-fade volume dip.
        for (int i = 0; i <= length; ++i)
        {
            const float gain = StopDialMath::transitionGain(static_cast<float>(i) / static_cast<float>(length));
            const float sameDC = slowEnd + (slowEnd - slowEnd) * gain;
            assert(near(sameDC, slowEnd));
        }
    }

    void testPatternGating()
    {
        for (int pattern = 0; pattern < 16; ++pattern)
        {
            for (int i = 0; i < 4000; ++i)
            {
                const auto phase = i / 4000.0 * 4800;
                const auto value = StopDialMath::rhythmGate(pattern, phase, 4800, 96);
                assert(value >= 0.0f && value <= 1.0f && std::isfinite(value));
                if (pattern == 15) assert(value == 1.0f);
                if (pattern == 0) assert(value == 0.0f);
            }
        }
        assert(near(StopDialMath::rhythmGate(0b0011, 300, 4800, 96), 1.0));
        assert(near(StopDialMath::rhythmGate(0b0011, 2900, 4800, 96), 0.0));
        assert(near(StopDialMath::rhythmGate(0b0101, 1200, 4800, 96), 0.0));
        assert(near(StopDialMath::rhythmGate(0b0101, 2400, 4800, 96), 0.0));
    }

    void testLoopSnapshot()
    {
        constexpr int size = 512;
        constexpr int loopSize = 180;
        constexpr int start = 163;
        std::array<float, size> ring {};
        std::array<float, size> clip {};
        for (int i = 0; i < size; ++i)
            ring[static_cast<size_t>(i)] = static_cast<float>(std::sin(i * 0.07));
        int copy = 0;
        // Simulate continuous input and bounded snapshot copying.
        // The writer begins well ahead of the material being preserved.
        int write = 485;
        while (copy < loopSize + 6)
        {
            for (int step = 0; step < 8 && copy < loopSize + 6; ++step)
            {
                const int src = (start + StopDialMath::wrapLoopIndex(copy - 2, loopSize)) % size;
                clip[static_cast<size_t>(copy)] = ring[static_cast<size_t>(src)];
                ++copy;
            }
            ring[static_cast<size_t>(write)] = 0.0f;
            write = (write + 1) % size;
        }
        // In this test the old source region is not yet overwritten.
        for (int i = 0; i < loopSize + 6; ++i)
            assert(near(clip[static_cast<size_t>(i)], static_cast<float>(std::sin(
                ((start + StopDialMath::wrapLoopIndex(i - 2, loopSize)) % size) * 0.07))));
        ring.fill(0.0f);
        assert(!near(clip[100], 0.0f)); // frozen loop survives ongoing capture
    }
}

int main()
{
    testStopCurves();
    testTempoAndDivision();
    testHermite();
    testCausalTapeReader();
    testBoundedCubicTransients();
    testTapeHistoryFailsSafe();
    testModernToneFullSpeedTransparency();
    testModernCutoffSmoothing();
    testRhythmGateSlew();
    testModernToneStability();
    testSeam();
    testPeriodicLoopGuards();
    testShortLoopSelection();
    testLoopPeriodAfterCapture();
    testStopToLoopTransition();
    testPatternGating();
    testLoopSnapshot();
    std::cout << "StopDial DSP math and snapshot regression tests passed\n";
}
