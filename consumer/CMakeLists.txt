cmake_minimum_required(VERSION 3.20)
project(consumer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# telemetry_proto (defined in root CMakeLists.txt) provides the generated
# headers and compiled proto objects.  No local protoc invocation needed.

# ---------------------------------------------------------------------------
# consumer_lib
# ---------------------------------------------------------------------------
add_library(consumer_lib STATIC
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
    ${CMAKE_SOURCE_DIR}/generator
)
target_link_libraries(consumer_lib PUBLIC
    generator
    telemetry_proto
    gRPC::grpc++_reflection
    SQLite3::SQLite3
    spdlog::spdlog
    moodycamel
)
target_compile_options(consumer_lib PRIVATE ${SIGNALS_WARN_FLAGS})

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
gtest_discover_tests(consumer_tests DISCOVERY_MODE PRE_TEST)
