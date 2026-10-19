#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include <array>

class WaveformView final : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView(StopDialAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    float pixelToFraction(float x) const noexcept;
    StopDialAudioProcessor& processor;
    static constexpr size_t columns = 188;
    std::array<WavePeak, columns> snapshot {};
    size_t writeColumn = 0;
    bool liveInput = false;
    int selectedMarker = -1;
    float baseW = 380.0f, baseH = 300.0f;
};
