#include "text_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace inputweaver::ui::tui {
namespace {

[[nodiscard]] bool IsWide(char32_t value) noexcept
{
    return (value >= 0x1100U && value <= 0x115fU)
        || value == 0x2329U || value == 0x232aU
        || (value >= 0x2e80U && value <= 0xa4cfU && value != 0x303fU)
        || (value >= 0xac00U && value <= 0xd7a3U)
        || (value >= 0xf900U && value <= 0xfaffU)
        || (value >= 0xfe10U && value <= 0xfe19U)
        || (value >= 0xfe30U && value <= 0xfe6fU)
        || (value >= 0xff00U && value <= 0xff60U)
        || (value >= 0xffe0U && value <= 0xffe6U)
        || (value >= 0x1f300U && value <= 0x1faffU)
        || (value >= 0x20000U && value <= 0x3fffdU);
}

} // namespace

bool NextUtf8CodePoint(
    std::string_view text,
    std::size_t& offset,
    Utf8CodePoint& codePoint) noexcept
{
    if (offset >= text.size()) {
        return false;
    }
    const std::size_t begin = offset;
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    char32_t value{};
    std::size_t continuation{};
    if (first <= 0x7fU) {
        value = first;
    } else if ((first & 0xe0U) == 0xc0U) {
        value = first & 0x1fU;
        continuation = 1U;
    } else if ((first & 0xf0U) == 0xe0U) {
        value = first & 0x0fU;
        continuation = 2U;
    } else if ((first & 0xf8U) == 0xf0U) {
        value = first & 0x07U;
        continuation = 3U;
    } else {
        codePoint = {0xfffdU, begin, 1U, 1U};
        return true;
    }
    if (offset + continuation > text.size()) {
        offset = begin + 1U;
        codePoint = {0xfffdU, begin, 1U, 1U};
        return true;
    }
    for (std::size_t index = 0U; index < continuation; ++index) {
        const auto byte = static_cast<std::uint8_t>(text[offset]);
        if ((byte & 0xc0U) != 0x80U) {
            offset = begin + 1U;
            codePoint = {0xfffdU, begin, 1U, 1U};
            return true;
        }
        value = (value << 6U) | (byte & 0x3fU);
        ++offset;
    }
    codePoint = {
        value,
        begin,
        offset - begin,
        CodePointDisplayWidth(value)};
    return true;
}

std::size_t CodePointDisplayWidth(char32_t codePoint) noexcept
{
    if (codePoint == U'\0'
        || (codePoint >= 0x0300U && codePoint <= 0x036fU)
        || (codePoint >= 0xfe00U && codePoint <= 0xfe0fU)) {
        return 0U;
    }
    return IsWide(codePoint) ? 2U : 1U;
}

std::size_t Utf8DisplayWidth(std::string_view text) noexcept
{
    std::size_t width = 0U;
    std::size_t offset = 0U;
    Utf8CodePoint codePoint{};
    while (NextUtf8CodePoint(text, offset, codePoint)) {
        width += codePoint.displayWidth;
    }
    return width;
}

std::string TruncateUtf8(std::string_view text, std::size_t maximumWidth)
{
    std::size_t width = 0U;
    std::size_t offset = 0U;
    std::size_t acceptedBytes = 0U;
    Utf8CodePoint codePoint{};
    while (NextUtf8CodePoint(text, offset, codePoint)) {
        if (width + codePoint.displayWidth > maximumWidth) {
            break;
        }
        width += codePoint.displayWidth;
        acceptedBytes = offset;
    }
    return std::string{text.substr(0U, acceptedBytes)};
}

std::vector<std::string> WrapUtf8(
    std::string_view text,
    std::size_t maximumWidth,
    std::size_t continuationIndent)
{
    std::vector<std::string> lines;
    if (maximumWidth == 0U) {
        return lines;
    }
    std::size_t paragraphStart = 0U;
    while (paragraphStart <= text.size()) {
        const std::size_t paragraphEnd = text.find('\n', paragraphStart);
        const std::string_view paragraph = paragraphEnd == std::string_view::npos
            ? text.substr(paragraphStart)
            : text.substr(paragraphStart, paragraphEnd - paragraphStart);
        std::size_t offset = 0U;
        bool firstLine = true;
        if (paragraph.empty()) {
            lines.emplace_back();
        }
        while (offset < paragraph.size()) {
            const std::size_t indent = firstLine
                ? 0U
                : (std::min)(continuationIndent, maximumWidth - 1U);
            const std::size_t available = maximumWidth - indent;
            std::size_t cursor = offset;
            std::size_t width = 0U;
            std::size_t accepted = offset;
            Utf8CodePoint codePoint{};
            while (NextUtf8CodePoint(paragraph, cursor, codePoint)) {
                if (width + codePoint.displayWidth > available) {
                    break;
                }
                width += codePoint.displayWidth;
                accepted = cursor;
            }
            if (accepted == offset) {
                accepted = cursor;
            }
            std::string line(indent, ' ');
            line.append(paragraph.substr(offset, accepted - offset));
            lines.push_back(std::move(line));
            offset = accepted;
            firstLine = false;
        }
        if (paragraphEnd == std::string_view::npos) {
            break;
        }
        paragraphStart = paragraphEnd + 1U;
    }
    return lines;
}

