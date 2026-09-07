#pragma once

#include <array>
#include <string_view>

namespace inputweaver::language {

enum class WordRole {
    None,
    Type,
    Constant,
    Transition,
    Keyword,
    Action,
    IntrinsicValue,
    RawControl,
    ScanPrefix,
};

struct WordEntry final {
    std::string_view text;
    WordRole role{WordRole::None};
};

inline constexpr std::array kWordCatalog{
    WordEntry{"TARGET", WordRole::IntrinsicValue},
    WordEntry{"TAP_DURATION", WordRole::IntrinsicValue},
    WordEntry{"ACTION_GAP", WordRole::IntrinsicValue},
    WordEntry{"MOUSE_IDLE_TIMEOUT", WordRole::IntrinsicValue},
    WordEntry{"Mouse", WordRole::IntrinsicValue},
    WordEntry{"RAND_SEED", WordRole::IntrinsicValue},
    WordEntry{"RAND01", WordRole::IntrinsicValue},
    WordEntry{"PAUSE", WordRole::IntrinsicValue},
    WordEntry{"GLOBAL", WordRole::Constant},
    WordEntry{"state", WordRole::Type},
    WordEntry{"number", WordRole::Type},
    WordEntry{"duration", WordRole::Type},
    WordEntry{"meter", WordRole::Type},
    WordEntry{"every", WordRole::Keyword},
    WordEntry{"move", WordRole::Transition},
    WordEntry{"wheel", WordRole::Transition},
    WordEntry{"horizontalwheel", WordRole::Transition},
    WordEntry{"tick", WordRole::Transition},
    WordEntry{"move_by", WordRole::Action},
    WordEntry{"move_to", WordRole::Action},
    WordEntry{"scroll", WordRole::Action},
    WordEntry{"scroll_horizontal", WordRole::Action},
    WordEntry{"restart", WordRole::Action},
    WordEntry{"exit", WordRole::Keyword},
    WordEntry{"pause", WordRole::Keyword},
    WordEntry{"when", WordRole::Keyword},
    WordEntry{"on", WordRole::Constant},
    WordEntry{"off", WordRole::Constant},
    WordEntry{"held", WordRole::Constant},
    WordEntry{"idle", WordRole::Constant},
    WordEntry{"toggle", WordRole::Action},
    WordEntry{"down", WordRole::Transition},
    WordEntry{"repeat", WordRole::Keyword},
    WordEntry{"again", WordRole::Transition},
    WordEntry{"up", WordRole::Transition},
    WordEntry{"and"},
    WordEntry{"or"},
    WordEntry{"not"},
    WordEntry{"press", WordRole::Action},
    WordEntry{"release", WordRole::Action},
    WordEntry{"tap", WordRole::Action},
    WordEntry{"wait", WordRole::Action},
    WordEntry{"gap", WordRole::Action},
    WordEntry{"set", WordRole::Action},
    WordEntry{"append", WordRole::Action},
    WordEntry{"pop", WordRole::Action},
    WordEntry{"clear", WordRole::Action},
    WordEntry{"exec", WordRole::Action},
    WordEntry{"if", WordRole::Keyword},
    WordEntry{"then", WordRole::Keyword},
    WordEntry{"else", WordRole::Keyword},
    WordEntry{"end", WordRole::Keyword},
    WordEntry{"do", WordRole::Keyword},
    WordEntry{"while", WordRole::Keyword},
    WordEntry{"HID.Usage", WordRole::RawControl},
    WordEntry{"Windows.VirtualKey", WordRole::RawControl},
    WordEntry{"Windows.ScanCode", WordRole::RawControl},
    WordEntry{"Linux.Key", WordRole::RawControl},
    WordEntry{"MacOS.KeyCode", WordRole::RawControl},
    WordEntry{"E0", WordRole::ScanPrefix},
    WordEntry{"E1", WordRole::ScanPrefix},
};

[[nodiscard]] constexpr const WordEntry* FindWordEntry(
    std::string_view word) noexcept
{
    for (const WordEntry& entry : kWordCatalog) {
        if (entry.text == word) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr WordRole LookupWordRole(std::string_view word) noexcept
{
    const WordEntry* entry = FindWordEntry(word);
    return entry == nullptr ? WordRole::None : entry->role;
}

[[nodiscard]] constexpr bool IsReservedLanguageWord(
    std::string_view word) noexcept
{
    return FindWordEntry(word) != nullptr;
}

} // namespace inputweaver::language
