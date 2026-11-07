#pragma once
// Dependency-free realtime DSP helpers. No allocations, exceptions or JUCE API.
#include <algorithm>
#include <cmath>

namespace StopDialMath
{
    inline double stopSpeed(double phase, double curve) noexcept
    {
        const double p = std::clamp(phase, 0.0, 1.0);
        const double shape = std::pow(2.0, std::clamp(curve, -100.0, 100.0) / 50.0);
        return 1.0 - std::pow(p, shape);
    }

    // Fade with exact 0/1 endpoints and unity on a DC loop seam.
    inline float smoothFade(float amount) noexcept
    {
        const float t = std::clamp(amount, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float splice(float end, float beginning, float position) noexcept
    {
        const float fade = smoothFade(position);
        return end + (beginning - end) * fade;
    }

    inline float hermite(float prev, float a, float b, float next, float frac) noexcept
    {
        const float m0 = 0.5f * (b - prev);
        const float m1 = 0.5f * (next - a);
        const float a2 = 3.0f * (b - a) - 2.0f * m0 - m1;
        const float a3 = 2.0f * (a - b) + m0 + m1;
        return ((a3 * frac + a2) * frac + m0) * frac + a;
    }

    // The next history write is at writeIndex. A four-sample causal reader
    // avoids the unrecorded future taps of cubic interpolation.
    inline int causalReaderIndex(int writeIndex, int historySize) noexcept
    {
        if (historySize < 8) return 0;
        int index = (writeIndex - 4) % historySize;
        if (index < 0) index += historySize;
        return index;
    }

    // Constrain cubic ringing on sharp transients to extrema of existing
    // samples while retaining smooth cubic interpolation on normal material.
    inline float boundedHermite(float prev, float a, float b, float next, float frac) noexcept
    {
        const float candidate = hermite(prev, a, b, next, frac);
        const float minValue = std::min(std::min(prev, a), std::min(b, next));
        const float maxValue = std::max(std::max(prev, a), std::max(b, next));
        return std::clamp(candidate, minValue, maxValue);
    }

    // Map a signed sample offset to one of the captured loop's samples.
    // Both the preroll and trailing Hermite guard frames use this mapping;
    // no interpolation sample can leak in from outside the selected region.
    inline int wrapLoopIndex(int offset, int loopSize) noexcept
    {
        if (loopSize <= 0) return 0;
        const int remainder = offset % loopSize;
        return remainder < 0 ? remainder + loopSize : remainder;
    }

    inline int loopOverlapSamples(int loopSize, double sampleRate) noexcept
    {
        const int preferred = std::max(2, static_cast<int>(sampleRate * 0.006));
        return std::min(preferred, std::max(2, loopSize / 4));
    }

    inline int loopPeriodSamples(int loopSize, double sampleRate) noexcept
    {
        return std::max(1, loopSize - loopOverlapSamples(loopSize, sampleRate));
    }

    // A short continuity handoff for loop entry. Input is 0..1.
    // This same smoothstep is used for retrigger transitions.
    inline float transitionGain(float position) noexcept
    {
        return smoothFade(position);
    }

    inline float rhythmGate(int pattern, double phase, double period, double fadeSamples) noexcept
    {
        if ((pattern & 15) == 15) return 1.0f; // Fully-on must not pump at 4 subdivision boundaries.
        if ((pattern & 15) == 0 || period <= 0.0) return 0.0f;
        if (phase >= period || phase < 0.0)
        {
            phase = std::fmod(phase, period);
            if (phase < 0.0) phase += period;
        }
        const double stepLength = period * 0.25;
        const int step = std::clamp(static_cast<int>(phase / stepLength), 0, 3);
        if ((pattern & (1 << step)) == 0) return 0.0f;
        const bool previousOn = (pattern & (1 << ((step + 3) & 3))) != 0;
        const bool nextOn = (pattern & (1 << ((step + 1) & 3))) != 0;
        const double within = phase - stepLength * step;
        const float ramp = static_cast<float>(std::max(1.0, std::min(fadeSamples, stepLength * 0.45)));
        float gain = 1.0f;
        if (!previousOn) gain *= smoothFade(static_cast<float>(within) / ramp);
        if (!nextOn) gain *= smoothFade(static_cast<float>(stepLength - within) / ramp);
        return gain;
    }

    // A long tempo-ramped stop may otherwise read audio that the live writer
    // has already overwritten. Reserve the entire release fade plus cubic
    // guard samples; fail safely to dry input instead of playing corrupted data.
    inline bool historyOverwriteImminent(double lag, int historySamples,
                                          int releaseSamples) noexcept
    {
        if (!std::isfinite(lag)) return true;
        return lag >= static_cast<double>(std::max(0, historySamples
                                                       - std::max(1, releaseSamples) - 32));
    }

    // Coefficients are expensive transcendental functions. Recompute only
    // every 16 samples and smoothly approach the target at audio rate. One
    // instance drives both channels so the stereo image remains coherent.
    struct ModernCutoffSmoother
    {
        static constexpr int updateInterval = 16;
        double sampleRate = 48000.0;
        float coefficient = 0.0f, target = 0.0f, smoothing = 0.0f;
        int framesUntilUpdate = 0;

        void prepare(double rate) noexcept
        {
            sampleRate = std::clamp(rate, 8000.0, 384000.0);
            const float maximum = static_cast<float>(std::min(18000.0, 0.45 * sampleRate));
            coefficient = target = 1.0f - std::exp(-6.283185307179586f
                             * maximum / static_cast<float>(sampleRate));
            // An 8 ms attack/release prevents filter zipper noise without
            // putting pow/exp in every per-sample DSP path.
            smoothing = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.008));
            framesUntilUpdate = 0;
        }

        float next(float speed) noexcept
        {
            if (framesUntilUpdate-- <= 0)
            {
                framesUntilUpdate = updateInterval - 1;
                const float maxCutoff = static_cast<float>(std::min(18000.0, 0.45 * sampleRate));
                const float safeSpeed = std::isfinite(speed) ? std::clamp(speed, 0.0f, 1.0f) : 1.0f;
                const float cutoff = 180.0f + (maxCutoff - 180.0f)
                    * std::pow(safeSpeed, 1.45f);
                target = 1.0f - std::exp(-6.283185307179586f
                              * cutoff / static_cast<float>(sampleRate));
            }
            coefficient += smoothing * (target - coefficient);
            return std::clamp(coefficient, 0.0f, 1.0f);
        }
    };

