cmake_minimum_required(VERSION 3.24)

# Product metadata has one source of truth, including wrapper versions.
project(Ironstack VERSION 0.5.0 LANGUAGES C CXX)
set(IRONSTACK_PRODUCT_NAME "Ironstack")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

# FETCHCONTENT_SOURCE_DIR_JUCE supports a prepopulated, offline source tree.
# This digest was measured from the official archive of the exact commit.
FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/zip/3fb5cbc0340758596dac39f7c8de47d562b4896e
    URL_HASH SHA256=47722c9e64f5af35b99c6d5f9605a2213e41858ff6ef1f5b9a9747b1fd310de3
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(shared/dsp-core)
add_subdirectory(src/dsp)

add_library(ironstack_core INTERFACE)
target_link_libraries(ironstack_core INTERFACE ironstack_dsp)
add_subdirectory(tools/render_tool)
add_executable(benchmark_tool tools/benchmark_tool/main.cpp)
target_link_libraries(benchmark_tool PRIVATE ironstack_core juce::juce_audio_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(benchmark_tool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

juce_add_plugin(Ironstack
    COMPANY_NAME "Hugo Cacciatori"
    BUNDLE_ID "com.hugocacciatori.ironstack"
    PLUGIN_MANUFACTURER_CODE Hcct
    PLUGIN_CODE Irsk
    PRODUCT_NAME "${IRONSTACK_PRODUCT_NAME}"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

if(WIN32)
    # JUCE's resource generator depends on icons, but not its version-info input.
    # Without this dependency an incremental version bump leaves stale Explorer
    # file/product versions even though the plugin itself reports the new version.
    get_target_property(resource_info Ironstack JUCE_INFO_FILE)
    get_target_property(resource_directory Ironstack JUCE_GENERATED_SOURCES_DIRECTORY)
    add_custom_command(OUTPUT "${resource_directory}/Ironstack_resources.rc"
        APPEND DEPENDS "${resource_info}")
endif()

target_sources(Ironstack PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/MidiInput.h
    src/plugin/Parameters.h
    src/ui/PluginEditor.cpp
    src/ui/PluginEditor.h
    src/ui/LayerTab.h)
target_compile_definitions(Ironstack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_WINDOWS_MIDI_SERVICES=0
    JUCE_ASIO=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_LAME_AUDIO_FORMAT=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Ironstack
    PRIVATE ironstack_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Keep JUCE's normalised parameter round-trips consistent with MSVC.
    # ARM contraction of (2/3 * 18 - 12) otherwise changes default 0 dB.
    target_compile_options(Ironstack PUBLIC -ffp-contract=off)
endif()

# Bind package metadata to this configuration, rather than whichever checkout
# happens to be present later when the packager is run.
find_package(Git QUIET)
set(IRONSTACK_CONFIGURED_COMMIT "unknown")
set(IRONSTACK_CONFIGURED_DIRTY true)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_result)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE source_changes RESULT_VARIABLE status_result)
    if(revision_result EQUAL 0 AND status_result EQUAL 0)
        set(IRONSTACK_CONFIGURED_COMMIT "${revision}")
        if(source_changes STREQUAL "")
            set(IRONSTACK_CONFIGURED_DIRTY false)
        endif()
    endif()
endif()

# Render evidence identifies the same configured source as package metadata.
target_compile_definitions(render_tool PRIVATE
    IRONSTACK_COMMIT="${IRONSTACK_CONFIGURED_COMMIT}"
    IRONSTACK_DIRTY=${IRONSTACK_CONFIGURED_DIRTY})
# Packaging consumes JUCE's actual bundle paths, including .app on macOS.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/render-tool-$<CONFIG>.path" CONTENT "$<TARGET_FILE:render_tool>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/package-info-$<CONFIG>.json" CONTENT
"{\n  \"product\": \"${IRONSTACK_PRODUCT_NAME}\",\n  \"version\": \"${PROJECT_VERSION}\",\n  \"system\": \"${CMAKE_SYSTEM_NAME}\",\n  \"processor\": \"${CMAKE_SYSTEM_PROCESSOR}\",\n  \"configuredCommit\": \"${IRONSTACK_CONFIGURED_COMMIT}\",\n  \"configuredDirty\": ${IRONSTACK_CONFIGURED_DIRTY},\n  \"standalone\": \"$<TARGET_GENEX_EVAL:Ironstack_Standalone,$<TARGET_PROPERTY:Ironstack_Standalone,JUCE_PLUGIN_ARTEFACT_FILE>>\",\n  \"vst3\": \"$<TARGET_GENEX_EVAL:Ironstack_VST3,$<TARGET_PROPERTY:Ironstack_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\"\n}\n")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
