#include "canvas.hpp"

#include "text_layout.hpp"

#include <algorithm>

namespace inputweaver::ui::tui {

Canvas::Canvas(std::size_t width, std::size_t height, TextStyle baseStyle)
    : width_(width),
      height_(height),
      cells_(width * height, Cell{U' ', baseStyle, false})
{
}

void Canvas::Clear(TextStyle style)
{
    std::fill(cells_.begin(), cells_.end(), Cell{U' ', style, false});
}

void Canvas::Put(
    std::size_t x,
    std::size_t y,
    char32_t codePoint,
    TextStyle style)
{
    Cell* cell = At(x, y);
    if (cell == nullptr) {
        return;
    }
    const std::size_t characterWidth = CodePointDisplayWidth(codePoint);
    if (characterWidth == 2U && x + 1U >= width_) {
        return;
    }
    *cell = {codePoint, style, false};
    if (characterWidth == 2U) {
        Cell* next = At(x + 1U, y);
        if (next != nullptr) {
            *next = {U' ', style, true};
        }
    }
}

void Canvas::Text(
    std::size_t x,
    std::size_t y,
    std::string_view text,
    std::size_t maximumWidth,
    TextStyle style)
{
    std::size_t offset = 0U;
    std::size_t used = 0U;
    Utf8CodePoint codePoint{};
    while (NextUtf8CodePoint(text, offset, codePoint)) {
        if (codePoint.value == U'\n' || codePoint.value == U'\r') {
            break;
        }
        if (used + codePoint.displayWidth > maximumWidth) {
            break;
        }
        Put(x + used, y, codePoint.value, style);
        used += codePoint.displayWidth;
    }
}

void Canvas::Fill(Rectangle rectangle, char32_t codePoint, TextStyle style)
{
    const std::size_t endY = (std::min)(height_, rectangle.y + rectangle.height);
    const std::size_t endX = (std::min)(width_, rectangle.x + rectangle.width);
    for (std::size_t y = rectangle.y; y < endY; ++y) {
        for (std::size_t x = rectangle.x; x < endX; ++x) {
            Put(x, y, codePoint, style);
        }
    }
}

void Canvas::Box(
    Rectangle rectangle,
    std::string_view title,
    RgbColor borderColor,
    RgbColor titleColor,
    RgbColor textColor)
{
    if (rectangle.width < 2U || rectangle.height < 2U
        || rectangle.x >= width_ || rectangle.y >= height_) {
        return;
    }
    const TextStyle border{borderColor, {}, false, false};
    const std::size_t right = rectangle.x + rectangle.width - 1U;
    const std::size_t bottom = rectangle.y + rectangle.height - 1U;
    Put(rectangle.x, rectangle.y, U'┌', border);
    Put(right, rectangle.y, U'┐', border);
    Put(rectangle.x, bottom, U'└', border);
    Put(right, bottom, U'┘', border);
    for (std::size_t x = rectangle.x + 1U; x < right; ++x) {
        Put(x, rectangle.y, U'─', border);
        Put(x, bottom, U'─', border);
    }
    for (std::size_t y = rectangle.y + 1U; y < bottom; ++y) {
        Put(rectangle.x, y, U'│', border);
        Put(right, y, U'│', border);
    }
    if (!title.empty() && rectangle.width > 4U) {
        Put(rectangle.x + 1U, rectangle.y, U' ', border);
        Text(
            rectangle.x + 2U,
            rectangle.y,
            title,
            rectangle.width - 4U,
            {titleColor, {}, false, false});
        const std::size_t titleWidth = (std::min)(
            Utf8DisplayWidth(title),
            rectangle.width - 4U);
        Put(rectangle.x + 2U + titleWidth, rectangle.y, U' ', border);
    }
    if (rectangle.width > 2U && rectangle.height > 2U) {
        Fill(
            {rectangle.x + 1U,
             rectangle.y + 1U,
             rectangle.width - 2U,
             rectangle.height - 2U},
            U' ',
            {textColor, {}, false, false});
    }
}

Cell* Canvas::At(std::size_t x, std::size_t y) noexcept
{
    if (x >= width_ || y >= height_) {
        return nullptr;
    }
    return &cells_[y * width_ + x];
}

} // namespace inputweaver::ui::tui
