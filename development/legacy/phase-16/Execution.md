# Phase 16: Compiler and Program Contract

## Scope and sequence

The approved language input is [Grammar.md](Grammar.md). Phase 16 implements compilation and the persistent program contract; Phase 17 implements platform-independent execution; Phase 18 implements Windows observation and injection; Phase 19 implements Debug transport and presentation. Each phase starts with a committed execution document and ends with a verified implementation commit. The syntax and semantics baseline is commit `cbaf1b5`.

## Representation

- Extend the existing event key with a named source identifier and mouse/tick transitions. Keyboard, raw mouse, and named cycle subscriptions use the same rule buckets and source ordering.
- Store each named source once: its name, mouse transition, period expression, and declaration span. The expression type selects distance, duration, or wheel accumulation; declaration order determines source identity.
- Use one typed field-reference instruction for live Mouse fields, current source fields, and completed source fields. A shared field catalog defines names, availability, and result types for binding and artifact validation.
- Use one pointer-output instruction with an operation discriminator and one or two expression operands. Add `restart(source)` as a source operation, using the declared source identity directly.
- Carry `MOUSE_IDLE_TIMEOUT` through settings and builtin duration reads, with an 80 ms default. Persist the new tables and operands in `.weavec` version 5, including deterministic dumps, validation, requirements, and source information.
- Keep mouse-specific parsing and binding in small companion files; reuse existing expression binding, instruction lowering, name checks, and source diagnostics.
- Keep the phase checkpoint buildable across program consumers: add explicit handling for the new instruction values and an activation capability gate. Phase 17 supplies field evaluation, cycle execution, and pointer output through runtime ports; Phase 18 supplies those ports for Windows.

## Language decisions

- Event names share the scalar/array declaration namespace and must precede references. An event is a named source identity; fields yield expression values and `restart(source)` acts on that identity.
- Period expressions use literals, scalar values, array reads, and existing arithmetic. Mouse/source fields and completed views belong to rule evaluation contexts. Moving sources accept number or duration periods; wheel sources accept number periods. Positive constant periods are checked during compilation; dynamic periods are checked when a period opens.
- Raw mouse and named tick sources use ordinary event rules. Tick subscriptions use observation arrows. Existing mapping, pause, and exit rule forms retain control triggers and can read the new fields in their conditions.
- `restart(source)` discards that source's progress and latest completion when the action executes. Tasks retain already selected completion snapshots. The next qualifying input establishes a new origin and latches the current period expression.
- Runtime initialization, time-boundary allocation, pointer normalization, and snapshot storage are specified in their owning execution phases before implementation.

## Implementation steps

1. Extend the shared program model, field catalog, codec, validator, dump, and derived requirements.
2. Add lexical tokens, named declarations, field references, raw/tick subscriptions, pointer actions, and restart parsing and binding.
3. Lower the new constructs through common expression and action instruction paths.
4. Add positive compiler scenarios covering raw mouse rules, dynamic number/duration periods, both field views, all Mouse fields, source restart, pointer actions, and artifact round trips. Keep new test cases in a focused companion file.
5. Verify compiler and language builds/tests, program round trips, and dependency boundaries. Record the results before the phase completion commit. Runtime/backend verification follows as those phases implement the new contract.

## Verification commands

```bat
script\build_compiler.bat
script\build_compiler_tests.bat
script\test_compiler.bat
script\build_language_tests.bat
script\test_language.bat
script\build_executor_tests.bat
script\test_executor.bat
script\audit_dependencies.bat
git diff --check
```

The final cross-phase gate is `script\verify_project.bat`, including all products, tests, analysis, and dependency checks. Product documentation is updated with the executable behavior as the owning phases complete.
