# spreekbuis — Dutch as an instrument (Tongval's words: lesson loop, word cloud, vowel chart, g, Orca bridge)
set(SB_SOURCES Source/Bank.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${SPREEK_COMMON_SOURCES})
set(SB_DEFS ${SPREEK_DEFS})

# the course items (Dutch + English gloss) the built-in voice speaks when no voice bank has been rendered
juce_add_binary_data(SbData NAMESPACE SbData HEADER_NAME SbData.h SOURCES data/items.json)

juce_add_plugin(Spreekbuis
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.spreekbuis"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Sprk
    FORMATS ${SPREEK_FORMATS}
    PRODUCT_NAME "Spreekbuis"
    DESCRIPTION "Dutch as an instrument - learn Tongval's words inside your music"
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio input (optional) is shaped by the vowel pad."
    COPY_PLUGIN_AFTER_BUILD ${SPREEK_COPY_AFTER_BUILD})
target_sources(Spreekbuis PRIVATE ${SB_SOURCES})
target_include_directories(Spreekbuis PRIVATE Source ${SPREEK_COMMON_DIR})
target_compile_definitions(Spreekbuis PUBLIC ${SB_DEFS})
target_link_libraries(Spreekbuis PRIVATE SpraakData SbData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags ${SPREEK_LTO_FLAGS} juce::juce_recommended_warning_flags)

if(SPREEK_BUILD_TESTS)
    # offline render test of every layer (no plug-in wrapper)
    juce_add_console_app(SBTest PRODUCT_NAME "SBTest")
    target_sources(SBTest PRIVATE tests/SBTest.cpp ${SB_SOURCES})
    target_include_directories(SBTest PRIVATE Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SBTest PRIVATE ${SB_DEFS} JucePlugin_Name="SBTest" JucePlugin_Build_Standalone=0
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0)
    target_link_libraries(SBTest PRIVATE SpraakData SbData juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
