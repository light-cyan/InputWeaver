#pragma once

#include "app_types.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
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
    std::string error;
};

struct ImportPublishRequest final {
    std::string sourcePath;
    ProgramEntry entry;
    std::vector<ProgramEntryId> order;
    bool overwrite{};
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
    [[nodiscard]] virtual OperationResult SaveEntry(
        const ProgramEntry& entry) = 0;
    [[nodiscard]] virtual OperationResult SaveOrder(
        std::span<const ProgramEntryId> order) = 0;
    [[nodiscard]] virtual OperationResult DeleteEntry(ProgramEntryId id) = 0;
    [[nodiscard]] virtual std::string LoadDump(ProgramEntryId id) const = 0;

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
