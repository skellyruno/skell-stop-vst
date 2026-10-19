#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace Theme
{
    constexpr float designW = 725.0f;
    constexpr float designH = 892.0f;
    constexpr float dialCX = 362.0f;
    constexpr float dialCY = 405.0f;
    constexpr float dialOuterRadius = 249.0f;
    constexpr float dialFaceRadius = 192.0f;

    inline const juce::Colour background { 0xff262629 };
    inline const juce::Colour header     { 0xff2b2a2d };
    inline const juce::Colour footer     { 0xff202124 };
    inline const juce::Colour green      { 0xff6fd13a };
    inline const juce::Colour greenDark  { 0xff4fb52a };
    inline const juce::Colour face       { 0xff1b1b1e };
    inline const juce::Colour bezel      { 0xff303136 };
    inline const juce::Colour pill       { 0xff323236 };
    inline const juce::Colour text       { 0xffd0d0d2 };
    inline const juce::Colour muted      { 0xffa8a8ab };
    inline const juce::Colour orange     { 0xffd88b40 };

    // UI uses the host OS's system sans family; no copyrighted bundled font assets.
    inline juce::Font font(float pixels, bool bold = false)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font(juce::FontOptions(pixels)
            .withStyle(bold ? "Bold" : "Regular"));
       #else
        return juce::Font(pixels, bold ? juce::Font::bold : juce::Font::plain);
       #endif
    }

    inline juce::Path arc(juce::Point<float> centre, float radius,
                          float from, float to)
    {
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, radius, radius,
                        0.0f, from, to, true);
        return p;
    }
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float tau = juce::MathConstants<float>::twoPi;

    inline juce::Point<float> radial(juce::Point<float> centre, float radius, float angle)
    {
        return { centre.x + radius * std::sin(angle),
                 centre.y - radius * std::cos(angle) };
    }
    inline bool fineAdjust(const juce::ModifierKeys& mods) { return mods.isShiftDown(); }
}
