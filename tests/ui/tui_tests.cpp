#include "app/application.hpp"
#include "ui/tui/support/color_scheme.hpp"
#include "ui/tui/support/interaction.hpp"
#include "ui/tui/support/source_editor.hpp"
#include "ui/tui/support/source_highlighter.hpp"
#include "ui/tui/support/text_layout.hpp"
#include "ui/tui/tui_controller.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

[[nodiscard]] std::size_t FindAscii(
    const inputweaver::ui::tui::Canvas& canvas,
    std::string_view text)
{
    for (std::size_t row = 0U; row < canvas.Height(); ++row) {
        for (std::size_t column = 0U;
             column + text.size() <= canvas.Width();
             ++column) {
            bool matched = true;
            for (std::size_t index = 0U; index < text.size(); ++index) {
                matched = matched
                    && canvas.Cells()[row * canvas.Width() + column + index]
                            .codePoint
                        == static_cast<char32_t>(
                            static_cast<unsigned char>(text[index]));
            }
            if (matched) {
                return row * canvas.Width() + column;
            }
        }
    }
    return canvas.Cells().size();
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
            {},
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
        imported = request.entry;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult PublishNew(
        const inputweaver::app::ProgramPublishRequest& request) override
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
        return dump;
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
        return validation;
    }

    [[nodiscard]] inputweaver::app::OperationResult CompileProgram(
        const inputweaver::app::ProgramEntry& entry,
        std::string_view) override
    {
        imported = entry;
        return inputweaver::app::OperationResult::Success();
    }

    [[nodiscard]] inputweaver::app::OperationResult GenerateDump(
        const inputweaver::app::ProgramEntry&,
        std::string_view) override
    {
        ++dumpCount;
        dump = "generated dump\n";
        return inputweaver::app::OperationResult::Success();
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
    std::string source{"number count = 1;\nA:down => tap(B);"};
    std::string dump{"source display=test.weave\ncontrols 1\n"};
    inputweaver::app::SourceValidationResult validation{true, true, {}, {}};
    std::size_t dumpCount{};
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

    inputweaver::ui::tui::SourceEditor sourceEditor;
    sourceEditor.Set("state combat = off;\nA:down => tap(B);");
    Check(
        sourceEditor.LineCount() == 2U
            && sourceEditor.Insert(U'x')
            && sourceEditor.Text().starts_with("xstate"),
        "source editor stores logical lines and inserts UTF-8 text");
    sourceEditor.End();
    Check(
        sourceEditor.NewLine()
            && sourceEditor.Backspace()
            && sourceEditor.LineCount() == 2U,
        "source editor splits and rejoins lines");
    sourceEditor.Set("ab\ncd");
    sourceEditor.BeginSelection();
    sourceEditor.Down();
    Check(
        sourceEditor.HasSelection()
            && sourceEditor.SelectedText() == "ab\n"
            && sourceEditor.Delete()
            && sourceEditor.Text() == "cd",
        "source editor selects and replaces ranges across lines");
    Check(
        sourceEditor.Undo() && sourceEditor.Text() == "ab\ncd"
            && sourceEditor.Redo() && sourceEditor.Text() == "cd",
        "source editor restores edits through bounded undo and redo history");
    sourceEditor.Set("a\nb\nc");
    sourceEditor.LastLine();
    const bool reachedLastLine = sourceEditor.CursorLine() == 2U;
    sourceEditor.FirstLine();
    Check(
        reachedLastLine && sourceEditor.CursorLine() == 0U,
        "source viewer navigation reaches the first and last logical lines");

    inputweaver::ui::tui::SourceHighlightState highlightState{};
    const auto hasSpan = [](
                             std::string_view source,
                             const auto& spans,
                             std::string_view text,
                             inputweaver::ui::tui::SourceTokenKind kind) {
        const std::size_t begin = source.find(text);
        return begin != std::string_view::npos
            && std::any_of(
                spans.begin(),
                spans.end(),
                [begin, text, kind](const auto& span) {
                    return span.beginByte == begin
                        && span.endByte == begin + text.size()
                        && span.kind == kind;
                });
    };
    const auto isOrdinary = [](const auto& spans, std::size_t byteOffset) {
        return std::none_of(
            spans.begin(),
            spans.end(),
            [byteOffset](const auto& span) {
                return byteOffset >= span.beginByte
                    && byteOffset < span.endByte;
            });
    };
    constexpr std::string_view declaration =
        "state combat = off; // disabled";
    const auto declarationSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            declaration,
            highlightState);
    Check(
        declarationSpans.size() == 4U
            && declarationSpans.front().kind
                == inputweaver::ui::tui::SourceTokenKind::Type
            && hasSpan(
                declaration,
                declarationSpans,
                "combat",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                declaration,
                declarationSpans,
                "off",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && declarationSpans.back().kind
                == inputweaver::ui::tui::SourceTokenKind::Comment,
        "Weave highlighter classifies types, variables, constants, and comments");
    constexpr std::string_view setting =
        "TARGET = GLOBAL; TAP_DURATION = ACTION_GAP; RAND_SEED = 42; RAND01; PAUSE";
    const auto settingSpans = inputweaver::ui::tui::HighlightWeaveLine(
        setting,
        highlightState);
    Check(
        hasSpan(
            setting,
            settingSpans,
            "TARGET",
            inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                setting,
                settingSpans,
                "GLOBAL",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                setting,
                settingSpans,
                "TAP_DURATION",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                setting,
                settingSpans,
                "ACTION_GAP",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                setting,
                settingSpans,
                "RAND_SEED",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                setting,
                settingSpans,
                "RAND01",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                setting,
                settingSpans,
                "PAUSE",
                inputweaver::ui::tui::SourceTokenKind::Variable),
        "Weave highlighter renders intrinsic values as variables");
    constexpr std::string_view pauseState = "PAUSE == on";
    const auto pauseSpans = inputweaver::ui::tui::HighlightWeaveLine(
        pauseState,
        highlightState);
    Check(
        hasSpan(
            pauseState,
            pauseSpans,
            "PAUSE",
            inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                pauseState,
                pauseSpans,
                "on",
                inputweaver::ui::tui::SourceTokenKind::Constant),
        "PAUSE uses variable color while its state comparison is a constant");
    constexpr std::string_view logicalExpression =
        "@ not combat and on or off";
    const auto logicalSpans = inputweaver::ui::tui::HighlightWeaveLine(
        logicalExpression,
        highlightState);
    Check(
        hasSpan(
            logicalExpression,
            logicalSpans,
            "combat",
            inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                logicalExpression,
                logicalSpans,
                "on",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                logicalExpression,
                logicalSpans,
                "off",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && isOrdinary(logicalSpans, logicalExpression.find("not"))
            && isOrdinary(logicalSpans, logicalExpression.find("and"))
            && isOrdinary(logicalSpans, logicalExpression.find("or"))
            && isOrdinary(logicalSpans, logicalExpression.find('@')),
        "invalid tokens and logical operators remain ordinary source text");
    constexpr std::string_view action =
        "A:down => tap(B) | wait(20ms);";
    const auto actionSpans = inputweaver::ui::tui::HighlightWeaveLine(
        action,
        highlightState);
    Check(
        hasSpan(
            action,
            actionSpans,
            "tap",
                inputweaver::ui::tui::SourceTokenKind::Action)
            && hasSpan(
                action,
                actionSpans,
                "|",
                inputweaver::ui::tui::SourceTokenKind::Action)
            && hasSpan(
                action,
                actionSpans,
                "down",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                action,
                actionSpans,
                "20ms",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                action,
                actionSpans,
                "=>",
                inputweaver::ui::tui::SourceTokenKind::Operator)
            && hasSpan(
                action,
                actionSpans,
                "A",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                action,
                actionSpans,
                "B",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && isOrdinary(actionSpans, action.find('('))
            && isOrdinary(actionSpans, action.find(')')),
        "Weave highlighter leaves delimiters ordinary while coloring semantic tokens");
    constexpr std::string_view upRule = "A:up ~> toggle;";
    const auto upSpans = inputweaver::ui::tui::HighlightWeaveLine(
        upRule,
        highlightState);
    Check(
        hasSpan(
            upRule,
            upSpans,
            "up",
            inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                upRule,
                upSpans,
                "toggle",
                inputweaver::ui::tui::SourceTokenKind::Action)
            && hasSpan(
                upRule,
                upSpans,
                "~>",
                inputweaver::ui::tui::SourceTokenKind::Operator),
        "Weave highlighter treats up as a constant and pause toggle as an action");
    constexpr std::string_view againRule =
        "Windows.VirtualKey(0x41):again => repeat 2 do | end;";
    const auto againSpans = inputweaver::ui::tui::HighlightWeaveLine(
        againRule,
        highlightState);
    Check(
        hasSpan(
            againRule,
            againSpans,
            "Windows",
            inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                againRule,
                againSpans,
                "VirtualKey",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && isOrdinary(againSpans, againRule.find('.'))
            && hasSpan(
                againRule,
                againSpans,
                "again",
                inputweaver::ui::tui::SourceTokenKind::Constant)
            && hasSpan(
                againRule,
                againSpans,
                "repeat",
                inputweaver::ui::tui::SourceTokenKind::Keyword),
        "raw controls, again transitions, and repeat loops use distinct colors");
    constexpr std::string_view mapping = "Mouse.Middle -> Keyboard.F6;";
    const auto mappingSpans = inputweaver::ui::tui::HighlightWeaveLine(
        mapping,
        highlightState);
    Check(
        hasSpan(
            mapping,
            mappingSpans,
            "Mouse",
            inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                mapping,
                mappingSpans,
                "Middle",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                mapping,
                mappingSpans,
                "Keyboard",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                mapping,
                mappingSpans,
                "F6",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && isOrdinary(mappingSpans, mapping.find('.'))
            && isOrdinary(
                mappingSpans,
                mapping.find('.', mapping.find('.') + 1U))
            && hasSpan(
                mapping,
                mappingSpans,
                "->",
                inputweaver::ui::tui::SourceTokenKind::Operator),
        "control words use the designed color while dots remain ordinary");

    inputweaver::ui::tui::SourceHighlightState contextualState{};
    (void)inputweaver::ui::tui::HighlightWeaveLine(
        "state /*",
        contextualState);
    constexpr std::string_view scalarDeclaration = "*/ enabled = off;";
    const auto scalarDeclarationSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            scalarDeclaration,
            contextualState);
    (void)inputweaver::ui::tui::HighlightWeaveLine(
        "number[]",
        contextualState);
    constexpr std::string_view arrayDeclaration = "values = [1];";
    const auto arrayDeclarationSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            arrayDeclaration,
            contextualState);
    Check(
        hasSpan(
            scalarDeclaration,
            scalarDeclarationSpans,
            "enabled",
            inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                arrayDeclaration,
                arrayDeclarationSpans,
                "values",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && isOrdinary(
                arrayDeclarationSpans,
                arrayDeclaration.find('['))
            && isOrdinary(
                arrayDeclarationSpans,
                arrayDeclaration.find(']')),
        "declarations cross lines while array brackets remain ordinary");

    const auto arrayLengthSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            "values.length",
            contextualState);
    const auto scalarLengthSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            "enabled.length",
            contextualState);
    const auto bareLengthSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            "length",
            contextualState);
    Check(
        arrayLengthSpans.size() == 2U
            && arrayLengthSpans[0].kind
                == inputweaver::ui::tui::SourceTokenKind::Variable
            && arrayLengthSpans[1].kind
                == inputweaver::ui::tui::SourceTokenKind::Variable
            && scalarLengthSpans.size() == 1U
            && bareLengthSpans.empty(),
        "only array length properties use variable coloring");

    (void)inputweaver::ui::tui::HighlightWeaveLine(
        "state Windows = off;",
        contextualState);
    (void)inputweaver::ui::tui::HighlightWeaveLine(
        "state Consumer = off;",
        contextualState);
    constexpr std::string_view dottedControls =
        "Windows /* gap */ . Keyboard . IMEOn -> Consumer . VolumeUp;";
    const auto dottedControlSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            dottedControls,
            contextualState);
    const std::size_t firstDot = dottedControls.find('.');
    const std::size_t secondDot = dottedControls.find('.', firstDot + 1U);
    const std::size_t thirdDot = dottedControls.find('.', secondDot + 1U);
    Check(
        hasSpan(
            dottedControls,
            dottedControlSpans,
            "Windows",
            inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                dottedControls,
                dottedControlSpans,
                "Keyboard",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                dottedControls,
                dottedControlSpans,
                "IMEOn",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                dottedControls,
                dottedControlSpans,
                "Consumer",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && hasSpan(
                dottedControls,
                dottedControlSpans,
                "VolumeUp",
                inputweaver::ui::tui::SourceTokenKind::Control)
            && isOrdinary(dottedControlSpans, firstDot)
            && isOrdinary(dottedControlSpans, secondDot)
            && isOrdinary(dottedControlSpans, thirdDot),
        "dotted control words cross trivia and override namespace variables without coloring dots");

    std::string longDottedControl = "Root";
    for (std::size_t segment = 0U; segment < 128U; ++segment) {
        longDottedControl.append(".Segment");
        longDottedControl.append(std::to_string(segment));
    }
    const auto longDottedSpans =
        inputweaver::ui::tui::HighlightWeaveLine(
            longDottedControl,
            contextualState);
    std::size_t coloredBytes{};
    const bool allControlWords = std::all_of(
        longDottedSpans.begin(),
        longDottedSpans.end(),
        [&coloredBytes](const auto& span) {
            coloredBytes += span.endByte - span.beginByte;
            return span.kind == inputweaver::ui::tui::SourceTokenKind::Control;
        });
    Check(
        allControlWords && longDottedSpans.size() == 129U
            && coloredBytes + 128U == longDottedControl.size(),
        "long dotted controls classify each word once while leaving dots ordinary");

    inputweaver::ui::tui::SourceEditor cachedEditor;
    cachedEditor.Set("state\nflag = off;\nflag");
    inputweaver::ui::tui::SourceHighlightDocument cachedHighlights;
    const bool builtHighlights = cachedHighlights.Update(cachedEditor);
    cachedEditor.LastLine();
    const bool reusedHighlights = !cachedHighlights.Update(cachedEditor);
    cachedEditor.FirstLine();
    cachedEditor.End();
    const bool edited = cachedEditor.InsertSpaces(1U);
    const bool rebuiltHighlights = cachedHighlights.Update(cachedEditor);
    Check(
        builtHighlights && reusedHighlights && edited && rebuiltHighlights
            && hasSpan(
                "flag = off;",
                cachedHighlights.Line(1U),
                "flag",
                inputweaver::ui::tui::SourceTokenKind::Variable)
            && hasSpan(
                "flag",
                cachedHighlights.Line(2U),
                "flag",
                inputweaver::ui::tui::SourceTokenKind::Variable),
        "document highlighting caches revisions and preserves cross-line declarations");
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
                == inputweaver::ui::tui::RgbColor{0x43U, 0xa0U, 0x47U}
            && colors.syntaxControl
                == inputweaver::ui::tui::RgbColor{0x4fU, 0xc1U, 0xffU},
        "configured colors include darker completion green and blue controls");
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
    controller.Handle({Key::Escape, 0U});
    const auto canvas = controller.Render(80U, 30U);
    Check(
        canvas.Width() == 80U && canvas.Height() == 30U,
        "Programs page renders to requested dimensions");
    const auto minimumPrograms = controller.Render(80U, 24U);
    Check(
        minimumPrograms.Cells()[20U * 80U].codePoint == U'┌'
            && minimumPrograms.Cells()[23U * 80U].codePoint == U'└',
        "NEXT RUN remains boxed at the minimum viewport size");
    Check(
        CanvasText(minimumPrograms).find("[E] Edit") == std::string::npos
            && CanvasText(minimumPrograms).find("[Z] Fullscreen")
                == std::string::npos
            && CanvasText(minimumPrograms).find("[C] Compile")
                == std::string::npos
            && CanvasText(minimumPrograms).find("[V] Source/Dump")
                == std::string::npos
            && FindAscii(minimumPrograms, "[Space] Run") / 80U >= 20U,
        "Programs header omits document commands while NEXT RUN owns Run");
    const std::size_t offModePosition = FindAscii(minimumPrograms, "[S]");
    controller.Handle({
        Key::Character,
        U'd',
        false,
        inputweaver::ui::tui::KeyEventSource::Paste});
    controller.Handle({
        Key::Enter,
        0U,
        false,
        inputweaver::ui::tui::KeyEventSource::Paste});
    Check(
        application.ReadSnapshot().programs.size() == 1U,
        "pasted text cannot execute Programs commands or confirmations");
    controller.Handle({Key::Character, U't'});
    const auto enabledMode = controller.Render(80U, 24U);
    Check(
        FindAscii(enabledMode, "[S]") == offModePosition,
        "fixed-width ON and OFF labels keep NEXT RUN positions stable");
    controller.Handle({Key::Character, U't'});

    controller.Handle({Key::Enter, 0U});
    const auto informationCanvas = controller.Render(80U, 24U);
    Check(
        informationCanvas.Cells()[0U].style.foreground
                == colors.focusProgramInformation
            && CanvasText(informationCanvas).find("[E] Edit Source")
                == std::string::npos
            && CanvasText(informationCanvas).find("[X] Stop")
                == std::string::npos,
        "information focus shows only information editing commands");
    controller.Handle({Key::Down, 0U});
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
    const auto pendingDebug = controller.Render(80U, 24U);
    Check(
        controller.CurrentPage() == inputweaver::ui::tui::Page::Debug
            && platform.executors.empty()
            && CanvasText(pendingDebug).find("STARTING") != std::string::npos
            && pendingDebug.Cells()[20U * 80U].style.foreground
                == colors.healthRecovering,
        "debug run switches pages before its delayed launch");
    std::this_thread::sleep_for(std::chrono::milliseconds{550});
    controller.Tick();
    Check(
        platform.launched.options.debug
            && platform.launched.options.dryRun
            && platform.launched.options.allowExec,
        "T/S/P options reach the delayed debug launch");
    controller.Handle({Key::Escape, 0U});

    controller.Handle({Key::Character, U'a'});
    const auto addModal = controller.Render(80U, 24U);
    Check(
        CanvasText(addModal).find("[←]/[→] Select") != std::string::npos
            && FindAscii(addModal, "> New Blank") % 80U == 20U,
        "horizontal add choices use matching keys and a centered group");
    controller.Handle({Key::Right, 0U});
    controller.Handle({Key::Enter, 0U});
    for (const char character : std::string{"media.weave"}) {
        controller.Handle({
            Key::Character,
            static_cast<char32_t>(character),
            false,
            inputweaver::ui::tui::KeyEventSource::Drop});
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
    debugState->arrays.push_back({"empty", {}});
    inputweaver::debug::DebugArrayValue gates{};
    gates.elementType = inputweaver::ArrayElementType::State;
    gates.length = 2U;
    gates.prefixCount = 2U;
    gates.elements[0].stateValue = true;
    gates.elements[1].stateValue = false;
    debugState->arrays.push_back({"gates", gates});
    inputweaver::debug::DebugArrayValue values{};
    values.elementType = inputweaver::ArrayElementType::Number;
    values.length = 10U;
    values.prefixCount = 4U;
    values.suffixCount = 4U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        values.elements[index].numberValue = static_cast<double>(index + 1U);
        values.elements[index + 4U].numberValue = static_cast<double>(index + 7U);
    }
    debugState->arrays.push_back({"values", values});
    inputweaver::debug::DebugInputEvent againInput{};
    againInput.captureUnixTimeMilliseconds = 1'725'000'000'100LL;
    againInput.control.virtualKey = 0x41U;
    againInput.transition = inputweaver::Transition::Down;
    againInput.origin = inputweaver::InputOrigin::PhysicalCandidate;
    againInput.disposition = inputweaver::debug::InputDisposition::Forward;
    againInput.againDown = true;
    debugState->recentInputEvents.push_back(againInput);
    inputweaver::debug::DebugPressedControl pressed{};
    pressed.control.virtualKey = 0xa2U;
    debugState->pressedControls.push_back(pressed);
    inputweaver::debug::DebugRuleExecution execution{};
    execution.executionMarker = 17U;
    execution.matchedUnixTimeMilliseconds = 1'725'000'000'123LL;
    execution.triggerInput.captureUnixTimeMilliseconds = 1'725'000'000'120LL;
    execution.triggerInput.control.virtualKey = 0xa2U;
    execution.triggerInput.transition = inputweaver::Transition::Down;
    execution.triggerInput.origin = inputweaver::InputOrigin::PhysicalCandidate;
    execution.triggerInput.disposition =
        inputweaver::debug::InputDisposition::Suppress;
    execution.conditionText = "combat == on and LCtrl == held";
    execution.actionText = "tap(B) | set(count, count + 1)";
    execution.result = inputweaver::RuntimeExecutionResult::Completed;
    debugState->ruleExecutions.push_back(std::move(execution));
    platform.debugState = debugState;
    controller.Tick();
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
            && debugText.find("AS   combat == on and LCtrl == held")
                != std::string::npos
            && debugText.find("ACT  tap(B) | set(count, count + 1)")
                != std::string::npos,
        "Debug executions render readable event, AS, and ACT lines");
    const std::size_t marker = FindAscii(minimumDebug, "#17");
    const std::size_t event = FindAscii(minimumDebug, "EVENT ");
    const std::size_t conditionLabel = FindAscii(
        minimumDebug,
        "AS   combat == on and LCtrl == held");
    const std::size_t condition = FindAscii(minimumDebug, "combat == on");
    const std::size_t actionLabel = FindAscii(
        minimumDebug,
        "ACT  tap(B) | set(count, count + 1)");
    const std::size_t action = FindAscii(minimumDebug, "tap(B)");
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
            && debugText.find("STATE") != std::string::npos,
        "HEALTH renders PAUSE beside the STATE viewport");
    const auto wideDebug = controller.Render(120U, 40U);
    const std::string wideDebugText = CanvasText(wideDebug);
    Check(
        wideDebugText.find("combat=off") != std::string::npos
            && wideDebugText.find("count=2") != std::string::npos
            && wideDebugText.find("delay=80ms") != std::string::npos
            && wideDebugText.find("[empty[0]=[]]") != std::string::npos
            && wideDebugText.find("[gates[2]=[on, off]]") != std::string::npos
            && wideDebugText.find("AGAIN") != std::string::npos,
        "STATE shows all scalars and short arrays with explicit array lengths");
    const std::size_t gatesPosition = FindAscii(wideDebug, "[gates[2]=[on, off]]");
    const std::size_t valuesPosition = FindAscii(wideDebug, "[values[10]=[");
    Check(
        gatesPosition < wideDebug.Cells().size()
            && valuesPosition < wideDebug.Cells().size()
            && wideDebugText.find("10]]") != std::string::npos,
        "STATE wraps array cells without dropping their suffix");
    const std::string veryWideDebugText = CanvasText(controller.Render(360U, 30U));
    Check(
        veryWideDebugText.find(
            "[values[10]=[1, 2, 3, 4, ..., 7, 8, 9, 10]]")
            != std::string::npos,
        "STATE renders exact length with bounded prefix and suffix for long arrays");
    const std::size_t stateNumber = FindAscii(wideDebug, "count=2");
    const std::size_t stateControl = FindAscii(wideDebug, "LCtrl PHY");
    Check(
        stateNumber < stateControl
            && stateControl < wideDebug.Cells().size(),
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

void TestSourceEditorPage()
{
    using inputweaver::ui::tui::Key;
    FakePlatform platform;
    platform.dump.clear();
    platform.source =
        "TARGET = GLOBAL;\nnumber count = 1;\nA:down => tap(B);";
    platform.validation = {
        true,
        false,
        {{2U, 8U, 5U, "Unknown value."}},
        {}};
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "editor application initializes");
    const auto colors = LoadColors();
    inputweaver::ui::tui::TuiController controller(application, colors);
    std::this_thread::sleep_for(std::chrono::milliseconds{450});
    controller.Tick();
    controller.Handle({Key::Tab, 0U});
    controller.Handle({Key::Tab, 0U});

    const auto sourceCanvas = controller.Render(100U, 30U);
    const std::string sourceText = CanvasText(sourceCanvas);
    const std::size_t keyword = FindAscii(sourceCanvas, "number");
    const std::size_t error = FindAscii(sourceCanvas, "count");
    const std::size_t target = FindAscii(sourceCanvas, "TARGET");
    const std::size_t global = FindAscii(sourceCanvas, "GLOBAL");
    Check(
        sourceText.find("SOURCE") != std::string::npos
            && sourceText.find("1 │ TARGET = GLOBAL;") != std::string::npos
            && sourceText.find("2 │ number count = 1;") != std::string::npos,
        "Programs page shows editable source with line numbers and a gutter");
    Check(
        target < sourceCanvas.Cells().size()
            && sourceCanvas.Cells()[target].style.foreground
                == colors.syntaxVariable
            && global < sourceCanvas.Cells().size()
            && sourceCanvas.Cells()[global].style.foreground
                == colors.syntaxConstant,
        "source view distinguishes TARGET variables from GLOBAL constants");
    Check(
        keyword < sourceCanvas.Cells().size()
            && sourceCanvas.Cells()[keyword].style.foreground
                == colors.syntaxType
            && sourceCanvas.Cells()[keyword].style.hasBackground
            && sourceCanvas.Cells()[keyword].style.background
                == colors.editorErrorLine,
        "source view colors types green and gives error lines a red background");
    Check(
        error < sourceCanvas.Cells().size()
            && sourceCanvas.Cells()[error].style.foreground
                == colors.healthFault
            && sourceCanvas.Cells()[error].style.underline,
        "validation diagnostics render as red underlined source ranges");

    controller.Handle({Key::Character, U'e'});
    const auto splitEdit = controller.Render(100U, 30U);
    const std::size_t splitExit = FindAscii(splitEdit, "[Esc] Exit");
    Check(
        splitExit < splitEdit.Cells().size() && splitExit % 100U == 88U
            && CanvasText(splitEdit).find("[Enter] New Line")
                == std::string::npos
            && CanvasText(splitEdit).find("NEXT RUN") == std::string::npos,
        "split source editing places Escape at the top title's right edge");
    controller.Handle({Key::Escape, 0U});

    controller.Handle({Key::Character, U'z'});
    const auto fullscreenView = controller.Render(100U, 30U);
    Check(
        CanvasText(fullscreenView).find("DOCUMENT FULLSCREEN")
                != std::string::npos
            && CanvasText(fullscreenView).find("NEXT RUN")
                != std::string::npos
            && CanvasText(fullscreenView).find("[Space] Run")
                != std::string::npos,
        "fullscreen source keeps NEXT RUN at the bottom outside editing");
    controller.Handle({Key::Character, U'e'});
    const auto cursorVisible = controller.Render(100U, 30U);
    const std::size_t fullscreenExit = FindAscii(cursorVisible, "[Esc] Exit");
    Check(
        CanvasText(cursorVisible).find("NEXT RUN") == std::string::npos
            && fullscreenExit < cursorVisible.Cells().size()
            && fullscreenExit % 100U == 88U
            && CanvasText(cursorVisible).find("[Enter] New Line")
                == std::string::npos
            && std::any_of(
            cursorVisible.Cells().begin(),
            cursorVisible.Cells().end(),
            [&colors](const inputweaver::ui::tui::Cell& cell) {
                return cell.codePoint == U'▏'
                    && cell.style.hasBackground
                    && cell.style.background == colors.editorCurrentLine;
            }),
        "fullscreen editing is distraction-free and preserves the line background");
    std::this_thread::sleep_for(std::chrono::milliseconds{350});
    const auto cursorHidden = controller.Render(100U, 30U);
    Check(
        std::none_of(
            cursorHidden.Cells().begin(),
            cursorHidden.Cells().end(),
            [](const inputweaver::ui::tui::Cell& cell) {
                return cell.codePoint == U'▏';
            }),
        "source cursor blink reaches its hidden phase");
    controller.Handle({Key::Right, 0U});
    const auto movedCursor = controller.Render(100U, 30U);
    Check(
        std::any_of(
            movedCursor.Cells().begin(),
            movedCursor.Cells().end(),
            [](const inputweaver::ui::tui::Cell& cell) {
                return cell.codePoint == U'▏';
            }),
        "moving the source cursor restarts its visible blink phase");
    controller.Handle({Key::Home, 0U});
    for (std::size_t index = 0U; index < 6U; ++index) {
        controller.Handle({Key::Right, 0U, true});
    }
    const auto selectionCanvas = controller.Render(100U, 30U);
    const std::size_t selection = FindAscii(selectionCanvas, "TARGET");
    controller.Handle({Key::Copy, 0U});
    const auto copied = controller.TakeClipboardText();
    Check(
        selection < selectionCanvas.Cells().size()
            && selectionCanvas.Cells()[selection].style.background
                == colors.selectionActiveBackground
            && copied.has_value() && *copied == "TARGET",
        "Shift movement selects source and Copy publishes the selected text");
    controller.Handle({Key::Cut, 0U});
    const auto cut = controller.TakeClipboardText();
    const bool cutRemovedSelection =
        CanvasText(controller.Render(100U, 30U)).find("TARGET")
        == std::string::npos;
    controller.Handle({Key::Undo, 0U});
    const bool undoRestoredSelection =
        CanvasText(controller.Render(100U, 30U)).find("TARGET")
        != std::string::npos;
    controller.Handle({Key::Redo, 0U});
    const bool redoRemovedSelection =
        CanvasText(controller.Render(100U, 30U)).find("TARGET")
        == std::string::npos;
    controller.Handle({Key::Undo, 0U});
    Check(
        cut.has_value() && *cut == "TARGET" && cutRemovedSelection
            && undoRestoredSelection && redoRemovedSelection,
        "Cut, Undo, and Redo share the source editor command history");
    controller.Handle({Key::Home, 0U});
    controller.Handle({Key::Character, U'/'});
    controller.Handle({Key::Escape, 0U});
    Check(
        platform.source.starts_with("/TARGET")
            && CanvasText(controller.Render(100U, 30U))
                    .find("DOCUMENT FULLSCREEN")
                != std::string::npos,
        "first Escape saves and exits editing while preserving fullscreen");
    controller.Handle({Key::Escape, 0U});
    Check(
        CanvasText(controller.Render(100U, 30U)).find("DOCUMENT FULLSCREEN")
            == std::string::npos,
        "second Escape restores the split Programs layout");

    controller.Handle({Key::Character, U'v'});
    Check(
        platform.dumpCount == 1U
            && CanvasText(controller.Render(100U, 30U)).find("generated dump")
                != std::string::npos,
        "V generates a missing dump and shows it in the source region");
    controller.Handle({Key::Character, U'v'});
    controller.Handle({Key::Character, U' '});
    Check(!platform.executors.empty(), "Space runs the selected program");
    controller.Handle({Key::Character, U'x'});
    Check(platform.executors.empty(), "X stops the selected program");

    controller.Handle({Key::Escape, 0U});
    controller.Handle({Key::Character, U'a'});
    controller.Handle({Key::Enter, 0U});
    controller.Handle({Key::Enter, 0U});
    Check(
        application.ReadSnapshot().programs.size() == 2U,
        "A creates a named blank program without an import path");
}

