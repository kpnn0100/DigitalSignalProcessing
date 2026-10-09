# Arstro DSP Library — Basic Synth and Drum Machine as VST3 instruments (REQ-vst-1…5; Solaris R-VST).
#
# The SDK is installed once per machine, outside this repo (Steinberg's vst3sdk, MIT since 3.8):
#     git clone --recursive --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3sdk ~/sdk/vst3sdk
# and found through VST3_SDK_ROOT (a CMake variable or the environment; default ~/sdk/vst3sdk). With
# no SDK the plugins are skipped, saying so — arstro_dsp itself never depends on it.
#
# The SDK's own CMake needs 3.25; this builds its sources with OUR CMake (the source lists are the
# SDK's — cmake/modules/SMTG_VST3_SDK.cmake and the validator's CMakeLists), so its global settings
# never reach the rest of the build. Everything here is position-independent: it links into a module.

set(_vst3_default "$ENV{VST3_SDK_ROOT}")
if(NOT _vst3_default)
  set(_vst3_default "$ENV{HOME}/sdk/vst3sdk")
endif()
set(VST3_SDK_ROOT "${_vst3_default}" CACHE PATH "Steinberg VST3 SDK (vst3sdk) root")
if(NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces/vst/ivstaudioprocessor.h")
  message(STATUS "arstro VST3: no SDK at '${VST3_SDK_ROOT}' — the plugins are skipped (set VST3_SDK_ROOT)")
  return()
endif()
message(STATUS "arstro VST3: SDK at ${VST3_SDK_ROOT} — building Arstro Basic Synth and Arstro Drum Machine")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_target_properties(arstro_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(S "${VST3_SDK_ROOT}")
set(_vst3_defs $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)

# ── the SDK, as static libraries (its lists, not its CMake) ──
add_library(vst3_base STATIC
  ${S}/base/source/baseiids.cpp ${S}/base/source/fbuffer.cpp ${S}/base/source/fdebug.cpp
  ${S}/base/source/fdynlib.cpp ${S}/base/source/fobject.cpp ${S}/base/source/fstreamer.cpp
  ${S}/base/source/fstring.cpp ${S}/base/source/timer.cpp ${S}/base/source/updatehandler.cpp
  ${S}/base/thread/source/fcondition.cpp ${S}/base/thread/source/flock.cpp
  ${S}/pluginterfaces/base/conststringtable.cpp ${S}/pluginterfaces/base/coreiids.cpp
  ${S}/pluginterfaces/base/funknown.cpp ${S}/pluginterfaces/base/ustring.cpp)
target_include_directories(vst3_base PUBLIC ${S})
target_compile_definitions(vst3_base PUBLIC ${_vst3_defs})
target_compile_options(vst3_base PRIVATE -w) # third-party code: its warnings are not ours to fix
target_link_libraries(vst3_base PUBLIC Threads::Threads)

add_library(vst3_common STATIC
  ${S}/public.sdk/source/common/commoniids.cpp ${S}/public.sdk/source/common/commonstringconvert.cpp
  ${S}/public.sdk/source/common/openurl.cpp ${S}/public.sdk/source/common/systemclipboard_linux.cpp
  ${S}/public.sdk/source/common/threadchecker_linux.cpp ${S}/public.sdk/source/common/readfile.cpp
  ${S}/public.sdk/source/vst/vstpresetfile.cpp)
target_compile_options(vst3_common PRIVATE -w)
target_link_libraries(vst3_common PUBLIC vst3_base)

add_library(vst3_sdk STATIC
  ${S}/public.sdk/source/common/pluginview.cpp ${S}/public.sdk/source/main/pluginfactory.cpp
  ${S}/public.sdk/source/main/moduleinit.cpp ${S}/public.sdk/source/vst/utility/dataexchange.cpp
  ${S}/public.sdk/source/vst/utility/stringconvert.cpp ${S}/public.sdk/source/vst/utility/systemtime.cpp
  ${S}/public.sdk/source/vst/utility/testing.cpp ${S}/public.sdk/source/vst/utility/vst2persistence.cpp
  ${S}/public.sdk/source/vst/vstaudioeffect.cpp ${S}/public.sdk/source/vst/vstbus.cpp
  ${S}/public.sdk/source/vst/vstcomponent.cpp ${S}/public.sdk/source/vst/vstcomponentbase.cpp
  ${S}/public.sdk/source/vst/vsteditcontroller.cpp ${S}/public.sdk/source/vst/vstinitiids.cpp
  ${S}/public.sdk/source/vst/vstnoteexpressiontypes.cpp ${S}/public.sdk/source/vst/vstparameters.cpp
  ${S}/public.sdk/source/vst/vstrepresentation.cpp)
target_compile_options(vst3_sdk PRIVATE -w)
target_link_libraries(vst3_sdk PUBLIC vst3_common)

add_library(vst3_hosting STATIC
  ${S}/public.sdk/source/vst/hosting/connectionproxy.cpp ${S}/public.sdk/source/vst/hosting/eventlist.cpp
  ${S}/public.sdk/source/vst/hosting/hostclasses.cpp ${S}/public.sdk/source/vst/hosting/module.cpp
  ${S}/public.sdk/source/vst/hosting/module_linux.cpp ${S}/public.sdk/source/vst/hosting/parameterchanges.cpp
  ${S}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp ${S}/public.sdk/source/vst/hosting/processdata.cpp
  ${S}/public.sdk/source/vst/utility/stringconvert.cpp ${S}/public.sdk/source/vst/vstinitiids.cpp)
target_compile_options(vst3_hosting PRIVATE -w)
target_link_libraries(vst3_hosting PUBLIC vst3_common ${CMAKE_DL_LIBS})

# ── the plugins: one source, built per registry instrument, as a VST3 bundle ──
set(ARSTRO_VST3_DIR "${CMAKE_BINARY_DIR}/vst3" CACHE INTERNAL "")
set(_arch "${CMAKE_SYSTEM_PROCESSOR}-linux")
function(arstro_vst3_plugin target bundle kind)
  add_library(${target} MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/factory.cpp ${CMAKE_CURRENT_SOURCE_DIR}/RegistryPlugin.cpp
    ${S}/public.sdk/source/main/linuxmain.cpp)
  target_compile_definitions(${target} PRIVATE ARSTRO_VST3_KIND=${kind})
  target_link_libraries(${target} PRIVATE arstro_dsp vst3_sdk)
  set_target_properties(${target} PROPERTIES
    PREFIX "" OUTPUT_NAME "${bundle}" SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY "${ARSTRO_VST3_DIR}/${bundle}.vst3/Contents/${_arch}"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    LINK_FLAGS "-Wl,--no-undefined")
endfunction()
arstro_vst3_plugin(arstro_vst3_synth ArstroBasicSynth 1)
arstro_vst3_plugin(arstro_vst3_drums ArstroDrumMachine 2)

# ── the SDK's editor host (Linux, X11): opens a plugin's editor in a real host window, the L5 check of
#    an editor (REQ-vst-6) — `vst3_editorhost <bundle>.vst3`. Its sources need X11 only (the SDK's CMake
#    also asks for gtkmm, which none of them uses). ──
find_package(X11)
if(X11_FOUND)
  set(_eh ${S}/public.sdk/samples/vst-hosting/editorhost/source)
  add_executable(vst3_editorhost
    ${_eh}/editorhost.cpp ${_eh}/usediids.cpp ${_eh}/platform/linux/platform.cpp ${_eh}/platform/linux/runloop.cpp
    ${_eh}/platform/linux/window.cpp ${S}/public.sdk/source/vst/hosting/plugprovider.cpp)
  target_include_directories(vst3_editorhost PRIVATE ${X11_INCLUDE_DIR})
  target_compile_options(vst3_editorhost PRIVATE -w)
  target_link_libraries(vst3_editorhost PRIVATE vst3_hosting ${X11_LIBRARIES})
endif()

# ── the SDK's validator (its own test suite for any plugin) ──
file(GLOB_RECURSE _vst3_testsuite ${S}/public.sdk/source/vst/testsuite/*.cpp)
file(GLOB _vst3_hosttests ${S}/public.sdk/source/vst/hosting/test/*.cpp)
set(_v ${S}/public.sdk/samples/vst-hosting/validator/source)
add_executable(vst3_validator
  ${_v}/main.cpp ${_v}/usediids.cpp ${_v}/validator.cpp ${_vst3_testsuite} ${_vst3_hosttests}
  ${S}/public.sdk/source/common/memorystream.cpp ${S}/public.sdk/source/main/moduleinit.cpp
  ${S}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp ${S}/public.sdk/source/vst/hosting/plugprovider.cpp
  ${S}/public.sdk/source/vst/utility/testing.cpp)
target_compile_options(vst3_validator PRIVATE -w)
target_link_libraries(vst3_validator PRIVATE vst3_hosting)

# ── tests: the validator passes both; the plugin IS the device, sample for sample (REQ-vst-5) ──
foreach(p synth:ArstroBasicSynth drums:ArstroDrumMachine)
  string(REPLACE ":" ";" pp ${p})
  list(GET pp 0 kind)
  list(GET pp 1 bundle)
  add_test(NAME vst3_validate_${kind} COMMAND vst3_validator "${ARSTRO_VST3_DIR}/${bundle}.vst3")
endforeach()
add_executable(vst3_equivalence ${CMAKE_CURRENT_SOURCE_DIR}/tests/equivalence.cpp ${S}/public.sdk/source/common/memorystream.cpp)
target_compile_options(vst3_equivalence PRIVATE -Wno-deprecated-declarations)
target_link_libraries(vst3_equivalence PRIVATE arstro_dsp vst3_hosting)
target_compile_definitions(vst3_equivalence PRIVATE ARSTRO_VST3_DIR="${ARSTRO_VST3_DIR}")
add_dependencies(vst3_equivalence arstro_vst3_synth arstro_vst3_drums)
add_test(NAME vst3_equivalence COMMAND vst3_equivalence)

# `cmake --build build --target vst3_install` — copies both bundles to ~/.vst3, where hosts look
add_custom_target(vst3_install
  COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.vst3"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARSTRO_VST3_DIR}/ArstroBasicSynth.vst3" "$ENV{HOME}/.vst3/ArstroBasicSynth.vst3"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARSTRO_VST3_DIR}/ArstroDrumMachine.vst3" "$ENV{HOME}/.vst3/ArstroDrumMachine.vst3"
  DEPENDS arstro_vst3_synth arstro_vst3_drums
  COMMENT "Installing Arstro Basic Synth and Arstro Drum Machine to ~/.vst3")
