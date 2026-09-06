#pragma once

#include "app_platform.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::app {

class Application final {
public:
    explicit Application(AppPlatform& platform);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] OperationResult Initialize();
    void Tick();
    void Shutdown() noexcept;

    [[nodiscard]] ImportPreparation PrepareImport(
        std::string_view sourcePath) const;
    [[nodiscard]] OperationResult ImportProgram(
        std::string_view sourcePath,
        std::string_view displayName,
        std::optional<ProgramEntryId> overwriteId = std::nullopt);
    [[nodiscard]] OperationResult CreateProgram(std::string_view displayName);
    [[nodiscard]] OperationResult RenameProgram(
        ProgramEntryId id,
        std::string_view displayName);
    [[nodiscard]] OperationResult DeleteProgram(ProgramEntryId id);
    [[nodiscard]] OperationResult ReorderPrograms(
        std::span<const ProgramEntryId> order);
    [[nodiscard]] OperationResult UpdateConfiguration(
        ProgramEntryId id,
        const RunConfiguration& configuration);
    [[nodiscard]] SourceReadResult ReadSource(ProgramEntryId id) const;
    [[nodiscard]] OperationResult SaveSource(
        ProgramEntryId id,
        std::string_view source);
    [[nodiscard]] SourceValidationResult ValidateProgram(ProgramEntryId id);
    [[nodiscard]] OperationResult CompileProgram(ProgramEntryId id);
    [[nodiscard]] OperationResult GenerateDump(ProgramEntryId id);
    [[nodiscard]] bool IsCompiledCurrent(ProgramEntryId id) const;

    [[nodiscard]] OperationResult StartProgram(
        ProgramEntryId id,
        const NextRunOptions& options);
    [[nodiscard]] OperationResult StopProgram(ProgramEntryId id);
    [[nodiscard]] OperationResult StartCapture();
    [[nodiscard]] OperationResult StopCapture();

    [[nodiscard]] std::string ReadDump(ProgramEntryId id) const;
    [[nodiscard]] ApplicationSnapshot ReadSnapshot() const;
    [[nodiscard]] ApplicationAttention ConsumeAttention() noexcept;

private:
    [[nodiscard]] const ProgramEntry* FindProgram(ProgramEntryId id) const;
    [[nodiscard]] ProgramEntry* FindProgram(ProgramEntryId id);
    [[nodiscard]] ProgramEntryId FindProgramByName(
        std::string_view displayName,
        ProgramEntryId excluded = kInvalidProgramEntryId) const noexcept;
    [[nodiscard]] bool ExecutorExists(ProgramEntryId id) const;
    void DrainPlatformEvents();
    void AppendConsole(
        ProgramEntryId id,
        std::string_view programName,
        ConsoleSource source,
        std::string_view text);
    void AppendAppMessage(std::string_view text);
    void SetAttention(ApplicationAttention attention) noexcept;
    void Changed() noexcept;

    AppPlatform& platform_;
    std::vector<ProgramEntry> programs_;
    std::deque<ConsoleLine> consoleLines_;
    ProgramEntryId nextId_{1U};
    std::uint64_t version_{};
    std::uint64_t nextConsoleSequence_{1U};
    std::optional<DebugSessionView> terminatedDebugSession_;
    ProgramEntryId closingDebugProgramId_{kInvalidProgramEntryId};
    ApplicationAttention attention_{ApplicationAttention::None};
    bool awaitingDebugCapture_{};
    bool initialized_{};
    bool shutdown_{};
};

} // namespace inputweaver::app
