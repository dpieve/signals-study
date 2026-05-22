cmake_minimum_required(VERSION 3.20)
project(client CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies — use targets already established by root CMakeLists.txt
# ---------------------------------------------------------------------------
if(NOT TARGET raylib)
    find_package(raylib REQUIRED)
endif()
if(NOT TARGET gRPC::grpc++)
    find_package(gRPC REQUIRED)
endif()
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()
if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()
if(NOT TARGET GTest::gtest_main)
    find_package(GTest REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Proto-generated sources
#
# The consumer subdirectory sets TELEMETRY_PROTO_BINARY_DIR and
# TELEMETRY_PROTO_SOURCES in the parent scope.  If it ran first (which it does
# via add_subdirectory ordering in the root), we inherit those.  Otherwise we
# regenerate from the same proto file.
# ---------------------------------------------------------------------------
if(NOT TELEMETRY_PROTO_BINARY_DIR)
    # Fallback: regenerate (should only happen when building client standalone)
    set(PROTO_FILE  ${CMAKE_SOURCE_DIR}/consumer/grpc/telemetry.proto)
    set(PROTO_SRCS  ${CMAKE_CURRENT_BINARY_DIR}/telemetry.pb.cc)
    set(PROTO_HDRS  ${CMAKE_CURRENT_BINARY_DIR}/telemetry.pb.h)
    set(GRPC_SRCS   ${CMAKE_CURRENT_BINARY_DIR}/telemetry.grpc.pb.cc)
    set(GRPC_HDRS   ${CMAKE_CURRENT_BINARY_DIR}/telemetry.grpc.pb.h)

    add_custom_command(
        OUTPUT  ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND protobuf::protoc
        ARGS
            --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
            --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
            "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
            -I${CMAKE_SOURCE_DIR}/consumer/grpc
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE} protobuf::protoc gRPC::grpc_cpp_plugin
        COMMENT "Generating telemetry protobuf/gRPC sources (client standalone)"
    )
    set(TELEMETRY_PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
    set(TELEMETRY_PROTO_SOURCES ${PROTO_SRCS} ${GRPC_SRCS})
endif()

# ---------------------------------------------------------------------------
# icu_monitor executable
# ---------------------------------------------------------------------------
add_executable(icu_monitor
    app/main.cpp
    app/client_app.cpp
    networking/grpc_client.cpp
    networking/live_stream.cpp
    networking/historical_stream.cpp
    rendering/render_buffers.cpp
    rendering/waveform_renderer.cpp
    rendering/numeric_panel.cpp
    rendering/monitor_layout.cpp
    rendering/status_bar.cpp
    rendering/action_bar.cpp
    timeline/playback_state.cpp
    timeline/timeline_controller.cpp
    ui/channel_panel.cpp
    ui/telemetry_view.cpp
    ${TELEMETRY_PROTO_SOURCES}
)

target_include_directories(icu_monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TELEMETRY_PROTO_BINARY_DIR}
)

target_link_libraries(icu_monitor PRIVATE
    raylib
    gRPC::grpc++
    protobuf::libprotobuf
    spdlog::spdlog
)

target_compile_options(icu_monitor PRIVATE ${SIGNALS_WARN_FLAGS})

# Suppress warnings from generated protobuf code.
set_source_files_properties(
    ${TELEMETRY_PROTO_SOURCES}
    PROPERTIES COMPILE_FLAGS "-w"
)

# On Windows, raylib needs these system libs.
if(WIN32)
    target_link_libraries(icu_monitor PRIVATE opengl32 gdi32 winmm)
endif()

# ---------------------------------------------------------------------------
# client_tests — no Raylib; tests data-logic only
# ---------------------------------------------------------------------------
add_executable(client_tests
    tests/render_buffers_tests.cpp
    tests/timeline_tests.cpp
    tests/grpc_client_tests.cpp
    tests/navigation_tests.cpp
    # Source files under test (no Raylib)
    rendering/render_buffers.cpp
    timeline/playback_state.cpp
    timeline/timeline_controller.cpp
)

target_include_directories(client_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TELEMETRY_PROTO_BINARY_DIR}
)

target_link_libraries(client_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    protobuf::libprotobuf
)
target_compile_options(client_tests PRIVATE ${SIGNALS_WARN_FLAGS})

include(GoogleTest)
gtest_discover_tests(client_tests)
