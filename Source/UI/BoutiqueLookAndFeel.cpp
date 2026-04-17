#include "BoutiqueLookAndFeel.h"

BoutiqueLookAndFeel::BoutiqueLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff101014));
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffc87f2f));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2f3b));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xfff5d37a));
    setColour(juce::Label::textColourId, juce::Colour(0xffd5d9e2));
    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd3d7df));
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff161a22));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xffc87f2f));
}

void BoutiqueLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           float sliderPosProportional,
                                           float rotaryStartAngle,
                                           float rotaryEndAngle,
                                           juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                               static_cast<float>(y),
                                               static_cast<float>(width),
                                               static_cast<float>(height)).reduced(6.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = juce::jmap(sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

    g.setColour(findColour(juce::Slider::rotarySliderOutlineColourId));
    g.fillEllipse(bounds);

    juce::Path track;
    track.addCentredArc(centre.x,
                        centre.y,
                        radius - 5.0f,
                        radius - 5.0f,
                        0.0f,
                        rotaryStartAngle,
                        rotaryEndAngle,
                        true);
    g.setColour(findColour(juce::Slider::rotarySliderOutlineColourId).brighter(0.2f));
    g.strokePath(track, juce::PathStrokeType(2.0f));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x,
                           centre.y,
                           radius - 5.0f,
                           radius - 5.0f,
                           0.0f,
                           rotaryStartAngle,
                           angle,
                           true);
    g.setColour(findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(valueArc, juce::PathStrokeType(3.5f));

    juce::Path pointer;
    pointer.addRectangle(-1.8f, -radius + 9.0f, 3.6f, radius * 0.48f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(findColour(juce::Slider::thumbColourId));
    g.fillPath(pointer);

    g.setColour(findColour(juce::Slider::rotarySliderOutlineColourId).brighter(0.4f));
    g.drawEllipse(bounds, 1.0f);
}
