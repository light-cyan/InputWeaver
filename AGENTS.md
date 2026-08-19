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

## Repository Practices

- Keep source code, comments, filenames, and documentation in English.
- Keep code concise and avoid unnecessary complexity or verbosity.
- Design structures carefully so each implementation is clear, cohesive, and efficient.
- Do not add third-party dependencies or unrelated generated files.
