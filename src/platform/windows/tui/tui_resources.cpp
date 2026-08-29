#include "tui_resources.hpp"

#include <exception>
#include <fstream>
#include <sstream>

namespace inputweaver::win32 {

bool LoadTuiColorScheme(
    const std::filesystem::path& executableDirectory,
    ui::tui::ColorScheme& colors,
    std::string& error)
{
    const std::filesystem::path path = executableDirectory
        / L"res" / L"InputWeaverTUI.colors.json";
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot open color scheme: " + path.string() + ".";
            return false;
        }
        std::ostringstream bytes;
        bytes << input.rdbuf();
        std::string parseError;
        if (!ui::tui::ParseColorScheme(bytes.str(), colors, parseError)) {
            error = "Invalid color scheme " + path.string() + ": "
                + parseError;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = "Cannot load color scheme " + path.string() + ": "
            + exception.what();
        return false;
    }
}

} // namespace inputweaver::win32
