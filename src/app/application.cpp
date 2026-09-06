#include "application.hpp"

#include "entry_codec.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace inputweaver::app {
namespace {

inline constexpr std::size_t kMaximumConsoleLines = 2'048U;

[[nodiscard]] std::vector<ProgramEntryId> ProgramOrder(
    std::span<const ProgramEntry> programs)
{
    std::vector<ProgramEntryId> order;
    order.reserve(programs.size());
    for (const ProgramEntry& program : programs) {
        order.push_back(program.id);
    }
    return order;
}

} // namespace

Application::Application(AppPlatform& platform)
    : platform_(platform)
{
}

Application::~Application()
{
    Shutdown();
}

OperationResult Application::Initialize()
{
    if (initialized_) {
        return OperationResult::Success();
    }
    LibraryLoadResult loaded = platform_.LoadProgramLibrary();
    if (!loaded.succeeded) {
        AppendAppMessage("Library load failed: " + loaded.error);
        SetAttention(ApplicationAttention::Console);
        return OperationResult::Failure(std::move(loaded.error));
    }
    programs_ = std::move(loaded.entries);
    nextId_ = loaded.nextId == kInvalidProgramEntryId ? 1U : loaded.nextId;
    for (const std::string& notice : loaded.notices) {
        AppendAppMessage(notice);
    }
    initialized_ = true;
    shutdown_ = false;
    Changed();
    return OperationResult::Success();
}

void Application::Tick()
{
    DrainPlatformEvents();
    if (!awaitingDebugCapture_) {
        return;
    }
    const auto state = platform_.ReadDebugState();
    if (state != nullptr && state->connected && state->capturing
        && state->captureTrusted) {
        awaitingDebugCapture_ = false;
        SetAttention(ApplicationAttention::Debug);
        Changed();
    }
}

void Application::Shutdown() noexcept
{
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    awaitingDebugCapture_ = false;
    platform_.StopAllExecutors();
}

ImportPreparation Application::PrepareImport(
    std::string_view sourcePath) const
{
    const ImportSourceInfo source = platform_.InspectImportSource(sourcePath);
    if (!source.succeeded) {
        return {
            ImportPreparationStatus::Invalid,
            {},
            {},
            kInvalidProgramEntryId,
            source.error};
    }
    const ProgramEntryId conflict = FindProgramByName(source.defaultName);
    return {
        conflict == kInvalidProgramEntryId
            ? ImportPreparationStatus::Ready
            : ImportPreparationStatus::NameConflict,
        source.normalizedPath,
        source.defaultName,
        conflict,
        {}};
}

OperationResult Application::ImportProgram(
    std::string_view sourcePath,
    std::string_view displayName,
    std::optional<ProgramEntryId> overwriteId)
{
    const ImportSourceInfo source = platform_.InspectImportSource(sourcePath);
    if (!source.succeeded) {
        AppendAppMessage("Import failed: " + source.error);
        SetAttention(ApplicationAttention::Console);
        return OperationResult::Failure(source.error);
    }
    ProgramEntry entry{};
    bool overwrite = overwriteId.has_value();
    if (overwrite) {
        const ProgramEntry* existing = FindProgram(*overwriteId);
        if (existing == nullptr) {
            return OperationResult::Failure("The overwrite target no longer exists.");
        }
        entry = *existing;
    } else {
        if (!ValidDisplayName(displayName)) {
            return OperationResult::Failure("The program name is invalid.");
        }
        if (FindProgramByName(displayName) != kInvalidProgramEntryId) {
            return OperationResult::Failure("The program name is already in use.");
        }
        if (nextId_ == kInvalidProgramEntryId
            || nextId_ > kMaximumProgramEntryId) {
            return OperationResult::Failure("No program IDs remain available.");
        }
        entry.id = nextId_;
        entry.displayName = displayName;
    }
    entry.compiledSourceHash = source.sourceHash;

    std::vector<ProgramEntryId> order = ProgramOrder(programs_);
    if (!overwrite) {
        order.push_back(entry.id);
    }
    const OperationResult result = platform_.PublishImport({
        entry,
        order,
        overwrite,
        source.sourceText});
    DrainPlatformEvents();
    if (!result.succeeded) {
        AppendAppMessage("Import failed: " + result.error);
        SetAttention(ApplicationAttention::Console);
        return result;
    }
    if (!overwrite) {
        programs_.push_back(entry);
        ++nextId_;
    } else {
        *FindProgram(entry.id) = entry;
    }
    Changed();
    return OperationResult::Success();
}

