#pragma once

#include "app/app_platform.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace inputweaver::win32 {

class WindowsProgramLibrary final {
public:
    explicit WindowsProgramLibrary(std::filesystem::path executableDirectory);

    [[nodiscard]] app::LibraryLoadResult Load();
    [[nodiscard]] app::ImportSourceInfo InspectSource(
        std::string_view sourcePath) const;
    [[nodiscard]] bool NamesEqual(
        std::string_view left,
        std::string_view right) const noexcept;

    [[nodiscard]] app::OperationResult PublishImport(
        const app::ImportPublishRequest& request,
        const std::filesystem::path& sourceTemporary,
        const std::filesystem::path& compiledTemporary,
        std::string_view dumpText);
    [[nodiscard]] app::OperationResult PublishNew(
        const app::ProgramPublishRequest& request);
    [[nodiscard]] app::OperationResult PublishCompilation(
        const app::ProgramEntry& entry,
        std::string_view sourceText,
        const std::filesystem::path& compiledTemporary,
        std::string_view dumpText);
    [[nodiscard]] app::OperationResult SaveEntry(
        const app::ProgramEntry& entry);
    [[nodiscard]] app::OperationResult SaveOrder(
        std::span<const app::ProgramEntryId> order);
    [[nodiscard]] app::OperationResult DeleteEntry(app::ProgramEntryId id);
    [[nodiscard]] app::SourceReadResult LoadSource(
        app::ProgramEntryId id) const;
    [[nodiscard]] std::string LoadDump(app::ProgramEntryId id) const;
    [[nodiscard]] app::OperationResult SaveSource(
        app::ProgramEntryId id,
        std::string_view source);
    [[nodiscard]] app::OperationResult SaveDump(
        app::ProgramEntryId id,
        std::string_view dump);
    [[nodiscard]] app::OperationResult PublishDump(
        app::ProgramEntryId id,
        std::string_view sourceText,
        std::string_view dump);

    [[nodiscard]] const std::filesystem::path& ExecutableDirectory()
        const noexcept;
    [[nodiscard]] const std::filesystem::path& ProgramsDirectory()
        const noexcept;
    [[nodiscard]] std::filesystem::path ArtifactPath(
        app::ProgramEntryId id) const;
    [[nodiscard]] std::filesystem::path ArtifactTemporaryPath(
        app::ProgramEntryId id) const;
    [[nodiscard]] std::filesystem::path SourcePath(
        app::ProgramEntryId id) const;
    [[nodiscard]] std::pair<std::filesystem::path, std::string> CreateLogPath(
        app::ProgramEntryId id);

private:
    [[nodiscard]] std::filesystem::path EntryPath(app::ProgramEntryId id) const;
    [[nodiscard]] std::filesystem::path DumpPath(app::ProgramEntryId id) const;
    [[nodiscard]] std::filesystem::path IndexPath() const;
    [[nodiscard]] bool EnsureDirectories(std::string& error) const;

    std::filesystem::path executableDirectory_;
    std::filesystem::path programsDirectory_;
    std::uint64_t logSequence_{};
};

} // namespace inputweaver::win32
