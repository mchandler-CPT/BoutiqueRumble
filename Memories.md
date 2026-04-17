# Boutique Rumble: Architectural Memory

## Project Ethos
- **Paradigm**: Boutique (Curated constraints over parameter density).
- **Core Aesthetic**: Rhythmic "Rumble" bass (Skrillex/Fred Again style).
- **Primary Interface**: The "Boutique 5" Macros.

## Technical Decisions Log
- **[2026-04-17] Build System**: Migrated to CMake 4.3 + JUCE 8. Established modular `Source/` subfolder structure.
- **[2026-04-17] VST3 Compatibility**: Set `JUCE_VST3_CAN_REPLACE_VST2=0` to bypass legacy automation errors.
- **[2026-04-17] Anti-Aliasing**: Decided on a hybrid approach—Poly-BLEP for Oscillators, Oversampling (2x/4x) for the GRIT/Saturation block.
- **[2026-04-17] Parameter IDs**: Centralized macro parameter IDs in `Source/Parameters/ParamConstants.h` and refactored `PluginProcessor` APVTS layout to consume constants instead of hardcoded strings.
- **[2026-04-17] Testing Infrastructure**: Added `BoutiqueTests` executable via CMake `FetchContent` with Catch2 v3 and validated setup with a dummy arithmetic test.

## The "Ant" Preferences
- Focus on "Separation" and "Vibe" over clinical phase perfection.
- Support for "Sub-Drift" to create independent movement between the sub and mids.

## Current Technical Debt / Next Steps
- Wire `Oscillator` class into `RumbleEngine`.