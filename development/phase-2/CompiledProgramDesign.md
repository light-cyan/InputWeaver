# Phase 2 Compiled Program Design

## Status and authority

This document is the normative compiler/runtime contract for both the immutable in-memory `CompiledProgram` and its persistent `.weavec` representation. `docs/language/grammar.v1.md` is authoritative for the existing Weave execution semantics, while `docs/language/grammar.v2.md` extends source-level control references that lower into the control identity defined here. The compiler and runtime successor plans own implementation and verification, and `development/OpenDesignIssues.md` records decisions that remain open.

The C++20 object layout is an internal implementation detail and is not an ABI. The `.weavec` field encoding, numeric assignments, table meanings, validation rules, and execution semantics defined here are the shared standard. Compiler and runtime code may change the internal C++ layout only while preserving this contract.

## Role in the pipeline

```text
.weave source
    -> TokenStream
    -> SyntaxTree
    -> BoundProgram
    -> CompiledProgramStorage
    -> FinalizeCompiledProgram
    -> CompiledProgram
    -> EncodeWeavec
    -> .weavec file

.weavec file
    -> DecodeWeavec
    -> CompiledProgramStorage
    -> FinalizeCompiledProgram
    -> CompiledProgram
    -> backend activation binding
    -> RuleRuntime
```

`SyntaxTree` preserves source structure and spans. `BoundProgram` contains resolved names, static types, and structured control flow. `CompiledProgram` is the immutable executable representation after lowering. The runtime does not inspect tokens, syntax nodes, symbol names, or unresolved control names.

The parser does not emit `CompiledProgram` directly. This boundary allows syntax diagnostics, semantic diagnostics, and executable lowering to evolve independently while preserving a single runtime contract.

## Compiler/runtime ownership boundary

| Concern | Compiler | Shared `CompiledProgram` / `.weavec` | Runtime core | Platform adapter |
|---|---|---|---|---|
| Source text | Decode, parse, bind, type-check, and diagnose | Retain only required strings, source identity, line starts, and spans | Never parse source | None |
| Controls | Resolve source names and raw constructors to stable `ControlRef` values and derive required uses | Store canonical identities and `ControlRequirement` bits | Build dense activated-control records and execute by `ControlRefId` | Bind identities to native input matches, state queries, capabilities, and output recipes |
| Expressions, actions, rules, and mappings | Lower and canonicalize | Store immutable typed instructions, descriptors, indexes, and operands | Execute without name binding or type inference | Perform only requested platform operations |
| Variables | Emit types, initial values, and resolved `ValueRef` operands | Store immutable layout and initial values | Own mutable values, the variable-pool lock, and the dedicated `PAUSE` lock | None |
| Capacity | Derive conservative maxima from final tables | Store recomputable `ProgramRequirements` | Reject activation when fixed storage cannot satisfy them | Report backend-specific capacity or capability limits |
| Targets and `Exec` | Validate source strings and preserve authored text | Store selectors, commands, requirements, and spans without resolved processes or paths | Schedule target checks and process-launch requests | Resolve platform paths, processes, command semantics, and native API parameters |
| Persistence | Encode one validated program deterministically | Define the canonical `.weavec` bytes | Decode with bounds, finalize, validate, and activate transactionally | Never reinterpret file fields |

The shared contract is neither a platform-native program nor unresolved source delegated to the runtime. The compiler performs all language binding, while platform-dependent capability and native-recipe selection occurs once during runtime activation. No compiler branch or runtime branch may introduce a private opcode, numeric identity, operand meaning, table, or serialization field.

The control identity definitions, `CompiledProgram` records, structural validator, and `.weavec` codec API form the shared `program` module and must be frozen before the compiler and runtime branches diverge. The compiler owns calls that encode finalized programs; the runtime owns calls that decode artifacts and create activated state. Neither successor branch may copy these definitions into an owned directory or revise the shared module independently.

## Global invariants

- A successful compile produces one structurally valid `CompiledProgram`; a failed compile produces diagnostics and no program.
- Encoding and then decoding a valid program produces the same deterministic program dump and execution semantics.
- A runtime activates only a structurally validated program whose capability and capacity requirements are satisfied.
- The program owns every table, string, constant, descriptor, instruction, source span, and index used during execution.
- The program contains no pointers or references to source buffers, syntax nodes, compiler symbol objects, Win32 handles, process IDs, runtime tasks, mutable variables, active mappings, or output ownership records.
- The program is immutable after finalization and may be shared by the hook, task, application, and diagnostic threads through a `std::shared_ptr<const CompiledProgram>`.
- Every cross-table reference is a strong typed ID or a checked table range. Runtime string lookup is not used for controls, variables, rules, mappings, expressions, or action programs.
- Every instruction operand is fully resolved before activation. Runtime execution never performs name binding, type inference, source parsing, or backend control-name lookup.
- `CompiledProgram` contains no unqualified native key number, native hook record, native output recipe, operating-system handle, or resolved executable path.
- Access to `CompiledProgram` itself never allocates, blocks, or mutates state. Runtime synchronization for mutable variables and `PAUSE` is outside the immutable program contract.
- Mutable execution data is stored in `RuntimeState`, `TaskInstance`, `MappingRuntime`, and `OutputOwnership`, never in `CompiledProgram`.
- A program that violates an internal invariant is rejected before hooks are installed.

## Primitive representation types

### Strong IDs and ranges

