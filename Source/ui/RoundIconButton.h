#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

class RoundIconButton final : public juce::Component
{
public:
    enum class Kind { curve, stop };
    RoundIconButton(StopDialAudioProcessor&, Kind);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
private:
    void setValue(float);
    void showDivisionMenu();
    StopDialAudioProcessor& processor;
    Kind kind;
    std::unique_ptr<juce::ParameterAttachment> valueAttachment, syncAttachment;
    float curveValue = -54.0f;
    int syncIndex = 2;
    float dragStartValue = 0.0f;
    bool hovered = false, down = false;
    static constexpr float baseW = 120.0f, baseH = 145.0f;
};
