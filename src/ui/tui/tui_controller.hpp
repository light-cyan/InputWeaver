#pragma once

#include "app/application.hpp"
#include "support/canvas.hpp"
#include "support/color_scheme.hpp"
#include "support/interaction.hpp"
#include "support/source_editor.hpp"
#include "support/source_highlighter.hpp"
#include "tui_input.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace inputweaver::ui::tui {

enum class Page : std::uint8_t {
    Console,
    Program,
    Debug,
};

class TuiController final {
public:
    TuiController(app::Application& application, ColorScheme colors);

    void Tick();
    void Handle(const KeyEvent& event);
    [[nodiscard]] bool RequestExit();
    [[nodiscard]] Canvas Render(std::size_t width, std::size_t height);
    [[nodiscard]] std::optional<std::string> TakeClipboardText();
    [[nodiscard]] bool TakeBackgroundRequest() noexcept;

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] Page CurrentPage() const noexcept;

private:
    enum class ProgramRegion : std::uint8_t {
        List,
        Information,
        Source,
    };

    enum class DebugRegion : std::uint8_t {
        Events,
        State,
        Executions,
    };

    enum class RegionInteraction : std::uint8_t {
        Selecting,
        Active,
    };

    enum class ProgramLayout : std::uint8_t {
        Split,
        DocumentFullscreen,
    };

    enum class SourceMode : std::uint8_t {
        Browse,
        Edit,
    };

    enum class Mode : std::uint8_t {
        None,
        AddSelect,
        AddPath,
        NewName,
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
    [[nodiscard]] bool SourceEditing() const noexcept;
    [[nodiscard]] bool DocumentFullscreen() const noexcept;
    [[nodiscard]] const app::ExecutorInfo* ExecutorFor(
        app::ProgramEntryId id) const;
    void RefreshSnapshot();
    void SelectIndex(std::size_t index);
    void ActivateSourceInSplitView() noexcept;
    void ReloadDocument();
    void ReloadDump();
    [[nodiscard]] bool FlushSource();
    void ValidateSource();
    void SourceChanged() noexcept;
    void HandleSourceEditor(const KeyEvent& event);
    void StartSelectedProgram();
    void StartPendingDebugProgram();
    void StopSelectedProgram();
    void ToggleDump();
    void ResetNextRun() noexcept;
    void ResetEditorCursorBlink() noexcept;
    void HandleModal(const KeyEvent& event);
    void HandleConsole(const KeyEvent& event);
    void HandleProgramPage(const KeyEvent& event);
    void HandleDebug(const KeyEvent& event);
    [[nodiscard]] bool HandlePageNavigation(const KeyEvent& event) noexcept;
    void SelectProgramRegion(Key key) noexcept;
    void SelectDebugRegion(Key key) noexcept;
    void HandleViewport(Viewport& viewport, const KeyEvent& event);
    void BeginLineEdit(Mode mode, std::string_view initial = {});
    void FinishProgramAddition();
    void SubmitLineEdit();
    void SubmitConflictChoice();
    void SubmitTargetChoice();
    void SubmitLoggingChoice();
    void CloseMode() noexcept;
    void ShowOperationError(const app::OperationResult& result);
    [[nodiscard]] bool RequireSuccess(const app::OperationResult& result);
    void SwitchPage(Page page) noexcept;

    app::Application& application_;
    ColorScheme colors_{};
    app::ApplicationSnapshot snapshot_{};
    Page page_{Page::Program};
    ProgramRegion programRegion_{ProgramRegion::List};
    DebugRegion debugRegion_{DebugRegion::Events};
    RegionInteraction programInteraction_{RegionInteraction::Selecting};
    RegionInteraction debugInteraction_{RegionInteraction::Selecting};
    ProgramLayout programLayout_{ProgramLayout::Split};
    SourceMode sourceMode_{SourceMode::Browse};
    Mode mode_{Mode::None};
    std::size_t selectedIndex_{};
    std::size_t informationField_{};
    app::NextRunOptions nextRun_{};
    enum class DocumentView : std::uint8_t { Source, Dump };
    struct PendingDebugRun final {
        app::ProgramEntryId id{app::kInvalidProgramEntryId};
        app::NextRunOptions options{};
        std::chrono::steady_clock::time_point due{};
    };
    DocumentView documentView_{DocumentView::Source};
    SourceEditor sourceEditor_;
    SourceHighlightDocument sourceHighlights_;
    std::vector<app::SourceDiagnostic> sourceDiagnostics_;
    app::ProgramEntryId loadedSourceId_{app::kInvalidProgramEntryId};
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
    Viewport stateViewport_;
    Viewport executionsViewport_;
    std::chrono::steady_clock::time_point validationDue_{};
    std::chrono::steady_clock::time_point cursorVisibleSince_{
        std::chrono::steady_clock::now()};
    bool validationPending_{};
    bool sourceDirty_{};
    std::optional<PendingDebugRun> pendingDebugRun_;
    std::optional<std::string> clipboardText_;
    bool backgroundRequested_{};
    bool running_{true};
};

} // namespace inputweaver::ui::tui
