#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace inputweaver::support {

[[nodiscard]] inline bool IsValidUtf8(std::string_view text) noexcept
{
    std::size_t index = 0U;
    while (index < text.size()) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        std::uint32_t minimum = 0U;
        if ((first & 0xe0U) == 0xc0U) {
            continuationCount = 1U;
            codePoint = first & 0x1fU;
            minimum = 0x80U;
        } else if ((first & 0xf0U) == 0xe0U) {
            continuationCount = 2U;
            codePoint = first & 0x0fU;
            minimum = 0x800U;
        } else if ((first & 0xf8U) == 0xf0U) {
            continuationCount = 3U;
            codePoint = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuationCount >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1U; offset <= continuationCount; ++offset) {
            const auto byte = static_cast<std::uint8_t>(text[index + offset]);
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (byte & 0x3fU);
        }
        if (codePoint < minimum
            || codePoint > 0x10ffffU
            || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

} // namespace inputweaver::support
