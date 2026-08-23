# Compiler Successor Phase Implementation Plan

## Objective

This successor phase implements the complete Weave v1 source-to-`CompiledProgram` compiler against the frozen Phase 2 contract. It owns compilation and compiled-artifact emission and is complete when valid `.weave` source deterministically produces a validated immutable program and a persistent `.weavec` intermediate file, while invalid source produces bounded source diagnostics with no program or artifact.

## Plan coverage

This is the complete implementation and verification plan for the compiler successor phase, not an initial subset or an exploratory sequence. Work packages C1 through C6 cover every compiler-owned stage from source loading through persistent `.weavec` emission and the public compiler commands. Application integration, runtime execution, TUI work, and installation are outside this phase and belong to later integration work.

The work packages may be refined into implementation checklists inside this directory, but such checklists may not reduce the objective, normative inputs, semantic obligations, tests, or completion gate defined here.

## Normative inputs

- `docs/language/grammar.v1.md` is the authoritative source for Weave v1 lexical rules, grammar, type rules, source-order behavior, and user-visible runtime semantics that affect compilation.
- `development/phase-2/CompiledProgramDesign.md` is the authoritative design for every emitted table, ID, range, instruction, descriptor, requirement, debug record, and validation invariant.
- `src/program/compiled_program.*`, `src/program/program_validator.*`, and `src/program/program_dump.*` are the executable shared contract used by compiler code and tests.
- `development/OpenDesignIssues.md` records active decisions that must be resolved before affected compiler behavior can pass its completion gate.

A contradiction between the language specification and the compiled-program contract is a phase blocker. The compiler does not silently choose one interpretation, introduce a compiler-private semantic side table, or encode behavior that the runtime cannot derive from the frozen contract.

## Independence rule

This phase starts from the reviewed Phase 2 completion commit and proceeds without synchronization with the runtime successor phase. It does not wait for runtime features, consume runtime branch commits, share progress gates, or modify runtime-owned files. Tests compare compiler output with the frozen Phase 2 fixtures and dumps, not with a live runtime.

The normative Phase 2 design and frozen shared `program` implementation are dependencies. A discovered representation gap is recorded as a compiler-phase blocker for a future contract revision; this phase does not add compiler-private side tables or change the contract unilaterally.

## Owned paths

- `src/compiler/` contains source loading, diagnostics, tokens, syntax nodes, parser, bound nodes, binder, type checker, lowering, and the public compile entry point.
- `tests/compiler/` contains compiler-only unit tests, diagnostic tests, and source-to-dump golden tests.
- `script/build_compiler_tests.bat` provides an independent strict build entry point when a dedicated script is useful.
- Compiler documentation remains under `development/phase-3-compiler/`.

This phase does not modify `src/runtime/`, runtime tests, Windows hooks, input injection, scheduling, or output ownership.

## Inputs and outputs

```text
UTF-8 .weave bytes
    -> SourceFile
    -> TokenStream
    -> SyntaxTree
    -> BoundProgram
    -> CompiledProgramStorage
    -> FinalizeCompiledProgram
    -> shared_ptr<const CompiledProgram>
    -> .weavec encoder
    -> persistent .weavec file
```

A compile failure returns bounded diagnostics and no program. A lowering or finalization defect is reported as an internal compiler failure and no program.

## C1: source and diagnostics

- Load one file as bytes, validate UTF-8, preserve byte offsets, derive line starts, and retain its diagnostic display path.
- Define stable diagnostic codes, severity, primary span, message, and a bounded set of related spans.
- Reject embedded NUL where target, path, or command strings will reach a NUL-terminated platform API.
- Make source and diagnostic tests independent of Win32 and the runtime branch.

## C2: lexer

