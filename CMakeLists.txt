cmake_minimum_required(VERSION 3.20)
project(signals_study C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Point CMake at the MSYS2 UCRT64 prefix so find_package locates everything.
list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")

# ---------------------------------------------------------------------------
# Sanitizer options (mutually exclusive)
# ---------------------------------------------------------------------------
option(ENABLE_ASAN   "Enable AddressSanitizer + UBSan" OFF)
option(ENABLE_TSAN   "Enable ThreadSanitizer"          OFF)
option(ENABLE_WERROR "Treat warnings as errors (CI)"   OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive")
endif()
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# Shared warning flags — applied per-target in subdirectories.
set(SIGNALS_WARN_FLAGS
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference
    $<$<BOOL:${ENABLE_WERROR}>:-Werror>
)

# ---------------------------------------------------------------------------
# Dependencies — all from MSYS2 UCRT64
# ---------------------------------------------------------------------------
find_package(GTest    REQUIRED)
find_package(spdlog   REQUIRED)
find_package(gRPC     REQUIRED)
find_package(Protobuf REQUIRED)
find_package(SQLite3  REQUIRED)  # target: SQLite3::SQLite3
find_package(raylib   REQUIRED)

# moodycamel ConcurrentQueue — header-only.
# MSYS2 installs it as <concurrentqueue/moodycamel/concurrentqueue.h>.
# We point the include root at the moodycamel/ subdir so source files
# can keep #include <concurrentqueue.h>.
find_path(MOODYCAMEL_INCLUDE_DIR concurrentqueue.h
    REQUIRED
    HINTS "C:/msys64/ucrt64/include/concurrentqueue/moodycamel"
)
add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE ${MOODYCAMEL_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# telemetry_proto — shared protobuf/gRPC generated library.
# Compiled once here at root so both consumer and client can link against it
# without re-running protoc or creating duplicate object files.
# ---------------------------------------------------------------------------
set(PROTO_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/consumer/grpc)
set(PROTO_BINARY ${CMAKE_CURRENT_BINARY_DIR}/proto_gen)
file(MAKE_DIRECTORY ${PROTO_BINARY})

set(PROTO_SRCS ${PROTO_BINARY}/telemetry.pb.cc)
set(PROTO_HDRS ${PROTO_BINARY}/telemetry.pb.h)
set(GRPC_SRCS  ${PROTO_BINARY}/telemetry.grpc.pb.cc)
set(GRPC_HDRS  ${PROTO_BINARY}/telemetry.grpc.pb.h)

add_custom_command(
    OUTPUT  ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND protobuf::protoc
    ARGS
        --grpc_out=${PROTO_BINARY}
        --cpp_out=${PROTO_BINARY}
        "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
        -I${PROTO_DIR}
        ${PROTO_DIR}/telemetry.proto
    DEPENDS ${PROTO_DIR}/telemetry.proto protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "Generating telemetry protobuf/gRPC sources"
)

add_library(telemetry_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(telemetry_proto PUBLIC ${PROTO_BINARY})
target_link_libraries(telemetry_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
set_source_files_properties(${PROTO_SRCS} ${GRPC_SRCS} PROPERTIES COMPILE_FLAGS "-w")

# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(generator)
add_subdirectory(consumer)
add_subdirectory(client)
