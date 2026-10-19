#include "RoundIconButton.h"
#include <cmath>

RoundIconButton::RoundIconButton(StopDialAudioProcessor& p, Kind type)
    : processor(p), kind(type)
{
    auto& tree = processor.apvts;
    if (kind == Kind::curve)
    {
        valueAttachment = std::make_unique<juce::ParameterAttachment>(
            *tree.getParameter("curve"),
            [this](float v) { curveValue = v; repaint(); }, nullptr);
        valueAttachment->sendInitialUpdate();
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    else
    {
        valueAttachment = std::make_unique<juce::ParameterAttachment>(
            *tree.getParameter("stopTrigger"),
            [this](float v) { down = v > 0.5f; repaint(); }, nullptr);
        syncAttachment = std::make_unique<juce::ParameterAttachment>(
            *tree.getParameter("syncDiv"),
            [this](float v) { syncIndex = juce::jlimit(0, 9, juce::roundToInt(v)); repaint(); }, nullptr);
        valueAttachment->sendInitialUpdate();
        syncAttachment->sendInitialUpdate();
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}
void RoundIconButton::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / baseW, getHeight() / baseH));
    g.setColour(Theme::pill.brighter(hovered ? 0.18f : 0.0f));
    g.fillEllipse(18.0f, 5.0f, 82.0f, 82.0f);
    if (kind == Kind::curve)
    {
        const float bend = juce::jmap(curveValue, -100.0f, 100.0f, 1.0f, 0.0f);
        juce::Path curve;
        curve.startNewSubPath(36.0f, 27.0f);
        curve.cubicTo(40.0f + 35.0f * bend, 42.0f + 34.0f * bend,
                      60.0f - 35.0f * bend, 54.0f + 24.0f * bend,
                      79.0f, 66.0f);
        g.setColour(Theme::green);
        g.strokePath(curve, juce::PathStrokeType(3.2f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffeeeeed));
        g.fillEllipse(33.0f, 58.0f, 12.0f, 12.0f);
        g.setColour(juce::Colour(0xff212326));
        g.drawEllipse(33.0f, 58.0f, 12.0f, 12.0f, 1.4f);
    }
    else
    {
        const auto col = down ? Theme::green : Theme::text;
        g.setColour(col);
        g.fillRoundedRectangle(47.0f, 32.0f, 24.0f, 24.0f, 3.5f);
    }
    g.setColour(Theme::muted.brighter(hovered ? 0.25f : 0.0f));
    g.setFont(Theme::font(25.0f));
    g.drawText(kind == Kind::curve ? juce::String(juce::roundToInt(curveValue))
                                 : StopDialAudioProcessor::syncNames()[syncIndex],
        0, 100, 120, 33, juce::Justification::centred);
}
void RoundIconButton::setValue(float v)
{
    if (kind == Kind::curve)
        valueAttachment->setValueAsPartOfGesture(juce::jlimit(-100.0f, 100.0f, v));
}
void RoundIconButton::showDivisionMenu()
{
    juce::PopupMenu menu;
    const auto& names = StopDialAudioProcessor::syncNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem(i + 1, names[i], true, i == syncIndex);
    juce::Component::SafePointer<RoundIconButton> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe](int result)
        {
            if (safe != nullptr && result > 0 && safe->syncAttachment)
            {
                safe->syncAttachment->beginGesture();
                safe->syncAttachment->setValueAsPartOfGesture(static_cast<float>(result - 1));
                safe->syncAttachment->endGesture();
            }
        });
}
void RoundIconButton::mouseDown(const juce::MouseEvent& e)
{
    const float y = e.position.y * baseH / std::max(1, getHeight());
    if (kind == Kind::stop)
    {
        if (y > 98.0f) { showDivisionMenu(); return; }
        processor.requestStopFromUI();
        valueAttachment->beginGesture();
        valueAttachment->setValueAsPartOfGesture(1.0f);
    }
    else
    {
        dragStartValue = curveValue;
        valueAttachment->beginGesture();
    }
}
void RoundIconButton::mouseDrag(const juce::MouseEvent& e)
{
    if (kind == Kind::curve)
    {
        const float move = e.getDistanceFromDragStartY() * baseH / std::max(1, getHeight());
        setValue(dragStartValue - move * (Theme::fineAdjust(e.mods) ? 0.1f : 0.75f));
    }
}
void RoundIconButton::mouseUp(const juce::MouseEvent& e)
{
    const float y = e.getMouseDownY() * baseH / std::max(1, getHeight());
    if (kind == Kind::stop)
    {
        if (y <= 98.0f)
        {
            valueAttachment->setValueAsPartOfGesture(0.0f);
            valueAttachment->endGesture();
        }
    }
    else valueAttachment->endGesture();
}
void RoundIconButton::mouseDoubleClick(const juce::MouseEvent&)
{
    if (kind != Kind::curve) return;
    valueAttachment->beginGesture();
    setValue(-54.0f);
    valueAttachment->endGesture();
}
void RoundIconButton::mouseWheelMove(const juce::MouseEvent& e,
                                     const juce::MouseWheelDetails& w)
{
    if (kind == Kind::curve)
    {
        valueAttachment->beginGesture();
        setValue(curveValue + w.deltaY * (Theme::fineAdjust(e.mods) ? 2.0f : 18.0f));
        valueAttachment->endGesture();
    }
}
void RoundIconButton::mouseEnter(const juce::MouseEvent&) { hovered = true; repaint(); }
void RoundIconButton::mouseExit(const juce::MouseEvent&) { hovered = false; repaint(); }