- Implement the complete Weave v1 token set with longest matching for arrows, assignment, comparison operators, and the dedicated `pause` production.
- Implement identifiers, canonical control names, number literals, exact duration literals, strings, comments, punctuation, keywords, and end of file.
- Enforce ASCII for syntax-bearing tokens and string contents while allowing valid non-ASCII UTF-8 only inside comment text; terminate line comments at a line break or end of file and reject nested block comments.
- Preserve byte spans and recover deterministically from invalid bytes, invalid UTF-8, unterminated strings, and unterminated comments.
- Bound token and diagnostic growth according to explicit compiler limits.

## C3: syntax tree and parser

- Record normative EBNF productions for the complete grammar before implementing corresponding parser productions.
- Represent settings, typed declarations, complete mappings, ordinary event rules, dedicated pause-control rules, conditions, action calls, action gaps, `if`, `repeat`, `while`, and expressions.
- Accept only `=>` or `~>` followed by exactly one `on`, `off`, or `toggle` effect in a pause-control rule; reject continuing arrows, action flows, gaps, waits, mappings, and control structures in that production.
- Encode precedence, associativity, adjacency, delimiters, and the four arrows in focused parser tests.
- Preserve source order and spans without assigning runtime table IDs.
- Recover at top-level semicolons and control-structure boundaries to report multiple bounded diagnostics.

## C4: binding and type checking

- Build scoped symbols and reject duplicate, reserved, unresolved, or conflicting names.
- Resolve controls through the shared catalog contract whose results are platform-neutral `ControlRef` values and whose event, physical-state, down/up output, and repeat capabilities correspond to `ControlRequirement` uses.
- Bind targets, defaults, built-in values, actions, state queries, physical queries, operators, and control flow.
- Enforce Boolean, state, number, and duration signatures, exact duration conversion, finite numbers, writable targets, and command string rules.
- Apply the language defaults and closed bounds for `TAP_DURATION` and `ACTION_GAP`, require declaration initializers to be same-type literals, allow Boolean reads of `PAUSE`, and reject all ordinary `set` or `toggle` writes to built-in durations and `PAUSE`.
- Bind pause-control effects as a dedicated rule class with no action flow, mapping, task, or writable-value target.
- Produce a `BoundProgram` containing resolved identities and static types but no runtime IDs or unresolved control names.

## C5: lowering

- Lower expressions to typed stack programs with forward short-circuit branches and exact maximum stack depths.
- Lower non-empty action flows to immutable action programs and leave an empty rule action as an invalid `ActionProgramId` so it creates no runtime task.
- Lower each authored `|` and `gap()` to exactly one `Gap` instruction and introduce no implicit gap between adjacent actions or control-structure boundaries.
- Lower `if` to forward branches, evaluate each `repeat` limit once into a task-local frame, re-evaluate each `while` condition at its loop header, and place `Yield` immediately before every backward action `Jump`.
- Lower complete mappings to slots, descriptors, and mapping-down rules.
- Lower pause-control statements directly to sorted `PauseControlBucket` and `PauseControlRule` tables with stop-only delivery, globally ordered source ordinals, and no `ActionProgramId`.
- Group `Down`, `Repeat`, and `Up` event rules into sorted buckets while preserving source ordinals, merge control uses, attach exact debug spans, and derive every resource requirement from the final tables.
- Canonicalize pools and indexes through the frozen builder and finalizer so semantically equivalent input construction produces the same deterministic dump.
- Finalize through the frozen Phase 2 API and treat any validation error as a compiler defect rather than a user-language diagnostic.

## C6: compiler-facing commands

- Provide compile, validate, and deterministic dump operations without installing hooks or creating runtime objects.
- Make the compile operation serialize the finalized program as a `.weavec` intermediate file that the runtime can load without reparsing `.weave` source.
- Format source diagnostics with path, line, column, source excerpt, and stable diagnostic code.
- Keep command handling behind a narrow compiler entry point so a later application or TUI integration phase can reuse it.

## Required semantic preservation

