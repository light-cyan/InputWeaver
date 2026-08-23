# Phase 3 Compiler Open Questions

## Purpose

This file records active ambiguities, decisions that require shared agreement, and implementation concerns discovered while building and verifying the Phase 3 compiler. It does not override the language specifications, the compiled-program contract, or the phase plan.

When a question affects the shared `program` contract, its validator, codec, fixtures, or deterministic hashes, the compiler work stops at reporting the issue until a decision is made jointly. A resolved decision belongs in its authoritative specification, design, or tests and should then be removed from this file.

## PCQ-001: Precise debug spans versus frozen Phase 2 fixture dumps

- Status: Shared decision required; this blocks the exact frozen-dump completion gate.
- Current evidence: The Phase 2 fixtures assign the whole source-file span to retained target, rule, mapping, variable, expression, action, and instruction records, while C5 requires exact debug spans and the compiler emits precise authored regions.
- Consequence: The four compiled sources match the fixture semantics and table topology after source spans are normalized, but their complete deterministic dumps and hashes cannot match while both span policies remain in force.
- Additional ambiguity: The exact span policy for synthetic branch, loop, yield, return, and end instructions is not frozen independently of the hand-built fixtures.
- Suggested direction: Keep precise authored spans, define spans for synthetic instructions explicitly, and update the shared fixtures and hashes only after joint approval.
- Protected scope: No file under `src/program/` or `tests/program/` has been changed for this issue.

## PCQ-002: Authority for the Weave v2 compiler scope

- Status: Scope confirmation required before declaring the phase complete.
- Current evidence: The task supplies `grammar.v2.ebnf` and requests review of the v2 documents, while the starting Phase 3 plan at commit `dad2f4b` described a v1-only compiler and required a reviewed plan revision before adopting v2 syntax.
- Current implementation: The compiler and the revised phase plan implement the complete v2 grammar, including qualified controls, aliases, raw controls, and dedicated pause-control rules.
- Decision needed: Confirm that the task instruction and supplied EBNF constitute the reviewed scope change from v1 to v2; otherwise the plan and completion gate must be restored to the intended scope without changing the shared program contract.

## PCQ-003: Authoritative string escape semantics

- Status: Language decision required.
- Current evidence: The language examples contain escaped backslashes, but the specifications do not define a complete escape set, whether string values are cooked or raw, or how an unknown escape is interpreted.
- Current implementation: The lexer decodes `\\`, `\"`, `\n`, `\r`, and `\t`, rejects every other escape, requires decoded string contents to be ASCII, and rejects embedded NUL.
- Decision needed: Freeze the accepted escape set and decoded-value rules in the language specification so target names, paths, and `exec` commands have one portable compiler/runtime representation.

## PCQ-004: Numeric domains for raw controls

- Status: Language and control-contract decision required.
- Current evidence: The v2 grammar says that out-of-range raw-control arguments are invalid but does not give exact numeric bounds or say whether zero, reserved values, and currently unassigned native codes are valid source identities.
- Current implementation: HID pages use `1..0xFFFF`, HID usages use `0..0xFFFF`, Windows virtual keys and scan codes use `0..0xFF`, Linux event codes use `0..0x2FF`, and macOS key codes use `0..0xFFFF`; scan prefixes are `none`, `E0`, or `E1`.
- Decision needed: Define whether compilation validates only stable storage/native numeric domains or also validates assigned-code catalogs that can vary by platform version.
- Suggested direction: Validate stable numeric domains in the compiler and leave availability or native support to activation, while documenting any universally invalid sentinel values explicitly.

## PCQ-005: Negative results from duration scaling

- Status: Cross-authority semantic decision required.
- Current evidence: The language rule says that a duration operation producing a negative result saturates to `0ms`; the compiled-program design explicitly states clamping for duration subtraction but describes multiplication and division without stating whether negative scaling also clamps.
- Current implementation: Constant duration subtraction, multiplication, and division all saturate negative results to zero, following the language rule.
- Decision needed: Confirm that runtime duration multiplication and division use the same saturation rule, then align the owning language and compiled-program descriptions.

## PCQ-006: Constant faults on a short-circuited expression branch

- Status: Language diagnostic decision required.
- Current evidence: The compiled-program contract requires `and` and `or` short-circuit execution and requires constant expressions that would fault to be rejected, but it does not state whether an unreachable constant subtree is considered faulting for compilation.
- Current implementation: Binding diagnoses a constant-faulting subtree even when a compile-time constant left operand makes that subtree unreachable, such as division by zero on the right side of a false `and`.
- Decision needed: Choose between rejecting every constant-faulting subtree or applying compile-time reachability consistent with runtime short-circuit behavior.
- Suggested direction: Apply short-circuit reachability so a fault is diagnosed only when constant evaluation proves that the faulting branch is evaluated.

## PCQ-007: Terminal line-start entry after a final line break

- Status: Shared source-metadata decision required because it can change deterministic dumps.
- Current evidence: The compiled-program design can be read as requiring the byte after every recognized line break in `lineStarts`, including the end-of-file byte after a terminal line break, while the Phase 2 fixtures omit that terminal entry.
- Current implementation: The compiler mirrors the fixtures and omits an end-of-file line-start entry after a final line break.
- Decision needed: Freeze whether the empty logical line after a terminal line break is represented, then align source diagnostics, shared fixtures, validation expectations, and hashes.

## PCQ-008: Empty target and `exec` strings

- Status: Language ownership decision required.
- Current evidence: The language describes target names, executable forms, paths, and command strings but does not say whether an empty NUL-free string is a compile-time error or an activation/process-launch failure.
- Current implementation: The compiler accepts empty strings and preserves them in the compiled artifact; downstream target resolution or process launch determines whether they are usable.
- Decision needed: Define any universally invalid empty forms in the language specification, while retaining platform-specific resolution outside the compiler.

## PCQ-009: Windows command-line path encoding

- Status: Compiler interface decision required before the CLI is treated as a production Windows boundary.
- Current evidence: The public compiler API uses `std::filesystem::path`, but the standalone CLI currently enters through `main(char**)` and interprets argument bytes as UTF-8 when constructing paths.
- Concern: Windows does not guarantee that narrow process arguments are UTF-8, so valid non-ASCII source or artifact paths can be misdecoded independently of source-file UTF-8 rules.
- Suggested direction: Use a native wide Windows entry point for CLI paths while keeping source contents UTF-8 and keeping the reusable compiler API based on `std::filesystem::path`.

## PCQ-010: Recovery at nested control-structure boundaries

- Status: Compiler implementation concern; no shared decision is required unless the C3 gate is intentionally narrowed.
- Current evidence: The parser recovers invalid top-level items at semicolons, but it does not yet guarantee multiple independent diagnostics within the same malformed `if`, `repeat`, or `while` structure.
- Required work: Add deterministic recovery at nested action and control-structure boundaries, then add bounded multi-error tests before claiming full C3 completion.
