#include "program_library.hpp"

#include "app/entry_codec.hpp"
#include "platform/windows/support/atomic_file.hpp"
#include "platform/windows/support/ordinal_string.hpp"
#include "platform/windows/support/text_encoding.hpp"
#include "platform/windows/support/unique_handle.hpp"
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

[[nodiscard]] bool WriteNewFile(
    const std::filesystem::path& path,
    std::string_view bytes,
    std::string& error)
{
    UniqueHandle file(CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0U,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!file) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (WriteFile(
                file.Get(),
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE
            || written == 0U) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        offset += written;
    }
    if (FlushFileBuffers(file.Get()) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
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
        if (!IsRegularFile(ArtifactPath(id))) {
            result.notices.push_back(
                "Removed " + entry.displayName
                + " because its compiled artifact is missing.");
            RemoveIfPresent(EntryPath(id));
            RemoveIfPresent(DumpPath(id));
            continue;
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
        return {false, {}, {}, "A valid UTF-8 path is required."};
    }
    std::wstring wide;
    if (!Utf8ToWide(unquoted, wide)) {
        return {false, {}, {}, "The source path is not valid UTF-8."};
    }
    try {
        std::filesystem::path path = std::filesystem::absolute(
            std::filesystem::path{wide});
        if (!IsRegularFile(path)) {
            return {false, {}, {}, "The source file does not exist."};
        }
        if (!EqualOrdinalIgnoreCase(path.extension().wstring(), L".weave")) {
            return {false, {}, {}, "A single .weave file is required."};
        }
        std::string normalized;
        std::string name;
        if (!WideToUtf8(path.wstring(), normalized)
            || !WideToUtf8(path.stem().wstring(), name)
            || !app::ValidDisplayName(name)) {
            return {false, {}, {}, "The source filename cannot be used."};
        }
        return {true, std::move(normalized), std::move(name), {}};
    } catch (const std::exception& exception) {
        return {false, {}, {}, exception.what()};
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
    const std::filesystem::path& compiledTemporary,
    std::string_view dumpText)
{
    std::string error;
    if (!EnsureDirectories(error)) {
        return app::OperationResult::Failure(std::move(error));
    }
    const std::filesystem::path entryTemporary = MakeSiblingTemporaryPath(
        EntryPath(request.entry.id));
    const std::filesystem::path dumpTemporary = MakeSiblingTemporaryPath(
        DumpPath(request.entry.id));
    const std::filesystem::path indexTemporary = MakeSiblingTemporaryPath(
        IndexPath());
    const std::array temporaryFiles{
        entryTemporary,
        dumpTemporary,
        indexTemporary};
    const auto cleanup = [&] {
        for (const auto& path : temporaryFiles) {
            RemoveIfPresent(path);
        }
        RemoveIfPresent(compiledTemporary);
    };
    if (!WriteNewFile(entryTemporary, app::EncodeEntry(request.entry), error)
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
        dumpTemporary,
        entryTemporary,
        indexTemporary};
    const std::array destinations{
        ArtifactPath(request.entry.id),
        DumpPath(request.entry.id),
        EntryPath(request.entry.id),
        IndexPath()};
    std::array<std::filesystem::path, sources.size()> backups{};
    std::array<bool, sources.size()> hasBackup{};
    for (std::size_t index = 0U; index < destinations.size(); ++index) {
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
            cleanup();
            return app::OperationResult::Failure(std::move(error));
        }
        hasBackup[index] = true;
    }

    std::size_t published{};
    for (; published < sources.size(); ++published) {
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
        for (std::size_t index = 0U; index < backups.size(); ++index) {
            if (!hasBackup[index] || index >= published
                || rollbackSucceeded) {
                RemoveIfPresent(backups[index]);
            }
        }
        cleanup();
        if (!rollbackSucceeded) {
            error += " Import rollback was incomplete; backup files were retained.";
        }
        return app::OperationResult::Failure(std::move(error));
    }
    for (const auto& backup : backups) {
        RemoveIfPresent(backup);
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
    std::string index;
    std::string error;
    std::vector<app::ProgramEntryId> order;
    if (IsRegularFile(IndexPath())
        && (!ReadBytes(IndexPath(), index, error)
            || !app::DecodeProgramIndex(index, order, error))) {
        return app::OperationResult::Failure(std::move(error));
    }
    order.erase(std::remove(order.begin(), order.end(), id), order.end());
    const app::OperationResult saved = SaveOrder(order);
    if (!saved.succeeded) {
        return saved;
    }
    RemoveIfPresent(EntryPath(id));
    RemoveIfPresent(ArtifactPath(id));
    RemoveIfPresent(DumpPath(id));
    return app::OperationResult::Success();
}

std::string WindowsProgramLibrary::LoadDump(app::ProgramEntryId id) const
{
    std::string dump;
    std::string error;
    if (!ReadBytes(DumpPath(id), dump, error)) {
        return "Unable to load compiled dump: " + error;
    }
    return dump;
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
