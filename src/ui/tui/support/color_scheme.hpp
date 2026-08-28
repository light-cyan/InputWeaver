#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace inputweaver::ui::tui {

struct RgbColor final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};

    auto operator<=>(const RgbColor&) const = default;
};

struct ColorScheme final {
    RgbColor text{};
    RgbColor mutedText{};
    RgbColor unfocusedBorder{};
    RgbColor focusConsole{};
    RgbColor focusPrograms{};
    RgbColor focusProgramInformation{};
    RgbColor focusCompiledDump{};
    RgbColor focusEvents{};
    RgbColor focusPressed{};
    RgbColor focusActionExecutions{};
    RgbColor selectionActiveForeground{};
    RgbColor selectionActiveBackground{};
    RgbColor selectionInactiveForeground{};
    RgbColor selectionInactiveBackground{};
    RgbColor statusRunning{};
    RgbColor statusDebug{};
    RgbColor statusDryRun{};
    RgbColor statusExecPermission{};
    RgbColor executionRunning{};
    RgbColor executionCompleted{};
    RgbColor executionFailed{};
    RgbColor executionCancelled{};
    RgbColor instructionRecentOldest{};
    RgbColor instructionRecent{};
    RgbColor instructionCurrent{};
    RgbColor healthTrusted{};
    RgbColor healthRecovering{};
    RgbColor healthFault{};
};

[[nodiscard]] bool ParseColorScheme(
    std::string_view json,
    ColorScheme& scheme,
    std::string& error);

} // namespace inputweaver::ui::tui
