#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::ui::tui {

struct Utf8CodePoint final {
    char32_t value{};
    std::size_t byteOffset{};
    std::size_t byteLength{};
    std::size_t displayWidth{1U};
};

struct ColumnRow final {
    std::size_t firstIndex{};
    std::size_t count{};
};

[[nodiscard]] bool NextUtf8CodePoint(
    std::string_view text,
    std::size_t& offset,
    Utf8CodePoint& codePoint) noexcept;
[[nodiscard]] std::size_t CodePointDisplayWidth(char32_t codePoint) noexcept;
[[nodiscard]] std::size_t Utf8DisplayWidth(std::string_view text) noexcept;
[[nodiscard]] std::string TruncateUtf8(
    std::string_view text,
    std::size_t maximumWidth);
[[nodiscard]] std::vector<std::string> WrapUtf8(
    std::string_view text,
    std::size_t maximumWidth,
    std::size_t continuationIndent = 0U);
[[nodiscard]] std::string EncodeUtf8(char32_t codePoint);
[[nodiscard]] std::vector<std::size_t> DistributeColumns(
    std::span<const std::size_t> widths,
    std::size_t availableWidth,
    std::size_t minimumGap = 1U,
    std::size_t minimumOuterGap = 0U);
[[nodiscard]] std::vector<ColumnRow> PackColumnRows(
    std::span<const std::size_t> widths,
    std::size_t availableWidth,
    std::size_t minimumGap = 1U,
    std::size_t minimumOuterGap = 0U);

} // namespace inputweaver::ui::tui
