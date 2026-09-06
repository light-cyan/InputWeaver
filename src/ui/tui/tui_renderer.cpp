#include "tui_controller.hpp"

#include "support/source_highlighter.hpp"
#include "support/text_layout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

[[nodiscard]] bool EditorCursorVisible(
    std::chrono::steady_clock::time_point visibleSince) noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - visibleSince);
    return (elapsed.count() / 300LL) % 2LL == 0LL;
}

void RenderLineEditor(
    Canvas& canvas,
    LineEditor& editor,
    std::size_t x,
    std::size_t y,
    std::size_t width,
    std::chrono::steady_clock::time_point cursorVisibleSince,
    const ColorScheme& colors)
{
    if (width == 0U) {
        return;
    }
    const TextStyle style = ActiveFieldStyle(colors);
    canvas.Fill({x, y, width, 1U}, U' ', style);
    const LineEditorView view = editor.View(width - 1U);
    canvas.Text(x, y, view.text, width - 1U, style);
    if (EditorCursorVisible(cursorVisibleSince)) {
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
    return value ? "ON " : "OFF";
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

[[nodiscard]] std::string_view TransitionText(Transition transition) noexcept
{
    switch (transition) {
    case Transition::Down: return "down";
    case Transition::Up: return "up";
    case Transition::Move: return "move";
    case Transition::VerticalWheel: return "wheel-v";
    case Transition::HorizontalWheel: return "wheel-h";
    }
    return "unknown";
}

[[nodiscard]] std::string_view DispositionText(
    debug::InputDisposition disposition) noexcept
{
    switch (disposition) {
    case debug::InputDisposition::NotApplicable: return "-";
    case debug::InputDisposition::Forward: return "PASS";
    case debug::InputDisposition::Suppress: return "DROP";
    }
    return "unknown";
}

[[nodiscard]] std::string DebugNumberText(double value);
[[nodiscard]] std::string DebugValueText(const debug::DebugValue& value);

#include "debug_mouse_format.inc"

[[nodiscard]] std::string EventText(const debug::DebugInputEvent& event)
{
    std::string text = FormatTime(event.captureUnixTimeMilliseconds) + "  "
        + FixedField(ControlName(event.control), 16U) + "  "
        + FixedField(TransitionText(event.transition), 7U) + "  "
        + FixedField(debug::InputOriginLabel(event.origin), 4U) + "  "
        + FixedField(DispositionText(event.disposition), 4U);
    if (event.againDown) {
        text += " AGAIN";
    }
    if (event.unmatchedUp) {
        text += " NO-DOWN";
    }
    return text;
}

[[nodiscard]] std::string ExecutionEventText(
    const debug::DebugInputEvent& event, const debug::DebugClientState& state)
{
    if (IsNumericMouseEvent(event)) return MouseEventText(event, state);
    std::string text = ControlName(event.control) + ' '
        + std::string{TransitionText(event.transition)};
    if (event.disposition != debug::InputDisposition::NotApplicable) {
        text += " (" + std::string{DispositionText(event.disposition)} + ')';
    }
    if (event.againDown) {
        text += " AGAIN";
    }
    if (event.unmatchedUp) {
        text += " NO-DOWN";
    }
    return text;
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

void AppendExecutionField(
    std::vector<StyledLine>& lines,
    const debug::DebugRuleExecution& execution,
    std::string_view firstPrefix,
    std::string_view text,
    std::size_t width,
    const ColorScheme& colors)
{
    constexpr std::string_view continuationPrefix = "       ";
    constexpr std::size_t prefixWidth = 7U;
    const TextStyle executionStyle = ExecutionStyle(execution, colors);
    const std::vector<std::string> wrapped = WrapUtf8(
        text,
        width - prefixWidth);
    for (std::size_t index = 0U; index < wrapped.size(); ++index) {
        lines.push_back({
            {std::string{index == 0U ? firstPrefix : continuationPrefix},
             Foreground(colors.mutedText)},
            {wrapped[index], executionStyle}});
    }
}

[[nodiscard]] std::vector<StyledLine> ExecutionLines(
    const debug::DebugClientState& state,
    std::size_t width,
    const ColorScheme& colors)
{
    std::vector<StyledLine> lines;
    for (const debug::DebugRuleExecution& execution : state.ruleExecutions) {
        const std::string marker = '#'
            + std::to_string(execution.executionMarker) + "  ";
        const std::string header = "EVENT "
            + FormatTime(execution.triggerInput.captureUnixTimeMilliseconds)
            + "  MATCH "
            + FormatTime(execution.matchedUnixTimeMilliseconds) + "  "
            + ExecutionEventText(execution.triggerInput, state);
        const std::size_t markerWidth = Utf8DisplayWidth(marker);
        const std::vector<std::string> wrapped = WrapUtf8(
            header,
            width > markerWidth ? width - markerWidth : 1U);
        for (std::size_t index = 0U; index < wrapped.size(); ++index) {
            lines.push_back({
                {index == 0U ? marker : std::string(markerWidth, ' '),
                 Foreground(colors.mutedText)},
                {wrapped[index], ExecutionStyle(execution, colors)}});
        }
        AppendExecutionField(
            lines,
            execution,
            "  AS   ",
            execution.conditionText.empty() ? "always" : execution.conditionText,
            width,
            colors);
        AppendExecutionField(
            lines,
            execution,
            "  ACT  ",
            execution.actionText.empty() ? "<none>" : execution.actionText,
            width,
            colors);
    }
    return lines;
}

[[nodiscard]] std::string DebugNumberText(double value)
{
    std::ostringstream output;
    output << std::setprecision(15) << value;
    return output.str();
}

[[nodiscard]] std::string DebugValueText(const debug::DebugValue& value)
{
    switch (value.type) {
    case ValueType::State:
        return value.stateValue ? "on" : "off";
    case ValueType::Number:
        return DebugNumberText(value.numberValue);
    case ValueType::Duration: {
        constexpr std::int64_t nanosecondsPerMillisecond = 1'000'000LL;
        constexpr std::int64_t nanosecondsPerSecond = 1'000LL
            * nanosecondsPerMillisecond;
        constexpr std::int64_t nanosecondsPerMinute = 60LL
            * nanosecondsPerSecond;
        const std::int64_t nanoseconds = value.durationValue.nanoseconds;
        if (nanoseconds == 0) {
            return "0ms";
        }
        if (nanoseconds % nanosecondsPerMinute == 0) {
            return std::to_string(nanoseconds / nanosecondsPerMinute) + "min";
        }
        if (nanoseconds % nanosecondsPerSecond == 0) {
            return std::to_string(nanoseconds / nanosecondsPerSecond) + 's';
        }
        if (nanoseconds % nanosecondsPerMillisecond == 0) {
            return std::to_string(nanoseconds / nanosecondsPerMillisecond) + "ms";
        }
        return std::to_string(nanoseconds) + "ns";
    }
    }
    return "?";
}

[[nodiscard]] std::string DebugArrayText(
    const debug::DebugArrayState& array)
{
    std::string text = '[' + array.name + '['
        + std::to_string(array.value.length) + "]=[";
    const std::size_t prefix = array.value.prefixCount;
    const std::size_t suffix = array.value.suffixCount;
    for (std::size_t index = 0U; index < prefix + suffix; ++index) {
        if (index != 0U) {
            text += ", ";
        }
        if (index == prefix && suffix != 0U) {
            text += "..., ";
        }
        text += array.value.elementType == ArrayElementType::State
            ? (array.value.elements[index].stateValue ? "on" : "off")
            : DebugNumberText(array.value.elements[index].numberValue);
    }
    text += "]]";
    return text;
}

[[nodiscard]] std::string DebugPauseText(
    const debug::DebugClientState* state)
{
    if (state != nullptr) {
        for (const debug::DebugVariableState& value : state->values) {
            if (value.name == "PAUSE" && value.value.type == ValueType::State) {
                return DebugValueText(value.value);
            }
        }
    }
    return "unavailable";
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
    return RuntimeDiagnosticKindName(payload.issue.kind);
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

[[nodiscard]] RgbColor SourceTokenColor(
    SourceTokenKind kind,
    const ColorScheme& colors) noexcept
{
    switch (kind) {
    case SourceTokenKind::Keyword:
        return colors.syntaxKeyword;
    case SourceTokenKind::Type:
        return colors.syntaxType;
    case SourceTokenKind::Variable:
        return colors.syntaxVariable;
    case SourceTokenKind::Constant:
        return colors.syntaxConstant;
    case SourceTokenKind::Control:
        return colors.syntaxControl;
    case SourceTokenKind::Action:
        return colors.syntaxAction;
    case SourceTokenKind::Operator:
        return colors.syntaxOperator;
    case SourceTokenKind::String:
        return colors.syntaxString;
    case SourceTokenKind::Comment:
        return colors.syntaxComment;
    }
    return colors.text;
}

[[nodiscard]] bool DiagnosticOnLine(
    std::span<const app::SourceDiagnostic> diagnostics,
    std::size_t line) noexcept
{
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [line](const app::SourceDiagnostic& diagnostic) {
            return diagnostic.line == line + 1U;
        });
}

[[nodiscard]] std::size_t DecimalDigits(std::size_t value) noexcept
{
    std::size_t digits = 1U;
    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
}

[[nodiscard]] bool DiagnosticCovers(
    std::span<const app::SourceDiagnostic> diagnostics,
    std::size_t line,
    std::size_t byte) noexcept
{
    for (const app::SourceDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.line != line + 1U) {
            continue;
        }
        const std::size_t begin = diagnostic.column == 0U
            ? 0U
            : diagnostic.column - 1U;
        const std::size_t length = (std::max)(
            static_cast<std::size_t>(diagnostic.byteLength),
            static_cast<std::size_t>(1U));
        if (byte >= begin && byte < begin + length) {
            return true;
        }
    }
    return false;
}

void RenderSource(
    Canvas& canvas,
    SourceEditor& editor,
    SourceHighlightDocument& highlights,
    std::span<const app::SourceDiagnostic> diagnostics,
    Rectangle body,
    bool active,
    bool editing,
    std::chrono::steady_clock::time_point cursorVisibleSince,
    const ColorScheme& colors)
{
    if (body.width < 8U || body.height == 0U) {
        return;
    }
    const std::size_t numberWidth = DecimalDigits(editor.LineCount());
    const std::size_t gutterWidth = numberWidth + 3U;
    const std::size_t codeWidth = body.width > gutterWidth
        ? body.width - gutterWidth
        : 1U;
    editor.PrepareView(
        body.height,
        codeWidth,
        editing
            ? SourceHorizontalTracking::Cursor
            : SourceHorizontalTracking::Manual);

    (void)highlights.Update(editor);
    for (std::size_t row = 0U; row < body.height; ++row) {
        const std::size_t lineIndex = editor.TopLine() + row;
        if (lineIndex >= editor.LineCount()) {
            break;
        }
        const bool cursorLine = lineIndex == editor.CursorLine();
        const bool activeLine = active && cursorLine;
        const bool errorLine = DiagnosticOnLine(diagnostics, lineIndex);
        TextStyle base = Foreground(colors.text);
        if (activeLine || errorLine) {
            base.background = errorLine
                ? colors.editorErrorLine
                : colors.editorCurrentLine;
            base.hasBackground = true;
            canvas.Fill(
                {body.x, body.y + row, body.width, 1U},
                U' ',
                base);
        }
        std::string number = std::to_string(lineIndex + 1U);
        if (number.size() < numberWidth) {
            number.insert(0U, numberWidth - number.size(), ' ');
        }
        TextStyle gutter = Foreground(colors.mutedText);
        if (activeLine || errorLine) {
            gutter.background = base.background;
            gutter.hasBackground = true;
        }
        canvas.Text(body.x, body.y + row, number, numberWidth, gutter);
        canvas.Put(
            body.x + numberWidth + 1U,
            body.y + row,
            U'│',
            gutter);

        const std::string_view line = editor.Line(lineIndex);
        const std::span<const SourceTokenSpan> spans = highlights.Line(lineIndex);
        std::size_t offset{};
        std::size_t displayColumn{};
        std::size_t spanIndex{};
        Utf8CodePoint codePoint{};
        while (NextUtf8CodePoint(line, offset, codePoint)) {
            const std::size_t characterWidth = codePoint.displayWidth;
            if (displayColumn < editor.LeftColumn()) {
                displayColumn += characterWidth;
                continue;
            }
            const std::size_t visibleColumn = displayColumn
                - editor.LeftColumn();
            if (visibleColumn + characterWidth > codeWidth) {
                break;
            }
            TextStyle style = base;
            while (spanIndex < spans.size()
                && codePoint.byteOffset >= spans[spanIndex].endByte) {
                ++spanIndex;
            }
            if (spanIndex < spans.size()
                && codePoint.byteOffset >= spans[spanIndex].beginByte) {
                style.foreground = SourceTokenColor(spans[spanIndex].kind, colors);
            }
            if (editing
                && editor.IsSelected(lineIndex, codePoint.byteOffset)) {
                style.foreground = colors.selectionActiveForeground;
                style.background = colors.selectionActiveBackground;
                style.hasBackground = true;
            }
            if (DiagnosticCovers(diagnostics, lineIndex, codePoint.byteOffset)) {
                style.foreground = colors.healthFault;
                style.underline = true;
            }
            canvas.Put(
                body.x + gutterWidth + visibleColumn,
                body.y + row,
                codePoint.value == U'\t' ? U' ' : codePoint.value,
                style);
            displayColumn += characterWidth;
        }
        for (const app::SourceDiagnostic& diagnostic : diagnostics) {
            if (diagnostic.line != lineIndex + 1U) {
                continue;
            }
            const std::size_t errorByte = diagnostic.column == 0U
                ? 0U
                : diagnostic.column - 1U;
            if (errorByte < line.size()) {
                continue;
            }
            const std::size_t errorColumn = Utf8DisplayWidth(line)
                + errorByte - line.size();
            if (errorColumn >= editor.LeftColumn()
                && errorColumn - editor.LeftColumn() < codeWidth) {
                TextStyle errorStyle = base;
                errorStyle.foreground = colors.healthFault;
                errorStyle.underline = true;
                canvas.Put(
                    body.x + gutterWidth + errorColumn - editor.LeftColumn(),
                    body.y + row,
                    U' ',
                    errorStyle);
            }
        }
        if (editing && cursorLine && EditorCursorVisible(cursorVisibleSince)) {
            const std::size_t cursorColumn = editor.CursorDisplayColumn();
            if (cursorColumn >= editor.LeftColumn()
                && cursorColumn - editor.LeftColumn() < codeWidth) {
                TextStyle cursorStyle = base;
                cursorStyle.foreground = colors.text;
                canvas.Put(
                    body.x + gutterWidth + cursorColumn - editor.LeftColumn(),
                    body.y + row,
                    U'▏',
                    cursorStyle);
            }
        }
    }
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
    std::string_view pageRail;
    RgbColor headerColor{};
    std::vector<std::string> headerKeys;
    const app::DebugSessionView* displayedDebugSession =
        snapshot_.debugSession.has_value()
        ? &*snapshot_.debugSession
        : nullptr;
    if (pendingDebugRun_.has_value() && displayedDebugSession != nullptr
        && displayedDebugSession->status
            == app::DebugSessionStatus::Terminated) {
        displayedDebugSession = nullptr;
    }
    const bool debugTerminated = displayedDebugSession != nullptr
        && displayedDebugSession->status == app::DebugSessionStatus::Terminated;
    const app::ProgramEntryId displayedDebugId =
        displayedDebugSession != nullptr
        ? displayedDebugSession->programId
        : pendingDebugRun_.has_value()
            ? pendingDebugRun_->id
            : app::kInvalidProgramEntryId;
    const app::ExecutorInfo* displayedDebugExecutor = debugTerminated
        ? nullptr
        : ExecutorFor(displayedDebugId);
    if (page_ == Page::Console) {
        pageRail = "‹[ CONSOLE · Program · Debug ]›";
        headerTitle = "InputWeaver | " + std::string{pageRail};
        headerColor = colors_.focusConsole;
        headerKeys = {
            "[↑]/[↓] Line",
            "[PgUp]/[PgDn] Page",
            "[Home] First",
            "[End] Latest",
            "[Esc] Program"};
    } else if (page_ == Page::Program) {
        pageRail = "‹[ Console · PROGRAM · Debug ]›";
        headerTitle = "InputWeaver | " + std::string{pageRail};
        headerColor = programRegion_ == ProgramRegion::List
            ? colors_.focusProgram
            : programRegion_ == ProgramRegion::Information
                ? colors_.focusProgramInformation
                : colors_.focusSource;
        if (DocumentFullscreen()) {
            headerTitle += " | DOCUMENT FULLSCREEN";
        }
        switch (mode_) {
        case Mode::Move:
            headerKeys = {
                "[↑]/[↓] Move", "[Enter] Save Order", "[Esc] Cancel"};
            break;
        case Mode::TargetSelect:
        case Mode::LoggingSelect:
            headerKeys = {
                "[↑]/[↓] Mode", "[Enter] Confirm", "[Esc] Cancel"};
            break;
        case Mode::ExecutableInput:
        case Mode::AddPath:
        case Mode::NewName:
        case Mode::Rename:
        case Mode::ConflictRename:
            headerKeys = {
                "[←]/[→] Cursor",
                "[Home]/[End] Edge",
                "[Backspace]/[Delete] Edit",
                "[Enter] Confirm",
                mode_ == Mode::ExecutableInput
                    ? "[Esc] Modes"
                    : "[Esc] Cancel"};
            break;
        case Mode::DeleteConfirm:
            headerKeys = {"[Enter] Delete", "[Esc] Cancel"};
            break;
        case Mode::AddSelect:
        case Mode::ConflictSelect:
            headerKeys = {
                "[←]/[→] Select", "[Enter] Confirm", "[Esc] Cancel"};
            break;
        case Mode::None:
            if (SourceEditing()) {
                break;
            }
            if (programInteraction_ == RegionInteraction::Selecting) {
                headerKeys = {
                    "[Tab] Region",
                    "[Arrow Keys] Region",
                    "[Enter] Enter",
                    "[X] Stop",
                    "[Esc] Background"};
                break;
            }
            switch (programRegion_) {
            case ProgramRegion::List:
                headerKeys = {
                    "[↑]/[↓] Select",
                    "[A] Add",
                    "[D] Delete",
                    "[M] Move",
                    "[X] Stop",
                    "[Esc] Regions"};
                break;
            case ProgramRegion::Information:
                headerKeys = {
                    "[↑]/[↓] Field",
                    "[Enter] Edit",
                    "[Esc] Regions"};
                break;
            case ProgramRegion::Source:
                headerKeys = {
                    "[↑]/[↓] Line",
                    "[PgUp]/[PgDn] Page",
                    "[Home]/[End] First/Last",
                    "[E] Edit",
                    "[V] Source/Dump",
                    "[X] Stop",
                    DocumentFullscreen()
                        ? "[Esc] Split View"
                        : "[Esc] Regions"};
                if (documentView_ == DocumentView::Source) {
                    headerKeys.insert(
                        headerKeys.begin() + 1,
                        "[←]/[→] Pan");
                }
                if (!DocumentFullscreen()) {
                    headerKeys.insert(
                        headerKeys.end() - 3,
                        "[Z] Fullscreen");
                }
                break;
            }
            if (!DocumentFullscreen()) {
                headerKeys.insert(headerKeys.begin(), "[Tab] Region");
            }
            break;
        }
    } else {
        pageRail = "‹[ Console · Program · DEBUG ]›";
        headerTitle = "InputWeaver | " + std::string{pageRail};
        headerColor = debugRegion_ == DebugRegion::Events
            ? colors_.focusEvents
            : debugRegion_ == DebugRegion::State
                ? colors_.focusState
                : colors_.focusActionExecutions;
        const std::string debugProgram = ProgramName(
            snapshot_,
            displayedDebugId);
        if (!debugProgram.empty()) {
            headerTitle += " | " + debugProgram;
        }
        if (displayedDebugExecutor != nullptr) {
            headerTitle += " | TRACE";
            if (displayedDebugExecutor->dryRun) {
                headerTitle += " + SAFETY";
            }
        } else if (pendingDebugRun_.has_value()) {
            headerTitle += " | STARTING";
        } else if (debugTerminated) {
            headerTitle += " | TERMINATED";
        }
        if (debugInteraction_ == RegionInteraction::Selecting) {
            headerKeys = {
                "[Arrow Keys] Region",
                "[Enter] Enter",
                "[Esc] Program"};
        } else {
            std::string focusKey = debugRegion_ == DebugRegion::Events
                ? "Event"
                : debugRegion_ == DebugRegion::State
                    ? "State Row"
                    : "Execution";
            headerKeys = {
                "[↑]/[↓] " + focusKey,
                "[PgUp]/[PgDn] Page",
                "[Home]/[End] Edge",
                "[Esc] Regions"};
        }
        headerKeys.insert(headerKeys.begin(), "[Tab] Region");
        if (displayedDebugExecutor != nullptr) {
            headerKeys.insert(
                headerKeys.end() - 1,
                {"[C] Start/Stop Capture", "[X] Stop Executor"});
        } else if (pendingDebugRun_.has_value()) {
            headerKeys.insert(headerKeys.end() - 1, "[X] Cancel Start");
        } else if (debugTerminated) {
            headerKeys.insert(headerKeys.end() - 1, "[X] Clear Debug");
        }
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
        colors_.mutedText,
        colors_.text);
    const std::size_t pageRailByte = headerTitle.find(pageRail);
    if (pageRailByte != std::string::npos) {
        const std::size_t pageRailX = 2U + Utf8DisplayWidth(
            std::string_view{headerTitle}.substr(0U, pageRailByte));
        canvas.Text(
            pageRailX,
            0U,
            pageRail,
            Utf8DisplayWidth(pageRail),
            Foreground(headerColor));
    }
    if (page_ == Page::Program && SourceEditing()) {
        constexpr std::string_view exitLabel = "[Esc] Exit";
        const std::size_t labelWidth = Utf8DisplayWidth(exitLabel);
        canvas.Text(
            width - labelWidth - 2U,
            0U,
            exitLabel,
            labelWidth,
            Foreground(headerColor));
    }
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
    } else if (page_ == Page::Program) {
        const std::array<std::pair<std::string, RgbColor>, 4U> options{{
            {"[T] Trace and Debug: " + OnOff(nextRun_.debug),
             nextRun_.debug ? colors_.statusDebug : colors_.mutedText},
            {"[S] Skip Simulated Input: " + OnOff(nextRun_.dryRun),
             nextRun_.dryRun ? colors_.statusDryRun : colors_.mutedText},
            {"[P] Authorize Execution Permission: " + OnOff(nextRun_.allowExec),
             nextRun_.allowExec
                 ? colors_.statusExecPermission
                 : colors_.mutedText},
            {"[Space] Run", colors_.statusRunning}}};
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
        const bool showNextRun = !SourceEditing();
        const std::size_t nextRunHeight = showNextRun
            ? optionRows.size() + 2U
            : 0U;
        const std::size_t contentTop = headerHeight;

        std::string documentTitle = documentView_ == DocumentView::Source
            ? "SOURCE"
            : "COMPILED DUMP";
        if (SourceEditing()) {
            documentTitle += " | EDITING";
        }
        if (!sourceDiagnostics_.empty()) {
            documentTitle += " | " + std::to_string(sourceDiagnostics_.size())
                + (sourceDiagnostics_.size() == 1U ? " ERROR" : " ERRORS");
        }
        const auto renderDocument = [&](Rectangle body) {
            if (documentView_ == DocumentView::Source) {
                RenderSource(
                    canvas,
                    sourceEditor_,
                    sourceHighlights_,
                    sourceDiagnostics_,
                    body,
                    programInteraction_ == RegionInteraction::Active
                        && programRegion_ == ProgramRegion::Source,
                    SourceEditing(),
                    cursorVisibleSince_,
                    colors_);
                return;
            }
            const std::vector<std::string> lines = WrapUtf8(
                dumpText_,
                body.width);
            RenderViewportRows(
                dumpViewport_,
                lines.size(),
                body.height,
                [&](std::size_t row, std::size_t index) {
                    canvas.Text(
                        body.x,
                        body.y + row,
                        lines[index],
                        body.width,
                        Foreground(colors_.text));
                });
        };

        if (DocumentFullscreen()) {
            const Rectangle editorBox{
                0U,
                contentTop,
                width,
                height - contentTop - nextRunHeight};
            canvas.Box(
                editorBox,
                documentTitle,
                colors_.focusSource,
                colors_.focusSource,
                colors_.text);
            const Rectangle documentBody{
                1U,
                contentTop + 1U,
                width - 2U,
                editorBox.height - 2U};
            renderDocument(documentBody);
        } else {
            const std::size_t contentHeight =
                height - headerHeight - nextRunHeight;
            const std::size_t leftWidth = (std::max)(
                static_cast<std::size_t>(26U),
                width * 3U / 10U);
            const std::size_t rightWidth = width - leftWidth;
            const RgbColor programBorder = RegionBorder(
                programRegion_ == ProgramRegion::List,
                colors_.focusProgram,
                colors_);
            const RgbColor informationBorder = RegionBorder(
                programRegion_ == ProgramRegion::Information,
                colors_.focusProgramInformation,
                colors_);
            const RgbColor sourceBorder = RegionBorder(
                programRegion_ == ProgramRegion::Source,
                colors_.focusSource,
                colors_);
            canvas.Box(
                {0U, contentTop, leftWidth, contentHeight},
                "PROGRAM",
                programBorder,
                programBorder,
                colors_.text);
            canvas.Box(
                {leftWidth, contentTop, rightWidth, 6U},
                "PROGRAM INFORMATION",
                informationBorder,
                informationBorder,
                colors_.text);
            canvas.Box(
                {leftWidth, contentTop + 6U, rightWidth, contentHeight - 6U},
                documentTitle,
                sourceBorder,
                sourceBorder,
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
                const bool selectedRow = index == selectedIndex_;
                const bool programListActive =
                    programInteraction_ == RegionInteraction::Active
                    && programRegion_ == ProgramRegion::List;
                const TextStyle selectedStyle = {
                    programListActive
                        ? colors_.selectionActiveForeground
                        : colors_.selectionInactiveForeground,
                    programListActive
                        ? colors_.selectionActiveBackground
                        : colors_.selectionInactiveBackground,
                    true,
                    false};
                if (selectedRow) {
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
                const TextStyle rowStyle = selectedRow
                    ? selectedStyle
                    : Foreground(colors_.text);
                canvas.Text(
                    1U,
                    contentTop + 1U + row,
                    selectedRow ? "> " + name : "  " + name,
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
                            selectedRow ? selectedStyle : Foreground(color));
                        tagX += tagWidth;
                    }
                }
            }

            const app::ProgramEntry* selected = SelectedProgram();
            if (selected != nullptr) {
                std::array<TextStyle, 3U> fieldStyles;
                for (std::size_t field = 0U; field < fieldStyles.size(); ++field) {
                    fieldStyles[field] = Foreground(
                        programInteraction_ == RegionInteraction::Active
                                && programRegion_ == ProgramRegion::Information
                                && informationField_ == field
                            ? colors_.focusProgramInformation
                            : colors_.text);
                }
                const std::size_t informationX = leftWidth + 1U;
                const std::size_t valueX = informationX + 10U;
                const std::size_t valueWidth = rightWidth - 12U;
                canvas.Text(
                    informationX,
                    contentTop + 1U,
                    "Name:     ",
                    10U,
                    fieldStyles[0U]);
                canvas.Text(
                    informationX,
                    contentTop + 2U,
                    "Target:   ",
                    10U,
                    fieldStyles[1U]);
                canvas.Text(
                    informationX,
                    contentTop + 3U,
                    "Logging:  ",
                    10U,
                    fieldStyles[2U]);
                if (mode_ == Mode::Rename) {
                    RenderLineEditor(
                        canvas,
                        editor_,
                        valueX,
                        contentTop + 1U,
                        valueWidth,
                        cursorVisibleSince_,
                        colors_);
                } else {
                    canvas.Text(
                        valueX,
                        contentTop + 1U,
                        selected->displayName,
                        valueWidth,
                        fieldStyles[0U]);
                }
                if (mode_ == Mode::TargetSelect) {
                    canvas.Text(
                        valueX,
                        contentTop + 2U,
                        TargetModeText(static_cast<app::TargetMode>(choice_)),
                        valueWidth,
                        ActiveFieldStyle(colors_));
                } else if (mode_ == Mode::ExecutableInput) {
                    constexpr std::string_view prefix = "Executable -> ";
                    const std::size_t prefixWidth = Utf8DisplayWidth(prefix);
                    canvas.Text(
                        valueX,
                        contentTop + 2U,
                        prefix,
                        valueWidth,
                        fieldStyles[1U]);
                    if (valueWidth > prefixWidth) {
                        RenderLineEditor(
                            canvas,
                            editor_,
                            valueX + prefixWidth,
                            contentTop + 2U,
                            valueWidth - prefixWidth,
                            cursorVisibleSince_,
                            colors_);
                    }
                } else {
                    canvas.Text(
                        valueX,
                        contentTop + 2U,
                        TargetText(selected->configuration),
                        valueWidth,
                        fieldStyles[1U]);
                }
                if (mode_ == Mode::LoggingSelect) {
                    canvas.Text(
                        valueX,
                        contentTop + 3U,
                        LoggingText(static_cast<app::LoggingMode>(choice_)),
                        valueWidth,
                        ActiveFieldStyle(colors_));
                } else {
                    canvas.Text(
                        valueX,
                        contentTop + 3U,
                        LoggingText(selected->configuration.logging),
                        valueWidth,
                        fieldStyles[2U]);
                }
            }

            const Rectangle documentBody{
                leftWidth + 1U,
                contentTop + 7U,
                rightWidth - 2U,
                contentHeight - 8U};
            renderDocument(documentBody);
        }
        if (showNextRun) {
            const Rectangle nextRunBox{
                0U, height - nextRunHeight, width, nextRunHeight};
            canvas.Box(
                nextRunBox,
                "NEXT RUN",
                colors_.unfocusedBorder,
                colors_.text,
                colors_.text);
            RenderDistributedRows(
                canvas,
                1U,
                height - nextRunHeight + 1U,
                width - 2U,
                optionSegments,
                optionRows,
                3U,
                1U);
        }
    } else {
        const debug::DebugClientState* debugState =
            displayedDebugSession == nullptr
            ? nullptr
            : displayedDebugSession->state.get();
        constexpr std::size_t healthRows = 4U;
        const std::size_t healthY = height - healthRows;
        const std::size_t bodyTop = headerHeight;
        const std::size_t bodyHeight = healthY - bodyTop;
        const std::size_t topHeight = (std::max)(
            static_cast<std::size_t>(6U),
            bodyHeight * 3U / 5U);
        const std::size_t leftWidth = (std::min)(width * 2U / 3U, std::size_t{60U});
        const RgbColor eventsBorder = RegionBorder(
            debugRegion_ == DebugRegion::Events,
            colors_.focusEvents,
            colors_);
        const RgbColor stateBorder = RegionBorder(
            debugRegion_ == DebugRegion::State,
            colors_.focusState,
            colors_);
        const RgbColor executionBorder = RegionBorder(
            debugRegion_ == DebugRegion::Executions,
            colors_.focusActionExecutions,
            colors_);
        canvas.Box(
            {0U, bodyTop, leftWidth, topHeight},
            "EVENTS",
            eventsBorder,
            eventsBorder,
            colors_.text);
        canvas.Box(
            {leftWidth, bodyTop, width - leftWidth, topHeight},
            "STATE",
            stateBorder,
            stateBorder,
            colors_.text);
        canvas.Box(
            {0U, bodyTop + topHeight, width, bodyHeight - topHeight},
            "ACTION EXECUTIONS",
            executionBorder,
            executionBorder,
            colors_.text);

        if (debugState != nullptr) {
            const std::size_t eventVisible = topHeight - 2U;
            std::vector<std::string> eventLines;
            for (const auto& event : debugState->recentInputEvents) {
                eventLines.push_back(EventText(event));
            }
            RenderViewportRows(
                eventsViewport_,
                eventLines.size(),
                eventVisible,
                [&](std::size_t row, std::size_t index) {
                    canvas.Text(
                        1U,
                        bodyTop + 1U + row,
                        eventLines[index],
                        leftWidth - 2U,
                        Foreground(colors_.text));
                });

            const std::size_t stateWidth = width - leftWidth - 2U;
            std::vector<std::string> stateCells;
            for (const debug::DebugVariableState& value : debugState->values) {
                if (value.name == "PAUSE") {
                    continue;
                }
                std::string cell = '[' + value.name + '='
                    + DebugValueText(value.value) + ']';
                stateCells.push_back(std::move(cell));
            }
            for (const debug::DebugArrayState& array : debugState->arrays) {
                stateCells.push_back(DebugArrayText(array));
            }
            for (const debug::DebugPressedControl& pressed
                 : debugState->pressedControls) {
                std::string cell = '[' + ControlName(pressed.control) + ' '
                    + std::string{debug::InputOriginLabel(pressed.origin)} + ']';
                stateCells.push_back(std::move(cell));
            }
            AppendMouseStateCells(stateCells, *debugState);
            using StateLine = std::vector<std::pair<std::size_t, std::string>>;
            std::vector<StateLine> stateRows;
            StateLine stateLine;
            std::size_t usedWidth{};
            const auto finishStateLine = [&]() {
                if (!stateLine.empty()) {
                    stateRows.push_back(std::move(stateLine));
                    stateLine.clear();
                    usedWidth = 0U;
                }
            };
            for (std::string& cell : stateCells) {
                const std::size_t cellWidth = Utf8DisplayWidth(cell);
                if (cellWidth > stateWidth) {
                    finishStateLine();
                    for (std::string& line : WrapUtf8(cell, stateWidth, 2U)) {
                        StateLine wrapped;
                        wrapped.emplace_back(0U, std::move(line));
                        stateRows.push_back(std::move(wrapped));
                    }
                    continue;
                }
                constexpr std::size_t gap = 2U;
                const std::size_t offset = stateLine.empty()
                    ? 0U
                    : usedWidth + gap;
                if (!stateLine.empty()
                    && offset + cellWidth > stateWidth) {
                    finishStateLine();
                }
                const std::size_t placedAt = stateLine.empty()
                    ? 0U
                    : usedWidth + gap;
                stateLine.emplace_back(placedAt, std::move(cell));
                usedWidth = placedAt + cellWidth;
            }
            finishStateLine();
            const std::size_t stateVisible = topHeight - 2U;
            RenderViewportRows(
                stateViewport_,
                stateRows.size(),
                stateVisible,
                [&](std::size_t row, std::size_t sourceRow) {
                    for (const auto& [offset, text] : stateRows[sourceRow]) {
                        canvas.Text(
                            leftWidth + 1U + offset,
                            bodyTop + 1U + row,
                            text,
                            stateWidth - offset,
                            Foreground(colors_.text));
                    }
                });

            const std::size_t executionWidth = width - 2U;
            const std::size_t executionVisible = bodyHeight - topHeight - 2U;
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
                        bodyTop + topHeight + 1U + row,
                        executionWidth,
                        executionLines[index]);
                });
        }

        const bool starting = pendingDebugRun_.has_value();
        const bool trusted = debugState != nullptr && debugState->captureTrusted;
        const bool recovering = !debugTerminated && debugState != nullptr
            && debugState->captureRequested && !debugState->capturing;
        const bool hasClientFault = debugState != nullptr
            && debugState->lastFault != debug::DebugClientFault::None;
        const bool incompleteSnapshot = debugTerminated
            && debugState != nullptr
            && !debugState->streamComplete;
        const bool hasProblem = (debugTerminated
                && displayedDebugSession->exitCode != 0U)
            || hasClientFault
            || incompleteSnapshot
            || (debugState != nullptr && !debugState->runtimeIssues.empty());
        const bool debugInactive = !starting && displayedDebugSession == nullptr;
        const RgbColor healthColor = debugInactive
            ? colors_.unfocusedBorder
            : hasProblem
                ? colors_.healthFault
                : starting || recovering
                    ? colors_.healthRecovering
                    : colors_.healthTrusted;
        const RgbColor healthTextColor = debugInactive
            ? colors_.text
            : healthColor;
        std::string health1;
        std::string health2;
        if (debugTerminated) {
            health1 = "Terminated | Exit="
                + std::to_string(displayedDebugSession->exitCode)
                + (debugState == nullptr
                    ? " | No capture"
                    : debugState->streamComplete
                        ? " | Complete snapshot | PAUSE="
                            + DebugPauseText(debugState)
                        : " | Best-effort snapshot | PAUSE="
                        + DebugPauseText(debugState));
            health2 = "Fault: " + (debugState == nullptr
                    ? std::string{"None"}
                    : DebugFaultText(debugState->lastFault))
                + " | Runtime issues: "
                + std::to_string(
                    debugState == nullptr
                    ? 0U
                    : debugState->runtimeIssues.size());
            if (debugState != nullptr && !debugState->runtimeIssues.empty()) {
                health2 += " | Latest: " + RuntimeDiagnosticText(
                    debugState->runtimeIssues.back().payload);
            }
        } else if (debugState == nullptr) {
            health1 = starting
                ? "Starting | Capture pending | Untrusted | PAUSE=unavailable"
                : "Disconnected | Not capturing | Untrusted | PAUSE=unavailable";
            health2 = "Fault: None | Runtime issues: 0";
        } else {
            health1 += std::string{debugState->connected ? "Connected" : "Disconnected"}
                + " | "
                + (recovering
                    ? "Recovering"
                    : debugState->capturing ? "Capturing" : "Stopped")
                + " | " + (trusted ? "Trusted" : "Untrusted");
            if (displayedDebugExecutor != nullptr
                && displayedDebugExecutor->dryRun) {
                health1 += " | Dry-run";
            }
            health1 += " | PAUSE=" + DebugPauseText(debugState);
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
            Foreground(healthTextColor));
        canvas.Text(
            1U,
            healthY + 2U,
            health2,
            width - 2U,
            Foreground(healthTextColor));
    }

    const bool inlineInformationEdit = mode_ == Mode::TargetSelect
        || mode_ == Mode::ExecutableInput
        || mode_ == Mode::LoggingSelect
        || mode_ == Mode::Rename;
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
        if (mode_ == Mode::AddSelect) {
            title = "ADD PROGRAM";
            prompt = "Create or import a program:";
        } else if (mode_ == Mode::AddPath) {
            title = "IMPORT PROGRAM";
            prompt = "Path to one .weave file:";
        } else if (mode_ == Mode::NewName) {
            title = "NEW PROGRAM";
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
        if (mode_ == Mode::AddSelect || mode_ == Mode::ConflictSelect) {
            const std::array addChoices{"New Blank", "Import .weave", "Cancel"};
            const std::array conflictChoices{"Overwrite", "Rename", "Cancel"};
            constexpr std::size_t choiceGap = 3U;
            std::array<std::string, 3U> labels;
            std::array<std::size_t, 3U> labelWidths{};
            std::size_t totalWidth = choiceGap * 2U;
            for (std::size_t index = 0U; index < 3U; ++index) {
                const std::string_view choice = mode_ == Mode::AddSelect
                    ? addChoices[index]
                    : conflictChoices[index];
                labels[index] = std::string{index == choice_ ? "> " : "  "}
                    + std::string{choice};
                labelWidths[index] = Utf8DisplayWidth(labels[index]);
                totalWidth += labelWidths[index];
            }
            std::size_t choiceX = modal.x + (modal.width - totalWidth) / 2U;
            for (std::size_t index = 0U; index < 3U; ++index) {
                const TextStyle style = index == choice_
                    ? TextStyle{
                        colors_.selectionActiveForeground,
                        colors_.selectionActiveBackground,
                        true,
                        false}
                    : Foreground(colors_.text);
                canvas.Text(
                    choiceX,
                    modal.y + 4U,
                    labels[index],
                    labelWidths[index],
                    style);
                choiceX += labelWidths[index] + choiceGap;
            }
        } else if (mode_ != Mode::DeleteConfirm) {
            const std::size_t inputWidth = modal.width - 4U;
            RenderLineEditor(
                canvas,
                editor_,
                modal.x + 2U,
                modal.y + 4U,
                inputWidth,
                cursorVisibleSince_,
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
