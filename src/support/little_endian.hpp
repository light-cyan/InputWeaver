#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace inputweaver::support {

template <typename Integer>
inline constexpr bool kLittleEndianInteger =
    std::is_integral_v<Integer>
    && std::is_unsigned_v<Integer>
    && !std::is_same_v<Integer, bool>
    && sizeof(Integer) <= sizeof(std::uintmax_t);

template <typename Integer>
void StoreLittleEndian(
    std::span<std::uint8_t> destination,
    std::size_t offset,
    Integer value) noexcept
{
    static_assert(kLittleEndianInteger<Integer>);
    assert(offset <= destination.size());
    assert(sizeof(Integer) <= destination.size() - offset);
    const std::uintmax_t bits = static_cast<std::uintmax_t>(value);
    for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        destination[offset + index] = static_cast<std::uint8_t>(bits >> shift);
    }
}

template <typename Integer>
void AppendLittleEndian(
    std::vector<std::uint8_t>& destination,
    Integer value)
{
    static_assert(kLittleEndianInteger<Integer>);
    const std::size_t offset = destination.size();
    destination.resize(offset + sizeof(Integer));
    StoreLittleEndian<Integer>(destination, offset, value);
}

template <typename Integer>
[[nodiscard]] bool ReadLittleEndian(
    std::span<const std::uint8_t> source,
    std::size_t& offset,
    Integer& value) noexcept
{
    static_assert(kLittleEndianInteger<Integer>);
    if (offset > source.size() || sizeof(Integer) > source.size() - offset) {
        return false;
    }
    std::uintmax_t bits = 0U;
    for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        bits |= static_cast<std::uintmax_t>(source[offset + index]) << shift;
    }
    value = static_cast<Integer>(bits);
    offset += sizeof(Integer);
    return true;
}

} // namespace inputweaver::support
