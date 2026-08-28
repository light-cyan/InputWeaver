#include "tui_controller.hpp"

#include "support/text_layout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::ui::tui {
namespace {

struct StyledSegment final {
    std::string text;
    TextStyle style{};
};

using StyledLine = std::vector<StyledSegment>;

[[nodiscard]] TextStyle Foreground(RgbColor color) noexcept
{
    return {color, {}, false, false};
}

[[nodiscard]] RgbColor RegionBorder(
    bool focused,
    RgbColor focusedColor,
    const ColorScheme& colors) noexcept
{
    return focused ? focusedColor : colors.unfocusedBorder;
}

[[nodiscard]] TextStyle ActiveFieldStyle(const ColorScheme& colors) noexcept
{
    return {
        colors.selectionActiveForeground,
        colors.selectionActiveBackground,
        true,
        false};
}

[[nodiscard]] bool EditorCursorVisible() noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    return (elapsed.count() / 500LL) % 2LL == 0LL;
}

void RenderLineEditor(
    Canvas& canvas,
    LineEditor& editor,
    std::size_t x,
    std::size_t y,
    std::size_t width,
    const ColorScheme& colors)
{
    if (width == 0U) {
        return;
    }
    const TextStyle style = ActiveFieldStyle(colors);
    canvas.Fill({x, y, width, 1U}, U' ', style);
    const LineEditorView view = editor.View(width - 1U);
    canvas.Text(x, y, view.text, width - 1U, style);
    if (EditorCursorVisible()) {
        canvas.Put(x + view.cursorColumn, y, U'▏', style);
    }
}

[[nodiscard]] std::string ConsoleSourceName(app::ConsoleSource source)
{
    switch (source) {
    case app::ConsoleSource::App:
        return "App";
    case app::ConsoleSource::Compiler:
        return "Compiler";
    case app::ConsoleSource::Runtime:
        return "Runtime";
    }
    return "App";
}

[[nodiscard]] std::string TargetText(const app::RunConfiguration& configuration)
{
    switch (configuration.target) {
    case app::TargetMode::Compiled:
        return "Compiled";
    case app::TargetMode::Executable:
        return "Executable -> " + configuration.executableSelector;
    case app::TargetMode::Global:
        return "Global";
    }
    return "Compiled";
}

[[nodiscard]] std::string TargetModeText(app::TargetMode mode)
{
    switch (mode) {
    case app::TargetMode::Compiled:
        return "Compiled";
    case app::TargetMode::Executable:
        return "Executable";
    case app::TargetMode::Global:
        return "Global";
    }
    return "Compiled";
}

[[nodiscard]] std::string LoggingText(app::LoggingMode mode)
{
    switch (mode) {
    case app::LoggingMode::Off:
        return "Off";
    case app::LoggingMode::Operational:
        return "Operational -> programs/logs/";
    case app::LoggingMode::InputTrace:
        return "Input Trace -> programs/logs/";
    }
    return "Off";
}

[[nodiscard]] std::string OnOff(bool value)
{
    return value ? "ON" : "OFF";
}

