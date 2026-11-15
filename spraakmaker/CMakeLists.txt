cmake_minimum_required(VERSION 3.22)
project(Spraakmaker VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "")
set(JUCE_DIR "$ENV{HOME}/disting-ex-plugins/JUCE" CACHE PATH "JUCE source")
add_subdirectory(${JUCE_DIR} juce)

juce_add_binary_data(SpraakData SOURCES
    data/en_dict.txt data/nl_words.txt data/nl_corpus.txt data/en_corpus.txt)

set(SM_SOURCES
    Source/Phonemes.cpp Source/G2P.cpp Source/Planner.cpp Source/Voice.cpp
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)

juce_add_plugin(Spraakmaker
    COMPANY_NAME "femifleming"
    PLUGIN_MANUFACTURER_CODE Ffle
    PLUGIN_CODE Sprk
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Spraakmaker"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    BUNDLE_ID com.femifleming.spraakmaker
    ICON_BIG ${CMAKE_SOURCE_DIR}/Resources/icon.png
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Spraakmaker PRIVATE ${SM_SOURCES})
target_compile_definitions(Spraakmaker PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Spraakmaker PRIVATE SpraakData
    juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# headless render test (no plug-in wrapper): engine + G2P only
juce_add_console_app(SpraakTest PRODUCT_NAME "SpraakTest")
target_sources(SpraakTest PRIVATE tests/SpraakTest.cpp Source/Phonemes.cpp Source/G2P.cpp Source/Planner.cpp Source/Voice.cpp)
target_compile_definitions(SpraakTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SPRAAK_TEST=1)
target_link_libraries(SpraakTest PRIVATE SpraakData juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)

# full plug-in test (processor + editor through the shared code)
juce_add_gui_app(SpraakShot PRODUCT_NAME "SpraakShot")
target_sources(SpraakShot PRIVATE tests/SpraakShot.cpp ${SM_SOURCES})
target_compile_definitions(SpraakShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Spraakmaker" JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpraakShot PRIVATE SpraakData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

# every feature, one by one (trigger modes, sources, engines, tunings, fx, modulation, trg out, presets, state)
juce_add_gui_app(SpraakFull PRODUCT_NAME "SpraakFull")
target_sources(SpraakFull PRIVATE tests/SpraakFull.cpp ${SM_SOURCES})
target_compile_definitions(SpraakFull PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Spraakmaker" JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpraakFull PRIVATE SpraakData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
