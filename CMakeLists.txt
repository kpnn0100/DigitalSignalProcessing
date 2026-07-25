cmake_minimum_required(VERSION 3.14)
project(arstro_dsp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ── Library: channel-aware synth subset ──
# Excludes spatial/ (legacy mono process()) and its util/ support (pre-existing
# bug in Coordinate.cpp); neither is on the channel-aware synth path.
file(GLOB_RECURSE ARSTRO_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER ARSTRO_SOURCES EXCLUDE REGEX "src/(spatial|util)/")

add_library(arstro_dsp STATIC ${ARSTRO_SOURCES})
target_include_directories(arstro_dsp PUBLIC src)
target_link_libraries(arstro_dsp PUBLIC Threads::Threads)

# ── Unit tests (dependency-free MiniTest harness) ──
enable_testing()
add_executable(synth_tests unittest/synthTests.cpp unittest/coverageTests.cpp)
target_link_libraries(synth_tests PRIVATE arstro_dsp)
add_test(NAME unit COMMAND synth_tests)

# ── Integration tests (Python drives a WAV-emitting harness) ──
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME integration
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_integration.py)
endif()

# ── Kitchen-sink bench (default audio I/O via stdout/WAV — no extra deps) ──
add_executable(arstro_kitchen_sink apps/kitchen_sink/main.cpp)
target_link_libraries(arstro_kitchen_sink PRIVATE arstro_dsp)

# Smoke test: offline render produces a non-empty WAV.
if(Python3_Interpreter_FOUND)
  add_test(NAME kitchen_sink_smoke
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_kitchen_sink.py
                   $<TARGET_FILE:arstro_kitchen_sink>)
endif()

# RackEngine: the C++ engine the WASM web UI drives. Verified natively here; the
# WASM build (apps/kitchen_sink/build_web.sh) and browser UI run on your machine.
add_executable(rack_smoke apps/kitchen_sink/test_rack.cpp apps/kitchen_sink/RackEngine.cpp)
target_link_libraries(rack_smoke PRIVATE arstro_dsp)
add_test(NAME rack_smoke COMMAND rack_smoke)

# ── Piano demo (the "simple UI" for auditioning PianoVoice — REQ-piano-10) ──
add_executable(arstro_piano_demo apps/piano_demo/main.cpp apps/piano_demo/PianoEngine.cpp)
target_link_libraries(arstro_piano_demo PRIVATE arstro_dsp)

if(Python3_Interpreter_FOUND)
  add_test(NAME piano_demo_smoke
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_piano_demo.py
                   $<TARGET_FILE:arstro_piano_demo>)
endif()

# ── Optional demo app (needs libsndfile; skipped if not present) ──
find_library(SNDFILE_LIB sndfile)
if(SNDFILE_LIB)
  add_executable(wav_demo apps/wav_demo/main.cpp)
  target_link_libraries(wav_demo PRIVATE arstro_dsp ${SNDFILE_LIB})
endif()
