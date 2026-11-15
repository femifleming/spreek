cmake_minimum_required(VERSION 3.22)
# spreek — two Dutch speech instruments: spreekbuis (Tongval's words as an instrument) and spraakmaker
# (a formant speech computer, after the Phonemer by moddict).
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target spreek_all SBTest SpraakTest SpraakFull SpreekHostCheck
# macOS universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
project(spreek VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SPREEK_COPY_AFTER_BUILD "Install the plug-ins into the user plug-in folders after building" OFF)
option(SPREEK_LTO "Link-time optimisation for the plug-ins (slow links; CI turns it off)" ON)
option(SPREEK_BUILD_TESTS "Build the offline test tools" ON)

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
set(SPREEK_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0 _USE_MATH_DEFINES=1)
if(SPREEK_LTO)
    set(SPREEK_LTO_FLAGS juce::juce_recommended_lto_flags)
else()
    set(SPREEK_LTO_FLAGS "")
endif()

# ---- shared speech code: text -> phonemes (CMUdict English, rule-based Dutch), prosody, Klatt formant voice
set(SPREEK_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(SPREEK_COMMON_SOURCES
    ${SPREEK_COMMON_DIR}/G2P.cpp ${SPREEK_COMMON_DIR}/Phonemes.cpp ${SPREEK_COMMON_DIR}/Planner.cpp
    ${SPREEK_COMMON_DIR}/Voice.cpp ${SPREEK_COMMON_DIR}/Speak.cpp)
juce_add_binary_data(SpraakData SOURCES
    common/data/en_dict.txt common/data/nl_words.txt common/data/nl_corpus.txt common/data/en_corpus.txt)

add_subdirectory(spreekbuis)
add_subdirectory(spraakmaker)
add_custom_target(spreek_all DEPENDS Spreekbuis_All Spraakmaker_All)

if(SPREEK_BUILD_TESTS)
    # loads the BUILT plug-ins like a DAW and checks that they make sound (mac: VST3 + AU)
    juce_add_console_app(SpreekHostCheck PRODUCT_NAME "SpreekHostCheck")
    target_sources(SpreekHostCheck PRIVATE tests/HostCheck.cpp)
    target_compile_definitions(SpreekHostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(APPLE)
        target_link_libraries(SpreekHostCheck PRIVATE "-framework CoreAudioKit")
    endif()
    target_link_libraries(SpreekHostCheck PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
