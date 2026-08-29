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

struct KeyEvent final {
    Key key{Key::Character};
    char32_t character{};
    bool shift{};
};

} // namespace inputweaver::ui::tui
