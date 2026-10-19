# hdtrans — rule-based Japanese→Korean translation engine in C++

One static library and thin front ends: CLI, `hdTransEngine.dll` (C API), `J2KEngine.dll`
(J2K_* exports for hosts that load that DLL), WebAssembly. No dependencies.
README.md is for users; developer documentation is in docs/ (index: docs/README.md).

## Build and test
- CMake presets (CMakePresets.json): `mac` / `linux` use Ninja with clang or gcc;
  `windows-x86` / `windows-x64` use Visual Studio 2026 without a Developer prompt
  (CMake 4.2+). `windows-x86-ninja` / `windows-x64-ninja` need Ninja and a matching
  MSVC Developer prompt or `vcvarsall` environment. All native presets build the
  static/shared libraries, CLI and tests; J2KEngine.dll is Windows-only.
  `tools/build.ps1` selects installed Visual Studio 2022 or 2026. Visual Studio
  Release binaries are under `build/<preset>/Release`; Ninja binaries are directly
  under `build/<preset>`. `wasm` needs Emscripten and Ninja: `emcmake cmake --preset wasm`.
- `cmake --preset mac && cmake --build --preset mac --parallel && ctest --preset mac`
  is the development loop; substitute any native platform's preset for `mac`.
- CI uses Ninja with sccache for native builds and ccache for Emscripten's Clang calls;
  neither compiler cache is required by local presets.
- Unit tests use small local fixtures, not external expectation sets or dictionary totals.
- `tools/snapshot.sh` records built-CLI outputs; `tools/compare.sh` reports baseline changes
  (docs/testing.md).
- End-to-end test data (copyrighted texts and their expected outputs) lives in the private
  repository sokcuri/hdTrans-data, found through `HDTRANS_TESTDATA`; without it the tests
  skip. `tools/e2e.sh --expected "$HDTRANS_TESTDATA" CLI dicts` reports changed lines and
  `--accept` rewrites the expected outputs there; ctest runs it as `testdata`
  (docs/testing.md). Never copy its texts or translations into this repository, commit
  messages, issues or pull requests.
- A translation behaviour change needs its own commit with the reason; once it is on `main`,
  accept the new outputs in the test-data repository and commit them there.
- `tools/detect-bad.sh` lists ill-formed Korean sequences in outputs; a pattern becomes a
  constraint rule (`src/core/word/constraints.cpp`) only when its checked hits are all errors.
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
- `dicts/` is the shipped dictionary set and is committed; each file starts with
  `#! layer=...` and a `#` description line (docs/dictionaries.md).
- Never commit build outputs, expectation sets, machine names, user
  names or absolute paths. Locations outside the repository come from environment
  variables with relative defaults.
