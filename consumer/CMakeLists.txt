cmake_minimum_required(VERSION 3.20)
project(consumer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies — skip find_package if targets already exist (from root's
# FetchContent or a parent find_package call).
# ---------------------------------------------------------------------------
if(NOT TARGET gRPC::grpc++)
    find_package(gRPC REQUIRED)
endif()
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()
if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 REQUIRED)
endif()
if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()
if(NOT TARGET GTest::gtest_main)
    find_package(GTest REQUIRED)
endif()
# moodycamel is created as INTERFACE target by root CMakeLists.txt
# Nothing to do here if it exists.

# ---------------------------------------------------------------------------
# Proto / gRPC code generation
# ---------------------------------------------------------------------------
set(PROTO_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/grpc/telemetry.proto)
set(PROTO_SRCS   ${CMAKE_CURRENT_BINARY_DIR}/telemetry.pb.cc)
set(PROTO_HDRS   ${CMAKE_CURRENT_BINARY_DIR}/telemetry.pb.h)
set(GRPC_SRCS    ${CMAKE_CURRENT_BINARY_DIR}/telemetry.grpc.pb.cc)
set(GRPC_HDRS    ${CMAKE_CURRENT_BINARY_DIR}/telemetry.grpc.pb.h)

add_custom_command(
    OUTPUT  ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND protobuf::protoc
    ARGS
        --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
        -I${CMAKE_CURRENT_SOURCE_DIR}/grpc
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE} protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "Generating telemetry protobuf/gRPC sources (consumer)"
)

# Export the generated include dir so client can use the same generated headers.
set(TELEMETRY_PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
set(TELEMETRY_PROTO_SOURCES ${PROTO_SRCS} ${GRPC_SRCS} PARENT_SCOPE)

# ---------------------------------------------------------------------------
# consumer_lib — static library shared by the binary and the tests
# ---------------------------------------------------------------------------
add_library(consumer_lib STATIC
    ${PROTO_SRCS}
    ${GRPC_SRCS}
    ingestion/packet_queue.cpp
    ingestion/ingestion_manager.cpp
    persistence/sqlite_writer.cpp
    persistence/persistence_events.cpp
    processing/signal_metrics.cpp
    processing/processing_engine.cpp
    grpc/telemetry_service.cpp
    grpc/grpc_broadcaster.cpp
    app/app.cpp
)

target_include_directories(consumer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/generator
)

target_link_libraries(consumer_lib PUBLIC
    generator
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
    SQLite::SQLite3
    spdlog::spdlog
    moodycamel
)

target_compile_options(consumer_lib PRIVATE ${SIGNALS_WARN_FLAGS})

# Suppress warnings from generated protobuf code.
set_source_files_properties(
    ${PROTO_SRCS} ${GRPC_SRCS}
    PROPERTIES COMPILE_FLAGS "-w"
)

# ---------------------------------------------------------------------------
# consumer executable
# ---------------------------------------------------------------------------
add_executable(consumer app/main.cpp)
target_link_libraries(consumer PRIVATE consumer_lib)
target_compile_options(consumer PRIVATE ${SIGNALS_WARN_FLAGS})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(consumer_tests
    tests/ingestion_tests.cpp
    tests/queue_tests.cpp
    tests/persistence_tests.cpp
    tests/processing_tests.cpp
    tests/grpc_tests.cpp
    tests/replay_tests.cpp
)

target_link_libraries(consumer_tests PRIVATE
    consumer_lib
    GTest::gtest_main
    GTest::gmock
)
target_compile_options(consumer_tests PRIVATE ${SIGNALS_WARN_FLAGS})

include(GoogleTest)
gtest_discover_tests(consumer_tests)
