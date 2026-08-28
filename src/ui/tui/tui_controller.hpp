#pragma once

#include "app/application.hpp"
#include "support/canvas.hpp"
#include "support/color_scheme.hpp"
#include "support/interaction.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
};

struct KeyEvent final {
    Key key{Key::Character};
    char32_t character{};
};

enum class Page : std::uint8_t {
    Console,
    Programs,
    Debug,
};

enum class ProgramsFocus : std::uint8_t {
    Programs,
    Information,
    Dump,
};

enum class DebugFocus : std::uint8_t {
    Events,
    Pressed,
    Executions,
};

class TuiController final {
public:
    TuiController(app::Application& application, ColorScheme colors);

    void Tick();
    void Handle(const KeyEvent& event);
    [[nodiscard]] Canvas Render(std::size_t width, std::size_t height);

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] Page CurrentPage() const noexcept;

private:
    enum class Mode : std::uint8_t {
        None,
        AddPath,
        Rename,
        DeleteConfirm,
        Move,
        TargetSelect,
        ExecutableInput,
        LoggingSelect,
        ConflictSelect,
        ConflictRename,
    };

    [[nodiscard]] const app::ProgramEntry* SelectedProgram() const;
    [[nodiscard]] const app::ProgramEntry* DisplayProgram(
        std::size_t index) const;
    [[nodiscard]] std::size_t DisplayProgramCount() const noexcept;
    [[nodiscard]] const app::ExecutorInfo* ExecutorFor(
        app::ProgramEntryId id) const;
    void RefreshSnapshot();
    void SelectIndex(std::size_t index);
    void ReloadDump();
    void ResetNextRun() noexcept;
    void HandleModal(const KeyEvent& event);
    void HandleConsole(const KeyEvent& event);
    void HandlePrograms(const KeyEvent& event);
    void HandleDebug(const KeyEvent& event);
    void HandleViewport(Viewport& viewport, const KeyEvent& event);
    void BeginLineEdit(Mode mode, std::string_view initial = {});
    void SubmitLineEdit();
    void SubmitConflictChoice();
    void SubmitTargetChoice();
    void SubmitLoggingChoice();
    void CloseMode() noexcept;
    void ShowOperationError(const app::OperationResult& result);
    void SwitchPage(Page page) noexcept;

    app::Application& application_;
    ColorScheme colors_{};
    app::ApplicationSnapshot snapshot_{};
    Page page_{Page::Programs};
    ProgramsFocus programsFocus_{ProgramsFocus::Programs};
    DebugFocus debugFocus_{DebugFocus::Events};
    Mode mode_{Mode::None};
    std::size_t selectedIndex_{};
    std::size_t informationField_{};
    app::NextRunOptions nextRun_{};
    std::string dumpText_;
    std::string statusMessage_;
    LineEditor editor_;
    std::vector<app::ProgramEntryId> moveOrder_;
    std::string pendingSource_;
    std::string pendingName_;
    app::ProgramEntryId conflictId_{app::kInvalidProgramEntryId};
    std::size_t choice_{};
    Viewport consoleViewport_;
    Viewport programsViewport_;
    Viewport dumpViewport_;
    Viewport eventsViewport_;
    Viewport pressedViewport_;
    Viewport executionsViewport_;
    bool running_{true};
};

} // namespace inputweaver::ui::tui
