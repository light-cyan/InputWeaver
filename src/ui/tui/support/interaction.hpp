#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace inputweaver::ui::tui {

struct LineEditorView final {
    std::string text;
    std::size_t cursorColumn{};
};

class Viewport final {
public:
    void Update(std::size_t contentLines, std::size_t visibleLines) noexcept;
    void LineUp() noexcept;
    void LineDown() noexcept;
    void PageUp() noexcept;
    void PageDown() noexcept;
    void Home() noexcept;
    void End() noexcept;
    void Reveal(std::size_t line) noexcept;

    [[nodiscard]] std::size_t Top() const noexcept;
    [[nodiscard]] bool Following() const noexcept;

private:
    [[nodiscard]] std::size_t MaximumTop() const noexcept;

    std::size_t contentLines_{};
    std::size_t visibleLines_{};
    std::size_t top_{};
    bool follow_{true};
};

class LineEditor final {
public:
    void Set(std::string_view utf8);
    void Clear() noexcept;
    void Insert(char32_t codePoint);
    void Backspace() noexcept;
    void Delete() noexcept;
    void Left() noexcept;
    void Right() noexcept;
    void Home() noexcept;
    void End() noexcept;

    [[nodiscard]] std::string Text() const;
    [[nodiscard]] LineEditorView View(std::size_t maximumWidth);

private:
    std::u32string text_;
    std::size_t cursor_{};
    std::size_t viewStart_{};
    std::size_t viewWidth_{};
};

} // namespace inputweaver::ui::tui
