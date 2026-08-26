#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace inputweaver::support {

template <typename T, std::size_t Capacity>
class FixedSpscRing final {
public:
    static_assert(Capacity > 0U);

    [[nodiscard]] bool TryPush(const T& value) noexcept
    {
        const std::uint64_t write = writeIndex_.load(std::memory_order_relaxed);
        const std::uint64_t read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= kCapacity) {
            return false;
        }
        records_[static_cast<std::size_t>(write % kCapacity)] = value;
        writeIndex_.store(write + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPop(T& value) noexcept
    {
        const std::uint64_t read = readIndex_.load(std::memory_order_relaxed);
        const std::uint64_t write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        value = records_[static_cast<std::size_t>(read % kCapacity)];
        readIndex_.store(read + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return readIndex_.load(std::memory_order_acquire)
            == writeIndex_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::uint64_t kCapacity =
        static_cast<std::uint64_t>(Capacity);
    std::array<T, Capacity> records_{};
    alignas(64) std::atomic<std::uint64_t> writeIndex_{0U};
    alignas(64) std::atomic<std::uint64_t> readIndex_{0U};
};

} // namespace inputweaver::support
