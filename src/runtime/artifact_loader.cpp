#include "artifact_loader.hpp"

#include <fstream>
#include <ios>
#include <limits>
#include <new>
#include <utility>

namespace inputweaver {

RuntimeArtifactReadResult ReadWeavec(
    const std::filesystem::path& path,
    const WeavecDecodeLimits& limits)
{
    RuntimeArtifactReadResult result{};
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        result.error = RuntimeArtifactLoadErrorCode::OpenFailed;
        return result;
    }
    const std::ifstream::pos_type end = input.tellg();
    if (end < std::ifstream::pos_type{0}) {
        result.error = RuntimeArtifactLoadErrorCode::SizeUnavailable;
        return result;
    }
    const auto byteCount = static_cast<std::uint64_t>(end);
    const std::uint64_t maximumFileBytes = limits.maximumPayloadBytes
        > (std::numeric_limits<std::uint64_t>::max)() - kWeavecHeaderSize
        ? (std::numeric_limits<std::uint64_t>::max)()
        : limits.maximumPayloadBytes + kWeavecHeaderSize;
    if (byteCount > maximumFileBytes
        || byteCount > (std::numeric_limits<std::size_t>::max)()) {
        result.error = RuntimeArtifactLoadErrorCode::LimitExceeded;
        return result;
    }

    std::vector<std::uint8_t> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(byteCount));
    } catch (const std::bad_alloc&) {
        result.error = RuntimeArtifactLoadErrorCode::AllocationFailure;
        return result;
    }
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        result.error = RuntimeArtifactLoadErrorCode::ReadFailed;
        return result;
    }

    DecodeWeavecResult decoded = DecodeWeavec(bytes, limits);
    if (decoded.decodeError.has_value()) {
        result.error = RuntimeArtifactLoadErrorCode::DecodeFailed;
        result.decodeError = std::move(decoded.decodeError);
        return result;
    }
    if (!decoded.validationErrors.empty()) {
        result.error = RuntimeArtifactLoadErrorCode::ValidationFailed;
        result.validationErrors.swap(decoded.validationErrors);
        return result;
    }
    result.program = std::move(decoded.program);
    return result;
}

RuntimeArtifactLoadResult LoadAndActivateWeavec(
    ProgramRuntime& runtime,
    const std::filesystem::path& path,
    const WeavecDecodeLimits& limits)
{
    RuntimeArtifactLoadResult result{};
    RuntimeArtifactReadResult read = ReadWeavec(path, limits);
    if (!read.Succeeded()) {
        result.error = read.error;
        result.decodeError = std::move(read.decodeError);
        result.validationErrors = std::move(read.validationErrors);
        return result;
    }
    const RuntimeActivationResult activation = runtime.Activate(
        std::move(read.program));
    if (!activation.activated) {
        result.error = RuntimeArtifactLoadErrorCode::ActivationFailed;
        result.activationError = activation.error;
        return result;
    }
    result.activated = true;
    return result;
}

} // namespace inputweaver
