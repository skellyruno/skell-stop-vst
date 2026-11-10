#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/StopDialMath.h"
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
    // Additional parameter does not alter existing IDs/preset state.
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "resumeTrigger", 1 }, "Resume Live", false));
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
    // A whole note at the slowest supported host BPM (20) lasts 12 seconds.
    // Keep enough data for the complete brake and the longest loop window,
    // including interpolation guard samples. Allocate only in prepareToPlay.
    historySize = juce::jmax(8192, static_cast<int>(std::ceil(sampleRateHz * 16.0)) + 8);
    history.setSize(2, historySize, false, true, false);
    history.clear();
    // Max loop division is 3 quarter-note beats: 9 s at the 20 BPM floor.
    loopClip.setSize(2, juce::jmax(64, static_cast<int>(std::ceil(sampleRateHz * 9.0)) + 16),
                     false, true, false);
    loopClip.clear();
    // Do not reset the FIFO counters while an already-open editor may be
    // draining peaks on the message thread during a host sample-rate change.
    pendingUIStop.store(false, std::memory_order_relaxed);
    pendingUIResume.store(false, std::memory_order_relaxed);
    writeHead = 0;
    readHead = 0.0;
    stopSample = 0;
    stopDuration = 1;
    stopPhase = 0.0;
    loopCursor = 0.0;
    capturedBeatSeconds = 0.5;
    latchedStopBeats = 1.0;
    clipCopied = clipToCopy = 0;
    clipReady = false;
    lastOutput[0] = lastOutput[1] = 0.0f;
    transitionFrom[0] = transitionFrom[1] = 0.0f;
    transitionRemaining = 0;
    tapeLag = 0.0;
    safetyReleaseRemaining = 0;
    safetyReleaseLength = juce::jmax(1, static_cast<int>(sampleRateHz * 0.05));
    transitionLength = juce::jmax(1, static_cast<int>(sampleRateHz * 0.006));
    lastMode = juce::jlimit(0, 2, static_cast<int>(apvts.getRawParameterValue("mode")->load()));
    state = EngineState::passThrough;
    oldTrigger = oldResume = false;
    displayState.store(live, std::memory_order_relaxed);
    for (auto& tone : modernTone) tone.prime(0.0f);
    modernCutoff.prepare(sampleRateHz);
    loopGateSlew.prepare(sampleRateHz);
    dcPole = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * 12.0 / sampleRateHz));
    peakSamples = juce::jmax(16, static_cast<int>(sampleRateHz / 160.0));
    peakCount = 0;
    peakMin = peakMax = 0.0f;
    currentBeatSeconds = 0.5;
    heldLoopSize = 1;
    amountSmooth.reset(sampleRateHz, 0.02);
    curveSmooth.reset(sampleRateHz, 0.015);
    lockSmooth.reset(sampleRateHz, 0.02);
    outputMix.reset(sampleRateHz, 0.008);
    amountSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("stopAmount")->load() * 0.01f);
    curveSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("curve")->load());
    lockSmooth.setCurrentAndTargetValue(apvts.getRawParameterValue("lockAmount")->load() * 0.01f);
    outputMix.setCurrentAndTargetValue(0.0f);
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
    currentBeatSeconds = StopDialMath::beatSeconds(bpm);
    detectedBpm.store(static_cast<float>(bpm), std::memory_order_relaxed);
}

void StopDialAudioProcessor::beginStop(double bpm, int syncIndex) noexcept
{
    updateBeat(bpm);
    latchedStopBeats = beatLengthForIndex(syncIndex);
    stopDuration = juce::jmax<int64_t>(1,
        static_cast<int64_t>(std::llround(sampleRateHz * currentBeatSeconds * latchedStopBeats)));
    stopPhase = 0.0;
    stopSample = 0;
    tapeLag = 1.0;
    safetyReleaseRemaining = 0;
    // Causal cubic: the reader trails the newest written sample by at least
    // four frames. Hermite never sees future/uninitialised history samples.
    readHead = static_cast<double>(StopDialMath::causalReaderIndex(writeHead, historySize));
    state = EngineState::slowing;
    // Avoid dragging stale IIR history into a fresh retrigger after minutes of
    // dry input. The output handoff crossfade masks this reinitialisation.
    for (int ch = 0; ch < 2; ++ch) modernTone[static_cast<size_t>(ch)].prime(lastOutput[ch]);
    modernCutoff.prepare(sampleRateHz);
    loopGateSlew.prime(1.0f);
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
    return StopDialMath::boundedHermite(get(i - 1), get(i), get(i + 1), get(i + 2), f);
}