[[nodiscard]] std::string FormatTime(std::int64_t unixMilliseconds)
{
    if (unixMilliseconds <= 0) {
        return "--:--:--.---";
    }
    const std::time_t seconds = static_cast<std::time_t>(
        unixMilliseconds / 1'000LL);
    const std::int64_t milliseconds = unixMilliseconds % 1'000LL;
    const std::tm* local = std::localtime(&seconds);
    if (local == nullptr) {
        return "--:--:--.---";
    }
    std::ostringstream output;
    output << std::put_time(local, "%H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << milliseconds;
    return output.str();
}

[[nodiscard]] std::string VirtualKeyName(std::uint32_t key)
{
    if ((key >= static_cast<std::uint32_t>('0')
            && key <= static_cast<std::uint32_t>('9'))
        || (key >= static_cast<std::uint32_t>('A')
            && key <= static_cast<std::uint32_t>('Z'))) {
        return std::string(1U, static_cast<char>(key));
    }
    if (key >= 0x70U && key <= 0x87U) {
        return "F" + std::to_string(key - 0x6fU);
    }
    switch (key) {
    case 0x01U:
        return "Mouse.Left";
    case 0x02U:
        return "Mouse.Right";
    case 0x04U:
        return "Mouse.Middle";
    case 0x08U:
        return "Backspace";
    case 0x09U:
        return "Tab";
    case 0x0dU:
        return "Enter";
    case 0x10U:
        return "Shift";
    case 0x11U:
        return "Ctrl";
    case 0x12U:
        return "Alt";
    case 0x1bU:
        return "Esc";
    case 0x20U:
        return "Space";
    case 0x25U:
        return "Left";
    case 0x26U:
        return "Up";
    case 0x27U:
        return "Right";
    case 0x28U:
        return "Down";
    case 0xa0U:
        return "LShift";
    case 0xa1U:
        return "RShift";
    case 0xa2U:
        return "LCtrl";
    case 0xa3U:
        return "RCtrl";
    case 0xa4U:
        return "LAlt";
    case 0xa5U:
        return "RAlt";
    default:
        return "VK" + std::to_string(key);
    }
}

[[nodiscard]] std::string ControlName(
    const debug::DebugControlIdentity& control)
{
    if (control.virtualKey != 0U) {
        return VirtualKeyName(control.virtualKey);
    }
    if (control.hasCompiledControl) {
        return std::to_string(control.compiledIdentity.namespaceId) + ':'
            + std::to_string(control.compiledIdentity.familyId) + ':'
            + std::to_string(control.compiledIdentity.code) + ':'
            + std::to_string(control.compiledIdentity.qualifier);
    }
    return control.device == DeviceKind::Mouse
        ? "Mouse"
        : "Unknown";
}

[[nodiscard]] std::string FixedField(
    std::string_view text,
    std::size_t width)
{
    std::string field = TruncateUtf8(text, width);
    const std::size_t used = Utf8DisplayWidth(field);
    if (used < width) {
        field.append(width - used, ' ');
    }
    return field;
}

[[nodiscard]] std::string EventText(const debug::DebugInputEvent& event)
{
    std::string disposition;
    switch (event.disposition) {
    case debug::InputDisposition::NotApplicable:
        disposition = "-";
        break;
    case debug::InputDisposition::Forward:
        disposition = "PASS";
        break;
    case debug::InputDisposition::Suppress:
        disposition = "DROP";
        break;
    }
    std::string text = FormatTime(event.captureUnixTimeMilliseconds) + "  "
        + FixedField(ControlName(event.control), 16U) + "  "
        + FixedField(debug::InputOriginLabel(event.origin), 4U) + "  "
        + FixedField(disposition, 4U);
    if (event.repeatedDown) {
        text += " REPEAT";
    }
    if (event.unmatchedUp) {
        text += " NO-DOWN";
    }
    return text;
}

[[nodiscard]] std::string ActionOpcodeName(ActionOpcode opcode)
{
    switch (opcode) {
    case ActionOpcode::Press:
        return "press";
    case ActionOpcode::Release:
        return "release";
    case ActionOpcode::Tap:
        return "tap";
    case ActionOpcode::Wait:
        return "wait";
    case ActionOpcode::Gap:
        return "gap";
    case ActionOpcode::Set:
        return "set";
    case ActionOpcode::Toggle:
        return "toggle";
    case ActionOpcode::Exec:
        return "exec";
    case ActionOpcode::Jump:
        return "jump";
    case ActionOpcode::JumpIfFalse:
        return "jump-if-false";
    case ActionOpcode::RepeatInit:
        return "repeat-init";
    case ActionOpcode::RepeatCheck:
        return "repeat-check";
    case ActionOpcode::RepeatNext:
        return "repeat-next";
    case ActionOpcode::Yield:
        return "yield";
    case ActionOpcode::End:
        return "end";
    }
    return "unknown";
}

[[nodiscard]] std::string ActionText(const ActionInstruction& instruction)
{
    const std::string opcode = ActionOpcodeName(instruction.opcode);
    if (instruction.opcode == ActionOpcode::End
        || instruction.opcode == ActionOpcode::Yield
        || instruction.opcode == ActionOpcode::Gap) {
        return opcode;
    }
    if (instruction.operand1 == 0U) {
        return opcode + '(' + std::to_string(instruction.operand0) + ')';
    }
    return opcode + '(' + std::to_string(instruction.operand0) + ','
        + std::to_string(instruction.operand1) + ')';
}

[[nodiscard]] TextStyle ExecutionStyle(
    const debug::DebugRuleExecution& execution,
    const ColorScheme& colors) noexcept
{
    if (!execution.result.has_value()) {
        return Foreground(colors.executionRunning);
    }
    switch (*execution.result) {
    case RuntimeExecutionResult::Completed:
        return Foreground(colors.executionCompleted);
    case RuntimeExecutionResult::Failed:
        return Foreground(colors.executionFailed);
    case RuntimeExecutionResult::Cancelled:
        return Foreground(colors.executionCancelled);
    }
    return Foreground(colors.executionFailed);
}

[[nodiscard]] TextStyle InstructionStyle(
    const debug::DebugRuleExecution& execution,
    std::uint32_t instructionIndex,
    const ColorScheme& colors) noexcept
{
    if (execution.currentInstructionIndex == instructionIndex) {
        return Foreground(colors.instructionCurrent);
    }
    const auto found = std::find(
        execution.recentInstructionIndices.begin(),
        execution.recentInstructionIndices.end(),
        instructionIndex);
    if (found == execution.recentInstructionIndices.end()) {
        return Foreground(colors.text);
    }
    const std::size_t position = static_cast<std::size_t>(
        found - execution.recentInstructionIndices.begin());
    return Foreground(
        position == 0U && execution.recentInstructionIndices.size() == 3U
            ? colors.instructionRecentOldest
            : colors.instructionRecent);
}

void AppendInstructionLines(
    std::vector<StyledLine>& lines,
    const debug::DebugRuleExecution& execution,
    std::size_t width,
    const ColorScheme& colors)
{
    StyledLine line{{"     ", Foreground(colors.text)}};
    std::size_t used = 5U;
    if (execution.program == nullptr) {
        line.push_back({"<program unavailable>", Foreground(colors.mutedText)});
        lines.push_back(std::move(line));
        return;
    }
    for (std::size_t index = 0U;
         index < execution.program->actionInstructions.size();
         ++index) {
        const std::string token = ActionText(
            execution.program->actionInstructions[index]) + ' ';
        const std::size_t tokenWidth = Utf8DisplayWidth(token);
        if (used > 5U && used + tokenWidth > width) {
            lines.push_back(std::move(line));
            line = {{"     ", Foreground(colors.text)}};
            used = 5U;
        }
        line.push_back({
            token,
            InstructionStyle(
                execution,
                static_cast<std::uint32_t>(index),
                colors)});
        used += tokenWidth;
    }
    lines.push_back(std::move(line));
}

[[nodiscard]] std::vector<StyledLine> ExecutionLines(
    const debug::DebugClientState& state,
    std::size_t width,
    const ColorScheme& colors)
{
    std::vector<StyledLine> lines;
    for (const debug::DebugRuleExecution& execution : state.ruleExecutions) {
        const std::string trigger = EventText(execution.triggerInput);
        const std::string header = '#'
            + std::to_string(execution.executionMarker) + "  "
            + FormatTime(execution.matchedUnixTimeMilliseconds) + "  ["
            + trigger + ']';
        for (const std::string& wrapped : WrapUtf8(header, width, 5U)) {
            lines.push_back({{wrapped, ExecutionStyle(execution, colors)}});
        }
        AppendInstructionLines(lines, execution, width, colors);
    }
    return lines;
}

void RenderStyledLine(
    Canvas& canvas,
    std::size_t x,
    std::size_t y,
    std::size_t width,
    const StyledLine& line)
{
    std::size_t used = 0U;
    for (const StyledSegment& segment : line) {
        if (used >= width) {
            break;
        }
        canvas.Text(x + used, y, segment.text, width - used, segment.style);
        used += (std::min)(Utf8DisplayWidth(segment.text), width - used);
    }
}

void RenderDistributedLine(
    Canvas& canvas,
    std::size_t x,
    std::size_t y,
    std::size_t width,
    std::span<const StyledSegment> segments,
    std::size_t minimumGap = 2U,
    std::size_t minimumOuterGap = 0U)
{
    std::vector<std::size_t> widths;
    widths.reserve(segments.size());
    for (const StyledSegment& segment : segments) {
        widths.push_back(Utf8DisplayWidth(segment.text));
    }
    const std::vector<std::size_t> columns = DistributeColumns(
        widths,
        width,
        minimumGap,
        minimumOuterGap);
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        if (columns[index] >= width) {
            break;
        }
        canvas.Text(
            x + columns[index],
            y,
            segments[index].text,
            width - columns[index],
            segments[index].style);
    }
}

[[nodiscard]] std::vector<ColumnRow> PackStyledRows(
    std::span<const StyledSegment> segments,
    std::size_t width,
    std::size_t minimumGap,
    std::size_t minimumOuterGap)
{
    std::vector<std::size_t> widths;
    widths.reserve(segments.size());
    for (const StyledSegment& segment : segments) {
        widths.push_back(Utf8DisplayWidth(segment.text));
    }
    return PackColumnRows(widths, width, minimumGap, minimumOuterGap);
}

void RenderDistributedRows(
    Canvas& canvas,
    std::size_t x,
    std::size_t y,
    std::size_t width,
    std::span<const StyledSegment> segments,
    std::span<const ColumnRow> rows,
    std::size_t minimumGap,
    std::size_t minimumOuterGap)
{
    for (std::size_t rowIndex = 0U; rowIndex < rows.size(); ++rowIndex) {
        const ColumnRow row = rows[rowIndex];
        RenderDistributedLine(
            canvas,
            x,
            y + rowIndex,
            width,
            segments.subspan(row.firstIndex, row.count),
            minimumGap,
            minimumOuterGap);
    }
}

template<typename RenderRow>
void RenderViewportRows(
    Viewport& viewport,
    std::size_t itemCount,
    std::size_t visibleRows,
    RenderRow&& renderRow)
{
    viewport.Update(itemCount, visibleRows);
    for (std::size_t row = 0U; row < visibleRows; ++row) {
        const std::size_t index = viewport.Top() + row;
        if (index >= itemCount) {
            break;
        }
        renderRow(row, index);
    }
}

[[nodiscard]] std::string DebugFaultText(debug::DebugClientFault fault)
{
    switch (fault) {
    case debug::DebugClientFault::None:
        return "None";
    case debug::DebugClientFault::ConnectionLost:
        return "ConnectionLost";
    case debug::DebugClientFault::CorruptFrame:
        return "CorruptFrame";
    case debug::DebugClientFault::SessionMismatch:
        return "SessionMismatch";
    case debug::DebugClientFault::ProtocolSequenceMismatch:
        return "ProtocolSequenceMismatch";
    case debug::DebugClientFault::CaptureEpochMismatch:
        return "CaptureEpochMismatch";
    case debug::DebugClientFault::UnknownExecutionMarker:
        return "UnknownExecutionMarker";
    case debug::DebugClientFault::DebugStreamLost:
        return "DebugStreamLost";
    case debug::DebugClientFault::CapacityExceeded:
        return "CapacityExceeded";
    case debug::DebugClientFault::InconsistentState:
        return "InconsistentState";
    }
    return "Unknown";
}

[[nodiscard]] std::string RuntimeDiagnosticText(
    const debug::RuntimeIssuePayload& payload)
{
    if (payload.code == debug::IssueCode::DebugStreamOverflow) {
        return "DebugStreamOverflow(" + std::to_string(payload.droppedRecords)
            + ')';
    }
    switch (payload.issue.kind) {
    case RuntimeDiagnosticKind::ActivationFailure:
        return "ActivationFailure";
    case RuntimeDiagnosticKind::TransactionCapacity:
        return "TransactionCapacity";
    case RuntimeDiagnosticKind::PredicateFault:
        return "PredicateFault";
    case RuntimeDiagnosticKind::TaskExpressionFault:
        return "TaskExpressionFault";
    case RuntimeDiagnosticKind::TaskActionFault:
        return "TaskActionFault";
    case RuntimeDiagnosticKind::LaunchFailure:
        return "LaunchFailure";
    case RuntimeDiagnosticKind::OutputFailure:
        return "OutputFailure";
    case RuntimeDiagnosticKind::OwnershipChange:
        return "OwnershipChange";
    case RuntimeDiagnosticKind::MappingChange:
        return "MappingChange";
    case RuntimeDiagnosticKind::Cancellation:
        return "Cancellation";
    case RuntimeDiagnosticKind::TargetEligibilityChange:
        return "TargetEligibilityChange";
    case RuntimeDiagnosticKind::PhysicalStateSynchronization:
        return "PhysicalStateSynchronization";
    case RuntimeDiagnosticKind::TaskBudgetExceeded:
        return "TaskBudgetExceeded";
    case RuntimeDiagnosticKind::OutputRateExceeded:
        return "OutputRateExceeded";
    }
    return "Unknown";
}

[[nodiscard]] std::string ProgramName(
    const app::ApplicationSnapshot& snapshot,
    app::ProgramEntryId id)
{
    const auto found = std::find_if(
        snapshot.programs.begin(),
        snapshot.programs.end(),
        [id](const app::ProgramEntry& program) { return program.id == id; });
    return found == snapshot.programs.end() ? "" : found->displayName;
}

} // namespace

