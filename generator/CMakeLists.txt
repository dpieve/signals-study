cmake_minimum_required(VERSION 3.20)
project(generator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(generator STATIC
    generator.c
    generator_simulations.c
)
target_include_directories(generator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(generator PRIVATE -Wall -Wextra -Wpedantic)

# Link math and pthread; on MSYS2/MinGW these are part of the runtime,
# but explicit linking avoids surprises on Linux.
if(UNIX)
    target_link_libraries(generator PUBLIC m pthread)
endif()

# The C wrapper called from C++ (in the generator callback) must not throw.
set_source_files_properties(generator.c generator_simulations.c
    PROPERTIES COMPILE_FLAGS "-fno-exceptions")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NOT TARGET GTest::gtest_main)
    find_package(GTest REQUIRED)
endif()

add_executable(generator_tests
    tests/generator_tests.cpp
    tests/timing_tests.cpp
    tests/simulation_tests.cpp
)
target_link_libraries(generator_tests PRIVATE
    generator
    GTest::gtest_main
    GTest::gmock
)
target_compile_options(generator_tests PRIVATE -Wall -Wextra)

include(GoogleTest)
gtest_discover_tests(generator_tests)