float StopDialAudioProcessor::interpolateClip(int channel, double position) const noexcept
{
    // The snapshot has two pre-roll and four trailing guard samples, so the
    // interpolator never wraps within an uninitialised portion of loopClip.
    const auto i = static_cast<int>(position);
    const float t = static_cast<float>(position - i);
    return StopDialMath::boundedHermite(loopClip.getSample(channel, i - 1),
                                        loopClip.getSample(channel, i),
                                        loopClip.getSample(channel, i + 1),
                                        loopClip.getSample(channel, i + 2), t);
}

float StopDialAudioProcessor::interpolateLiveLoop(int channel, double position) const noexcept
{
    // During the incremental snapshot, listen to the same *periodic* window
    // used by the finished clip, so the audio does not jump when clipReady flips.
    const int i = static_cast<int>(position);
    const float frac = static_cast<float>(position - i);
    const auto sample = [this, channel](int relative) noexcept -> float
    {
        const auto offset = StopDialMath::wrapLoopIndex(relative, heldLoopSize);
        return history.getSample(channel,
            static_cast<int>(wrap(heldLoopStart + static_cast<double>(offset))));
    };
    return StopDialMath::boundedHermite(sample(i - 1), sample(i),
                                         sample(i + 1), sample(i + 2), frac);
}

void StopDialAudioProcessor::copyClipStep() noexcept
{
    if (clipReady || clipToCopy == 0) return;
    // Fixed 8 samples of work at most per audio frame: there is never a
    // multi-second memory copy in the realtime callback when a loop starts.
    for (int n = 0; n < 8 && clipCopied < clipToCopy; ++n, ++clipCopied)
    {
        // Main region: [2, loopSize+1]. The two samples before it and four
        // following it are loop-wrapped guards, not unrelated live input.
        const int relative = StopDialMath::wrapLoopIndex(clipCopied - 2, heldLoopSize);
        const int source = static_cast<int>(wrap(heldLoopStart + relative));
        for (int ch = 0; ch < 2; ++ch)
            loopClip.setSample(ch, clipCopied, history.getSample(ch, source));
    }
    clipReady = clipCopied == clipToCopy;
}

void StopDialAudioProcessor::startTransition() noexcept
{
    transitionFrom[0] = lastOutput[0];
    transitionFrom[1] = lastOutput[1];
    transitionRemaining = transitionLength;
}

void StopDialAudioProcessor::finishStop(int mode, int loopDiv) noexcept
{
    stopSample = 0;
    loopCursor = 0.0;
    capturedBeatSeconds = currentBeatSeconds;
    transportProgress.store(1.0f, std::memory_order_relaxed);
    if (mode != 2)
    {
        const bool locked = apvts.getRawParameterValue("lock")->load() > 0.5f;
        state = locked ? EngineState::lockedRun : EngineState::halted;
        return;
    }

    // Preserve the last slowing sample when entering Loop: the newly captured
    // slice can begin at an unrelated phase or amplitude. Without this handoff
    // the output jumped immediately at the stop/loop boundary.
    startTransition();

    // Map loop divisions into quarter-note beats. Capture the source position
    // in the always-running input history, then make a guarded loop snapshot
    // incrementally. The snapshot can repeat indefinitely while new input is
    // recorded for future retriggers.
    static constexpr double loopBeats[] {
        1.0, 0.5, 0.75, 0.25, 0.375, 0.125, 0.1875, 2.0, 3.0
    };
    const int sourceLength = juce::jlimit(32, historySize - 8,
        static_cast<int>(sampleRateHz * currentBeatSeconds * loopBeats[juce::jlimit(0, 8, loopDiv)]));
    const float a = loopStart.load(std::memory_order_relaxed);
    const float b = juce::jlimit(a + 0.05f, 1.0f,
                               loopEnd.load(std::memory_order_relaxed));
    heldLoopSize = juce::jlimit(16, sourceLength,
                               static_cast<int>(sourceLength * (b - a)));
    // If a very short selected window must be expanded to the 16-frame
    // minimum, shift its start backward instead of reading unrecorded future
    // samples beyond the current write cursor.
    const int startOffset = juce::jlimit(0, sourceLength - heldLoopSize,
                                         static_cast<int>(sourceLength * a));
    heldLoopStart = wrap(static_cast<double>(writeHead - sourceLength + startOffset));
    clipToCopy = heldLoopSize + 6;
    clipCopied = 0;
    clipReady = false;
    // Respect the actual rhythm mask at the *first* loop sample. Priming at
    // unity made an all-muted pattern briefly audible on every fresh capture;
    // priming the exact target avoids that burst and preserves all-on unity.
    const int pattern = juce::jlimit(0, 15,
        static_cast<int>(apvts.getRawParameterValue("loopPattern")->load()));
    const double period = StopDialMath::loopPeriodSamples(heldLoopSize, sampleRateHz);
    loopGateSlew.prime(StopDialMath::rhythmGate(pattern, 0.0, period,
                                               sampleRateHz * 0.002));
    state = EngineState::halted;
}

