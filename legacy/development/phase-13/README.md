# Phase 13: Weave V3 Arrays and Shared Lexical Analysis

## Objective

Phase 13 implements the source-language contract in [GrammarV3.md](GrammarV3.md) across lexical analysis, compilation, the WEAVEC artifact, runtime state, debugging, editor highlighting, tests, build scripts, and current documentation.

The implementation adds state and number arrays, equality-based state and control-state expressions, array reads in every expression context, array mutation actions, and floor-normalized repeat limits. It also leaves exactly one character-level Weave scanner in the repository.

## Architectural Constraints

- Weave continues to define expressions and actions; Phase 13 does not introduce user functions or predicate functions.
- Arrays are storage objects with stable `ArrayId` identities. They do not enter `ValueRef`, which remains the scalar-value reference model.
- Array element types are limited to `state` and `number`. Scalar `duration` remains supported, while no duration-array storage or opcode is added.
- Array indexes and lengths use `number`; no integer or index type is introduced.
- Existing event dispatch, task scheduling, cancellation, output ownership, and physical-input synchronization remain the execution model.
- Existing `variableMutex` and `pumpLock` provide array synchronization and single-writer task execution. No second state lock, array worker, or parallel action executor is added.
- The debug STATE model represents every array by name, exact logical length, and a bounded value preview. Debug transport and rendering remain bounded independently of array length.
- V1 and V2 artifact decoding remains supported and covered by tests. Compatibility branches that are exercised by those decoders are retained; unused source-language and editor compatibility aliases are removed.

## Dependency Boundary

Add a platform-independent `src/language/` module that depends only on the C++ standard library. `compiler` and `ui/tui` depend on `language`; `language` does not depend on `compiler`, `program`, `runtime`, or UI code.

The resulting relevant dependency edges are:

```text
compiler -> language + program
program  -> standard library
runtime  -> program + input
debug    -> runtime + program + input
ui/tui   -> language + app + debug
```

Only the following production files are added:

```text
src/language/lexer.hpp
src/language/lexer.cpp
src/language/word_catalog.hpp
src/runtime/array_storage.hpp
src/runtime/array_storage.cpp
```

`word_catalog.hpp` is header-only because it contains one small immutable catalog and lookup helpers. Array mechanics are implemented once through a typed paged-storage template in `array_storage`, not duplicated for state and number arrays.

## Shared Language Module

### `src/language/lexer.hpp` and `lexer.cpp`

Define `LexemeKind`, `LexemeSpan`, `Lexeme`, `LexicalIssueCode`, `LexicalIssue`, and `LexerState`. `LexerState` carries block-comment nesting recovery and the opening position across editor lines.

Expose one scanner that accepts a source slice, a base byte offset, and an initial state, then returns lossless lexemes, lexical issues, and the final state. Whole-source compiler scanning and line-oriented editor scanning call this same implementation.

Emit whitespace, line comments, block comments, words, numbers, durations, hexadecimal integers, strings, every punctuation and operator token, invalid tokens, incomplete tokens, and end of input. Every invalid path advances at least one byte.

Keep raw source spans in lexemes. Provide the string-literal decoder beside the scanner so escape validation and decoding have one implementation. Number and duration value conversion remains semantic compiler work because it depends on target value ranges rather than token boundaries.

The language scanner owns lexical correctness for ASCII syntax, non-ASCII comment content, string escapes, number boundaries, duration suffixes, hexadecimal spelling, nested block-comment diagnostics, and incomplete input. It owns no compiler limits and emits no compiler diagnostics.

### `src/language/word_catalog.hpp`

Define one immutable catalog for settings, types, constants, event transitions, structural keywords, actions, intrinsic values, raw-control constructors, scan prefixes, and the contextual `length` property.

Classify `on`, `off`, `held`, and `idle` as constants. Classify action names and the `Pipe` lexeme as actions. Classify rule arrows for the brighter operator color. Parentheses, expression operators, and other delimiters have no semantic color role.

Expose `LookupWordRole` and `IsReservedLanguageWord`. The compiler reserved-name check combines this result with compiler-owned named-control reservations; the highlighter consumes the same roles directly.

## Compiler Module

### `src/compiler/compiler.cpp` and `frontend.hpp`

