# Firmware for the PedalPCB Terrarium (Daisy Seed). A separate CMake project
# from the desktop build, because everything here is cross-compiled:
#
#   cmake -S hosts/daisy -B build-daisy -G Ninja
#   cmake --build build-daisy
#
# gives build-daisy/terrarium-<effect>.bin for every effect in TFX_DAISY_EFFECTS
# (default: every folder under effects/ with an effect.cmake).
# Needs arm-none-eabi-gcc on PATH. libDaisy (MIT) is downloaded at configure
# time; pass -DLIBDAISY_DIR=/path/to/libDaisy to use a local checkout instead.
cmake_minimum_required(VERSION 3.20)

set(LIBDAISY_TAG v9.0.0) # first release that recognises the Seed3

# libDaisy carries the toolchain description, so fetch it before project().
if(NOT LIBDAISY_DIR)
  include(FetchContent)
  FetchContent_Populate(libdaisy
    GIT_REPOSITORY https://github.com/electro-smith/libDaisy.git
    GIT_TAG        ${LIBDAISY_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES Drivers/CMSIS_5 Drivers/CMSIS-Device/ST/STM32H7xx Drivers/STM32H7xx_HAL_Driver
                   Middlewares/ST/STM32_USB_Device_Library Middlewares/ST/STM32_USB_Host_Library
    SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/libdaisy-src
    QUIET)
  set(LIBDAISY_DIR ${CMAKE_BINARY_DIR}/_deps/libdaisy-src CACHE PATH "libDaisy source" FORCE)
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
  include(${LIBDAISY_DIR}/cmake/toolchains/autodetect.cmake)
endif()

project(terrarium_fx_daisy C CXX ASM)

set(TFX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(${LIBDAISY_DIR} libdaisy EXCLUDE_FROM_ALL)
# libDaisy only sets optimisation when it is the top-level project, and the
# firmware has to fit the 128 KB of internal flash: build it and the ST
# drivers for size, our own code for speed.
get_target_property(daisy_deps daisy LINK_LIBRARIES)
foreach(dep IN LISTS daisy_deps ITEMS daisy)
  get_target_property(type ${dep} TYPE)
  if(NOT type STREQUAL "INTERFACE_LIBRARY")
    target_compile_options(${dep} PRIVATE -Os)
  endif()
endforeach()
set(LINKER_SCRIPT ${LIBDAISY_DIR}/core/STM32H750IB_flash.lds)

# Which commit the firmware was built from; printed at boot. A release build
# passes its own, e.g. -DTFX_VERSION=oncemore-v1.0.
if(NOT TFX_VERSION)
  execute_process(COMMAND git describe --always --dirty WORKING_DIRECTORY ${TFX_ROOT}
                  OUTPUT_VARIABLE TFX_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT TFX_VERSION)
  set(TFX_VERSION unknown)
endif()

# The hardware-free core, compiled for the pedal with the same strict checks
# as on the desktop.
add_library(tfx_core_arm STATIC
  ${TFX_ROOT}/core/src/param.cpp
  ${TFX_ROOT}/core/src/gesture.cpp
  ${TFX_ROOT}/core/src/panel.cpp)
target_include_directories(tfx_core_arm PUBLIC ${TFX_ROOT}/core/include)
# Parameter help texts are for desktop hosts; keep them out of the flash.
target_compile_definitions(tfx_core_arm PUBLIC TFX_NO_HELP)
set(TFX_STRICT -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wdouble-promotion -Werror)
target_compile_options(tfx_core_arm PRIVATE ${TFX_STRICT} -O3 -ffp-contract=off)

# Each effect describes itself in effects/<name>/effect.cmake. Build a subset
# with -DTFX_DAISY_EFFECTS="oncemore" (default: all of them).
include(${TFX_ROOT}/cmake/tfx_effects.cmake)
tfx_find_effects(${TFX_ROOT} TFX_ALL_EFFECTS)
set(TFX_DAISY_EFFECTS "${TFX_ALL_EFFECTS}" CACHE STRING "Effects to build firmware for")

foreach(fx IN LISTS TFX_DAISY_EFFECTS)
  tfx_load_effect(${TFX_ROOT} ${fx})
  set(target terrarium-${fx})
  set(sources ${${fx}_SOURCES})
  add_executable(${target} main.cpp ${sources})
  set_source_files_properties(${sources} PROPERTIES
    COMPILE_OPTIONS "${TFX_STRICT};-ffp-contract=off")
  target_include_directories(${target} PRIVATE ${TFX_ROOT}/effects)
  target_compile_definitions(${target} PRIVATE
    TFX_EFFECT_CLASS=${${fx}_CLASS}
    TFX_EFFECT_HEADER="${${fx}_HEADER}"
    TFX_EFFECT_NAME="${fx}"
    TFX_VERSION="${TFX_VERSION}")
  target_compile_options(${target} PRIVATE -O3 -Wall -Wextra)
  target_link_libraries(${target} PRIVATE tfx_core_arm daisy)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES SUFFIX ".elf")
  target_link_options(${target} PRIVATE
    LINKER:-T,${LINKER_SCRIPT}
    LINKER:--gc-sections
    LINKER:--print-memory-usage
    LINKER:-Map=${target}.map)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${target}> ${CMAKE_BINARY_DIR}/${target}.bin
    COMMENT "Writing ${target}.bin"
    VERBATIM)
endforeach()
