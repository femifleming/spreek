# spraakmaker — speech computer instrument (English + Dutch), after the Phonemer by moddict
set(SM_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp ${SPREEK_COMMON_SOURCES})
set(SM_DEFS ${SPREEK_DEFS})

juce_add_plugin(Spraakmaker
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "femifleming"
    PLUGIN_MANUFACTURER_CODE Ffle
    PLUGIN_CODE Sprk
    FORMATS ${SPREEK_FORMATS}
    PRODUCT_NAME "Spraakmaker"
    DESCRIPTION "Speech computer instrument: formant synthesis, English and Dutch"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    BUNDLE_ID com.femifleming.spraakmaker
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    COPY_PLUGIN_AFTER_BUILD ${SPREEK_COPY_AFTER_BUILD})
target_sources(Spraakmaker PRIVATE ${SM_SOURCES})
target_include_directories(Spraakmaker PRIVATE Source ${SPREEK_COMMON_DIR})
target_compile_definitions(Spraakmaker PUBLIC ${SM_DEFS})
target_link_libraries(Spraakmaker PRIVATE SpraakData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags ${SPREEK_LTO_FLAGS} juce::juce_recommended_warning_flags)

if(SPREEK_BUILD_TESTS)
    # headless render test (no plug-in wrapper): engine + G2P only
    juce_add_console_app(SpraakTest PRODUCT_NAME "SpraakTest")
    target_sources(SpraakTest PRIVATE tests/SpraakTest.cpp ${SPREEK_COMMON_SOURCES})
    target_include_directories(SpraakTest PRIVATE Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SpraakTest PRIVATE ${SM_DEFS} SPRAAK_TEST=1)
    target_link_libraries(SpraakTest PRIVATE SpraakData juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)

    # full plug-in test (processor + editor through the shared code): presets + UI shots
    juce_add_console_app(SpraakShot PRODUCT_NAME "SpraakShot")   # console app: links on Windows (no WinMain), still draws the editor
    target_sources(SpraakShot PRIVATE tests/SpraakShot.cpp ${SM_SOURCES})
    target_include_directories(SpraakShot PRIVATE Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SpraakShot PRIVATE ${SM_DEFS} JucePlugin_Name="Spraakmaker" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SpraakShot PRIVATE SpraakData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # every feature, one by one (trigger modes, sources, engines, tunings, fx, modulation, trg out, presets, state)
    juce_add_console_app(SpraakFull PRODUCT_NAME "SpraakFull")
    target_sources(SpraakFull PRIVATE tests/SpraakFull.cpp ${SM_SOURCES})
    target_include_directories(SpraakFull PRIVATE Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SpraakFull PRIVATE ${SM_DEFS} JucePlugin_Name="Spraakmaker" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SpraakFull PRIVATE SpraakData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
