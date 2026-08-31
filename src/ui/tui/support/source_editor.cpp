#include "source_editor.hpp"

#include "text_layout.hpp"

#include <algorithm>

namespace inputweaver::ui::tui {
namespace {

inline constexpr std::size_t kMaximumSourceBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumHistoryBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumHistoryEntries = 256U;

[[nodiscard]] bool IsContinuationByte(char byte) noexcept
{
    return (static_cast<unsigned char>(byte) & 0xc0U) == 0x80U;
}

} // namespace

void SourceEditor::Set(std::string_view source)
{
    AdvanceRevision();
    lines_.clear();
    std::size_t offset{};
    while (offset <= source.size()) {
        const std::size_t end = source.find_first_of("\r\n", offset);
        lines_.emplace_back(source.substr(
            offset,
            end == std::string_view::npos ? source.size() - offset : end - offset));
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1U;
        if (source[end] == '\r' && offset < source.size()
            && source[offset] == '\n') {
            ++offset;
        }
    }
    if (lines_.empty()) {
        lines_.emplace_back();
    }
    sourceBytes_ = lines_.size() - 1U;
    for (const std::string& line : lines_) {
        sourceBytes_ += line.size();
    }
    cursorLine_ = 0U;
    cursorByte_ = 0U;
    preferredByte_ = 0U;
    topLine_ = 0U;
    leftColumn_ = 0U;
    selectionAnchor_.reset();
    undoHistory_ = {};
    redoHistory_ = {};
}

bool SourceEditor::Insert(char32_t codePoint)
{
    if (codePoint < 0x20U || codePoint == 0x7fU) {
        return false;
    }
    const std::string encoded = EncodeUtf8(codePoint);
    const std::size_t selectedBytes = SelectionSize();
    if (sourceBytes_ - selectedBytes + encoded.size() > kMaximumSourceBytes) {
        return false;
    }
    RecordEdit();
    (void)DeleteSelection();
    lines_[cursorLine_].insert(cursorByte_, encoded);
    cursorByte_ += encoded.size();
    sourceBytes_ += encoded.size();
    RememberColumn();
    return true;
}

bool SourceEditor::InsertSpaces(std::size_t count)
{
    if (count == 0U) {
        return false;
    }
    const std::size_t selectedBytes = SelectionSize();
    if (sourceBytes_ - selectedBytes + count > kMaximumSourceBytes) {
        return false;
    }
    RecordEdit();
    (void)DeleteSelection();
    lines_[cursorLine_].insert(cursorByte_, count, ' ');
    cursorByte_ += count;
    sourceBytes_ += count;
    RememberColumn();
    return true;
}

bool SourceEditor::NewLine()
{
    const std::size_t selectedBytes = SelectionSize();
    if (sourceBytes_ - selectedBytes == kMaximumSourceBytes) {
        return false;
    }
    RecordEdit();
    (void)DeleteSelection();
    std::string remainder = lines_[cursorLine_].substr(cursorByte_);
    lines_[cursorLine_].erase(cursorByte_);
    lines_.insert(
        lines_.begin() + static_cast<std::ptrdiff_t>(cursorLine_ + 1U),
        std::move(remainder));
    ++cursorLine_;
    cursorByte_ = 0U;
    ++sourceBytes_;
    RememberColumn();
    return true;
}

bool SourceEditor::Backspace()
{
    if (!HasSelection() && cursorByte_ == 0U && cursorLine_ == 0U) {
        return false;
    }
    RecordEdit();
    if (DeleteSelection()) {
        return true;
    }
    if (cursorByte_ != 0U) {
        const std::size_t previous = PreviousBoundary(
            lines_[cursorLine_],
            cursorByte_);
        sourceBytes_ -= cursorByte_ - previous;
        lines_[cursorLine_].erase(previous, cursorByte_ - previous);
        cursorByte_ = previous;
        RememberColumn();
        return true;
    }
    const std::size_t previousSize = lines_[cursorLine_ - 1U].size();
    lines_[cursorLine_ - 1U] += lines_[cursorLine_];
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(cursorLine_));
    --cursorLine_;
    cursorByte_ = previousSize;
    --sourceBytes_;
    RememberColumn();
    return true;
}

bool SourceEditor::Delete()
{
    if (!HasSelection() && cursorByte_ == lines_[cursorLine_].size()
        && cursorLine_ + 1U == lines_.size()) {
        return false;
    }
    RecordEdit();
    if (DeleteSelection()) {
        return true;
    }
    std::string& line = lines_[cursorLine_];
    if (cursorByte_ < line.size()) {
        const std::size_t next = NextBoundary(line, cursorByte_);
        sourceBytes_ -= next - cursorByte_;
        line.erase(cursorByte_, next - cursorByte_);
        return true;
    }
    line += lines_[cursorLine_ + 1U];
    lines_.erase(
        lines_.begin() + static_cast<std::ptrdiff_t>(cursorLine_ + 1U));
    --sourceBytes_;
    return true;
}

bool SourceEditor::Undo()
{
    return RestoreHistory(undoHistory_, redoHistory_);
}

