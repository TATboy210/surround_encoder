# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "surround_encoder")
project(${PROJECT_NAME} LANGUAGES CXX)

# Explicitly opt in to modern CMake behaviors to avoid warnings with recent
# versions of CMake.
cmake_policy(VERSION 3.14...3.25)

# This value is used when generating builds using this plugin, so it must
# not be changed
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
  "../src/windows/wasapi_capture.cpp"
  "../src/windows/wasapi_capture.h"
  "../src/windows/capture_factory_win.cpp"
)

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "messages.g.cpp"
  "messages.g.h"
  "surround_encoder_plugin.cpp"
  "surround_encoder_plugin.h"
  ${COMMON_SOURCES}
  ${PLATFORM_SOURCES}
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "include/surround_encoder/surround_encoder_plugin_c_api.h"
  "surround_encoder_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# FFmpeg headers emit benign MSVC conversion warnings and non-ASCII comments;
# our own code compiles clean UTF-8.
target_compile_options(${PLUGIN_NAME} PRIVATE
  /utf-8
  /wd4244  # int -> smaller int conversions inside FFmpeg headers
  /wd4267
  /wd4101
  /wd4189
  /wd4996
)

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# --- FFmpeg discovery ---------------------------------------------------
# Resolution order:
#   1. vcpkg toolchain (find_package)
#   2. SURROUND_ENCODER_FFMPEG_ROOT cache variable or environment variable
#      pointing at a dev layout: include/ + lib/ + bin/.
# Run tool/setup_ffmpeg.ps1 to populate native/ffmpeg/<platform>-<arch>.
set(SURROUND_ENCODER_FFMPEG_ROOT "" CACHE PATH
    "Path to an FFmpeg development layout (include/, lib/, bin/)")
if(NOT SURROUND_ENCODER_FFMPEG_ROOT AND DEFINED ENV{SURROUND_ENCODER_FFMPEG_ROOT})
  set(SURROUND_ENCODER_FFMPEG_ROOT "$ENV{SURROUND_ENCODER_FFMPEG_ROOT}")
endif()
# Fall back to the repo-local layout produced by tool/setup_ffmpeg.ps1.
if(NOT SURROUND_ENCODER_FFMPEG_ROOT)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM|aarch64|AARCH64)")
    set(_se_arch "arm64")
  else()
    set(_se_arch "x64")
  endif()
  set(_se_repo_layout
    "${CMAKE_CURRENT_SOURCE_DIR}/../native/ffmpeg/windows-${_se_arch}")
  if(EXISTS "${_se_repo_layout}/include")
    set(SURROUND_ENCODER_FFMPEG_ROOT "${_se_repo_layout}")
  endif()
  unset(_se_arch)
  unset(_se_repo_layout)
endif()

if(SURROUND_ENCODER_FFMPEG_ROOT)
  target_include_directories(${PLUGIN_NAME} PRIVATE
    "${SURROUND_ENCODER_FFMPEG_ROOT}/include")
  foreach(_lib avcodec avformat swresample avutil)
    find_library(_ffmpeg_${_lib} ${_lib}
      PATHS "${SURROUND_ENCODER_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT _ffmpeg_${_lib})
      message(FATAL_ERROR "FFmpeg library ${_lib} not found under "
        "${SURROUND_ENCODER_FFMPEG_ROOT}/lib - run tool/setup_ffmpeg.ps1")
    endif()
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${_ffmpeg_${_lib}})
    unset(_ffmpeg_${_lib} CACHE)
  endforeach()
else()
  find_package(FFMPEG QUIET COMPONENTS avcodec avformat swresample avutil)
  if(NOT FFMPEG_FOUND)
    message(FATAL_ERROR
      "FFmpeg not found. Either build with the vcpkg toolchain, or set "
      "SURROUND_ENCODER_FFMPEG_ROOT / SURROUND_ENCODER_FFMPEG_ROOT env "
      "to an FFmpeg dev layout (see tool/setup_ffmpeg.ps1).")
  endif()
  target_include_directories(${PLUGIN_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(${PLUGIN_NAME} PRIVATE
    ${FFMPEG_LIBRARIES} ${FFMPEG_LIBRARY_DIRS})
endif()

# --- dcaenc discovery (DTS Coherent Acoustics encoder, see NOTICE.md) -----
# Same resolution order: cache/env var, then the repo-local layout produced
# by tool/setup_dcaenc.ps1.
set(SURROUND_ENCODER_DCAENC_ROOT "" CACHE PATH
    "Path to a dcaenc development layout (include/, lib/, bin/)")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../native/dcaenc/windows-${_se_arch}")
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
      "${SURROUND_ENCODER_DCAENC_ROOT}/lib - run tool/setup_dcaenc.ps1")
  endif()
  target_link_libraries(${PLUGIN_NAME} PRIVATE ${_dcaenc_lib})
  unset(_dcaenc_lib CACHE)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}")

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin
  ole32 user32)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
#
# FFmpeg runtime DLLs are bundled here so Flutter copies them next to the
# host executable, where the Windows loader resolves them.
set(surround_encoder_bundled_libraries
  ""
  PARENT_SCOPE
)
if(SURROUND_ENCODER_FFMPEG_ROOT)
  file(GLOB SURROUND_FFMPEG_DLLS "${SURROUND_ENCODER_FFMPEG_ROOT}/bin/*.dll")
  list(APPEND surround_encoder_bundled_libraries ${SURROUND_FFMPEG_DLLS})
endif()
if(SURROUND_ENCODER_DCAENC_ROOT)
  file(GLOB SURROUND_DCAENC_DLLS "${SURROUND_ENCODER_DCAENC_ROOT}/bin/*.dll")
  list(APPEND surround_encoder_bundled_libraries ${SURROUND_DCAENC_DLLS})
endif()
set(surround_encoder_bundled_libraries
  ${surround_encoder_bundled_libraries}
  PARENT_SCOPE
)

# === Tests ===
# These unit tests can be run from a terminal after building the example, or
# from Visual Studio after opening the generated solution file.

# Only enable test builds when building the example (which sets this variable)
# so that plugin clients aren't building the tests.
if (${include_${PROJECT_NAME}_tests})
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

# The plugin's C API is not very useful for unit testing, so build the sources
# directly into the test binary rather than using the DLL.
add_executable(${TEST_RUNNER}
  test/surround_encoder_plugin_test.cpp
  ${PLUGIN_SOURCES}
)
apply_standard_settings(${TEST_RUNNER})
target_include_directories(${TEST_RUNNER} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TEST_RUNNER} PRIVATE flutter_wrapper_plugin)
target_link_libraries(${TEST_RUNNER} PRIVATE gtest_main gmock)
# flutter_wrapper_plugin has link dependencies on the Flutter DLL.
add_custom_command(TARGET ${TEST_RUNNER} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${FLUTTER_LIBRARY}" $<TARGET_FILE_DIR:${TEST_RUNNER}>
)

# Enable automatic test discovery.
include(GoogleTest)
gtest_discover_tests(${TEST_RUNNER})
endif()
