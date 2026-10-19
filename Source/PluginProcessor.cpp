#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::StringArray syncChoices {
        "1/1", "1/2", "1/4", "1/8", "1/16",
        "1/1.", "1/2.", "1/4.", "1/8.", "1/16."
    };
    const juce::StringArray loopChoices {
        "1/4", "1/8", "1/8.", "1/16", "1/16.", "1/32", "1/32.", "1/2", "1/2."
    };
    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    float normalizedStopSpeed(double progress, float curve) noexcept
    {
        const double shape = std::pow(2.0, static_cast<double>(curve) / 50.0);
        // Curve from -100 (late brake) to +100 (early brake), continuous at ends.
        return static_cast<float>(1.0 - std::pow(juce::jlimit(0.0, 1.0, progress), shape));
    }
}

const juce::StringArray& StopDialAudioProcessor::syncNames() { return syncChoices; }
const juce::StringArray& StopDialAudioProcessor::loopNames() { return loopChoices; }

StopDialAudioProcessor::StopDialAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STOPDIAL_PARAMETERS", createLayout())
{
    setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout StopDialAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "stopAmount", 1 }, "Stop Amount",
        NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "curve", 1 }, "Stop Curve",
        NormalisableRange<float>(-100.0f, 100.0f, 0.1f), -54.0f));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "syncDiv", 1 }, "Stop Length", syncChoices, 2));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mode", 1 }, "Mode",
        StringArray { "Classic", "Modern", "Loop" }, 2));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "loopDiv", 1 }, "Loop Length", loopChoices, 2));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "loopPattern", 1 }, "Loop Pattern", 0, 15, 15));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "lock", 1 }, "Brake Lock", false));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "lockAmount", 1 }, "Lock Speed",
        NormalisableRange<float>(0.0f, 100.0f, 0.1f), 14.0f));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "stopTrigger", 1 }, "Stop Trigger", false));
    return l;
}

bool StopDialAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && in == layout.getMainOutputChannelSet();
}

void StopDialAudioProcessor::prepareToPlay(double newSampleRate, int /*maxBlockSize*/)
{
    sampleRateHz = juce::jmax(1.0, newSampleRate);
    historySize = juce::jmax(8192, static_cast<int>(std::ceil(sampleRateHz * 8.0)) + 8);
    history.setSize(2, historySize, false, true, false);
    history.clear();
    writeHead = 0;
    readHead = 0.0;
    stopSample = 0;
    stopDuration = 1;
    state = EngineState::passThrough;
    oldTrigger = false;
    lowpass[0] = lowpass[1] = 0.0f;
    peakSamples = juce::jmax(16, static_cast<int>(sampleRateHz / 160.0));
    peakCount = 0;
    peakMin = peakMax = 0.0f;
    currentBeatSeconds = 0.5;
    heldLoopSize = 1;
    amountSmooth.reset(sampleRateHz, 0.02);
    curveSmooth.reset(sampleRateHz, 0.015);
    lockSmooth.reset(sampleRateHz, 0.02);
    outputMix.reset(sampleRateHz, 0.008);
    cutoffSmooth.reset(sampleRateHz, 0.012);
    amountSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("stopAmount")->load() * 0.01f);
    curveSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("curve")->load());
    lockSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("lockAmount")->load() * 0.01f);
    outputMix.setCurrentAndTargetValue(0.0f);
    cutoffSmooth.setCurrentAndTargetValue(18000.0f);
    transportProgress.store(0.0f, std::memory_order_relaxed);
    setLatencySamples(0);
}

void StopDialAudioProcessor::setLoopMarkers(float start, float end) noexcept
{
    const float a = juce::jlimit(0.0f, 0.95f, start);
    const float b = juce::jlimit(a + 0.05f, 1.0f, end);
    loopStart.store(a, std::memory_order_relaxed);
    loopEnd.store(b, std::memory_order_relaxed);
}

