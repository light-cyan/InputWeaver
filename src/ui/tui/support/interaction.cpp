#include "interaction.hpp"

#include "text_layout.hpp"

#include <algorithm>

namespace inputweaver::ui::tui {

void Viewport::Update(
    std::size_t contentLines,
    std::size_t visibleLines) noexcept
{
    contentLines_ = contentLines;
    visibleLines_ = visibleLines;
    if (follow_) {
        top_ = MaximumTop();
    } else {
        top_ = (std::min)(top_, MaximumTop());
    }
}

void Viewport::LineUp() noexcept
{
    follow_ = false;
    if (top_ > 0U) {
        --top_;
    }
}

void Viewport::LineDown() noexcept
{
    if (top_ < MaximumTop()) {
        ++top_;
    }
    follow_ = top_ == MaximumTop();
}

void Viewport::PageUp() noexcept
{
    follow_ = false;
    top_ = visibleLines_ >= top_ ? 0U : top_ - visibleLines_;
}

void Viewport::PageDown() noexcept
{
    top_ = (std::min)(MaximumTop(), top_ + visibleLines_);
    follow_ = top_ == MaximumTop();
}

void Viewport::Home() noexcept
{
    follow_ = false;
    top_ = 0U;
}

void Viewport::End() noexcept
{
    follow_ = true;
    top_ = MaximumTop();
}

void Viewport::Reveal(std::size_t line) noexcept
{
    if (line < top_) {
        top_ = line;
        follow_ = false;
    } else if (visibleLines_ != 0U && line >= top_ + visibleLines_) {
        top_ = (std::min)(MaximumTop(), line - visibleLines_ + 1U);
        follow_ = top_ == MaximumTop();
    }
}

std::size_t Viewport::Top() const noexcept
{
    return top_;
}

bool Viewport::Following() const noexcept
{
    return follow_;
}

std::size_t Viewport::MaximumTop() const noexcept
{
    return contentLines_ > visibleLines_
        ? contentLines_ - visibleLines_
        : 0U;
}

void LineEditor::Set(std::string_view utf8)
{
    text_.clear();
    std::size_t offset = 0U;
    Utf8CodePoint codePoint{};
    while (NextUtf8CodePoint(utf8, offset, codePoint)) {
        text_.push_back(codePoint.value);
    }
    cursor_ = text_.size();
    viewStart_ = 0U;
    viewWidth_ = 0U;
}

void LineEditor::Clear() noexcept
{
    text_.clear();
    cursor_ = 0U;
    viewStart_ = 0U;
    viewWidth_ = 0U;
}

void LineEditor::Insert(char32_t codePoint)
{
    if (codePoint >= 0x20U && codePoint != 0x7fU
        && text_.size() < 32'768U) {
        text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), codePoint);
        ++cursor_;
    }
}

void LineEditor::Backspace() noexcept
{
    if (cursor_ > 0U) {
        text_.erase(text_.begin() + static_cast<std::ptrdiff_t>(cursor_ - 1U));
        --cursor_;
    }
}

void LineEditor::Delete() noexcept
{
    if (cursor_ < text_.size()) {
        text_.erase(text_.begin() + static_cast<std::ptrdiff_t>(cursor_));
    }
}

void LineEditor::Left() noexcept
{
    if (cursor_ > 0U) {
        --cursor_;
    }
}

void LineEditor::Right() noexcept
{
    if (cursor_ < text_.size()) {
        ++cursor_;
    }
}

void LineEditor::Home() noexcept
{
    cursor_ = 0U;
}

void LineEditor::End() noexcept
{
    cursor_ = text_.size();
}

std::string LineEditor::Text() const
{
    std::string result;
    for (const char32_t codePoint : text_) {
        result += EncodeUtf8(codePoint);
    }
    return result;
}

LineEditorView LineEditor::View(std::size_t maximumWidth)
{
    LineEditorView view{};
    if (maximumWidth == 0U) {
        return view;
    }
    if (maximumWidth != viewWidth_) {
        viewStart_ = 0U;
        viewWidth_ = maximumWidth;
    }
    viewStart_ = (std::min)(viewStart_, text_.size());
    if (cursor_ < viewStart_) {
        viewStart_ = cursor_;
    }

    struct Window final {
        std::size_t end{};
        std::size_t contentWidth{};
        bool rightClipped{};
    };
    const auto buildWindow = [&](std::size_t start) {
        const std::size_t leftWidth = start == 0U ? 0U : 1U;
        const std::size_t available = maximumWidth > leftWidth
            ? maximumWidth - leftWidth
            : 0U;
        Window window{start, 0U, false};
        while (window.end < text_.size()) {
            const std::size_t characterWidth = CodePointDisplayWidth(
                text_[window.end]);
            if (window.contentWidth + characterWidth > available) {
                break;
            }
            window.contentWidth += characterWidth;
            ++window.end;
        }
        window.rightClipped = window.end < text_.size();
        if (window.rightClipped) {
            while (window.end > start
                && window.contentWidth + 1U > available) {
                --window.end;
                window.contentWidth -= CodePointDisplayWidth(text_[window.end]);
            }
        }
        return window;
    };

    Window window = buildWindow(viewStart_);
    while (cursor_ > window.end && viewStart_ < cursor_) {
        ++viewStart_;
        window = buildWindow(viewStart_);
    }
    if (viewStart_ > 0U) {
        view.text = "…";
        view.cursorColumn = 1U;
    }
    for (std::size_t index = viewStart_; index < window.end; ++index) {
        view.text += EncodeUtf8(text_[index]);
        if (index < cursor_) {
            view.cursorColumn += CodePointDisplayWidth(text_[index]);
        }
    }
    if (window.rightClipped) {
        view.text += "…";
    }
    return view;
}

} // namespace inputweaver::ui::tui