Replace the public `LexSource` plus `ParseTokens` sequence with one compiler-facing `ParseSource` entry point. `ParseSource` invokes the shared scanner, maps lexical issues to compile diagnostics, enforces the significant-token limit, and invokes the parser.

Compiler tests stop calling `LexSource`; scanner behavior moves to `tests/language/lexer_tests.cpp`. This prevents a compiler adapter API from becoming a second lexical contract.

### `src/compiler/frontend.cpp`

Make the parser consume shared lossless lexemes directly. Its cursor skips whitespace and comments centrally, so it does not allocate a second filtered token vector.

Read word and numeric spelling from the source span. Decode strings through the shared string decoder. Replace the lexical arrow stored in syntax with a compiler-owned semantic `RuleArrowSyntax` enum so later compiler stages do not depend on lexical kinds.

Parse `state[]` and `number[]` declarations, typed array literals, array-element expressions, `.length`, and structured writable targets. Initial array elements remain literals rather than general expressions.

Parse bare references without prematurely deciding whether they are scalar variables or controls. Parse unqualified array names and indexes structurally; the binder resolves storage and control identity.

Parse `on` and `off` as state constants and `held` and `idle` as control-state constants. A bare control reference becomes an expression. Bracket syntax is exclusively array access, so the parser contains no state-query production.

Parse `set` and `toggle` through a reusable `TargetSyntax` containing either a scalar name or an array name plus index expression. Parse `append`, `pop`, and `clear` with their grammar-specific operands. Keep `|` and `gap()` as the two spellings of `ActionSyntax::Kind::Gap`.

Delete the embedded `Lexer`, `SkipTrivia`, `LexToken`, compiler `Token`, compiler `TokenKind`, token-text copies, and all lexical character helpers from `frontend.cpp` and `syntax.hpp` after migration.

### `src/compiler/syntax.hpp`

Add state-array and number-array declaration kinds, an array-literal element record, a reference expression, a control-state literal, array-element and array-length expression kinds, `TargetSyntax`, and the new array action kinds.

Remove `ExpressionSyntax::Kind::StateQuery`, `querySubject`, and lexical `TokenKind` storage. Syntax nodes retain only source structure needed by binding.

### `src/compiler/bound_program.hpp` and `semantics.cpp`

Represent each declared symbol as either a scalar `ValueRef` or an `ArrayId` with an element type. Assign array IDs in declaration order and store initial elements once in the bound program.

Add compiler expression type `ControlState`, control-state constants, control-state reads, array-length loads, and array-element loads. `combat == on`, `A == held`, `A == idle`, and `A == B` bind through ordinary same-type equality.

Resolve an unqualified reference to a declared scalar first and otherwise as a named control. Resolve dotted and raw-control references as controls. Reject a bare array value and require either indexing or `.length`.

Require every array index to have type `number`. Require `set` and `toggle` array targets to have matching element types, require `append` values to match the array element type, require `pop` to target a same-typed writable scalar, and require `clear` to name an array.

Use `IsReservedLanguageWord` plus `IsReservedControlIdentifier` for declarations. Add no compiler-local keyword array.

Delete `BindStateQuery`, `MakeHeldExpression`, the `StateQuery` binding branch, and the old duplicated reserved-language list. Replace default-exit construction with ordinary control-state equality expressions.

### `src/compiler/lowering.cpp`

Lower arrays by direct `ArrayId`; do not add an interned array-reference table. Copy array descriptors and initial pools into `CompiledProgramStorage` in declaration order.

Lower control-state constants and reads to V3 expression instructions. Lower array indexes before `LoadArrayElement`, lower `.length` directly, and include array control-state reads in derived control requirements.

Lower array actions with direct array IDs and expression IDs. Expand `ActionInstruction` to three operands so element set can carry array ID, index expression ID, and value expression ID without adding a sparse action-descriptor table.

Keep the existing `Gap` opcode for both source spellings. Normalize repeat limits in the runtime repeat frame rather than adding a new opcode or precomputing dynamic expressions in the compiler.

## Program Module

### `src/program/compiled_program.hpp` and `compiled_program.cpp`

Add `ArrayId`, `ArrayElementType`, `ArrayDescriptor`, typed initial state and number pools, and `ArrayDebugRecord`. An array descriptor stores its element type and a range into the matching initial-value pool.

