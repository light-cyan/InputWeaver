#include "windows_app_platform.hpp"

#include "child_process.hpp"
#include "platform/windows/debug/debug_client.hpp"
#include "platform/windows/support/text_encoding.hpp"
#include "program_library.hpp"
#include "support/utf8.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace inputweaver::win32 {
namespace {

inline constexpr DWORD kExecutorStopTimeoutMilliseconds = 5'000U;
inline constexpr auto kDebugLaunchDelay = std::chrono::milliseconds{500};

[[nodiscard]] std::string DebugClientErrorText(debug::DebugClientError error)
{
    switch (error) {
    case debug::DebugClientError::None:
        return "None";
    case debug::DebugClientError::InvalidProcessIdentity:
        return "Invalid process identity";
    case debug::DebugClientError::InvalidDebugToken:
        return "Invalid debug token";
    case debug::DebugClientError::ProcessUnavailable:
        return "Executor process unavailable";
    case debug::DebugClientError::PipeUnavailable:
        return "Debug pipe unavailable";
    case debug::DebugClientError::ProcessMismatch:
        return "Debug pipe process mismatch";
    case debug::DebugClientError::HandshakeFailed:
        return "Debug handshake failed";
    case debug::DebugClientError::ProtocolRejected:
        return "Debug protocol rejected";
    case debug::DebugClientError::NotConnected:
        return "Debug client is not connected";
    case debug::DebugClientError::IoFailure:
        return "Debug pipe I/O failed";
    case debug::DebugClientError::AllocationFailure:
        return "Debug client allocation failed";
    }
    return "Unknown debug client error";
}

[[nodiscard]] std::string NormalizeOutput(std::string_view source)
{
    if (support::IsValidUtf8(source)) {
        return std::string{source};
    }
    if (source.size() > static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {
        return "[output encoding error]";
    }
    const int required = MultiByteToWideChar(
        CP_ACP,
        0U,
        source.data(),
        static_cast<int>(source.size()),
        nullptr,
        0);
    if (required <= 0) {
        return "[output encoding error]";
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_ACP,
            0U,
            source.data(),
            static_cast<int>(source.size()),
            wide.data(),
            required) != required) {
        return "[output encoding error]";
    }
    std::string result;
    return WideToUtf8(wide, result) ? result : "[output encoding error]";
}

[[nodiscard]] std::string MakeDebugToken()
{
    static std::atomic<std::uint64_t> sequence{0U};
    return "tui-" + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(GetTickCount64()) + "-"
        + std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] DWORD InteractiveHostProcessId() noexcept
{
    DWORD processId{};
    const HWND foreground = GetForegroundWindow();
    if (foreground != nullptr) {
        (void)GetWindowThreadProcessId(foreground, &processId);
    }
    return processId == 0U ? GetCurrentProcessId() : processId;
}

void RemoveTemporary(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}

} // namespace

struct WindowsAppPlatform::Impl final {
    struct ManagedExecutor final {
        app::ExecutorInfo info{};
        std::string programName;
        ChildProcess child;
        std::thread outputReader;
        std::thread errorReader;
        std::unique_ptr<WindowsDebugClient> debugClient;
    };

    explicit Impl(std::filesystem::path executableDirectory)
        : library(std::move(executableDirectory))
    {
    }

    void QueueEvent(app::PlatformEvent event)
    {
        std::lock_guard lock(eventMutex);
        events.push_back(std::move(event));
    }

    void QueueOutput(
        app::ProgramEntryId id,
        std::string_view name,
        app::ConsoleSource source,
        std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        QueueEvent({
            app::PlatformEventKind::Output,
            id,
            std::string{name},
            source,
            NormalizeOutput(text),
            0U});
    }

