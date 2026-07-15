cmake_minimum_required(VERSION 3.13)

project(keyword_spotter)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
link_directories(${CMAKE_CURRENT_SOURCE_DIR})

# -----------------------------
# Executable 1
# Standalone Keyword Spotter
# -----------------------------
add_executable(keyword_spotter
    keyword_spotter.c
    audio_stream.c
    audio_capture.c
    audio_capture_real.c
    audio_capture_mock_backend.c
    ring_buffer.c
)

target_link_libraries(keyword_spotter
    sherpa-onnx-c-api
    pthread
    asound
    m
)

# -----------------------------
# Executable 2
# Streaming Demo (Mock WAV)
# -----------------------------
add_executable(audio_stream_demo_mock
    audio_stream_demo_mock.c
    audio_stream.c
    audio_capture_mock.c
    ring_buffer.c
)

target_link_libraries(audio_stream_demo_mock
    sherpa-onnx-c-api
    pthread
    asound
    m
)

# Runtime library path
set_target_properties(keyword_spotter PROPERTIES
    BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
    INSTALL_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(audio_stream_demo_mock PROPERTIES
    BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
    INSTALL_RPATH ${CMAKE_CURRENT_SOURCE_DIR}
)
