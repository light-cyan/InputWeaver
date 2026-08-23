#include "compiler.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

[[nodiscard]] std::filesystem::path Utf8Path(const char* value)
{
    return std::filesystem::path{reinterpret_cast<const char8_t*>(value)};
}

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

int main(int argc, char* argv[])
{
    using namespace inputweaver::compiler;
    if (argc < 3) {
        PrintUsage();
        return 2;
    }
    const std::string_view command = argv[1];
    const std::filesystem::path source = Utf8Path(argv[2]);
    if (command == "compile") {
        if (argc > 4) {
            PrintUsage();
            return 2;
        }
        std::filesystem::path destination;
        if (argc == 4) {
            destination = Utf8Path(argv[3]);
        } else {
            destination = source;
            destination.replace_extension(".weavec");
        }
        const CompilerCommandResult result = CompileFile(source, destination);
        PrintDiagnostics(result.diagnostics);
        if (!result.succeeded) {
            return 1;
        }
        std::cout << "Wrote " << result.artifactByteLength << " bytes to "
                  << destination.string() << '\n';
        return 0;
    }
    if (command == "validate") {
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
    if (command == "dump") {
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
