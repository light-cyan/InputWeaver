#pragma once

#include "input_injector.hpp"

#include <mutex>
#include <vector>

namespace inputweaver::win32 {

struct PointerDesktop final {
    RECT bounds{};
    RECT clip{};
    std::vector<RECT> monitors;
    [[nodiscard]] bool Capture() noexcept;
};

struct PreparedPointerOutput final {
    PreparedInput native{};
    ScreenPoint origin{};
    ScreenPoint destination{};
    MouseDelta remainder{};
    std::uint64_t physicalVersion{};
    bool movement{};

    [[nodiscard]] bool TargetsMatch(support::CallbackRef<bool(ScreenPoint) noexcept> accepts) const noexcept
    {
        return accepts.Invoke(origin) && (!movement || accepts.Invoke(destination));
    }
};

class WindowsPointerOutput final {
public:
    explicit WindowsPointerOutput(bool dryRun = false) noexcept : dryRun_(dryRun) {}
    [[nodiscard]] PreparedPointerOutput Prepare(const RuntimePointerOutput& request, std::uint64_t generation) noexcept;
    [[nodiscard]] PreparedPointerOutput Prepare(const RuntimePointerOutput& request, std::uint64_t generation,
        ScreenPoint actualPosition, const PointerDesktop& desktop) noexcept;
    void Commit(const PreparedPointerOutput& prepared) noexcept;
    void ObservePhysical(ScreenPoint position) noexcept;

private:
    const bool dryRun_;
    MouseDelta remainder_{};
    std::uint64_t generation_{};
    std::mutex positionMutex_;
    ScreenPoint simulatedPosition_{};
    std::uint64_t physicalVersion_{};
    bool positioned_{};
};

} // namespace inputweaver::win32
