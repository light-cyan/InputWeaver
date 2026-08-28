#include "tui_controller.hpp"

#include <algorithm>
#include <utility>

namespace inputweaver::ui::tui {
namespace {

inline constexpr auto kValidationDelay = std::chrono::milliseconds{400};
inline constexpr auto kDebugLaunchDelay = std::chrono::milliseconds{500};

[[nodiscard]] char32_t LowerAscii(char32_t character) noexcept
{
    return character >= U'A' && character <= U'Z'
        ? character + (U'a' - U'A')
        : character;
}

[[nodiscard]] bool IsCharacter(
    const KeyEvent& event,
    char32_t expected) noexcept
{
    return event.key == Key::Character
        && LowerAscii(event.character) == LowerAscii(expected);
}

[[nodiscard]] bool IsMovementKey(Key key) noexcept
{
    switch (key) {
    case Key::Up:
    case Key::Down:
    case Key::Left:
    case Key::Right:
    case Key::PageUp:
    case Key::PageDown:
    case Key::Home:
    case Key::End:
        return true;
    default:
        return false;
    }
}

} // namespace

TuiController::TuiController(
    app::Application& application,
    ColorScheme colors)
    : application_(application),
      colors_(colors),
      snapshot_(application.ReadSnapshot())
{
    ReloadDocument();
}

void TuiController::Tick()
{
    StartPendingDebugProgram();
    application_.Tick();
    const app::ApplicationAttention attention = application_.ConsumeAttention();
    RefreshSnapshot();
    if (attention == app::ApplicationAttention::Console) {
        CloseMode();
        if (SourceEditing() || DocumentFullscreen()) {
            programsState_ = ProgramsState::Source;
        }
        SwitchPage(Page::Console);
        consoleViewport_.End();
        return;
    }
    if (mode_ == Mode::None
        && attention == app::ApplicationAttention::Debug) {
        SwitchPage(Page::Debug);
    }
    if (validationPending_
        && std::chrono::steady_clock::now() >= validationDue_) {
        if (FlushSource()) {
            ValidateSource();
        }
    }
}

void TuiController::Handle(const KeyEvent& event)
{
    statusMessage_.clear();
    if (mode_ != Mode::None) {
        HandleModal(event);
        return;
    }
    switch (page_) {
    case Page::Console:
        HandleConsole(event);
        break;
    case Page::Programs:
        HandlePrograms(event);
        break;
    case Page::Debug:
        HandleDebug(event);
        break;
    }
}

bool TuiController::Running() const noexcept
{
    return running_;
}

Page TuiController::CurrentPage() const noexcept
{
    return page_;
}

std::optional<std::string> TuiController::TakeClipboardText()
{
    std::optional<std::string> text = std::move(clipboardText_);
    clipboardText_.reset();
    return text;
}

const app::ProgramEntry* TuiController::SelectedProgram() const
{
    return DisplayProgram(selectedIndex_);
}

const app::ProgramEntry* TuiController::DisplayProgram(std::size_t index) const
{
    if (mode_ == Mode::Move && index < moveOrder_.size()) {
        const app::ProgramEntryId id = moveOrder_[index];
        const auto found = std::find_if(
            snapshot_.programs.begin(),
            snapshot_.programs.end(),
            [id](const app::ProgramEntry& program) { return program.id == id; });
        return found == snapshot_.programs.end() ? nullptr : &*found;
    }
    return index < snapshot_.programs.size()
        ? &snapshot_.programs[index]
        : nullptr;
}

std::size_t TuiController::DisplayProgramCount() const noexcept
{
    return mode_ == Mode::Move
        ? moveOrder_.size()
        : snapshot_.programs.size();
}

bool TuiController::SourceEditing() const noexcept
{
    return programsState_ == ProgramsState::Editing
        || programsState_ == ProgramsState::FullscreenEditing;
}

bool TuiController::DocumentFullscreen() const noexcept
{
    return programsState_ == ProgramsState::Fullscreen
        || programsState_ == ProgramsState::FullscreenEditing;
}

const app::ExecutorInfo* TuiController::ExecutorFor(
    app::ProgramEntryId id) const
{
    const auto found = std::find_if(
        snapshot_.executors.begin(),
        snapshot_.executors.end(),
        [id](const app::ExecutorInfo& executor) {
            return executor.programId == id;
        });
    return found == snapshot_.executors.end() ? nullptr : &*found;
}

void TuiController::RefreshSnapshot()
{
    const app::ProgramEntryId selected = SelectedProgram() == nullptr
        ? app::kInvalidProgramEntryId
        : SelectedProgram()->id;
    snapshot_ = application_.ReadSnapshot();
    if (mode_ == Mode::Move) {
        return;
    }
    if (snapshot_.programs.empty()) {
        selectedIndex_ = 0U;
        loadedSourceId_ = app::kInvalidProgramEntryId;
        sourceEditor_.Set({});
        sourceDiagnostics_.clear();
        dumpText_.clear();
        sourceDirty_ = false;
        validationPending_ = false;
        return;
    }
    const auto found = std::find_if(
        snapshot_.programs.begin(),
        snapshot_.programs.end(),
        [selected](const app::ProgramEntry& program) {
            return program.id == selected;
        });
    selectedIndex_ = found == snapshot_.programs.end()
        ? (std::min)(selectedIndex_, snapshot_.programs.size() - 1U)
        : static_cast<std::size_t>(found - snapshot_.programs.begin());
    const app::ProgramEntry* program = SelectedProgram();
    if (program != nullptr && program->id != loadedSourceId_) {
        ResetNextRun();
        ReloadDocument();
    }
}

void TuiController::SelectIndex(std::size_t index)
{
    if (DisplayProgramCount() == 0U) {
        selectedIndex_ = 0U;
        return;
    }
    const std::size_t selected = (std::min)(index, DisplayProgramCount() - 1U);
    if (selected != selectedIndex_) {
        selectedIndex_ = selected;
        ResetNextRun();
        ReloadDocument();
    }
}

void TuiController::ReloadDocument()
{
    const app::ProgramEntry* program = SelectedProgram();
    loadedSourceId_ = program == nullptr
        ? app::kInvalidProgramEntryId
        : program->id;
    sourceDirty_ = false;
    if (DocumentFullscreen() || SourceEditing()) {
        programsState_ = ProgramsState::Source;
    }
    sourceDiagnostics_.clear();
    documentView_ = DocumentView::Source;
    if (program == nullptr) {
        sourceEditor_.Set({});
        dumpText_.clear();
        validationPending_ = false;
        return;
    }
    const app::SourceReadResult source = application_.ReadSource(program->id);
    if (source.succeeded) {
        sourceEditor_.Set(source.text);
        validationPending_ = true;
        validationDue_ = std::chrono::steady_clock::now() + kValidationDelay;
    } else {
        sourceEditor_.Set({});
        statusMessage_ = source.error;
        validationPending_ = false;
    }
    ReloadDump();
}

void TuiController::ReloadDump()
{
    const app::ProgramEntry* program = SelectedProgram();
    dumpText_ = program == nullptr ? "" : application_.ReadDump(program->id);
    dumpViewport_.Home();
}

bool TuiController::FlushSource()
{
    if (!sourceDirty_) {
        return true;
    }
    if (loadedSourceId_ == app::kInvalidProgramEntryId) {
        return false;
    }
    const app::OperationResult result = application_.SaveSource(
        loadedSourceId_,
        sourceEditor_.Text());
    if (!RequireSuccess(result)) {
        return false;
    }
    sourceDirty_ = false;
    validationPending_ = true;
    return true;
}

void TuiController::ValidateSource()
{
    validationPending_ = false;
    if (loadedSourceId_ == app::kInvalidProgramEntryId) {
        sourceDiagnostics_.clear();
        return;
    }
    app::SourceValidationResult result = application_.ValidateProgram(
        loadedSourceId_);
    if (!result.completed) {
        sourceDiagnostics_.clear();
        statusMessage_ = std::move(result.error);
        return;
    }
    sourceDiagnostics_ = std::move(result.diagnostics);
}

void TuiController::SourceChanged() noexcept
{
    sourceDirty_ = true;
    sourceDiagnostics_.clear();
    validationPending_ = true;
    validationDue_ = std::chrono::steady_clock::now() + kValidationDelay;
}

void TuiController::ResetNextRun() noexcept
{
    nextRun_ = {};
}

void TuiController::ResetEditorCursorBlink() noexcept
{
    cursorVisibleSince_ = std::chrono::steady_clock::now();
}

void TuiController::HandleModal(const KeyEvent& event)
{
    if (event.key == Key::Escape) {
        switch (mode_) {
        case Mode::ExecutableInput:
            mode_ = Mode::TargetSelect;
            break;
        case Mode::ConflictRename:
            mode_ = Mode::ConflictSelect;
            break;
        case Mode::Move: {
            const app::ProgramEntryId selected = SelectedProgram() == nullptr
                ? app::kInvalidProgramEntryId
                : SelectedProgram()->id;
            CloseMode();
            const auto found = std::find_if(
                snapshot_.programs.begin(),
                snapshot_.programs.end(),
                [selected](const app::ProgramEntry& program) {
                    return program.id == selected;
                });
            if (found != snapshot_.programs.end()) {
                selectedIndex_ = static_cast<std::size_t>(
                    found - snapshot_.programs.begin());
            }
            break;
        }
        default:
            CloseMode();
            break;
        }
        return;
    }

    switch (mode_) {
    case Mode::DeleteConfirm:
        if (event.key == Key::Enter) {
            const app::ProgramEntry* program = SelectedProgram();
            if (program != nullptr && FlushSource()) {
                const app::OperationResult result =
                    application_.DeleteProgram(program->id);
                ShowOperationError(result);
                if (result.succeeded) {
                    CloseMode();
                    RefreshSnapshot();
                    ReloadDocument();
                }
            }
        }
        return;
    case Mode::Move:
        if (event.key == Key::Up && selectedIndex_ > 0U) {
            std::swap(moveOrder_[selectedIndex_], moveOrder_[selectedIndex_ - 1U]);
            --selectedIndex_;
        } else if (event.key == Key::Down
            && selectedIndex_ + 1U < moveOrder_.size()) {
            std::swap(moveOrder_[selectedIndex_], moveOrder_[selectedIndex_ + 1U]);
            ++selectedIndex_;
        } else if (event.key == Key::Enter) {
            const app::OperationResult result =
                application_.ReorderPrograms(moveOrder_);
            ShowOperationError(result);
            if (result.succeeded) {
                CloseMode();
                RefreshSnapshot();
            }
        }
        return;
    case Mode::AddSelect:
    case Mode::ConflictSelect:
        if (event.key == Key::Left) {
            choice_ = choice_ == 0U ? 2U : choice_ - 1U;
        } else if (event.key == Key::Right) {
            choice_ = (choice_ + 1U) % 3U;
        } else if (event.key == Key::Enter) {
            if (mode_ == Mode::AddSelect) {
                if (choice_ == 0U) {
                    BeginLineEdit(Mode::NewName, "Untitled");
                } else if (choice_ == 1U) {
                    BeginLineEdit(Mode::AddPath);
                } else {
                    CloseMode();
                }
            } else if (mode_ == Mode::ConflictSelect) {
                SubmitConflictChoice();
            }
        }
        return;
    case Mode::TargetSelect:
    case Mode::LoggingSelect:
        if (event.key == Key::Up) {
            choice_ = choice_ == 0U ? 2U : choice_ - 1U;
        } else if (event.key == Key::Down) {
            choice_ = (choice_ + 1U) % 3U;
        } else if (event.key == Key::Enter) {
            if (mode_ == Mode::TargetSelect) {
                SubmitTargetChoice();
            } else {
                SubmitLoggingChoice();
            }
        }
        return;
    case Mode::AddPath:
    case Mode::NewName:
    case Mode::Rename:
    case Mode::ExecutableInput:
    case Mode::ConflictRename:
        break;
    case Mode::None:
        return;
    }

    bool cursorActivity = true;
    switch (event.key) {
    case Key::Left:
        editor_.Left();
        break;
    case Key::Right:
        editor_.Right();
        break;
    case Key::Home:
        editor_.Home();
        break;
    case Key::End:
        editor_.End();
        break;
    case Key::Backspace:
        editor_.Backspace();
        break;
    case Key::Delete:
        editor_.Delete();
        break;
    case Key::Enter:
        cursorActivity = false;
        SubmitLineEdit();
        break;
    case Key::Character:
        editor_.Insert(event.character);
        break;
    default:
        cursorActivity = false;
        break;
    }
    if (cursorActivity) {
        ResetEditorCursorBlink();
    }
}

void TuiController::HandleConsole(const KeyEvent& event)
{
    if (event.key == Key::Escape) {
        SwitchPage(Page::Programs);
        return;
    }
    HandleViewport(consoleViewport_, event);
    if (event.key == Key::Right) {
        SwitchPage(Page::Programs);
    }
}

void TuiController::StartSelectedProgram()
{
    const app::ProgramEntry* program = SelectedProgram();
    if (program == nullptr || !FlushSource()) {
        return;
    }
    if (nextRun_.debug) {
        const app::ExecutorInfo* executor = ExecutorFor(program->id);
        if (executor == nullptr) {
            pendingDebugRun_ = PendingDebugRun{
                program->id,
                nextRun_,
                std::chrono::steady_clock::now() + kDebugLaunchDelay};
            ResetNextRun();
        }
        SwitchPage(Page::Debug);
        return;
    }
    const app::OperationResult result = application_.StartProgram(
        program->id,
        nextRun_);
    if (RequireSuccess(result)) {
        ResetNextRun();
        ReloadDump();
    }
}

void TuiController::StartPendingDebugProgram()
{
    if (!pendingDebugRun_.has_value()
        || std::chrono::steady_clock::now() < pendingDebugRun_->due) {
        return;
    }
    PendingDebugRun pending = *pendingDebugRun_;
    pendingDebugRun_.reset();
    const app::OperationResult result = application_.StartProgram(
        pending.id,
        pending.options);
    (void)RequireSuccess(result);
}

void TuiController::StopSelectedProgram()
{
    const app::ProgramEntry* program = SelectedProgram();
    if (program != nullptr) {
        if (pendingDebugRun_.has_value()
            && pendingDebugRun_->id == program->id) {
            pendingDebugRun_.reset();
        }
        const app::OperationResult result = application_.StopProgram(program->id);
        (void)RequireSuccess(result);
    }
}

void TuiController::ToggleDump()
{
    const app::ProgramEntry* program = SelectedProgram();
    if (program == nullptr) {
        return;
    }
    if (documentView_ == DocumentView::Dump) {
        documentView_ = DocumentView::Source;
        return;
    }
    if (!FlushSource()) {
        return;
    }
    ReloadDump();
    if (dumpText_.empty()) {
        const app::OperationResult result = application_.GenerateDump(program->id);
        if (!RequireSuccess(result)) {
            return;
        }
        ReloadDump();
    }
    documentView_ = DocumentView::Dump;
}

void TuiController::HandleSourceEditor(const KeyEvent& event)
{
    if (event.key == Key::Escape) {
        if (FlushSource()) {
            sourceEditor_.ClearSelection();
            programsState_ = programsState_ == ProgramsState::FullscreenEditing
                ? ProgramsState::Fullscreen
                : ProgramsState::Source;
        }
        return;
    }
    if (IsMovementKey(event.key)) {
        if (event.shift) {
            sourceEditor_.BeginSelection();
        } else {
            sourceEditor_.ClearSelection();
        }
    }
    bool changed = false;
    bool cursorActivity = true;
    switch (event.key) {
    case Key::Copy:
    case Key::Cut:
        if (sourceEditor_.HasSelection()) {
            clipboardText_ = sourceEditor_.SelectedText();
            changed = event.key == Key::Cut && sourceEditor_.Delete();
        }
        cursorActivity = event.key == Key::Cut;
        break;
    case Key::Undo:
        changed = sourceEditor_.Undo();
        break;
    case Key::Redo:
        changed = sourceEditor_.Redo();
        break;
    case Key::Left:
        sourceEditor_.Left();
        break;
    case Key::Right:
        sourceEditor_.Right();
        break;
    case Key::Up:
        sourceEditor_.Up();
        break;
    case Key::Down:
        sourceEditor_.Down();
        break;
    case Key::PageUp:
        sourceEditor_.PageUp();
        break;
    case Key::PageDown:
        sourceEditor_.PageDown();
        break;
    case Key::Home:
        sourceEditor_.Home();
        break;
    case Key::End:
        sourceEditor_.End();
        break;
    case Key::Backspace:
        changed = sourceEditor_.Backspace();
        break;
    case Key::Delete:
        changed = sourceEditor_.Delete();
        break;
    case Key::Enter:
        changed = sourceEditor_.NewLine();
        break;
    case Key::Tab:
        changed = sourceEditor_.InsertSpaces(4U);
        break;
    case Key::Character:
        changed = sourceEditor_.Insert(event.character);
        break;
    case Key::Escape:
        cursorActivity = false;
        break;
    }
    if (changed) {
        SourceChanged();
    }
    if (cursorActivity) {
        ResetEditorCursorBlink();
    }
}

void TuiController::HandlePrograms(const KeyEvent& event)
{
    if (SourceEditing()) {
        HandleSourceEditor(event);
        return;
    }
    if (event.key == Key::Escape) {
        if (programsState_ == ProgramsState::Fullscreen) {
            programsState_ = ProgramsState::Source;
        } else if (programsState_ == ProgramsState::Programs) {
            (void)FlushSource();
            pendingDebugRun_.reset();
            running_ = false;
            application_.Shutdown();
        } else {
            programsState_ = ProgramsState::Programs;
        }
        return;
    }
    if (IsCharacter(event, U't')) {
        nextRun_.debug = !nextRun_.debug;
        return;
    }
    if (IsCharacter(event, U's')) {
        nextRun_.dryRun = !nextRun_.dryRun;
        return;
    }
    if (IsCharacter(event, U'p')) {
        nextRun_.allowExec = !nextRun_.allowExec;
        return;
    }
    if (IsCharacter(event, U'e') && SelectedProgram() != nullptr) {
        documentView_ = DocumentView::Source;
        sourceEditor_.ClearSelection();
        programsState_ = DocumentFullscreen()
            ? ProgramsState::FullscreenEditing
            : ProgramsState::Editing;
        ResetEditorCursorBlink();
        return;
    }
    if (IsCharacter(event, U'z') && SelectedProgram() != nullptr) {
        programsState_ = DocumentFullscreen()
            ? ProgramsState::Source
            : ProgramsState::Fullscreen;
        return;
    }
    if (IsCharacter(event, U'v')) {
        ToggleDump();
        return;
    }
    if (event.key == Key::Character && event.character == U' ') {
        StartSelectedProgram();
        return;
    }
    if (IsCharacter(event, U'x')) {
        StopSelectedProgram();
        return;
    }

    if (!DocumentFullscreen() && event.key == Key::Left) {
        (void)FlushSource();
        SwitchPage(Page::Console);
        return;
    }
    if (!DocumentFullscreen() && event.key == Key::Right) {
        (void)FlushSource();
        SwitchPage(Page::Debug);
        return;
    }
    if (!DocumentFullscreen() && event.key == Key::Tab) {
        if (programsState_ == ProgramsState::Source) {
            (void)FlushSource();
        }
        programsState_ = programsState_ == ProgramsState::Programs
            ? ProgramsState::Information
            : programsState_ == ProgramsState::Information
                ? ProgramsState::Source
                : ProgramsState::Programs;
        return;
    }
    switch (programsState_) {
    case ProgramsState::Source:
    case ProgramsState::Fullscreen:
        if (documentView_ == DocumentView::Dump) {
            HandleViewport(dumpViewport_, event);
        } else if (event.key == Key::Left) {
            sourceEditor_.Left();
        } else if (event.key == Key::Right) {
            sourceEditor_.Right();
        } else if (event.key == Key::Up) {
            sourceEditor_.Up();
        } else if (event.key == Key::Down) {
            sourceEditor_.Down();
        } else if (event.key == Key::PageUp) {
            sourceEditor_.PageUp();
        } else if (event.key == Key::PageDown) {
            sourceEditor_.PageDown();
        } else if (event.key == Key::Home) {
            sourceEditor_.FirstLine();
        } else if (event.key == Key::End) {
            sourceEditor_.LastLine();
        }
        return;
    case ProgramsState::Information:
        if (event.key == Key::Up) {
            informationField_ = informationField_ == 0U
                ? 2U
                : informationField_ - 1U;
        } else if (event.key == Key::Down) {
            informationField_ = (informationField_ + 1U) % 3U;
        } else if (event.key == Key::Enter && SelectedProgram() != nullptr) {
            if (informationField_ == 0U) {
                BeginLineEdit(Mode::Rename, SelectedProgram()->displayName);
            } else if (informationField_ == 1U) {
                choice_ = static_cast<std::size_t>(
                    SelectedProgram()->configuration.target);
                mode_ = Mode::TargetSelect;
            } else {
                choice_ = static_cast<std::size_t>(
                    SelectedProgram()->configuration.logging);
                mode_ = Mode::LoggingSelect;
            }
        }
        return;
    case ProgramsState::Programs:
        break;
    case ProgramsState::Editing:
    case ProgramsState::FullscreenEditing:
        return;
    }

    if (event.key == Key::Up && selectedIndex_ > 0U) {
        if (FlushSource()) {
            SelectIndex(selectedIndex_ - 1U);
        }
    } else if (event.key == Key::Down
        && selectedIndex_ + 1U < DisplayProgramCount()) {
        if (FlushSource()) {
            SelectIndex(selectedIndex_ + 1U);
        }
    } else if (event.key == Key::Enter) {
        programsState_ = ProgramsState::Information;
    } else if (IsCharacter(event, U'a')) {
        mode_ = Mode::AddSelect;
        choice_ = 0U;
    } else if (IsCharacter(event, U'd') && SelectedProgram() != nullptr) {
        mode_ = Mode::DeleteConfirm;
    } else if (IsCharacter(event, U'm') && SelectedProgram() != nullptr) {
        moveOrder_.clear();
        for (const app::ProgramEntry& program : snapshot_.programs) {
            moveOrder_.push_back(program.id);
        }
        mode_ = Mode::Move;
    }
}

void TuiController::HandleDebug(const KeyEvent& event)
{
    if (event.key == Key::Escape) {
        SwitchPage(Page::Programs);
        return;
    }
    if (event.key == Key::Left) {
        SwitchPage(Page::Programs);
        return;
    }
    if (event.key == Key::Tab) {
        debugFocus_ = debugFocus_ == DebugFocus::Events
            ? DebugFocus::Pressed
            : debugFocus_ == DebugFocus::Pressed
                ? DebugFocus::Executions
                : DebugFocus::Events;
        return;
    }
    if (IsCharacter(event, U'c')) {
        const bool capturing = snapshot_.debugState != nullptr
            && snapshot_.debugState->captureRequested;
        ShowOperationError(
            capturing ? application_.StopCapture() : application_.StartCapture());
        return;
    }
    if (IsCharacter(event, U'x')
        && snapshot_.debugProgramId != app::kInvalidProgramEntryId) {
        ShowOperationError(application_.StopProgram(snapshot_.debugProgramId));
        return;
    }
    if (IsCharacter(event, U'x') && pendingDebugRun_.has_value()) {
        pendingDebugRun_.reset();
        return;
    }
    if (debugFocus_ == DebugFocus::Events) {
        HandleViewport(eventsViewport_, event);
    } else if (debugFocus_ == DebugFocus::Pressed) {
        HandleViewport(pressedViewport_, event);
    } else {
        HandleViewport(executionsViewport_, event);
    }
}

void TuiController::HandleViewport(
    Viewport& viewport,
    const KeyEvent& event)
{
    switch (event.key) {
    case Key::Up:
        viewport.LineUp();
        break;
    case Key::Down:
        viewport.LineDown();
        break;
    case Key::PageUp:
        viewport.PageUp();
        break;
    case Key::PageDown:
        viewport.PageDown();
        break;
    case Key::Home:
        viewport.Home();
        break;
    case Key::End:
        viewport.End();
        break;
    default:
        break;
    }
}

void TuiController::BeginLineEdit(Mode mode, std::string_view initial)
{
    mode_ = mode;
    editor_.Set(initial);
    ResetEditorCursorBlink();
}

void TuiController::FinishProgramAddition()
{
    CloseMode();
    RefreshSnapshot();
    SelectIndex(snapshot_.programs.size() - 1U);
    programsState_ = ProgramsState::Source;
}

void TuiController::SubmitLineEdit()
{
    const std::string text = editor_.Text();
    switch (mode_) {
    case Mode::NewName: {
        const app::OperationResult result = application_.CreateProgram(text);
        ShowOperationError(result);
        if (result.succeeded) {
            FinishProgramAddition();
        }
        break;
    }
    case Mode::AddPath: {
        const app::ImportPreparation prepared = application_.PrepareImport(text);
        if (prepared.status == app::ImportPreparationStatus::Invalid) {
            statusMessage_ = prepared.error;
            return;
        }
        pendingSource_ = prepared.sourcePath;
        pendingName_ = prepared.defaultName;
        conflictId_ = prepared.conflictId;
        if (prepared.status == app::ImportPreparationStatus::NameConflict) {
            mode_ = Mode::ConflictSelect;
            choice_ = 0U;
            return;
        }
        const app::OperationResult result = application_.ImportProgram(
            pendingSource_,
            pendingName_);
        ShowOperationError(result);
        if (result.succeeded) {
            FinishProgramAddition();
        }
        break;
    }
    case Mode::Rename: {
        const app::ProgramEntry* program = SelectedProgram();
        if (program == nullptr) {
            CloseMode();
            return;
        }
        const app::OperationResult result =
            application_.RenameProgram(program->id, text);
        ShowOperationError(result);
        if (result.succeeded) {
            CloseMode();
            RefreshSnapshot();
        }
        break;
    }
    case Mode::ConflictRename: {
        const app::OperationResult result = application_.ImportProgram(
            pendingSource_,
            text);
        ShowOperationError(result);
        if (result.succeeded) {
            FinishProgramAddition();
        }
        break;
    }
    case Mode::ExecutableInput: {
        const app::ProgramEntry* program = SelectedProgram();
        if (program == nullptr) {
            CloseMode();
            return;
        }
        app::RunConfiguration configuration = program->configuration;
        configuration.target = app::TargetMode::Executable;
        configuration.executableSelector = text;
        const app::OperationResult result = application_.UpdateConfiguration(
            program->id,
            configuration);
        ShowOperationError(result);
        if (result.succeeded) {
            CloseMode();
            RefreshSnapshot();
        }
        break;
    }
    default:
        break;
    }
}

void TuiController::SubmitConflictChoice()
{
    if (choice_ == 2U) {
        CloseMode();
        return;
    }
    if (choice_ == 1U) {
        BeginLineEdit(Mode::ConflictRename, pendingName_);
        return;
    }
    const app::OperationResult result = application_.ImportProgram(
        pendingSource_,
        pendingName_,
        conflictId_);
    ShowOperationError(result);
    if (result.succeeded) {
        CloseMode();
        RefreshSnapshot();
        ReloadDocument();
    }
}

void TuiController::SubmitTargetChoice()
{
    const app::ProgramEntry* program = SelectedProgram();
    if (program == nullptr) {
        CloseMode();
        return;
    }
    if (choice_ == static_cast<std::size_t>(app::TargetMode::Executable)) {
        BeginLineEdit(
            Mode::ExecutableInput,
            program->configuration.executableSelector);
        return;
    }
    app::RunConfiguration configuration = program->configuration;
    configuration.target = static_cast<app::TargetMode>(choice_);
    configuration.executableSelector.clear();
    const app::OperationResult result = application_.UpdateConfiguration(
        program->id,
        configuration);
    ShowOperationError(result);
    if (result.succeeded) {
        CloseMode();
        RefreshSnapshot();
    }
}

void TuiController::SubmitLoggingChoice()
{
    const app::ProgramEntry* program = SelectedProgram();
    if (program == nullptr) {
        CloseMode();
        return;
    }
    app::RunConfiguration configuration = program->configuration;
    configuration.logging = static_cast<app::LoggingMode>(choice_);
    const app::OperationResult result = application_.UpdateConfiguration(
        program->id,
        configuration);
    ShowOperationError(result);
    if (result.succeeded) {
        CloseMode();
        RefreshSnapshot();
    }
}

void TuiController::CloseMode() noexcept
{
    mode_ = Mode::None;
    editor_.Clear();
    moveOrder_.clear();
    pendingSource_.clear();
    pendingName_.clear();
    conflictId_ = app::kInvalidProgramEntryId;
    choice_ = 0U;
}

void TuiController::ShowOperationError(const app::OperationResult& result)
{
    if (!result.succeeded) {
        statusMessage_ = result.error;
    }
}

bool TuiController::RequireSuccess(const app::OperationResult& result)
{
    ShowOperationError(result);
    if (result.succeeded) {
        return true;
    }
    SwitchPage(Page::Console);
    consoleViewport_.End();
    return false;
}

void TuiController::SwitchPage(Page page) noexcept
{
    page_ = page;
}

} // namespace inputweaver::ui::tui
