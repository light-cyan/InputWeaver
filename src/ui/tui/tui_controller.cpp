#include "tui_controller.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace inputweaver::ui::tui {
namespace {

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

} // namespace

TuiController::TuiController(
    app::Application& application,
    ColorScheme colors)
    : application_(application),
      colors_(colors),
      snapshot_(application.ReadSnapshot())
{
    ReloadDump();
}

void TuiController::Tick()
{
    application_.Tick();
    const app::ApplicationAttention attention = application_.ConsumeAttention();
    RefreshSnapshot();
    if (attention == app::ApplicationAttention::Console) {
        CloseMode();
        SwitchPage(Page::Console);
        consoleViewport_.End();
    } else if (mode_ == Mode::None
        && attention == app::ApplicationAttention::Debug) {
        SwitchPage(Page::Debug);
    }
}

void TuiController::Handle(const KeyEvent& event)
{
    statusMessage_.clear();
    if (mode_ != Mode::None) {
        HandleModal(event);
        return;
    }
    if (page_ == Page::Console) {
        HandleConsole(event);
    } else if (page_ == Page::Programs) {
        HandlePrograms(event);
    } else {
        HandleDebug(event);
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
        dumpText_.clear();
        return;
    }
    const auto found = std::find_if(
        snapshot_.programs.begin(),
        snapshot_.programs.end(),
        [selected](const app::ProgramEntry& program) {
            return program.id == selected;
        });
    const std::size_t newIndex = found == snapshot_.programs.end()
        ? (std::min)(selectedIndex_, snapshot_.programs.size() - 1U)
        : static_cast<std::size_t>(found - snapshot_.programs.begin());
    if (newIndex != selectedIndex_ || selected == app::kInvalidProgramEntryId) {
        selectedIndex_ = newIndex;
        ResetNextRun();
        ReloadDump();
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
        ReloadDump();
    }
}

void TuiController::ReloadDump()
{
    const app::ProgramEntry* program = SelectedProgram();
    dumpText_ = program == nullptr ? "" : application_.ReadDump(program->id);
    dumpViewport_.Home();
}

void TuiController::ResetNextRun() noexcept
{
    nextRun_ = {};
}

void TuiController::HandleModal(const KeyEvent& event)
{
    if (event.key == Key::Escape) {
        if (mode_ == Mode::ExecutableInput) {
            mode_ = Mode::TargetSelect;
        } else if (mode_ == Mode::ConflictRename) {
            mode_ = Mode::ConflictSelect;
        } else if (mode_ == Mode::Move) {
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
        } else {
            CloseMode();
        }
        return;
    }
    if (mode_ == Mode::DeleteConfirm) {
        if (event.key == Key::Enter) {
            const app::ProgramEntry* program = SelectedProgram();
            if (program != nullptr) {
                const app::OperationResult result =
                    application_.DeleteProgram(program->id);
                ShowOperationError(result);
                if (result.succeeded) {
                    CloseMode();
                    RefreshSnapshot();
                    ReloadDump();
                }
            }
        }
        return;
    }
    if (mode_ == Mode::Move) {
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
    }
    if (mode_ == Mode::ConflictSelect
        || mode_ == Mode::TargetSelect
        || mode_ == Mode::LoggingSelect) {
        if (event.key == Key::Up) {
            choice_ = choice_ == 0U ? 2U : choice_ - 1U;
        } else if (event.key == Key::Down) {
            choice_ = (choice_ + 1U) % 3U;
        } else if (event.key == Key::Enter) {
            if (mode_ == Mode::ConflictSelect) {
                SubmitConflictChoice();
            } else if (mode_ == Mode::TargetSelect) {
                SubmitTargetChoice();
            } else {
                SubmitLoggingChoice();
            }
        }
        return;
    }

    if (event.key == Key::Left) {
        editor_.Left();
    } else if (event.key == Key::Right) {
        editor_.Right();
    } else if (event.key == Key::Home) {
        editor_.Home();
    } else if (event.key == Key::End) {
        editor_.End();
    } else if (event.key == Key::Backspace) {
        editor_.Backspace();
    } else if (event.key == Key::Delete) {
        editor_.Delete();
    } else if (event.key == Key::Enter) {
        SubmitLineEdit();
    } else if (event.key == Key::Character) {
        editor_.Insert(event.character);
    }
}

void TuiController::HandleConsole(const KeyEvent& event)
{
    if (IsCharacter(event, U'q')) {
        SwitchPage(Page::Programs);
        return;
    }
    HandleViewport(consoleViewport_, event);
    if (event.key == Key::Right) {
        SwitchPage(Page::Programs);
    }
}

