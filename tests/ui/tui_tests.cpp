#include "app/application.hpp"
#include "ui/tui/support/color_scheme.hpp"
#include "ui/tui/support/interaction.hpp"
#include "ui/tui/support/text_layout.hpp"
#include "ui/tui/tui_controller.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

[[nodiscard]] std::string CanvasText(
    const inputweaver::ui::tui::Canvas& canvas)
{
    std::string text;
    for (std::size_t row = 0U; row < canvas.Height(); ++row) {
        for (std::size_t column = 0U; column < canvas.Width(); ++column) {
            const inputweaver::ui::tui::Cell& cell =
                canvas.Cells()[row * canvas.Width() + column];
            if (!cell.continuation) {
                text += inputweaver::ui::tui::EncodeUtf8(cell.codePoint);
            }
        }
        text.push_back('\n');
    }
    return text;
}

class FakePlatform final : public inputweaver::app::AppPlatform {
public:
    [[nodiscard]] inputweaver::app::LibraryLoadResult LoadProgramLibrary()
        override
    {
        return {true, {{1U, "Game", {}}}, 2U, {}, {}};
    }

    [[nodiscard]] inputweaver::app::ImportSourceInfo InspectImportSource(
        std::string_view sourcePath) const override
    {
        if (!sourcePath.ends_with(".weave")) {
            return {false, {}, {}, "A .weave file is required."};
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
            {}};
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
        imported = request.entry;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult SaveEntry(
        const inputweaver::app::ProgramEntry&) override
    {
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult SaveOrder(
        std::span<const inputweaver::app::ProgramEntryId>) override
    {
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult DeleteEntry(
        inputweaver::app::ProgramEntryId) override
    {
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] std::string LoadDump(
        inputweaver::app::ProgramEntryId) const override
    {
        return "source display=test.weave\ncontrols 1\n";
    }

    [[nodiscard]] inputweaver::app::OperationResult LaunchExecutor(
        const inputweaver::app::LaunchRequest& request) override
    {
        launched = request;
        executors.push_back({
            request.entry.id,
            request.options.debug
                ? inputweaver::app::ExecutorMode::Debug
                : inputweaver::app::ExecutorMode::Run,
            request.options.dryRun,
            request.options.allowExec,
            {}});
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult StopExecutor(
        inputweaver::app::ProgramEntryId id,
        bool) override
    {
        executors.erase(
            std::remove_if(
                executors.begin(),
                executors.end(),
                [id](const inputweaver::app::ExecutorInfo& executor) {
                    return executor.programId == id;
                }),
            executors.end());
        return inputweaver::app::OperationResult::Success();
    }

    void StopAllExecutors() noexcept override
    {
        executors.clear();
    }

    [[nodiscard]] std::vector<inputweaver::app::ExecutorInfo> ReadExecutors()
        const override
    {
        return executors;
    }

    [[nodiscard]] std::vector<inputweaver::app::PlatformEvent> PollEvents()
        override
    {
        return std::exchange(events, {});
    }

    [[nodiscard]] inputweaver::app::ProgramEntryId DebugProgramId()
        const noexcept override
    {
        return inputweaver::app::kInvalidProgramEntryId;
    }

    [[nodiscard]] std::shared_ptr<const inputweaver::debug::DebugClientState>
        ReadDebugState() const override
    {
        return debugState;
    }

    [[nodiscard]] inputweaver::app::OperationResult StartCapture() override
    {
        return inputweaver::app::OperationResult::Failure("No debug executor.");
    }

    [[nodiscard]] inputweaver::app::OperationResult StopCapture() override
    {
        return inputweaver::app::OperationResult::Failure("No debug executor.");
    }

    inputweaver::app::ProgramEntry imported{};
    inputweaver::app::LaunchRequest launched{};
    std::vector<inputweaver::app::ExecutorInfo> executors;
    std::vector<inputweaver::app::PlatformEvent> events;
    std::shared_ptr<const inputweaver::debug::DebugClientState> debugState;
};

[[nodiscard]] inputweaver::ui::tui::ColorScheme LoadColors()
{
    std::ifstream input("res/InputWeaverTUI.colors.json", std::ios::binary);
    std::ostringstream bytes;
    bytes << input.rdbuf();
    inputweaver::ui::tui::ColorScheme colors{};
    std::string error;
    Check(
        inputweaver::ui::tui::ParseColorScheme(bytes.str(), colors, error),
        "repository color scheme parses");
    return colors;
}

void TestSupport()
{
    inputweaver::ui::tui::ColorScheme invalidColors{};
    std::string colorError;
    Check(
        !inputweaver::ui::tui::ParseColorScheme(
            R"({"version":1,"colors":{}})",
            invalidColors,
            colorError)
            && !colorError.empty(),
        "incomplete color scheme is rejected");
    Check(
        inputweaver::ui::tui::Utf8DisplayWidth("A界") == 3U,
        "UTF-8 layout accounts for wide characters");
    const auto wrapped = inputweaver::ui::tui::WrapUtf8("abcdef", 4U, 2U);
    Check(
        wrapped == std::vector<std::string>({"abcd", "  ef"}),
        "text wraps with continuation indentation");
    inputweaver::ui::tui::Viewport viewport;
    viewport.Update(20U, 5U);
    Check(viewport.Top() == 15U && viewport.Following(), "viewport follows end");
    viewport.LineUp();
    viewport.Update(21U, 5U);
    Check(viewport.Top() == 14U && !viewport.Following(), "manual scroll suspends follow");
    viewport.End();
    Check(viewport.Top() == 16U && viewport.Following(), "End restores follow");
    inputweaver::ui::tui::LineEditor editor;
    editor.Set("0123456789");
    const auto endView = editor.View(5U);
    Check(
        endView.text == "…6789" && endView.cursorColumn == 5U,
        "line editor scrolls horizontally to keep the cursor visible");
    editor.Left();
    const auto stableView = editor.View(5U);
    Check(
        stableView.text == "…6789" && stableView.cursorColumn == 4U,
        "line editor preserves its viewport while the cursor remains visible");
    editor.Left();
    editor.Left();
    editor.Left();
    editor.Left();
    const auto middleView = editor.View(5U);
    Check(
        middleView.text == "…567…" && middleView.cursorColumn == 1U,
        "line editor shows both clipping directions while editing the middle");
    editor.Home();
    const auto homeView = editor.View(5U);
    Check(
        homeView.text == "0123…" && homeView.cursorColumn == 0U,
        "line editor returns to the beginning without stale clipping");
    const std::array<std::size_t, 3U> widths{5U, 5U, 5U};
    Check(
        inputweaver::ui::tui::DistributeColumns(widths, 25U, 2U)
            == std::vector<std::size_t>({0U, 10U, 20U}),
        "distributed columns expand gaps to consume available width");
    Check(
        inputweaver::ui::tui::DistributeColumns(widths, 29U, 2U, 1U)
            == std::vector<std::size_t>({3U, 12U, 21U}),
        "distributed columns share extra width with both outer margins");
    const std::array<std::size_t, 3U> packedWidths{40U, 40U, 40U};
    const auto packedRows = inputweaver::ui::tui::PackColumnRows(
        packedWidths,
        90U,
        2U);
    Check(
        packedRows.size() == 2U
            && packedRows[0].firstIndex == 0U
            && packedRows[0].count == 2U
            && packedRows[1].firstIndex == 2U
            && packedRows[1].count == 1U,
        "column rows preserve every item while wrapping to available width");
    const std::array<std::size_t, 4U> unevenWidths{30U, 10U, 10U, 10U};
    const auto balancedRows = inputweaver::ui::tui::PackColumnRows(
        unevenWidths,
        55U,
        2U);
    Check(
        balancedRows.size() == 2U
            && balancedRows[0].count == 1U
            && balancedRows[1].count == 3U,
        "wrapped column rows balance occupied width instead of filling greedily");
}

void TestController()
{
    using inputweaver::ui::tui::Key;
    FakePlatform platform;
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "test application initializes");
    const auto colors = LoadColors();
    Check(
        colors.executionCompleted
            == inputweaver::ui::tui::RgbColor{0x43U, 0xa0U, 0x47U},
        "completed execution uses the darker configured green");
    inputweaver::ui::tui::TuiController controller(application, colors);
    platform.events = {
        {inputweaver::app::PlatformEventKind::Output,
         1U,
         "Game",
         inputweaver::app::ConsoleSource::Runtime,
         "first output",
         0U},
        {inputweaver::app::PlatformEventKind::Output,
         1U,
         "Game",
         inputweaver::app::ConsoleSource::Runtime,
         "second output",
         0U},
        {inputweaver::app::PlatformEventKind::Output,
         1U,
         "Game",
         inputweaver::app::ConsoleSource::Compiler,
         "compiler output",
         0U}};
    controller.Tick();
    controller.Handle({Key::Left, 0U});
    const std::string consoleText = CanvasText(controller.Render(80U, 24U));
    const std::size_t runtimeIdentity = consoleText.find("[Game][Runtime]");
    const std::size_t firstOutput = consoleText.find("first output");
    const std::size_t secondOutput = consoleText.find("second output");
    const std::size_t compilerIdentity = consoleText.find("[Game][Compiler]");
    Check(
        runtimeIdentity != std::string::npos
            && consoleText.find("[Game][Runtime]", runtimeIdentity + 1U)
                == std::string::npos
            && runtimeIdentity < firstOutput
            && firstOutput < secondOutput
            && secondOutput < compilerIdentity,
        "Console renders one identity line for each consecutive source group");
    controller.Handle({Key::Character, U'q'});
    const auto canvas = controller.Render(80U, 30U);
    Check(
        canvas.Width() == 80U && canvas.Height() == 30U,
        "Programs page renders to requested dimensions");
    const auto minimumPrograms = controller.Render(80U, 24U);
    Check(
        minimumPrograms.Cells()[20U * 80U].codePoint == U'┌'
            && minimumPrograms.Cells()[23U * 80U].codePoint == U'└',
        "NEXT RUN remains boxed at the minimum terminal size");
    Check(
        minimumPrograms.Cells()[80U + 1U].codePoint == U' '
            && minimumPrograms.Cells()[80U + 2U].codePoint == U'[',
        "header key rows retain space between text and the border");

    controller.Handle({Key::Enter, 0U});
    const auto informationCanvas = controller.Render(80U, 24U);
    Check(
        informationCanvas.Cells()[0U].style.foreground
            == colors.focusProgramInformation,
        "Programs header color follows the focused information region");
    controller.Handle({Key::Enter, 0U});
    controller.Handle({Key::Down, 0U});
    controller.Handle({Key::Enter, 0U});
    for (const char character : std::string{"game.exe"}) {
        controller.Handle({Key::Character, static_cast<char32_t>(character)});
    }
    controller.Handle({Key::Enter, 0U});
    Check(
        application.ReadSnapshot().programs[0].configuration.target
                == inputweaver::app::TargetMode::Executable
            && application.ReadSnapshot()
                    .programs[0]
                    .configuration
                    .executableSelector
                == "game.exe",
        "Target mode and executable selector edit in the information region");
    controller.Handle({Key::Escape, 0U});

    controller.Handle({Key::Character, U't'});
    controller.Handle({Key::Character, U's'});
    controller.Handle({Key::Character, U'p'});
    controller.Handle({Key::Character, U' '});
    Check(
        platform.launched.options.debug
            && platform.launched.options.dryRun
            && platform.launched.options.allowExec,
        "T/S/P options are submitted on Space");

    controller.Handle({Key::Character, U'a'});
    for (const char character : std::string{"media.weave"}) {
        controller.Handle({Key::Character, static_cast<char32_t>(character)});
    }
    controller.Handle({Key::Enter, 0U});
    Check(
        platform.imported.id == 2U
            && application.ReadSnapshot().programs.size() == 2U,
        "Add path input imports a new program");
    auto debugState = std::make_shared<inputweaver::debug::DebugClientState>();
    debugState->connected = true;
    debugState->capturing = true;
    debugState->captureTrusted = true;
    debugState->captureEpoch = 3U;
    debugState->values = {
        {"PAUSE", {inputweaver::ValueType::State, true, 0.0, {}}},
        {"combat", {inputweaver::ValueType::State, false, 0.0, {}}},
        {"count", {inputweaver::ValueType::Number, false, 2.0, {}}},
        {"delay", {
            inputweaver::ValueType::Duration,
            false,
            0.0,
            {80'000'000}}},
    };
    inputweaver::debug::DebugPressedControl pressed{};
    pressed.control.virtualKey = 0xa2U;
    debugState->pressedControls.push_back(pressed);
    auto rule = std::make_shared<inputweaver::debug::DebugRuleProgram>();
    rule->conditionText = "combat[on] and LCtrl[held]";
    rule->actionInstructions = {
        {inputweaver::ActionOpcode::Tap, 0U, 0U},
        {inputweaver::ActionOpcode::Set, 0U, 0U},
        {inputweaver::ActionOpcode::End, 0U, 0U},
    };
    rule->actionText = "tap(B) | set(count, count + 1)";
    inputweaver::debug::DebugRuleExecution execution{};
    execution.executionMarker = 17U;
    execution.matchedUnixTimeMilliseconds = 1'725'000'000'123LL;
    execution.triggerInput.captureUnixTimeMilliseconds = 1'725'000'000'120LL;
    execution.triggerInput.control.virtualKey = 0xa2U;
    execution.triggerInput.transition = inputweaver::Transition::Down;
    execution.triggerInput.origin = inputweaver::InputOrigin::PhysicalCandidate;
    execution.triggerInput.disposition =
        inputweaver::debug::InputDisposition::Suppress;
    execution.program = std::move(rule);
    execution.result = inputweaver::RuntimeExecutionResult::Completed;
    debugState->ruleExecutions.push_back(std::move(execution));
    platform.debugState = debugState;
    controller.Tick();
    controller.Handle({Key::Right, 0U});
    Check(
        controller.CurrentPage() == inputweaver::ui::tui::Page::Debug,
        "Right opens Debug page");
    const auto minimumDebug = controller.Render(80U, 24U);
    Check(
        minimumDebug.Cells()[20U * 80U].codePoint == U'┌'
            && minimumDebug.Cells()[23U * 80U].codePoint == U'└'
            && minimumDebug.Cells()[20U * 80U].style.foreground
                == colors.healthTrusted
            && minimumDebug.Cells()[0U].style.foreground
                == colors.focusEvents,
        "HEALTH remains boxed and healthy without a reported problem");
    const std::string debugText = CanvasText(minimumDebug);
    Check(
        debugText.find("#17") != std::string::npos
            && debugText.find("EVENT ") != std::string::npos
            && debugText.find("MATCH ") != std::string::npos
            && debugText.find("LCtrl down (DROP)") != std::string::npos
            && debugText.find("LCtrl down PHY") == std::string::npos
            && debugText.find("AS   combat[on] and LCtrl[held]")
                != std::string::npos
            && debugText.find("ACT  tap(B) | set(count, count + 1)")
                != std::string::npos,
        "Debug executions render readable event, AS, and ACT lines");
    const auto findAscii = [&](std::string_view text) {
        for (std::size_t row = 0U; row < minimumDebug.Height(); ++row) {
            for (std::size_t column = 0U;
                 column + text.size() <= minimumDebug.Width();
                 ++column) {
                bool matched = true;
                for (std::size_t index = 0U; index < text.size(); ++index) {
                    matched = matched
                        && minimumDebug.Cells()[
                                row * minimumDebug.Width() + column + index]
                                .codePoint
                            == static_cast<char32_t>(
                                static_cast<unsigned char>(text[index]));
                }
                if (matched) {
                    return row * minimumDebug.Width() + column;
                }
            }
        }
        return minimumDebug.Cells().size();
    };
    const std::size_t marker = findAscii("#17");
    const std::size_t event = findAscii("EVENT ");
    const std::size_t conditionLabel = findAscii(
        "AS   combat[on] and LCtrl[held]");
    const std::size_t condition = findAscii("combat[on]");
    const std::size_t actionLabel = findAscii(
        "ACT  tap(B) | set(count, count + 1)");
    const std::size_t action = findAscii("tap(B)");
    const std::size_t stateNumber = findAscii("count=2");
    const std::size_t stateControl = findAscii("LCtrl PHY");
    Check(
        marker < minimumDebug.Cells().size()
            && minimumDebug.Cells()[marker].style.foreground == colors.mutedText
            && event < minimumDebug.Cells().size()
            && minimumDebug.Cells()[event].style.foreground
                == colors.executionCompleted
            && conditionLabel < minimumDebug.Cells().size()
            && minimumDebug.Cells()[conditionLabel].style.foreground
                == colors.mutedText
            && condition < minimumDebug.Cells().size()
            && minimumDebug.Cells()[condition].style.foreground
                == colors.executionCompleted
            && actionLabel < minimumDebug.Cells().size()
            && minimumDebug.Cells()[actionLabel].style.foreground
                == colors.mutedText
            && action < minimumDebug.Cells().size()
            && minimumDebug.Cells()[action].style.foreground
                == colors.executionCompleted,
        "execution labels are muted while entry content uses one status color");
    Check(
        debugText.find("PAUSE=on") != std::string::npos
            && debugText.find("STATE") != std::string::npos
            && debugText.find("combat=off") != std::string::npos
            && debugText.find("count=2") != std::string::npos
            && debugText.find("delay=80ms") != std::string::npos,
        "HEALTH renders PAUSE and STATE renders all user value types");
    Check(
        stateNumber < stateControl
            && stateControl < minimumDebug.Cells().size(),
        "STATE keeps user values before pressed controls");
    controller.Handle({Key::Tab, 0U});
    const auto pressedDebug = controller.Render(80U, 24U);
    Check(
        pressedDebug.Cells()[0U].style.foreground == colors.focusPressed,
        "Debug header color follows the focused pressed region");
    auto faultState = std::make_shared<inputweaver::debug::DebugClientState>();
    faultState->lastFault = inputweaver::debug::DebugClientFault::ConnectionLost;
    platform.debugState = faultState;
    controller.Tick();
    const auto faultDebug = controller.Render(80U, 24U);
    Check(
        faultDebug.Cells()[20U * 80U].style.foreground
            == colors.healthFault,
        "HEALTH uses the fault color only for a reported problem");
    controller.Handle({Key::Left, 0U});
    Check(
        controller.CurrentPage() == inputweaver::ui::tui::Page::Programs,
        "Left returns to Programs page");
}

void TestQuitNavigation()
{
    using inputweaver::ui::tui::Key;
    using inputweaver::ui::tui::Page;
    FakePlatform platform;
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "quit navigation initializes");
    inputweaver::ui::tui::TuiController controller(application, LoadColors());

    controller.Handle({Key::Left, 0U});
    controller.Handle({Key::Character, U'q'});
    Check(
        controller.Running() && controller.CurrentPage() == Page::Programs,
        "Q returns Console to Programs without exiting");

    controller.Handle({Key::Right, 0U});
    controller.Handle({Key::Character, U'q'});
    Check(
        controller.Running() && controller.CurrentPage() == Page::Programs,
        "Q returns Debug to Programs without exiting");

    controller.Handle({Key::Enter, 0U});
    controller.Handle({Key::Character, U'q'});
    Check(
        controller.Running(),
        "Q returns a secondary Programs focus to the program list");
    controller.Handle({Key::Character, U'q'});
    Check(
        !controller.Running(),
        "Q exits only from the Programs list focus");
}

} // namespace

int main()
{
    TestSupport();
    TestController();
    TestQuitNavigation();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " TUI test(s) failed.\n";
        return 1;
    }
    std::cout << "TUI tests passed.\n";
    return 0;
}
