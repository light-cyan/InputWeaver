#include "color_scheme.hpp"

#include "support/utf8.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace inputweaver::ui::tui {
namespace {

inline constexpr std::size_t kColorCount = 38U;

class JsonCursor final {
public:
    explicit JsonCursor(std::string_view text) noexcept
        : text_(text)
    {
    }

    void SkipWhitespace() noexcept
    {
        while (offset_ < text_.size()) {
            const char character = text_[offset_];
            if (character != ' ' && character != '\t'
                && character != '\r' && character != '\n') {
                break;
            }
            ++offset_;
        }
    }

    [[nodiscard]] bool Consume(char expected) noexcept
    {
        SkipWhitespace();
        if (offset_ >= text_.size() || text_[offset_] != expected) {
            return false;
        }
        ++offset_;
        return true;
    }

    [[nodiscard]] bool Peek(char expected) noexcept
    {
        SkipWhitespace();
        return offset_ < text_.size() && text_[offset_] == expected;
    }

    [[nodiscard]] bool AtEnd() noexcept
    {
        SkipWhitespace();
        return offset_ == text_.size();
    }

    [[nodiscard]] bool String(std::string& value)
    {
        SkipWhitespace();
        if (offset_ >= text_.size() || text_[offset_] != '"') {
            return false;
        }
        ++offset_;
        value.clear();
        while (offset_ < text_.size()) {
            const char character = text_[offset_++];
            if (character == '"') {
                return support::IsValidUtf8(value);
            }
            if (character == '\\') {
                if (offset_ >= text_.size()) {
                    return false;
                }
                const char escaped = text_[offset_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    return false;
                }
            } else if (static_cast<unsigned char>(character) < 0x20U) {
                return false;
            } else {
                value.push_back(character);
            }
        }
        return false;
    }