bool SourceEditor::Redo()
{
    return RestoreHistory(redoHistory_, undoHistory_);
}

void SourceEditor::Left() noexcept
{
    if (cursorByte_ != 0U) {
        cursorByte_ = PreviousBoundary(lines_[cursorLine_], cursorByte_);
    } else if (cursorLine_ != 0U) {
        --cursorLine_;
        cursorByte_ = lines_[cursorLine_].size();
    }
    RememberColumn();
}

void SourceEditor::Right() noexcept
{
    if (cursorByte_ < lines_[cursorLine_].size()) {
        cursorByte_ = NextBoundary(lines_[cursorLine_], cursorByte_);
    } else if (cursorLine_ + 1U < lines_.size()) {
        ++cursorLine_;
        cursorByte_ = 0U;
    }
    RememberColumn();
}

void SourceEditor::Up() noexcept
{
    MoveVertical(-1);
}

void SourceEditor::Down() noexcept
{
    MoveVertical(1);
}

void SourceEditor::PageUp() noexcept
{
    MoveVertical(-static_cast<std::ptrdiff_t>(
        (std::max)(visibleLines_, std::size_t{1U})));
}

void SourceEditor::PageDown() noexcept
{
    MoveVertical(static_cast<std::ptrdiff_t>(
        (std::max)(visibleLines_, std::size_t{1U})));
}

void SourceEditor::Home() noexcept
{
    cursorByte_ = 0U;
    RememberColumn();
}

void SourceEditor::End() noexcept
{
    cursorByte_ = lines_[cursorLine_].size();
    RememberColumn();
}

void SourceEditor::FirstLine() noexcept
{
    cursorLine_ = 0U;
    cursorByte_ = 0U;
    RememberColumn();
}

void SourceEditor::LastLine() noexcept
{
    cursorLine_ = lines_.size() - 1U;
    cursorByte_ = 0U;
    RememberColumn();
}

void SourceEditor::BeginSelection() noexcept
{
    if (!selectionAnchor_.has_value()) {
        selectionAnchor_ = Position{cursorLine_, cursorByte_};
    }
}

void SourceEditor::ClearSelection() noexcept
{
    selectionAnchor_.reset();
}

void SourceEditor::PrepareView(
    std::size_t visibleLines,
    std::size_t visibleColumns)
{
    visibleLines_ = visibleLines;
    if (cursorLine_ < topLine_) {
        topLine_ = cursorLine_;
    } else if (visibleLines != 0U && cursorLine_ >= topLine_ + visibleLines) {
        topLine_ = cursorLine_ - visibleLines + 1U;
    }
    const std::size_t column = CursorDisplayColumn();
    if (column < leftColumn_) {
        leftColumn_ = column;
    } else if (visibleColumns != 0U && column >= leftColumn_ + visibleColumns) {
        leftColumn_ = column - visibleColumns + 1U;
    }
}

std::string SourceEditor::Text() const
{
    std::string result;
    result.reserve(sourceBytes_);
    for (std::size_t index = 0U; index < lines_.size(); ++index) {
        if (index != 0U) {
            result.push_back('\n');
        }
        result += lines_[index];
    }
    return result;
}

std::size_t SourceEditor::LineCount() const noexcept
{
    return lines_.size();
}

std::string_view SourceEditor::Line(std::size_t index) const noexcept
{
    return index < lines_.size() ? std::string_view{lines_[index]} : std::string_view{};
}

std::size_t SourceEditor::CursorLine() const noexcept
{
    return cursorLine_;
}

std::size_t SourceEditor::CursorByte() const noexcept
{
    return cursorByte_;
}

std::size_t SourceEditor::CursorDisplayColumn() const noexcept
{
    return Utf8DisplayWidth(
        std::string_view{lines_[cursorLine_]}.substr(0U, cursorByte_));
}

std::size_t SourceEditor::TopLine() const noexcept
{
    return topLine_;
}

std::size_t SourceEditor::LeftColumn() const noexcept
{
    return leftColumn_;
}

std::uint64_t SourceEditor::Revision() const noexcept
{
    return revision_;
}

bool SourceEditor::HasSelection() const noexcept
{
    return selectionAnchor_.has_value()
        && (selectionAnchor_->line != cursorLine_
            || selectionAnchor_->byte != cursorByte_);
}

bool SourceEditor::IsSelected(
    std::size_t line,
    std::size_t byte) const noexcept
{
    if (!HasSelection()) {
        return false;
    }
    const auto [begin, end] = SelectionBounds();
    const Position position{line, byte};
    return !Before(position, begin) && Before(position, end);
}

std::string SourceEditor::SelectedText() const
{
    if (!HasSelection()) {
        return {};
    }
    const auto [begin, end] = SelectionBounds();
    if (begin.line == end.line) {
        return lines_[begin.line].substr(begin.byte, end.byte - begin.byte);
    }
    std::string text = lines_[begin.line].substr(begin.byte);
    for (std::size_t line = begin.line + 1U; line < end.line; ++line) {
        text.push_back('\n');
        text += lines_[line];
    }
    text.push_back('\n');
    text.append(lines_[end.line], 0U, end.byte);
    return text;
}

