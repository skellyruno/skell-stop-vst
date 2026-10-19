#include "ModePills.h"

ModePills::ModePills(StopDialAudioProcessor& p) : processor(p)
{
    auto make = [this](const char* id, auto cb)
    {
        return std::make_unique<juce::ParameterAttachment>(
            *processor.apvts.getParameter(id), cb, nullptr);
    };
    modeAttachment = make("mode", [this](float x) { modeIndex = juce::roundToInt(x); repaint(); });
    loopAttachment = make("loopDiv", [this](float x) { loopIndex = juce::roundToInt(x); repaint(); });
    patternAttachment = make("loopPattern", [this](float x) { patternIndex = juce::roundToInt(x); repaint(); });
    modeAttachment->sendInitialUpdate();
    loopAttachment->sendInitialUpdate();
    patternAttachment->sendInitialUpdate();
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ModePills::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / baseW, getHeight() / baseH));
    const char* labels[] { "CLASSIC", "MODERN", "LOOP" };
    for (int i = 0; i < 3; ++i)
    {
        const float x = 0.0f + i * 204.0f;
        const bool active = modeIndex == i;
        g.setColour(active ? juce::Colour(0xff343839)
                           : Theme::pill.brighter(hovered == i ? 0.15f : 0.0f));
        g.fillRoundedRectangle(x, 0.0f, 166.0f, 44.0f, 23.0f);
        if (active)
        {
            g.setColour(Theme::green);
            g.drawRoundedRectangle(x + 0.8f, 0.8f, 164.4f, 42.4f, 22.0f, 1.9f);
        }
        g.setColour(active ? Theme::green : Theme::text);
        g.setFont(Theme::font(23.5f));
        g.drawText(labels[i], juce::Rectangle<float>(x + 5.0f, 6.0f, 156.0f, 34.0f),
                   juce::Justification::centred);
    }
    if (modeIndex == 2)
    {
        g.setColour(Theme::muted);
        g.setFont(Theme::font(25.0f));
        g.drawText(StopDialAudioProcessor::loopNames()[juce::jlimit(0, 8, loopIndex)],
                   433.0f, 57.0f, 89.0f, 35.0f, juce::Justification::centred);
        const int heights[4] { 24, 22, 26, 25 };
        for (int i = 0; i < 4; ++i)
        {
            const bool on = (patternIndex >> i) & 1;
            const float barX = 527.0f + i * 8.0f;
            g.setColour(Theme::green.withAlpha(on ? 1.0f : 0.28f));
            g.fillRoundedRectangle(barX, 66.0f + (26.0f - heights[i]) * 0.5f,
                                    3.1f, static_cast<float>(heights[i]), 1.0f);
        }
    }
}
void ModePills::setMode(int i)
{
    modeAttachment->beginGesture();
    modeAttachment->setValueAsPartOfGesture(static_cast<float>(juce::jlimit(0, 2, i)));
    modeAttachment->endGesture();
}
void ModePills::setPattern(int i)
{
    patternAttachment->beginGesture();
    patternAttachment->setValueAsPartOfGesture(static_cast<float>(i & 15));
    patternAttachment->endGesture();
}
void ModePills::showLoopMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < StopDialAudioProcessor::loopNames().size(); ++i)
        menu.addItem(i + 1, StopDialAudioProcessor::loopNames()[i], true, i == loopIndex);
    juce::Component::SafePointer<ModePills> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe](int result)
        {
            if (safe != nullptr && result > 0)
            {
                safe->loopAttachment->beginGesture();
                safe->loopAttachment->setValueAsPartOfGesture(static_cast<float>(result - 1));
                safe->loopAttachment->endGesture();
            }
        });
}
void ModePills::mouseDown(const juce::MouseEvent& e)
{
    const float x = e.position.x * baseW / std::max(1, getWidth());
    const float y = e.position.y * baseH / std::max(1, getHeight());
    if (y < 48.0f)
    {
        for (int i = 0; i < 3; ++i)
            if (x >= 204.0f * i && x <= 204.0f * i + 166.0f) { setMode(i); break; }
    }
    else if (modeIndex == 2 && y >= 51.0f && y < 99.0f)
    {
        if (x > 518.0f) setPattern(patternIndex + 1);
        else if (x > 427.0f) showLoopMenu();
    }
}
void ModePills::mouseMove(const juce::MouseEvent& e)
{
    const float x = e.position.x * baseW / std::max(1, getWidth());
    const float y = e.position.y * baseH / std::max(1, getHeight());
    const int selected = y < 48.0f ? juce::jlimit(-1, 2, static_cast<int>(x / 204.0f)) : -1;
    if (selected != hovered) { hovered = selected; repaint(); }
}
void ModePills::mouseExit(const juce::MouseEvent&) { hovered = -1; repaint(); }
void ModePills::mouseWheelMove(const juce::MouseEvent& e,
                              const juce::MouseWheelDetails& w)
{
    const float x = e.position.x * baseW / std::max(1, getWidth());
    const float y = e.position.y * baseH / std::max(1, getHeight());
    if (modeIndex == 2 && y > 50.0f && x > 420.0f)
    {
        auto& attachment = x > 520.0f ? *patternAttachment : *loopAttachment;
        const int value = x > 520.0f ? patternIndex : loopIndex;
        const int limit = x > 520.0f ? 15 : 8;
        attachment.beginGesture();
        attachment.setValueAsPartOfGesture(static_cast<float>(juce::jlimit(0, limit,
            value + (w.deltaY > 0.0f ? 1 : -1))));
        attachment.endGesture();
    }
}
