#include "compiler.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void PrintDiagnostics(
    const std::vector<inputweaver::compiler::CompileDiagnostic>& diagnostics)
{
    for (const auto& diagnostic : diagnostics) {
        std::cerr << inputweaver::compiler::FormatCompileDiagnostic(diagnostic);
    }
}

void PrintUsage()
{
    std::cerr
        << "Usage:\n"
        << "  InputWeaverCompiler compile <source.weave> [destination.weavec]\n"
        << "  InputWeaverCompiler validate <source.weave>\n"
        << "  InputWeaverCompiler dump <source.weave>\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    using namespace inputweaver::compiler;
    if (argc < 3) {
        PrintUsage();
        return 2;
    }
    const std::wstring_view command = argv[1];
    const std::filesystem::path source{argv[2]};
    if (command == L"compile") {
        if (argc > 4) {
            PrintUsage();
            return 2;
        }
        std::filesystem::path destination;
        if (argc == 4) {
            destination = std::filesystem::path{argv[3]};
        } else {
            destination = source;
            destination.replace_extension(".weavec");
        }
        const CompilerCommandResult result = CompileFile(source, destination);
        PrintDiagnostics(result.diagnostics);
        if (!result.succeeded) {
            return 1;
        }
        std::wcout << L"Wrote " << result.artifactByteLength << L" bytes to "
                   << destination.wstring() << L'\n';
        return 0;
    }
    if (command == L"validate") {
        if (argc != 3) {
            PrintUsage();
            return 2;
        }
        const CompilerCommandResult result = ValidateFile(source);
        PrintDiagnostics(result.diagnostics);
        if (!result.succeeded) {
            return 1;
        }
        std::cout << "Source is valid.\n";
        return 0;
    }
    if (command == L"dump") {
        if (argc != 3) {
            PrintUsage();
            return 2;
        }
        const CompilerCommandResult result = DumpFile(source);
        PrintDiagnostics(result.diagnostics);
        if (!result.succeeded) {
            return 1;
        }
        std::cout << result.dump;
        return 0;
    }
    PrintUsage();
    return 2;
}
