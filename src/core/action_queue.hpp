#pragma once

#include "input_event.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace inputweaver {

enum class ActionQueuePushResult : unsigned char {
    Accepted,
    Full,
    CommitRejected
};

class ActionQueue {
public:
    ActionQueue() noexcept = default;

    ActionQueue(const ActionQueue&) = delete;
    ActionQueue& operator=(const ActionQueue&) = delete;
    ActionQueue(ActionQueue&&) = delete;
    ActionQueue& operator=(ActionQueue&&) = delete;

    [[nodiscard]] bool TryPush(const ActionBatch& batch) noexcept
    {
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_relaxed);
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_acquire);
        if (writeSequence - readSequence >= kActionQueueCapacity) {
            rejectedPushes_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        entries_[static_cast<std::size_t>(writeSequence % kActionQueueCapacity)] = batch;
        writeSequence_.store(writeSequence + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPushPair(
        const ActionBatch& first,
        const ActionBatch& second) noexcept
    {
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_relaxed);
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_acquire);
        if (writeSequence - readSequence > kActionQueueCapacity - 2U) {
            rejectedPushes_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        entries_[static_cast<std::size_t>(writeSequence % kActionQueueCapacity)] = first;
        entries_[static_cast<std::size_t>((writeSequence + 1U) % kActionQueueCapacity)] = second;
        writeSequence_.store(writeSequence + 2U, std::memory_order_release);
        return true;
    }

    template <typename CommitFunction>
    [[nodiscard]] ActionQueuePushResult TryPushWithCommit(
        const ActionBatch& batch,
        CommitFunction&& commitFunction) noexcept
    {
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_relaxed);
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_acquire);
        if (writeSequence - readSequence >= kActionQueueCapacity) {
            rejectedPushes_.fetch_add(1, std::memory_order_relaxed);
            return ActionQueuePushResult::Full;
        }

        entries_[static_cast<std::size_t>(writeSequence % kActionQueueCapacity)] = batch;
        if (!commitFunction()) {
            return ActionQueuePushResult::CommitRejected;
        }
        writeSequence_.store(writeSequence + 1, std::memory_order_release);
        return ActionQueuePushResult::Accepted;
    }

    [[nodiscard]] bool TryPop(ActionBatch& batch) noexcept
    {
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_relaxed);
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_acquire);
        if (readSequence == writeSequence) {
            return false;
        }

        batch = entries_[static_cast<std::size_t>(readSequence % kActionQueueCapacity)];
        readSequence_.store(readSequence + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return readSequence_.load(std::memory_order_acquire)
            == writeSequence_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t SizeApprox() const noexcept
    {
        const std::uint64_t writeSequence = writeSequence_.load(std::memory_order_acquire);
        const std::uint64_t readSequence = readSequence_.load(std::memory_order_acquire);
        const std::uint64_t size = writeSequence - readSequence;
        return static_cast<std::size_t>(
            size < kActionQueueCapacity ? size : kActionQueueCapacity);
    }

    [[nodiscard]] std::uint64_t RejectedPushCount() const noexcept
    {
        return rejectedPushes_.load(std::memory_order_relaxed);
    }

private:
    std::array<ActionBatch, kActionQueueCapacity> entries_{};
    alignas(64) std::atomic<std::uint64_t> writeSequence_{ 0 };
    alignas(64) std::atomic<std::uint64_t> readSequence_{ 0 };
    std::atomic<std::uint64_t> rejectedPushes_{ 0 };
};

} // namespace inputweaver
