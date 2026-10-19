#pragma once
#include "../PluginProcessor.h"
#include "WaveformView.h"
#include "Theme.h"

class DialComponent final : public juce::Component, private juce::Timer
{
public:
    explicit DialComponent(StopDialAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    class BezelLayer final : public juce::Component
    {
    public:
        BezelLayer();
        void paint(juce::Graphics&) override;
    } bezel;
    class OverlayLayer final : public juce::Component
    {
    public:
        explicit OverlayLayer(DialComponent& owner) : dial(owner) { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics&) override;
    private:
        DialComponent& dial;
    } overlay;
    void timerCallback() override;
    void setAmount(float amount);
    void setLockAmount(float amount);
    float toBaseX(float x) const;
    float toBaseY(float y) const;
    enum class Dragging { none, amount, lockAmount } dragging = Dragging::none;
    StopDialAudioProcessor& processor;
    WaveformView waveform;
    std::unique_ptr<juce::ParameterAttachment> amountAttachment, lockAttachment, lockAmountAttachment;
    float amountValue = 100.0f, lockValue = 14.0f, dragStartLock = 14.0f;
    bool isLocked = false, hovering = false;
    float phase = 0.0f;
    static constexpr float designSize = 560.0f;
};
