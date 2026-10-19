#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/LookAndFeel.h"
#include "ui/DialComponent.h"
#include "ui/RoundIconButton.h"
#include "ui/ModePills.h"

class StopDialAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StopDialAudioProcessorEditor(StopDialAudioProcessor&);
    ~StopDialAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    class Backdrop final : public juce::Component
    {
    public:
        Backdrop() { setBufferedToImage(true); setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics&) override;
    } backdrop;
    void popupMainMenu();
    StopDialAudioProcessor& processor;
    StopDialLookAndFeel look;
    DialComponent dial;
    RoundIconButton curveButton, stopButton;
    ModePills modes;
    juce::ComponentBoundsConstrainer boundsConstrainer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StopDialAudioProcessorEditor)
};
