#include "program_library.hpp"

#include "app/entry_codec.hpp"
#include "platform/windows/support/atomic_file.hpp"
#include "platform/windows/support/ordinal_string.hpp"
#include "platform/windows/support/text_encoding.hpp"
#include "support/utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace inputweaver::win32 {
namespace {

[[nodiscard]] std::wstring FormatId(app::ProgramEntryId id)
{
    std::wostringstream output;
    output << std::setw(6) << std::setfill(L'0') << id;
    return output.str();
}

[[nodiscard]] bool ReadBytes(
    const std::filesystem::path& path,
    std::string& bytes,
    std::string& error)
{
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot open " + path.string() + ".";
            return false;
        }
        input.seekg(0, std::ios::end);
        const std::streampos end = input.tellg();
        if (end < 0) {
            error = "Cannot determine the size of " + path.string() + ".";
            return false;
        }
        const auto size = static_cast<std::uintmax_t>(end);
        if (size > static_cast<std::uintmax_t>(
                (std::numeric_limits<std::size_t>::max)())) {
            error = "File is too large: " + path.string() + ".";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!bytes.empty()) {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
                bytes.clear();
                error = "File changed while it was being read: "
                    + path.string() + ".";
                return false;
            }
        }
        if (!input && !input.eof()) {
            error = "Cannot read " + path.string() + ".";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

[[nodiscard]] bool ParseEntryId(
    const std::filesystem::path& path,
    app::ProgramEntryId& id) noexcept
{
    const std::wstring filename = path.filename().wstring();
    if (filename.size() < 7U) {
        return false;
    }
    id = 0U;
    for (std::size_t index = 0U; index < 6U; ++index) {
        const wchar_t character = filename[index];
        if (character < L'0' || character > L'9') {
            return false;
        }
        id = id * 10U + static_cast<app::ProgramEntryId>(character - L'0');
    }
    return id != app::kInvalidProgramEntryId
        && id <= app::kMaximumProgramEntryId;
}

[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

void RemoveIfPresent(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}

struct StagedRemoval final {
    std::filesystem::path original;
    std::filesystem::path staged;
    bool moved{};
};

[[nodiscard]] bool StageRemoval(
    StagedRemoval& removal,
    std::string& error)
{
    const DWORD attributes = GetFileAttributesW(removal.original.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD status = GetLastError();
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        error = std::system_category().message(static_cast<int>(status));
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        error = "Cannot remove a program file because its path is a directory.";
        return false;
    }
    removal.staged = MakeSiblingTemporaryPath(removal.original);
    if (MoveFileExW(
            removal.original.c_str(),
            removal.staged.c_str(),
            MOVEFILE_WRITE_THROUGH) == FALSE) {
        error = std::system_category().message(
            static_cast<int>(GetLastError()));
        return false;
    }
    removal.moved = true;
    return true;
}

void RestoreRemoval(StagedRemoval& removal) noexcept
{
    if (!removal.moved) {
        return;
    }
    if (MoveFileExW(
            removal.staged.c_str(),
            removal.original.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        removal.moved = false;
    }
}

void DiscardRemoval(StagedRemoval& removal) noexcept
{
    if (!removal.moved) {
        return;
    }
    if (DeleteFileW(removal.staged.c_str()) == FALSE) {
        (void)MoveFileExW(
            removal.staged.c_str(),
            nullptr,
            MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    removal.moved = false;
}

[[nodiscard]] std::string TrimAndUnquote(std::string_view source)
{
    while (!source.empty()
        && (source.front() == ' ' || source.front() == '\t'
            || source.front() == '\r' || source.front() == '\n')) {
        source.remove_prefix(1U);
    }
    while (!source.empty()
        && (source.back() == ' ' || source.back() == '\t'
            || source.back() == '\r' || source.back() == '\n')) {
        source.remove_suffix(1U);
    }
    if (source.size() >= 2U && source.front() == '"' && source.back() == '"') {
        source.remove_prefix(1U);
        source.remove_suffix(1U);
    }
    return std::string{source};
}

template <std::size_t Count>
[[nodiscard]] bool PublishFiles(
    const std::array<std::filesystem::path, Count>& sources,
    const std::array<std::filesystem::path, Count>& destinations,
    std::string& error)
{
    std::array<std::filesystem::path, Count> backups{};
    std::array<bool, Count> hasBackup{};
    for (std::size_t index = 0U; index < Count; ++index) {
        if (!IsRegularFile(destinations[index])) {
            continue;
        }
        backups[index] = MakeSiblingTemporaryPath(destinations[index]);
        if (CopyFileW(
                destinations[index].c_str(),
                backups[index].c_str(),
                TRUE) == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            for (const auto& backup : backups) {
                RemoveIfPresent(backup);
            }
            return false;
        }
        hasBackup[index] = true;
    }

    std::size_t published{};
    for (; published < Count; ++published) {
        if (ReplaceFileAtomically(
                sources[published],
                destinations[published],
                error)) {
            continue;
        }
        bool rollbackSucceeded = true;
        for (std::size_t count = published; count > 0U; --count) {
            const std::size_t index = count - 1U;
            if (!hasBackup[index]) {
                RemoveIfPresent(destinations[index]);
                continue;
            }
            std::string rollbackError;
            if (!ReplaceFileAtomically(
                    backups[index],
                    destinations[index],
                    rollbackError)) {
                rollbackSucceeded = false;
            }
        }
        for (std::size_t index = 0U; index < Count; ++index) {
            if (!hasBackup[index] || index >= published
                || rollbackSucceeded) {
                RemoveIfPresent(backups[index]);
            }
        }
        if (!rollbackSucceeded) {
            error += " Rollback was incomplete; backup files were retained.";
        }
        return false;
    }
    for (const auto& backup : backups) {
        RemoveIfPresent(backup);
    }
    return true;
}

} // namespace

WindowsProgramLibrary::WindowsProgramLibrary(
    std::filesystem::path executableDirectory)
    : executableDirectory_(std::move(executableDirectory)),
      programsDirectory_(executableDirectory_ / L"programs")
{
}

app::LibraryLoadResult WindowsProgramLibrary::Load()
{
    app::LibraryLoadResult result{};
    std::string error;
    if (!EnsureDirectories(error)) {
        result.error = std::move(error);
        return result;
    }

    app::ProgramEntryId maximumId{};
    std::vector<app::ProgramEntryId> discovered;
    try {
        for (const auto& item : std::filesystem::directory_iterator(
                 programsDirectory_)) {
            app::ProgramEntryId id{};
            if (ParseEntryId(item.path(), id)) {
                maximumId = (std::max)(maximumId, id);
                if (item.path().extension() == L".entry") {
                    discovered.push_back(id);
                }
            }
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }

    std::vector<app::ProgramEntryId> order;
    const bool indexExists = IsRegularFile(IndexPath());
    if (indexExists) {
        std::string index;
        if (!ReadBytes(IndexPath(), index, error)
            || !app::DecodeProgramIndex(index, order, error)) {
            result.error = "Cannot load programs.index: " + error;
            return result;
        }
        for (const app::ProgramEntryId id : order) {
            maximumId = (std::max)(maximumId, id);
        }
    } else {
        order = discovered;
        std::sort(order.begin(), order.end());
        result.notices.push_back(
            "programs.index was missing; program order was rebuilt.");
    }

    std::vector<app::ProgramEntryId> repairedOrder;
    for (const app::ProgramEntryId id : order) {
        if (!IsRegularFile(EntryPath(id))) {
            result.notices.push_back(
                "Removed program ID " + std::to_string(id)
                + " from the index because its metadata is missing.");
            continue;
        }
        std::string metadata;
        app::ProgramEntry entry{};
        if (!ReadBytes(EntryPath(id), metadata, error)
            || !app::DecodeEntry(id, metadata, entry, error)) {
            result.notices.push_back(
                "Removed program ID " + std::to_string(id)
                + " from the index because its metadata is invalid.");
            continue;
        }
        if (!IsRegularFile(SourcePath(id))) {
            if (!WriteFileAtomically(SourcePath(id), {}, error)) {
                result.notices.push_back(
                    "Removed " + entry.displayName
                    + " because its source file could not be created.");
                continue;
            }
            result.notices.push_back(
                "Created an empty editable source for legacy program "
                + entry.displayName + ".");
        }
        if (entry.compiledSourceHash != 0U
            && !IsRegularFile(ArtifactPath(id))) {
            entry.compiledSourceHash = 0U;
            if (!SaveEntry(entry).succeeded) {
                result.notices.push_back(
                    "Removed " + entry.displayName
                    + " because its metadata could not be repaired.");
                continue;
            }
            result.notices.push_back(
                "Marked " + entry.displayName
                + " uncompiled because its artifact is missing.");
        }
        repairedOrder.push_back(id);
        result.entries.push_back(std::move(entry));
    }
    if (!indexExists || repairedOrder != order) {
        const app::OperationResult saved = SaveOrder(repairedOrder);
        if (!saved.succeeded) {
            result.error = saved.error;
            return result;
        }
    }
    result.nextId = maximumId >= app::kMaximumProgramEntryId
        ? app::kMaximumProgramEntryId + 1U
        : maximumId + 1U;
    result.succeeded = true;
    return result;
}

app::ImportSourceInfo WindowsProgramLibrary::InspectSource(
    std::string_view sourcePath) const
{
    const std::string unquoted = TrimAndUnquote(sourcePath);
    if (unquoted.empty() || !support::IsValidUtf8(unquoted)) {
        return {false, {}, {}, 0U, "A valid UTF-8 path is required.", {}};
    }
    std::wstring wide;
    if (!Utf8ToWide(unquoted, wide)) {
        return {false, {}, {}, 0U, "The source path is not valid UTF-8.", {}};
    }
    try {
        std::filesystem::path path = std::filesystem::absolute(
            std::filesystem::path{wide});
        if (!IsRegularFile(path)) {
            return {false, {}, {}, 0U, "The source file does not exist.", {}};
        }
        if (!EqualOrdinalIgnoreCase(path.extension().wstring(), L".weave")) {
            return {false, {}, {}, 0U, "A single .weave file is required.", {}};
        }
        std::string normalized;
        std::string name;
        if (!WideToUtf8(path.wstring(), normalized)
            || !WideToUtf8(path.stem().wstring(), name)
            || !app::ValidDisplayName(name)) {
            return {false, {}, {}, 0U, "The source filename cannot be used.", {}};
        }
        std::string source;
        std::string error;
        if (!ReadBytes(path, source, error)) {
            return {false, {}, {}, 0U, std::move(error), {}};
        }
        const std::uint64_t sourceHash = app::SourceHash(source);
        return {
            true,
            std::move(normalized),
            std::move(name),
            sourceHash,
            {},
            std::move(source)};
    } catch (const std::exception& exception) {
        return {false, {}, {}, 0U, exception.what(), {}};
    }
}

bool WindowsProgramLibrary::NamesEqual(
    std::string_view left,
    std::string_view right) const noexcept
{
    std::wstring wideLeft;
    std::wstring wideRight;
    return Utf8ToWide(left, wideLeft)
        && Utf8ToWide(right, wideRight)
        && EqualOrdinalIgnoreCase(wideLeft, wideRight);
}

app::OperationResult WindowsProgramLibrary::PublishImport(
    const app::ImportPublishRequest& request,
    const std::filesystem::path& sourceTemporary,
    const std::filesystem::path& compiledTemporary,
    std::string_view dumpText)
{
    std::string error;
    if (!EnsureDirectories(error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    if (app::SourceHash(request.sourceText)
        != request.entry.compiledSourceHash) {
        RemoveIfPresent(compiledTemporary);
        return app::OperationResult::Failure(
            "The import source changed before publication.");
    }
    const std::filesystem::path entryTemporary = MakeSiblingTemporaryPath(
        EntryPath(request.entry.id));
    const std::filesystem::path dumpTemporary = MakeSiblingTemporaryPath(
        DumpPath(request.entry.id));
    const std::filesystem::path indexTemporary = MakeSiblingTemporaryPath(
        IndexPath());
    const std::array temporaryFiles{
        sourceTemporary,
        entryTemporary,
        dumpTemporary,
        indexTemporary};
    const auto cleanup = [&] {
        for (const auto& path : temporaryFiles) {
            RemoveIfPresent(path);
        }
        RemoveIfPresent(compiledTemporary);
    };
    if (!WriteNewFile(
            entryTemporary,
            app::EncodeEntry(request.entry),
            error)
        || !WriteNewFile(dumpTemporary, dumpText, error)
        || !WriteNewFile(
            indexTemporary,
            app::EncodeProgramIndex(request.order),
            error)) {
        cleanup();
        return app::OperationResult::Failure(std::move(error));
    }
    const std::array sources{
        compiledTemporary,
        sourceTemporary,
        dumpTemporary,
        entryTemporary,
        indexTemporary};
    const std::array destinations{
        ArtifactPath(request.entry.id),
        SourcePath(request.entry.id),
        DumpPath(request.entry.id),
        EntryPath(request.entry.id),
        IndexPath()};
    if (!PublishFiles(sources, destinations, error)) {
        cleanup();
        return app::OperationResult::Failure(std::move(error));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::PublishNew(
    const app::ProgramPublishRequest& request)
{
    std::string error;
    if (!EnsureDirectories(error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    const std::array destinations{
        SourcePath(request.entry.id),
        EntryPath(request.entry.id),
        IndexPath()};
    std::array<std::filesystem::path, 3U> sources{};
    for (std::size_t index = 0U; index < sources.size(); ++index) {
        sources[index] = MakeSiblingTemporaryPath(destinations[index]);
    }
    const auto cleanup = [&] {
        for (const auto& path : sources) {
            RemoveIfPresent(path);
        }
    };
    if (!WriteNewFile(sources[0], {}, error)
        || !WriteNewFile(sources[1], app::EncodeEntry(request.entry), error)
        || !WriteNewFile(
            sources[2],
            app::EncodeProgramIndex(request.order),
            error)
        || !PublishFiles(sources, destinations, error)) {
        cleanup();
        return app::OperationResult::Failure(std::move(error));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::PublishCompilation(
    const app::ProgramEntry& entry,
    std::string_view sourceText,
    const std::filesystem::path& compiledTemporary,
    std::string_view dumpText)
{
    std::string error;
    std::string currentSource;
    if (!ReadBytes(SourcePath(entry.id), currentSource, error)
        || currentSource != sourceText) {
        RemoveIfPresent(compiledTemporary);
        return app::OperationResult::Failure(
            error.empty()
                ? "Source changed during compilation."
                : std::move(error));
    }
    const std::filesystem::path dumpTemporary = MakeSiblingTemporaryPath(
        DumpPath(entry.id));
    const std::filesystem::path entryTemporary = MakeSiblingTemporaryPath(
        EntryPath(entry.id));
    const std::array sources{
        compiledTemporary,
        dumpTemporary,
        entryTemporary};
    const std::array destinations{
        ArtifactPath(entry.id),
        DumpPath(entry.id),
        EntryPath(entry.id)};
    const auto cleanup = [&] {
        for (const auto& path : sources) {
            RemoveIfPresent(path);
        }
    };
    if (!WriteNewFile(dumpTemporary, dumpText, error)
        || !WriteNewFile(entryTemporary, app::EncodeEntry(entry), error)
        || !PublishFiles(sources, destinations, error)) {
        cleanup();
        return app::OperationResult::Failure(std::move(error));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::SaveEntry(
    const app::ProgramEntry& entry)
{
    std::string error;
    if (!WriteFileAtomically(EntryPath(entry.id), app::EncodeEntry(entry), error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::SaveOrder(
    std::span<const app::ProgramEntryId> order)
{
    std::string error;
    if (!WriteFileAtomically(IndexPath(), app::EncodeProgramIndex(order), error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::DeleteEntry(app::ProgramEntryId id)
{
    std::string indexText;
    std::string error;
    std::vector<app::ProgramEntryId> order;
    if (IsRegularFile(IndexPath())
        && (!ReadBytes(IndexPath(), indexText, error)
            || !app::DecodeProgramIndex(indexText, order, error))) {
        return app::OperationResult::Failure(std::move(error));
    }
    order.erase(std::remove(order.begin(), order.end(), id), order.end());
    std::array removals{
        StagedRemoval{EntryPath(id), {}, false},
        StagedRemoval{SourcePath(id), {}, false},
        StagedRemoval{ArtifactPath(id), {}, false},
        StagedRemoval{DumpPath(id), {}, false}};
    for (std::size_t index = 0U; index < removals.size(); ++index) {
        if (StageRemoval(removals[index], error)) {
            continue;
        }
        while (index > 0U) {
            --index;
            RestoreRemoval(removals[index]);
        }
        return app::OperationResult::Failure(std::move(error));
    }
    const app::OperationResult saved = SaveOrder(order);
    if (!saved.succeeded) {
        for (auto& removal : removals) {
            RestoreRemoval(removal);
        }
        return saved;
    }
    for (auto& removal : removals) {
        DiscardRemoval(removal);
    }
    return app::OperationResult::Success();
}

app::SourceReadResult WindowsProgramLibrary::LoadSource(
    app::ProgramEntryId id) const
{
    app::SourceReadResult result{};
    result.succeeded = ReadBytes(SourcePath(id), result.text, result.error);
    return result;
}

std::string WindowsProgramLibrary::LoadDump(app::ProgramEntryId id) const
{
    std::string dump;
    std::string error;
    if (!ReadBytes(DumpPath(id), dump, error)) {
        return {};
    }
    return dump;
}

app::OperationResult WindowsProgramLibrary::SaveSource(
    app::ProgramEntryId id,
    std::string_view source)
{
    if (source.size() > 16U * 1024U * 1024U
        || !support::IsValidUtf8(source)) {
        return app::OperationResult::Failure(
            "Source must be valid UTF-8 and no larger than 16 MiB.");
    }
    std::string error;
    StagedRemoval staleDump{DumpPath(id), {}, false};
    if (!StageRemoval(staleDump, error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    if (!WriteFileAtomically(SourcePath(id), source, error)) {
        RestoreRemoval(staleDump);
        return app::OperationResult::Failure(std::move(error));
    }
    DiscardRemoval(staleDump);
    return app::OperationResult::Success();
}

app::OperationResult WindowsProgramLibrary::SaveDump(
    app::ProgramEntryId id,
    std::string_view dump)
{
    std::string error;
    return WriteFileAtomically(DumpPath(id), dump, error)
        ? app::OperationResult::Success()
        : app::OperationResult::Failure(std::move(error));
}

app::OperationResult WindowsProgramLibrary::PublishDump(
    app::ProgramEntryId id,
    std::string_view sourceText,
    std::string_view dump)
{
    std::string currentSource;
    std::string error;
    if (!ReadBytes(SourcePath(id), currentSource, error)
        || currentSource != sourceText) {
        return app::OperationResult::Failure(
            error.empty()
                ? "Source changed while generating the dump."
                : std::move(error));
    }
    return SaveDump(id, dump);
}

const std::filesystem::path& WindowsProgramLibrary::ExecutableDirectory()
    const noexcept
{
    return executableDirectory_;
}

const std::filesystem::path& WindowsProgramLibrary::ProgramsDirectory()
    const noexcept
{
    return programsDirectory_;
}

std::filesystem::path WindowsProgramLibrary::ArtifactPath(
    app::ProgramEntryId id) const
{
    return programsDirectory_ / (FormatId(id) + L".weavec");
}

std::filesystem::path WindowsProgramLibrary::ArtifactTemporaryPath(
    app::ProgramEntryId id) const
{
    return MakeSiblingTemporaryPath(ArtifactPath(id));
}

std::filesystem::path WindowsProgramLibrary::SourcePath(
    app::ProgramEntryId id) const
{
    return programsDirectory_ / (FormatId(id) + L".weave");
}

std::pair<std::filesystem::path, std::string>
WindowsProgramLibrary::CreateLogPath(app::ProgramEntryId id)
{
    SYSTEMTIME local{};
    GetLocalTime(&local);
    std::wostringstream filename;
    filename << FormatId(id) << L'-'
             << std::setw(4) << std::setfill(L'0') << local.wYear
             << std::setw(2) << local.wMonth
             << std::setw(2) << local.wDay << L'-'
             << std::setw(2) << local.wHour
             << std::setw(2) << local.wMinute
             << std::setw(2) << local.wSecond << L'-'
             << ++logSequence_ << L".jsonl";
    const std::filesystem::path absolute =
        programsDirectory_ / L"logs" / filename.str();
    const std::filesystem::path relative =
        std::filesystem::path{L"programs"} / L"logs" / filename.str();
    std::string relativeUtf8;
    (void)WideToUtf8(relative.wstring(), relativeUtf8);
    return {absolute, relativeUtf8};
}

std::filesystem::path WindowsProgramLibrary::EntryPath(
    app::ProgramEntryId id) const
{
    return programsDirectory_ / (FormatId(id) + L".entry");
}

std::filesystem::path WindowsProgramLibrary::DumpPath(
    app::ProgramEntryId id) const
{
    return programsDirectory_ / (FormatId(id) + L".dump.txt");
}

std::filesystem::path WindowsProgramLibrary::IndexPath() const
{
    return programsDirectory_ / L"programs.index";
}

bool WindowsProgramLibrary::EnsureDirectories(std::string& error) const
{
    try {
        std::filesystem::create_directories(programsDirectory_ / L"logs");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace inputweaver::win32