    void ReadExecutorOutput(
        HANDLE handle,
        app::ProgramEntryId id,
        std::string name) noexcept
    {
        try {
            std::array<char, 4'096U> buffer{};
            std::string pending;
            for (;;) {
                DWORD read{};
                if (ReadFile(
                        handle,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &read,
                        nullptr) == FALSE
                    || read == 0U) {
                    break;
                }
                pending.append(buffer.data(), static_cast<std::size_t>(read));
                std::size_t lineEnd{};
                while ((lineEnd = pending.find('\n')) != std::string::npos) {
                    QueueOutput(
                        id,
                        name,
                        app::ConsoleSource::Runtime,
                        std::string_view{pending}.substr(0U, lineEnd + 1U));
                    pending.erase(0U, lineEnd + 1U);
                }
            }
            if (!pending.empty()) {
                QueueOutput(id, name, app::ConsoleSource::Runtime, pending);
            }
        } catch (...) {
            QueueEvent({
                app::PlatformEventKind::Error,
                id,
                std::move(name),
                app::ConsoleSource::Runtime,
                "Runtime output capture failed.",
                0U});
        }
    }

    [[nodiscard]] ManagedExecutor* FindExecutor(
        app::ProgramEntryId id) noexcept
    {
        const auto found = std::find_if(
            executors.begin(),
            executors.end(),
            [id](const std::unique_ptr<ManagedExecutor>& executor) {
                return executor->info.programId == id;
            });
        return found == executors.end() ? nullptr : found->get();
    }

    void StartOutputReaders(ManagedExecutor& executor)
    {
        executor.outputReader = std::thread(
            &Impl::ReadExecutorOutput,
            this,
            executor.child.standardOutput.Get(),
            executor.info.programId,
            executor.programName);
        executor.errorReader = std::thread(
            &Impl::ReadExecutorOutput,
            this,
            executor.child.standardError.Get(),
            executor.info.programId,
            executor.programName);
    }

    void FinishExecutor(std::unique_ptr<ManagedExecutor> executor)
    {
        if (WaitForSingleObject(executor->child.process.Get(), 0U)
            != WAIT_OBJECT_0) {
            (void)TerminateProcess(executor->child.process.Get(), 1U);
            (void)WaitForSingleObject(executor->child.process.Get(), INFINITE);
        }
        if (executor->debugClient != nullptr) {
            executor->debugClient->Disconnect();
        }
        if (executor->outputReader.joinable()) {
            executor->outputReader.join();
        }
        if (executor->errorReader.joinable()) {
            executor->errorReader.join();
        }
        DWORD exitCode{};
        if (GetExitCodeProcess(executor->child.process.Get(), &exitCode) == FALSE
            || exitCode == STILL_ACTIVE) {
            exitCode = 1U;
        }
        QueueEvent({
            app::PlatformEventKind::ExecutorExited,
            executor->info.programId,
            executor->programName,
            app::ConsoleSource::Runtime,
            {},
            exitCode});
    }

