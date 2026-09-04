#pragma once

#include "debug/debug_client.hpp"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::app {

using ProgramEntryId = std::uint32_t;
inline constexpr ProgramEntryId kInvalidProgramEntryId = 0U;
inline constexpr ProgramEntryId kMaximumProgramEntryId = 999'999U;

[[nodiscard]] constexpr std::uint64_t SourceHash(
    std::string_view source) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const char byte : source) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1'099'511'628'211ULL;
    }
    return hash == 0U ? 1U : hash;
}

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
    std::uint64_t compiledSourceHash{};

    auto operator<=>(const ProgramEntry&) const = default;
};

struct SourceDiagnostic final {
    std::uint32_t line{1U};
    std::uint32_t column{1U};
    std::uint32_t byteLength{1U};
    std::string message;
};

struct SourceValidationResult final {
    bool completed{};
    bool valid{};
    std::vector<SourceDiagnostic> diagnostics;
    std::string error;
};

struct SourceReadResult final {
    bool succeeded{};
    std::string text;
    std::string error;
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

enum class DebugSessionStatus : std::uint8_t {
    Active,
    Terminated,
};

struct DebugSessionView final {
    ProgramEntryId programId{kInvalidProgramEntryId};
    DebugSessionStatus status{DebugSessionStatus::Active};
    std::uint32_t exitCode{};
    std::shared_ptr<const debug::DebugClientState> state;
};

struct ApplicationSnapshot final {
    std::uint64_t version{};
    std::vector<ProgramEntry> programs;
    std::vector<ExecutorInfo> executors;
    std::vector<ConsoleLine> consoleLines;
    std::optional<DebugSessionView> debugSession;
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
