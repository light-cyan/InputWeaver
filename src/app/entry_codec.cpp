#include "entry_codec.hpp"

#include "support/utf8.hpp"

#include <charconv>
#include <limits>
#include <string_view>

namespace inputweaver::app {
namespace {

[[nodiscard]] std::string Escape(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    return result;
}

[[nodiscard]] bool Unescape(
    std::string_view value,
    std::string& result) noexcept
{
    try {
        result.clear();
        result.reserve(value.size());
        bool escaped = false;
        for (const char character : value) {
            if (!escaped) {
                if (character == '\\') {
                    escaped = true;
                } else {
                    result.push_back(character);
                }
                continue;
            }
            if (character == 'n') {
                result.push_back('\n');
            } else if (character == 'r') {
                result.push_back('\r');
            } else if (character == '\\') {
                result.push_back('\\');
            } else {
                return false;
            }
            escaped = false;
        }
        return !escaped;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::string_view TargetName(TargetMode mode) noexcept
{
    switch (mode) {
    case TargetMode::Compiled:
        return "compiled";
    case TargetMode::Executable:
        return "executable";
    case TargetMode::Global:
        return "global";
    }
    return {};
}

[[nodiscard]] std::string_view LoggingName(LoggingMode mode) noexcept
{
    switch (mode) {
    case LoggingMode::Off:
        return "off";
    case LoggingMode::Operational:
        return "operational";
    case LoggingMode::InputTrace:
        return "input-trace";
    }
    return {};
}

[[nodiscard]] bool ParseTarget(std::string_view text, TargetMode& mode) noexcept
{
    if (text == "compiled") {
        mode = TargetMode::Compiled;
        return true;
    }
    if (text == "executable") {
        mode = TargetMode::Executable;
        return true;
    }
    if (text == "global") {
        mode = TargetMode::Global;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseLogging(
    std::string_view text,
    LoggingMode& mode) noexcept
{
    if (text == "off") {
        mode = LoggingMode::Off;
        return true;
    }
    if (text == "operational") {
        mode = LoggingMode::Operational;
        return true;
    }
    if (text == "input-trace") {
        mode = LoggingMode::InputTrace;
        return true;
    }
    return false;
}

[[nodiscard]] bool TakeLine(
    std::string_view text,
    std::size_t& offset,
    std::string_view& line) noexcept
{
    if (offset > text.size()) {
        return false;
    }
    const std::size_t end = text.find('\n', offset);
    if (end == std::string_view::npos) {
        line = text.substr(offset);
        offset = text.size() + 1U;
    } else {
        line = text.substr(offset, end - offset);
        offset = end + 1U;
    }
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1U);
    }
    return true;
}

[[nodiscard]] bool ReadField(
    std::string_view text,
    std::size_t& offset,
    std::string_view key,
    std::string_view& value) noexcept
{
    std::string_view line;
    if (!TakeLine(text, offset, line)
        || !line.starts_with(key)
        || line.size() <= key.size()
        || line[key.size()] != '=') {
        return false;
    }
    value = line.substr(key.size() + 1U);
    return true;
}

} // namespace

bool ValidDisplayName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 256U || !support::IsValidUtf8(name)) {
        return false;
    }
    for (const char sourceCharacter : name) {
        const auto character = static_cast<unsigned char>(sourceCharacter);
        if (character < 0x20U || character == 0x7fU) {
            return false;
        }
    }
    return true;
}

bool ValidRunConfiguration(const RunConfiguration& configuration) noexcept
{
    if (!support::IsValidUtf8(configuration.executableSelector)) {
        return false;
    }
    if (configuration.target == TargetMode::Executable) {
        return !configuration.executableSelector.empty()
            && configuration.executableSelector.size() <= 32'768U;
    }
    return configuration.executableSelector.empty();
}

std::string EncodeEntry(const ProgramEntry& entry)
{
    std::string text = "InputWeaverEntry=2\n";
    text += "name=" + Escape(entry.displayName) + "\n";
    text += "target=" + std::string{TargetName(entry.configuration.target)}
        + "\n";
    text += "selector=" + Escape(entry.configuration.executableSelector)
        + "\n";
    text += "logging=" + std::string{LoggingName(entry.configuration.logging)}
        + "\n";
    text += "compiled_source_hash="
        + std::to_string(entry.compiledSourceHash) + "\n";
    return text;
}

bool DecodeEntry(
    ProgramEntryId id,
    std::string_view text,
    ProgramEntry& entry,
    std::string& error)
{
    std::size_t offset = 0U;
    std::string_view header;
    std::string_view name;
    std::string_view target;
    std::string_view selector;
    std::string_view logging;
    std::string_view compiledSourceHash;
    if (!TakeLine(text, offset, header)
        || (header != "InputWeaverEntry=1" && header != "InputWeaverEntry=2")
        || !ReadField(text, offset, "name", name)
        || !ReadField(text, offset, "target", target)
        || !ReadField(text, offset, "selector", selector)
        || !ReadField(text, offset, "logging", logging)
        || (header == "InputWeaverEntry=2"
            && !ReadField(
                text,
                offset,
                "compiled_source_hash",
                compiledSourceHash))) {
        error = "Invalid entry structure.";
        return false;
    }
    ProgramEntry decoded{};
    decoded.id = id;
    if (header == "InputWeaverEntry=2") {
        const auto parsed = std::from_chars(
            compiledSourceHash.data(),
            compiledSourceHash.data() + compiledSourceHash.size(),
            decoded.compiledSourceHash);
        if (parsed.ec != std::errc{}
            || parsed.ptr
                != compiledSourceHash.data() + compiledSourceHash.size()) {
            error = "Invalid entry value.";
            return false;
        }
    }
    if (!Unescape(name, decoded.displayName)
        || !Unescape(selector, decoded.configuration.executableSelector)
        || !ParseTarget(target, decoded.configuration.target)
        || !ParseLogging(logging, decoded.configuration.logging)
        || !ValidDisplayName(decoded.displayName)
        || !ValidRunConfiguration(decoded.configuration)) {
        error = "Invalid entry value.";
        return false;
    }
    entry = std::move(decoded);
    return true;
}

std::string EncodeProgramIndex(std::span<const ProgramEntryId> order)
{
    std::string text = "InputWeaverIndex=1\n";
    for (const ProgramEntryId id : order) {
        text += std::to_string(id) + "\n";
    }
    return text;
}

bool DecodeProgramIndex(
    std::string_view text,
    std::vector<ProgramEntryId>& order,
    std::string& error)
{
    order.clear();
    std::size_t offset = 0U;
    std::string_view header;
    if (!TakeLine(text, offset, header) || header != "InputWeaverIndex=1") {
        error = "Invalid program index header.";
        return false;
    }
    std::string_view line;
    while (offset <= text.size() && TakeLine(text, offset, line)) {
        if (line.empty()) {
            continue;
        }
        ProgramEntryId id{};
        const auto parsed = std::from_chars(
            line.data(),
            line.data() + line.size(),
            id);
        if (parsed.ec != std::errc{}
            || parsed.ptr != line.data() + line.size()
            || id == kInvalidProgramEntryId) {
            error = "Invalid program ID in index.";
            return false;
        }
        for (const ProgramEntryId existing : order) {
            if (existing == id) {
                error = "Duplicate program ID in index.";
                return false;
            }
        }
        order.push_back(id);
    }
    return true;
}

} // namespace inputweaver::app