All table positions use 32-bit unsigned indices. `0xffffffff` is reserved as the invalid value and may not identify an element.

```cpp
inline constexpr std::uint32_t kInvalidProgramIndex = 0xffffffffU;

template <typename Tag>
struct ProgramId final {
    std::uint32_t value{kInvalidProgramIndex};
};

struct TableRange final {
    std::uint32_t begin{};
    std::uint32_t count{};
};
```

The implementation defines distinct tags for `StringId`, `ControlRefId`, `ValueRefId`, `ExpressionId`, `ActionProgramId`, `MappingId`, and `MappingSlotId`. IDs of different kinds are not implicitly convertible. An invalid ID represents an optional reference only where this document explicitly permits it.

`TableRange` is half-open: it identifies `[begin, begin + count)`. Validation performs addition in a wider type and rejects overflow or a range that exceeds its owning table.

### Source spans

```cpp
struct SourceSpan final {
    std::uint32_t beginByte{};
    std::uint32_t byteLength{};
};
```

Spans use byte offsets into the validated UTF-8 source file. The program owns a line-start table whose first entry is zero and whose remaining entries identify the first byte after each recognized line break. Line and column values are derived for diagnostics and are not stored in hot-path records.

### Duration values

```cpp
struct DurationValue final {
    std::int64_t nanoseconds{};
};
```

Valid compiled durations are in `[0, INT64_MAX]` nanoseconds. The compiler parses duration literals as decimal rationals and requires an exact integral nanosecond representation; a literal outside the range or below nanosecond resolution is a compile error. Runtime duration subtraction clamps negative results to zero. Runtime multiplication or division converts a finite intermediate result by truncating toward zero; division by zero, a non-finite result, or a result above `INT64_MAX` is an evaluation fault.

### Controls and event transitions

```cpp
struct ControlRef final {
    std::uint32_t namespaceId{};
    std::uint32_t familyId{};
    std::uint32_t code{};
    std::uint32_t qualifier{};
};

enum class EventTransition : std::uint8_t {
    Down,
    Repeat,
    Up,
};

struct EventKey final {
    ControlRefId control{};
    EventTransition transition{};
};
```

`EventTransition` is separate from the Phase 1 normalized `Transition` because Weave distinguishes first down from keyboard repeat and excludes movement and wheel transitions in v1. The input normalizer and physical-state tracker classify an incoming physical keyboard down as `Down` or `Repeat` before rule lookup.

The complete four-field tuple is the control identity. Equality never compares `code` alone. Ordering is lexicographic by `namespaceId`, `familyId`, `code`, and `qualifier`. The finalized control pool is sorted and unique by this order; all other program tables use its dense `ControlRefId`. `EventKey` ordering compares `ControlRefId` and then `EventTransition`.

`namespaceId = 0` is invalid. Published namespace and family assignments are permanent and may not be reinterpreted or reused.

| `namespaceId` | Meaning | `familyId` | `code` | `qualifier` |
|---|---|---|---|---|
| `1` | USB HID | Usage Page | Usage ID | `0` |
| `2` | Weave-defined portable controls | Published control family | Published family-local code | `0` |
| `256` | Windows | `1` = Virtual-Key, `2` = Scan Code | Native code | Scan Code uses `0` = none, `1` = E0, `2` = E1; other families use `0` |
| `257` | Linux | `1` = `EV_KEY` | Native event code | `0` |
| `258` | macOS | `1` = native key code | Native key code | `0` |

Source raw constructors lower deterministically: `HID.Usage(page, usage)` becomes `{1, page, usage, 0}`; `Windows.VirtualKey(code)` becomes `{256, 1, code, 0}`; `Windows.ScanCode(code)`, `Windows.ScanCode(code, E0)`, and `Windows.ScanCode(code, E1)` use namespace `256`, family `2`, and qualifiers `0`, `1`, and `2`; `Linux.Key(code)` becomes `{257, 1, code, 0}`; and `MacOS.KeyCode(code)` becomes `{258, 1, code, 0}`.

The compiler control catalog contains source name, canonical name, and `ControlRef`. Aliases always lower to the same identity and never create duplicate control-pool entries. The catalog contains no platform capability or output recipe. Those properties belong exclusively to backend activation tables.

Portable keyboard names use HID Keyboard/Keypad Usage Page `0x07`: `A` through `Z` use Usage IDs `0x04` through `0x1D`; `Digit1` through `Digit9` use `0x1E` through `0x26`, and `Digit0` uses `0x27`; `Enter`, `Esc`, `Backspace`, `Tab`, and `Space` use `0x28` through `0x2C`; `CapsLock` uses `0x39`; `F1` through `F12` use `0x3A` through `0x45`; `ScrollLock` and `Pause` use `0x47` and `0x48`; `Insert`, `Home`, `PageUp`, `Delete`, `End`, `PageDown`, `ArrowRight`, `ArrowLeft`, `ArrowDown`, and `ArrowUp` use `0x49` through `0x52`; `NumLock`, `NumpadDivide`, `NumpadMultiply`, `NumpadSubtract`, `NumpadAdd`, `Numpad1` through `Numpad9`, `Numpad0`, and `NumpadDecimal` use `0x53` through `0x57`, `0x59` through `0x61`, `0x62`, and `0x63`; `F13` through `F24` use `0x68` through `0x73`; and `LCtrl`, `LShift`, `LAlt`, `RCtrl`, `RShift`, and `RAlt` use `0xE0`, `0xE1`, `0xE2`, `0xE4`, `0xE5`, and `0xE6`. A v1 short name and its `Keyboard.` form lower to the same identity.