Add `ExpressionType::ControlState`, `PushControlState`, `ReadControlState`, `LoadArrayLength`, and `LoadArrayElement`. Rename the existing physical-state opcode in place so its numeric position remains stable; V3 instructions return `ControlState`, while decoded V1 and V2 Boolean instructions retain their legacy result type.

Add array-element set, array-element toggle, append, pop, and clear action opcodes. Add `operand2` to `ActionInstruction`; V1 and V2 decoding initializes it to zero.

Add array count and initial array-element bytes to `ProgramRequirements` as a pre-allocation lower bound. Runtime activation separately accounts for the exact allocated pages and page-directory storage. Add getters for descriptors, initial pools, and array debug records.

Keep scalar `UserValueLayout`, `ValueRef`, and scalar debug records unchanged in purpose. Do not add array domains to `ValueDomain`, array cases to `ValueType`, or duration-array tables.

Canonicalization preserves declaration-order `ArrayId` values and canonicalizes only referenced tables that already require remapping. It updates array-referencing instructions only if a future canonicalization rule explicitly remaps arrays.

### `src/program/program_validator.cpp`

Validate array element types, initial ranges, complete pool coverage, state bytes, finite number values, unique IDs, debug records, and all new table limits.

Validate expression stack behavior for control-state and array operations. Require array-result types to match descriptor element types and require index operands to be numbers.

Validate every array action operand, expression ID, scalar pop target, writable target, and unused operand field. Include array expression steps in predicate limits and derive the new requirements from final tables.

Accept the legacy Boolean result form of the renamed physical-state instruction for decoded V1 and V2 programs. V3 compiler and fixture tests require the control-state form.

### `src/program/weavec_codec.cpp`

Add WEAVEC V3 magic and encode only V3. Decode V1, V2, and V3 through shared field helpers rather than copied version-specific decoders.

Encode and decode new requirements, array descriptors, initial pools, array debug records, control-state opcodes, array opcodes, and the third action operand. Version branches are limited to fields whose wire layouts differ.

Retain V1 and V2 magic, old instruction-width decoding, and existing V1 debug-record handling because they remain tested compatibility paths. Remove no compatibility code that still has a golden decode test.

### `src/program/program_dump.cpp`

Print array requirements, descriptors, initial values, debug names, control-state instructions, array instructions, and all three action operands deterministically.

## Runtime Module

### `src/runtime/array_storage.hpp` and `array_storage.cpp`

Implement one `PagedArray<T>` template used by state and number arrays. Fixed-size pages keep existing-element addresses stable and make reads, writes, pop, clear, and most appends allocation-free.

Implement one `RuntimeArrayStorage` wrapper that owns the element type, logical length, page directory, allocation charge, and typed element access. It converts between typed elements and `RuntimeValue` without duplicating action logic.

Normalize indexes in one helper: require a finite nonnegative number, apply `floor`, convert to the host index type, and then perform the logical-length bounds check. Return a precise normalization or bounds result to both expression evaluation and actions.

Prepare any required element page and expanded page directory before taking `variableMutex`. Commit the prepared storage and logical length under the exclusive lock, so array growth performs no allocation while mutable state is locked.

Retain allocated pages after pop and clear. Reuse them on later append and release them only when the active program state is destroyed.

### `src/runtime/expression_vm.hpp` and `expression_vm.cpp`

Extend `RuntimeExpressionState` with a read-only span of runtime arrays. The caller continues to own locking.

Evaluate control-state values with their own expression type. Add same-type control-state equality while retaining legacy Boolean physical-state instructions for V1 and V2 artifacts.

Evaluate array length without allocation. Evaluate an array element by popping its number index, calling the shared normalization helper, and pushing a typed scalar value.

Add runtime evaluation faults for invalid array identity, invalid array index, and array bounds. Existing predicate and task-expression fault paths publish the source instruction span.

### `src/runtime/runtime_types.hpp`

Add `maximumArrayCount` and `maximumArrayBytes` to `RuntimeCapacities`, matching activation error subjects, and array-capacity diagnostics. Capacity accounts for allocated pages and page-directory storage rather than logical element count alone.

Add current array bytes, peak array bytes, and rejected array growth to `RuntimeMetrics` and its atomic counters.

