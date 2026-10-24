add_executable(ironstack_scaffold_tests ScaffoldTests.cpp)
target_link_libraries(ironstack_scaffold_tests PRIVATE ironstack_core)
add_test(NAME scaffold_silence COMMAND ironstack_scaffold_tests)

add_executable(ironstack_bass_notes_tests BassNotesTests.cpp)
target_link_libraries(ironstack_bass_notes_tests PRIVATE ironstack_core)
add_test(NAME bass_note_selection COMMAND ironstack_bass_notes_tests)

add_executable(ironstack_layer_mode_tests LayerModeTests.cpp)
target_link_libraries(ironstack_layer_mode_tests PRIVATE ironstack_core)
add_test(NAME layer_modes COMMAND ironstack_layer_mode_tests)

add_executable(ironstack_bass_audio_tests BassAudioTests.cpp)
target_link_libraries(ironstack_bass_audio_tests PRIVATE ironstack_core)
add_test(NAME bass_audio COMMAND ironstack_bass_audio_tests)

add_executable(ironstack_guitar_tests GuitarTests.cpp)
target_link_libraries(ironstack_guitar_tests PRIVATE ironstack_core)
add_test(NAME guitar_voices COMMAND ironstack_guitar_tests)

add_executable(ironstack_guitar_bus_tests GuitarBusTests.cpp)
target_link_libraries(ironstack_guitar_bus_tests PRIVATE ironstack_core)
add_test(NAME guitar_bus COMMAND ironstack_guitar_bus_tests)

add_executable(ironstack_guitar_spectrum_tests GuitarSpectrumTests.cpp)
target_include_directories(ironstack_guitar_spectrum_tests PRIVATE "${CMAKE_SOURCE_DIR}/shared/dsp-core/tests")
target_link_libraries(ironstack_guitar_spectrum_tests PRIVATE ironstack_core)
add_test(NAME guitar_spectrum COMMAND ironstack_guitar_spectrum_tests)

add_executable(ironstack_guitar_mix_tests GuitarMixTests.cpp)
target_include_directories(ironstack_guitar_mix_tests PRIVATE "${CMAKE_SOURCE_DIR}/shared/dsp-core/tests")
target_compile_definitions(ironstack_guitar_mix_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    IRONSTACK_COMMIT="${IRONSTACK_CONFIGURED_COMMIT}" IRONSTACK_DIRTY=${IRONSTACK_CONFIGURED_DIRTY})
target_link_libraries(ironstack_guitar_mix_tests PRIVATE ironstack_core juce::juce_core
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME guitar_mix COMMAND ironstack_guitar_mix_tests "${CMAKE_SOURCE_DIR}/docs/test-sequences/fixtures.json"
    "${CMAKE_BINARY_DIR}/guitar-mix.json")
# Seven moving unison components make this full 280-case render matrix slower
# on hosted macOS runners. Keep every case and numeric gate; allow it to finish.
set_tests_properties(guitar_mix PROPERTIES TIMEOUT 1200)

add_executable(ironstack_guitar_mono_tests GuitarMonoTests.cpp)
target_compile_definitions(ironstack_guitar_mono_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ironstack_guitar_mono_tests PRIVATE ironstack_core juce::juce_core
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME guitar_mono COMMAND ironstack_guitar_mono_tests "${CMAKE_SOURCE_DIR}/docs/test-sequences/fixtures.json")

add_executable(ironstack_routing_tests RoutingTests.cpp)
target_link_libraries(ironstack_routing_tests PRIVATE ironstack_core)
add_test(NAME routing_matrix COMMAND ironstack_routing_tests)

# Exercise the same raw MIDI decoder and sample-offset bridge as the plugin,
# without pulling any editor/GUI code into the test executable.
add_executable(ironstack_oscillator_tests OscillatorTests.cpp)
target_include_directories(ironstack_oscillator_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(ironstack_oscillator_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ironstack_oscillator_tests PRIVATE ironstack_dsp juce::juce_audio_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME oscillator_midi COMMAND ironstack_oscillator_tests)

# Link the actual processor/editor objects, reusing the compiled JUCE code.
add_executable(ironstack_processor_tests ProcessorTests.cpp)
target_include_directories(ironstack_processor_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${juce_SOURCE_DIR}/modules")
target_link_libraries(ironstack_processor_tests PRIVATE Ironstack ironstack_dsp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Reuse the existing system X11 dependency only in the GUI test fixture.
    find_package(X11 REQUIRED)
    target_sources(ironstack_processor_tests PRIVATE X11TestDisplay.cpp X11TestDisplay.h)
    target_link_libraries(ironstack_processor_tests PRIVATE X11::X11)
endif()
add_test(NAME processor_state_editor COMMAND ironstack_processor_tests "${CMAKE_BINARY_DIR}/m1.4-editor.png")
set_tests_properties(processor_state_editor PROPERTIES TIMEOUT 60)

# End-to-end file/CLI checks use only JUCE's non-GUI audio/core modules.
add_executable(ironstack_render_tests RenderTests.cpp)
target_compile_definitions(ironstack_render_tests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0 JUCE_USE_LAME_AUDIO_FORMAT=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
target_link_libraries(ironstack_render_tests PRIVATE ironstack_core juce::juce_audio_formats
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME renderer_offline COMMAND ironstack_render_tests "$<TARGET_FILE:render_tool>" "${CMAKE_BINARY_DIR}")
add_test(NAME standard_sequences COMMAND ironstack_render_tests "$<TARGET_FILE:render_tool>"
    "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/docs/test-sequences/fixtures.json")
add_test(NAME bass_sequences COMMAND ironstack_render_tests "$<TARGET_FILE:render_tool>"
    "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/docs/test-sequences/fixtures.json" "--bass")
