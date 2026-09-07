# Phase 15: TUI Spatial Navigation

## Status

This document defines the approved TUI navigation model and the state refactor required to implement it. Implementation begins after review of this plan.

## Objective

The TUI will distinguish top-level page navigation, spatial region selection, region interaction, and nested editing or modal interaction. Each input has one meaning at its current interaction level, and page-local state remains intact while another page is visible.

## Interaction hierarchy

The interaction hierarchy is:

```text
Top-level page
    Region selection
        Region interaction
            Modal, confirmation, option selection, or text editing
```

`[` and `]` move between top-level pages. Arrow keys select a region while a multi-region page is in region-selection mode. `Enter` activates the selected region. `Esc` leaves the current interaction level.

Console contains one content region and therefore operates directly without a region-selection level.

## State model

The existing Programs state combines region identity, source editing, and document fullscreen presentation. Spatial region selection adds another independent dimension, so the combined state will be replaced by orthogonal page-local state.

```cpp
enum class ProgramsRegion : std::uint8_t {
    Programs,
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

enum class ProgramsLayout : std::uint8_t {
    Split,
    DocumentFullscreen,
};

enum class SourceMode : std::uint8_t {
    Browse,
    Edit,
};
```

The controller will own one selected region and one interaction state for Programs, one selected region and one interaction state for Debug, one Programs layout, and one source mode.

`ProgramsLayout::Split` means the ordinary Programs presentation in which PROGRAMS occupies the left column, PROGRAM INFORMATION occupies the upper-right area, SOURCE or COMPILED DUMP occupies the lower-right area, and NEXT RUN remains below them. The name belongs to `ProgramsLayout`, rather than `DocumentLayout`, because the choice changes the composition of the entire Programs page.

The layout remains a separate state because fullscreen presentation is independent of the selected top-level page and source editing. A source document can be browsed or edited in either the split Programs layout or document fullscreen, and either presentation must survive a temporary switch to Console or Debug. Combining these dimensions would require distinct enum values for split browsing, split editing, fullscreen browsing, fullscreen editing, and every region-selection state.

The state model maintains these invariants:

- Source editing implies that Source is the selected active Programs region.
- Document fullscreen implies that Source is the selected active Programs region.
- A modal belongs to an active region and captures input until it closes.
- Changing the visible top-level page changes only `page_`; it does not reset page-local region, interaction, viewport, layout, source, or field state.

## Top-level pages

The page order is fixed and non-cyclic:

```text
Console <-> Programs <-> Debug
```

The page transitions are:

| Current page | Key | Destination |
| --- | --- | --- |
| Console | `]` | Programs |
| Programs | `[` | Console |
| Programs | `]` | Debug |
| Debug | `[` | Programs |

A bracket key at an outer boundary is consumed without changing the page.

Page navigation remains available from region-selection mode, active read-only regions, and document fullscreen browsing. Returning to a page restores the exact page-local state that was present when it was left.

Source editing and line editing consume bracket characters as text. Modal confirmations and option-selection modes capture bracket keys without changing pages.

## Page title rail

The top border carries the complete page-navigation affordance, so contextual command rows do not repeat bracket-key instructions.

```text
InputWeaver | CONSOLE ] Programs · Debug
InputWeaver | Console [ PROGRAMS ] Debug
InputWeaver | Console · Programs [ DEBUG
```

The current page is uppercase and uses the active page or region color. Other page names are muted. `[` and `]` appear only at valid transitions, while `·` occupies the unavailable transition position and keeps the page rail stable.

The title uses the literal bracket characters because they are the actual bindings. Direction glyphs enclosed in keycap brackets would visually identify different character keys.

## Programs spatial navigation

Programs opens with Programs selected in region-selection mode.

| Selected region | `Up` | `Down` | `Left` | `Right` |
| --- | --- | --- | --- | --- |
| Programs | Programs | Programs | Programs | Information |
| Information | Information | Source | Programs | Information |
| Source | Information | Source | Programs | Source |

Directional movement at an outer edge retains the current region. Movement from the full-height Programs region to the right enters Information deterministically. This avoids window-size-dependent geometry and hidden remembered-direction behavior.

`Enter` changes Programs from region-selection mode to active mode without changing the selected region. `Esc` from an active Programs region returns to region-selection mode and retains the selected region. `Esc` from Programs region-selection mode requests that the frontend return to the system tray.

The active Programs region owns program selection and program-management commands. The active Information region owns field selection and field editing. The active Source region owns source or dump browsing, view switching, fullscreen presentation, and source editing.

