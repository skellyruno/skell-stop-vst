// Integration smoke test: exercises the ACTUAL AudioProcessor and vector editor.
// Built only when JUCE is available. All failures remain active in Release builds.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    void check(bool ok, const char* message)
    {
        if (!ok)
        {
            std::fprintf(stderr, "StopDial host regression failed: %s\n", message);
            std::fflush(stderr);
            std::abort();
        }
    }

    void setParameter(StopDialAudioProcessor& p, const char* id, float unscaled)
    {
        auto* value = p.apvts.getParameter(id);
        check(value != nullptr, "expected parameter exists");
        value->beginChangeGesture();
        value->setValueNotifyingHost(value->convertTo0to1(unscaled));
        value->endChangeGesture();
    }

    struct Signal
    {
        double phase = 0.0;
        double rate = 48000.0;
        explicit Signal(double sampleRate) : rate(sampleRate) {}
        float next(int channel)
        {
            const auto t = phase / rate;
            // 0.42 maximum magnitude: plenty of headroom for interpolation.
            return static_cast<float>((channel ? 0.24 : 0.27) * std::sin(2.0 * juce::MathConstants<double>::pi * 219.0 * t)
                  + (channel ? -0.12 : 0.15) * std::sin(2.0 * juce::MathConstants<double>::pi * 1373.0 * t));
        }
    };

    struct Metrics
    {
        double peak = 0.0;
        double rmsAccum = 0.0;
        double largestStep = 0.0;
        int64_t samples = 0;
        float last[2] { 0.0f, 0.0f };
        bool hasLast = false;
        void observe(int channel, float value)
        {
            check(std::isfinite(value), "NaN or infinity in processor output");
            check(std::abs(value) <= 1.001f, "processed signal clipped despite input headroom");
            check(value == 0.0f || std::fpclassify(value) != FP_SUBNORMAL,
                  "denormal escaped processor output");
            peak = std::max(peak, static_cast<double>(std::abs(value)));
            rmsAccum += static_cast<double>(value) * value;
            ++samples;
            if (hasLast)
                largestStep = std::max(largestStep, static_cast<double>(std::abs(value - last[channel])));
            last[channel] = value;
        }
        double rms() const { return samples ? std::sqrt(rmsAccum / samples) : 0.0; }
    };

    Metrics render(StopDialAudioProcessor& p, Signal& signal,
                   int totalSamples, int maxBlock, bool expectDry = false)
    {
        Metrics m;
        juce::MidiBuffer midi;
        int remaining = totalSamples;
        while (remaining > 0)
        {
            const int n = std::min(maxBlock, remaining);
            juce::AudioBuffer<float> audio(2, n);
            std::vector<float> expected(static_cast<size_t>(n) * 2);
            for (int s = 0; s < n; ++s)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float sample = signal.next(ch);
                    audio.setSample(ch, s, sample);
                    expected[static_cast<size_t>(s) * 2 + ch] = sample;
                }
                signal.phase += 1.0;
            }
            p.processBlock(audio, midi);
            for (int s = 0; s < n; ++s)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float y = audio.getSample(ch, s);
                    m.observe(ch, y);
                    if (expectDry)
                        check(y == expected[static_cast<size_t>(s) * 2 + ch],
                              "dry pass-through changed a sample");
                }
            m.hasLast = true;
            remaining -= n;
        }
        return m;
    }

    void exerciseProcessor(double rate, int block)
    {
        StopDialAudioProcessor p;
        p.setPlayConfigDetails(2, 2, rate, std::max(block, 1024));
        p.prepareToPlay(rate, std::max(block, 1024));
        check(p.getLatencySamples() == 0, "host latency contract changed");
        Signal signal(rate);
        render(p, signal, static_cast<int>(rate / 5), block, true);

        // Use an actual triggered stop. Its tail must be silent in Classic,
        // not a wrapped read from stale history.
        setParameter(p, "mode", 0.0f);
        setParameter(p, "stopAmount", 100.0f);
        p.requestStopFromUI();
        render(p, signal, static_cast<int>(rate * 0.8), block);
        check(p.displayState.load() == StopDialAudioProcessor::stopped,
              "Classic stop did not reach stopped state");
        const auto silent = render(p, signal, std::max(128, static_cast<int>(rate / 20)), block);
        check(silent.peak < 1.0e-5, "Classic stop did not settle to digital silence");

        p.requestResumeFromUI();
        render(p, signal, static_cast<int>(rate / 15), block);
        check(p.displayState.load() == StopDialAudioProcessor::live,
              "resume did not restore live state");
        render(p, signal, std::max(100, static_cast<int>(rate / 10)), block, true);

        setParameter(p, "mode", 2.0f);
        p.requestStopFromUI();
        render(p, signal, static_cast<int>(rate * 0.85), block);
        check(p.displayState.load() == StopDialAudioProcessor::looping,
              "Loop mode did not enter captured loop state");
        const auto loop = render(p, signal, static_cast<int>(rate / 7), block);
        check(loop.rms() > 0.01, "captured loop produced no audio");
        check(loop.largestStep < 0.7, "discontinuous jump in loop output");

        setParameter(p, "loopPattern", 0.0f);
        render(p, signal, static_cast<int>(rate / 20), block);
        const auto muted = render(p, signal, static_cast<int>(rate / 10), block);
        check(muted.peak < 1.0e-4, "muted loop pattern leaked output");
        setParameter(p, "loopPattern", 15.0f);
        render(p, signal, static_cast<int>(rate / 15), block);
        check(render(p, signal, static_cast<int>(rate / 10), block).rms() > 0.01,
              "loop pattern did not recover after automation");

        // Retrigger from frozen loop and exercise Modern's actual filter path.
        // Starting a freshly captured loop with every rhythm step muted
        // must not leak an audible initial gate burst.
        p.requestResumeFromUI();
        render(p, signal, static_cast<int>(rate / 10), block);
        setParameter(p, "loopPattern", 0.0f);
        p.requestStopFromUI();
        render(p, signal, static_cast<int>(rate * 0.9), block);
        const auto initiallyMuted = render(p, signal, static_cast<int>(rate / 20), block);
        check(initiallyMuted.peak < 1.0e-4,
              "fresh muted loop did not settle to silence");

        setParameter(p, "mode", 1.0f);
        p.requestStopFromUI();
        render(p, signal, static_cast<int>(rate * 0.9), block);
        check(p.displayState.load() == StopDialAudioProcessor::stopped,
              "Modern stop did not complete");
        p.requestResumeFromUI();
        render(p, signal, static_cast<int>(rate / 12), block);
        render(p, signal, static_cast<int>(rate / 12), block, true);

        // Corrupt external input should not infect the history or produce NaN.
        juce::AudioBuffer<float> malformed(2, 8);
        malformed.clear();
        malformed.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
        malformed.setSample(1, 1, std::numeric_limits<float>::infinity());
        juce::MidiBuffer midi;
        p.processBlock(malformed, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 8; ++i)
                check(std::isfinite(malformed.getSample(ch, i)),
                      "malformed source poisoned processor output");

        p.setLoopMarkers(0.17f, 0.83f);
        setParameter(p, "curve", 27.0f);
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        check(saved.getSize() > 30, "state save returned empty data");
        StopDialAudioProcessor restored;
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(std::abs(restored.loopStart.load() - 0.17f) < 1.0e-4f,
              "loop start failed to round-trip");
        check(std::abs(restored.loopEnd.load() - 0.83f) < 1.0e-4f,
              "loop end failed to round-trip");
        check(std::abs(restored.apvts.getRawParameterValue("curve")->load() - 27.0f) < 0.11f,
              "automated curve failed to round-trip");
        std::printf("DSP host pass: %.0f Hz, block %d, loop RMS %.5f, max step %.5f, zero latency\n",
                    rate, block, loop.rms(), loop.largestStep);
    }

    void exerciseRealtimeBudget()
    {
        constexpr double rate = 48000.0;
        constexpr int blockSize = 512;
        StopDialAudioProcessor p;
        p.setPlayConfigDetails(2, 2, rate, blockSize);
        p.prepareToPlay(rate, blockSize);
        setParameter(p, "mode", 1.0f); // Modern: most demanding DSP path
        setParameter(p, "stopAmount", 100.0f);
        juce::AudioBuffer<float> audio(2, blockSize);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                audio.setSample(ch, i, static_cast<float>(0.3 * std::sin(i * 0.071 + ch)));
        constexpr int blocks = 256; // 2.73s at 48k; repeatedly exercise braking
        double processingSeconds = 0.0;
        for (int n = 0; n < blocks; ++n)
        {
            if (n % 36 == 0) p.requestStopFromUI();
            const auto start = std::chrono::steady_clock::now();
            p.processBlock(audio, midi);
            const auto end = std::chrono::steady_clock::now();
            processingSeconds += std::chrono::duration<double>(end - start).count();
        }
        const double realTimeRatio = processingSeconds / (blocks * blockSize / rate);
        std::printf("48k Modern active-brake CPU: %.2f%% realtime (processor callback only)\n",
                    realTimeRatio * 100.0);
        // Intentionally generous: fail only true algorithmic performance
        // regressions; hosted CI machines have variable processor loads.
        check(realTimeRatio < 2.0, "Modern engine exceeded 2x realtime CPU budget");
    }

    void exerciseEditor()
    {
        StopDialAudioProcessor p;
        p.prepareToPlay(48000, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        check(editor != nullptr, "could not create plugin editor");
        for (const auto [width, height] : { std::pair{435, 535},
                                            std::pair{725, 892},
                                            std::pair{1087, 1338} })
        {
            editor->setSize(width, height);
            check(editor->getNumChildComponents() >= 5,
                  "editor lost its interactive controls");
            for (int i = 0; i < editor->getNumChildComponents(); ++i)
            {
                const auto* child = editor->getChildComponent(i);
                check(!child->getBounds().isEmpty(), "child has empty bounds");
                check(editor->getLocalBounds().contains(child->getBounds()),
                      "editor child clipped at resized dimensions");
            }
            // The header menu should be hit-testable through the decorative
            // background. Previously the background stole the mouse click.
            check(editor->getComponentAt(40 * width / 725, 40 * height / 892) == editor.get(),
                  "decorative backdrop intercepts menu clicks");
            juce::Image image(juce::Image::ARGB, width, height, true);
            juce::Graphics graphics(image);
            editor->paintEntireComponent(graphics, true);
            int greenPixels = 0;
            for (int y = 0; y < height; y += 3)
                for (int x = 0; x < width; x += 3)
                {
                    const auto colour = image.getPixelAt(x, y);
                    if (colour.getGreen() > colour.getRed() * 1.30f
                        && colour.getGreen() > colour.getBlue() * 1.15f)
                        ++greenPixels;
                }
            check(greenPixels > 30, "vector editor rendering missed the green dial");
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(
                "StopDial-ui-" + juce::String(width) + "x" + juce::String(height) + ".png");
            juce::FileOutputStream stream(file);
            check(stream.openedOk(), "failed to open screenshot output");
            juce::PNGImageFormat png;
            check(png.writeImageToStream(image, stream), "failed to save UI screenshot");
            std::printf("UI render pass: %dx%d, %d green pixels\n", width, height, greenPixels);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    for (const auto [rate, block] : { std::pair{22050.0, 1},
                                      std::pair{48000.0, 37},
                                      std::pair{96000.0, 512} })
        exerciseProcessor(rate, block);
    exerciseRealtimeBudget();
    exerciseEditor();
    std::puts("StopDial host DSP + UI integration tests passed");
    return EXIT_SUCCESS;
}
