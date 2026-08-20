#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>

#include "core/input_event.hpp"

namespace inputweaver {

inline constexpr std::size_t kMaximumPreparedInputs = kMaxActionsPerBatch;

using SendInputFunction = UINT(WINAPI*)(UINT, LPINPUT, int);

enum class InjectionOutcome : unsigned char {
    Succeeded,
    InvalidSelfTag,
    InvalidBatch,
    ConversionFailed,
    SendFailed,
    SendPartial
};

struct PreparedInputBatch {
    std::array<INPUT, kMaximumPreparedInputs> inputs{};
    UINT count{0};
    DWORD error{ERROR_SUCCESS};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == ERROR_SUCCESS && count != 0;
    }
};

struct InjectionResult {
    InjectionOutcome outcome{InjectionOutcome::InvalidBatch};
    UINT requested{0};
    UINT sent{0};
    DWORD error{ERROR_SUCCESS};
    bool cleanupAttempted{false};
    UINT cleanupRequested{0};
    UINT cleanupSent{0};
    DWORD cleanupError{ERROR_SUCCESS};

    [[nodiscard]] bool Succeeded() const noexcept {
        return outcome == InjectionOutcome::Succeeded;
    }
};

class InjectionCircuitBreaker final {
public:
    explicit InjectionCircuitBreaker(unsigned int failureThreshold) noexcept
        : failureThreshold_(failureThreshold == 0 ? 1U : failureThreshold) {}

    void RecordSuccess() noexcept {
        if (!open_) {
            consecutiveFailures_ = 0;
        }
    }

    bool RecordFailure() noexcept {
        if (!open_) {
            ++consecutiveFailures_;
            open_ = consecutiveFailures_ >= failureThreshold_;
        }
        return open_;
    }

    [[nodiscard]] unsigned int ConsecutiveFailures() const noexcept {
        return consecutiveFailures_;
    }

    [[nodiscard]] bool IsOpen() const noexcept {
        return open_;
    }

private:
    unsigned int failureThreshold_;
    unsigned int consecutiveFailures_{0};
    bool open_{false};
};

class InputInjector final {
public:
    explicit InputInjector(
        inputweaver::SelfTag selfTag,
        SendInputFunction sendInput = &::SendInput) noexcept;

    [[nodiscard]] inputweaver::SelfTag Tag() const noexcept;
    [[nodiscard]] PreparedInputBatch Prepare(
        const ActionBatch& batch) const noexcept;
    [[nodiscard]] InjectionResult Inject(
        const ActionBatch& batch) const noexcept;

private:
    inputweaver::SelfTag selfTag_;
    SendInputFunction sendInput_;
};

}  // namespace inputweaver
