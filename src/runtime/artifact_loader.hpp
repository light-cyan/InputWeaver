#pragma once

#include "program_runtime.hpp"
#include "program/weavec_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace inputweaver {

enum class RuntimeArtifactLoadErrorCode : std::uint8_t {
    None,
    OpenFailed,
    SizeUnavailable,
    LimitExceeded,
    ReadFailed,
    AllocationFailure,
    DecodeFailed,
    ValidationFailed,
    ActivationFailed,
};

struct RuntimeArtifactLoadResult final {
    bool activated{};
    RuntimeArtifactLoadErrorCode error{RuntimeArtifactLoadErrorCode::None};
    std::optional<WeavecDecodeError> decodeError;
    std::vector<ProgramValidationError> validationErrors;
    RuntimeActivationError activationError{};
};

[[nodiscard]] RuntimeArtifactLoadResult LoadAndActivateWeavec(
    ProgramRuntime& runtime,
    const std::filesystem::path& path,
    const WeavecDecodeLimits& limits = {});

} // namespace inputweaver
