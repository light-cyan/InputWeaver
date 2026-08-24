#pragma once

#include "windows_input_types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace inputweaver {

class WindowsOutputQueue {
public:
    [[nodiscard]] bool TryPush(const WindowsOutputItem& item) noexcept
    {
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_relaxed);
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_acquire);
        if (writeSequence - readSequence >= kWindowsOutputQueueCapacity) {
            return false;
        }

        entries_[static_cast<std::size_t>(writeSequence % kWindowsOutputQueueCapacity)] = item;
        writeSequence_.store(writeSequence + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPop(WindowsOutputItem& item) noexcept
    {
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_relaxed);
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_acquire);
        if (readSequence == writeSequence) {
            return false;
        }

        item = entries_[static_cast<std::size_t>(readSequence % kWindowsOutputQueueCapacity)];
        readSequence_.store(readSequence + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return readSequence_.load(std::memory_order_acquire)
            == writeSequence_.load(std::memory_order_acquire);
    }

private:
    std::array<WindowsOutputItem, kWindowsOutputQueueCapacity> entries_{};
    alignas(64) std::atomic<std::uint64_t> writeSequence_{ 0 };
    alignas(64) std::atomic<std::uint64_t> readSequence_{ 0 };
};

} // namespace inputweaver