Add `RuntimeDebugArrayElement` and `RuntimeDebugArraySnapshot` beside scalar `RuntimeDebugValue`. A snapshot carries `ArrayId`, element type, exact logical length, prefix count, suffix count, and a fixed array of eight element slots. Arrays of at most eight elements use all elements as one prefix; longer arrays use the first four and last four elements.

Add `RuntimeDebugEventKind::ArrayChanged` and carry one complete bounded snapshot in the event. The event and snapshot remain trivially copyable and allocation-free so runtime publication can continue through the existing bounded debug event path.

### `src/runtime/program_runtime.cpp`

Construct runtime arrays from compiled descriptors during activation, enforce array count and byte capacities, and reject activation atomically on malformed storage, capacity excess, or allocation failure.

Store arrays beside scalar vectors in `MutableState` and protect both with the existing `variableMutex`. Input dispatch already acquires one shared variable lock for an entire physical event, so all scalar and array predicates observe the same event-start state without copying arrays.

Task expression evaluation continues to take the shared variable lock. Scalar and array mutation actions take the exclusive variable lock. The existing `pumpLock` guarantees that task slices are the only array writer, so no array-local mutex or atomic element storage is added.

Element set evaluates the index and value once under the exclusive lock and commits one element. Toggle performs the same operation for state elements. Pop updates logical length and the scalar destination atomically under one lock. Clear updates logical length under one lock.

Append evaluates its value once. If a page is required, it prepares the page, rechecks task generation before commit, and either commits the complete append or leaves the array unchanged.

An empty pop, invalid mutation index, capacity rejection, or allocation failure leaves all storage unchanged, emits an array action diagnostic, fails the current task, and releases that task's outputs through the existing finish path. It does not invalidate unrelated tasks or the active program generation.

After every successful set, toggle, append, pop, or clear, build one bounded array snapshot while holding the existing exclusive `variableMutex`, release the lock, and publish one `ArrayChanged` event. The snapshot therefore describes the committed mutation, including middle-element changes that do not affect length. Failed actions publish no changed value.

Normalize a repeat frame limit once with `max(0, floor(limit))` in `RepeatInit`. Keep the existing repeat frame and check opcodes; only the stored limit changes, so no new repeat instruction or integer source type is added.

## Debug and Application Modules

### `src/platform/windows/debug/debug_server.cpp`

Add array descriptors and an `ArrayId` to array-index map beside the existing scalar state descriptors. Initialize one bounded snapshot per array from the compiled descriptor and initial-value pool, and include named array snapshots in `CaptureStarted`.

Keep the current array snapshot even while capture is inactive, matching the existing scalar-state behavior. `Publish(ArrayChanged)` validates the ID and element type, commits the complete bounded snapshot to the cache, and enqueues an array-change record only for an active capture.

Store each cached snapshot in fixed atomic fields guarded by a per-array sequence counter. The task thread writes an odd sequence, the fields, and then an even sequence; the debug writer retries until it reads the same even sequence before and after the fields. This keeps publication nonblocking without adding a debug mutex to array actions.

Count array names and the fixed maximum snapshot encoding in `ProgramFitsProtocol`. No protocol calculation depends on logical array length.

### `src/debug/debug_protocol.hpp` and `debug_protocol.cpp`

Raise the protocol version to 4. Add `DebugArrayElement`, `DebugArrayValue`, and `DebugNamedArray`; add named arrays to `CaptureStartedPayload`; and add `MessageKind::ArrayChanged` with an array index and one `DebugArrayValue`.

Encode the element type, exact logical length, prefix count, suffix count, and only the populated preview elements. Decode with fixed limits and reject invalid element types, counts above eight, inconsistent prefix and suffix counts, short-array previews that omit elements, and previews whose represented positions exceed the logical length.

Keep scalar `DebugValue`, `DebugNamedValue`, and `StateChanged` unchanged. Arrays remain a distinct protocol value rather than being expanded into synthetic scalar names.

### `src/debug/debug_client.hpp` and `debug_client.cpp`

Add `DebugArrayState` and an `arrays` vector to `DebugClientState`. Initialize it from `CaptureStarted`, apply `ArrayChanged` by array index, and use the existing stream-loss and capture-restart behavior to replace stale state with the next complete capture snapshot.

Update source-text fixtures from bracket predicates to equality expressions.

### `src/app/`

