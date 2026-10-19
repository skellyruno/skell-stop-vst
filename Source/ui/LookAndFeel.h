#pragma once
#include "Theme.h"

class StopDialLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StopDialLookAndFeel();
    void drawPopupMenuBackground(juce::Graphics&, int, int) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&,
        bool isSeparator, bool isActive, bool isHighlighted,
        bool isTicked, bool hasSubMenu, const juce::String& text,
        const juce::String& shortcutKeyText, const juce::Drawable* icon,
        const juce::Colour* textColour) override;
};
