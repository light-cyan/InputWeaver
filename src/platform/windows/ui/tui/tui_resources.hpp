#pragma once

#include "ui/tui/support/color_scheme.hpp"

#include <filesystem>
#include <string>

namespace inputweaver::win32 {

[[nodiscard]] bool LoadTuiColorScheme(
    const std::filesystem::path& executableDirectory,
    ui::tui::ColorScheme& colors,
    std::string& error);

} // namespace inputweaver::win32