    // Envelopes for newly-enabled rhythm steps should never jump at an
    // arbitrary automation sample. A short one-pole slew smooths the gate
    // without introducing latency or allocating on the audio thread.
    struct GateSlew
    {
        float level = 1.0f, coefficient = 1.0f;
        void prepare(double sampleRate) noexcept
        {
            coefficient = 1.0f - std::exp(-1.0f / static_cast<float>(
                std::max(1.0, sampleRate) * 0.00065));
            level = 1.0f;
        }
        void prime(float value) noexcept { level = std::clamp(value, 0.0f, 1.0f); }
        float next(float target) noexcept
        {
            const float saneTarget = std::isfinite(target) ? std::clamp(target, 0.0f, 1.0f) : 0.0f;
            level += coefficient * (saneTarget - level);
            return std::clamp(level, 0.0f, 1.0f);
        }
    };

    // Shared by the real JUCE engine and dependency-free native tests.
    // Output is completely unchanged by saturation at full tape speed; tone
    // and drive increase together as the brake approaches zero.
    struct ModernTone
    {
        float lp1 = 0.0f, lp2 = 0.0f, dcInput = 0.0f, dcOutput = 0.0f;

        void prime(float sample) noexcept
        {
            lp1 = lp2 = dcInput = std::isfinite(sample) ? sample : 0.0f;
            dcOutput = 0.0f;
        }

        float process(float wet, float speed, float coefficient, float dcPole) noexcept
        {
            coefficient = std::clamp(coefficient, 0.0f, 1.0f);
            speed = std::clamp(speed, 0.0f, 1.0f);
            lp1 += coefficient * (wet - lp1);
            lp2 += coefficient * (lp1 - lp2);
            const float tone = 1.0f - speed;
            wet += tone * (lp2 - wet);
            // At full speed, avoid an unnecessary tanh (per channel per
            // sample), yet keep DC history warm for a seamless slowdown.
            if (tone <= 1.0e-5f)
            {
                dcOutput = wet - dcInput + dcPole * dcOutput;
                dcInput = wet;
                return wet;
            }
            const float drive = 1.0f + 0.8f * tone;
            const float saturated = std::tanh(wet * drive) / drive;
            wet += tone * 0.6f * (saturated - wet);
            const float hp = wet - dcInput + dcPole * dcOutput;
            dcInput = wet;
            dcOutput = hp;
            // Introduce the subsonic blocker gradually as speed decays.
            return wet + tone * (hp - wet);
        }
    };

    inline double beatSeconds(double bpm) noexcept
    {
        if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0) bpm = 120.0;
        return 60.0 / bpm;
    }
}
