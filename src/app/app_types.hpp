#pragma once

#include "debug/debug_client.hpp"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace inputweaver::app {

using ProgramEntryId = std::uint32_t;
inline constexpr ProgramEntryId kInvalidProgramEntryId = 0U;
inline constexpr ProgramEntryId kMaximumProgramEntryId = 999'999U;

enum class TargetMode : std::uint8_t {
    Compiled,
    Executable,
    Global,
};

enum class LoggingMode : std::uint8_t {
    Off,
    Operational,
    InputTrace,
};

struct RunConfiguration final {
    TargetMode target{TargetMode::Compiled};
    std::string executableSelector;
    LoggingMode logging{LoggingMode::Off};

    auto operator<=>(const RunConfiguration&) const = default;
};

struct ProgramEntry final {
    ProgramEntryId id{kInvalidProgramEntryId};
    std::string displayName;
    RunConfiguration configuration{};

    auto operator<=>(const ProgramEntry&) const = default;
};

struct NextRunOptions final {
    bool debug{};
    bool dryRun{};
    bool allowExec{};
};

enum class ExecutorMode : std::uint8_t {
    Run,
    Debug,
};

struct ExecutorInfo final {
    ProgramEntryId programId{kInvalidProgramEntryId};
    ExecutorMode mode{ExecutorMode::Run};
    bool dryRun{};
    bool allowExec{};
    std::string logPath;
};

enum class ConsoleSource : std::uint8_t {
    App,
    Compiler,
    Runtime,
};

struct ConsoleLine final {
    std::uint64_t sequence{};
    ProgramEntryId programId{kInvalidProgramEntryId};
    std::string programName;
    ConsoleSource source{ConsoleSource::App};
    std::string text;
};

struct ApplicationSnapshot final {
    std::uint64_t version{};
    std::vector<ProgramEntry> programs;
    std::vector<ExecutorInfo> executors;
    std::vector<ConsoleLine> consoleLines;
    ProgramEntryId debugProgramId{kInvalidProgramEntryId};
    std::shared_ptr<const debug::DebugClientState> debugState;
};

enum class ApplicationAttention : std::uint8_t {
    None,
    Console,
    Debug,
};

struct OperationResult final {
    bool succeeded{};
    std::string error;

    [[nodiscard]] static OperationResult Success()
    {
        return {true, {}};
    }

    [[nodiscard]] static OperationResult Failure(std::string message)
    {
        return {false, std::move(message)};
    }
};

enum class ImportPreparationStatus : std::uint8_t {
    Ready,
    NameConflict,
    Invalid,
};

struct ImportPreparation final {
    ImportPreparationStatus status{ImportPreparationStatus::Invalid};
    std::string sourcePath;
    std::string defaultName;
    ProgramEntryId conflictId{kInvalidProgramEntryId};
    std::string error;
};

} // namespace inputweaver::app
