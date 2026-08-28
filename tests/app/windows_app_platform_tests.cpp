#include "platform/windows/app/program_library.hpp"
#include "platform/windows/support/command_line.hpp"
#include "platform/windows/support/text_encoding.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] std::filesystem::path MakeTestDirectory()
{
    return std::filesystem::temp_directory_path()
        / (L"InputWeaverAppTests-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64()));
}

void Write(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void TestProgramLibrary()
{
    const std::filesystem::path directory = MakeTestDirectory();
    std::filesystem::create_directories(directory);
    {
        inputweaver::win32::WindowsProgramLibrary library(directory);
        auto loaded = library.Load();
        Check(
            loaded.succeeded && loaded.entries.empty()
                && !loaded.notices.empty(),
            "missing index creates an empty repaired library");

        const std::filesystem::path source = directory / L"Game.weave";
        Write(source, "TARGET GLOBAL\n");
        std::string sourceUtf8;
        Check(
            inputweaver::win32::WideToUtf8(source.wstring(), sourceUtf8),
            "test source path converts to UTF-8");
        const auto inspected = library.InspectSource(sourceUtf8);
        Check(
            inspected.succeeded && inspected.defaultName == "Game",
            "source inspection validates extension and derives name");

        const inputweaver::app::ProgramEntry entry{
            1U,
            "Game",
            {},
            inspected.sourceHash};
        const std::filesystem::path temporary = library.ArtifactTemporaryPath(1U);
        Write(temporary, "compiled");
        const std::vector<inputweaver::app::ProgramEntryId> order{1U};
        Check(
            library.PublishImport(
                       {inspected.normalizedPath, entry, order, false},
                       temporary,
                       "compiled dump\n")
                .succeeded,
            "compiled artifact, dump, metadata, and index publish");
        loaded = library.Load();
        Check(
            loaded.succeeded && loaded.entries.size() == 1U
                && loaded.entries[0] == entry
                && library.LoadDump(1U) == "compiled dump\n"
                && library.LoadSource(1U).text == "TARGET GLOBAL\n",
            "published program library reloads");
        Check(
            library.NamesEqual("Game", "game"),
            "display names use ordinal case-insensitive comparison");

        std::filesystem::remove(directory / L"programs" / L"programs.index");
        loaded = library.Load();
        Check(
            loaded.succeeded && loaded.entries.size() == 1U,
            "missing index rebuilds from complete entries");
        std::filesystem::remove(library.ArtifactPath(1U));
        loaded = library.Load();
        Check(
            loaded.succeeded && loaded.entries.size() == 1U
                && loaded.entries[0].compiledSourceHash == 0U
                && library.LoadSource(1U).succeeded,
            "missing artifact keeps editable source and marks it uncompiled");

        const inputweaver::app::ProgramEntry blank{2U, "Blank", {}};
        Check(
            library.PublishNew({blank, {1U, 2U}}).succeeded
                && library.LoadSource(2U).succeeded
                && library.LoadSource(2U).text.empty()
                && !std::filesystem::exists(library.ArtifactPath(2U)),
            "blank programs publish an empty editable source without an artifact");
        Check(
            library.SaveSource(2U, "A:down => tap(B);\n").succeeded
                && library.LoadSource(2U).text == "A:down => tap(B);\n"
                && library.SaveDump(2U, "generated dump\n").succeeded
                && library.LoadDump(2U) == "generated dump\n",
            "editable source and generated dumps persist independently");
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void TestCommandLineQuoting()
{
    using inputweaver::win32::QuoteCommandLineArgument;
    Check(
        QuoteCommandLineArgument(L"alpha") == L"alpha",
        "plain command-line argument is unchanged");
    Check(
        QuoteCommandLineArgument(L"") == L"\"\"",
        "empty command-line argument is quoted");
    Check(
        QuoteCommandLineArgument(L"C:\\Program Files\\")
            == L"\"C:\\Program Files\\\\\"",
        "quoted command-line argument doubles trailing backslashes");
    Check(
        QuoteCommandLineArgument(L"a\"b") == L"\"a\\\"b\"",
        "embedded command-line quote is escaped");
}

} // namespace

int main()
{
    TestProgramLibrary();
    TestCommandLineQuoting();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " Windows app platform test(s) failed.\n";
        return 1;
    }
    std::cout << "Windows app platform tests passed.\n";
    return 0;
}