std::string EncodeUtf8(char32_t codePoint)
{
    std::string result;
    if (codePoint <= 0x7fU) {
        result.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        result.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        result.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        result.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        result.push_back(static_cast<char>(
            0x80U | ((codePoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        result.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        result.push_back(static_cast<char>(
            0x80U | ((codePoint >> 12U) & 0x3fU)));
        result.push_back(static_cast<char>(
            0x80U | ((codePoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
    return result;
}

std::vector<std::size_t> DistributeColumns(
    std::span<const std::size_t> widths,
    std::size_t availableWidth,
    std::size_t minimumGap,
    std::size_t minimumOuterGap)
{
    std::vector<std::size_t> columns;
    columns.reserve(widths.size());
    if (widths.empty()) {
        return columns;
    }
    std::size_t contentWidth{};
    for (const std::size_t width : widths) {
        contentWidth += width;
    }
    const std::size_t innerGapCount = widths.size() - 1U;
    const std::size_t outerGapCount = minimumOuterGap == 0U ? 0U : 2U;
    const std::size_t distributedGapCount = innerGapCount + outerGapCount;
    const std::size_t required = contentWidth
        + innerGapCount * minimumGap
        + outerGapCount * minimumOuterGap;
    const std::size_t extra = availableWidth > required
        ? availableWidth - required
        : 0U;
    const std::size_t gapExtra = distributedGapCount == 0U
        ? 0U
        : extra / distributedGapCount;
    const std::size_t remainder = distributedGapCount == 0U
        ? 0U
        : extra % distributedGapCount;
    std::size_t gapIndex{};
    std::size_t column{};
    if (outerGapCount != 0U) {
        column = minimumOuterGap + gapExtra
            + (gapIndex < remainder ? 1U : 0U);
        ++gapIndex;
    }
    for (std::size_t index = 0U; index < widths.size(); ++index) {
        columns.push_back(column);
        column += widths[index];
        if (index < innerGapCount) {
            column += minimumGap + gapExtra
                + (gapIndex < remainder ? 1U : 0U);
            ++gapIndex;
        }
    }
    return columns;
}

std::vector<ColumnRow> PackColumnRows(
    std::span<const std::size_t> widths,
    std::size_t availableWidth,
    std::size_t minimumGap,
    std::size_t minimumOuterGap)
{
    if (widths.empty()) {
        return {};
    }

    std::size_t rowCount = 1U;
    std::size_t firstIndex{};
    const std::size_t outerWidth = minimumOuterGap * 2U;
    std::size_t rowWidth = outerWidth;
    for (std::size_t index = 0U; index < widths.size(); ++index) {
        const std::size_t addedWidth = (index == firstIndex ? 0U : minimumGap)
            + widths[index];
        if (index != firstIndex && rowWidth + addedWidth > availableWidth) {
            ++rowCount;
            firstIndex = index;
            rowWidth = outerWidth + widths[index];
        } else {
            rowWidth += addedWidth;
        }
    }

    std::vector<std::size_t> prefixWidths(widths.size() + 1U, 0U);
    for (std::size_t index = 0U; index < widths.size(); ++index) {
        prefixWidths[index + 1U] = prefixWidths[index] + widths[index];
    }

    constexpr std::uint64_t unavailable =
        (std::numeric_limits<std::uint64_t>::max)();
    std::vector<std::vector<std::uint64_t>> costs(
        rowCount + 1U,
        std::vector<std::uint64_t>(widths.size() + 1U, unavailable));
    std::vector<std::vector<std::size_t>> splits(
        rowCount + 1U,
        std::vector<std::size_t>(widths.size() + 1U, 0U));
    costs[0U][0U] = 0U;
    for (std::size_t row = 1U; row <= rowCount; ++row) {
        for (std::size_t end = row; end <= widths.size(); ++end) {
            for (std::size_t begin = row - 1U; begin < end; ++begin) {
                if (costs[row - 1U][begin] == unavailable) {
                    continue;
                }
                const std::size_t itemCount = end - begin;
                const std::size_t candidateWidth = outerWidth
                    + prefixWidths[end] - prefixWidths[begin]
                    + (itemCount - 1U) * minimumGap;
                if (candidateWidth > availableWidth && itemCount > 1U) {
                    continue;
                }
                const std::uint64_t squaredWidth =
                    static_cast<std::uint64_t>(candidateWidth)
                    * static_cast<std::uint64_t>(candidateWidth);
                const std::uint64_t candidateCost =
                    costs[row - 1U][begin] + squaredWidth;
                if (candidateCost <= costs[row][end]) {
                    costs[row][end] = candidateCost;
                    splits[row][end] = begin;
                }
            }
        }
    }

    std::vector<ColumnRow> rows(rowCount);
    std::size_t end = widths.size();
    for (std::size_t row = rowCount; row > 0U; --row) {
        const std::size_t begin = splits[row][end];
        rows[row - 1U] = {begin, end - begin};
        end = begin;
    }
    return rows;
}

} // namespace inputweaver::ui::tui
