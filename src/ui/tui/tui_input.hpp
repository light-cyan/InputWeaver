#pragma once

#include <cstdint>

namespace inputweaver::ui::tui {

enum class Key : std::uint8_t {
    Character,
    Enter,
    Escape,
    Tab,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End,
    Backspace,
    Delete,
    Copy,
    Cut,
    Undo,
    Redo,
};

enum class KeyEventSource : std::uint8_t {
    Keyboard,
    Paste,
    Drop,
};

struct KeyEvent final {
    Key key{Key::Character};
    char32_t character{};
    bool shift{};
    KeyEventSource source{KeyEventSource::Keyboard};
};

} // namespace inputweaver::ui::tui
