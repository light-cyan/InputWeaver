#include "compiler_cli.hpp"

#include "compiler/compiler.hpp"

#include <ostream>

namespace {

void PrintDiagnostics(
    const std::vector<inputweaver::compiler::CompileDiagnostic>& diagnostics,
    std::ostream& output) {
    for (const auto& diagnostic : diagnostics) {
        output << inputweaver::compiler::FormatCompileDiagnostic(diagnostic);
    }
}

}  // namespace

namespace inputweaver::ui::cli {

void PrintCompilerUsage(std::ostream& output) {
    output
        << "Usage:\n"
        << "  InputWeaverCompiler compile <source.weave> [destination.weavec]\n"
        << "  InputWeaverCompiler validate <source.weave>\n"
        << "  InputWeaverCompiler dump <source.weave>\n";
}

bool ParseCompilerCommandLine(
    std::span<const std::filesystem::path> arguments,
    CompilerCliOptions& options,
    std::string& errorMessage) {
    if (arguments.size() < 3U) {
        errorMessage = "A command and source file are required.";
        return false;
    }
    const std::filesystem::path& command = arguments[1];
    options.source = arguments[2];
    if (command == std::filesystem::path{"compile"}) {
        if (arguments.size() > 4U) {
            errorMessage = "compile accepts at most one destination file.";
            return false;
        }
        options.command = CompilerCliCommand::Compile;
        if (arguments.size() == 4U) {
            options.destination = arguments[3];
        } else {
            options.destination = options.source;
            options.destination.replace_extension(".weavec");
        }
        return true;
    }
    if (command == std::filesystem::path{"validate"}
        || command == std::filesystem::path{"dump"}) {
        if (arguments.size() != 3U) {
            errorMessage = command == std::filesystem::path{"validate"}
                ? "validate accepts only one source file."
                : "dump accepts only one source file.";
            return false;
        }
        options.command = command == std::filesystem::path{"validate"}
            ? CompilerCliCommand::Validate
            : CompilerCliCommand::Dump;
        return true;
    }
    errorMessage = "Unknown compiler command.";
    return false;
}

int RunCompilerCommand(
    const CompilerCliOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    using namespace inputweaver::compiler;
    if (options.command == CompilerCliCommand::Compile) {
        const CompilerCommandResult result = CompileFile(
            options.source, options.destination);
        PrintDiagnostics(result.diagnostics, errors);
        if (!result.succeeded) {
            return 1;
        }
        output << "Wrote " << result.artifactByteLength << " bytes to "
               << options.destination << '\n';
        return 0;
    }
    if (options.command == CompilerCliCommand::Validate) {
        const CompilerCommandResult result = ValidateFile(options.source);
        PrintDiagnostics(result.diagnostics, errors);
        if (!result.succeeded) {
            return 1;
        }
        output << "Source is valid.\n";
        return 0;
    }
    const CompilerCommandResult result = DumpFile(options.source);
    PrintDiagnostics(result.diagnostics, errors);
    if (!result.succeeded) {
        return 1;
    }
    output << result.dump;
    return 0;
}

}  // namespace inputweaver::ui::cli
