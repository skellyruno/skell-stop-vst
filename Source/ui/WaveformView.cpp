#include "WaveformView.h"
#include <cmath>

WaveformView::WaveformView(StopDialAudioProcessor& p) : processor(p)
{
    setOpaque(false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    startTimerHz(60);
}

void WaveformView::timerCallback()
{
    WavePeak next;
    bool dirty = false;
    // Bounded drain avoids UI stalls if a host floods faster than paint.
    for (int i = 0; i < 600 && processor.peaks.pop(next); ++i)
    {
        snapshot[writeColumn] = next;
        writeColumn = (writeColumn + 1) % columns;
        liveInput = true;
        dirty = true;
    }
    if (dirty) repaint();
}

float WaveformView::pixelToFraction(float x) const noexcept
{
    return juce::jlimit(0.0f, 1.0f, (x - 12.0f) / (baseW - 24.0f));
}

void WaveformView::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / baseW, getHeight() / baseH));
    juce::Path clip;
    clip.addEllipse(baseW * 0.5f - 186.0f, baseH * 0.5f - 186.0f, 372.0f, 372.0f);
    g.reduceClipRegion(clip);
    constexpr float yMid = 144.0f;
    constexpr float range = 101.0f;
    const float start = 12.0f + (baseW - 24.0f) * processor.loopStart.load();
    const float end = 12.0f + (baseW - 24.0f) * processor.loopEnd.load();

    g.setColour(juce::Colour(0xff88898b).withAlpha(0.2f));
    for (float y : { yMid - range, yMid + range })
        for (int x = 10; x < static_cast<int>(baseW - 10); x += 9)
            g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(x),
                                 static_cast<float>(x + 4));

    // When no stream exists, a deliberately subdued design-time preview gives
    // the same silhouette without inventing measured audio readings.
    const float step = (baseW - 24.0f) / static_cast<float>(columns);
    for (size_t i = 0; i < columns; ++i)
    {
        float minVal, maxVal;
        if (liveInput)
        {
            const auto sample = snapshot[(writeColumn + i) % columns];
            minVal = sample.minimum;
            maxVal = sample.maximum;
        }
        else
        {
            const float t = static_cast<float>(i);
            const float envelope = 0.11f + 0.47f * std::abs(std::sin(t * 0.062f))
                                    * (0.5f + 0.5f * std::sin(t * 0.21f) * std::sin(t * 0.016f));
            const float a = envelope * (0.46f + 0.54f * std::abs(std::sin(t * 1.91f)));
            minVal = -a;
            maxVal = a;
        }
        const float x = 12.0f + static_cast<float>(i) * step;
        const float lo = juce::jlimit(-1.0f, 1.0f, minVal);
        const float hi = juce::jlimit(-1.0f, 1.0f, maxVal);
        // Mirror amplitude around the center line while preserving min/max peak data.
        const float mag = juce::jmin(1.0f, juce::jmax(std::abs(lo), std::abs(hi)));
        const float height = std::max(0.6f, mag * range);
        g.setColour(Theme::muted.withAlpha(liveInput ? 0.70f : 0.43f));
        g.drawLine(x, yMid - height, x, yMid + height, 1.7f);
    }

    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRect(0.0f, yMid - range, start, 2.0f * range);
    g.fillRect(end, yMid - range, baseW - end, 2.0f * range);
    g.setColour(Theme::green.withAlpha(0.16f));
    g.fillRect(start, yMid - range, end - start, 2.0f * range);

    for (float marker : { start, end })
    {
        g.setColour(Theme::green.withAlpha(0.68f));
        g.drawRoundedRectangle(marker - 4.0f, yMid - range, 8.0f, range * 2.0f,
                               5.0f, 1.5f);
        g.setColour(Theme::green.withAlpha(0.35f));
        g.drawVerticalLine(static_cast<int>(marker), yMid - range, yMid + range);
    }
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    const float x = e.position.x * baseW / std::max(1, getWidth());
    const float a = 12.0f + processor.loopStart.load() * (baseW - 24.0f);
    const float b = 12.0f + processor.loopEnd.load() * (baseW - 24.0f);
    setMouseCursor(std::min(std::abs(x - a), std::abs(x - b)) < 13.0f
        ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void WaveformView::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    const float x = e.position.x * baseW / std::max(1, getWidth());
    const float a = 12.0f + processor.loopStart.load() * (baseW - 24.0f);
    const float b = 12.0f + processor.loopEnd.load() * (baseW - 24.0f);
    const float da = std::abs(x - a), db = std::abs(x - b);
    selectedMarker = da < db ? 0 : 1;
    if (std::min(da, db) > 24.0f) selectedMarker = -1;
}
void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (selectedMarker < 0) return;
    const auto v = pixelToFraction(e.position.x * baseW / std::max(1, getWidth()));
    const float start = processor.loopStart.load(), end = processor.loopEnd.load();
    processor.setLoopMarkers(selectedMarker == 0 ? juce::jmin(v, end - 0.05f) : start,
                             selectedMarker == 1 ? juce::jmax(v, start + 0.05f) : end);
    repaint();
}
void WaveformView::mouseUp(const juce::MouseEvent&) { selectedMarker = -1; }
