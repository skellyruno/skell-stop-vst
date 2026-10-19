#include "LookAndFeel.h"
StopDialLookAndFeel::StopDialLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff27282b));
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::greenDark.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}
void StopDialLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(juce::Colour(0xff28292c));
    g.setColour(Theme::green.withAlpha(0.3f));
    g.drawRect(0, 0, w, h, 1);
}
void StopDialLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& bounds, bool separator, bool active,
    bool highlighted, bool ticked, bool, const juce::String& name,
    const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (separator)
    {
        g.setColour(Theme::muted.withAlpha(0.22f));
        g.drawLine(static_cast<float>(bounds.getX() + 10),
                   static_cast<float>(bounds.getCentreY()),
                   static_cast<float>(bounds.getRight() - 10),
                   static_cast<float>(bounds.getCentreY()));
        return;
    }
    if (highlighted && active)
    {
        g.setColour(Theme::green.withAlpha(0.16f));
        g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f), 5.0f);
    }
    g.setColour(active ? Theme::text : Theme::muted.withAlpha(0.5f));
    g.setFont(Theme::font(14.0f));
    g.drawText((ticked ? juce::String::fromUTF8("✓  ") : "   ") + name,
        bounds.reduced(6), juce::Justification::centredLeft);
    if (shortcut.isNotEmpty())
    {
        g.setColour(Theme::muted);
        g.drawText(shortcut, bounds.reduced(12), juce::Justification::centredRight);
    }
}
