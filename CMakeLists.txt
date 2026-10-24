cmake_minimum_required(VERSION 3.22)

# Windows: link the Visual C++ runtime statically so the plugin loads on PCs
# that don't have the "Microsoft Visual C++ Redistributable" installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Knockbox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KNOCKBOX_BUILD_TESTS "Build the offline DSP tests" ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# The VST3 manifest step runs a helper program on the build machine, so it is skipped when
# cross-compiling (only used for test builds; the GitLab build is native and keeps it).
if (CMAKE_CROSSCOMPILING)
    set(KNOCKBOX_VST3_MANIFEST FALSE)
else()
    set(KNOCKBOX_VST3_MANIFEST TRUE)
endif()

option(KNOCKBOX_BUILD_SNAPSHOTS "Build the UI screenshot tools" OFF)

set(KNOCKBOX_COMMON
    Source/common/KnockUI.cpp)

set(KNOCKBOX_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# ---- Knockbox: master bus -------------------------------------------------
juce_add_plugin(Knockbox
    COMPANY_NAME                "8cee"
    BUNDLE_ID                   "com.8cee.knockbox"
    PLUGIN_MANUFACTURER_CODE    Cee8
    PLUGIN_CODE                 Knbx
    FORMATS                     VST3
    PRODUCT_NAME                "Knockbox"
    DESCRIPTION                 "90s NY boom bap master bus"
    VST3_CATEGORIES             Fx Dynamics Mastering
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST          ${KNOCKBOX_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Knockbox PRIVATE
    Source/master/PluginProcessor.cpp
    Source/master/PluginEditor.cpp
    ${KNOCKBOX_COMMON})

# ---- Knockbox Track: channel strip -----------------------------------------
juce_add_plugin(KnockboxTrack
    COMPANY_NAME                "8cee"
    BUNDLE_ID                   "com.8cee.knockboxtrack"
    PLUGIN_MANUFACTURER_CODE    Cee8
    PLUGIN_CODE                 Knbt
    FORMATS                     VST3
    PRODUCT_NAME                "Knockbox Track"
    DESCRIPTION                 "90s NY boom bap channel strip"
    VST3_CATEGORIES             Fx EQ
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST          ${KNOCKBOX_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(KnockboxTrack PRIVATE
    Source/track/TrackProcessor.cpp
    Source/track/TrackEditor.cpp
    ${KNOCKBOX_COMMON})

foreach(t Knockbox KnockboxTrack)
    target_include_directories(${t} PRIVATE Source)
    target_compile_definitions(${t} PUBLIC ${KNOCKBOX_DEFS})
    target_link_libraries(${t}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endforeach()

if (KNOCKBOX_BUILD_TESTS)
    enable_testing()
    add_executable(KnockboxEngineTests tests/engine_test.cpp)
    add_test(NAME engine COMMAND KnockboxEngineTests ${CMAKE_BINARY_DIR})
    add_executable(KnockboxTrackTests tests/track_test.cpp)
    add_test(NAME track COMMAND KnockboxTrackTests)
    add_executable(KnockboxClickTests tests/switch_test.cpp)
    add_test(NAME clicks COMMAND KnockboxClickTests)

    # Loads the built .vst3 like a DAW and exercises it
    juce_add_console_app(KnockboxHostTest PRODUCT_NAME "KnockboxHostTest")
    target_sources(KnockboxHostTest PRIVATE tests/host/HostTest.cpp)
    target_compile_definitions(KnockboxHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KnockboxHostTest PRIVATE
        juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags)
    # export our operator new so the loaded plugin uses it (allocation counter, Linux only)
    target_link_options(KnockboxHostTest PRIVATE $<$<PLATFORM_ID:Linux>:-rdynamic>)

endif()

if (KNOCKBOX_BUILD_TESTS AND KNOCKBOX_BUILD_SNAPSHOTS)
    # Render each editor to a PNG (for checking the UI)
    juce_add_console_app(KnockboxSnapshot PRODUCT_NAME "KnockboxSnapshot")
    target_sources(KnockboxSnapshot PRIVATE
        tests/host/Snapshot.cpp Source/master/PluginProcessor.cpp Source/master/PluginEditor.cpp ${KNOCKBOX_COMMON})

    juce_add_console_app(KnockboxTrackSnapshot PRODUCT_NAME "KnockboxTrackSnapshot")
    target_sources(KnockboxTrackSnapshot PRIVATE
        tests/host/Snapshot.cpp Source/track/TrackProcessor.cpp Source/track/TrackEditor.cpp ${KNOCKBOX_COMMON})
    target_compile_definitions(KnockboxTrackSnapshot PRIVATE SNAPSHOT_TRACK=1)

    foreach(t KnockboxSnapshot KnockboxTrackSnapshot)
        target_include_directories(${t} PRIVATE Source)
        target_compile_definitions(${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${t} PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
    endforeach()
endif()
