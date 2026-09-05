#include "pointer_output.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace inputweaver::win32 {
namespace {

BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT rectangle, LPARAM context) noexcept
{
    try {
        reinterpret_cast<PointerDesktop*>(context)->monitors.push_back(*rectangle);
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}

bool ReachablePoint(const PointerDesktop& desktop, double x, double y, ScreenPoint& point) noexcept
{
    x = std::clamp(x, static_cast<double>(desktop.bounds.left), static_cast<double>(desktop.bounds.right) - 1);
    y = std::clamp(y, static_cast<double>(desktop.bounds.top), static_cast<double>(desktop.bounds.bottom) - 1);
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto& monitor : desktop.monitors) {
        RECT reachable{};
        if (!IntersectRect(&reachable, &monitor, &desktop.clip)) continue;
        const double px = std::clamp(x, static_cast<double>(reachable.left), static_cast<double>(reachable.right) - 1);
        const double py = std::clamp(y, static_cast<double>(reachable.top), static_cast<double>(reachable.bottom) - 1);
        const double distance = std::hypot(px - x, py - y);
        if (distance < nearest) {
            nearest = distance;
            point = {static_cast<InputCoordinate>(std::round(px)), static_cast<InputCoordinate>(std::round(py))};
        }
    }
    return std::isfinite(nearest);
}

LONG AbsoluteCoordinate(InputCoordinate pixel, LONG first, LONG last) noexcept
{
    const double encoded = (static_cast<double>(pixel) - first + 0.5) * 65536.0
        / (static_cast<double>(last) - first);
    return static_cast<LONG>(std::clamp(encoded, 0.0, 65535.0));
}

double WholePixels(double amount, double& remainder) noexcept
{
    const double total = amount + remainder;
    const double whole = std::trunc(total);
    remainder = total - whole;
    return whole;
}

} // namespace

bool PointerDesktop::Capture() noexcept
{
    monitors.clear();
    bounds.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.right = bounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.bottom = bounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds.right > bounds.left && bounds.bottom > bounds.top && GetClipCursor(&clip)
        && EnumDisplayMonitors(nullptr, nullptr, &CollectMonitor, reinterpret_cast<LPARAM>(this))
        && !monitors.empty();
}

PreparedPointerOutput WindowsPointerOutput::Prepare(const RuntimePointerOutput& request,
    std::uint64_t generation) noexcept
{
    POINT pointer{};
    PointerDesktop desktop;
    if (!GetPhysicalCursorPos(&pointer)
        || (request.operation <= PointerOperation::MoveTo && !desktop.Capture())) return {};
    return Prepare(request, generation, {pointer.x, pointer.y}, desktop);
}

PreparedPointerOutput WindowsPointerOutput::Prepare(const RuntimePointerOutput& request,
    std::uint64_t generation, ScreenPoint actualPosition, const PointerDesktop& desktop) noexcept
{
    PreparedPointerOutput prepared{};
    {
        const std::lock_guard lock(positionMutex_);
        if (generation_ != generation) {
            generation_ = generation;
            remainder_ = {};
            positioned_ = false;
        }
        prepared.origin = dryRun_ && positioned_ ? simulatedPosition_ : actualPosition;
        prepared.physicalVersion = physicalVersion_;
    }
    prepared.destination = prepared.origin;
    prepared.remainder = remainder_;
    prepared.movement = request.operation <= PointerOperation::MoveTo;
    auto& native = prepared.native.input;
    native.type = INPUT_MOUSE;
    if (prepared.movement) {
        double x = request.x;
        double y = request.y;
        if (request.operation == PointerOperation::MoveBy) {
            x = prepared.origin.x + WholePixels(x, prepared.remainder.dx);
            y = prepared.origin.y + WholePixels(y, prepared.remainder.dy);
        } else {
            prepared.remainder.dx = 0;
            prepared.remainder.dy = 0;
        }
        if (!ReachablePoint(desktop, x, y, prepared.destination)) return prepared;
        native.mi.dx = AbsoluteCoordinate(prepared.destination.x, desktop.bounds.left, desktop.bounds.right);
        native.mi.dy = AbsoluteCoordinate(prepared.destination.y, desktop.bounds.top, desktop.bounds.bottom);
        native.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        prepared.native.emit = prepared.destination != prepared.origin;
    } else {
        const bool horizontal = request.operation == PointerOperation::ScrollHorizontal;
        double& remainder = horizontal ? prepared.remainder.wheelX : prepared.remainder.wheelY;
        const double total = std::clamp((request.x + remainder) * WHEEL_DELTA,
            static_cast<double>(std::numeric_limits<LONG>::min()), static_cast<double>(std::numeric_limits<LONG>::max()));
        const auto ticks = static_cast<LONG>(std::trunc(total));
        remainder = (total - ticks) / WHEEL_DELTA;
        native.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
        native.mi.mouseData = static_cast<DWORD>(ticks);
        prepared.native.emit = ticks != 0;
    }
    prepared.native.error = ERROR_SUCCESS;
    return prepared;
}

void WindowsPointerOutput::Commit(const PreparedPointerOutput& prepared) noexcept
{
    remainder_ = prepared.remainder;
    if (dryRun_) {
        const std::lock_guard lock(positionMutex_);
        if (physicalVersion_ == prepared.physicalVersion) {
            simulatedPosition_ = prepared.destination;
            positioned_ = true;
        }
    }
}

void WindowsPointerOutput::ObservePhysical(ScreenPoint position) noexcept
{
    if (!dryRun_) return;
    const std::lock_guard lock(positionMutex_);
    simulatedPosition_ = position;
    positioned_ = true;
    ++physicalVersion_;
}

} // namespace inputweaver::win32