- Expression and action programs are immutable reusable code. Compilation emits no task position, repeat counter, wait deadline, cancellation generation, active mapping, mutable value, or output-ownership state.
- Each non-empty matched rule can create an independent runtime task that shares its action program while owning task-local execution state; lowering therefore uses program-local positions and repeat-frame indices only.
- Rule buckets preserve source order, the four arrows lower exactly to delivery and flow fields, and an empty action retains delivery and flow semantics without allocating a task.
- Pause-control rules preserve source order in their dedicated index, lower only `=>` and `~>` to consume or observe, and never create an action program, task, mapping, or ordinary rule.
- Every `repeat` limit is evaluated once, an integer index starts at zero, and the comparison is `index < limit`; finite `5.8` therefore represents six iterations while a non-positive limit represents zero iterations.
- Complete mappings lower to dedicated mapping descriptors, slots, and mapping-down rules rather than asynchronous ordinary press and release programs.
- Expressions use typed operands and results, forward-only expression control flow, consistent stack types at merges, and explicit physical-state reads through resolved controls.
- All strong IDs, ranges, canonical pools, source ordinals, debug spans, control requirements, and resource requirements required by the Phase 2 contract are emitted directly and remain independently recomputable by validation.
- Compilation performs no runtime capability probing, target discovery, hook installation, task creation, input injection, or process creation.

## Test matrix

- Source tests cover valid and invalid UTF-8, byte spans, line endings, diagnostic file identity, and embedded NUL policy.
- Lexer tests cover every token, longest-match boundary, comments, strings, numeric limits, duration precision, and bounded recovery.
- Parser tests cover every production, precedence level, nesting form, pause-control effect and invalid form, empty action flow, action adjacency, and recovery boundary.
- Binder tests cover every symbol rule, built-in, action signature, operator signature, target form, value type, writable-value rule, readable `PAUSE`, and rejected ordinary PAUSE write.
- Lowering tests cover every expression opcode, action opcode, rule kind, pause effect, arrow combination, mapping form, range, requirement, control-use bit, debug span table, empty-action invalid ID, task-local repeat-frame index, and yielded backward edge.
- Semantic lowering tests cover one-time repeat-limit evaluation, `5.8` producing six iterations under the contract model, non-positive repeat limits, per-iteration `while` evaluation, short-circuit expressions, and exact source-order rule buckets.
- Canonicalization tests prove that duplicate or differently ordered construction pools finalize to the same IDs and deterministic dump.
- Artifact tests prove deterministic `.weavec` emission, successful save-load equivalence, and rejection of invalid headers, truncation, corrupt fields, and trailing data.
- Golden tests compile the four Phase 2 fixture sources and match the frozen canonical dumps exactly.
- Negative end-to-end tests prove that source errors produce diagnostics and no program while internal lowering corruptions are caught by finalization.

## Open design gates

The compiler may implement work that does not depend on an active issue, but it may not declare completion while an issue in `development/OpenDesignIssues.md` can change compiler-owned behavior. `docs/language/grammar.v2.md` is successor-language design input and must not be backported by this branch without a separately reviewed contract revision.

## Completion gate

- Every valid Weave v1 syntax and semantic form compiles to a structurally valid immutable `CompiledProgram`.
- Every specified invalid lexical, syntax, naming, typing, duration, control, target, and configuration form produces bounded diagnostics and no program.
- The four canonical source files match the frozen Phase 2 fixture dumps exactly.
- Compiler tests use no hooks, `SendInput`, real time, target process, scheduler, or runtime branch code.
- Strict warnings, compiler tests, compiler `-fanalyzer`, dependency audit, and `git diff --check` pass.
- Verification evidence is recorded under `development/phase-3-compiler/` without requiring a runtime milestone.

## Handoff artifact

The completed artifact is a compiler API that accepts source and destination paths, writes a validated `.weavec` file on success, and returns bounded source diagnostics and artifact metadata. Its public boundary does not return a `CompiledProgram` for direct runtime use. A future integration phase may connect this API to the application; this phase does not invoke or wait for the runtime.
