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
- **[2026-04-17] Functional Signal Path**: Transitioned from silent stubs to a working core path—Poly-BLEP morphing oscillator (sine/square/saw), 3-voice `RumbleEngine` (1 sub + 2 mids), and processor integration mapping `SHAPE`/`HARMONY` APVTS macros into live audio output.
- **[2026-04-17] Transition to Test-Driven DSP Development**: Replaced placeholder checks with waveform-accurate oscillator tests (shape boundaries at 0.0 and 0.5) and engine-level summing/prepare propagation tests, making DSP behavior verifiable by default before feature expansion.
- **[2026-04-17] Monophonic MIDI Playability with Deterministic Phase Reset**: Added note-gated oscillator activation, `noteOn`/`noteOff` management in `RumbleEngine`, processor MIDI parsing (host + on-screen keyboard), deterministic phase reset on note start, and initial `GRIT` tanh soft-clipping with make-up normalization plus safe default master gain staging.
- **[2026-04-17] Code-driven to GUI-driven Control**: Added a custom `BoutiqueLookAndFeel`, introduced five rotary APVTS-bound macro knobs (`PULSE`, `SHAPE`, `GRIT`, `GIRTH`, `HARMONY`), and laid out dashboard controls above the integrated MIDI keyboard for direct performance/UI workflow.
- **[2026-04-17] Rhythmic Gating + Slew-based Click Reduction**: Implemented host-synced 16th-note gating in `RumbleEngine` using transport BPM/PPQ, mapped `PULSE` to both gate duty cycle and slew softness, and added a dedicated `SlewLimiter` utility + verification test to ensure non-instant transitions.
- **[2026-04-17] Internal Standalone Clock Fallback**: Added processor-level internal PPQ/BPM fallback when host transport is absent or stopped, continuously driving `RumbleEngine` gate sync in Standalone mode; also added an optional standalone-only `PULSE` influence (+/-10%) over internal tempo for playful rhythmic variation.
- **[2026-04-17] Linkwitz-Riley Crossover + GIRTH Spatial Management**: Added a 150 Hz 24 dB/oct Linkwitz-Riley split (LP/HP per output channel), enforced mono low-band integrity, and applied GIRTH-driven mid/side widening to the high band before recombining for stable low-end with controllable stereo width.

## The "Ant" Preferences
- Focus on "Separation" and "Vibe" over clinical phase perfection.
- Support for "Sub-Drift" to create independent movement between the sub and mids.

## Current Technical Debt / Next Steps
- Improve monophonic MIDI handling to include legato/last-note-priority behavior instead of immediate global note-off.