#include "app/application.hpp"
#include "platform/windows/app/windows_app_platform.hpp"
#include "platform/windows/tui/windows_terminal.hpp"
#include "ui/tui/tui_controller.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory()
{
    std::vector<wchar_t> buffer(512U);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            return std::filesystem::current_path();
        }
        if (length < buffer.size() - 1U) {
            return std::filesystem::path{
                std::wstring_view{buffer.data(), length}}.parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

} // namespace

int wmain()
{
    try {
        const std::filesystem::path executableDirectory =
            ExecutableDirectory();
        inputweaver::ui::tui::ColorScheme colors{};
        std::string error;
        if (!inputweaver::win32::LoadTuiColorScheme(
                executableDirectory,
                colors,
                error)) {
            std::cerr << "Error: " << error << '\n';
            return 2;
        }
        inputweaver::win32::WindowsAppPlatform platform(executableDirectory);
        inputweaver::app::Application application(platform);
        const inputweaver::app::OperationResult initialized =
            application.Initialize();
        if (!initialized.succeeded) {
            std::cerr << "Error: " << initialized.error << '\n';
            return 3;
        }
        inputweaver::win32::WindowsTerminal terminal;
        if (!terminal.Initialize(error)) {
            std::cerr << "Error: " << error << '\n';
            return 4;
        }
        inputweaver::ui::tui::TuiController controller(
            application,
            colors);
        while (controller.Running()) {
            controller.Tick();
            const auto [width, height] = terminal.Size();
            const inputweaver::ui::tui::Canvas canvas = controller.Render(
                width,
                height);
            if (!terminal.Draw(canvas, error)) {
                return 5;
            }
            inputweaver::ui::tui::KeyEvent event{};
            bool resized{};
            if (terminal.Poll(event, 50U, resized)) {
                controller.Handle(event);
            }
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 6;
    }
}
