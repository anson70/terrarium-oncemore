cmake_minimum_required(VERSION 3.24)
project(terrarium_fx LANGUAGES C CXX)

# Desktop build: core, effects, offline host and tests.
# The Daisy host is cross-compiled separately (hosts/daisy, milestone M1).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Windows: link the C++ runtime statically, so fx-live.exe and fx-run.exe run
# on any PC without installing the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TFX_BUILD_TESTS "Build the test suite" ON)
option(TFX_BUILD_LIVE  "Build fx-live (needs a desktop with OpenGL)" ON)
# ASIO (Windows): links Steinberg's ASIO host code (shipped with RtAudio), which
# is GPLv3 or proprietary; an fx-live built with it is GPLv3 as a whole.
# The source in this repository stays MIT either way.
if(WIN32)
  option(TFX_WITH_ASIO "fx-live: add the ASIO driver (makes the fx-live binary GPLv3)" ON)
else()
  set(TFX_WITH_ASIO OFF)
endif()

# Effects must behave the same on the desktop as on the Cortex-M7, so no
# fast-math here: it would let the compiler reorder float maths differently
# from the pedal build and hide NaN/Inf that the offline host must report.
# Applied per target (not add_compile_options) so third-party code fetched
# below keeps its own warning settings.
function(tfx_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /fp:precise)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -ffp-contract=off)
  endif()
endfunction()

# Code that runs on the pedal gets stricter checks: on the Cortex-M7 an
# accidental double (-Wdouble-promotion) is slower and drags in double libm,
# and silent float/int narrowing is a classic DSP bug source.
function(tfx_strict target)
  tfx_warnings(${target})
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wconversion -Wno-sign-conversion -Wdouble-promotion
                                             -Werror)
  endif()
endfunction()

include(FetchContent)
# yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake 4
# rejects outright. Raise the floor for fetched projects only.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
  GIT_SHALLOW    TRUE
  SYSTEM)
FetchContent_MakeAvailable(yaml-cpp)

# --- core: no libDaisy, no host code ---------------------------------------
add_library(tfx_core STATIC
  core/src/param.cpp
  core/src/gesture.cpp
  core/src/panel.cpp)
target_include_directories(tfx_core PUBLIC core/include)
tfx_strict(tfx_core)

# --- effects ----------------------------------------------------------------
# Each effect is a folder with an effect.cmake (cmake/tfx_effects.cmake).
# TFX_EFFECTS narrows the build, e.g. -DTFX_EFFECTS="passthrough;oncemore".
include(cmake/tfx_effects.cmake)
tfx_find_effects(${PROJECT_SOURCE_DIR} TFX_ALL_EFFECTS)
set(TFX_EFFECTS "${TFX_ALL_EFFECTS}" CACHE STRING "Effects to build (default: every folder under effects/)")
if(NOT "passthrough" IN_LIST TFX_EFFECTS)
  # fx-live falls back to it and the CLI tests use it.
  message(FATAL_ERROR "TFX_EFFECTS must include passthrough (got: ${TFX_EFFECTS})")
endif()

set(TFX_EFFECT_SOURCES "")
set(TFX_EFFECT_TESTS "")
set(TFX_REGISTRY_INCLUDES "")
set(TFX_REGISTRY_ENTRIES "")
foreach(fx IN LISTS TFX_EFFECTS)
  tfx_load_effect(${PROJECT_SOURCE_DIR} ${fx})
  list(APPEND TFX_EFFECT_SOURCES ${${fx}_SOURCES})
  list(APPEND TFX_EFFECT_TESTS ${${fx}_TESTS})
  string(APPEND TFX_REGISTRY_INCLUDES "#include \"${${fx}_HEADER}\"\n")
  string(APPEND TFX_REGISTRY_ENTRIES
    "    {\"${fx}\", [] () -> std::unique_ptr<Effect> { return std::make_unique<${${fx}_CLASS}>(); }},\n")
endforeach()
configure_file(effects/registry.cpp.in ${PROJECT_BINARY_DIR}/generated/registry.cpp @ONLY)

add_library(tfx_effects STATIC ${PROJECT_BINARY_DIR}/generated/registry.cpp ${TFX_EFFECT_SOURCES})
target_include_directories(tfx_effects PUBLIC effects)
target_link_libraries(tfx_effects PUBLIC tfx_core)
tfx_strict(tfx_effects)

# --- desktop hosts: shared pieces ---------------------------------------------
add_library(tfx_desktop STATIC
  hosts/common/aligned.cpp
  hosts/common/audio_io.cpp
  hosts/common/heap_allocator.cpp)
