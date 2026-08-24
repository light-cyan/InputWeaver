#include "compiled_target_resolver.hpp"

namespace inputweaver::win32 {

bool ResolveCompiledTarget(
    const CompiledProgram& program,
    bool commandLineGlobal,
    std::wstring_view commandLineExecutable,
    TargetSelectorKind& kind,
    std::wstring& selector,
    std::wstring& errorMessage) {
    if (commandLineGlobal && !commandLineExecutable.empty()) {
        errorMessage = L"Command-line executable and global target overrides are mutually exclusive.";
        return false;
    }
    if (commandLineGlobal) {
        kind = TargetSelectorKind::Global;
        selector.clear();
        return true;
    }
    if (!commandLineExecutable.empty()) {
        kind = TargetSelectorKind::Executable;
        selector.assign(commandLineExecutable);
        return true;
    }

    const TargetSelector& target = program.Settings().target;
    kind = target.kind;
    if (kind == TargetSelectorKind::Global) {
        selector.clear();
        return true;
    }
    if (kind != TargetSelectorKind::Executable
        || !target.text.IsValid()
        || target.text.value >= program.Strings().size()) {
        errorMessage = L"The compiled program does not contain a usable target selector.";
        return false;
    }
    const std::string& authored = program.Strings()[target.text.value];
    selector.assign(authored.begin(), authored.end());
    if (selector.empty()) {
        errorMessage = L"The compiled executable target is empty.";
        return false;
    }
    return true;
}

}  // namespace inputweaver::win32
