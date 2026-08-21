# Phase 2 Compiled Program Design

## Status and authority

This document is the normative definition of the in-memory `CompiledProgram` consumed by the InputWeaver rule runtime. `docs/language/grammar.v1.md` is authoritative for Weave v1 source syntax and user-visible semantics. The compiler and runtime successor plans own their complete implementation and verification scope, and `development/OpenDesignIssues.md` records active decisions that are not yet fixed.

The contract is an internal C++20 data model, not a serialized bytecode format or a stable external ABI. Compiler and runtime changes may revise the C++ layout during Phase 2 only through the contract-change process in `development/phase-2/ImplementationPlan.md`; the semantic invariants in this document must remain true.

## Role in the pipeline

```text
.weave source
    -> TokenStream
    -> SyntaxTree
    -> BoundProgram
    -> CompiledProgram
    -> RuleRuntime
```

`SyntaxTree` preserves source structure and spans. `BoundProgram` contains resolved names, static types, and structured control flow. `CompiledProgram` is the immutable executable representation after lowering. The runtime does not inspect tokens, syntax nodes, symbol names, or unresolved control names.

The parser does not emit `CompiledProgram` directly. This boundary allows syntax diagnostics, semantic diagnostics, and executable lowering to evolve independently while preserving a single runtime contract.

## Global invariants

- A successful compile produces one structurally valid `CompiledProgram`; a failed compile produces diagnostics and no program.
- A runtime activates only a structurally validated program whose capability and capacity requirements are satisfied.
- The program owns every table, string, constant, descriptor, instruction, source span, and index used during execution.
- The program contains no pointers or references to source buffers, syntax nodes, compiler symbol objects, Win32 handles, process IDs, runtime tasks, mutable variables, active mappings, or output ownership records.
- The program is immutable after finalization and may be shared by the hook, task, application, and diagnostic threads through a `std::shared_ptr<const CompiledProgram>`.
- Every cross-table reference is a strong typed ID or a checked table range. Runtime string lookup is not used for controls, variables, rules, mappings, expressions, or action programs.
- Every instruction operand is fully resolved before activation. Runtime execution never performs name binding, type inference, source parsing, or backend control-name lookup.
- Hook-path access performs only bounded table lookup, bounded expression evaluation, bounded rule scanning, and fixed-capacity reservation. It performs no allocation, file access, console access, process creation, waiting, or input injection.
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
    DeviceKind device{};
    ControlCode code{};
};

enum class EventTransition : std::uint8_t {
    Down,
    Repeat,
    Up,
};

struct EventKey final {
    ControlRef control{};
    EventTransition transition{};
};
```

`EventTransition` is separate from the Phase 1 normalized `Transition` because Weave distinguishes first down from keyboard repeat and excludes movement and wheel transitions in v1. The input normalizer and physical-state tracker classify an incoming physical keyboard down as `Down` or `Repeat` before rule lookup.

`ControlRef` ordering compares `device` and then `code`. `EventKey` ordering compares `ControlRef` and then `EventTransition`. These orderings are used by sorted program indexes and deterministic dumps.

## Source identity, strings, and settings

### String pool

The program owns a `std::vector<std::string>` UTF-8 string pool. Target selectors, the source path, `exec` command lines, and diagnostic symbol names use `StringId`. Platform adapters convert strings to their native process API representation.

The compiler rejects an embedded NUL in a target selector, path, or `exec` command because supported native process and path APIs use terminated strings.

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
    ExecutableName,
    AbsolutePath,
};

struct TargetSelector final {
    TargetSelectorKind kind{};
    StringId text{};
    SourceSpan source{};
};
```

`text` is invalid for `Unspecified` and `Global` and valid for the two string forms. A command-line target override is an application activation option and does not mutate this record.

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
    ControlRef source{};
};

