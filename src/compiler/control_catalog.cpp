#include "control_catalog.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace inputweaver::compiler {
namespace {

struct NamedUsage final {
    std::string_view name;
    std::uint32_t usage;
};

constexpr std::array<NamedUsage, 36U> kKeyboardUsages{{
    {"Enter", 0x28U},
    {"Esc", 0x29U},
    {"Backspace", 0x2aU},
    {"Tab", 0x2bU},
    {"Space", 0x2cU},
    {"CapsLock", 0x39U},
    {"ScrollLock", 0x47U},
    {"Pause", 0x48U},
    {"Insert", 0x49U},
    {"Home", 0x4aU},
    {"PageUp", 0x4bU},
    {"Delete", 0x4cU},
    {"End", 0x4dU},
    {"PageDown", 0x4eU},
    {"ArrowRight", 0x4fU},
    {"ArrowLeft", 0x50U},
    {"ArrowDown", 0x51U},
    {"ArrowUp", 0x52U},
    {"NumLock", 0x53U},
    {"NumpadDivide", 0x54U},
    {"NumpadMultiply", 0x55U},
    {"NumpadSubtract", 0x56U},
    {"NumpadAdd", 0x57U},
    {"NumpadDecimal", 0x63U},
    {"LCtrl", 0xe0U},
    {"LShift", 0xe1U},
    {"LAlt", 0xe2U},
    {"RCtrl", 0xe4U},
    {"RShift", 0xe5U},
    {"RAlt", 0xe6U},
    {"Numpad0", 0x62U},
    {"Digit0", 0x27U},
    {"Numpad1", 0x59U},
    {"Digit1", 0x1eU},
    {"F1", 0x3aU},
    {"F13", 0x68U},
}};

[[nodiscard]] std::optional<unsigned int> ParseSuffix(
    std::string_view value,
    std::string_view prefix) noexcept
{
    if (!value.starts_with(prefix) || value.size() == prefix.size()) {
        return std::nullopt;
    }
    if (value.size() > prefix.size() + 1U
        && value[prefix.size()] == '0') {
        return std::nullopt;
    }
    unsigned int result = 0U;
    const char* begin = value.data() + prefix.size();
    const char* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<ControlRef> ResolveKeyboard(
    std::string_view name) noexcept
{
    if (name.size() == 1U && name.front() >= 'A' && name.front() <= 'Z') {
        return ControlRef{
            kControlNamespaceUsbHid,
            0x07U,
            static_cast<std::uint32_t>(name.front() - 'A') + 0x04U,
            kControlQualifierNone};
    }
    if (const auto function = ParseSuffix(name, "F"); function.has_value()) {
        if (*function >= 1U && *function <= 12U) {
            return ControlRef{
                kControlNamespaceUsbHid,
                0x07U,
                0x3aU + *function - 1U,
                kControlQualifierNone};
        }
        if (*function >= 13U && *function <= 24U) {
            return ControlRef{
                kControlNamespaceUsbHid,
                0x07U,
                0x68U + *function - 13U,
                kControlQualifierNone};
        }
    }
    if (const auto digit = ParseSuffix(name, "Digit"); digit.has_value()
        && *digit <= 9U) {
        const std::uint32_t usage = *digit == 0U
            ? 0x27U
            : 0x1eU + *digit - 1U;
        return ControlRef{
            kControlNamespaceUsbHid,
            0x07U,
            usage,
            kControlQualifierNone};
    }
    if (const auto digit = ParseSuffix(name, "Numpad"); digit.has_value()
        && *digit <= 9U) {
        const std::uint32_t usage = *digit == 0U
            ? 0x62U
            : 0x59U + *digit - 1U;
        return ControlRef{
            kControlNamespaceUsbHid,
            0x07U,
            usage,
            kControlQualifierNone};
    }
    for (const NamedUsage entry : kKeyboardUsages) {
        if (entry.name == name) {
            return ControlRef{
                kControlNamespaceUsbHid,
                0x07U,
                entry.usage,
                kControlQualifierNone};
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<ControlRef> ResolveNamedControl(std::string_view name) noexcept
{
    if (name.starts_with("Keyboard.")) {
        return ResolveKeyboard(name.substr(9U));
    }
    if (const auto keyboard = ResolveKeyboard(name); keyboard.has_value()) {
        return keyboard;
    }

    constexpr std::array<NamedUsage, 5U> mouse{{
        {"Mouse.Left", 1U},
        {"Mouse.Right", 2U},
        {"Mouse.Middle", 3U},
        {"Mouse.X1", 4U},
        {"Mouse.X2", 5U},
    }};
    for (const NamedUsage entry : mouse) {
        if (entry.name == name) {
            return ControlRef{
                kControlNamespaceUsbHid,
                0x09U,
                entry.usage,
                kControlQualifierNone};
        }
    }

    constexpr std::array<NamedUsage, 7U> consumer{{
        {"Consumer.PlayPause", 0x00cdU},
        {"Consumer.ScanNextTrack", 0x00b5U},
        {"Consumer.ScanPreviousTrack", 0x00b6U},
        {"Consumer.Stop", 0x00b7U},
        {"Consumer.Mute", 0x00e2U},
        {"Consumer.VolumeUp", 0x00e9U},
        {"Consumer.VolumeDown", 0x00eaU},
    }};
    for (const NamedUsage entry : consumer) {
        if (entry.name == name) {
            return ControlRef{
                kControlNamespaceUsbHid,
                0x0cU,
                entry.usage,
                kControlQualifierNone};
        }
    }

    if (name == "Windows.Keyboard.IMEOn") {
        return ControlRef{
            kControlNamespaceWindows,
            kWindowsVirtualKeyFamily,
            0x16U,
            kControlQualifierNone};
    }
    if (name == "Linux.Keyboard.Compose") {
        return ControlRef{
            kControlNamespaceLinux,
            kLinuxEvKeyFamily,
            127U,
            kControlQualifierNone};
    }
    if (name == "MacOS.Keyboard.Fn") {
        return ControlRef{
            kControlNamespaceMacOs,
            kMacOsKeyCodeFamily,
            0x3fU,
            kControlQualifierNone};
    }
    return std::nullopt;
}

bool IsReservedControlIdentifier(std::string_view name) noexcept
{
    return name.find('.') == std::string_view::npos
        && ResolveNamedControl(name).has_value();
}

} // namespace inputweaver::compiler
