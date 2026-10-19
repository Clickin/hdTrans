# hdtrans — rule-based Japanese→Korean translation engine in C++

One static library and thin front ends: CLI, `hdTransEngine.dll` (C API), `J2KEngine.dll`
(J2K_* exports for hosts that load that DLL), WebAssembly. No dependencies.

## Build and test
- CMake presets (CMakePresets.json): `mac` / `linux` build the library, CLI and unit
  tests with clang or gcc; `msvc-x86` / `msvc-x64` build them and the two DLLs with
  MSVC inside a `vcvarsall` environment (`tools/build.ps1` sets one up);
  `wasm` is run as `emcmake cmake --preset wasm`.
- `cmake --preset mac && cmake --build --preset mac && ctest --preset mac` is the
  development loop. The DLLs are the product and are built with MSVC.
- Unit tests use small local fixtures, not external expectation sets or dictionary totals.
- `tools/snapshot.sh` records built-CLI outputs; `tools/compare.sh` reports baseline changes.
- `tools/fetch-syosetu.sh` and `tools/e2e.sh` compare two builds on the public web-novel
  chapters named by `tests/e2e/`; chapter text and its translations are never committed.
  A translation behaviour change needs its own commit with the reason.
- C++20. Library and DLLs are built without exceptions and RTTI; errors are return
  values. Each DLL links the CRT statically and is a single file.
- Warnings are errors; fix the code, not the flags.

## Source rules
- Standard library only. UTF-8 `std::string` internally. Shift-JIS and CP949 exist only
  at the `J2K_*` boundary, converted by the OS; the engine has no code-page tables.
- Types and functions `CamelCase`, local variables `lowerCamel`, private members with a
  trailing underscore, files `snake_case`, one pipeline stage per directory under
  `src/core/`. Internal functions go in `hdTrans::internal` and in headers under
  `src/`, never in `include/`. Identifiers starting with an underscore are not used.
- Plain functions and structs; no abstraction layer unless two implementations exist.
- The library runs inside other people's processes: no global constructors with side
  effects, no stdout/stderr, no `exit`, no locale changes; `Translate` is callable from
  several threads at once on one loaded dictionary.
- Inputs of any length are handled; no fixed-size buffers.
- Tests: table tests on real inputs with `tests/unit/hdtest.h`; no framework, no mocks.
  Every stage has tests next to it.

## Comments
- Comment only what a reader needs to use the code: non-obvious behaviour, invariants,
  data layouts. Short. Prefer no comment over a meta comment.
- No justification or history comments ("kept because", "verified against", "used to
  be"); no references to other projects, agents, test results, machines, users or paths.

## Repository hygiene
- One coherent change per commit; the message says what changed and whether the
  expectation sets moved.
- `src/core/dict/builtin/` holds the builtin tables compiled into the library
  (`cmake/builtin_tables.cmake` turns them into C++ at build time); edit the TSVs.
- `dicts/` is the shipped dictionary set and is committed; each file starts with
  `#! layer=...` and a `#` description line.
- Never commit build outputs, expectation sets, machine names, user
  names or absolute paths. Locations outside the repository come from environment
  variables with relative defaults.