struct MappingDescriptor final {
    MappingSlotId slot{};
    ControlRef target{};
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

The compiler merges all uses of each resolved control. Event triggers require `EventSource`, `[held]` and `[idle]` require `PhysicalState`, input actions and mapping targets require `OutputDownUp`, and complete mappings require target `OutputRepeat` when the backend represents repeat explicitly. Activation compares this table with the selected backend capability catalog before hooks are installed.

### Program requirements

```cpp
struct ProgramRequirements final {
    std::uint32_t stateSlotCount{};
    std::uint32_t numberSlotCount{};
    std::uint32_t durationSlotCount{};
    std::uint32_t mappingSlotCount{};
    std::uint32_t maximumPauseRulesPerEvent{};
    std::uint32_t maximumRulesPerEvent{};
    std::uint32_t maximumTasksPerEvent{};
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

A deterministic `DumpCompiledProgram` utility prints settings, value slots, controls, requirements, event buckets, rules, mappings, expressions, actions, and source spans using stable IDs. Compiler golden tests and runtime fixture tests use this dump as the contract comparison format; it is a diagnostic format rather than a serialized executable format.

## Top-level definition

```cpp
inline constexpr std::uint32_t kCompiledProgramSchemaVersion = 1U;

struct CompiledProgramStorage final {
    std::uint32_t schemaVersion{kCompiledProgramSchemaVersion};
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

The implementation exposes this storage through a `CompiledProgram` class with const accessors returning values or `std::span<const T>`. Only `CompiledProgramBuilder` and `FinalizeCompiledProgram` may mutate storage. The runtime never receives mutable storage.

`schemaVersion` protects in-process contract assumptions and deterministic dumps. Phase 2 does not load a program produced by another executable or persist this representation to disk.

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

The compiler lowers a fully valid `BoundProgram` into mutable storage, sorts and flattens indexes, derives requirements, and calls the shared finalizer. Test fixtures use the same finalizer. Finalization validates before constructing the immutable handle and never returns a partial handle.

Compiler diagnostics describe source errors. Program validation errors describe compiler, fixture, or internal data defects and are not user-language diagnostics.

## Structural validation

`ValidateCompiledProgram` performs all of the following checks without consulting a platform backend:

- The schema version is supported and every table length is below `kInvalidProgramIndex`.
- Every ID is valid for its table or is invalid only in an explicitly optional field.
- Every range uses checked arithmetic, lies inside its owning table, and obeys the required disjointness and coverage rules.
- All strings are valid UTF-8, API-bound strings contain no embedded NUL, source offsets are within `byteLength`, and line starts are strictly increasing.
- Settings and all initial or constant values satisfy finite-number and duration invariants.
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

The validator reports all defects that can be collected safely in one pass, up to a fixed diagnostic limit. Runtime activation treats any validation error as an internal startup failure and installs no hooks.

## Activation validation

After structural validation, application activation performs checks that depend on the selected environment:

- The backend supports every `ControlRequirement` use.
- The configured task pool and publication queue satisfy `maximumTasksPerEvent`.
- Expression scratch storage satisfies `maximumExpressionStackDepth`.
- Task local and ownership pools satisfy the per-task repeat-frame and control-ownership maxima.
- Runtime value and mapping-state storage satisfy their declared counts.
- Process launch is permitted when `requiresProcessLaunch` is true.
- A command-line target override or the compiled target selector produces a valid target policy.

Activation is transactional. Runtime state is initialized and all capacity checks complete before program publication or hook installation. Failure preserves the previously active program, or leaves the application in observer mode when no program was active.

## Runtime access model

The dispatcher first searches `pauseControlBuckets` without consulting the current `PAUSE` value. A matching pause-control rule applies one synchronous effect and returns its delivery decision. When no pause-control rule matches and `PAUSE` is on, the dispatcher searches `eventBuckets`, evaluates conditions with a preallocated typed stack, scans the contiguous ordinary rule span, and writes selected action and mapping IDs into a preallocated event transaction. It reserves the full transaction before committing mapping state or returning `Suppress`.

The task scheduler stores program IDs and local positions, not instruction pointers. Holding a shared immutable program handle keeps all referenced tables alive. A program reload publishes a new handle only after old-generation tasks and mapping state have been cancelled and their outputs released.

The action VM reads strings only for `Exec` on the task thread. The hook path never dereferences the string pool or debug-name records.

## Phase 1 integration boundary

The existing `Action`, `ActionBatch`, bounded queue, self-tagged injector, target checks, and cleanup behavior remain the platform-output boundary. `CompiledProgram` does not contain `ActionBatch` records. The rule runtime translates selected mapping lifecycle operations and action VM output primitives into the existing bounded injection contract.

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

Compiler output and hand-built runtime fixtures must produce the same deterministic dump for the same semantic program. This equality is the principal convergence gate for the forked Phase 2 workstreams.
