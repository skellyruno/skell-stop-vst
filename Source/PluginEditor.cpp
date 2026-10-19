#include "PluginEditor.h"

namespace
{
    void drawStopwatch(juce::Graphics& g, juce::Point<float> centre,
                       float radius, juce::Colour col)
    {
        const auto p = Theme::arc(centre, radius, -1.29f, 1.30f);
        g.setColour(col);
        g.strokePath(p, juce::PathStrokeType(4.2f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path hand;
        hand.startNewSubPath(centre.x, centre.y);
        hand.lineTo(centre.x + radius * 0.52f, centre.y - radius * 0.58f);
        g.strokePath(hand, juce::PathStrokeType(3.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(centre.x - 5.0f, centre.y - radius - 13.0f,
                               10.0f, 6.0f, 2.0f);
        g.fillEllipse(centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
}

void StopDialAudioProcessorEditor::Backdrop::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / Theme::designW,
                                                 getHeight() / Theme::designH));
    g.fillAll(Theme::background);
    juce::Path headerPath;
    headerPath.startNewSubPath(0, 0);
    headerPath.lineTo(725, 0);
    headerPath.lineTo(725, 76);
    headerPath.cubicTo(548, 115, 196, 115, 0, 77);
    headerPath.closeSubPath();
    g.setColour(Theme::header);
    g.fillPath(headerPath);
    g.setColour(Theme::footer);
    g.fillRect(0, 850, 725, 42);

    // Hamburger: all vector strokes, no stock brand symbols.
    g.setColour(Theme::text.withAlpha(0.89f));
    for (int i = 0; i < 3; ++i)
        g.drawLine(28.0f, 29.0f + i * 10.5f,
                   i == 1 ? 51.0f : 53.0f, 29.0f + i * 10.5f, 1.8f);

    // Original StopDial emblem and two-tone wordmark.
    drawStopwatch(g, { 240.0f, 54.0f }, 24.0f, Theme::green);
    g.setColour(Theme::green);
    g.setFont(Theme::font(47.0f, true));
    g.drawText("Stop", 278, 17, 120, 64, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff83dd53));
    g.setFont(Theme::font(47.0f));
    g.drawText("Dial", 391, 17, 115, 64, juce::Justification::centredLeft);

    // Invented four-orbit company mark, rather than a third-party wordmark.
    g.setColour(Theme::text.withAlpha(0.8f));
    for (int i = 0; i < 3; ++i)
    {
        const float xx = 629.0f + 9.0f * i;
        g.drawEllipse(xx, 21.0f + 3.5f * i, 12.0f, 12.0f, 1.7f);
    }
    g.setColour(Theme::muted);
    g.setFont(Theme::font(13.2f));
    g.drawText("skell audio", 591, 45, 120, 27, juce::Justification::centred);
}

StopDialAudioProcessorEditor::StopDialAudioProcessorEditor(StopDialAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), dial(p),
      curveButton(p, RoundIconButton::Kind::curve),
      stopButton(p, RoundIconButton::Kind::stop), modes(p)
{
    setLookAndFeel(&look);
    addAndMakeVisible(backdrop);
    addAndMakeVisible(dial);
    addAndMakeVisible(curveButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(modes);
    boundsConstrainer.setSizeLimits(435, 535, 1087, 1338);
    boundsConstrainer.setFixedAspectRatio(static_cast<double>(Theme::designW) /
                                          static_cast<double>(Theme::designH));
    setConstrainer(&boundsConstrainer);
    setResizable(true, true);
    setSize(725, 892);
}
StopDialAudioProcessorEditor::~StopDialAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    setConstrainer(nullptr);
}
void StopDialAudioProcessorEditor::paint(juce::Graphics&) {}
void StopDialAudioProcessorEditor::resized()
{
    const float sx = static_cast<float>(getWidth()) / Theme::designW;
    const float sy = static_cast<float>(getHeight()) / Theme::designH;
    auto place = [sx, sy](juce::Component& component, float x, float y, float w, float h)
    {
        component.setBounds(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };
    backdrop.setBounds(getLocalBounds());
    place(dial, 82, 125, 560, 560);
    place(curveButton, 25, 581, 120, 145);
    place(stopButton, 579, 581, 120, 145);
    place(modes, 74, 738, 605, 122);
    backdrop.toBack();
}
void StopDialAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    const float x = e.position.x * Theme::designW / std::max(1, getWidth());
    const float y = e.position.y * Theme::designH / std::max(1, getHeight());
    if (x < 75.0f && y < 80.0f) popupMainMenu();
}
void StopDialAudioProcessorEditor::popupMainMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader("STOPDIAL");
    m.addItem(1, "Reset controls");
    m.addSeparator();
    m.addItem(2, "About StopDial 1.0");
    juce::Component::SafePointer<StopDialAudioProcessorEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe](int result)
        {
            if (safe == nullptr) return;
            if (result == 1)
            {
                auto& p = safe->processor.apvts;
                for (const auto& id : { "stopAmount", "curve", "syncDiv", "mode", "loopDiv",
                                       "loopPattern", "lock", "lockAmount", "stopTrigger" })
                {
                    if (auto* param = p.getParameter(id))
                    {
                        param->beginChangeGesture();
                        param->setValueNotifyingHost(param->getDefaultValue());
                        param->endChangeGesture();
                    }
                }
                safe->processor.setLoopMarkers(0.25f, 0.75f);
            }
            if (result == 2)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "StopDial", "StopDial 1.0 — original JUCE tape-stop effect.\n"
                                "Design and DSP by Skell Audio.", "OK", safe.getComponent());
        });
}