Keep application state and platform ports unchanged. The immutable debug client state already crosses this boundary as a whole, so the application does not need a second array cache or an array-specific port.

## TUI and Resource Modules

### `src/ui/tui/support/source_highlighter.hpp` and `source_highlighter.cpp`

Replace `SourceTokenKind::Function` with `Action`. Keep contextual roles for keyword, type, declared storage, constant, control, action, operator, string, and comment.

Call the shared language scanner for every line and classify its lexemes. Retain only contextual work: declaration-name tracking, scalar versus array storage names, dotted control assembly, and the `.length` property.

Render `on`, `off`, `held`, and `idle` as constants. Render known action names and `|` as actions. Render the contextual `length` property as a variable. Leave parentheses, brackets, commas, colons, semicolons, dots, and expression operators unclassified so they use ordinary source text. Render rule arrows with the brighter operator color.

Delete keyword, type, intrinsic, constant, and arrow tables from the highlighter. Delete `IsWordStart`, `IsWordCharacter`, number scanning, string scanning, comment scanning, arrow scanning, dotted-name character scanning, and the call-shaped-name heuristic.

Keep the current renderer traversal that establishes lexical and declaration state from the beginning of the document before visible lines. Token caching is a separate optimization and is not added in Phase 13.

### `src/ui/tui/support/color_scheme.hpp`, `color_scheme.cpp`, and `res/InputWeaverTUI.colors.json`

Rename `syntaxFunction` to `syntaxAction` and `syntax_function` to `syntax_action`. Remove the old member, key, parser entry, and resource entry without an alias.

### `src/ui/tui/tui_renderer.cpp`

Map `SourceTokenKind::Action` to `syntaxAction`.

Render each debug array in the STATE grid after scalar variables and before pressed controls. Use one cell per array: `[values=[]]` for an empty array, `[values=[2, 4, 8]]` when the entire array fits in the preview, and `[values=[0, 1, 2, 3, ..., 996, 997, 998, 999] length=1000]` for a longer array. Format state and number elements with the existing scalar value rules, and apply the existing cell-width clipping after constructing the bounded text.

## Windows Runtime and Diagnostics

`src/platform/windows/runtime/` keeps the current hook, routing, control binding, and session assembly. Control-state expressions continue to contribute `ControlUse::PhysicalState`, so initial physical-state query requirements remain unchanged.

Add array activation failures and array action diagnostics to `src/platform/windows/diagnostics/diagnostic_log.cpp` and its tests. The generic JSONL record remains bounded and carries array ID, instruction position, and failure detail through existing fields.

## Build, Audit, and Documentation

Add `tests/language/lexer_tests.cpp`, `script/build_language_tests.bat`, and `script/test_language.bat`. Register them in `build_tests.bat` and `test.bat`.

Add language sources to compiler, compiler-test, TUI, and app-test build commands. Add runtime array storage to runtime-test, executor-test, and executor builds. Add new program sources only if implementation requires them; the planned program changes remain in existing files.

Update `script/audit_dependencies.bat` for the `language` boundary and update `AGENTS.md` dependency and layout sections when the module exists.

When implementation is complete, promote `GrammarV3.md` to `docs/grammar.md`, update `docs/safety-guide.md` to equality-based control state, update `docs/runtime-boundaries.md` with array capacities, and update `docs/tui-guide.md` with action coloring and ordinary delimiter coloring.

## Test Ownership

### Language tests

Cover every lexeme kind and exact byte span, ASCII boundaries, valid and invalid numeric boundaries, duration suffixes, hexadecimal restrictions, string escapes and decoding, line and block comments, nested-comment recovery, incomplete editor tokens, invalid bytes, cross-line state, and guaranteed forward progress.

Feed the same source corpus to whole-source and line-oriented scanner entry points and require identical non-whitespace token spans.

### Compiler tests

Cover array declarations and literals, empty arrays, declaration order, array access and length, storage-name resolution, `combat == on`, `A == held`, `A == idle`, `A == B`, writable targets, every array action type rule, removed bracket predicates, short-circuit guards, and floor-normalized repeat lowering.

Cover diagnostics for duration arrays, mixed initializers, bare array values, unknown arrays, scalar indexing, non-number indexes, read-only and mismatched pop targets, and reserved `length`, `append`, `pop`, and `clear` names.

### Program tests

