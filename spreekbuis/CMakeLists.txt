cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architecture")
project(Spreekbuis VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "$ENV{HOME}/disting-ex-plugins/JUCE" CACHE PATH "JUCE checkout (9.x)")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

set(SB_SOURCES Source/Bank.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(SB_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

juce_add_plugin(Spreekbuis
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.spreekbuis"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Sprk
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Spreekbuis"
    DESCRIPTION "Dutch as an instrument - learn Tongval's words inside your music"
    ICON_BIG Resources/icon.png
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio input (optional) is shaped by the vowel pad."
    COPY_PLUGIN_AFTER_BUILD TRUE)
target_sources(Spreekbuis PRIVATE ${SB_SOURCES})
target_include_directories(Spreekbuis PRIVATE Source)
target_compile_definitions(Spreekbuis PUBLIC ${SB_DEFS})
target_link_libraries(Spreekbuis PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# offline render test
juce_add_console_app(SBTest PRODUCT_NAME "SBTest")
target_sources(SBTest PRIVATE tests/SBTest.cpp ${SB_SOURCES})
target_include_directories(SBTest PRIVATE Source)
target_compile_definitions(SBTest PRIVATE ${SB_DEFS} JucePlugin_Name="SBTest" JucePlugin_Build_Standalone=0
    JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0)
target_link_libraries(SBTest PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
