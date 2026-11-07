#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include <array>

class WaveformView final : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView(StopDialAudioProcessor&);
    void paint(juce::Graphics&) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    float pixelToFraction(float x) const noexcept;
    StopDialAudioProcessor& processor;
    static constexpr size_t columns = 188;
    std::array<WavePeak, columns> snapshot {};
    size_t writeColumn = 0;
    bool liveInput = false;
    float playheadPosition = 0.0f;
    float displayedMarkerStart = 0.25f, displayedMarkerEnd = 0.75f;
    int displayedTransportState = StopDialAudioProcessor::live;
    int hoverHandle = -1;
    int selectedMarker = -1;
    float baseW = 380.0f, baseH = 300.0f;
};
