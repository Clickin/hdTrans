# hdtrans

Rule-based Japanese→Korean translation engine in C++: a static library, a CLI, two
Windows DLLs (`hdTransEngine.dll` with the C API, `J2KEngine.dll` with the J2K exports), and a
WebAssembly module.

Conventions: AGENTS.md.

## Build

```sh
cmake --preset mac          # or linux
cmake --build --preset mac
ctest --preset mac
```

Windows (Visual Studio 2022 or later with the C++ tools):

```powershell
powershell -File tools\build.ps1 -arch x86   # builds build\msvc-x86\{hdTransEngine,J2KEngine}.dll and runs the tests
```

WebAssembly: `emcmake cmake --preset wasm && cmake --build --preset wasm`.

## Layout

| path | contents |
|---|---|
| `include/core/` | public headers: `engine.h` (C++), `hdtrans.h` (C) |
| `src/core/` | the library, one directory per pipeline stage |
| `src/cli/` | `hdtrans` command |
| `src/cshared/` | `hdTransEngine.dll`: the C API |
| `src/j2kengine/` | `J2KEngine.dll`: `J2K_*` exports |
| `src/wasm/` | WebAssembly entry points |
| `tests/unit/` | unit tests (`hdtest.h` runner) |
| `tools/` | `golden.sh` (expectation sets), `build.ps1` (Windows build) |

Dictionaries are UTF-8 TSV folders; `dicts/` holds the default set (`dicts/list.txt` lists its
files). Each folder loads its `*.tsv` files in name order; a file's first line `#! layer=NAME`
gives its role (`base`, `words`, `names`, `user`, `kana` or `readings`, see
`src/core/dict/snapshot.h`), and a file without it is a `base` file.
Cache validation trusts source size, timestamps and file identity; after edits that preserve them,
use `-verify-dicts` or `HDTRANS_VERIFY_DICTS=1` to check source contents.
`CacheWrite::Off` never publishes a cache generation, but may write a `.bad` marker and
rename a generation proven corrupt.
