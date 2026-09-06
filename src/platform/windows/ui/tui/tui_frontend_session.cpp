#include "tui_frontend_session.hpp"

#include "platform/windows/support/command_line.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <utility>
#include <vector>

namespace inputweaver::win32 {
namespace {

inline constexpr ULONGLONG kFrontendStartupTimeoutMilliseconds = 5'000U;
inline constexpr ULONGLONG kFrontendHeartbeatTimeoutMilliseconds = 2'000U;
inline constexpr DWORD kFrontendStopTimeoutMilliseconds = 500U;

struct PipePair final {
    UniqueHandle read;
    UniqueHandle write;
};

class ProcessAttributeList final {
public:
    ~ProcessAttributeList()
    {
        if (initialized_) {
            DeleteProcThreadAttributeList(list_);
        }
    }

    ProcessAttributeList(const ProcessAttributeList&) = delete;
    ProcessAttributeList& operator=(const ProcessAttributeList&) = delete;

    ProcessAttributeList() = default;

    [[nodiscard]] bool Initialize(
        std::array<HANDLE, 2U>& handles,
        std::string& error)
    {
        SIZE_T bytes{};
        (void)InitializeProcThreadAttributeList(
            nullptr,
            1U,
            0U,
            &bytes);
        if (bytes == 0U) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        try {
            storage_.resize(bytes);
        } catch (...) {
            error = "Cannot allocate the frontend process attribute list.";
            return false;
        }
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            storage_.data());
        if (InitializeProcThreadAttributeList(
                list_,
                1U,
                0U,
                &bytes) == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        initialized_ = true;
        if (UpdateProcThreadAttribute(
                list_,
                0U,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                handles.data(),
                sizeof(HANDLE) * handles.size(),
                nullptr,
                nullptr) == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        return true;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept
    {
        return list_;
    }

private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
    bool initialized_{};
};

[[nodiscard]] bool CreateChildPipe(
    PipePair& pipe,
    bool childReads,
    std::string& error)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read{};
    HANDLE write{};
    if (CreatePipe(&read, &write, &security, 4U * 1024U * 1024U) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    pipe.read.Reset(read);
    pipe.write.Reset(write);
    HANDLE parentHandle = childReads ? pipe.write.Get() : pipe.read.Get();
    if (SetHandleInformation(parentHandle, HANDLE_FLAG_INHERIT, 0U) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    DWORD mode = PIPE_NOWAIT;
    if (SetNamedPipeHandleState(
            pipe.write.Get(),
            &mode,
            nullptr,
            nullptr) == FALSE) {
        error = std::system_category().message(
            static_cast<int>(GetLastError()));
        return false;
    }
    return true;
}

[[nodiscard]] std::wstring HandleArgument(HANDLE handle)
{
    return std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
}

} // namespace

TuiFrontendSession::TuiFrontendSession(
    std::filesystem::path executableDirectory)
    : executableDirectory_(std::move(executableDirectory))
{
}

TuiFrontendSession::~TuiFrontendSession()
{
    Hide();
}

bool TuiFrontendSession::Show(std::string& error)
{
    if (Visible()) {
        const ULONGLONG now = GetTickCount64();
        const bool starting = (!ready_ || window_ == nullptr)
            && now - startedAt_ < kFrontendStartupTimeoutMilliseconds;
        const bool responsive = ready_ && window_ != nullptr
            && IsWindow(window_) != FALSE
            && now - lastHeartbeatAt_
                < kFrontendHeartbeatTimeoutMilliseconds;
        if (responsive) {
            ShowWindowAsync(window_, SW_RESTORE);
            BringWindowToTop(window_);
            SetForegroundWindow(window_);
            return true;
        }
        if (starting) {
            return true;
        }
        Hide();
    }

    const std::filesystem::path frontend = executableDirectory_
        / L"InputWeaverTUI.exe";
    if (!std::filesystem::is_regular_file(frontend)) {
        error = "InputWeaverTUI.exe is missing from the application directory.";
        return false;
    }

    PipePair hostToChild;
    PipePair childToHost;
    if (!CreateChildPipe(hostToChild, true, error)
        || !CreateChildPipe(childToHost, false, error)) {
        return false;
    }

    const std::vector<std::wstring> arguments{
        frontend.wstring(),
        L"--read-handle",
        HandleArgument(hostToChild.read.Get()),
        L"--write-handle",
        HandleArgument(childToHost.write.Get())};
    std::wstring commandLine = BuildCommandLine(arguments);

    std::array<HANDLE, 2U> inheritedHandles{
        hostToChild.read.Get(),
        childToHost.write.Get()};
    ProcessAttributeList attributes;
    if (!attributes.Initialize(inheritedHandles, error)) {
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes.Get();
    PROCESS_INFORMATION information{};
    if (CreateProcessW(
            frontend.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT
                | EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            executableDirectory_.c_str(),
            &startup.StartupInfo,
            &information) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    UniqueHandle thread(information.hThread);
    process_.Reset(information.hProcess);
    hostToChild.read.Reset();
    childToHost.write.Reset();
    channel_.Reset(
        std::move(childToHost.read),
        std::move(hostToChild.write));
    width_ = kMinimumTuiColumns;
    height_ = kMinimumTuiRows;
    window_ = nullptr;
    startedAt_ = GetTickCount64();
    lastHeartbeatAt_ = startedAt_;
    ready_ = false;
    return true;
}

void TuiFrontendSession::Hide() noexcept
{
    if (!process_) {
        Reset();
        return;
    }
    std::string ignored;
    const std::span<const std::uint8_t> empty;
    (void)channel_.Send(TuiIpcMessageType::Close, empty, ignored);
    if (WaitForSingleObject(
            process_.Get(),
            kFrontendStopTimeoutMilliseconds) != WAIT_OBJECT_0) {
        (void)TerminateProcess(process_.Get(), 0U);
        (void)WaitForSingleObject(
            process_.Get(),
            kFrontendStopTimeoutMilliseconds);
    }
    Reset();
}

bool TuiFrontendSession::Poll(
    std::vector<ui::tui::KeyEvent>& events,
    bool& backgroundRequested,
    std::string& error) noexcept
{
    events.clear();
    backgroundRequested = false;
    if (!Visible()) {
        return true;
    }

    std::vector<TuiIpcMessage> messages;
    bool disconnected{};
    if (!channel_.Poll(messages, disconnected, error)) {
        Hide();
        return false;
    }
    for (const TuiIpcMessage& message : messages) {
        switch (message.type) {
        case TuiIpcMessageType::Key: {
            ui::tui::KeyEvent event{};
            if (!DecodeKeyEvent(message.payload, event)) {
                error = "The TUI frontend sent an invalid key event.";
                Hide();
                return false;
            }
            events.push_back(event);
            break;
        }
        case TuiIpcMessageType::Resize:
            if (!DecodeViewportSize(
                    message.payload,
                    width_,
                    height_)) {
                error = "The TUI frontend sent an invalid viewport size.";
                Hide();
                return false;
            }
            ready_ = true;
            break;
        case TuiIpcMessageType::Background:
            backgroundRequested = true;
            break;
        case TuiIpcMessageType::Window: {
            std::uintptr_t window{};
            if (!DecodeWindowHandle(message.payload, window)) {
                error = "The TUI frontend sent an invalid window handle.";
                Hide();
                return false;
            }
            window_ = reinterpret_cast<HWND>(window);
            break;
        }
        case TuiIpcMessageType::Heartbeat:
            if (!message.payload.empty()) {
                error = "The TUI frontend sent an invalid heartbeat.";
                Hide();
                return false;
            }
            lastHeartbeatAt_ = GetTickCount64();
            break;
        case TuiIpcMessageType::Frame:
        case TuiIpcMessageType::Close:
            error = "The TUI frontend sent a message in the wrong direction.";
            Hide();
            return false;
        }
    }
    if (disconnected) {
        Hide();
    } else if (WaitForSingleObject(process_.Get(), 0U) == WAIT_OBJECT_0) {
        Reset();
    } else if ((!ready_ || window_ == nullptr)
        && GetTickCount64() - startedAt_
            >= kFrontendStartupTimeoutMilliseconds) {
        error = "The TUI frontend did not expose its window within five seconds.";
        Hide();
        return false;
    } else if (ready_ && window_ != nullptr
        && GetTickCount64() - lastHeartbeatAt_
            >= kFrontendHeartbeatTimeoutMilliseconds) {
        error = "The TUI frontend stopped responding.";
        Hide();
        return false;
    }
    return true;
}

bool TuiFrontendSession::SendFrame(
    const ui::tui::Canvas& canvas,
    std::string& error) noexcept
{
    if (!Visible() || !ready_ || window_ == nullptr) {
        return true;
    }
    try {
        const std::vector<std::uint8_t> payload = EncodeTuiFrame(canvas);
        if (payload == lastFramePayload_) {
            return true;
        }
        if (!channel_.Send(TuiIpcMessageType::Frame, payload, error)) {
            Hide();
            return false;
        }
        lastFramePayload_ = payload;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        Hide();
        return false;
    }
}

bool TuiFrontendSession::Visible() noexcept
{
    if (!process_) {
        return false;
    }
    if (WaitForSingleObject(process_.Get(), 0U) != WAIT_TIMEOUT) {
        Reset();
        return false;
    }
    return true;
}

std::size_t TuiFrontendSession::Width() const noexcept
{
    return width_;
}

std::size_t TuiFrontendSession::Height() const noexcept
{
    return height_;
}

void TuiFrontendSession::Reset() noexcept
{
    channel_.Reset();
    process_.Reset();
    window_ = nullptr;
    lastFramePayload_.clear();
    startedAt_ = 0U;
    lastHeartbeatAt_ = 0U;
    ready_ = false;
}

} // namespace inputweaver::win32
