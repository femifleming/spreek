cmake_minimum_required(VERSION 3.22)
# spreek — Spreekbuis: Dutch as an instrument, with a speech computer inside (one plug-in; until 1.0 it was two:
# Spreekbuis and Spraakmaker).
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target Spreekbuis_All SBTest SpraakTest SpraakFull SpreekTest SpreekHostCheck
# macOS universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
project(spreek VERSION 2.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SPREEK_COPY_AFTER_BUILD "Install the plug-in into the user plug-in folders after building" OFF)
option(SPREEK_LTO "Link-time optimisation for the plug-in (slow links; CI turns it off)" ON)
option(SPREEK_BUILD_TESTS "Build the offline test tools" ON)
option(SPREEK_BUILD_VST2 "Also build VST2 (FST headers)" ON)

# ---- JUCE 9.0.5: ./JUCE (symlink or checkout), then JUCE_DIR, then ~/disting-ex-plugins/JUCE, else fetched
set(JUCE_DIR "" CACHE PATH "JUCE checkout (9.x); empty = look in ./JUCE, ~/disting-ex-plugins/JUCE, or fetch 9.0.5")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(SPREEK_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
elseif(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    set(SPREEK_JUCE "${JUCE_DIR}")
elseif(EXISTS "$ENV{HOME}/disting-ex-plugins/JUCE/CMakeLists.txt")
    set(SPREEK_JUCE "$ENV{HOME}/disting-ex-plugins/JUCE")
endif()
if(SPREEK_JUCE)
    message(STATUS "JUCE: ${SPREEK_JUCE}")
    add_subdirectory("${SPREEK_JUCE}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    message(STATUS "JUCE: fetching 9.0.5 from GitHub")
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.5 GIT_SHALLOW TRUE GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(MSVC)
    add_compile_options(/utf-8)   # the sources contain UTF-8 text (IPA, ë, …)
endif()
if(APPLE)
    set(SPREEK_FORMATS AU VST3 Standalone)
else()
    set(SPREEK_FORMATS VST3 Standalone)
endif()
if(SPREEK_BUILD_VST2)
    juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2-fst")
    list(APPEND SPREEK_FORMATS VST)
endif()
set(SPREEK_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0 _USE_MATH_DEFINES=1)
if(SPREEK_LTO)
    set(SPREEK_LTO_FLAGS juce::juce_recommended_lto_flags)
else()
    set(SPREEK_LTO_FLAGS "")
endif()

# ---- the speech code: text -> phonemes (CMUdict English, rule-based Dutch), prosody, Klatt formant voice
set(SPREEK_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(SPREEK_COMMON_SOURCES
    ${SPREEK_COMMON_DIR}/G2P.cpp ${SPREEK_COMMON_DIR}/Phonemes.cpp ${SPREEK_COMMON_DIR}/Planner.cpp
    ${SPREEK_COMMON_DIR}/Voice.cpp ${SPREEK_COMMON_DIR}/Speak.cpp)
juce_add_binary_data(SpraakData SOURCES
    common/data/en_dict.txt common/data/nl_words.txt common/data/nl_corpus.txt common/data/en_corpus.txt)
# the course items (Dutch + English gloss) the built-in voice speaks when no voice bank has been rendered
juce_add_binary_data(SbData NAMESPACE SbData HEADER_NAME SbData.h SOURCES data/items.json)
# the type of the faltung design system (Inter, IBM Plex Mono; SIL Open Font Licence)
juce_add_binary_data(SbFonts NAMESPACE SbFonts HEADER_NAME SbFonts.h SOURCES
    Resources/fonts/Inter-Regular.ttf Resources/fonts/Inter-Medium.ttf
    Resources/fonts/IBMPlexMono-Regular.ttf Resources/fonts/IBMPlexMono-Medium.ttf)

set(SB_SOURCES Source/Bank.cpp Source/Processor.cpp Source/Ui.cpp Source/Art.cpp Source/Editor.cpp ${SPREEK_COMMON_SOURCES})
set(SB_LIBS SpraakData SbData SbFonts juce::juce_audio_utils juce::juce_dsp)

juce_add_plugin(Spreekbuis
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.spreekbuis"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Sprk
    FORMATS ${SPREEK_FORMATS}
    PRODUCT_NAME "Spreekbuis"
    DESCRIPTION "Dutch as an instrument, with a speech computer inside"
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    VST2_CATEGORY kPlugCategSynth
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio input (optional) is shaped by the vowel chart and the vocoder."
    COPY_PLUGIN_AFTER_BUILD ${SPREEK_COPY_AFTER_BUILD})
target_sources(Spreekbuis PRIVATE ${SB_SOURCES})
target_include_directories(Spreekbuis PRIVATE Source ${SPREEK_COMMON_DIR})
target_compile_definitions(Spreekbuis PUBLIC ${SPREEK_DEFS})
target_link_libraries(Spreekbuis PRIVATE ${SB_LIBS}
    PUBLIC juce::juce_recommended_config_flags ${SPREEK_LTO_FLAGS} juce::juce_recommended_warning_flags)

if(SPREEK_BUILD_TESTS)
    set(SB_TEST_DEFS ${SPREEK_DEFS} JucePlugin_Name="Spreekbuis" JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0
        JucePlugin_VersionString="${PROJECT_VERSION}")
    # the processor and the editor without a plug-in wrapper, once, for every test tool
    add_library(SbCore STATIC ${SB_SOURCES})
    target_include_directories(SbCore PUBLIC Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SbCore PUBLIC ${SB_TEST_DEFS})
    target_link_libraries(SbCore PUBLIC ${SB_LIBS} juce::juce_recommended_config_flags)

    function(spreek_test name)
        juce_add_console_app(${name} PRODUCT_NAME "${name}")   # console apps: they link on Windows (no WinMain) and still draw the editor
        target_sources(${name} PRIVATE tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE SbCore)
    endfunction()
    spreek_test(SBTest)        # the word bank's layers (every check of Spreekbuis 1.0's test)
    spreek_test(SpraakFull)    # the speech computer, feature by feature (every check of Spraakmaker 1.0's test)
    spreek_test(SpreekTest)    # what joins them, old sessions, state, defaults, stereo, the play log; --shots draws every page

    # text -> phonemes -> voice without the processor
    juce_add_console_app(SpraakTest PRODUCT_NAME "SpraakTest")
    target_sources(SpraakTest PRIVATE tests/SpraakTest.cpp ${SPREEK_COMMON_SOURCES})
    target_include_directories(SpraakTest PRIVATE Source ${SPREEK_COMMON_DIR})
    target_compile_definitions(SpraakTest PRIVATE ${SPREEK_DEFS} SPRAAK_TEST=1)
    target_link_libraries(SpraakTest PRIVATE SpraakData juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)

    # loads the BUILT plug-in like a DAW and checks that it makes sound, in stereo (VST3, VST2; mac: AU)
    juce_add_console_app(SpreekHostCheck PRODUCT_NAME "SpreekHostCheck")
    target_sources(SpreekHostCheck PRIVATE tests/HostCheck.cpp)
    target_compile_definitions(SpreekHostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(SPREEK_BUILD_VST2)
        target_compile_definitions(SpreekHostCheck PRIVATE JUCE_PLUGINHOST_VST=1)
    endif()
    if(APPLE)
        target_link_libraries(SpreekHostCheck PRIVATE "-framework CoreAudioKit")
    endif()
    target_link_libraries(SpreekHostCheck PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