    void CollectExited()
    {
        std::vector<std::unique_ptr<ManagedExecutor>> exited;
        {
            std::lock_guard lock(executorMutex);
            for (std::size_t index = 0U; index < executors.size();) {
                if (WaitForSingleObject(
                        executors[index]->child.process.Get(),
                        0U) != WAIT_OBJECT_0) {
                    ++index;
                    continue;
                }
                if (debugProgramId == executors[index]->info.programId) {
                    debugProgramId = app::kInvalidProgramEntryId;
                }
                exited.push_back(std::move(executors[index]));
                executors.erase(
                    executors.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        for (auto& executor : exited) {
            FinishExecutor(std::move(executor));
        }
    }

    [[nodiscard]] app::OperationResult RequestStop(
        ManagedExecutor& executor) noexcept
    {
        if (executor.debugClient != nullptr) {
            const debug::DebugClientResult result =
                executor.debugClient->RequestExecutorStop();
            return result.Succeeded()
                ? app::OperationResult::Success()
                : app::OperationResult::Failure(
                    DebugClientErrorText(result.error));
        }
        if (GenerateConsoleCtrlEvent(
                CTRL_BREAK_EVENT,
                executor.child.processId) == FALSE) {
            return app::OperationResult::Failure(
                std::system_category().message(
                    static_cast<int>(GetLastError())));
        }
        return app::OperationResult::Success();
    }

    WindowsProgramLibrary library;
    mutable std::mutex executorMutex;
    std::vector<std::unique_ptr<ManagedExecutor>> executors;
    app::ProgramEntryId debugProgramId{app::kInvalidProgramEntryId};
    std::mutex eventMutex;
    std::deque<app::PlatformEvent> events;
};

WindowsAppPlatform::WindowsAppPlatform(
    std::filesystem::path executableDirectory)
    : impl_(std::make_unique<Impl>(std::move(executableDirectory)))
{
}

WindowsAppPlatform::~WindowsAppPlatform()
{
    StopAllExecutors();
}

app::LibraryLoadResult WindowsAppPlatform::LoadProgramLibrary()
{
    return impl_->library.Load();
}

app::ImportSourceInfo WindowsAppPlatform::InspectImportSource(
    std::string_view sourcePath) const
{
    return impl_->library.InspectSource(sourcePath);
}

bool WindowsAppPlatform::NamesEqual(
    std::string_view left,
    std::string_view right) const noexcept
{
    return impl_->library.NamesEqual(left, right);
}

app::OperationResult WindowsAppPlatform::PublishImport(
    const app::ImportPublishRequest& request)
{
    std::wstring source;
    if (!Utf8ToWide(request.sourcePath, source)) {
        return app::OperationResult::Failure("The source path is invalid.");
    }
    const std::filesystem::path compiler =
        impl_->library.ExecutableDirectory() / L"InputWeaverCompiler.exe";
    const std::filesystem::path temporary =
        impl_->library.ArtifactTemporaryPath(request.entry.id);
    const std::vector<std::wstring> compileArguments{
        L"compile",
        source,
        temporary.wstring()};
    CapturedProcessResult compile = RunChildProcess(
        compiler,
        compileArguments,
        CREATE_NO_WINDOW);
    impl_->QueueOutput(
        request.entry.id,
        request.entry.displayName,
        app::ConsoleSource::Compiler,
        compile.standardOutput);
    impl_->QueueOutput(
        request.entry.id,
        request.entry.displayName,
        app::ConsoleSource::Compiler,
        compile.standardError);
    if (!compile.started || !compile.error.empty() || compile.exitCode != 0U) {
        RemoveTemporary(temporary);
        return app::OperationResult::Failure(
            compile.error.empty()
                ? "Compiler exited with code "
                    + std::to_string(compile.exitCode) + "."
                : compile.error);
    }

    const std::vector<std::wstring> dumpArguments{L"dump", source};
    CapturedProcessResult dump = RunChildProcess(
        compiler,
        dumpArguments,
        CREATE_NO_WINDOW);
    impl_->QueueOutput(
        request.entry.id,
        request.entry.displayName,
        app::ConsoleSource::Compiler,
        dump.standardOutput);
    impl_->QueueOutput(
        request.entry.id,
        request.entry.displayName,
        app::ConsoleSource::Compiler,
        dump.standardError);
    if (!dump.started || !dump.error.empty() || dump.exitCode != 0U) {
        RemoveTemporary(temporary);
        return app::OperationResult::Failure(
            dump.error.empty()
                ? "Compiler dump exited with code "
                    + std::to_string(dump.exitCode) + "."
                : dump.error);
    }
    return impl_->library.PublishImport(request, temporary, dump.standardOutput);
}

app::OperationResult WindowsAppPlatform::SaveEntry(
    const app::ProgramEntry& entry)
{
    return impl_->library.SaveEntry(entry);
}

app::OperationResult WindowsAppPlatform::SaveOrder(
    std::span<const app::ProgramEntryId> order)
{
    return impl_->library.SaveOrder(order);
}

app::OperationResult WindowsAppPlatform::DeleteEntry(app::ProgramEntryId id)
{
    return impl_->library.DeleteEntry(id);
}

std::string WindowsAppPlatform::LoadDump(app::ProgramEntryId id) const
{
    return impl_->library.LoadDump(id);
}

app::OperationResult WindowsAppPlatform::LaunchExecutor(
    const app::LaunchRequest& request)
{
    {
        std::lock_guard lock(impl_->executorMutex);
        if (impl_->FindExecutor(request.entry.id) != nullptr) {
            return app::OperationResult::Success();
        }
        if (request.options.debug
            && impl_->debugProgramId != app::kInvalidProgramEntryId) {
            return app::OperationResult::Failure(
                "A debug executor is already active.");
        }
    }

    std::vector<std::wstring> arguments{
        L"--program",
        impl_->library.ArtifactPath(request.entry.id).wstring(),
        L"--exclude-process",
        std::to_wstring(InteractiveHostProcessId())};
    if (request.entry.configuration.target == app::TargetMode::Executable) {
        std::wstring selector;
        if (!Utf8ToWide(
                request.entry.configuration.executableSelector,
                selector)) {
            return app::OperationResult::Failure(
                "The executable selector is invalid UTF-8.");
        }
        arguments.push_back(L"--target");
        arguments.push_back(std::move(selector));
    } else if (request.entry.configuration.target == app::TargetMode::Global) {
        arguments.push_back(L"--target-global");
    }
    if (request.options.allowExec) {
        arguments.push_back(L"--allow-exec");
    }
    if (request.options.dryRun) {
        arguments.push_back(L"--dry-run");
    }
    std::string logPath;
    if (request.entry.configuration.logging != app::LoggingMode::Off) {
        auto [absolute, relative] = impl_->library.CreateLogPath(
            request.entry.id);
        arguments.push_back(L"--log");
        arguments.push_back(absolute.wstring());
        logPath = std::move(relative);
        if (request.entry.configuration.logging == app::LoggingMode::InputTrace) {
            arguments.push_back(L"--trace-input");
        }
    }
    const std::string token = request.options.debug ? MakeDebugToken() : "";
    if (request.options.debug) {
        std::wstring wideToken;
        (void)Utf8ToWide(token, wideToken);
        arguments.push_back(L"--debug-session");
        arguments.push_back(std::move(wideToken));
    }

    auto executor = std::make_unique<Impl::ManagedExecutor>();
    executor->info = {
        request.entry.id,
        request.options.debug ? app::ExecutorMode::Debug : app::ExecutorMode::Run,
        request.options.dryRun,
        request.options.allowExec,
        logPath};
    executor->programName = request.entry.displayName;
    std::string error;
    const std::filesystem::path runtime =
        impl_->library.ExecutableDirectory() / L"InputWeaver.exe";
    if (request.options.debug) {
        std::this_thread::sleep_for(kDebugLaunchDelay);
    }
    if (!StartChildProcess(
            runtime,
            arguments,
            CREATE_NEW_PROCESS_GROUP,
            executor->child,
            error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    impl_->StartOutputReaders(*executor);
    if (request.options.debug) {
        executor->debugClient = std::make_unique<WindowsDebugClient>();
        const debug::DebugClientResult connected = executor->debugClient->Connect(
            {executor->child.processId},
            token);
        if (!connected.Succeeded()) {
            (void)GenerateConsoleCtrlEvent(
                CTRL_BREAK_EVENT,
                executor->child.processId);
            (void)WaitForSingleObject(
                executor->child.process.Get(),
                kExecutorStopTimeoutMilliseconds);
            impl_->FinishExecutor(std::move(executor));
            return app::OperationResult::Failure(
                DebugClientErrorText(connected.error));
        }
        const debug::DebugClientResult captured =
            executor->debugClient->StartCapture();
        if (!captured.Succeeded()) {
            (void)executor->debugClient->RequestExecutorStop();
            (void)WaitForSingleObject(
                executor->child.process.Get(),
                kExecutorStopTimeoutMilliseconds);
            impl_->FinishExecutor(std::move(executor));
            return app::OperationResult::Failure(
                DebugClientErrorText(captured.error));
        }
    }
    if (!logPath.empty()) {
        impl_->QueueOutput(
            request.entry.id,
            request.entry.displayName,
            app::ConsoleSource::App,
            "Log: " + logPath);
    }
    {
        std::lock_guard lock(impl_->executorMutex);
        if (request.options.debug) {
            impl_->debugProgramId = request.entry.id;
        }
        impl_->executors.push_back(std::move(executor));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsAppPlatform::StopExecutor(
    app::ProgramEntryId id,
    bool waitForExit)
{
    HANDLE process{};
    app::OperationResult requested = app::OperationResult::Success();
    {
        std::lock_guard lock(impl_->executorMutex);
        Impl::ManagedExecutor* executor = impl_->FindExecutor(id);
        if (executor == nullptr) {
            return app::OperationResult::Success();
        }
        process = executor->child.process.Get();
        requested = impl_->RequestStop(*executor);
    }
    if (!requested.succeeded || !waitForExit) {
        return requested;
    }
    const DWORD wait = WaitForSingleObject(
        process,
        kExecutorStopTimeoutMilliseconds);
    if (wait != WAIT_OBJECT_0) {
        return app::OperationResult::Failure(
            wait == WAIT_TIMEOUT
                ? "Timed out while waiting for the executor to stop."
                : std::system_category().message(
                    static_cast<int>(GetLastError())));
    }
    impl_->CollectExited();
    return app::OperationResult::Success();
}

void WindowsAppPlatform::StopAllExecutors() noexcept
{
    try {
        std::vector<HANDLE> processes;
        {
            std::lock_guard lock(impl_->executorMutex);
            processes.reserve(impl_->executors.size());
            for (auto& executor : impl_->executors) {
                (void)impl_->RequestStop(*executor);
                processes.push_back(executor->child.process.Get());
            }
        }
        for (const HANDLE process : processes) {
            if (WaitForSingleObject(
                    process,
                    kExecutorStopTimeoutMilliseconds) != WAIT_OBJECT_0) {
                (void)TerminateProcess(process, 1U);
                (void)WaitForSingleObject(process, INFINITE);
            }
        }
        impl_->CollectExited();
    } catch (...) {
    }
}

std::vector<app::ExecutorInfo> WindowsAppPlatform::ReadExecutors() const
{
    std::lock_guard lock(impl_->executorMutex);
    std::vector<app::ExecutorInfo> result;
    result.reserve(impl_->executors.size());
    for (const auto& executor : impl_->executors) {
        result.push_back(executor->info);
    }
    return result;
}

std::vector<app::PlatformEvent> WindowsAppPlatform::PollEvents()
{
    impl_->CollectExited();
    std::lock_guard lock(impl_->eventMutex);
    std::vector<app::PlatformEvent> result;
    result.reserve(impl_->events.size());
    while (!impl_->events.empty()) {
        result.push_back(std::move(impl_->events.front()));
        impl_->events.pop_front();
    }
    return result;
}

app::ProgramEntryId WindowsAppPlatform::DebugProgramId() const noexcept
{
    std::lock_guard lock(impl_->executorMutex);
    return impl_->debugProgramId;
}

std::shared_ptr<const debug::DebugClientState>
WindowsAppPlatform::ReadDebugState() const
{
    std::lock_guard lock(impl_->executorMutex);
    const Impl::ManagedExecutor* executor =
        impl_->FindExecutor(impl_->debugProgramId);
    return executor == nullptr || executor->debugClient == nullptr
        ? nullptr
        : executor->debugClient->ReadState();
}

app::OperationResult WindowsAppPlatform::StartCapture()
{
    std::lock_guard lock(impl_->executorMutex);
    Impl::ManagedExecutor* executor = impl_->FindExecutor(impl_->debugProgramId);
    if (executor == nullptr || executor->debugClient == nullptr) {
        return app::OperationResult::Failure("No debug executor is active.");
    }
    const debug::DebugClientResult result = executor->debugClient->StartCapture();
    return result.Succeeded()
        ? app::OperationResult::Success()
        : app::OperationResult::Failure(DebugClientErrorText(result.error));
}

app::OperationResult WindowsAppPlatform::StopCapture()
{
    std::lock_guard lock(impl_->executorMutex);
    Impl::ManagedExecutor* executor = impl_->FindExecutor(impl_->debugProgramId);
    if (executor == nullptr || executor->debugClient == nullptr) {
        return app::OperationResult::Failure("No debug executor is active.");
    }
    const debug::DebugClientResult result = executor->debugClient->StopCapture();
    return result.Succeeded()
        ? app::OperationResult::Success()
        : app::OperationResult::Failure(DebugClientErrorText(result.error));
}

} // namespace inputweaver::win32
