// Optional repeatable no-JUCE microbenchmark; NOT a real DAW CPU test.
#include "../Source/dsp/StopDialMath.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace
{
    constexpr int frames = 3000000;
    constexpr float sr = 48000.0f, tau = 6.283185307179586f;
    inline float speedAt(int i) noexcept
    {
        // Ramp with hard turns to model braking/retrigger automation.
        const float position = (i % 48000) / 48000.0f;
        return i % 96000 < 48000 ? 1.0f - position : position;
    }

    template <typename Operation>
    double run(Operation op, float& checksum)
    {
        volatile float accumulator = 0.0f;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
            accumulator = accumulator + op(speedAt(i));
        const auto finish = std::chrono::steady_clock::now();
        checksum = accumulator;
        return std::chrono::duration<double, std::milli>(finish - start).count();
    }
}

int main()
{
    float oldSum = 0, newSum = 0;
    // The v2.1 baseline re-evaluates both transcendental functions every frame.
    const auto oldTime = run([](float speed) {
        const float cutoff = 180.0f + (18000.0f - 180.0f) * std::pow(speed, 1.45f);
        return 1.0f - std::exp(-tau * cutoff / sr);
    }, oldSum);
    StopDialMath::ModernCutoffSmoother cutoff;
    cutoff.prepare(sr);
    const auto newTime = run([&cutoff](float speed) { return cutoff.next(speed); }, newSum);
    std::cout << "Modern filter-coefficient-only microbenchmark at 48 kHz, " << frames
              << " frames\nprevious: " << oldTime << " ms; v2.2: " << newTime
              << " ms; ratio: " << oldTime / newTime << "x\n"
              << "checksums: " << oldSum << " / " << newSum << "\n"
              << "Note: excludes filter/saturation, UI, host, and I/O.\n";
}
