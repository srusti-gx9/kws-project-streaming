cmake_minimum_required(VERSION 3.13)

# Standalone Keyword Spotting Project
# Based on sherpa-onnx/c-api-examples/CMakeLists.txt

project(keyword_spotter)

set(CMAKE_C_STANDARD 11)

# Local header and library files
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
link_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Build executable
add_executable(keyword_spotter keyword_spotter.c audio_stream.c audio_capture.c ring_buffer.c)

# Link Sherpa-ONNX C API library
target_link_libraries(keyword_spotter sherpa-onnx-c-api pthread asound m)

# Look for shared libraries (.so) in the current directory
set_target_properties(keyword_spotter PROPERTIES
    BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
    INSTALL_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
) 
