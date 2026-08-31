#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputweaver::ui::tui {

class SourceEditor final {
public:
    void Set(std::string_view source);

    [[nodiscard]] bool Insert(char32_t codePoint);
    [[nodiscard]] bool InsertSpaces(std::size_t count);
    [[nodiscard]] bool NewLine();
    [[nodiscard]] bool Backspace();
    [[nodiscard]] bool Delete();
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    void Left() noexcept;
    void Right() noexcept;
    void Up() noexcept;
    void Down() noexcept;
    void PageUp() noexcept;
    void PageDown() noexcept;
    void Home() noexcept;
    void End() noexcept;
    void FirstLine() noexcept;
    void LastLine() noexcept;
    void BeginSelection() noexcept;
    void ClearSelection() noexcept;
    void PrepareView(std::size_t visibleLines, std::size_t visibleColumns);

    [[nodiscard]] std::string Text() const;
    [[nodiscard]] std::size_t LineCount() const noexcept;
    [[nodiscard]] std::string_view Line(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t CursorLine() const noexcept;
    [[nodiscard]] std::size_t CursorByte() const noexcept;
    [[nodiscard]] std::size_t CursorDisplayColumn() const noexcept;
    [[nodiscard]] std::size_t TopLine() const noexcept;
    [[nodiscard]] std::size_t LeftColumn() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] bool HasSelection() const noexcept;
    [[nodiscard]] bool IsSelected(
        std::size_t line,
        std::size_t byte) const noexcept;
    [[nodiscard]] std::string SelectedText() const;

private:
    struct Position final {
        std::size_t line{};
        std::size_t byte{};
    };

    struct Snapshot final {
        std::vector<std::string> lines;
        Position cursor;
        std::size_t preferredByte{};
        std::size_t sourceBytes{};
        std::optional<Position> selectionAnchor;
    };

    struct History final {
        std::vector<Snapshot> states;
        std::size_t bytes{};
    };

    [[nodiscard]] static std::size_t PreviousBoundary(
        std::string_view line,
        std::size_t offset) noexcept;
    [[nodiscard]] static std::size_t NextBoundary(
        std::string_view line,
        std::size_t offset) noexcept;
    [[nodiscard]] static bool Before(Position left, Position right) noexcept;
    [[nodiscard]] std::pair<Position, Position> SelectionBounds() const noexcept;
    [[nodiscard]] std::size_t SelectionSize() const noexcept;
    [[nodiscard]] bool DeleteSelection() noexcept;
    [[nodiscard]] Snapshot Capture() const;
    void Restore(Snapshot snapshot) noexcept;
    void RecordEdit();
    [[nodiscard]] bool RestoreHistory(History& source, History& destination);
    static void PushHistory(History& history, Snapshot snapshot);
    void MoveVertical(std::ptrdiff_t lines) noexcept;
    void RememberColumn() noexcept;
    void AdvanceRevision() noexcept;

    std::vector<std::string> lines_{1U};
    std::size_t cursorLine_{};
    std::size_t cursorByte_{};
    std::size_t preferredByte_{};
    std::size_t sourceBytes_{};
    std::size_t topLine_{};
    std::size_t leftColumn_{};
    std::size_t visibleLines_{};
    std::uint64_t revision_{1U};
    std::optional<Position> selectionAnchor_;
    History undoHistory_;
    History redoHistory_;
};

} // namespace inputweaver::ui::tui
