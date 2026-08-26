#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace inputweaver::support {

template <typename Item, std::size_t Capacity>
class BoundedMpmcQueue final {
public:
    BoundedMpmcQueue() noexcept
    {
        static_assert(Capacity >= 2U);
        static_assert((Capacity & (Capacity - 1U)) == 0U);
        static_assert(std::is_nothrow_default_constructible_v<Item>);
        static_assert(std::is_nothrow_copy_assignable_v<Item>);
        for (std::size_t index = 0U; index < Capacity; ++index) {
            cells_[index].sequence.store(index, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool TryPush(const Item& item) noexcept
    {
        std::size_t position = enqueuePosition_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &cells_[position & (Capacity - 1U)];
            const std::size_t sequence = cell->sequence.load(
                std::memory_order_acquire);
            const std::intptr_t difference = static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (enqueuePosition_.compare_exchange_weak(
                        position,
                        position + 1U,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }
        cell->item = item;
        cell->sequence.store(position + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPop(Item& item) noexcept
    {
        std::size_t position = dequeuePosition_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &cells_[position & (Capacity - 1U)];
            const std::size_t sequence = cell->sequence.load(
                std::memory_order_acquire);
            const std::intptr_t difference = static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position + 1U);
            if (difference == 0) {
                if (dequeuePosition_.compare_exchange_weak(
                        position,
                        position + 1U,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = dequeuePosition_.load(std::memory_order_relaxed);
            }
        }
        item = cell->item;
        cell->sequence.store(position + Capacity, std::memory_order_release);
        return true;
    }

private:
    struct Cell final {
        std::atomic<std::size_t> sequence{};
        Item item{};
    };

    std::array<Cell, Capacity> cells_{};
    alignas(64) std::atomic<std::size_t> enqueuePosition_{};
    alignas(64) std::atomic<std::size_t> dequeuePosition_{};
};

} // namespace inputweaver::support