OperationResult Application::CreateProgram(std::string_view displayName)
{
    if (!ValidDisplayName(displayName)) {
        return OperationResult::Failure("The program name is invalid.");
    }
    if (FindProgramByName(displayName) != kInvalidProgramEntryId) {
        return OperationResult::Failure("The program name is already in use.");
    }
    if (nextId_ == kInvalidProgramEntryId
        || nextId_ > kMaximumProgramEntryId) {
        return OperationResult::Failure("No program IDs remain available.");
    }
    ProgramEntry entry{};
    entry.id = nextId_;
    entry.displayName = displayName;
    std::vector<ProgramEntryId> order = ProgramOrder(programs_);
    order.push_back(entry.id);
    const OperationResult result = platform_.PublishNew({entry, order});
    if (!result.succeeded) {
        AppendAppMessage("Create failed: " + result.error);
        SetAttention(ApplicationAttention::Console);
        return result;
    }
    programs_.push_back(std::move(entry));
    ++nextId_;
    Changed();
    return OperationResult::Success();
}

OperationResult Application::RenameProgram(
    ProgramEntryId id,
    std::string_view displayName)
{
    ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    if (!ValidDisplayName(displayName)) {
        return OperationResult::Failure("The program name is invalid.");
    }
    if (FindProgramByName(displayName, id) != kInvalidProgramEntryId) {
        return OperationResult::Failure("The program name is already in use.");
    }
    ProgramEntry updated = *entry;
    updated.displayName = displayName;
    const OperationResult result = platform_.SaveEntry(updated);
    if (!result.succeeded) {
        AppendAppMessage("Rename failed: " + result.error);
        SetAttention(ApplicationAttention::Console);
        return result;
    }
    *entry = std::move(updated);
    Changed();
    return OperationResult::Success();
}

OperationResult Application::DeleteProgram(ProgramEntryId id)
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    if (ExecutorExists(id)) {
        const OperationResult stopped = platform_.StopExecutor(id, true);
        DrainPlatformEvents();
        if (!stopped.succeeded) {
            AppendAppMessage("Delete failed while stopping the executor: "
                + stopped.error);
            SetAttention(ApplicationAttention::Console);
            return stopped;
        }
    }
    const OperationResult removed = platform_.DeleteEntry(id);
    if (!removed.succeeded) {
        AppendAppMessage("Delete failed: " + removed.error);
        SetAttention(ApplicationAttention::Console);
        return removed;
    }
    programs_.erase(
        std::remove_if(
            programs_.begin(),
            programs_.end(),
            [id](const ProgramEntry& program) { return program.id == id; }),
        programs_.end());
    if (terminatedDebugSession_.has_value()
        && terminatedDebugSession_->programId == id) {
        terminatedDebugSession_.reset();
    }
    Changed();
    return OperationResult::Success();
}

OperationResult Application::ReorderPrograms(
    std::span<const ProgramEntryId> order)
{
    if (order.size() != programs_.size()) {
        return OperationResult::Failure("The program order is incomplete.");
    }
    std::vector<ProgramEntry> reordered;
    reordered.reserve(programs_.size());
    for (const ProgramEntryId id : order) {
        const ProgramEntry* program = FindProgram(id);
        if (program == nullptr
            || std::find_if(
                   reordered.begin(),
                   reordered.end(),
                   [id](const ProgramEntry& item) { return item.id == id; })
                != reordered.end()) {
            return OperationResult::Failure("The program order is invalid.");
        }
        reordered.push_back(*program);
    }
    const OperationResult saved = platform_.SaveOrder(order);
    if (!saved.succeeded) {
        AppendAppMessage("Reorder failed: " + saved.error);
        SetAttention(ApplicationAttention::Console);
        return saved;
    }
    programs_ = std::move(reordered);
    Changed();
    return OperationResult::Success();
}

OperationResult Application::UpdateConfiguration(
    ProgramEntryId id,
    const RunConfiguration& configuration)
{
    ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    if (!ValidRunConfiguration(configuration)) {
        return OperationResult::Failure("The run configuration is invalid.");
    }
    ProgramEntry updated = *entry;
    updated.configuration = configuration;
    const OperationResult saved = platform_.SaveEntry(updated);
    if (!saved.succeeded) {
        AppendAppMessage("Configuration update failed: " + saved.error);
        SetAttention(ApplicationAttention::Console);
        return saved;
    }
    *entry = std::move(updated);
    Changed();
    return OperationResult::Success();
}

SourceReadResult Application::ReadSource(ProgramEntryId id) const
{
    if (FindProgram(id) == nullptr) {
        return {false, {}, "The program no longer exists."};
    }
    return platform_.LoadSource(id);
}

OperationResult Application::SaveSource(
    ProgramEntryId id,
    std::string_view source)
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    const OperationResult saved = platform_.SaveSource(id, source);
    if (!saved.succeeded) {
        AppendAppMessage("Source save failed: " + saved.error);
        SetAttention(ApplicationAttention::Console);
        return saved;
    }
    Changed();
    return OperationResult::Success();
}

