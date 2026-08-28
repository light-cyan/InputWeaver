#pragma once

#include "color_scheme.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace inputweaver::ui::tui {

struct TextStyle final {
    RgbColor foreground{};
    RgbColor background{};
    bool hasBackground{};
    bool underline{};
};

struct Cell final {
    char32_t codePoint{U' '};
    TextStyle style{};
    bool continuation{};
};

struct Rectangle final {
    std::size_t x{};
    std::size_t y{};
    std::size_t width{};
    std::size_t height{};
};

class Canvas final {
public:
    Canvas(std::size_t width, std::size_t height, TextStyle baseStyle);

    [[nodiscard]] std::size_t Width() const noexcept;
    [[nodiscard]] std::size_t Height() const noexcept;
    [[nodiscard]] std::span<const Cell> Cells() const noexcept;

    void Clear(TextStyle style);
    void Put(
        std::size_t x,
        std::size_t y,
        char32_t codePoint,
        TextStyle style);
    void Text(
        std::size_t x,
        std::size_t y,
        std::string_view text,
        std::size_t maximumWidth,
        TextStyle style);
    void Fill(Rectangle rectangle, char32_t codePoint, TextStyle style);
    void Box(
        Rectangle rectangle,
        std::string_view title,
        RgbColor borderColor,
        RgbColor titleColor,
        RgbColor textColor);

private:
    [[nodiscard]] Cell* At(std::size_t x, std::size_t y) noexcept;

    std::size_t width_{};
    std::size_t height_{};
    std::vector<Cell> cells_;
};

} // namespace inputweaver::ui::tui