    [[nodiscard]] bool Unsigned(std::uint32_t& value) noexcept
    {
        SkipWhitespace();
        const char* begin = text_.data() + offset_;
        const char* end = text_.data() + text_.size();
        const auto parsed = std::from_chars(begin, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) {
            return false;
        }
        offset_ += static_cast<std::size_t>(parsed.ptr - begin);
        return true;
    }

private:
    std::string_view text_;
    std::size_t offset_{};
};

[[nodiscard]] bool HexDigit(char character, std::uint8_t& value) noexcept
{
    if (character >= '0' && character <= '9') {
        value = static_cast<std::uint8_t>(character - '0');
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        value = static_cast<std::uint8_t>(character - 'A' + 10);
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<std::uint8_t>(character - 'a' + 10);
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseColor(std::string_view text, RgbColor& color) noexcept
{
    if (text.size() != 7U || text[0] != '#') {
        return false;
    }
    std::array<std::uint8_t, 6U> digits{};
    for (std::size_t index = 0U; index < digits.size(); ++index) {
        if (!HexDigit(text[index + 1U], digits[index])) {
            return false;
        }
    }
    color = {
        static_cast<std::uint8_t>(digits[0] * 16U + digits[1]),
        static_cast<std::uint8_t>(digits[2] * 16U + digits[3]),
        static_cast<std::uint8_t>(digits[4] * 16U + digits[5])};
    return true;
}

[[nodiscard]] bool AssignColor(
    std::string_view key,
    RgbColor color,
    ColorScheme& scheme,
    std::array<bool, kColorCount>& assigned) noexcept
{
#define INPUTWEAVER_COLOR_FIELD(index, jsonName, fieldName) \
    if (key == jsonName) { \
        if (assigned[index]) { \
            return false; \
        } \
        assigned[index] = true; \
        scheme.fieldName = color; \
        return true; \
    }
    INPUTWEAVER_COLOR_FIELD(0U, "text", text)
    INPUTWEAVER_COLOR_FIELD(1U, "muted_text", mutedText)
    INPUTWEAVER_COLOR_FIELD(2U, "unfocused_border", unfocusedBorder)
    INPUTWEAVER_COLOR_FIELD(3U, "focus_console", focusConsole)
    INPUTWEAVER_COLOR_FIELD(4U, "focus_program", focusProgram)
    INPUTWEAVER_COLOR_FIELD(
        5U, "focus_program_information", focusProgramInformation)
    INPUTWEAVER_COLOR_FIELD(6U, "focus_source", focusSource)
    INPUTWEAVER_COLOR_FIELD(7U, "focus_events", focusEvents)
    INPUTWEAVER_COLOR_FIELD(8U, "focus_variables", focusVariables)
    INPUTWEAVER_COLOR_FIELD(
        9U, "focus_action_executions", focusActionExecutions)
    INPUTWEAVER_COLOR_FIELD(
        10U, "selection_active_foreground", selectionActiveForeground)
    INPUTWEAVER_COLOR_FIELD(
        11U, "selection_active_background", selectionActiveBackground)
    INPUTWEAVER_COLOR_FIELD(
        12U, "selection_inactive_foreground", selectionInactiveForeground)
    INPUTWEAVER_COLOR_FIELD(
        13U, "selection_inactive_background", selectionInactiveBackground)
    INPUTWEAVER_COLOR_FIELD(14U, "status_running", statusRunning)
    INPUTWEAVER_COLOR_FIELD(15U, "status_debug", statusDebug)
    INPUTWEAVER_COLOR_FIELD(16U, "status_dry_run", statusDryRun)
    INPUTWEAVER_COLOR_FIELD(
        17U, "status_exec_permission", statusExecPermission)
    INPUTWEAVER_COLOR_FIELD(18U, "execution_running", executionRunning)
    INPUTWEAVER_COLOR_FIELD(19U, "execution_completed", executionCompleted)
    INPUTWEAVER_COLOR_FIELD(20U, "execution_failed", executionFailed)
    INPUTWEAVER_COLOR_FIELD(21U, "execution_cancelled", executionCancelled)
    INPUTWEAVER_COLOR_FIELD(22U, "syntax_keyword", syntaxKeyword)
    INPUTWEAVER_COLOR_FIELD(23U, "syntax_type", syntaxType)
    INPUTWEAVER_COLOR_FIELD(24U, "syntax_variable", syntaxVariable)
    INPUTWEAVER_COLOR_FIELD(25U, "syntax_control", syntaxControl)
    INPUTWEAVER_COLOR_FIELD(26U, "syntax_action", syntaxAction)
    INPUTWEAVER_COLOR_FIELD(27U, "syntax_operator", syntaxOperator)
    INPUTWEAVER_COLOR_FIELD(28U, "syntax_string", syntaxString)
    INPUTWEAVER_COLOR_FIELD(29U, "syntax_constant", syntaxConstant)
    INPUTWEAVER_COLOR_FIELD(30U, "syntax_comment", syntaxComment)
    INPUTWEAVER_COLOR_FIELD(31U, "editor_current_line", editorCurrentLine)
    INPUTWEAVER_COLOR_FIELD(32U, "editor_error_line", editorErrorLine)
    INPUTWEAVER_COLOR_FIELD(33U, "health_trusted", healthTrusted)
    INPUTWEAVER_COLOR_FIELD(34U, "health_recovering", healthRecovering)
    INPUTWEAVER_COLOR_FIELD(35U, "health_fault", healthFault)
    INPUTWEAVER_COLOR_FIELD(36U, "focus_input_state", focusInputState)
    INPUTWEAVER_COLOR_FIELD(37U, "focus_meters", focusMeters)
#undef INPUTWEAVER_COLOR_FIELD
    return false;
}

[[nodiscard]] bool ParseColors(
    JsonCursor& cursor,
    ColorScheme& scheme,
    std::string& error)
{
    if (!cursor.Consume('{')) {
        error = "colors must be an object.";
        return false;
    }
    std::array<bool, kColorCount> assigned{};
    while (!cursor.Peek('}')) {
        std::string key;
        std::string value;
        RgbColor color{};
        if (!cursor.String(key) || !cursor.Consume(':')
            || !cursor.String(value) || !ParseColor(value, color)) {
            error = "Invalid color entry.";
            return false;
        }
        if (!AssignColor(key, color, scheme, assigned)) {
            error = "Unknown or duplicate color: " + key + ".";
            return false;
        }
        if (cursor.Peek('}')) {
            break;
        }
        if (!cursor.Consume(',')) {
            error = "Expected a comma between colors.";
            return false;
        }
    }
    if (!cursor.Consume('}')) {
        error = "The colors object is not closed.";
        return false;
    }
    for (const bool present : assigned) {
        if (!present) {
            error = "A required color is missing.";
            return false;
        }
    }
    return true;
}

} // namespace

bool ParseColorScheme(
    std::string_view json,
    ColorScheme& scheme,
    std::string& error)
{
    JsonCursor cursor(json);
    if (!cursor.Consume('{')) {
        error = "The root value must be an object.";
        return false;
    }
    bool hasVersion = false;
    bool hasColors = false;
    ColorScheme parsed{};
    while (!cursor.Peek('}')) {
        std::string key;
        if (!cursor.String(key) || !cursor.Consume(':')) {
            error = "Invalid root field.";
            return false;
        }
        if (key == "version" && !hasVersion) {
            std::uint32_t version{};
            if (!cursor.Unsigned(version) || version != 1U) {
                error = "Unsupported color scheme version.";
                return false;
            }
            hasVersion = true;
        } else if (key == "colors" && !hasColors) {
            if (!ParseColors(cursor, parsed, error)) {
                return false;
            }
            hasColors = true;
        } else {
            error = "Unknown or duplicate root field: " + key + ".";
            return false;
        }
        if (cursor.Peek('}')) {
            break;
        }
        if (!cursor.Consume(',')) {
            error = "Expected a comma between root fields.";
            return false;
        }
    }
    if (!cursor.Consume('}') || !cursor.AtEnd() || !hasVersion || !hasColors) {
        error = "The color scheme is incomplete.";
        return false;
    }
    scheme = parsed;
    return true;
}

} // namespace inputweaver::ui::tui
