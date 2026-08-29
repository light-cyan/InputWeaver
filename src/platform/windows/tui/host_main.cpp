#include "app/application.hpp"
#include "platform/windows/app/windows_app_platform.hpp"
#include "platform/windows/support/text_encoding.hpp"
#include "platform/windows/tui/tui_frontend_session.hpp"
#include "platform/windows/tui/tui_resources.hpp"
#include "platform/windows/tui/windows_clipboard.hpp"
#include "platform/windows/tui/windows_tray.hpp"
#include "ui/tui/tui_controller.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
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

void ShowError(std::string_view error) noexcept
{
    std::wstring message;
    if (!inputweaver::win32::Utf8ToWide(error, message)) {
        message = L"InputWeaver encountered an error.";
    }
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"InputWeaver",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand)
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
            ShowError(error);
            return 2;
        }

        inputweaver::win32::WindowsAppPlatform platform(executableDirectory);
        inputweaver::app::Application application(platform);
        const inputweaver::app::OperationResult initialized =
            application.Initialize();
        if (!initialized.succeeded) {
            ShowError(initialized.error);
            return 3;
        }

        inputweaver::win32::WindowsTray tray;
        if (!tray.Initialize(error)) {
            ShowError(error);
            return 4;
        }
        inputweaver::ui::tui::TuiController controller(
            application,
            colors);
        inputweaver::win32::TuiFrontendSession frontend(
            executableDirectory);
        if (showCommand != SW_HIDE && !frontend.Show(error)) {
            tray.NotifyError(error);
        }
        tray.SetFrontendVisible(frontend.Visible());

        while (controller.Running()) {
            controller.Tick();
            switch (tray.Poll()) {
            case inputweaver::win32::TrayAction::None:
                break;
            case inputweaver::win32::TrayAction::Show:
                error.clear();
                if (!frontend.Show(error)) {
                    tray.NotifyError(error);
                }
                break;
            case inputweaver::win32::TrayAction::Hide:
                frontend.Hide();
                break;
            case inputweaver::win32::TrayAction::Exit:
                if (controller.RequestExit()) {
                    frontend.Hide();
                } else {
                    error.clear();
                    if (!frontend.Show(error)) {
                        tray.NotifyError(error);
                    }
                }
                break;
            }
            if (!controller.Running()) {
                break;
            }

            std::vector<inputweaver::ui::tui::KeyEvent> events;
            bool frontendBackgroundRequested{};
            error.clear();
            if (!frontend.Poll(
                    events,
                    frontendBackgroundRequested,
                    error)) {
                tray.NotifyError(error);
            }
            for (const auto& event : events) {
                controller.Handle(event);
            }
            if (const auto text = controller.TakeClipboardText();
                text.has_value()) {
                error.clear();
                if (!inputweaver::win32::CopyTextToClipboard(
                        *text,
                        error)) {
                    tray.NotifyError(error);
                }
            }
            if (frontendBackgroundRequested
                || controller.TakeBackgroundRequest()) {
                frontend.Hide();
            }
            if (!controller.Running()) {
                break;
            }

            if (frontend.Visible()) {
                const inputweaver::ui::tui::Canvas canvas =
                    controller.Render(frontend.Width(), frontend.Height());
                error.clear();
                if (!frontend.SendFrame(canvas, error)) {
                    tray.NotifyError(error);
                    frontend.Hide();
                }
            }
            tray.SetFrontendVisible(frontend.Visible());
            Sleep(50U);
        }
        return 0;
    } catch (const std::exception& exception) {
        ShowError(exception.what());
        return 5;
    }
}
