cmake_minimum_required(VERSION 3.20)
project(signals_study C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LTO in Release builds
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Let MSYS2 packages be found automatically when building with MSYS2 toolchain
if(WIN32 AND EXISTS "C:/msys64/ucrt64")
    list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
endif()

# --------------------------------------------------------------------------
# Sanitizer options (mutually exclusive — TSan is preferred for this project)
# --------------------------------------------------------------------------
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

# --------------------------------------------------------------------------
# Common warning flags (applied per-target in subdirectories, not globally,
# to avoid polluting FetchContent deps)
# --------------------------------------------------------------------------
set(SIGNALS_WARN_FLAGS
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference
    $<$<BOOL:${ENABLE_WERROR}>:-Werror>
)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------- GoogleTest ---------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK            ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------- spdlog -------------------------------------------------------
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found via find_package — fetching via FetchContent")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.3
        GIT_SHALLOW    TRUE
    )
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

# ---------- moodycamel ConcurrentQueue (header-only) --------------------
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        v1.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(concurrentqueue)
add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE ${concurrentqueue_SOURCE_DIR})

# ---------- Raylib -------------------------------------------------------
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    message(STATUS "raylib not found via find_package — fetching via FetchContent")
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    set(BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES     OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

# ---------- gRPC + Protobuf ----------------------------------------------
# Preferred: install via MSYS2 first:
#   pacman -S mingw-w64-ucrt-x86_64-grpc
# Fallback: FetchContent (first build ~10-15 min)
option(SIGNALS_USE_FETCHCONTENT_GRPC
    "Force FetchContent for gRPC even if system package exists" OFF)

if(NOT SIGNALS_USE_FETCHCONTENT_GRPC)
    find_package(gRPC QUIET)
    find_package(Protobuf QUIET)
endif()

if(NOT gRPC_FOUND OR NOT Protobuf_FOUND)
    message(STATUS "gRPC/Protobuf not found — fetching via FetchContent (this takes 10-15 min)")
    FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG        v1.68.1
        GIT_SHALLOW    TRUE
    )
    set(gRPC_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT     OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL       OFF CACHE BOOL "" FORCE)
    set(RE2_BUILD_TESTING         OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(grpc)
    # When built via FetchContent, use these target names:
    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++           ALIAS grpc++)
        add_library(gRPC::grpc++_reflection ALIAS grpc++_reflection)
    endif()
    if(NOT TARGET protobuf::libprotobuf)
        add_library(protobuf::libprotobuf  ALIAS libprotobuf)
        add_library(protobuf::protoc       ALIAS protoc)
    endif()
    # protoc plugin path
    if(NOT TARGET gRPC::grpc_cpp_plugin)
        add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
    endif()
endif()

# ---------- SQLite3 ------------------------------------------------------
# Try system first (MSYS2 has it: mingw-w64-ucrt-x86_64-sqlite3)
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(STATUS "SQLite3 not found — fetching amalgamation via FetchContent")
    FetchContent_Declare(
        sqlite3_src
        URL      https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip
    )
    FetchContent_MakeAvailable(sqlite3_src)
    add_library(SQLite3_local STATIC ${sqlite3_src_SOURCE_DIR}/sqlite3.c)
    target_include_directories(SQLite3_local PUBLIC ${sqlite3_src_SOURCE_DIR})
    target_compile_definitions(SQLite3_local PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=0
        SQLITE_DEFAULT_PAGE_SIZE=8192
    )
    if(WIN32)
        target_link_libraries(SQLite3_local PRIVATE ws2_32)
    endif()
    add_library(SQLite::SQLite3 ALIAS SQLite3_local)
endif()

# --------------------------------------------------------------------------
# Enable CTest
# --------------------------------------------------------------------------
enable_testing()

# --------------------------------------------------------------------------
# Subdirectories
# --------------------------------------------------------------------------
add_subdirectory(generator)
add_subdirectory(consumer)
add_subdirectory(client)