Cover V3 canonicalization, validation, deterministic dumps, codec round trips, action operand three, array range coverage, malformed descriptors, non-finite initial numbers, invalid expression and action operands, derived requirements, and V1/V2 golden decode compatibility.

### Runtime tests

Cover activation capacities, initial arrays, length and element expressions, negative and fractional indexes, out-of-range faults, short-circuit avoidance, same-event snapshots, page boundaries, retained pages after clear and pop, append rollback, empty pop, atomic pop-to-scalar, set and toggle, growth rejection, allocation failure, task failure cleanup, and concurrent hook fail-open behavior.

Cover repeat limits below one, integral limits, positive fractional limits, and evaluation exactly once at loop entry.

### Debug, Windows, and TUI tests

Cover empty, short, and long initial array snapshots; set and toggle updates that retain length; append, pop, and clear updates; state and number elements; capture started after mutations; capture restart; cache sequence consistency; protocol V4 round trips and malformed previews; debug capacity accounting independent of logical array length; STATE array cells and clipping; JSON diagnostics; action-name and `|` coloring; ordinary parenthesis and bracket coloring; constant coloring for all four constants; array declarations; `.length`; dotted controls; invalid tokens; and multiline comments.

Update active source-text fixtures throughout compiler, debug, runtime, TUI, safety documentation, and validation assets to V3 equality syntax. Archived development material remains untouched.

## Removal Contract

The phase is not complete while any of the following active implementation remains:

- The compiler-local `Lexer`, `LexSource`, `Token`, `TokenKind`, `SkipTrivia`, or lexical character-scanning helpers.
- Highlighter character scanning, duplicate language-word tables, `SourceTokenKind::Function`, `syntaxFunction`, or `syntax_function`.
- `ExpressionSyntax::Kind::StateQuery`, `BindStateQuery`, `querySubject`, or the `ReadControlHeld` symbol name.
- Active source examples or tests using `control[held]`, `control[idle]`, `value[on]`, or `value[off]`.
- Array data copied into `ValueRef`, a parallel array expression evaluator, separate state and number array algorithms, duration-array placeholders, or a second array lock.
- Synthetic `.length` debug variables, per-element debug variables, full-array debug messages, or a second array copy owned by the application.
- A V2 encoder path, unused color-key aliases, migration-only adapters, temporary comparison scanners, or compatibility branches without a decoding test.

V1 and V2 decoder branches, magic values, and legacy Boolean physical-state instruction handling remain because they have explicit compatibility tests and executable behavior.

## Implementation Order and Gates

1. Add the language scanner and word catalog, migrate the V2 compiler and highlighter to them, rename action coloring, delete both old scanners, and pass language, compiler, and TUI tests before adding V3 syntax.
2. Add the V3 program model, validator, codec, dump, fixtures, and compatibility decoding; pass program tests before allowing the compiler to emit V3.
3. Add V3 compiler syntax, binding, type checking, lowering, diagnostics, and source tests; remove bracket-predicate compiler paths and pass compiler tests.
4. Add runtime array storage, expression operations, mutation actions, repeat normalization, capacities, metrics, and fault handling; pass runtime tests under both manual pumping and the task thread.
5. Add bounded array snapshots to runtime debug events, the V4 debug protocol, the Windows debug cache, the client reducer, STATE rendering, Windows diagnostics, TUI coverage, build-script registration, dependency rules, and current documentation.
6. Run repository-wide stale-symbol searches, remove every item in the removal contract, then run all focused suites and cross-cutting verification.

Use the following verification commands:

```text
script/build_language_tests.bat
script/test_language.bat
script/build_compiler.bat
script/build_compiler_tests.bat
script/test_compiler.bat
script/build_runtime_tests.bat
script/test_runtime.bat
script/build_executor.bat
script/build_executor_tests.bat
script/test_executor.bat
script/build_tui.bat
script/build_app_tests.bat
script/test_app.bat
script/verify_project.bat
```

## Completion Criteria

Phase 13 is complete when the shared scanner is the only character-level Weave tokenizer, compiler and editor spans come from that scanner, V3 arrays and equality expressions work through compiled artifacts and runtime execution, STATE displays every scalar and array variable with current bounded array previews, debug and documentation expose the current behavior, V1 and V2 artifacts still decode, all removal-contract searches are clean, and project verification succeeds.
