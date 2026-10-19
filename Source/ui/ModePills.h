#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

class ModePills final : public juce::Component
{
public:
    explicit ModePills(StopDialAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void showLoopMenu();
    void setMode(int);
    void setPattern(int);
    StopDialAudioProcessor& processor;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, loopAttachment, patternAttachment;
    int modeIndex = 2, loopIndex = 2, patternIndex = 15;
    int hovered = -1;
    static constexpr float baseW = 605.0f, baseH = 122.0f;
};