// 1/4 means one quarter note. 1/4. means a dotted quarter note.
double StopDialAudioProcessor::beatLengthForIndex(int index) noexcept
{
    static constexpr double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25,
                                      6.0, 3.0, 1.5, 0.75, 0.375 };
    return beats[juce::jlimit(0, 9, index)];
}

void StopDialAudioProcessor::updateBeat(double bpm) noexcept
{
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0) bpm = 120.0;
    currentBeatSeconds = 60.0 / bpm;
    detectedBpm.store(static_cast<float>(bpm), std::memory_order_relaxed);
}

void StopDialAudioProcessor::beginStop(double bpm, int syncIndex) noexcept
{
    updateBeat(bpm);
    stopDuration = juce::jmax<int64_t>(1,
        static_cast<int64_t>(std::llround(sampleRateHz * currentBeatSeconds
                                         * beatLengthForIndex(syncIndex))));
    stopSample = 0;
    readHead = static_cast<double>((writeHead + historySize - 1) % historySize);
    state = EngineState::slowing;
    lowpass[0] = lowpass[1] = 0.0f;
}

double StopDialAudioProcessor::wrap(double position) const noexcept
{
    if (position >= static_cast<double>(historySize) || position < 0.0)
    {
        position = std::fmod(position, static_cast<double>(historySize));
        if (position < 0.0) position += historySize;
    }
    return position;
}