Portable mouse buttons use HID Button Usage Page `0x09`: `Mouse.Left`, `Mouse.Right`, `Mouse.Middle`, `Mouse.X1`, and `Mouse.X2` use Usage IDs `1` through `5`. Portable consumer controls use HID Consumer Usage Page `0x0C`: `Consumer.PlayPause`, `Consumer.ScanNextTrack`, `Consumer.ScanPreviousTrack`, `Consumer.Stop`, `Consumer.Mute`, `Consumer.VolumeUp`, and `Consumer.VolumeDown` use Usage IDs `0x00CD`, `0x00B5`, `0x00B6`, `0x00B7`, `0x00E2`, `0x00E9`, and `0x00EA`.

Platform-qualified readable names are ordinary catalog entries whose identities use the corresponding platform namespace. Aliases lower to an existing identity. Every namespace and family used by the catalog requires a published numeric assignment and conformance tests.

The initial platform-qualified entries used by the v2 language examples are fixed as follows: `Windows.Keyboard.IMEOn` is `{256, 1, 0x16, 0}` for `VK_IME_ON`; `Linux.Keyboard.Compose` is `{257, 1, 127, 0}` for `KEY_COMPOSE`; and `MacOS.Keyboard.Fn` is `{258, 1, 0x3F, 0}` for `kVK_Function`.

