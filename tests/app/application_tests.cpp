#include "app/application.hpp"
#include "app/entry_codec.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

class FakePlatform final : public inputweaver::app::AppPlatform {
public:
    [[nodiscard]] inputweaver::app::LibraryLoadResult LoadProgramLibrary()
        override
    {
        return {
            true,
            {{1U, "Game", {}, inputweaver::app::SourceHash(source)}},
            2U,
            {"Recovered program order."},
            {}};
    }

    [[nodiscard]] inputweaver::app::ImportSourceInfo InspectImportSource(
        std::string_view sourcePath) const override
    {
        if (!sourcePath.ends_with(".weave")) {
            return {false, {}, {}, 0U, "A .weave file is required.", {}};
        }
        const std::size_t slash = sourcePath.find_last_of("/\\");
        const std::size_t begin = slash == std::string_view::npos
            ? 0U
            : slash + 1U;
        return {
            true,
            std::string{sourcePath},
            std::string{sourcePath.substr(
                begin,
                sourcePath.size() - begin - 6U)},
            inputweaver::app::SourceHash(source),
            {},
            source};
    }

    [[nodiscard]] bool NamesEqual(
        std::string_view left,
        std::string_view right) const noexcept override
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < left.size(); ++index) {
            const auto l = static_cast<unsigned char>(left[index]);
            const auto r = static_cast<unsigned char>(right[index]);
            if (std::tolower(l) != std::tolower(r)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inputweaver::app::OperationResult PublishImport(
        const inputweaver::app::ImportPublishRequest& request) override
    {
        lastImport = request;
        events.push_back({
            inputweaver::app::PlatformEventKind::Output,
            request.entry.id,
            request.entry.displayName,
            inputweaver::app::ConsoleSource::Compiler,
            "Compiled successfully.",
            0U});
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult PublishNew(
        const inputweaver::app::ProgramPublishRequest& request) override
    {
        lastNew = request;
        savedEntry = request.entry;
        savedOrder = request.order;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult SaveEntry(
        const inputweaver::app::ProgramEntry& entry) override
    {
        savedEntry = entry;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult SaveOrder(
        std::span<const inputweaver::app::ProgramEntryId> order) override
    {
        savedOrder.assign(order.begin(), order.end());
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult DeleteEntry(
        inputweaver::app::ProgramEntryId id) override
    {
        deletedId = id;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] std::string LoadDump(
        inputweaver::app::ProgramEntryId) const override
    {
        return "compiled dump";
    }

    [[nodiscard]] inputweaver::app::SourceReadResult LoadSource(
        inputweaver::app::ProgramEntryId) const override
    {
        return {true, source, {}};
    }

    [[nodiscard]] inputweaver::app::OperationResult SaveSource(
        inputweaver::app::ProgramEntryId,
        std::string_view text) override
    {
        source = text;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::SourceValidationResult ValidateSource(
        const inputweaver::app::ProgramEntry&,
        std::string_view) override
    {
        return {true, true, {}, {}};
    }

    [[nodiscard]] inputweaver::app::OperationResult CompileProgram(
        const inputweaver::app::ProgramEntry& entry,
        std::string_view) override
    {
        ++compileCount;
        savedEntry = entry;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult GenerateDump(
        const inputweaver::app::ProgramEntry&,
        std::string_view) override
    {
        ++dumpCount;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult LaunchExecutor(
        const inputweaver::app::LaunchRequest& request) override
    {
        ++launchCount;
        executors.push_back({
            request.entry.id,
            request.options.debug
                ? inputweaver::app::ExecutorMode::Debug
                : inputweaver::app::ExecutorMode::Run,
            request.options.dryRun,
            request.options.allowExec,
            {}});
        if (request.options.debug) {
            debugId = request.entry.id;
            debugState = std::make_shared<inputweaver::debug::DebugClientState>();
            debugState->connected = true;
            debugState->capturing = true;
            debugState->captureTrusted = true;
        }
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult StopExecutor(
        inputweaver::app::ProgramEntryId id,
        bool) override
    {
        stoppedIds.push_back(id);
        executors.erase(
            std::remove_if(
                executors.begin(),
                executors.end(),
                [id](const inputweaver::app::ExecutorInfo& executor) {
                    return executor.programId == id;
                }),
            executors.end());
        if (debugId == id) {
            debugId = inputweaver::app::kInvalidProgramEntryId;
            debugState.reset();
        }
        return inputweaver::app::OperationResult::Success();
    }

    void StopAllExecutors() noexcept override
    {
        executors.clear();
        debugId = inputweaver::app::kInvalidProgramEntryId;
        debugState.reset();
    }

    [[nodiscard]] std::vector<inputweaver::app::ExecutorInfo> ReadExecutors()
        const override
    {
        return executors;
    }

    [[nodiscard]] std::vector<inputweaver::app::PlatformEvent> PollEvents()
        override
    {
        std::vector<inputweaver::app::PlatformEvent> result;
        result.swap(events);
        return result;
    }

    [[nodiscard]] inputweaver::app::ProgramEntryId DebugProgramId()
        const noexcept override
    {
        return debugId;
    }

    [[nodiscard]] std::shared_ptr<const inputweaver::debug::DebugClientState>
        ReadDebugState() const override
    {
        return debugState;
    }

    [[nodiscard]] inputweaver::app::OperationResult StartCapture() override
    {
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult StopCapture() override
    {
        return inputweaver::app::OperationResult::Success();
    }

    inputweaver::app::ImportPublishRequest lastImport{};
    inputweaver::app::ProgramPublishRequest lastNew{};
    inputweaver::app::ProgramEntry savedEntry{};
    std::vector<inputweaver::app::ProgramEntryId> savedOrder;
    inputweaver::app::ProgramEntryId deletedId{};
    std::vector<inputweaver::app::ExecutorInfo> executors;
    std::vector<inputweaver::app::PlatformEvent> events;
    std::vector<inputweaver::app::ProgramEntryId> stoppedIds;
    inputweaver::app::ProgramEntryId debugId{};
    std::shared_ptr<inputweaver::debug::DebugClientState> debugState;
    std::size_t launchCount{};
    std::size_t compileCount{};
    std::size_t dumpCount{};
    std::string source{"A:down => tap(B);"};
};

void TestEntryCodec()
{
    const inputweaver::app::ProgramEntry source{
        7U,
        "Game\\Main",
        {inputweaver::app::TargetMode::Executable,
         "game.exe",
         inputweaver::app::LoggingMode::InputTrace},
        42U};
    inputweaver::app::ProgramEntry decoded{};
    std::string error;
    Check(
        inputweaver::app::DecodeEntry(
            7U,
            inputweaver::app::EncodeEntry(source),
            decoded,
            error)
            && decoded == source,
        "entry metadata round trips");

    const std::vector<inputweaver::app::ProgramEntryId> order{7U, 2U, 9U};
    std::vector<inputweaver::app::ProgramEntryId> decodedOrder;
    Check(
        inputweaver::app::DecodeProgramIndex(
            inputweaver::app::EncodeProgramIndex(order),
            decodedOrder,
            error)
            && decodedOrder == order,
        "program order round trips");
}

void TestApplicationFlow()
{
    FakePlatform platform;
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "application initializes");
    auto snapshot = application.ReadSnapshot();
    Check(
        snapshot.programs.size() == 1U
            && snapshot.consoleLines.size() == 1U,
        "library and repair notice enter application state");

    const auto conflict = application.PrepareImport("C:/new/game.weave");
    Check(
        conflict.status
            == inputweaver::app::ImportPreparationStatus::NameConflict
            && conflict.conflictId == 1U,
        "import names use platform case-insensitive comparison");
    platform.source = "A:down => tap(C);";
    Check(
        application.ImportProgram(
            conflict.sourcePath,
            conflict.defaultName,
            conflict.conflictId)
                .succeeded
            && application.ReadSnapshot().programs[0].compiledSourceHash
                == inputweaver::app::SourceHash(platform.source),
        "overwrite import refreshes the compiled source identity");

    const auto ready = application.PrepareImport("C:/new/media.weave");
    Check(
        ready.status == inputweaver::app::ImportPreparationStatus::Ready
            && application.ImportProgram(
                   ready.sourcePath,
                   ready.defaultName)
                   .succeeded,
        "new source compiles and publishes");
    snapshot = application.ReadSnapshot();
    Check(
        snapshot.programs.size() == 2U
            && platform.lastImport.entry.id == 2U
            && platform.lastImport.order
                == std::vector<inputweaver::app::ProgramEntryId>({1U, 2U}),
        "new import receives stable ID and order");

    Check(
        application.ReadSource(1U).succeeded
            && application.SaveSource(1U, "A:down => tap(D);").succeeded
            && !application.IsCompiledCurrent(1U),
        "source edits persist and make the compiled artifact stale");
    Check(
        application.CompileProgram(1U).succeeded
            && platform.compileCount == 1U
            && application.IsCompiledCurrent(1U)
            && application.GenerateDump(1U).succeeded
            && platform.dumpCount == 1U,
        "compile and dump operations flow through the platform port");

    Check(
        application.StartProgram(1U, {}).succeeded
            && application.StartProgram(1U, {}).succeeded
            && platform.launchCount == 1U,
        "running entry is not launched twice");
    Check(
        application.StartProgram(2U, {true, true, true}).succeeded,
        "different entry starts in debug mode");
    application.Tick();
    Check(
        application.ConsumeAttention()
            == inputweaver::app::ApplicationAttention::Debug,
        "trusted debug capture requests Debug page attention");

    inputweaver::app::RunConfiguration configuration{
        inputweaver::app::TargetMode::Global,
        {},
        inputweaver::app::LoggingMode::Operational};
    Check(
        application.UpdateConfiguration(2U, configuration).succeeded
            && platform.savedEntry.configuration == configuration,
        "configuration persists through the platform port");
    const std::vector<inputweaver::app::ProgramEntryId> reversed{2U, 1U};
    Check(
        application.ReorderPrograms(reversed).succeeded
            && application.ReadSnapshot().programs[0].id == 2U,
        "program ordering persists and updates state");
    Check(
        application.DeleteProgram(2U).succeeded
            && platform.deletedId == 2U
            && application.ReadSnapshot().programs.size() == 1U,
        "deletion stops and removes the selected entry");
    Check(
        application.CreateProgram("Blank").succeeded
            && platform.lastNew.entry.id == 3U
            && application.ReadSnapshot().programs.back().displayName
                == "Blank",
        "blank programs publish source metadata and join the library");
}

} // namespace

int main()
{
    TestEntryCodec();
    TestApplicationFlow();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " application test(s) failed.\n";
        return 1;
    }
    std::cout << "Application tests passed.\n";
    return 0;
}