SourceValidationResult Application::ValidateProgram(ProgramEntryId id)
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return {false, false, {}, "The program no longer exists."};
    }
    const SourceReadResult source = platform_.LoadSource(id);
    return source.succeeded
        ? platform_.ValidateSource(*entry, source.text)
        : SourceValidationResult{false, false, {}, source.error};
}

OperationResult Application::CompileProgram(ProgramEntryId id)
{
    ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    const SourceReadResult source = platform_.LoadSource(id);
    if (!source.succeeded) {
        AppendAppMessage("Compile failed: " + source.error);
        SetAttention(ApplicationAttention::Console);
        return OperationResult::Failure(source.error);
    }
    ProgramEntry updated = *entry;
    updated.compiledSourceHash = SourceHash(source.text);
    const OperationResult compiled = platform_.CompileProgram(
        updated,
        source.text);
    DrainPlatformEvents();
    if (!compiled.succeeded) {
        AppendAppMessage("Compile failed: " + compiled.error);
        SetAttention(ApplicationAttention::Console);
        return compiled;
    }
    *entry = std::move(updated);
    Changed();
    return OperationResult::Success();
}

OperationResult Application::GenerateDump(ProgramEntryId id)
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    const SourceReadResult source = platform_.LoadSource(id);
    if (!source.succeeded) {
        return OperationResult::Failure(source.error);
    }
    const OperationResult dumped = platform_.GenerateDump(*entry, source.text);
    DrainPlatformEvents();
    if (!dumped.succeeded) {
        AppendAppMessage("Dump failed: " + dumped.error);
        SetAttention(ApplicationAttention::Console);
        return dumped;
    }
    Changed();
    return OperationResult::Success();
}

bool Application::IsCompiledCurrent(ProgramEntryId id) const
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr || entry->compiledSourceHash == 0U) {
        return false;
    }
    const SourceReadResult source = platform_.LoadSource(id);
    return source.succeeded
        && entry->compiledSourceHash == SourceHash(source.text);
}

OperationResult Application::StartProgram(
    ProgramEntryId id,
    const NextRunOptions& options)
{
    const ProgramEntry* entry = FindProgram(id);
    if (entry == nullptr) {
        return OperationResult::Failure("The program no longer exists.");
    }
    if (ExecutorExists(id)) {
        return OperationResult::Success();
    }
    if (!IsCompiledCurrent(id)) {
        const OperationResult compiled = CompileProgram(id);
        if (!compiled.succeeded) {
            return compiled;
        }
        entry = FindProgram(id);
    }
    if (options.debug) {
        const ProgramEntryId debugId = platform_.DebugProgramId();
        if (debugId != kInvalidProgramEntryId && debugId != id) {
            const OperationResult stopped = platform_.StopExecutor(debugId, true);
            DrainPlatformEvents();
            if (!stopped.succeeded) {
                AppendAppMessage("Cannot replace the debug executor: "
                    + stopped.error);
                SetAttention(ApplicationAttention::Console);
                return stopped;
            }
        }
    }
    const OperationResult launched = platform_.LaunchExecutor({*entry, options});
    DrainPlatformEvents();
    if (!launched.succeeded) {
        AppendAppMessage("Start failed: " + launched.error);
        SetAttention(ApplicationAttention::Console);
        return launched;
    }
    const bool debugActive = options.debug
        && platform_.DebugProgramId() != kInvalidProgramEntryId;
    if (debugActive) {
        terminatedDebugSession_.reset();
    }
    awaitingDebugCapture_ = debugActive;
    Changed();
    return OperationResult::Success();
}

OperationResult Application::StopProgram(ProgramEntryId id)
{
    const bool running = ExecutorExists(id);
    const bool debug = running && platform_.DebugProgramId() == id;
    const bool clearDebug = debug || (terminatedDebugSession_.has_value()
        && terminatedDebugSession_->programId == id);
    const OperationResult stopped = running
        ? platform_.StopExecutor(id, false) : OperationResult::Success();
    if (!stopped.succeeded) {
        AppendAppMessage("Stop failed: " + stopped.error);
        SetAttention(ApplicationAttention::Console);
        return stopped;
    }
    if (debug) {
        closingDebugProgramId_ = id;
        awaitingDebugCapture_ = false;
        if (attention_ == ApplicationAttention::Debug) {
            attention_ = ApplicationAttention::None;
        }
    }
    if (clearDebug) {
        terminatedDebugSession_.reset();
    }
    if (running || clearDebug) {
        Changed();
    }
    return OperationResult::Success();
}

OperationResult Application::StartCapture()
{
    const OperationResult result = platform_.StartCapture();
    if (!result.succeeded) {
        AppendAppMessage("Start capture failed: " + result.error);
        SetAttention(ApplicationAttention::Console);
    } else {
        awaitingDebugCapture_ = true;
        Changed();
    }
    return result;
}

