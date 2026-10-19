#include "DialComponent.h"
#include <cmath>

namespace
{
    constexpr float sz = 560.0f;
    const juce::Point<float> c { 280.0f, 280.0f };
    constexpr float arcFrom = -0.78f;
    constexpr float arcTo = 2.34f;

    void drawPadlock(juce::Graphics& g, juce::Point<float> pos, float scale,
                     juce::Colour col)
    {
        juce::Path body, shackle;
        body.addRoundedRectangle(0.0f, 13.0f, 23.0f, 18.0f, 3.0f);
        shackle.startNewSubPath(4.5f, 14.0f);
        shackle.lineTo(4.5f, 9.0f);
        shackle.cubicTo(4.5f, -2.0f, 18.5f, -2.0f, 18.5f, 9.0f);
        shackle.lineTo(18.5f, 14.0f);
        const auto t = juce::AffineTransform::scale(scale).translated(pos.x, pos.y);
        g.setColour(col);
        g.fillPath(body, t);
        g.strokePath(shackle, juce::PathStrokeType(3.4f * scale), t);
    }
}

DialComponent::BezelLayer::BezelLayer()
{
    setBufferedToImage(true);
    setInterceptsMouseClicks(false, false);
}

void DialComponent::BezelLayer::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / sz, getHeight() / sz));

    // Soft multi-pass shadow and bevel, all drawn from vector geometry.
    for (int i = 17; i > 0; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.004f + 0.009f * (17 - i) / 17.0f));
        const float r = 251.0f + i * 0.72f;
        g.fillEllipse(c.x - r + 3.0f, c.y - r + 11.0f, 2.0f * r, 2.0f * r);
    }
    g.setColour(juce::Colour(0xff17181b));
    g.fillEllipse(25.0f, 26.0f, 510.0f, 510.0f);
    juce::ColourGradient bevel(juce::Colour(0xff43454b), 110.0f, 80.0f,
                               juce::Colour(0xff222327), 485.0f, 502.0f, false);
    bevel.addColour(0.55, juce::Colour(0xff313238));
    g.setGradientFill(bevel);
    g.fillEllipse(31.0f, 34.0f, 498.0f, 498.0f);
    g.setColour(juce::Colour(0xff1e1f23));
    g.fillEllipse(57.0f, 57.0f, 446.0f, 446.0f);
    juce::ColourGradient innerRing(juce::Colour(0xff37383c), 90, 90,
                                  juce::Colour(0xff131417), 475, 476, false);
    g.setGradientFill(innerRing);
    g.fillEllipse(71.0f, 69.0f, 418.0f, 418.0f);

    // Radial scale: 96 minor ticks, one brighter major top tick.
    for (int i = 0; i < 96; ++i)
    {
        const float angle = Theme::tau * static_cast<float>(i) / 96.0f;
        const bool major = (i % 8 == 0);
        const float r0 = major ? 208.0f : 211.0f;
        const float r1 = major ? 219.0f : 216.0f;
        const auto p0 = Theme::radial(c, r0, angle);
        const auto p1 = Theme::radial(c, r1, angle);
        g.setColour(juce::Colour(0xff929397).withAlpha(
            i == 0 ? 0.53f : major ? 0.21f : 0.085f));
        g.drawLine(p0.x, p0.y, p1.x, p1.y, major ? 1.6f : 0.85f);
    }

    // Four engraved interruptions in the gunmetal ring.
    g.setColour(juce::Colour(0xff18191b));
    for (float angle : { 0.0f, Theme::pi * 0.5f, Theme::pi,
                         Theme::pi * 1.5f })
    {
        const auto p = Theme::radial(c, 242.0f, angle);
        const auto tangent = juce::Point<float> { std::cos(angle), std::sin(angle) };
        g.drawLine(p.x - tangent.x * 16.0f, p.y - tangent.y * 16.0f,
                   p.x + tangent.x * 16.0f, p.y + tangent.y * 16.0f, 9.5f);
    }

    // Inner disc and subtle radial-like vignette using concentric fills.
    g.setColour(juce::Colour(0xff141417));
    g.fillEllipse(91.0f, 90.0f, 378.0f, 378.0f);
    juce::ColourGradient face(juce::Colour(0xff252528), 280, 208,
                              juce::Colour(0xff141418), 280, 463, true);
    face.addColour(0.65, juce::Colour(0xff202024));
    g.setGradientFill(face);
    g.fillEllipse(93.0f, 92.0f, 374.0f, 374.0f);
    g.setColour(juce::Colours::black.withAlpha(0.22f));
    g.drawEllipse(92.0f, 91.0f, 376.0f, 376.0f, 2.2f);
}