Canvas TuiController::Render(std::size_t width, std::size_t height)
{
    Canvas canvas(width, height, Foreground(colors_.text));
    constexpr std::size_t minimumWidth = 80U;
    constexpr std::size_t minimumHeight = 24U;
    if (width < minimumWidth || height < minimumHeight) {
        canvas.Text(
            1U,
            1U,
            "InputWeaver requires a terminal of at least 80x24.",
            width > 2U ? width - 2U : 0U,
            Foreground(colors_.healthFault));
        return canvas;
    }

    std::string headerTitle;
    RgbColor headerColor{};
    std::vector<std::string> headerKeys;
    if (page_ == Page::Console) {
        headerTitle = "InputWeaver | CONSOLE";
        headerColor = colors_.focusConsole;
        headerKeys = {
            "[↑]/[↓] Line",
            "[PgUp]/[PgDn] Page",
            "[Home] First",
            "[End] Latest",
            "[→] Programs",
            "[Q] Programs"};
    } else if (page_ == Page::Programs) {
        headerTitle = "InputWeaver | PROGRAMS";
        headerColor = programsFocus_ == ProgramsFocus::Programs
            ? colors_.focusPrograms
            : programsFocus_ == ProgramsFocus::Information
                ? colors_.focusProgramInformation
                : colors_.focusCompiledDump;
        if (mode_ == Mode::Move) {
            headerKeys = {
                "[↑]/[↓] Move", "[Enter] Save Order", "[Esc] Cancel"};
        } else if (mode_ == Mode::TargetSelect
            || mode_ == Mode::LoggingSelect) {
            headerKeys = {
                "[↑]/[↓] Mode", "[Enter] Confirm", "[Esc] Cancel"};
        } else if (mode_ == Mode::ExecutableInput) {
            headerKeys = {
                "[←]/[→] Cursor",
                "[Home]/[End] Edge",
                "[Backspace]/[Delete] Edit",
                "[Enter] Confirm",
                "[Esc] Modes"};
        } else if (mode_ == Mode::AddPath || mode_ == Mode::Rename
            || mode_ == Mode::ConflictRename) {
            headerKeys = {
                "[←]/[→] Cursor",
                "[Home]/[End] Edge",
                "[Backspace]/[Delete] Edit",
                "[Enter] Confirm",
                "[Esc] Cancel"};
        } else if (mode_ == Mode::DeleteConfirm) {
            headerKeys = {"[Enter] Delete", "[Esc] Cancel"};
        } else if (mode_ == Mode::ConflictSelect) {
            headerKeys = {
                "[↑]/[↓] Select", "[Enter] Confirm", "[Esc] Cancel"};
        } else if (programsFocus_ == ProgramsFocus::Programs) {
            headerKeys = {
                "[↑]/[↓] Select",
                "[A] Add",
                "[D] Delete",
                "[R] Rename",
                "[M] Move",
                "[Enter] Configure",
                "[Space] Start",
                "[X] Stop",
                "[Tab] Focus",
                "[←] Console",
                "[→] Debug",
                "[Q] Quit"};
        } else if (programsFocus_ == ProgramsFocus::Information) {
            headerKeys = {
                "[↑]/[↓] Field",
                "[Enter] Edit",
                "[Tab] Dump",
                "[Esc] Programs",
                "[←] Console",
                "[→] Debug",
                "[Q] Programs"};
        } else {
            headerKeys = {
                "[↑]/[↓] Line",
                "[PgUp]/[PgDn] Page",
                "[Home]/[End] Edge",
                "[Tab] Programs",
                "[Esc] Programs",
                "[Q] Programs"};
        }
    } else {
        headerTitle = "InputWeaver | DEBUG";
        headerColor = debugFocus_ == DebugFocus::Events
            ? colors_.focusEvents
            : debugFocus_ == DebugFocus::Pressed
                ? colors_.focusPressed
                : colors_.focusActionExecutions;
        const app::ExecutorInfo* debugExecutor = ExecutorFor(
            snapshot_.debugProgramId);
        const std::string debugProgram = ProgramName(
            snapshot_,
            snapshot_.debugProgramId);
        if (!debugProgram.empty()) {
            headerTitle += " | " + debugProgram;
        }
        if (debugExecutor != nullptr) {
            headerTitle += " | TRACE";
            if (debugExecutor->dryRun) {
                headerTitle += " + SAFETY";
            }
        }
        std::string focusKey = debugFocus_ == DebugFocus::Events
            ? "Event"
            : debugFocus_ == DebugFocus::Pressed ? "Pressed Row" : "Execution";
        headerKeys = {
            "[↑]/[↓] " + focusKey,
            "[PgUp]/[PgDn] Page",
            "[Home]/[End] Edge",
            "[Tab] Region",
            "[C] Start/Stop Capture",
            "[X] Stop Executor",
            "[←] Programs",
            "[Q] Programs"};
    }
    StyledLine headerSegments;
    if (!statusMessage_.empty()) {
        headerSegments.push_back({
            statusMessage_,
            Foreground(colors_.healthFault)});
    } else {
        headerSegments.reserve(headerKeys.size());
        for (const std::string& key : headerKeys) {
            headerSegments.push_back({key, Foreground(colors_.mutedText)});
        }
    }
    const std::vector<ColumnRow> headerRows = PackStyledRows(
        headerSegments,
        width - 2U,
        2U,
        1U);
    const std::size_t headerHeight = headerRows.size() + 2U;
    canvas.Box(
        {0U, 0U, width, headerHeight},
        headerTitle,
        headerColor,
        headerColor,
        colors_.text);
    RenderDistributedRows(
        canvas,
        1U,
        1U,
        width - 2U,
        headerSegments,
        headerRows,
        2U,
        1U);

    if (page_ == Page::Console) {
        const Rectangle box{0U, headerHeight, width, height - headerHeight};
        canvas.Box(
            box,
            "CONSOLE",
            colors_.focusConsole,
            colors_.focusConsole,
            colors_.text);
        const std::size_t innerWidth = width - 2U;
        std::vector<std::string> lines;
        std::string previousIdentity;
        for (const app::ConsoleLine& source : snapshot_.consoleLines) {
            const std::string identity = '[' + source.programName + "]["
                + ConsoleSourceName(source.source) + ']';
            if (identity != previousIdentity) {
                const std::vector<std::string> identityLines = WrapUtf8(
                    identity,
                    innerWidth);
                lines.insert(
                    lines.end(),
                    identityLines.begin(),
                    identityLines.end());
                previousIdentity = identity;
            }
            const std::vector<std::string> wrapped = WrapUtf8(
                source.text,
                innerWidth);
            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }
        const std::size_t visible = box.height - 2U;
        RenderViewportRows(
            consoleViewport_,
            lines.size(),
            visible,
            [&](std::size_t row, std::size_t index) {
                canvas.Text(
                    1U,
                    headerHeight + 1U + row,
                    lines[index],
                    innerWidth,
                    Foreground(colors_.text));
            });
    } else if (page_ == Page::Programs) {
        const std::array<std::pair<std::string, RgbColor>, 3U> options{{
            {"[T] Trace and Debug: " + OnOff(nextRun_.debug),
             nextRun_.debug ? colors_.statusDebug : colors_.mutedText},
            {"[S] Skip Simulated Input: " + OnOff(nextRun_.dryRun),
             nextRun_.dryRun ? colors_.statusDryRun : colors_.mutedText},
            {"[P] Authorize Execution Permission: " + OnOff(nextRun_.allowExec),
             nextRun_.allowExec
                 ? colors_.statusExecPermission
                 : colors_.mutedText}}};
        StyledLine optionSegments;
        optionSegments.reserve(options.size());
        for (const auto& [text, color] : options) {
            optionSegments.push_back({text, Foreground(color)});
        }
        const std::vector<ColumnRow> optionRows = PackStyledRows(
            optionSegments,
            width - 2U,
            3U,
            1U);
        const std::size_t nextRunHeight = optionRows.size() + 2U;
        const std::size_t contentTop = headerHeight;
        const std::size_t contentHeight = height - headerHeight - nextRunHeight;
        const std::size_t leftWidth = (std::max)(
            static_cast<std::size_t>(26U),
            width * 3U / 10U);
        const std::size_t rightWidth = width - leftWidth;
        const RgbColor programsBorder = RegionBorder(
            programsFocus_ == ProgramsFocus::Programs,
            colors_.focusPrograms,
            colors_);
        const RgbColor informationBorder = RegionBorder(
            programsFocus_ == ProgramsFocus::Information,
            colors_.focusProgramInformation,
            colors_);
        const RgbColor dumpBorder = RegionBorder(
            programsFocus_ == ProgramsFocus::Dump,
            colors_.focusCompiledDump,
            colors_);
        canvas.Box(
            {0U, contentTop, leftWidth, contentHeight},
            "PROGRAMS",
            programsBorder,
            programsBorder,
            colors_.text);
        canvas.Box(
            {leftWidth, contentTop, rightWidth, 5U},
            "PROGRAM INFORMATION",
            informationBorder,
            informationBorder,
            colors_.text);
        canvas.Box(
            {leftWidth, contentTop + 4U, rightWidth, contentHeight - 4U},
            "COMPILED DUMP",
            dumpBorder,
            dumpBorder,
            colors_.text);

        const std::size_t visiblePrograms = contentHeight - 2U;
        programsViewport_.Update(DisplayProgramCount(), visiblePrograms);
        programsViewport_.Reveal(selectedIndex_);
        for (std::size_t row = 0U; row < visiblePrograms; ++row) {
            const std::size_t index = programsViewport_.Top() + row;
            const app::ProgramEntry* program = DisplayProgram(index);
            if (program == nullptr) {
                break;
            }
            const bool selected = index == selectedIndex_;
            const TextStyle selectedStyle = {
                programsFocus_ == ProgramsFocus::Programs
                    ? colors_.selectionActiveForeground
                    : colors_.selectionInactiveForeground,
                programsFocus_ == ProgramsFocus::Programs
                    ? colors_.selectionActiveBackground
                    : colors_.selectionInactiveBackground,
                true,
                false};
            if (selected) {
                canvas.Fill(
                    {1U, contentTop + 1U + row, leftWidth - 2U, 1U},
                    U' ',
                    selectedStyle);
            }
            const app::ExecutorInfo* executor = ExecutorFor(program->id);
            std::vector<std::pair<std::string_view, RgbColor>> tags;
            if (executor != nullptr) {
                tags.push_back(executor->mode == app::ExecutorMode::Debug
                    ? std::pair<std::string_view, RgbColor>{
                        "[DBG]", colors_.statusDebug}
                    : std::pair<std::string_view, RgbColor>{
                        "[RUN]", colors_.statusRunning});
                if (executor->dryRun) {
                    tags.emplace_back("[DRY]", colors_.statusDryRun);
                }
                if (executor->allowExec) {
                    tags.emplace_back(
                        "[EXEC]",
                        colors_.statusExecPermission);
                }
            }
            const std::size_t innerWidth = leftWidth - 2U;
            std::size_t tagsWidth{};
            for (const auto& [tag, color] : tags) {
                (void)color;
                tagsWidth += Utf8DisplayWidth(tag);
            }
            const std::string name = TruncateUtf8(
                program->displayName,
                innerWidth > tagsWidth + 2U
                    ? innerWidth - tagsWidth - 2U
                    : 1U);
            const TextStyle rowStyle = selected
                ? selectedStyle
                : Foreground(colors_.text);
            canvas.Text(
                1U,
                contentTop + 1U + row,
                selected ? "> " + name : "  " + name,
                innerWidth,
                rowStyle);
            if (!tags.empty() && tagsWidth <= innerWidth) {
                std::size_t tagX = 1U + innerWidth - tagsWidth;
                for (const auto& [tag, color] : tags) {
                    const std::size_t tagWidth = Utf8DisplayWidth(tag);
                    canvas.Text(
                        tagX,
                        contentTop + 1U + row,
                        tag,
                        tagWidth,
                        selected ? selectedStyle : Foreground(color));
                    tagX += tagWidth;
                }
            }
        }

        const app::ProgramEntry* selected = SelectedProgram();
        if (selected != nullptr) {
            const TextStyle targetStyle = informationField_ == 0U
                    && programsFocus_ == ProgramsFocus::Information
                ? Foreground(colors_.focusProgramInformation)
                : Foreground(colors_.text);
            const TextStyle loggingStyle = informationField_ == 1U
                    && programsFocus_ == ProgramsFocus::Information
                ? Foreground(colors_.focusProgramInformation)
                : Foreground(colors_.text);
            const std::size_t informationX = leftWidth + 1U;
            const std::size_t valueX = informationX + 10U;
            const std::size_t valueWidth = rightWidth - 12U;
            canvas.Text(
                informationX,
                contentTop + 1U,
                "Target:   ",
                10U,
                targetStyle);
            canvas.Text(
                informationX,
                contentTop + 2U,
                "Logging:  ",
                10U,
                loggingStyle);
            if (mode_ == Mode::TargetSelect) {
                canvas.Text(
                    valueX,
                    contentTop + 1U,
                    TargetModeText(static_cast<app::TargetMode>(choice_)),
                    valueWidth,
                    ActiveFieldStyle(colors_));
            } else if (mode_ == Mode::ExecutableInput) {
                constexpr std::string_view prefix = "Executable -> ";
                const std::size_t prefixWidth = Utf8DisplayWidth(prefix);
                canvas.Text(
                    valueX,
                    contentTop + 1U,
                    prefix,
                    valueWidth,
                    targetStyle);
                if (valueWidth > prefixWidth) {
                    RenderLineEditor(
                        canvas,
                        editor_,
                        valueX + prefixWidth,
                        contentTop + 1U,
                        valueWidth - prefixWidth,
                        colors_);
                }
            } else {
                canvas.Text(
                    valueX,
                    contentTop + 1U,
                    TargetText(selected->configuration),
                    valueWidth,
                    targetStyle);
            }
            if (mode_ == Mode::LoggingSelect) {
                canvas.Text(
                    valueX,
                    contentTop + 2U,
                    LoggingText(static_cast<app::LoggingMode>(choice_)),
                    valueWidth,
                    ActiveFieldStyle(colors_));
            } else {
                canvas.Text(
                    valueX,
                    contentTop + 2U,
                    LoggingText(selected->configuration.logging),
                    valueWidth,
                    loggingStyle);
            }
        }
        const std::size_t dumpWidth = rightWidth - 2U;
        const std::size_t dumpVisible = contentHeight - 6U;
        const std::vector<std::string> dumpLines = WrapUtf8(
            dumpText_,
            dumpWidth);
        RenderViewportRows(
            dumpViewport_,
            dumpLines.size(),
            dumpVisible,
            [&](std::size_t row, std::size_t index) {
                canvas.Text(
                    leftWidth + 1U,
                    contentTop + 5U + row,
                    dumpLines[index],
                    dumpWidth,
                    Foreground(colors_.text));
            });

        const Rectangle nextRunBox{
            0U,
            contentTop + contentHeight,
            width,
            nextRunHeight};
        canvas.Box(
            nextRunBox,
            "NEXT RUN",
            colors_.unfocusedBorder,
            colors_.text,
            colors_.text);
        RenderDistributedRows(
            canvas,
            1U,
            contentTop + contentHeight + 1U,
            width - 2U,
            optionSegments,
            optionRows,
            3U,
            1U);
    } else {
        const std::size_t healthRows = 4U;
        const std::size_t healthY = height - healthRows;
        const std::size_t bodyTop = headerHeight;
        const std::size_t bodyHeight = healthY - bodyTop;
        const std::size_t topHeight = (std::max)(
            static_cast<std::size_t>(6U),
            bodyHeight * 2U / 5U);
        const std::size_t leftWidth = width * 2U / 3U;
        const RgbColor eventsBorder = RegionBorder(
            debugFocus_ == DebugFocus::Events,
            colors_.focusEvents,
            colors_);
        const RgbColor pressedBorder = RegionBorder(
            debugFocus_ == DebugFocus::Pressed,
            colors_.focusPressed,
            colors_);
        const RgbColor executionBorder = RegionBorder(
            debugFocus_ == DebugFocus::Executions,
            colors_.focusActionExecutions,
            colors_);
        const app::ExecutorInfo* debugExecutor = ExecutorFor(
            snapshot_.debugProgramId);
        canvas.Box(
            {0U, bodyTop, leftWidth, topHeight},
            "EVENTS",
            eventsBorder,
            eventsBorder,
            colors_.text);
        canvas.Box(
            {leftWidth, bodyTop, width - leftWidth, topHeight},
            "PRESSED",
            pressedBorder,
            pressedBorder,
            colors_.text);
        canvas.Box(
            {0U, bodyTop + topHeight - 1U, width,
             bodyHeight - topHeight + 1U},
            "ACTION EXECUTIONS",
            executionBorder,
            executionBorder,
            colors_.text);

        const debug::DebugClientState* debugState = snapshot_.debugState.get();
        if (debugState != nullptr) {
            const std::size_t eventVisible = topHeight - 2U;
            RenderViewportRows(
                eventsViewport_,
                debugState->recentInputEvents.size(),
                eventVisible,
                [&](std::size_t row, std::size_t index) {
                    canvas.Text(
                        1U,
                        bodyTop + 1U + row,
                        EventText(debugState->recentInputEvents[index]),
                        leftWidth - 2U,
                        Foreground(colors_.text));
                });

            const std::size_t pressedWidth = width - leftWidth - 2U;
            std::vector<std::string> pressedCells;
            std::size_t cellWidth = 1U;
            for (const debug::DebugPressedControl& pressed
                 : debugState->pressedControls) {
                std::string cell = '[' + ControlName(pressed.control) + ' '
                    + std::string{debug::InputOriginLabel(pressed.origin)} + ']';
                cellWidth = (std::max)(cellWidth, Utf8DisplayWidth(cell) + 2U);
                pressedCells.push_back(std::move(cell));
            }
            const std::size_t columns = (std::max)(
                static_cast<std::size_t>(1U),
                pressedWidth / cellWidth);
            const std::size_t pressedRows = pressedCells.empty()
                ? 0U
                : (pressedCells.size() + columns - 1U) / columns;
            const std::size_t pressedVisible = topHeight - 2U;
            RenderViewportRows(
                pressedViewport_,
                pressedRows,
                pressedVisible,
                [&](std::size_t row, std::size_t sourceRow) {
                    for (std::size_t column = 0U; column < columns; ++column) {
                        const std::size_t index = sourceRow * columns + column;
                        if (index >= pressedCells.size()) {
                            break;
                        }
                        canvas.Text(
                            leftWidth + 1U + column * cellWidth,
                            bodyTop + 1U + row,
                            pressedCells[index],
                            cellWidth,
                            Foreground(colors_.text));
                    }
                });

            const std::size_t executionWidth = width - 2U;
            const std::size_t executionVisible = bodyHeight - topHeight - 1U;
            const std::vector<StyledLine> executionLines = ExecutionLines(
                *debugState,
                executionWidth,
                colors_);
            RenderViewportRows(
                executionsViewport_,
                executionLines.size(),
                executionVisible,
                [&](std::size_t row, std::size_t index) {
                    RenderStyledLine(
                        canvas,
                        1U,
                        bodyTop + topHeight + row,
                        executionWidth,
                        executionLines[index]);
                });
        }

        const bool trusted = debugState != nullptr && debugState->captureTrusted;
        const bool recovering = debugState != nullptr
            && debugState->captureRequested && !debugState->capturing;
        const bool hasProblem = debugState != nullptr
            && (debugState->lastFault != debug::DebugClientFault::None
                || !debugState->runtimeIssues.empty());
        const RgbColor healthColor = hasProblem
            ? colors_.healthFault
            : colors_.healthTrusted;
        std::string health1;
        std::string health2;
        if (debugState == nullptr) {
            health1 = "Disconnected | Not capturing | Untrusted";
            health2 = "Fault: None | Runtime issues: 0";
        } else {
            health1 += std::string{debugState->connected ? "Connected" : "Disconnected"}
                + " | "
                + (recovering
                    ? "Recovering"
                    : debugState->capturing ? "Capturing" : "Stopped")
                + " | " + (trusted ? "Trusted" : "Untrusted")
                + " | Epoch " + std::to_string(debugState->captureEpoch);
            if (debugExecutor != nullptr && debugExecutor->dryRun) {
                health1 += " | Dry-run";
            }
            health2 += "Fault: " + DebugFaultText(debugState->lastFault)
                + " | Runtime issues: "
                + std::to_string(debugState->runtimeIssues.size());
            if (!debugState->runtimeIssues.empty()) {
                health2 += " | Latest: " + RuntimeDiagnosticText(
                    debugState->runtimeIssues.back().payload);
            }
        }
        canvas.Box(
            {0U, healthY, width, healthRows},
            "HEALTH",
            healthColor,
            healthColor,
            colors_.text);
        canvas.Text(
            1U,
            healthY + 1U,
            health1,
            width - 2U,
            Foreground(healthColor));
        canvas.Text(
            1U,
            healthY + 2U,
            health2,
            width - 2U,
            Foreground(healthColor));
    }

    const bool inlineInformationEdit = mode_ == Mode::TargetSelect
        || mode_ == Mode::ExecutableInput
        || mode_ == Mode::LoggingSelect;
    if (mode_ != Mode::None && mode_ != Mode::Move
        && !inlineInformationEdit) {
        const std::size_t modalWidth = (std::min)(
            width - 8U,
            static_cast<std::size_t>(72U));
        const std::size_t modalHeight = 7U;
        const Rectangle modal{
            (width - modalWidth) / 2U,
            (height - modalHeight) / 2U,
            modalWidth,
            modalHeight};
        std::string title;
        std::string prompt;
        if (mode_ == Mode::AddPath) {
            title = "ADD PROGRAM";
            prompt = "Path to one .weave file:";
        } else if (mode_ == Mode::Rename) {
            title = "RENAME PROGRAM";
            prompt = "New unique name:";
        } else if (mode_ == Mode::DeleteConfirm) {
            title = "DELETE PROGRAM";
            prompt = "Delete "
                + (SelectedProgram() == nullptr
                    ? std::string{"selected program"}
                    : SelectedProgram()->displayName)
                + "?";
        } else if (mode_ == Mode::ConflictSelect) {
            title = "NAME CONFLICT";
            prompt = pendingName_ + " already exists:";
        } else if (mode_ == Mode::ConflictRename) {
            title = "IMPORT AS";
            prompt = "New unique name:";
        }
        canvas.Box(
            modal,
            title,
            colors_.focusProgramInformation,
            colors_.focusProgramInformation,
            colors_.text);
        canvas.Text(
            modal.x + 2U,
            modal.y + 2U,
            prompt,
            modal.width - 4U,
            Foreground(colors_.text));
        if (mode_ == Mode::ConflictSelect) {
            const std::array conflictChoices{"Overwrite", "Rename", "Cancel"};
            for (std::size_t index = 0U; index < 3U; ++index) {
                const std::string_view choice = conflictChoices[index];
                const TextStyle style = index == choice_
                    ? TextStyle{
                        colors_.selectionActiveForeground,
                        colors_.selectionActiveBackground,
                        true,
                        false}
                    : Foreground(colors_.text);
                canvas.Text(
                    modal.x + 2U + index * 18U,
                    modal.y + 4U,
                    std::string{index == choice_ ? "> " : "  "} + choice.data(),
                    17U,
                    style);
            }
        } else if (mode_ != Mode::DeleteConfirm) {
            const std::size_t inputWidth = modal.width - 4U;
            RenderLineEditor(
                canvas,
                editor_,
                modal.x + 2U,
                modal.y + 4U,
                inputWidth,
                colors_);
        }
        if (!statusMessage_.empty()) {
            canvas.Text(
                modal.x + 2U,
                modal.y + 5U,
                statusMessage_,
                modal.width - 4U,
                Foreground(colors_.healthFault));
        }
    }
    return canvas;
}

} // namespace inputweaver::ui::tui
