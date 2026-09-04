#pragma once

#include "app_types.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::app {

struct LibraryLoadResult final {
    bool succeeded{};
    std::vector<ProgramEntry> entries;
    ProgramEntryId nextId{1U};
    std::vector<std::string> notices;
    std::string error;
};

struct ImportSourceInfo final {
    bool succeeded{};
    std::string normalizedPath;
    std::string defaultName;
    std::uint64_t sourceHash{};
    std::string error;
    std::string sourceText;
};

struct ImportPublishRequest final {
    ProgramEntry entry;
    std::vector<ProgramEntryId> order;
    bool overwrite{};
    std::string sourceText;
};

struct ProgramPublishRequest final {
    ProgramEntry entry;
    std::vector<ProgramEntryId> order;
};

struct LaunchRequest final {
    ProgramEntry entry;
    NextRunOptions options{};
};

enum class PlatformEventKind : std::uint8_t {
    Output,
    ExecutorExited,
    Error,
};

struct PlatformEvent final {
    PlatformEventKind kind{PlatformEventKind::Output};
    ProgramEntryId programId{kInvalidProgramEntryId};
    std::string programName;
    ConsoleSource source{ConsoleSource::App};
    std::string text;
    std::uint32_t exitCode{};
    ExecutorMode executorMode{ExecutorMode::Run};
    std::shared_ptr<const debug::DebugClientState> finalDebugState;

    [[nodiscard]] static PlatformEvent Output(
        ProgramEntryId programId,
        std::string programName,
        ConsoleSource source,
        std::string text,
        ExecutorMode executorMode = ExecutorMode::Run)
    {
        PlatformEvent event{};
        event.kind = PlatformEventKind::Output;
        event.programId = programId;
        event.programName = std::move(programName);
        event.source = source;
        event.text = std::move(text);
        event.executorMode = executorMode;
        return event;
    }

    [[nodiscard]] static PlatformEvent Error(
        ProgramEntryId programId,
        std::string programName,
        ConsoleSource source,
        std::string text,
        ExecutorMode executorMode = ExecutorMode::Run)
    {
        PlatformEvent event = Output(
            programId,
            std::move(programName),
            source,
            std::move(text),
            executorMode);
        event.kind = PlatformEventKind::Error;
        return event;
    }

    [[nodiscard]] static PlatformEvent ExecutorExited(
        ProgramEntryId programId,
        std::string programName,
        std::uint32_t exitCode,
        ExecutorMode executorMode,
        std::shared_ptr<const debug::DebugClientState> finalDebugState = {})
    {
        PlatformEvent event{};
        event.kind = PlatformEventKind::ExecutorExited;
        event.programId = programId;
        event.programName = std::move(programName);
        event.source = ConsoleSource::Runtime;
        event.exitCode = exitCode;
        event.executorMode = executorMode;
        event.finalDebugState = std::move(finalDebugState);
        return event;
    }
};

class AppPlatform {
public:
    virtual ~AppPlatform() = default;

    [[nodiscard]] virtual LibraryLoadResult LoadProgramLibrary() = 0;
    [[nodiscard]] virtual ImportSourceInfo InspectImportSource(
        std::string_view sourcePath) const = 0;
    [[nodiscard]] virtual bool NamesEqual(
        std::string_view left,
        std::string_view right) const noexcept = 0;
    [[nodiscard]] virtual OperationResult PublishImport(
        const ImportPublishRequest& request) = 0;
    [[nodiscard]] virtual OperationResult PublishNew(
        const ProgramPublishRequest& request) = 0;
    [[nodiscard]] virtual OperationResult SaveEntry(
        const ProgramEntry& entry) = 0;
    [[nodiscard]] virtual OperationResult SaveOrder(
        std::span<const ProgramEntryId> order) = 0;
    [[nodiscard]] virtual OperationResult DeleteEntry(ProgramEntryId id) = 0;
    [[nodiscard]] virtual SourceReadResult LoadSource(
        ProgramEntryId id) const = 0;
    [[nodiscard]] virtual std::string LoadDump(ProgramEntryId id) const = 0;
    [[nodiscard]] virtual OperationResult SaveSource(
        ProgramEntryId id,
        std::string_view source) = 0;
    [[nodiscard]] virtual SourceValidationResult ValidateSource(
        const ProgramEntry& entry,
        std::string_view source) = 0;
    [[nodiscard]] virtual OperationResult CompileProgram(
        const ProgramEntry& entry,
        std::string_view source) = 0;
    [[nodiscard]] virtual OperationResult GenerateDump(
        const ProgramEntry& entry,
        std::string_view source) = 0;

    [[nodiscard]] virtual OperationResult LaunchExecutor(
        const LaunchRequest& request) = 0;
    [[nodiscard]] virtual OperationResult StopExecutor(
        ProgramEntryId id,
        bool waitForExit) = 0;
    virtual void StopAllExecutors() noexcept = 0;
    [[nodiscard]] virtual std::vector<ExecutorInfo> ReadExecutors() const = 0;
    [[nodiscard]] virtual std::vector<PlatformEvent> PollEvents() = 0;

    [[nodiscard]] virtual ProgramEntryId DebugProgramId() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const debug::DebugClientState>
        ReadDebugState() const = 0;
    [[nodiscard]] virtual OperationResult StartCapture() = 0;
    [[nodiscard]] virtual OperationResult StopCapture() = 0;
};

} // namespace inputweaver::app