DialComponent::DialComponent(StopDialAudioProcessor& p)
    : overlay(*this), processor(p), waveform(p)
{
    addAndMakeVisible(bezel);
    addAndMakeVisible(waveform);
    addAndMakeVisible(overlay);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    auto make = [this](const char* id, auto callback)
    {
        return std::make_unique<juce::ParameterAttachment>(
            *processor.apvts.getParameter(id), callback, nullptr);
    };
    amountAttachment = make("stopAmount", [this](float v) { amountValue = v; overlay.repaint(); });
    lockAttachment = make("lock", [this](float v) { isLocked = v > 0.5f; overlay.repaint(); });
    lockAmountAttachment = make("lockAmount", [this](float v) { lockValue = v; overlay.repaint(); });
    amountAttachment->sendInitialUpdate();
    lockAttachment->sendInitialUpdate();
    lockAmountAttachment->sendInitialUpdate();
    startTimerHz(60);
}

void DialComponent::resized()
{
    bezel.setBounds(getLocalBounds());
    overlay.setBounds(getLocalBounds());
    const auto sx = getWidth() / designSize, sy = getHeight() / designSize;
    waveform.setBounds(juce::Rectangle<int>(
        juce::roundToInt(90.0f * sx), juce::roundToInt(130.0f * sy),
        juce::roundToInt(380.0f * sx), juce::roundToInt(300.0f * sy)));
    overlay.toFront(false);
}
void DialComponent::paint(juce::Graphics&) {}