target_include_directories(tfx_desktop PUBLIC hosts/common)
target_include_directories(tfx_desktop SYSTEM PRIVATE third_party)
target_link_libraries(tfx_desktop PUBLIC tfx_core)
tfx_warnings(tfx_desktop)
if(NOT MSVC)
  # dr_wav is third-party; keep our strict warnings for our own code only.
  set_source_files_properties(hosts/common/audio_io.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Replaces global operator new/delete, so it is linked straight into each
# executable as objects; from inside a static library MSVC can pull in the
# CRT's own definitions as well and fail with duplicate symbols.
add_library(tfx_alloc_guard OBJECT hosts/common/alloc_guard.cpp)
target_include_directories(tfx_alloc_guard PUBLIC hosts/common)
tfx_warnings(tfx_alloc_guard)

# --- offline host -----------------------------------------------------------
add_library(tfx_offline STATIC
  hosts/offline/engine.cpp
  hosts/offline/report.cpp
  hosts/offline/script.cpp)
target_include_directories(tfx_offline PUBLIC hosts/offline)
target_link_libraries(tfx_offline PUBLIC tfx_core tfx_desktop PRIVATE yaml-cpp::yaml-cpp)
tfx_warnings(tfx_offline)

add_executable(fx-run hosts/offline/main.cpp)
target_link_libraries(fx-run PRIVATE tfx_offline tfx_effects tfx_alloc_guard)
tfx_warnings(fx-run)

# --- live host --------------------------------------------------------------
# Absent from the per-effect source exports (publish/package.cmake), which
# carry only the firmware, the offline host and the tests.
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/hosts/live)
  set(TFX_BUILD_LIVE OFF)
else()
# Audio core with no device or GUI code, so it builds and tests everywhere.
add_library(tfx_live STATIC
  hosts/live/core/live_engine.cpp
  hosts/live/core/tuner.cpp
  hosts/live/core/live_sink.cpp
  hosts/live/core/midi_map.cpp
  hosts/live/core/recording.cpp)
target_include_directories(tfx_live PUBLIC hosts/live/core)
target_link_libraries(tfx_live PUBLIC tfx_core tfx_desktop)
tfx_warnings(tfx_live)

if(TFX_BUILD_LIVE)
  # Third-party pieces for the app, fetched at pinned versions.
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # X11 is enough on Linux, fewer build deps
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    SYSTEM)

  set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(RTMIDI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
  set(RTMIDI_TARGETNAME_UNINSTALL rtmidi-uninstall CACHE STRING "" FORCE) # yaml-cpp already has "uninstall"
  FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE
    SYSTEM)

  # Header-only or plain-source projects: fetch, but don't run their CMake.
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-configure)
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-configure)
  set(_tfx_live_deps glfw rtmidi imgui miniaudio)
  if(TFX_WITH_ASIO)
    set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ASIO ON CACHE BOOL "" FORCE)
    set(RTAUDIO_API_WASAPI OFF CACHE BOOL "" FORCE) # WASAPI comes from miniaudio
    set(RTAUDIO_API_DS OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_TARGETNAME_UNINSTALL rtaudio-uninstall CACHE STRING "" FORCE)
    FetchContent_Declare(rtaudio
      GIT_REPOSITORY https://github.com/thestk/rtaudio.git
      GIT_TAG        6.0.1
      GIT_SHALLOW    TRUE
      SYSTEM)
    list(APPEND _tfx_live_deps rtaudio)
  endif()
  FetchContent_MakeAvailable(${_tfx_live_deps})

  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)

  add_library(tfx_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(tfx_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(tfx_imgui PUBLIC glfw OpenGL::GL)

  add_executable(fx-live
    hosts/live/app/main.cpp
    hosts/live/app/diagnostics.cpp
    hosts/live/app/miniaudio_backend.cpp
    hosts/live/app/midi_input.cpp
    hosts/live/app/settings.cpp
    hosts/live/app/widgets.cpp)
  target_include_directories(fx-live SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
  target_include_directories(fx-live PRIVATE hosts/live/app)
  target_link_libraries(fx-live PRIVATE tfx_live tfx_effects tfx_imgui rtmidi Threads::Threads
                                        tfx_alloc_guard ${CMAKE_DL_LIBS})
  tfx_warnings(fx-live)
  if(NOT MSVC)
    # miniaudio is third-party C in a C++ file; silence its warnings only.
    set_source_files_properties(hosts/live/app/miniaudio_backend.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()
  if(TFX_WITH_ASIO)
    target_sources(fx-live PRIVATE hosts/live/app/asio_backend.cpp)
    target_compile_definitions(fx-live PRIVATE TFX_WITH_ASIO=1)
    # asio.h for ASIOControlPanel(); the SDK sources themselves are built into rtaudio.
    target_include_directories(fx-live SYSTEM PRIVATE ${rtaudio_SOURCE_DIR}/include ${rtaudio_SOURCE_DIR})
    target_link_libraries(fx-live PRIVATE rtaudio)
  endif()
  if(WIN32)
    target_link_libraries(fx-live PRIVATE ole32) # CoInitializeEx in diagnostics.cpp
    # A windowed app, but keep main() as the entry point.
    set_target_properties(fx-live PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
      target_link_options(fx-live PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
endif()

endif() # hosts/live exists

# --- tests ------------------------------------------------------------------
if(TFX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