Entering source editing adds one nested level. `Esc` saves and leaves source editing while retaining the active Source region. In document fullscreen browsing, `Esc` restores the split Programs layout while retaining the active Source region. A subsequent `Esc` returns to Programs region-selection mode.

NEXT RUN remains a page-level command area. Its existing commands remain available across Programs region-selection and active-region states.

## Debug spatial navigation

Debug opens with Events selected in region-selection mode.

| Selected region | `Up` | `Down` | `Left` | `Right` |
| --- | --- | --- | --- | --- |
| Events | Events | Executions | Events | State |
| State | State | Executions | Events | State |
| Executions | Events | Executions | Executions | Executions |

Executions moves upward to Events deterministically because Events is the first region in visual reading order and occupies the larger upper area.

`Enter` activates the selected Debug region. An active Debug region owns its viewport movement. `Esc` returns to Debug region-selection mode and retains the selected region. `Esc` from Debug region-selection mode returns to Programs.

Capture and executor commands remain page-level Debug commands. HEALTH remains a status area outside region selection.

## Console navigation

Console continues to expose its viewport directly. `Esc` returns to Programs. `]` also moves to Programs through top-level page navigation.

## Modal and character-input precedence

Input dispatch will preserve a single precedence order:

1. Non-keyboard text sources are admitted only to an eligible text editor.
2. An active modal, confirmation, option selector, or line editor handles keyboard input before page navigation.
3. Source editing handles keyboard input before page navigation so bracket characters remain editable source text.
4. Bracket page navigation handles eligible keyboard events.
5. The visible page handles region selection, region interaction, and page commands.

The Windows frontend currently represents typing, paste, and file drop through the same `KeyEvent` transport while retaining their source. Normal typing arrives through `WM_CHAR` with `KeyEventSource::Keyboard`. `Ctrl+V` reads `CF_UNICODETEXT`, converts it through `QueueText`, and emits character, Enter, and Tab events with `KeyEventSource::Paste`. File drop reads each path through `DragQueryFileW`, quotes paths containing spaces or tabs, and emits the resulting characters with `KeyEventSource::Drop`.

The controller currently rejects command handling for Paste and Drop events while accepting their text in eligible line and source editors. This source distinction is why pasted command letters do not execute TUI actions. Bracket page navigation will use the same keyboard-source boundary rather than introducing a second input path.

## Rendering behavior

Region-selection mode highlights the selected region border and shows arrow-key region selection plus `Enter` activation in the contextual command area. Region contents remain visually inactive.

Active-region mode retains the selected region border, renders its active row or field treatment, shows only commands owned by that region or page, and includes `Esc` for returning to region selection.

Document fullscreen continues to render the source or dump over the Programs content area while preserving the Programs page title rail and NEXT RUN outside source editing.

## Implementation structure

`src/ui/tui/tui_controller.hpp` will define the orthogonal state types and page-local fields. The combined Programs state and Debug focus naming will be removed as part of the state migration.

`src/ui/tui/tui_controller.cpp` will centralize bracket page navigation, Programs region adjacency, Debug region adjacency, and interaction-level Escape handling. Page handlers will operate only on their owned state level.

`src/ui/tui/tui_renderer.cpp` will derive the stable page rail, selected-region borders, active content styling, and contextual commands from the new state model.

`docs/tui-guide.md` will describe the resulting page, region, activation, and Escape behavior after implementation.

## Verification

Controller and rendering tests will cover:

- Every valid and boundary bracket page transition.
- Page navigation from split browsing and document fullscreen browsing.
- Preservation of Programs fullscreen state when visiting Debug and returning.
- Preservation of Programs and Debug selected and active region states across page changes.
- Bracket insertion during source and line editing.
- Bracket suppression during confirmations and option selection.
- Every Programs and Debug spatial adjacency transition.
- `Enter` activation and layered `Esc` behavior for Programs and Debug.
- Console and Debug region-selection `Esc` transitions to Programs.
- Page title rail text and active-page styling at the minimum viewport size.
- Paste and drop text admission without command execution.

Verification will use `script/build_tui.bat`, `script/build_tests.bat`, and `script/test.bat`. Cross-cutting verification will use `script/verify_project.bat` because the change modifies controller state, rendering, documentation, and Windows-originated input assumptions.

## Completion criteria

Phase 15 is complete when the orthogonal state model is implemented, bracket navigation and spatial region selection match this specification, page-local state survives page changes, the page title rail communicates every available transition, the TUI guide reflects the implemented behavior, and all required builds and tests pass.
