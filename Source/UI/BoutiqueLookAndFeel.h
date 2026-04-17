#pragma once

#include <JuceHeader.h>

class BoutiqueLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BoutiqueLookAndFeel();
    ~BoutiqueLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};
