#pragma once

#include "ordinal_string.hpp"

#include <filesystem>
#include <system_error>

namespace inputweaver::win32 {

[[nodiscard]] inline bool PathsReferToSameFile(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    if (left.empty() || right.empty()) {
        return false;
    }

    std::error_code equivalentError;
    if (std::filesystem::equivalent(left, right, equivalentError)) {
        return true;
    }

    std::error_code leftError;
    std::error_code rightError;
    const std::filesystem::path normalizedLeft =
        std::filesystem::weakly_canonical(left, leftError);
    const std::filesystem::path normalizedRight =
        std::filesystem::weakly_canonical(right, rightError);
    return !leftError && !rightError
        && EqualOrdinalIgnoreCase(
            normalizedLeft.native(),
            normalizedRight.native());
}

} // namespace inputweaver::win32