void TuiController::HandlePrograms(const KeyEvent& event)
{
    if (IsCharacter(event, U'q')) {
        if (programsFocus_ != ProgramsFocus::Programs) {
            programsFocus_ = ProgramsFocus::Programs;
        } else {
            running_ = false;
            application_.Shutdown();
        }
        return;
    }
    if (event.key == Key::Left) {
        SwitchPage(Page::Console);
        return;
    }
    if (event.key == Key::Right) {
        SwitchPage(Page::Debug);
        return;
    }
    if (event.key == Key::Tab) {
        programsFocus_ = programsFocus_ == ProgramsFocus::Programs
            ? ProgramsFocus::Information
            : programsFocus_ == ProgramsFocus::Information
                ? ProgramsFocus::Dump
                : ProgramsFocus::Programs;
        return;
    }
    if (event.key == Key::Escape) {
        programsFocus_ = ProgramsFocus::Programs;
        return;
    }
    if (programsFocus_ == ProgramsFocus::Dump) {
        HandleViewport(dumpViewport_, event);
        return;
    }
    if (programsFocus_ == ProgramsFocus::Information) {
        if (event.key == Key::Up || event.key == Key::Down) {
            informationField_ = informationField_ == 0U ? 1U : 0U;
        } else if (event.key == Key::Enter && SelectedProgram() != nullptr) {
            if (informationField_ == 0U) {
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
    }

    if (event.key == Key::Up && selectedIndex_ > 0U) {
        SelectIndex(selectedIndex_ - 1U);
    } else if (event.key == Key::Down
        && selectedIndex_ + 1U < DisplayProgramCount()) {
        SelectIndex(selectedIndex_ + 1U);
    } else if (event.key == Key::Enter) {
        programsFocus_ = ProgramsFocus::Information;
    } else if (IsCharacter(event, U'a')) {
        BeginLineEdit(Mode::AddPath);
    } else if (IsCharacter(event, U'd') && SelectedProgram() != nullptr) {
        mode_ = Mode::DeleteConfirm;
    } else if (IsCharacter(event, U'r') && SelectedProgram() != nullptr) {
        BeginLineEdit(Mode::Rename, SelectedProgram()->displayName);
    } else if (IsCharacter(event, U'm') && SelectedProgram() != nullptr) {
        moveOrder_.clear();
        for (const app::ProgramEntry& program : snapshot_.programs) {
            moveOrder_.push_back(program.id);
        }
        mode_ = Mode::Move;
    } else if (IsCharacter(event, U't')) {
        nextRun_.debug = !nextRun_.debug;
    } else if (IsCharacter(event, U's')) {
        nextRun_.dryRun = !nextRun_.dryRun;
    } else if (IsCharacter(event, U'p')) {
        nextRun_.allowExec = !nextRun_.allowExec;
    } else if (IsCharacter(event, U'x') && SelectedProgram() != nullptr) {
        ShowOperationError(application_.StopProgram(SelectedProgram()->id));
    } else if (event.key == Key::Character && event.character == U' '
        && SelectedProgram() != nullptr) {
        const app::OperationResult result = application_.StartProgram(
            SelectedProgram()->id,
            nextRun_);
        ShowOperationError(result);
        if (result.succeeded) {
            ResetNextRun();
        }
    }
}

void TuiController::HandleDebug(const KeyEvent& event)
{
    if (IsCharacter(event, U'q')) {
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
    if (event.key == Key::Up) {
        viewport.LineUp();
    } else if (event.key == Key::Down) {
        viewport.LineDown();
    } else if (event.key == Key::PageUp) {
        viewport.PageUp();
    } else if (event.key == Key::PageDown) {
        viewport.PageDown();
    } else if (event.key == Key::Home) {
        viewport.Home();
    } else if (event.key == Key::End) {
        viewport.End();
    }
}

void TuiController::BeginLineEdit(Mode mode, std::string_view initial)
{
    mode_ = mode;
    editor_.Set(initial);
}

void TuiController::SubmitLineEdit()
{
    const std::string text = editor_.Text();
    if (mode_ == Mode::AddPath) {
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
            CloseMode();
            RefreshSnapshot();
            SelectIndex(snapshot_.programs.empty()
                    ? 0U
                    : snapshot_.programs.size() - 1U);
        }
    } else if (mode_ == Mode::Rename) {
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
    } else if (mode_ == Mode::ConflictRename) {
        const app::OperationResult result = application_.ImportProgram(
            pendingSource_,
            text);
        ShowOperationError(result);
        if (result.succeeded) {
            CloseMode();
            RefreshSnapshot();
            SelectIndex(snapshot_.programs.size() - 1U);
        }
    } else if (mode_ == Mode::ExecutableInput) {
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

void TuiController::SwitchPage(Page page) noexcept
{
    page_ = page;
}

} // namespace inputweaver::ui::tui