Namespace `1` follows the numeric Usage Page and Usage ID assignments published in the USB-IF [HID Usage Tables](https://www.usb.org/hid). Windows Virtual-Key identities follow Microsoft's [Virtual-Key Codes](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes), Linux `EV_KEY` identities follow the Linux kernel [input event code](https://docs.kernel.org/input/event-codes.html) definitions, and macOS native key identities use Apple's [CGKeyCode](https://developer.apple.com/documentation/coregraphics/cgkeycode) numeric domain. External numbers enter only their assigned namespace and family; they never become unqualified portable codes.

During activation, the selected backend resolves every required `ControlRef` into one runtime-local activated-control record. That record owns the native input match, physical-state query mechanism, output-down/up recipe, repeat behavior, routing classification, and capability bits. It is mutable environment state, is indexed by `ControlRefId`, and is never serialized. An unsupported identity or missing required capability rejects activation before hooks are installed.

## Source identity, strings, and settings

### String pool

The program owns a `std::vector<std::string>` UTF-8 string pool. Target selectors, the source path, `exec` command lines, and diagnostic symbol names use `StringId`. Platform adapters convert strings to their native process API representation.

The compiler rejects an embedded NUL in a target selector, path, or `exec` command because supported native process and path APIs use terminated strings.

An `Exec` instruction retains exactly one authored command string. `CompiledProgram` does not store a compiler-parsed executable token, resolved executable path, child working directory, or native argument vector. These values depend on the execution environment and belong to the platform launcher. The exact Windows resolution policy remains governed by open issue `ODI-004`.

### Program source

```cpp
struct ProgramSource final {
    StringId displayPath{};
    std::uint32_t byteLength{};
    TableRange lineStarts{};
};
```

Weave v1 compiles one source file. Source identity supports diagnostics only and does not determine child-process execution context.

### Target selector

```cpp
enum class TargetSelectorKind : std::uint8_t {
    Unspecified,
    Global,
    Executable,
};

struct TargetSelector final {
    TargetSelectorKind kind{};
    StringId text{};
    SourceSpan source{};
};
```

`text` is invalid for `Unspecified` and `Global` and valid for `Executable`. It preserves the authored selector without classifying it under the compiler host's path rules. The selected platform adapter determines whether it is a supported executable name or path during activation. A command-line target override is an application activation option and does not mutate this record.

### Program settings

```cpp
struct ProgramSettings final {
    TargetSelector target{};
    DurationValue tapDuration{};
    DurationValue actionGap{};
};
```

The compiler supplies the language defaults when the source omits `TAP_DURATION` or `ACTION_GAP`. These settings are immutable. `PAUSE` is not a setting; it is a mutable built-in runtime state initialized to `on`.

## Values and runtime layout

### Value and expression types

```cpp
enum class ValueType : std::uint8_t {
    State,
    Number,
    Duration,
};

enum class ExpressionType : std::uint8_t {
    None,
    Boolean,
    State,
    Number,
    Duration,
};
```

`Boolean` is an expression result type and is not a user-declarable storage type. `number` is a finite IEEE 754 `double`. State values are represented as zero or one. Duration values use `DurationValue`.

### Value references

```cpp
enum class ValueDomain : std::uint8_t {
    UserState,
    UserNumber,
    UserDuration,
    BuiltinState,
    BuiltinDuration,
};

enum class BuiltinState : std::uint8_t {
    Pause,
};

enum class BuiltinDuration : std::uint8_t {
    TapDuration,
    ActionGap,
};

struct ValueRef final {
    ValueDomain domain{};
    ValueType type{};
    std::uint32_t index{};
};
```

The program owns a deduplicated `ValueRef` table. A user-domain index addresses the corresponding typed runtime array. A built-in index is the numeric value of the matching built-in enum. Validation rejects any domain, type, and index combination that is not defined above.

Physical control state is not a stored `ValueRef`. Expression bytecode reads it through a resolved `ControlRefId`, which makes physical-state capability requirements explicit.

### User value layout

```cpp
struct UserValueLayout final {
    std::vector<std::uint8_t> initialStates;
    std::vector<double> initialNumbers;
    std::vector<DurationValue> initialDurations;
};

struct VariableDebugRecord final {
    StringId name{};
    ValueRefId value{};
    SourceSpan declaration{};
};
```

Each source declaration creates exactly one typed slot and one debug record. Initial numbers must be finite and initial durations must be valid. Runtime storage preserves the same typed indices but owns the mutable values.

## Expression programs

### Storage

Expression programs use a typed stack machine. All expression instructions are stored in one flat table, and each descriptor owns a range in that table.

```cpp
enum class ExpressionOpcode : std::uint8_t {
    PushBoolean,
    PushState,
    PushNumber,
    PushDuration,
    LoadValue,
    ReadControlHeld,
    Unary,
    Binary,
    Jump,
    JumpIfFalse,
    JumpIfTrue,
    Return,
};

struct ExpressionInstruction final {
    ExpressionOpcode opcode{};
    ExpressionType type{};
    std::uint32_t operand0{};
    std::uint32_t operand1{};
};

struct ExpressionDescriptor final {
    TableRange code{};
    ExpressionType resultType{};
    std::uint32_t maximumStackDepth{};
    SourceSpan source{};
};
```

The program owns separate number and duration constant tables. `PushNumber` and `PushDuration` use `operand0` as the constant index. `PushBoolean` and `PushState` use `operand0` as zero or one. `LoadValue` uses `operand0` as `ValueRefId`. `ReadControlHeld` uses `operand0` as `ControlRefId` and pushes `Boolean`.

`Unary` uses `operand0` as a `UnaryOperator`. `Binary` uses `operand0` as a `BinaryOperator`. The instruction `type` is the result type and is checked against the operator signature. Unused operands must be zero.

`Jump`, `JumpIfFalse`, and `JumpIfTrue` use `operand0` as an instruction offset local to the descriptor. Conditional jumps pop one `Boolean`. Expression jumps are forward-only. `Return` pops exactly one value of `resultType` and terminates evaluation.

### Operators

```cpp
enum class UnaryOperator : std::uint8_t {
    NumberIdentity,
    NumberNegate,
    BooleanNot,
};

enum class BinaryOperator : std::uint8_t {
    NumberAdd,
    NumberSubtract,
    NumberMultiply,
    NumberDivide,
    NumberModulo,
    DurationAdd,
    DurationSubtract,
    DurationMultiplyNumber,
    NumberMultiplyDuration,
    DurationDivideNumber,
    Equal,
    NotEqual,
    NumberLess,
    NumberLessEqual,
    NumberGreater,
    NumberGreaterEqual,
};
```

`Equal` and `NotEqual` require two operands of the same `State`, `Number`, or `Duration` type and produce `Boolean`. The four ordered comparisons accept two numbers. The remaining signatures follow their names and the Weave v1 type rules.

`and` and `or` lower to `JumpIfFalse` or `JumpIfTrue` control flow and therefore short-circuit. `not` lowers to `BooleanNot`. Every expression control-flow path is acyclic, has a consistent stack type sequence at every merge, and reaches one `Return` with exactly one result.

### Evaluation faults

Number division or modulo by zero, a non-finite number result, invalid duration arithmetic, and a runtime type or stack violation are evaluation faults. Constant expressions that would fault are compile errors. A fault while evaluating a hook predicate causes the current event to be forwarded and requests controlled fatal shutdown. A fault on the task thread terminates the task, releases its owned outputs, and requests controlled fatal shutdown. This policy prevents an invalid runtime value from silently changing consumption or control-flow decisions.

## Action programs

### Storage

Action programs use one flat instruction table. A task stores an `ActionProgramId` and a program-local instruction position; the runtime fetches the instruction at `descriptor.code.begin + position`.

```cpp
enum class ActionOpcode : std::uint8_t {
    Press,
    Release,
    Tap,
    Wait,
    Gap,
    Set,
    Toggle,
    Exec,
    Jump,
    JumpIfFalse,
    RepeatInit,
    RepeatCheck,
    RepeatNext,
    Yield,
    End,
};

struct ActionInstruction final {
    ActionOpcode opcode{};
    std::uint32_t operand0{};
    std::uint32_t operand1{};
};

struct ActionProgramDescriptor final {
    TableRange code{};
    std::uint32_t repeatFrameCount{};
    std::uint32_t maximumOwnedControlCount{};
    SourceSpan source{};
};
```

The program owns a source-span table parallel to the flat action instruction table. Each instruction therefore has a stable runtime diagnostic span without increasing the hot instruction record.

### Instruction operands and effects

| Opcode | Operand 0 | Operand 1 | Runtime effect |
|---|---|---|---|
| `Press` | `ControlRefId` | zero | Acquire task ownership and inject down only on the global zero-to-one transition. |
| `Release` | `ControlRefId` | zero | Release task ownership and inject up only on the global one-to-zero transition. |
| `Tap` | `ControlRefId` | zero | Acquire temporary task ownership, wait `tapDuration`, then release it. |
| `Wait` | `ExpressionId` returning `Duration` | zero | Evaluate once and enter cancellable timed wait. |
| `Gap` | zero | zero | Enter cancellable timed wait for `actionGap`. |
| `Set` | `ValueRefId` | `ExpressionId` of the same value type | Evaluate and atomically publish the new value. |
| `Toggle` | `ValueRefId` of `State` | zero | Atomically invert the state value. |
| `Exec` | `StringId` | zero | Resolve and launch the stored command on the task thread with the resolved executable's containing directory as the child working directory. |
| `Jump` | local target position | zero | Set the local program position. |
| `JumpIfFalse` | `ExpressionId` returning `Boolean` | local target position | Evaluate and jump when false. |
| `RepeatInit` | repeat frame index | `ExpressionId` returning `Number` | Evaluate the limit once and initialize the frame index to zero. |
| `RepeatCheck` | repeat frame index | local end position | Jump to the end when `index < limit` is false. |
| `RepeatNext` | repeat frame index | zero | Increment the unsigned iteration index or raise a fatal overflow fault. |
| `Yield` | zero | zero | Return the task to the ready queue without a timed wait. |
| `End` | zero | zero | Finish the task and release any task ownership that remains. |

Every repeat frame stores an unsigned 64-bit `index` and a finite double `limit`. A descriptor frame index is local to one `TaskInstance`; task instances never share repeat frames.

`Set` and `Toggle` may write user values only. `BuiltinState::Pause` remains readable by expressions but is writable only through the dedicated pause-control tables. `Release` raises a task action fault when the current task does not own the referenced control.

`maximumOwnedControlCount` is the conservative number of distinct control identities that the task may own, not the sum of ownership acquisitions. One fixed ownership record per distinct control stores a checked acquisition count; counter overflow is a fatal runtime fault.

Action jumps may be forward or backward. Every backward edge must be an unconditional `Jump` immediately preceded by `Yield`, which makes all compiled loop back edges cooperative. The validator rejects any other backward action edge.

An empty source action flow does not create an action program reference in its rule. The compiler may retain a canonical `End`-only program for tests and lowering internals, but rules with no actions use an invalid `ActionProgramId` and do not allocate tasks.

`Tap` advances past its instruction only after the paired temporary ownership has been released. Cancellation, task failure, and normal `End` release all task-owned controls through the runtime cleanup path.

## Rules and event index

### Rule records

```cpp
enum class Delivery : std::uint8_t {
    Observe,
    Consume,
};

enum class MatchFlow : std::uint8_t {
    Stop,
    Continue,
};

enum class RuleKind : std::uint8_t {
    Event,
    MappingDown,
};

struct CompiledRule final {
    ExpressionId condition{};
    ActionProgramId action{};
    MappingId mapping{};
    Delivery delivery{};
    MatchFlow flow{};
    RuleKind kind{};
    std::uint32_t sourceOrdinal{};
    SourceSpan source{};
};
```

An invalid `condition` means unconditional true. For `Event`, `mapping` is invalid and `action` may be invalid. For `MappingDown`, `mapping` is valid, `action` is invalid, `delivery` is `Consume`, and `flow` is `Stop`.

`sourceOrdinal` is unique across ordinary, mapping-down, and pause-control rules and increases with top-level source order. It remains available for deterministic diagnostics even though rules are physically grouped by event and channel.

### Pause-control rules and index

```cpp
enum class PauseEffect : std::uint8_t {
    On,
    Off,
    Toggle,
};

struct PauseControlRule final {
    ExpressionId condition{};
    Delivery delivery{};
    PauseEffect effect{};
    std::uint32_t sourceOrdinal{};
    SourceSpan source{};
};

struct PauseControlBucket final {
    EventKey key{};
    TableRange rules{};
};
```

Pause-control rules are stored separately from ordinary rules and mappings. Their source syntax has one of the effects `on`, `off`, or `toggle` and one of the stop-only deliveries consume or observe. They contain no `ActionProgramId`, mapping, flow field, or task state.

Pause-control buckets are strictly sorted by `EventKey`; their ranges are disjoint, cover the complete pause-control rule table, and preserve global source ordinals. Runtime scans this index after physical-state and force-stop processing and applicable target routing but before the ordinary `PAUSE` guard. A matching rule applies its effect synchronously and creates no task or event transaction item.

### Event buckets

```cpp
struct EventBucket final {
    EventKey key{};
    TableRange rules{};
};
```

Event buckets are strictly sorted by `EventKey` and contain no duplicate key. Their rule ranges are disjoint, cover the complete compiled rule table, and preserve increasing `sourceOrdinal` within each bucket. Runtime lookup uses binary search and returns a contiguous rule span without allocation.

The conservative maximum number of non-empty event-rule actions in any bucket becomes `ProgramRequirements::maximumTasksPerEvent`. A runtime reserves that many task records and the corresponding queue publication capacity before it can consume an event.

## Complete mappings

### Mapping slots and descriptors

```cpp
struct MappingSlotDescriptor final {
    ControlRefId source{};
};

struct MappingDescriptor final {
    MappingSlotId slot{};
    ControlRefId target{};
    SourceSpan source{};
};
```

There is one mapping slot for each distinct source control used by `:=`. Slots are strictly sorted by source control. Mutable runtime state stores one optional active `MappingId` per slot, which is equivalent to the conceptual hidden state of each declaration while enforcing that a source has at most one active mapping.

Each `MappingDown` rule references one descriptor whose slot source equals the containing event bucket control and whose event transition is `Down`. Mapping conditions live on the rule. The descriptor target is fully resolved.

On physical source `Repeat` or `Up`, the mapping runtime looks up the source slot before scanning ordinary event rules. An active mapping generates its target repeat or release lifecycle action, marks the source event consumed, and clears the slot after a committed release. The ordinary event bucket is still scanned afterward.

Mapping lifecycle actions are not `TaskInstance` programs. They are synchronous selections committed through the same fixed-capacity publication transaction used by the dispatcher, preserving Phase 1 fail-open behavior when reservation cannot complete.

## Capabilities and resource requirements

### Control capabilities

```cpp
enum class ControlUse : std::uint8_t {
    EventSource = 1U << 0U,
    PhysicalState = 1U << 1U,
    OutputDownUp = 1U << 2U,
    OutputRepeat = 1U << 3U,
};

struct ControlRequirement final {
    ControlRefId control{};
    std::uint8_t uses{};
};
```

The compiler merges all uses of each resolved control. Event triggers require `EventSource`, `[held]` and `[idle]` require `PhysicalState`, input actions and mapping targets require `OutputDownUp`, and every complete mapping target requires the semantic `OutputRepeat` capability. The compiler does not inspect a backend to derive these bits. A backend may satisfy `OutputRepeat` with an explicit native repeat event, held-key behavior, or another tested recipe, but activation rejects a backend that cannot reproduce the required semantics.

### Program requirements

```cpp
struct ProgramRequirements final {
    std::uint32_t stateSlotCount{};
    std::uint32_t numberSlotCount{};
    std::uint32_t durationSlotCount{};
    std::uint32_t mappingSlotCount{};
    std::uint32_t maximumPauseRulesPerEvent{};
    std::uint32_t maximumRulesPerEvent{};
    std::uint32_t maximumPredicateStepsPerEvent{};
    std::uint32_t maximumTasksPerEvent{};
    std::uint32_t maximumMappingOperationsPerEvent{};
    std::uint32_t maximumTransactionItemsPerEvent{};
    std::uint32_t maximumExpressionStackDepth{};
    std::uint32_t maximumRepeatFramesPerTask{};
    std::uint32_t maximumOwnedControlsPerTask{};
    bool requiresProcessLaunch{};
};
```

Every field is derived from the final tables and independently recomputed by validation. `maximumPredicateStepsPerEvent` includes the pause-control and ordinary predicate work reachable for the same event key, while pause-control rules do not contribute task, mapping-operation, or transaction-item requirements. Runtime activation rejects a program when fixed task, queue, expression scratch, repeat-frame, ownership, mapping, or runtime-value capacities cannot satisfy these requirements.

`requiresProcessLaunch` is true when any action instruction is `Exec`. It allows the application and future UI to expose the executable-configuration capability before activation.

## Debug information

```cpp
struct ProgramDebugInfo final {
    std::vector<VariableDebugRecord> variables;
    std::vector<SourceSpan> expressionInstructionSpans;
    std::vector<SourceSpan> actionInstructionSpans;
};
```

Rule, mapping, expression, and action descriptors retain their enclosing source spans. Parallel instruction-span tables have exactly the same length as their instruction tables. Debug names do not participate in runtime lookup or semantics.

A deterministic `DumpCompiledProgram` utility prints settings, value slots, controls, requirements, event buckets, rules, mappings, expressions, actions, and source spans using stable IDs. Compiler golden tests and runtime fixture tests use this human-readable diagnostic format to compare semantics. It is distinct from the binary `.weavec` encoding.

## Top-level definition

```cpp
struct CompiledProgramStorage final {
    ProgramSource source{};
    ProgramSettings settings{};
    ProgramRequirements requirements{};

    std::vector<std::string> strings;
    std::vector<std::uint32_t> lineStarts;
    std::vector<ControlRef> controls;
    std::vector<ControlRequirement> controlRequirements;
    std::vector<ValueRef> valueRefs;
    UserValueLayout userValues;

    std::vector<double> numberConstants;
    std::vector<DurationValue> durationConstants;
    std::vector<ExpressionDescriptor> expressions;
    std::vector<ExpressionInstruction> expressionCode;

    std::vector<ActionProgramDescriptor> actionPrograms;
    std::vector<ActionInstruction> actionCode;

    std::vector<MappingSlotDescriptor> mappingSlots;
    std::vector<MappingDescriptor> mappings;
    std::vector<PauseControlBucket> pauseControlBuckets;
    std::vector<PauseControlRule> pauseControlRules;
    std::vector<EventBucket> eventBuckets;
    std::vector<CompiledRule> rules;

    ProgramDebugInfo debugInfo;
};
```

The implementation exposes this storage through a `CompiledProgram` class with const accessors returning values or `std::span<const T>`. Only `CompiledProgramBuilder`, the `.weavec` decoder, and `FinalizeCompiledProgram` may construct mutable storage. The executing runtime receives only the finalized immutable program.

## Persistent `.weavec` artifact

### File identity and header

`.weavec` is the canonical persistent representation of one finalized `CompiledProgram`. It is a field-by-field binary encoding, never a dump of C++ object memory, `sizeof` bytes, pointers, vector internals, padding, or implementation-specific enum layout.

The file begins with this 16-byte header:

| Offset | Size | Field |
|---|---|---|
| `0` | `8` | Magic bytes `57 45 41 56 45 43 00 00`, representing `WEAVEC` followed by two zero bytes |
| `8` | `8` | Payload byte length, unsigned little-endian |

The payload begins immediately after the header and must occupy exactly the declared length. Truncation and trailing bytes are errors. The field definitions and payload order in this document define the encoding.

The header contains no compiler-host or target-platform tag. Portability is determined by the identities and requirements inside the program: a program containing only identities supported by the selected backend can activate, while an explicit platform namespace, target selector, or command that the backend cannot support fails activation with a specific diagnostic.

### Scalar and record encoding

- Unsigned and signed integers use their documented fixed width and little-endian byte order.
- Strong IDs, table counts, `TableRange` members, `SourceSpan` members, and instruction operands are 32-bit unsigned integers.
- `ControlRef` is four consecutive 32-bit unsigned integers in `namespaceId`, `familyId`, `code`, and `qualifier` order.
- `DurationValue` is one signed 64-bit integer.
- A `number` is the exact IEEE 754 binary64 bit pattern written as one little-endian 64-bit word; decoding rejects non-finite values wherever the program contract requires finiteness.
- Enumerations are encoded using the fixed underlying width declared by their normative definition. Boolean fields use one byte and accept only `0` or `1`.
- A string is a 32-bit byte count followed by exactly that many UTF-8 bytes without a terminator.
- A vector is a 32-bit element count followed by its elements. Nested vectors, including initial-value and debug arrays, repeat the same rule.
- A record writes its documented logical fields in declaration order with no alignment or padding bytes.

Numeric enum and opcode values are their zero-based declaration order unless an explicit value is shown. Their declaration order and operand meanings are part of the persistent encoding. Names that lower to existing `ControlRef` identities use the same encoded identity.

### Payload order

The payload encodes `CompiledProgramStorage` in this fixed order:

1. `ProgramSource`, `ProgramSettings`, and `ProgramRequirements`.
2. `strings`, then `lineStarts`.
3. `controls`, `controlRequirements`, and `valueRefs`.
4. `UserValueLayout.initialStates`, `initialNumbers`, and `initialDurations`.
5. `numberConstants`, `durationConstants`, `expressions`, and `expressionCode`.
6. `actionPrograms` and `actionCode`.
7. `mappingSlots` and `mappings`.
8. `pauseControlBuckets` and `pauseControlRules`.
9. `eventBuckets` and `rules`.
10. `ProgramDebugInfo.variables`, `expressionInstructionSpans`, and `actionInstructionSpans`.

Every nested record uses the field order in its normative definition in this document. Optional strong IDs remain encoded as their 32-bit invalid value. There are no native paths, handles, pointer values, backend bindings, task records, active mappings, variable mutations, lock state, timers, queue contents, or output-ownership records in the artifact.

### Determinism, loading, and replacement

The compiler finalizes and canonicalizes the program before encoding. Identical finalized programs produce identical `.weavec` bytes. The compiler writes the encoded bytes to a sibling temporary file, closes it successfully, and atomically replaces the destination.

The runtime reads and validates the header and declared payload bound before allocating table storage. Every count, byte length, multiplication, addition, and cursor advance uses checked arithmetic and configured hard limits. Decoding rejects an invalid header, oversized artifact, invalid UTF-8, invalid scalar representation, truncated field, trailing field, or impossible allocation before publication.

Successful decoding produces `CompiledProgramStorage` and passes it through the same `FinalizeCompiledProgram` path used by the compiler and fixtures. Structural validation recomputes requirements and canonical invariants rather than trusting serialized claims. Backend activation begins only after decoding and structural validation succeed. A load failure preserves the previously active program.

## Construction and finalization

```cpp
struct CompileResult final {
    std::shared_ptr<const CompiledProgram> program;
    std::vector<CompileDiagnostic> diagnostics;
};

struct FinalizeResult final {
    std::shared_ptr<const CompiledProgram> program;
    std::vector<ProgramValidationError> errors;
};

FinalizeResult FinalizeCompiledProgram(CompiledProgramStorage storage);
```

The compiler lowers a fully valid `BoundProgram` into mutable storage, canonicalizes the control pool and every dependent `ControlRefId`, sorts and flattens indexes, derives requirements, and calls the shared finalizer. Test fixtures and the `.weavec` decoder use the same finalizer. Finalization validates before constructing the immutable handle and never returns a partial handle.

The field codec belongs to the shared `program` module used by both successor branches. The compiler calls the encoder only with a finalized `CompiledProgram`; the runtime calls the decoder and finalizer before creating any runtime state. Neither branch maintains a duplicate wire struct, opcode table, namespace assignment, or record-size calculation.

Compiler diagnostics describe source errors. An invalid compiler-built program is an internal compiler defect. Invalid bytes or a structurally invalid decoded program produce a bounded artifact-load diagnostic and are never activated.

## Structural validation

`ValidateCompiledProgram` performs all of the following checks without consulting a platform backend:

- Every table length is below `kInvalidProgramIndex`.
- Every ID is valid for its table or is invalid only in an explicitly optional field.
- Every range uses checked arithmetic, lies inside its owning table, and obeys the required disjointness and coverage rules.
- All strings are valid UTF-8, API-bound strings contain no embedded NUL, source offsets are within `byteLength`, and line starts are strictly increasing.
- Settings and all initial or constant values satisfy finite-number and duration invariants.
- The control pool is strictly sorted and unique; every `ControlRef` has a published nonzero namespace, a defined family, a valid qualifier for that family, and no reserved field value.
- Every `ControlRefId` in event keys, expression and action operands, mappings, and requirements addresses the canonical control pool.
- Every `ValueRef` domain, type, and index combination is valid.
- Every expression instruction has valid operands, valid operator signatures, forward-only targets, consistent stack types at merges, a bounded declared stack depth, and exactly one correctly typed result on every path.
- Every action descriptor ends with a reachable `End`, every target stays inside its descriptor, every expression operand has the required result type, every repeat frame is in range, and every backward edge is an immediately yielded loop edge.
- Action instructions reference valid controls, values, expressions, and strings; `Set` and `Toggle` targets are writable; unused operands are zero.
- Pause-control and ordinary event buckets are independently strictly sorted and unique; each rule table has disjoint, complete, source-ordered ranges, and source ordinals are globally unique across both channels.
- Every pause-control rule has a Boolean or absent condition, observe or consume delivery, a defined pause effect, a valid source span, and no ordinary action or mapping representation.
- Every rule condition is invalid or Boolean, and every rule kind satisfies its action, mapping, delivery, and flow field invariants.
- Mapping slots are strictly sorted and unique; every mapping references a valid slot and target; every `MappingDown` rule is in the matching source `Down` bucket.
- Control requirements exactly cover and merge all source, state-query, output, and mapping uses.
- Debug span arrays match their instruction arrays and every retained span is valid.
- Recomputed program requirements exactly equal the stored requirements.

The validator reports all defects that can be collected safely in one pass, up to a fixed diagnostic limit. Compiler-built validation failures are internal defects; decoded-artifact validation failures are artifact errors. Neither path installs hooks or publishes a partial program.

## Activation validation

After structural validation, application activation performs checks that depend on the selected environment:

- The selected backend resolves every required `ControlRef` into exactly one runtime-local activated-control record.
- Each activated-control capability set contains every `ControlRequirement` use requested for that identity.
- Platform-qualified identities are accepted structurally on every host but reject activation when the selected backend cannot bind them.
- The configured task pool and publication queue satisfy `maximumTasksPerEvent`.
- Expression scratch storage satisfies `maximumExpressionStackDepth`.
- Task local and ownership pools satisfy the per-task repeat-frame and control-ownership maxima.
- Runtime value and mapping-state storage satisfy their declared counts.
- Process launch is permitted when `requiresProcessLaunch` is true.
- A command-line target override or the compiled target selector produces a valid target policy.

Activation is transactional. Runtime state is initialized and all capacity checks complete before program publication or hook installation. Failure preserves the previously active program, or leaves the application in observer mode when no program was active.

Backend binding does not alter the immutable program. The activated-control array is indexed by `ControlRefId` and contains backend-owned opaque references or fixed descriptors. Input normalization maps a supported native event to an activated `ControlRefId`; an unbound native event bypasses user rules. Output execution uses the same activated record, so input and output cannot independently reinterpret one compiled identity.

## Runtime access model

The dispatcher receives an activated `ControlRefId`, forms an `EventKey`, and first searches `pauseControlBuckets` without consulting the current `PAUSE` value. A matching pause-control rule applies one synchronous effect and returns its delivery decision. When no pause-control rule matches and `PAUSE` is on, the dispatcher searches `eventBuckets`, evaluates conditions with a preallocated typed stack, scans the contiguous ordinary rule span, and writes selected action and mapping IDs into a preallocated event transaction. It reserves the full transaction before committing mapping state or returning `Suppress`.

The task scheduler stores program IDs and local positions, not instruction pointers. Holding a shared immutable program handle keeps all referenced tables alive. A program reload publishes a new handle only after old-generation tasks and mapping state have been cancelled and their outputs released.

The action VM reads strings only for `Exec` on the task thread. The hook path never dereferences the string pool or debug-name records.

## Phase 1 integration boundary

The existing `Action`, `ActionBatch`, bounded queue, self-tagged injector, target checks, and cleanup behavior remain the initial platform-output boundary. `CompiledProgram` does not contain `ActionBatch` records or native output codes. The rule runtime resolves each selected `ControlRefId` through its activated-control record and translates mapping lifecycle operations and action VM output primitives into the backend's bounded injection contract.

The existing `FixedRuleEngine` remains a regression oracle until the equivalent Weave rules pass deterministic runtime tests and real Windows loopback verification. It is not embedded in `CompiledProgram`.

## Required contract fixtures

Phase 2 implementation must provide deterministic validated fixtures for at least the following programs:

```weave
TARGET = GLOBAL;
F6:down => tap(F7);
```

```weave
TARGET = GLOBAL;
F6 := F7;
```

```weave
TARGET = GLOBAL;
state enabled = on;
F6:down when enabled[on] => repeat 2 do tap(F7) | end;
```

```weave
TARGET = GLOBAL;
pause F6:down => toggle;
```

The first fixture proves event indexing, consumption, task creation, `Tap`, timed wakeup, and paired release. The second proves mapping slots and down, repeat, and up lifecycle handling. The third proves value layout, Boolean expressions, repeat frames, backward-yield validation, and action gaps. The fourth proves the dedicated pause-control index, stop-only delivery, synchronous effect representation, and zero task requirement.

Compiler output and hand-built runtime fixtures must produce the same deterministic dump for the same semantic program. Every fixture must also encode to `.weavec`, decode, finalize, and reproduce the same dump byte-for-byte. Control fixtures additionally prove alias canonicalization, raw constructor lowering, portable backend binding, platform-specific activation rejection, and identical `ControlRefId` use by normalized input and output recipes. These equalities are the principal convergence gate for the forked compiler and runtime workstreams.