std::size_t SourceEditor::PreviousBoundary(
    std::string_view line,
    std::size_t offset) noexcept
{
    if (offset == 0U) {
        return 0U;
    }
    --offset;
    while (offset != 0U && IsContinuationByte(line[offset])) {
        --offset;
    }
    return offset;
}

std::size_t SourceEditor::NextBoundary(
    std::string_view line,
    std::size_t offset) noexcept
{
    if (offset >= line.size()) {
        return line.size();
    }
    ++offset;
    while (offset < line.size() && IsContinuationByte(line[offset])) {
        ++offset;
    }
    return offset;
}

bool SourceEditor::Before(Position left, Position right) noexcept
{
    return left.line < right.line
        || (left.line == right.line && left.byte < right.byte);
}

std::pair<SourceEditor::Position, SourceEditor::Position>
SourceEditor::SelectionBounds() const noexcept
{
    const Position cursor{cursorLine_, cursorByte_};
    const Position anchor = selectionAnchor_.value_or(cursor);
    return Before(cursor, anchor)
        ? std::pair{cursor, anchor}
        : std::pair{anchor, cursor};
}

bool SourceEditor::DeleteSelection() noexcept
{
    if (!HasSelection()) {
        selectionAnchor_.reset();
        return false;
    }
    const auto [begin, end] = SelectionBounds();
    sourceBytes_ -= SelectionSize();
    if (begin.line == end.line) {
        lines_[begin.line].erase(begin.byte, end.byte - begin.byte);
    } else {
        lines_[begin.line].erase(begin.byte);
        lines_[begin.line].append(lines_[end.line], end.byte);
        lines_.erase(
            lines_.begin() + static_cast<std::ptrdiff_t>(begin.line + 1U),
            lines_.begin() + static_cast<std::ptrdiff_t>(end.line + 1U));
    }
    cursorLine_ = begin.line;
    cursorByte_ = begin.byte;
    selectionAnchor_.reset();
    RememberColumn();
    return true;
}

std::size_t SourceEditor::SelectionSize() const noexcept
{
    if (!HasSelection()) {
        return 0U;
    }
    const auto [begin, end] = SelectionBounds();
    if (begin.line == end.line) {
        return end.byte - begin.byte;
    }
    std::size_t size = lines_[begin.line].size() - begin.byte + end.byte + 1U;
    for (std::size_t line = begin.line + 1U; line < end.line; ++line) {
        size += lines_[line].size() + 1U;
    }
    return size;
}

SourceEditor::Snapshot SourceEditor::Capture() const
{
    return {
        lines_,
        {cursorLine_, cursorByte_},
        preferredByte_,
        sourceBytes_,
        selectionAnchor_};
}

void SourceEditor::Restore(Snapshot snapshot) noexcept
{
    lines_ = std::move(snapshot.lines);
    cursorLine_ = snapshot.cursor.line;
    cursorByte_ = snapshot.cursor.byte;
    preferredByte_ = snapshot.preferredByte;
    sourceBytes_ = snapshot.sourceBytes;
    selectionAnchor_ = snapshot.selectionAnchor;
}

void SourceEditor::RecordEdit()
{
    AdvanceRevision();
    PushHistory(undoHistory_, Capture());
    redoHistory_ = {};
}

bool SourceEditor::RestoreHistory(
    History& source,
    History& destination)
{
    if (source.states.empty()) {
        return false;
    }
    PushHistory(destination, Capture());
    Snapshot snapshot = std::move(source.states.back());
    source.states.pop_back();
    source.bytes -= snapshot.sourceBytes;
    Restore(std::move(snapshot));
    AdvanceRevision();
    return true;
}

void SourceEditor::PushHistory(History& history, Snapshot snapshot)
{
    history.bytes += snapshot.sourceBytes;
    history.states.push_back(std::move(snapshot));
    while (history.states.size() > 1U
        && (history.states.size() > kMaximumHistoryEntries
            || history.bytes > kMaximumHistoryBytes)) {
        history.bytes -= history.states.front().sourceBytes;
        history.states.erase(history.states.begin());
    }
}

void SourceEditor::MoveVertical(std::ptrdiff_t lines) noexcept
{
    const std::ptrdiff_t target = (std::clamp)(
        static_cast<std::ptrdiff_t>(cursorLine_) + lines,
        std::ptrdiff_t{0},
        static_cast<std::ptrdiff_t>(lines_.size() - 1U));
    cursorLine_ = static_cast<std::size_t>(target);
    cursorByte_ = (std::min)(preferredByte_, lines_[cursorLine_].size());
    while (cursorByte_ != 0U
        && cursorByte_ < lines_[cursorLine_].size()
        && IsContinuationByte(lines_[cursorLine_][cursorByte_])) {
        --cursorByte_;
    }
}

void SourceEditor::RememberColumn() noexcept
{
    preferredByte_ = cursorByte_;
}

void SourceEditor::AdvanceRevision() noexcept
{
    ++revision_;
    if (revision_ == 0U) {
        ++revision_;
    }
}

} // namespace inputweaver::ui::tui
