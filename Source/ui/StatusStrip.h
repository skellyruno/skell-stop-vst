#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

// Compact transport-status readout. Pure JUCE vector graphics, no bitmaps.
// The button on its right returns the processor to normal live playback.
class StatusStrip final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit StatusStrip(StopDialAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    StopDialAudioProcessor& processor;
    std::unique_ptr<juce::ParameterAttachment> resumeAttachment;
    float displayedBpm = 120.0f;
    int displayedState = StopDialAudioProcessor::live;
    bool overResume = false;
    static constexpr float width = 390.0f, height = 37.0f;
};
