#pragma once

#include "compiled_program.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace inputweaver {

inline constexpr std::size_t kWeavecHeaderSize = 16U;

struct WeavecDecodeLimits final {
    std::uint64_t maximumPayloadBytes{64U * 1024U * 1024U};
    std::uint32_t maximumCollectionElements{4U * 1024U * 1024U};
    std::uint32_t maximumStringBytes{16U * 1024U * 1024U};
};

enum class WeavecDecodeErrorCode : std::uint8_t {
    InvalidHeader,
    LengthMismatch,
    LimitExceeded,
    Truncated,
    InvalidScalar,
    TrailingData,
    AllocationFailure,
};

struct WeavecDecodeError final {
    WeavecDecodeErrorCode code{};
    std::size_t byteOffset{};
    std::string message;
};

struct DecodeWeavecResult final {
    std::shared_ptr<const CompiledProgram> program;
    std::optional<WeavecDecodeError> decodeError;
    std::vector<ProgramValidationError> validationErrors;
};

[[nodiscard]] std::vector<std::uint8_t> EncodeWeavec(
    const CompiledProgram& program);

[[nodiscard]] DecodeWeavecResult DecodeWeavec(
    std::span<const std::uint8_t> bytes,
    const WeavecDecodeLimits& limits = {});

} // namespace inputweaver
