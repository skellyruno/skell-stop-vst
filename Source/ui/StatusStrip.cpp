#include "StatusStrip.h"
#include <cmath>

StatusStrip::StatusStrip(StopDialAudioProcessor& p) : processor(p)
{
    resumeAttachment = std::make_unique<juce::ParameterAttachment>(
        *p.apvts.getParameter("resumeTrigger"), [](float) {}, nullptr);
    resumeAttachment->sendInitialUpdate();
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setTooltip("Resume live playback after stopping, holding or looping");
    displayedState = processor.displayState.load(std::memory_order_relaxed);
    displayedBpm = processor.detectedBpm.load(std::memory_order_relaxed);
    startTimerHz(20);
}

void StatusStrip::timerCallback()
{
    const auto state = processor.displayState.load(std::memory_order_relaxed);
    const auto bpm = processor.detectedBpm.load(std::memory_order_relaxed);
    if (state != displayedState || std::abs(bpm - displayedBpm) > 0.1f)
    {
        displayedState = state;
        displayedBpm = bpm;
        repaint();
    }
}

void StatusStrip::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / width,
                                                getHeight() / height));
    auto body = juce::Rectangle<float>(0.8f, 0.8f, width - 1.6f, height - 1.6f);
    g.setColour(juce::Colour(0xff1c1e20));
    g.fillRoundedRectangle(body, 17.5f);
    g.setColour(juce::Colour(0xff585c60).withAlpha(0.30f));
    g.drawRoundedRectangle(body, 17.5f, 1.0f);

    const bool active = displayedState != StopDialAudioProcessor::live;
    const char* stateName = "LIVE INPUT";
    if (displayedState == StopDialAudioProcessor::braking) stateName = "BRAKING";
    if (displayedState == StopDialAudioProcessor::stopped) stateName = "STOPPED";
    if (displayedState == StopDialAudioProcessor::holding) stateName = "SPEED HOLD";
    if (displayedState == StopDialAudioProcessor::looping) stateName = "LOOPING";

    g.setColour(active ? Theme::orange.withAlpha(0.19f) : Theme::green.withAlpha(0.15f));
    g.fillEllipse(13.0f, 11.0f, 16.0f, 16.0f);
    g.setColour(active ? Theme::orange : Theme::green);
    g.fillEllipse(18.0f, 16.0f, 6.0f, 6.0f);
    g.setFont(Theme::font(13.7f, true));
    g.setColour(Theme::text);
    g.drawText(stateName, 38.0f, 7.0f, 123.0f, 24.0f, juce::Justification::centredLeft);

    g.setColour(Theme::muted.withAlpha(0.22f));
    g.fillRect(165.0f, 9.0f, 1.0f, 19.0f);
    g.setFont(Theme::font(13.0f, true));
    g.setColour(Theme::muted);
    g.drawText(juce::String(juce::roundToInt(displayedBpm)) + " BPM",
               172.0f, 7.0f, 81.0f, 24.0f, juce::Justification::centred);

    const juce::Rectangle<float> control(267.0f, 4.0f, 118.0f, 29.0f);
    g.setColour(overResume ? Theme::green.withAlpha(0.22f)
                           : Theme::green.withAlpha(active ? 0.12f : 0.05f));
    g.fillRoundedRectangle(control, 14.0f);
    g.setColour(Theme::green.withAlpha(overResume || active ? 0.8f : 0.35f));
    g.drawRoundedRectangle(control, 14.0f, 1.0f);

    juce::Path replay;
    replay.addCentredArc(282.0f, 18.3f, 6.0f, 6.0f, 0.0f, -1.9f, 2.0f, true);
    g.strokePath(replay, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    juce::Path arrow;
    arrow.addTriangle(287.0f, 9.0f, 286.0f, 16.0f, 280.0f, 12.0f);
    g.fillPath(arrow);
    g.setFont(Theme::font(12.9f, true));
    g.setColour(active || overResume ? Theme::green : Theme::text);
    g.drawText(active ? "RESUME" : "READY", 298.0f, 8.0f, 77.0f, 21.0f,
               juce::Justification::centred);
}

void StatusStrip::mouseDown(const juce::MouseEvent& e)
{
    const float bx = e.position.x * width / std::max(1, getWidth());
    if (bx < 262.0f) return;
    processor.requestResumeFromUI(); // Works even for <1-block button taps.
    resumeAttachment->beginGesture();
    resumeAttachment->setValueAsPartOfGesture(1.0f);
    resumeAttachment->setValueAsPartOfGesture(0.0f);
    resumeAttachment->endGesture();
}

void StatusStrip::mouseMove(const juce::MouseEvent& e)
{
    const bool now = e.position.x * width / std::max(1, getWidth()) >= 262.0f;
    if (now != overResume)
    {
        overResume = now;
        setMouseCursor(now ? juce::MouseCursor::PointingHandCursor
                           : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StatusStrip::mouseExit(const juce::MouseEvent&)
{
    overResume = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}
