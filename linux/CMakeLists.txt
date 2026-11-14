# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

# Project-level configuration.
set(PROJECT_NAME "surround_encoder")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "surround_encoder_plugin")

# Common pipeline sources (shared across Windows/macOS/Linux backends).
set(COMMON_SOURCES
  "../src/common/ac3_encoder.cpp"
  "../src/common/ac3_encoder.h"
  "../src/common/capture_source.h"
  "../src/common/dca_encoder.cpp"
  "../src/common/dca_encoder.h"
  "../src/common/encoder.h"
  "../src/common/encoder_pipeline.cpp"
  "../src/common/encoder_pipeline.h"
  "../src/common/ts_muxer.cpp"
  "../src/common/ts_muxer.h"
)

set(PLATFORM_SOURCES
  "../src/linux/pulse_capture.h"
  "../src/linux/capture_factory_linux.cpp"
)

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "messages.g.cc"
  "surround_encoder_plugin.cc"
  ${COMMON_SOURCES}
  ${PLATFORM_SOURCES}
)

# libpulse is required for capture; pkg-config resolves it (works against
# PulseAudio and PipeWire's pulse compat layer).
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

# --- FFmpeg discovery ---------------------------------------------------
# Resolution order:
#   1. pkg-config (system FFmpeg)
#   2. SURROUND_ENCODER_FFMPEG_ROOT cache variable or environment variable
#      pointing at a dev layout: include/ + lib/.
set(SURROUND_ENCODER_FFMPEG_ROOT "" CACHE PATH
    "Path to an FFmpeg development layout (include/, lib/)")
if(NOT SURROUND_ENCODER_FFMPEG_ROOT AND DEFINED ENV{SURROUND_ENCODER_FFMPEG_ROOT})
  set(SURROUND_ENCODER_FFMPEG_ROOT "$ENV{SURROUND_ENCODER_FFMPEG_ROOT}")
endif()

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::PULSE)

if(SURROUND_ENCODER_FFMPEG_ROOT)
  target_include_directories(${PLUGIN_NAME} PRIVATE
    "${SURROUND_ENCODER_FFMPEG_ROOT}/include")
  find_library(FFMPEG_AVCODEC avcodec
    PATHS "${SURROUND_ENCODER_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(FFMPEG_AVFORMAT avformat
    PATHS "${SURROUND_ENCODER_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(FFMPEG_SWRESAMPLE swresample
    PATHS "${SURROUND_ENCODER_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(FFMPEG_AVUTIL avutil
    PATHS "${SURROUND_ENCODER_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
  foreach(_lib FFMPEG_AVCODEC FFMPEG_AVFORMAT FFMPEG_SWRESAMPLE FFMPEG_AVUTIL)
    if(NOT ${_lib})
      message(FATAL_ERROR "FFmpeg library ${_lib} not found under "
        "${SURROUND_ENCODER_FFMPEG_ROOT}/lib - run tool/setup_ffmpeg.sh")
    endif()
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${${_lib}})
  endforeach()
else()
  pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat
    libswresample libavutil)
  if(NOT FFMPEG_FOUND)
    message(FATAL_ERROR
      "FFmpeg not found. Install libavcodec-dev libavformat-dev "
      "libswresample-dev libavutil-dev, or run tool/setup_ffmpeg.sh and set "
      "SURROUND_ENCODER_FFMPEG_ROOT.")
  endif()
  target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::FFMPEG)
endif()

# --- dcaenc discovery (DTS Coherent Acoustics encoder, see NOTICE.md) -----
set(SURROUND_ENCODER_DCAENC_ROOT "" CACHE PATH
    "Path to a dcaenc development layout (include/, lib/)")
if(NOT SURROUND_ENCODER_DCAENC_ROOT AND DEFINED ENV{SURROUND_ENCODER_DCAENC_ROOT})
  set(SURROUND_ENCODER_DCAENC_ROOT "$ENV{SURROUND_ENCODER_DCAENC_ROOT}")
endif()
if(NOT SURROUND_ENCODER_DCAENC_ROOT)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM|aarch64|AARCH64)")
    set(_se_arch "arm64")
  else()
    set(_se_arch "x64")
  endif()
  set(_se_dcaenc_layout
    "${CMAKE_CURRENT_SOURCE_DIR}/../native/dcaenc/linux-${_se_arch}")
  if(EXISTS "${_se_dcaenc_layout}/include")
    set(SURROUND_ENCODER_DCAENC_ROOT "${_se_dcaenc_layout}")
  endif()
  unset(_se_arch)
  unset(_se_dcaenc_layout)
endif()

if(SURROUND_ENCODER_DCAENC_ROOT)
  target_include_directories(${PLUGIN_NAME} PRIVATE
    "${SURROUND_ENCODER_DCAENC_ROOT}/include")
  find_library(_dcaenc_lib dcaenc
    PATHS "${SURROUND_ENCODER_DCAENC_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT _dcaenc_lib)
    message(FATAL_ERROR "dcaenc library not found under "
      "${SURROUND_ENCODER_DCAENC_ROOT}/lib - run tool/setup_dcaenc.sh")
  endif()
  target_link_libraries(${PLUGIN_NAME} PRIVATE ${_dcaenc_lib})
  unset(_dcaenc_lib CACHE)
endif()

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(surround_encoder_bundled_libraries
  ""
  PARENT_SCOPE
)

# === Tests ===
# These unit tests can be run from a terminal after building the example.

# Only enable test builds when building the example (which sets this variable)
# so that plugin clients aren't building the tests.
if (${include_${PROJECT_NAME}_tests})
if(${CMAKE_VERSION} VERSION_LESS "3.11.0")
message("Unit tests require CMake 3.11.0 or later")
else()
set(TEST_RUNNER "${PROJECT_NAME}_test")
enable_testing()

# Add the Google Test dependency.
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.14.1.zip
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# Disable install commands for gtest so it doesn't end up in the bundle.
set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of googletest" FORCE)

FetchContent_MakeAvailable(googletest)

# The plugin's exported API is not very useful for unit testing, so build the
# sources directly into the test binary rather than using the shared library.
add_executable(${TEST_RUNNER}
  test/surround_encoder_plugin_test.cc
  ${PLUGIN_SOURCES}
)
apply_standard_settings(${TEST_RUNNER})
target_include_directories(${TEST_RUNNER} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TEST_RUNNER} PRIVATE flutter)
target_link_libraries(${TEST_RUNNER} PRIVATE PkgConfig::GTK)
target_link_libraries(${TEST_RUNNER} PRIVATE PkgConfig::PULSE)
target_link_libraries(${TEST_RUNNER} PRIVATE gtest_main gmock)

# Enable automatic test discovery.
include(GoogleTest)
gtest_discover_tests(${TEST_RUNNER})

endif()  # CMake version check
endif()  # include_${PROJECT_NAME}_tests
