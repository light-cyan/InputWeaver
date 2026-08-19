# UniversalKeyRemapper

## Project Overview

UniversalKeyRemapper is a planned Windows key and mouse remapping utility written in C++. It is intended to run with administrator privileges when required by a target application and to read user-defined `.krm` mapping configurations. The original mapping-language design, including key states, actions, sequences, variables, and mouse events, is retained in `legacy/OriginalDesign.md` as historical reference material.

The repository also retains `legacy/MouseHookPrototype.cpp`, an unsuccessful exploratory mouse-hook prototype that is not part of the application implementation. Reserved directories are `src/` for application source code and `script/` for build and run batch files, `res/` for runtime resources, and `bin/` for generated executables.

## Environment

- Platform: Windows.
- Compiler: MinGW-w64 GCC and G++ 15.1.0 are available on `PATH`.
- Language: C++.
- Dependencies: use only the C++ standard library and the Windows API; do not introduce third-party libraries.
- Git: this repository is initialized for Git-based version control. Inspect `git status` before changing files and do not commit or push unless explicitly requested.

## Build and Verification

- Keep the canonical build and run commands in batch files under `script/` when the application source is added.
- Place generated executables and other build output under `bin/` and keep them out of version control.
- Compile with `g++` and verify a successful build after changing C++ source code.
- Use `legacy/OriginalDesign.md` as historical reference for the `.krm` language; establish a separate current specification when implementation begins.

## Agent Coordination

- `AGENTS.md` is the persistent agent-to-agent shared message channel for all agents working on this repository.
- Read this file before starting work and use it to preserve repository-wide decisions, active implementation status, completion gates, and handoff information for later agents.
- Add concise durable handoff notes under `Shared Agent Messages` and preserve unresolved notes written by other agents.
- Keep detailed design and verification records in their linked documents; keep the shared status here concise and authoritative.
- Update a phase status when work starts or its completion gate is satisfied, but do not mark partially implemented or unverified work as complete.

## Implementation Status

### Phase 1: Input Loop and Self-Injection Isolation

- Status: `Planned`.
- Plan: `docs/phase-1/ImplementationPlan.md`.
- Code design: `docs/phase-1/CodeDesign.md`.
- Scope: low-level keyboard and mouse hooks, origin classification, self-tagged `SendInput`, recursion prevention, bounded diagnostics, an input probe, and Phase 1 verification without the `.krm` parser.
- Start rule: change the status to `In Progress` when Phase 1 source implementation begins.
- Completion rule: change the status to `Complete` only after the plan's completion gate is satisfied, then add the completion date and a link to `docs/phase-1/Verification.md`.

## Shared Agent Messages

- 2026-08-19 | Phase 1 design | `docs/phase-1/ImplementationPlan.md` and `docs/phase-1/CodeDesign.md` define the implementation boundary, architecture, verification requirements, and completion gate. Phase 1 remains `Planned`; source implementation has not started.

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.
