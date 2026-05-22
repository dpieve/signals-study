#pragma once
#include <raylib.h>
#include <string>

/// Render the top status bar (patient ID, date/time, profile).
///
/// Visual style: thin dark navy strip, compact white text, subtle separators —
/// matching Philips IntelliVue / GE CARESCAPE conventions.
///
/// @param rect          Full-width bar rectangle
/// @param patient_id    e.g. "ICU_SIM_01"
/// @param profile_name  e.g. "Adult"
void draw_status_bar(Rectangle          rect,
                     const std::string& patient_id,
                     const std::string& profile_name);