double StopDialAudioProcessor::interpolate(int channel, double position) const noexcept
{
    const double p = wrap(position);
    const auto i = static_cast<int>(p);
    const float f = static_cast<float>(p - static_cast<double>(i));
    const auto get = [this, channel](int index) noexcept -> float
    {
        if (index < 0) index += historySize;
        if (index >= historySize) index -= historySize;
        return history.getSample(channel, index);
    };
    const float x0 = get(i - 1), x1 = get(i), x2 = get(i + 1), x3 = get(i + 2);
    // Four-point Catmull-Rom / Hermite interpolation.
    const float c0 = x1;
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

float StopDialAudioProcessor::renderLoop(int channel, double phase) const noexcept
{
    const int fadeLen = juce::jmin(juce::jmax(2, static_cast<int>(sampleRateHz * 0.006)),
                                 juce::jmax(2, heldLoopSize / 4));
    // Overlap splice: advance the physical read head by the overlap duration.
    // At the end of the period the fade points to 'start + overlap', so the
    // wrapped next sample is continuous rather than jumping back to 'start'.
    const double period = static_cast<double>(juce::jmax(1, heldLoopSize - fadeLen));
    phase = std::fmod(phase, period);
    if (phase < 0.0) phase += period;
    const double physical = phase + fadeLen;
    const double tail = interpolate(channel, heldLoopStart + physical);
    if (phase >= period - fadeLen)
    {
        const float blend = juce::jlimit(0.0f, 1.0f,
            static_cast<float>((phase - (period - fadeLen)) / fadeLen));
        const double head = interpolate(channel,
            heldLoopStart + phase - (period - fadeLen));
        return static_cast<float>(tail * std::cos(blend * halfPi)
                                  + head * std::sin(blend * halfPi));
    }
    return static_cast<float>(tail);
}

void StopDialAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(2, buffer.getNumChannels());
    const int count = buffer.getNumSamples();
    if (historySize <= 4 || channels == 0) return;

    double bpm = 120.0;
    if (auto* playhead = getPlayHead())
    {
        if (auto pos = playhead->getPosition())
            if (auto hostBpm = pos->getBpm()) bpm = *hostBpm;
    }
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0) bpm = 120.0;
    updateBeat(bpm);

    const auto value = [this](const char* id) noexcept -> float
    { return apvts.getRawParameterValue(id)->load(std::memory_order_relaxed); };
    const int sync = juce::jlimit(0, 9, static_cast<int>(value("syncDiv")));
    const int loopDiv = juce::jlimit(0, 8, static_cast<int>(value("loopDiv")));
    const int mode = juce::jlimit(0, 2, static_cast<int>(value("mode")));
    const int pattern = juce::jlimit(0, 15, static_cast<int>(value("loopPattern")));
    const bool locked = value("lock") > 0.5f;
    const bool pressed = value("stopTrigger") > 0.5f;

    amountSmooth.setTargetValue(value("stopAmount") / 100.0f);
    curveSmooth.setTargetValue(value("curve"));
    lockSmooth.setTargetValue(value("lockAmount") / 100.0f);

    if ((pressed && !oldTrigger) || pendingUIStop.exchange(false, std::memory_order_acq_rel))
        beginStop(bpm, sync);
    if (!pressed && oldTrigger)
    {
        // A short tap still completes the programmed stop; the button is momentary.
        // The next trigger retriggers from the current history.
    }
    oldTrigger = pressed;

    for (int s = 0; s < count; ++s)
    {
        float dry[2] { 0.0f, 0.0f };
        for (int ch = 0; ch < channels; ++ch)
            dry[ch] = buffer.getSample(ch, s);
        // Continuously record incoming audio while running or braking. Freeze it
        // in the held state, so a long loop cannot be overwritten by live input.
        if (state != EngineState::halted)
        {
            for (int ch = 0; ch < channels; ++ch)
                history.setSample(ch, writeHead, dry[ch]);
            writeHead = (writeHead + 1) % historySize;
        }

        const float depth = amountSmooth.getNextValue();
        const float curve = curveSmooth.getNextValue();
        const float minSpeed = locked ? lockSmooth.getNextValue() : 0.0f;
        if (!locked) lockSmooth.getNextValue();
        const float desiredMix = state == EngineState::passThrough ? 0.0f : depth;
        outputMix.setTargetValue(desiredMix);
        const float wetMix = outputMix.getNextValue();

        const double t = state == EngineState::slowing
            ? juce::jlimit(0.0, 1.0, static_cast<double>(stopSample) /
                                   static_cast<double>(stopDuration))
            : 1.0;
        const float speed = juce::jmax(minSpeed, normalizedStopSpeed(t, curve));
        // Smooth the filter cutoff for modern mode, including source changes.
        const float targetCutoff = mode == 1 && state != EngineState::passThrough
            ? 280.0f + 17720.0f * std::pow(speed, 1.7f) : 18000.0f;
        cutoffSmooth.setTargetValue(targetCutoff);
        const float cutoff = cutoffSmooth.getNextValue();
        const float alpha = std::exp(-juce::MathConstants<float>::twoPi
                                    * cutoff / static_cast<float>(sampleRateHz));

        // Shared fractional cursor preserves stereo phase alignment.
        const double sharedRead = readHead;
        const double loopPhase = state == EngineState::halted
            ? static_cast<double>(stopSample) : 0.0;
        const int loopOverlap = juce::jmin(
            juce::jmax(2, static_cast<int>(sampleRateHz * 0.006)),
            juce::jmax(2, heldLoopSize / 4));
        const int loopPeriod = juce::jmax(1, heldLoopSize - loopOverlap);
        const int rhythmStep = state == EngineState::halted
            ? juce::jlimit(0, 3, static_cast<int>(4.0 *
                std::fmod(loopPhase, static_cast<double>(loopPeriod)) /
                loopPeriod))
            : 0;
        const float phaseWithinStep = state == EngineState::halted
            ? static_cast<float>(std::fmod(loopPhase,
                juce::jmax(1.0, static_cast<double>(loopPeriod) * 0.25)))
            : 0.0f;
        const float gateFade = juce::jmax(1.0f, static_cast<float>(sampleRateHz * 0.002));
        const float gateEnvelope = juce::jmin(1.0f,
            juce::jmin(phaseWithinStep / gateFade,
                juce::jmax(0.0f, static_cast<float>(loopPeriod) * 0.25f
                     - phaseWithinStep) / gateFade));
        const float rhythmGain = (pattern & (1 << rhythmStep)) != 0 ? gateEnvelope : 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            float wet = dry[ch];
            if (state == EngineState::slowing)
                wet = static_cast<float>(interpolate(ch, sharedRead));
            else if (state == EngineState::halted)
                wet = mode == 2 ? renderLoop(ch, loopPhase) * rhythmGain : 0.0f;

            if (mode == 1 && state != EngineState::passThrough)
            {
                lowpass[ch] = (1.0f - alpha) * wet + alpha * lowpass[ch];
                const float drive = 1.0f + 0.9f * (1.0f - speed);
                wet = std::tanh(lowpass[ch] * drive) / std::tanh(drive);
            }
            // For a lock, a nonzero minimum speed is permitted; even when the
            // trigger has ended, held state is a frozen loop or silence.
            buffer.setSample(ch, s, dry[ch] + (wet - dry[ch]) * wetMix);
        }

        if (state == EngineState::slowing)
        {
            readHead = wrap(readHead + static_cast<double>(speed));
            ++stopSample;
            transportProgress.store(static_cast<float>(t), std::memory_order_relaxed);
            if (stopSample >= stopDuration)
            {
                state = EngineState::halted;
                // Dedicated fixed point in the input ring: stop writing to keep
                // the last slice available for arbitrary-length looping.
                // Map loop divisions into the whole-note reference table.
                static constexpr double loopBeats[] {
                    1.0, 0.5, 0.75, 0.25, 0.375, 0.125, 0.1875, 2.0, 3.0
                };
                const int sourceLength = juce::jlimit(32, historySize - 8,
                    static_cast<int>(sampleRateHz * currentBeatSeconds * loopBeats[loopDiv]));
                const float markerA = loopStart.load(std::memory_order_relaxed);
                const float markerB = juce::jmax(markerA + 0.05f,
                                    loopEnd.load(std::memory_order_relaxed));
                heldLoopSize = juce::jmax(16,
                    static_cast<int>(sourceLength * (markerB - markerA)));
                heldLoopStart = wrap(static_cast<double>(writeHead - sourceLength)
                                   + static_cast<int>(sourceLength * markerA));
                stopSample = 0;
                transportProgress.store(1.0f, std::memory_order_relaxed);
            }
        }
        else if (state == EngineState::halted)
        {
            ++stopSample;
            transportProgress.store(static_cast<float>(std::fmod(
                static_cast<double>(stopSample) / loopPeriod, 1.0)),
                std::memory_order_relaxed);
        }
        else transportProgress.store(0.0f, std::memory_order_relaxed);

        // The waveform FIFO never blocks the audio callback.
        float mn = dry[0], mx = dry[0];
        for (int ch = 1; ch < channels; ++ch)
        {
            mn = juce::jmin(mn, dry[ch]);
            mx = juce::jmax(mx, dry[ch]);
        }
        if (peakCount == 0) { peakMin = mn; peakMax = mx; }
        else { peakMin = juce::jmin(peakMin, mn); peakMax = juce::jmax(peakMax, mx); }
        if (++peakCount >= peakSamples)
        {
            peaks.push({ peakMin, peakMax });
            peakCount = 0;
        }
    }
}

juce::AudioProcessorEditor* StopDialAudioProcessor::createEditor()
{
    return new StopDialAudioProcessorEditor(*this);
}

void StopDialAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = apvts.copyState();
    tree.setProperty("markerStart", loopStart.load(), nullptr);
    tree.setProperty("markerEnd", loopEnd.load(), nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, dest);
}

void StopDialAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            setLoopMarkers(static_cast<float>(tree.getProperty("markerStart", 0.25f)),
                           static_cast<float>(tree.getProperty("markerEnd", 0.75f)));
            apvts.replaceState(tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StopDialAudioProcessor(); }