void TestBackgroundAndExitNavigation()
{
    using inputweaver::ui::tui::Key;
    using inputweaver::ui::tui::Page;
    FakePlatform platform;
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "quit navigation initializes");
    inputweaver::ui::tui::TuiController controller(application, LoadColors());

    controller.Handle({Key::Left, 0U});
    controller.Handle({Key::Escape, 0U});
    Check(
        controller.Running() && controller.CurrentPage() == Page::Programs,
        "Escape returns Console to Programs without exiting");

    controller.Handle({Key::Right, 0U});
    controller.Handle({Key::Escape, 0U});
    Check(
        controller.Running() && controller.CurrentPage() == Page::Programs,
        "Escape returns Debug to Programs without exiting");

    controller.Handle({Key::Enter, 0U});
    controller.Handle({Key::Escape, 0U});
    Check(
        controller.Running(),
        "Escape returns a secondary Programs focus to the program list");
    platform.executors.push_back({
        1U,
        inputweaver::app::ExecutorMode::Run,
        false,
        false,
        {}});
    controller.Handle({Key::Escape, 0U});
    Check(
        controller.Running()
            && controller.TakeBackgroundRequest()
            && platform.executors.size() == 1U,
        "Escape backgrounds the TUI without stopping managed executors");
    Check(
        !controller.TakeBackgroundRequest(),
        "the background request is consumed once");
    Check(controller.RequestExit(), "an explicit exit request succeeds");
    Check(
        !controller.Running() && platform.executors.empty(),
        "an explicit exit request stops the TUI and managed executors");
}

} // namespace

int main()
{
    TestSupport();
    TestController();
    TestSourceEditorPage();
    TestBackgroundAndExitNavigation();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " TUI test(s) failed.\n";
        return 1;
    }
    std::cout << "TUI tests passed.\n";
    return 0;
}
