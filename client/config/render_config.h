#pragma once
#include <raylib.h>
#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// ICU monitor color conventions (matches real monitor standards)
// ---------------------------------------------------------------------------
inline constexpr Color WAVEFORM_COLOR_ECG     = {0,   255, 0,   255}; // bright green
inline constexpr Color WAVEFORM_COLOR_BP      = {255, 50,  50,  255}; // red
inline constexpr Color WAVEFORM_COLOR_CO2     = {255, 255, 0,   255}; // yellow
inline constexpr Color WAVEFORM_COLOR_RESP    = {100, 200, 255, 255}; // light blue
inline constexpr Color WAVEFORM_COLOR_DEFAULT = {200, 200, 200, 255}; // gray

// ---------------------------------------------------------------------------
// Background and panel colors
// ---------------------------------------------------------------------------
inline constexpr Color MONITOR_BG        = {10,  12,  18,  255}; // near black
inline constexpr Color STATUS_BAR_BG     = {15,  20,  35,  255}; // dark navy
inline constexpr Color ACTION_BAR_BG     = {20,  30,  55,  255}; // dark blue
inline constexpr Color NUMERIC_PANEL_BG  = {12,  15,  25,  255}; // dark
inline constexpr Color GRID_COLOR        = {30,  40,  30,  60};  // subtle green grid

// ---------------------------------------------------------------------------
// Layout fractions and dimensions
// ---------------------------------------------------------------------------
inline constexpr float STATUS_BAR_HEIGHT        = 45.0f;
inline constexpr float ACTION_BAR_HEIGHT        = 50.0f;
inline constexpr float NUMERIC_PANEL_WIDTH_FRAC = 0.22f; // 22 % of window width

// Circular waveform buffer size is defined in render_buffers.h to keep
// that header free of raylib dependencies (needed by tests).
// Include render_buffers.h to get WAVEFORM_BUFFER_SIZE.
