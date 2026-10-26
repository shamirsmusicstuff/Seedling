cmake_minimum_required(VERSION 3.22)

project(Seedling VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# ---- JUCE -----------------------------------------------------------------------------
# Uses ./JUCE if you cloned/copied it next to this file, otherwise downloads JUCE 8.0.4.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(juce)
endif()

# Standalone app is handy for testing without a DAW; it needs the audio-device module (ALSA on Linux).
if(WIN32 OR APPLE)
    option(SEED_BUILD_STANDALONE "Also build the Standalone app" ON)
else()
    option(SEED_BUILD_STANDALONE "Also build the Standalone app" OFF)
endif()

set(SEED_FORMATS VST3)
if(SEED_BUILD_STANDALONE)
    list(APPEND SEED_FORMATS Standalone)
endif()
if(APPLE)
    list(APPEND SEED_FORMATS AU)
endif()

juce_add_plugin(Seedling
    COMPANY_NAME "Seedling Audio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE SdSy
    PLUGIN_CODE Sdl1
    FORMATS ${SEED_FORMATS}
    PRODUCT_NAME "Seedling"
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Seedling PRIVATE
    Source/Engine.cpp
    Source/Engine.h
    Source/Params.h
    Source/Skins.h
    Source/DuskPlate.h
    Source/Randomizer.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_include_directories(Seedling PRIVATE Source)

# Roboto (SIL Open Font License, see Source/fonts/OFL.txt) is compiled into the plug-in.
juce_add_binary_data(SeedFonts NAMESPACE SeedFontData HEADER_NAME SeedFontData.h SOURCES
    Source/fonts/Roboto-Light.ttf Source/fonts/Roboto-Regular.ttf Source/fonts/Roboto-Medium.ttf Source/fonts/Roboto-Bold.ttf)

target_compile_definitions(Seedling PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Keep Randomize bit-identical on every platform (no fused multiply-add), so a seed grows the same patch everywhere.
if(NOT MSVC)
    target_compile_options(Seedling PRIVATE -ffp-contract=off)
endif()

# Only for building the VST3 on a minimal Linux box that lacks the optional X11 extension libraries.
option(SEED_MINIMAL_LINUX "Disable optional X11 extensions (Xrandr/Xinerama/Xcursor)" OFF)
if(SEED_MINIMAL_LINUX)
    target_compile_definitions(Seedling PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0 JUCE_USE_XSHM=0)
endif()

target_link_libraries(Seedling
    PRIVATE
        juce::juce_audio_processors
        SeedFonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless engine test (no JUCE):  cmake --build . --target seed_engine_test && ctest
add_executable(seed_engine_test EXCLUDE_FROM_ALL Tests/engine_test.cpp Source/Engine.cpp)
target_include_directories(seed_engine_test PRIVATE Source)
add_executable(seed_compat_test EXCLUDE_FROM_ALL Tests/seed_compat_test.cpp)
target_include_directories(seed_compat_test PRIVATE Source)
if(NOT MSVC)
    target_compile_options(seed_compat_test PRIVATE -ffp-contract=off)
endif()
enable_testing()
add_test(NAME seed_compat COMMAND seed_compat_test)
add_executable(seed_reverb_test EXCLUDE_FROM_ALL Tests/reverb_test.cpp)
target_include_directories(seed_reverb_test PRIVATE Source)
add_test(NAME seed_reverb COMMAND seed_reverb_test)
add_test(NAME engine COMMAND seed_engine_test)

# Offscreen UI snapshot + processor test (renders the 3 pages to PNG):  cmake --build . --target seedling_snapshot
juce_add_console_app(seedling_snapshot PRODUCT_NAME "seedling_snapshot")
set_target_properties(seedling_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(seedling_snapshot PRIVATE Tests/snapshot_test.cpp Source/Engine.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(seedling_snapshot PRIVATE Source)
target_compile_definitions(seedling_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
if(SEED_MINIMAL_LINUX)
    target_compile_definitions(seedling_snapshot PRIVATE JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0 JUCE_USE_XSHM=0)
endif()
target_link_libraries(seedling_snapshot PRIVATE juce::juce_audio_processors SeedFonts juce::juce_recommended_config_flags)
