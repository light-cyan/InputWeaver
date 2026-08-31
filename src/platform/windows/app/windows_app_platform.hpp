#pragma once

#include "app/app_platform.hpp"

#include <filesystem>
#include <memory>

namespace inputweaver::win32 {

class WindowsAppPlatform final : public app::AppPlatform {
public:
    explicit WindowsAppPlatform(std::filesystem::path executableDirectory);
    ~WindowsAppPlatform() override;

    WindowsAppPlatform(const WindowsAppPlatform&) = delete;
    WindowsAppPlatform& operator=(const WindowsAppPlatform&) = delete;

    [[nodiscard]] app::LibraryLoadResult LoadProgramLibrary() override;
    [[nodiscard]] app::ImportSourceInfo InspectImportSource(
        std::string_view sourcePath) const override;
    [[nodiscard]] bool NamesEqual(
        std::string_view left,
        std::string_view right) const noexcept override;
    [[nodiscard]] app::OperationResult PublishImport(
        const app::ImportPublishRequest& request) override;
    [[nodiscard]] app::OperationResult PublishNew(
        const app::ProgramPublishRequest& request) override;
    [[nodiscard]] app::OperationResult SaveEntry(
        const app::ProgramEntry& entry) override;
    [[nodiscard]] app::OperationResult SaveOrder(
        std::span<const app::ProgramEntryId> order) override;
    [[nodiscard]] app::OperationResult DeleteEntry(
        app::ProgramEntryId id) override;
    [[nodiscard]] app::SourceReadResult LoadSource(
        app::ProgramEntryId id) const override;
    [[nodiscard]] std::string LoadDump(
        app::ProgramEntryId id) const override;
    [[nodiscard]] app::OperationResult SaveSource(
        app::ProgramEntryId id,
        std::string_view source) override;
    [[nodiscard]] app::SourceValidationResult ValidateSource(
        const app::ProgramEntry& entry,
        std::string_view source) override;
    [[nodiscard]] app::OperationResult CompileProgram(
        const app::ProgramEntry& entry,
        std::string_view source) override;
    [[nodiscard]] app::OperationResult GenerateDump(
        const app::ProgramEntry& entry,
        std::string_view source) override;

    [[nodiscard]] app::OperationResult LaunchExecutor(
        const app::LaunchRequest& request) override;
    [[nodiscard]] app::OperationResult StopExecutor(
        app::ProgramEntryId id,
        bool waitForExit) override;
    void StopAllExecutors() noexcept override;
    [[nodiscard]] std::vector<app::ExecutorInfo> ReadExecutors()
        const override;
    [[nodiscard]] std::vector<app::PlatformEvent> PollEvents() override;

    [[nodiscard]] app::ProgramEntryId DebugProgramId()
        const noexcept override;
    [[nodiscard]] std::shared_ptr<const debug::DebugClientState>
        ReadDebugState() const override;
    [[nodiscard]] app::OperationResult StartCapture() override;
    [[nodiscard]] app::OperationResult StopCapture() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inputweaver::win32
