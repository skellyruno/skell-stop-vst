#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

struct WavePeak { float minimum = 0.0f, maximum = 0.0f; };

// Single producer: audio callback. Single consumer: editor timer. Never allocates or locks.
class WavePeakFifo final
{
public:
    static constexpr uint32_t capacity = 4096;
    void push(WavePeak value) noexcept
    {
        auto w = write.load(std::memory_order_relaxed);
        const auto r = read.load(std::memory_order_acquire);
        if (w - r >= capacity) return; // drop newest safely when editor is closed
        storage[w % capacity] = value;
        write.store(w + 1, std::memory_order_release);
    }
    bool pop(WavePeak& value) noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return false;
        value = storage[r % capacity];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<WavePeak, capacity> storage {};
    std::atomic<uint32_t> write { 0 }, read { 0 };
};

class StopDialAudioProcessor final : public juce::AudioProcessor
{
public:
    StopDialAudioProcessor();
    ~StopDialAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "StopDial"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    const juce::String getProgramName(int) override { return {}; }
    void setCurrentProgram(int) override {}
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    WavePeakFifo peaks;
    std::atomic<float> transportProgress { 0.0f };
    std::atomic<float> detectedBpm { 120.0f };
    // Persisted marker positions in 0..1 of the visual waveform.
    std::atomic<float> loopStart { 0.25f }, loopEnd { 0.75f };
    void setLoopMarkers(float start, float end) noexcept;
    void requestStopFromUI() noexcept { pendingUIStop.store(true, std::memory_order_release); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& syncNames();
    static const juce::StringArray& loopNames();

private:
    enum class EngineState { passThrough, slowing, halted } state = EngineState::passThrough;
    juce::AudioBuffer<float> history;
    int historySize = 0, writeHead = 0;
    double sampleRateHz = 44100.0, readHead = 0.0;
    int64_t stopSample = 0, stopDuration = 1;
    double currentBeatSeconds = 0.5, heldLoopStart = 0.0;
    int heldLoopSize = 1;
    bool oldTrigger = false;
    std::atomic<bool> pendingUIStop { false };
    float lowpass[2] { 0.0f, 0.0f };
    int peakCount = 0, peakSamples = 128;
    float peakMin = 0.0f, peakMax = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        amountSmooth, curveSmooth, lockSmooth, outputMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmooth;

    static double beatLengthForIndex(int index) noexcept;
    double interpolate(int channel, double position) const noexcept;
    double wrap(double position) const noexcept;
    void beginStop(double bpm, int syncIndex) noexcept;
    void updateBeat(double bpm) noexcept;
    float renderLoop(int channel, double loopPhase) const noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StopDialAudioProcessor)
};
