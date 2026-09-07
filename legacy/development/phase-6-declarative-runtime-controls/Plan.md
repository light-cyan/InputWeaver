# Phase 6: Declarative Runtime Controls

## Objective

Phase 6 makes executor exit behavior part of the compiled Weave program and removes the runtime-only exit recognizer. It also establishes that PAUSE controls are optional and changes the full-lifecycle mapping operator to `->`.

## Language contract

- A full mapping uses `source -> target [ when condition ];`.
- An exit control uses `exit event [ when condition ];`.
- Exit conditions must be Boolean and may use the same side-effect-free expressions as other rule conditions.
- Multiple explicit exit rules are evaluated in source order.
- If at least one explicit exit rule exists, the compiler emits only the authored exit rules.
- If no explicit exit rule exists, the compiler emits a default rule equivalent to `exit F12:down when (LCtrl[held] or RCtrl[held]) and (LShift[held] or RShift[held]);`.
- PAUSE rules are optional. An empty PAUSE rule set remains empty in the compiled program.

## Compiled-program contract

- Exit event buckets and exit rules are immutable compiled-program tables.
- Exit predicates, source order, source spans, activated controls, and exact per-event requirements are validated and serialized.
- The artifact format identifies this table layout so artifacts using another layout are rejected before activation.
- An accepted compiled program always contains at least one exit rule because the compiler synthesizes the default when necessary.

## Runtime contract

- Physical state is updated before exit predicate evaluation.
- Exit rules are evaluated before target eligibility, pointer routing, PAUSE controls, mappings, and ordinary rules.
- Injected input cannot match exit rules.
- A matching exit rule consumes its event, invalidates active work, releases mappings and owned outputs, and requests executor shutdown.
- Runtime control activation and startup state seeding are derived from compiled control requirements.
- When PAUSE control tables are empty, event dispatch bypasses PAUSE lookup and its synchronization path while `PAUSE` remains `on`.

## Verification

- Compiler tests cover explicit exit rules, default synthesis, Boolean checking, source order, optional PAUSE rules, `->`, and rejection of `:=`.
- Compiled-program tests cover exit-table validation, deterministic encoding, artifact format identification, and exact derived requirements.
- Runtime tests cover exit precedence over target and PAUSE routing, physical modifier state, cancellation, output release, capacities, and optional PAUSE behavior.
- Windows adapter tests cover compiled exit activation and startup state seeding without a dedicated hard-coded recognizer.
- The combined build, test, static-analysis, dependency, and diff gates must pass before Phase 6 is delivered.
