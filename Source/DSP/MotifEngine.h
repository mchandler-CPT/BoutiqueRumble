#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <random>

#include <juce_core/juce_core.h>

class MotifEngine {
public:
    std::array<bool, 16> pattern {};
    uint8_t stepIndex { 0 };
    bool bypass { true };
    int lockedMidiNote { -1 };
    float lockedSkipValue { -1.0f };
    int64_t lastBarIndex { 0 };
    bool haveBarAnchor { false };
    bool currentStepSkipped { false };

    static uint32_t seedFromNoteAndSkip(int midiNoteNumber, float skipValue) noexcept
    {
        return static_cast<uint32_t>(midiNoteNumber)
            + static_cast<uint32_t>(skipValue * 100000.0f);
    }

    void captureBarAnchor(double ppqPosition, bool hasHostPpq) noexcept
    {
        if (hasHostPpq && std::isfinite(ppqPosition))
        {
            lastBarIndex = static_cast<int64_t>(std::floor(ppqPosition / 4.0));
            haveBarAnchor = true;
        }
        else
        {
            haveBarAnchor = false;
        }
    }

    void prepareState() noexcept
    {
        currentStepSkipped = false;
        pattern.fill(true);
        bypass = true;
        stepIndex = 0;
        lastBarIndex = 0;
        haveBarAnchor = false;
        lockedMidiNote = -1;
        lockedSkipValue = -1.0f;
    }

    void rebuild(int midiNoteNumber, float skipValue, double ppqPosition, bool hasHostPpq) noexcept
    {
        lockedMidiNote = midiNoteNumber;
        lockedSkipValue = skipValue;

        if (skipValue <= 0.0f)
        {
            pattern.fill(true);
            bypass = true;
            stepIndex = 0;
            currentStepSkipped = false;
            captureBarAnchor(ppqPosition, hasHostPpq);
            return;
        }

        bypass = false;
        const uint32_t seed = seedFromNoteAndSkip(midiNoteNumber, skipValue);
        std::mt19937 gen(seed);
        const double hitProbability = juce::jlimit(0.0, 1.0, 1.0 - static_cast<double>(skipValue));
        std::bernoulli_distribution hit(hitProbability);

        for (bool& step : pattern)
        {
            step = hit(gen);
        }

        stepIndex = 0;
        captureBarAnchor(ppqPosition, hasHostPpq);
        currentStepSkipped = !pattern[0];
    }

    void advanceOnPulseWrap(double ppqPosition, bool hasHostPpq) noexcept
    {
        if (bypass)
        {
            currentStepSkipped = false;
            return;
        }

        if (hasHostPpq && std::isfinite(ppqPosition) && haveBarAnchor)
        {
            const int64_t currentBar = static_cast<int64_t>(std::floor(ppqPosition / 4.0));

            if (currentBar != lastBarIndex)
            {
                lastBarIndex = currentBar;
                stepIndex = 0;
            }
            else
            {
                stepIndex = static_cast<uint8_t>((static_cast<int>(stepIndex) + 1) % 16);
            }
        }
        else
        {
            stepIndex = static_cast<uint8_t>((static_cast<int>(stepIndex) + 1) % 16);
        }

        currentStepSkipped = !pattern[static_cast<size_t>(stepIndex & 15)];
    }
};
