#include "runtime_cli.hpp"

#include <ostream>

namespace inputweaver::ui::cli {

void PrintRuntimeUsage(std::ostream& output) {
    output
        << "InputWeaver\n"
        << "context-aware input mapping and macro engine\n\n"
        << "Usage:\n"
        << "  InputWeaver --program <file.weavec>"
           " [--target <exe-name-or-absolute-path> | --target-global]"
           " [--allow-exec] [--dry-run] [--log <jsonl-path>] [--trace-input]"
           " [--debug-session <opaque-token>]\n\n"
        << "The command-line target overrides the compiled TARGET declaration.\n"
        << "The --dry-run option forwards physical input and simulates output effects.\n"
        << "Compiled exit rules stop the program; the default is physical Ctrl+Shift+F12.\n";
}

bool ParseRuntimeCommandLine(
    std::span<const std::filesystem::path> arguments,
    RuntimeCliOptions& options,
    std::string& errorMessage) {
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        const std::filesystem::path& argument = arguments[index];
        if (argument == std::filesystem::path{"--help"}
            || argument == std::filesystem::path{"-h"}) {
            options.showHelp = true;
        } else if (argument == std::filesystem::path{"--trace-input"}) {
            options.traceInput = true;
        } else if (argument == std::filesystem::path{"--dry-run"}) {
            options.dryRun = true;
        } else if (argument == std::filesystem::path{"--allow-exec"}) {
            options.allowExec = true;
        } else if (argument == std::filesystem::path{"--target-global"}) {
            options.targetGlobal = true;
        } else if (argument == std::filesystem::path{"--program"}) {
            ++index;
            if (index >= arguments.size() || arguments[index].empty()) {
                errorMessage = "--program requires a .weavec file path.";
                return false;
            }
            options.programPath = arguments[index];
        } else if (argument == std::filesystem::path{"--target"}) {
            ++index;
            if (index >= arguments.size() || arguments[index].empty()) {
                errorMessage = "--target requires an executable name or absolute path.";
                return false;
            }
            options.targetSelector = arguments[index];
        } else if (argument == std::filesystem::path{"--log"}) {
            ++index;
            if (index >= arguments.size() || arguments[index].empty()) {
                errorMessage = "--log requires a JSONL file path.";
                return false;
            }
            options.jsonlPath = arguments[index];
        } else if (argument == std::filesystem::path{"--debug-session"}) {
            ++index;
            if (index >= arguments.size() || arguments[index].empty()) {
                errorMessage = "--debug-session requires an opaque token.";
                return false;
            }
            options.debugSessionToken = arguments[index];
        } else {
            errorMessage = "Unknown option: " + argument.string();
            return false;
        }
    }

    if (options.showHelp) {
        return true;
    }
    if (options.targetGlobal && !options.targetSelector.empty()) {
        errorMessage = "--target and --target-global are mutually exclusive.";
        return false;
    }
    if (options.targetGlobal && options.programPath.empty()) {
        errorMessage = "--target-global is valid only with --program.";
        return false;
    }
    if (options.programPath.empty() && !options.targetSelector.empty()) {
        errorMessage = "--target is valid only with --program.";
        return false;
    }
    if (options.allowExec && options.programPath.empty()) {
        errorMessage = "--allow-exec is valid only with --program.";
        return false;
    }
    if (options.dryRun && options.programPath.empty()) {
        errorMessage = "--dry-run is valid only with --program.";
        return false;
    }
    if (options.traceInput && options.jsonlPath.empty()) {
        errorMessage = "--trace-input requires --log.";
        return false;
    }
    if (options.programPath.empty()) {
        errorMessage = "--program is required.";
        return false;
    }
    return true;
}

}  // namespace inputweaver::ui::cli