void DialComponent::OverlayLayer::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / sz, getHeight() / sz));
    const float amt = juce::jlimit(0.0f, 1.0f, dial.amountValue / 100.0f);
    if (amt > 0.002f)
    {
        const auto path = Theme::arc(c, 249.0f, arcFrom, arcFrom + (arcTo - arcFrom) * amt);
        // Cheap vector glow: broad transparent strokes under the solid arc.
        for (int i = 15; i >= 1; i -= 3)
        {
            g.setColour(Theme::green.withAlpha(0.014f + (15 - i) * 0.0018f));
            g.strokePath(path, juce::PathStrokeType(20.0f + i * 1.9f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::ColourGradient green(juce::Colour(0xff85e257), 330, 115,
                                   Theme::greenDark, 460, 550, false);
        g.setGradientFill(green);
        g.strokePath(path, juce::PathStrokeType(20.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // White playhead triangle at start of the green sweep.
    const auto anchor = Theme::radial(c, 249.0f, arcFrom);
    juce::Path pointer;
    pointer.addTriangle(anchor.x - 15.0f, anchor.y - 10.0f,
                        anchor.x + 11.0f, anchor.y - 2.0f,
                        anchor.x - 8.0f, anchor.y + 15.0f);
    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillPath(pointer, juce::AffineTransform::translation(1.8f, 2.3f));
    juce::ColourGradient metal(juce::Colour(0xfff0f3ef), anchor.x - 10, anchor.y - 10,
                               juce::Colour(0xff757c78), anchor.x + 11, anchor.y + 14, false);
    g.setGradientFill(metal);
    g.fillPath(pointer);
    g.setColour(Theme::greenDark.withAlpha(0.7f));
    g.strokePath(pointer, juce::PathStrokeType(1.2f));

    // Orange transport progress with continuously updated fraction.
    const float progress = juce::jlimit(0.0f, 1.0f, dial.phase);
    const auto orangePath = Theme::arc(c, 201.5f, 1.56f, 1.56f + 1.28f * progress);
    g.setColour(Theme::orange.withAlpha(0.19f));
    g.strokePath(Theme::arc(c, 201.5f, 1.56f, 2.84f),
        juce::PathStrokeType(11.0f, juce::PathStrokeType::curved,
                             juce::PathStrokeType::rounded));
    if (progress > 0.001f)
    {
        g.setColour(Theme::orange);
        g.strokePath(orangePath,
            juce::PathStrokeType(11.0f, juce::PathStrokeType::curved,
                                 juce::PathStrokeType::rounded));
    }

    // Dark lower crescent, lock glyph and large numeric readout.
    juce::Path moon;
    moon.startNewSubPath(134.0f, 406.0f);
    moon.cubicTo(193.0f, 374.0f, 362.0f, 373.0f, 426.0f, 409.0f);
    moon.lineTo(423.0f, 459.0f);
    moon.cubicTo(358.0f, 489.0f, 210.0f, 492.0f, 141.0f, 461.0f);
    moon.closeSubPath();
    g.setColour(juce::Colour(0xff101114).withAlpha(0.92f));
    g.fillPath(moon);
    drawPadlock(g, { 214.0f, 412.0f }, 0.92f,
                dial.isLocked ? Theme::green : Theme::text);
    g.setColour(Theme::text);
    g.setFont(Theme::font(29.0f));
    g.drawText(juce::String(juce::roundToInt(dial.lockValue)) + "%",
        260, 405, 112, 48, juce::Justification::centred);
    if (dial.hovering)
    {
        g.setColour(Theme::green.withAlpha(0.24f));
        g.drawEllipse(35.0f, 35.0f, 490.0f, 490.0f, 1.0f);
    }
}

void DialComponent::timerCallback()
{
    const float now = processor.transportProgress.load(std::memory_order_relaxed);
    if (std::abs(now - phase) > 0.002f)
    {
        phase = now;
        overlay.repaint();
    }
}

float DialComponent::toBaseX(float x) const { return x * designSize / std::max(1, getWidth()); }
float DialComponent::toBaseY(float y) const { return y * designSize / std::max(1, getHeight()); }
void DialComponent::setAmount(float v)
{
    if (amountAttachment) amountAttachment->setValueAsPartOfGesture(juce::jlimit(0.0f, 100.0f, v));
}
void DialComponent::setLockAmount(float v)
{
    if (lockAmountAttachment) lockAmountAttachment->setValueAsPartOfGesture(juce::jlimit(0.0f, 100.0f, v));
}
void DialComponent::mouseDown(const juce::MouseEvent& e)
{
    const float x = toBaseX(e.position.x), y = toBaseY(e.position.y);
    const float radius = std::hypot(x - c.x, y - c.y);
    if (y > 402.0f && y < 468.0f && x > 200.0f && x < 256.0f)
    {
        if (lockAttachment)
        {
            lockAttachment->beginGesture();
            lockAttachment->setValueAsPartOfGesture(isLocked ? 0.0f : 1.0f);
            lockAttachment->endGesture();
        }
        return;
    }
    if (y > 402.0f && y < 468.0f && x >= 256.0f && x < 380.0f)
    {
        dragging = Dragging::lockAmount;
        dragStartLock = lockValue;
        lockAmountAttachment->beginGesture();
        return;
    }
    if (radius > 215.0f && radius < 278.0f)
    {
        dragging = Dragging::amount;
        amountAttachment->beginGesture();
        mouseDrag(e);
    }
}
void DialComponent::mouseDrag(const juce::MouseEvent& e)
{
    const float x = toBaseX(e.position.x), y = toBaseY(e.position.y);
    if (dragging == Dragging::lockAmount)
    {
        const float delta = -e.getDistanceFromDragStartY() / std::max(1.0f, getHeight() / sz);
        setLockAmount(dragStartLock + delta * (Theme::fineAdjust(e.mods) ? 0.025f : 0.17f));
    }
    else if (dragging == Dragging::amount)
    {
        float a = std::atan2(x - c.x, c.y - y);
        if (a < arcFrom) a += Theme::tau;
        float v = 100.0f * (a - arcFrom) / (arcTo - arcFrom);
        if (Theme::fineAdjust(e.mods)) v = amountValue + (v - amountValue) * 0.12f;
        setAmount(juce::jlimit(0.0f, 100.0f, v));
    }
}
void DialComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragging == Dragging::amount) amountAttachment->endGesture();
    if (dragging == Dragging::lockAmount) lockAmountAttachment->endGesture();
    dragging = Dragging::none;
}
void DialComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const float x = toBaseX(e.position.x), y = toBaseY(e.position.y);
    if (std::hypot(x - c.x, y - c.y) > 211.0f)
    {
        amountAttachment->beginGesture();
        setAmount(100.0f);
        amountAttachment->endGesture();
    }
}
void DialComponent::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& w)
{
    amountAttachment->beginGesture();
    setAmount(amountValue + w.deltaY * (Theme::fineAdjust(e.mods) ? 1.0f : 8.0f));
    amountAttachment->endGesture();
}
void DialComponent::mouseMove(const juce::MouseEvent& e)
{
    const float x = toBaseX(e.position.x), y = toBaseY(e.position.y);
    const float r = std::hypot(x - c.x, y - c.y);
    const bool h = r > 213.0f && r < 268.0f;
    if (hovering != h) { hovering = h; overlay.repaint(); }
}
