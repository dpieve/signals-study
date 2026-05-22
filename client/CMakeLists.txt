cmake_minimum_required(VERSION 3.20)
project(client CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# telemetry_proto (defined in root CMakeLists.txt) provides the generated
# headers and compiled proto objects shared with consumer.

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
)
target_include_directories(icu_monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(icu_monitor PRIVATE
    telemetry_proto
    raylib
    spdlog::spdlog
    opengl32 gdi32 winmm
)
target_compile_options(icu_monitor PRIVATE ${SIGNALS_WARN_FLAGS})
# On Windows, grpc pulls in windows.h which declares Rectangle() as a GDI
# function and CloseWindow/ShowCursor as WinUser functions — all of which
# clash with raylib's typedefs.  NOGDI/NOUSER suppress those declarations so
# raylib's versions are used.  The compiled libraylib.a is unaffected.
if(WIN32)
    target_compile_definitions(icu_monitor PRIVATE NOGDI NOUSER)
endif()

# ---------------------------------------------------------------------------
# client_tests — no Raylib; tests data-logic only
# ---------------------------------------------------------------------------
add_executable(client_tests
    tests/render_buffers_tests.cpp
    tests/timeline_tests.cpp
    tests/grpc_client_tests.cpp
    tests/navigation_tests.cpp
    rendering/render_buffers.cpp
    timeline/playback_state.cpp
    timeline/timeline_controller.cpp
)
target_include_directories(client_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_tests PRIVATE GTest::gtest_main GTest::gmock)
target_compile_options(client_tests PRIVATE ${SIGNALS_WARN_FLAGS})

include(GoogleTest)
gtest_discover_tests(client_tests DISCOVERY_MODE PRE_TEST)
