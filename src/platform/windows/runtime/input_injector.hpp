#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows_input_types.hpp"

namespace inputweaver {

using SendInputFunction = UINT(WINAPI*)(UINT, LPINPUT, int);

enum class InjectionOutcome : unsigned char {
    Succeeded,
    InvalidSelfTag,
    InvalidOutput,
    ConversionFailed,
    SendFailed
};

struct PreparedInput {
    INPUT input{};
    DWORD error{ERROR_INVALID_DATA};
    bool emit{true};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == ERROR_SUCCESS;
    }
};

struct InjectionResult {
    InjectionOutcome outcome{InjectionOutcome::InvalidOutput};
    UINT requested{0};
    UINT sent{0};
    DWORD error{ERROR_SUCCESS};

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

private:
    unsigned int failureThreshold_;
    unsigned int consecutiveFailures_{0};
    bool open_{false};
};

class InputInjector final {
public:
    explicit InputInjector(
        WindowsSelfTag selfTag,
        SendInputFunction sendInput = &::SendInput,
        bool dryRun = false) noexcept;

    [[nodiscard]] PreparedInput Prepare(
        const WindowsOutputItem& item) const noexcept;
    [[nodiscard]] InjectionResult Inject(
        const WindowsOutputItem& item) const noexcept;
    [[nodiscard]] InjectionResult InjectPrepared(PreparedInput prepared) const noexcept;

private:
    WindowsSelfTag selfTag_;
    SendInputFunction sendInput_;
    bool dryRun_;
};

}  // namespace inputweaver