float StopDialAudioProcessor::renderLoop(int channel, double phase) const noexcept
{
    const int fadeLength = StopDialMath::loopOverlapSamples(heldLoopSize, sampleRateHz);
    const double period = static_cast<double>(StopDialMath::loopPeriodSamples(heldLoopSize, sampleRateHz));
    if (phase >= period || phase < 0.0)
    {
        phase = std::fmod(phase, period);
        if (phase < 0.0) phase += period;
    }

    const auto read = [this, channel](double position) noexcept -> float
    {
        return clipReady ? interpolateClip(channel, 2.0 + position)
                         : interpolateLiveLoop(channel, position);
    };
    const float tail = read(phase + fadeLength);
    if (phase >= period - fadeLength)
    {
        const float blend = static_cast<float>((phase - (period - fadeLength)) / fadeLength);
        const float head = read(phase - (period - fadeLength));
        // Correlated signals remain at unity on the seam; equal-power
        // crossfades can create +3dB bumps on nearly identical samples.
        return StopDialMath::splice(tail, head, blend);
    }
    return tail;
}

void StopDialAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(2, buffer.getNumChannels());
    const int count = buffer.getNumSamples();
    if (historySize <= 4 || channels == 0) return;

    double bpm = 120.0;
    if (auto* playhead = getPlayHead())
        if (auto position = playhead->getPosition())
            if (auto hostBpm = position->getBpm()) bpm = *hostBpm;
    updateBeat(bpm);

    const auto value = [this](const char* id) noexcept -> float
    { return apvts.getRawParameterValue(id)->load(std::memory_order_relaxed); };
    const int sync = juce::jlimit(0, 9, static_cast<int>(value("syncDiv")));
    const int loopDiv = juce::jlimit(0, 8, static_cast<int>(value("loopDiv")));
    const int mode = juce::jlimit(0, 2, static_cast<int>(value("mode")));
    const int pattern = juce::jlimit(0, 15, static_cast<int>(value("loopPattern")));
    const bool locked = value("lock") > 0.5f;
    const bool pressed = value("stopTrigger") > 0.5f;
    const bool resumePressed = value("resumeTrigger") > 0.5f;

    amountSmooth.setTargetValue(value("stopAmount") * 0.01f);
    curveSmooth.setTargetValue(value("curve"));
    lockSmooth.setTargetValue(value("lockAmount") * 0.01f);

    // Change the brake's phase velocity (beats per sample), not its elapsed
    // sample counter. A host tempo change preserves the phase of the stop.
    if (state == EngineState::slowing)
        stopDuration = juce::jmax<int64_t>(1, static_cast<int64_t>(std::llround(
            sampleRateHz * currentBeatSeconds * latchedStopBeats)));

    const bool trigger = (pressed && !oldTrigger)
                         || pendingUIStop.exchange(false, std::memory_order_acq_rel);
    const bool resume = (resumePressed && !oldResume)
                        || pendingUIResume.exchange(false, std::memory_order_acq_rel);
    oldTrigger = pressed;
    oldResume = resumePressed;
    // A stop wins if both triggers arrive in the same callback.
    if (trigger)
    {
        startTransition();
        beginStop(bpm, sync);
    }
    else if (resume)
    {
        if (state != EngineState::passThrough) startTransition();
        state = EngineState::passThrough;
        stopPhase = 0.0;
        safetyReleaseRemaining = 0;
        transportProgress.store(0.0f, std::memory_order_relaxed);
    }
    else if (mode != lastMode && state != EngineState::passThrough)
    {
        startTransition();
        if ((state == EngineState::lockedRun || state == EngineState::halted)
            && mode == 2)
            finishStop(mode, loopDiv);
    }
    lastMode = mode;
    if (state == EngineState::lockedRun && !locked)
    {
        startTransition();
        state = EngineState::halted;
    }
    else if (state == EngineState::halted && mode != 2 && locked)
    {
        startTransition();
        state = EngineState::lockedRun;
    }

    const int display = state == EngineState::passThrough ? live
        : state == EngineState::slowing ? braking
        : state == EngineState::lockedRun ? holding
        : mode == 2 ? looping : stopped;
    displayState.store(display, std::memory_order_relaxed);

    // Must be refreshed if a stop finishes *inside* this processBlock:
    // previously the next loop could advance modulo the previous clip's size
    // for the remainder of the block, audibly freezing a freshly captured loop.
    int loopPeriod = StopDialMath::loopPeriodSamples(heldLoopSize, sampleRateHz);

    // Hot path: when fully LIVE there is no tape resampling, filter, tanh,
    // fading or phase arithmetic. Preserve dry input exactly while recording
    // fresh audio for later retriggers and keeping the waveform meter alive.
    // This runs only once the previous resume crossfade has fully settled.
    if (state == EngineState::passThrough && transitionRemaining == 0
        && !outputMix.isSmoothing() && outputMix.getCurrentValue() == 0.0f)
    {
        for (int s = 0; s < count; ++s)
        {
            float mn = 0.0f, mx = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                float sample = buffer.getSample(ch, s);
                if (!std::isfinite(sample))
                {
                    sample = 0.0f;
                    buffer.setSample(ch, s, sample);
                }
                history.setSample(ch, writeHead, sample);
                if (ch == 0) mn = mx = sample;
                else { mn = juce::jmin(mn, sample); mx = juce::jmax(mx, sample); }
                lastOutput[ch] = sample;
            }
            if (++writeHead >= historySize) writeHead = 0;
            if (peakCount == 0) { peakMin = mn; peakMax = mx; }
            else { peakMin = juce::jmin(peakMin, mn); peakMax = juce::jmax(peakMax, mx); }
            if (++peakCount >= peakSamples)
            {
                peaks.push({ peakMin, peakMax });
                peakCount = 0;
            }
        }
        transportProgress.store(0.0f, std::memory_order_relaxed);
        return;
    }

    for (int s = 0; s < count; ++s)
    {
        float dry[2] { 0.0f, 0.0f };
        for (int ch = 0; ch < channels; ++ch)
        {
            dry[ch] = buffer.getSample(ch, s);
            // A broken upstream plugin must not permanently poison the delay
            // history or Modern filter with NaN / infinity.
            if (!std::isfinite(dry[ch])) dry[ch] = 0.0f;
            history.setSample(ch, writeHead, dry[ch]);
        }
        if (++writeHead >= historySize) writeHead = 0;

        const float depth = amountSmooth.getNextValue();
        const float curve = curveSmooth.getNextValue();
        const float minSpeed = locked ? lockSmooth.getNextValue() : 0.0f;
        if (!locked) lockSmooth.getNextValue();
        outputMix.setTargetValue(state == EngineState::passThrough ? 0.0f : depth);
        const float wetMix = outputMix.getNextValue();

        // The exponent/pow curve evaluation is needed only while braking;
        // frozen loops otherwise spent CPU calculating stopSpeed(1.0).
        float speed = state == EngineState::passThrough ? 1.0f : 0.0f;
        if (state == EngineState::slowing)
            speed = juce::jmax(minSpeed, static_cast<float>(
                StopDialMath::stopSpeed(stopPhase, curve)));
        else if (state == EngineState::lockedRun)
            speed = minSpeed;
        // Don't calculate transcendental filter coefficients in Classic/Loop,
        // particularly when the effect is passing through live input.
        const bool modernProcessing = mode == 1 && state != EngineState::passThrough;
        // Compute cutoff's pow/exp only every 16 frames; an 8 ms coefficient
        // slew ensures no zipper noise, including irregular host block sizes.
        const float oneMinusAlpha = modernProcessing ? modernCutoff.next(speed) : 0.0f;

        // A severe BPM/curve change can exhaust the history even before a
        // stop completes. Fade to clean dry audio while there is still enough
        // recorded input for every remaining read; never wrap into overwritten
        // samples. The same guard applies to indefinite speed-lock playback.
        if ((state == EngineState::slowing || state == EngineState::lockedRun)
            && safetyReleaseRemaining == 0
            && StopDialMath::historyOverwriteImminent(tapeLag, historySize,
                                                       safetyReleaseLength))
            safetyReleaseRemaining = safetyReleaseLength;
        const float safetyGain = safetyReleaseRemaining > 0
            ? StopDialMath::smoothFade(static_cast<float>(safetyReleaseRemaining)
                                      / static_cast<float>(safetyReleaseLength)) : 1.0f;
        const double loopRate = capturedBeatSeconds / currentBeatSeconds;
        const double sharedRead = readHead; // stereo uses the same fractional read cursor
        const bool loopPlaying = state == EngineState::halted && mode == 2;
        const double loopPhase = loopCursor;
        const float rhythmGain = loopPlaying
            ? loopGateSlew.next(StopDialMath::rhythmGate(
                pattern, loopPhase, loopPeriod, sampleRateHz * 0.002 * loopRate))
            : 1.0f;
        // The brake tails smoothly to zero, including before entering a loop.
        const float remaining = static_cast<float>((1.0 - stopPhase) * stopDuration);
        // Loop mode uses the handoff crossfade at stop completion. Do NOT
        // pre-fade to zero as well, which creates a 12 ms transient dropout.
        const float stopExit = state == EngineState::slowing && !locked && mode != 2
            ? StopDialMath::smoothFade(remaining / static_cast<float>(transitionLength))
            : 1.0f;
        const float transition = transitionRemaining > 0
            ? StopDialMath::transitionGain(1.0f - static_cast<float>(transitionRemaining)
                                             / static_cast<float>(transitionLength)) : 1.0f;

        for (int ch = 0; ch < channels; ++ch)
        {
            float wet = dry[ch];
            if (state == EngineState::slowing || state == EngineState::lockedRun)
                wet = static_cast<float>(interpolate(ch, sharedRead)) * stopExit;
            else if (state == EngineState::halted)
                wet = loopPlaying ? renderLoop(ch, loopPhase) * rhythmGain : 0.0f;

            if (modernProcessing)
                wet = modernTone[static_cast<size_t>(ch)].process(wet, speed,
                                                               oneMinusAlpha, dcPole);
            else
                modernTone[static_cast<size_t>(ch)].prime(wet);

            const float output = dry[ch] + (wet - dry[ch]) * wetMix * safetyGain;
            const float seamless = transitionFrom[ch] + (output - transitionFrom[ch]) * transition;
            const float rendered = transitionRemaining > 0 ? seamless : output;
            buffer.setSample(ch, s, rendered);
            lastOutput[ch] = rendered;
        }
        if (transitionRemaining > 0) --transitionRemaining;

        if (state == EngineState::slowing)
        {
            readHead = wrap(readHead + static_cast<double>(speed));
            tapeLag += 1.0 - speed;
            stopPhase = juce::jmin(1.0, stopPhase + 1.0 / static_cast<double>(stopDuration));
            ++stopSample;
            if ((s & 31) == 0 || stopPhase >= 1.0)
                transportProgress.store(static_cast<float>(stopPhase), std::memory_order_relaxed);
            // A safety release takes priority over the normal stop/loop
            // handoff: otherwise a completed stop could freeze the release
            // envelope and leave the output attenuated indefinitely.
            if (safetyReleaseRemaining > 0)
            {
                if (--safetyReleaseRemaining == 0)
                {
                    state = EngineState::passThrough;
                    stopPhase = 0.0;
                    displayState.store(live, std::memory_order_relaxed);
                    transportProgress.store(0.0f, std::memory_order_relaxed);
                }
            }
            else if (stopPhase >= 1.0)
            {
                finishStop(mode, loopDiv);
                displayState.store(mode == 2 ? looping : locked ? holding : stopped,
                                   std::memory_order_relaxed);
                loopPeriod = StopDialMath::loopPeriodSamples(heldLoopSize, sampleRateHz);
            }
        }
        else if (state == EngineState::lockedRun)
        {
            readHead = wrap(readHead + static_cast<double>(speed));
            tapeLag += 1.0 - speed;
            if (safetyReleaseRemaining > 0 && --safetyReleaseRemaining == 0)
            {
                state = EngineState::passThrough; // bounded buffer cannot hold an endless brake
                displayState.store(live, std::memory_order_relaxed);
            }
            if ((s & 31) == 0)
                transportProgress.store(1.0f, std::memory_order_relaxed);
        }
        else if (state == EngineState::halted)
        {
            if (loopPlaying)
            {
                copyClipStep();
                loopCursor += loopRate;
                if (loopCursor >= loopPeriod) loopCursor = std::fmod(loopCursor, static_cast<double>(loopPeriod));
            }
            ++stopSample;
            if ((s & 31) == 0)
                transportProgress.store(loopPlaying
                    ? static_cast<float>(loopCursor / loopPeriod)
                    : 1.0f, std::memory_order_relaxed);
        }
        else if ((s & 31) == 0)
            transportProgress.store(0.0f, std::memory_order_relaxed);

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
