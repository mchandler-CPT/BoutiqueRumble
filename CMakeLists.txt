cmake_minimum_required(VERSION 3.22)

project(BoutiqueRumble VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)
include(CTest)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal Binary support" FORCE)
endif()

# 1. Point to your JUCE installation
# JUCE is expected as a repository submodule at ./Juce
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Juce" CACHE PATH "Path to JUCE")
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Check submodules!")
endif()
add_subdirectory("${JUCE_PATH}" JUCE)

set(RUMBLE_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RUMBLE_PLUGIN_FORMATS AU)
endif()

# 2. Define the Plugin
juce_add_plugin(BoutiqueRumble
    COMPANY_NAME "bdEnergy"
    MANUFACTURER_CODE "bdEn"
    PLUGIN_CODE "Rumb"
    PRODUCT_NAME "Rumble"
    PLUGIN_NAME "Rumble"               # Explicitly set the name strings
    PLUGIN_MANUFACTURER "bdEnergy"     # Explicitly set manufacturer string
    AU_MAIN_TYPE kAudioUnitType_MusicDevice # Hard-code to Synth type
    BUNDLE_ID "com.synth.rumble"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    DESCRIPTION "Rumble Synthesizer"
    FORMATS ${RUMBLE_PLUGIN_FORMATS}
)

juce_generate_juce_header(BoutiqueRumble)

juce_add_binary_data(RumbleAssets
    SOURCES
        logo.png
)

# 3. Add your Source files
target_sources(BoutiqueRumble PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/BrakePhysics.h
    Source/DSP/MotifEngine.h
    Source/DSP/Oscillator.h
    Source/DSP/RumbleEngine.h
    Source/DSP/SignalProcessor.h
    Source/DSP/VoiceBank.h
    Source/Parameters/ParamConstants.h
    Source/Utils/SlewLimiter.h
    Source/UI/BoutiqueLookAndFeel.cpp
    Source/UI/BoutiqueLookAndFeel.h
) 

target_include_directories(BoutiqueRumble PRIVATE Source)

# 4. Link the "Boutique" Ingredients
# These modules are essential for the 'Grit' and 'Girth' logic
target_link_libraries(BoutiqueRumble PRIVATE
    RumbleAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp           # Needed for TPT Filters and Saturation
    juce::juce_gui_basics)

    target_compile_definitions(BoutiqueRumble PUBLIC 
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

# 5. Professional Standards
target_compile_features(BoutiqueRumble PRIVATE cxx_std_20)

# 6. Test Infrastructure
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(BoutiqueTests
    Tests/OscillatorTests.cpp
    Tests/EngineTests.cpp
    Tests/SlewTests.cpp
    Tests/MotifEngineTests.cpp
    Tests/SignalProcessorTests.cpp
    Tests/BrakePhysicsTests.cpp
)

target_include_directories(BoutiqueTests PRIVATE Source)

target_link_libraries(BoutiqueTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
)

target_compile_features(BoutiqueTests PRIVATE cxx_std_20)

if (BUILD_TESTING)
    add_test(NAME BoutiqueTests COMMAND BoutiqueTests)
endif()