OperationResult Application::StopCapture()
{
    const OperationResult result = platform_.StopCapture();
    if (!result.succeeded) {
        AppendAppMessage("Stop capture failed: " + result.error);
        SetAttention(ApplicationAttention::Console);
    } else {
        awaitingDebugCapture_ = false;
        Changed();
    }
    return result;
}

std::string Application::ReadDump(ProgramEntryId id) const
{
    return platform_.LoadDump(id);
}

ApplicationSnapshot Application::ReadSnapshot() const
{
    ApplicationSnapshot snapshot{};
    snapshot.version = version_;
    snapshot.programs = programs_;
    snapshot.executors = platform_.ReadExecutors();
    snapshot.consoleLines.assign(consoleLines_.begin(), consoleLines_.end());
    const ProgramEntryId activeDebugId = platform_.DebugProgramId();
    if (activeDebugId == kInvalidProgramEntryId) {
        snapshot.debugSession = terminatedDebugSession_;
    } else if (activeDebugId != closingDebugProgramId_) {
        snapshot.debugSession = DebugSessionView{
            activeDebugId,
            DebugSessionStatus::Active,
            0U,
            platform_.ReadDebugState()};
    }
    return snapshot;
}

ApplicationAttention Application::ConsumeAttention() noexcept
{
    const ApplicationAttention result = attention_;
    attention_ = ApplicationAttention::None;
    return result;
}

const ProgramEntry* Application::FindProgram(ProgramEntryId id) const
{
    const auto found = std::find_if(
        programs_.begin(),
        programs_.end(),
        [id](const ProgramEntry& program) { return program.id == id; });
    return found == programs_.end() ? nullptr : &*found;
}

ProgramEntry* Application::FindProgram(ProgramEntryId id)
{
    return const_cast<ProgramEntry*>(std::as_const(*this).FindProgram(id));
}

ProgramEntryId Application::FindProgramByName(
    std::string_view displayName,
    ProgramEntryId excluded) const noexcept
{
    for (const ProgramEntry& program : programs_) {
        if (program.id != excluded
            && platform_.NamesEqual(program.displayName, displayName)) {
            return program.id;
        }
    }
    return kInvalidProgramEntryId;
}

bool Application::ExecutorExists(ProgramEntryId id) const
{
    const std::vector<ExecutorInfo> executors = platform_.ReadExecutors();
    return std::find_if(
               executors.begin(),
               executors.end(),
               [id](const ExecutorInfo& executor) {
                   return executor.programId == id;
               }) != executors.end();
}

void Application::DrainPlatformEvents()
{
    for (const PlatformEvent& event : platform_.PollEvents()) {
        const bool exited = event.kind == PlatformEventKind::ExecutorExited;
        const bool debug = event.executorMode == ExecutorMode::Debug;
        const bool closingDebug = debug
            && event.programId == closingDebugProgramId_;
        AppendConsole(
            event.programId,
            event.programName,
            exited ? ConsoleSource::Runtime : event.source,
            exited ? "Executor exited with code "
                + std::to_string(event.exitCode) + "." : std::string_view{event.text});
        if (exited && debug) {
            awaitingDebugCapture_ = false;
            if (closingDebug) {
                closingDebugProgramId_ = kInvalidProgramEntryId;
            } else {
                terminatedDebugSession_ = DebugSessionView{
                    event.programId,
                    DebugSessionStatus::Terminated,
                    event.exitCode,
                    event.finalDebugState};
            }
        }
        if (!closingDebug && (event.kind == PlatformEventKind::Error
                || (exited && event.exitCode != 0U))) {
            SetAttention(debug ? ApplicationAttention::Debug
                               : ApplicationAttention::Console);
        }
        Changed();
    }
}

void Application::AppendConsole(
    ProgramEntryId id,
    std::string_view programName,
    ConsoleSource source,
    std::string_view text)
{
    std::size_t offset = 0U;
    do {
        const std::size_t end = text.find('\n', offset);
        std::string_view line = end == std::string_view::npos
            ? text.substr(offset)
            : text.substr(offset, end - offset);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        consoleLines_.push_back({
            nextConsoleSequence_++,
            id,
            std::string{programName},
            source,
            std::string{line}});
        if (consoleLines_.size() > kMaximumConsoleLines) {
            consoleLines_.pop_front();
        }
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1U;
    } while (offset < text.size());
}

void Application::AppendAppMessage(std::string_view text)
{
    AppendConsole(
        kInvalidProgramEntryId,
        "InputWeaver",
        ConsoleSource::App,
        text);
    Changed();
}

void Application::SetAttention(ApplicationAttention attention) noexcept
{
    if (attention == ApplicationAttention::Console
        || attention_ == ApplicationAttention::None) {
        attention_ = attention;
    }
}

void Application::Changed() noexcept
{
    ++version_;
}

} // namespace inputweaver::app
