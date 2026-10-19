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
| `examples/` | input/output example documents (below) |
| `tools/` | `snapshot.sh`, `compare.sh`, `explain.sh` (self-baselines), `examples.sh` (examples), `build.ps1` (Windows build) |

## Examples

`examples/` holds short Japanese documents and their translations:

- `<case>.ja.txt`: the input, translated as one document;
- `<case>.ko.txt`: the engine's current output, reviewed;
- `<case>.want.ko.txt`: the correct translation, present only while the engine gets lines wrong;
- `list.txt`: one line per case (name, line count, want lines correct, what it tests).

```sh
tools/examples.sh build/mac/hdtrans DICTS            # fails on any change from *.ko.txt
tools/examples.sh build/mac/hdtrans DICTS --accept   # takes the new output and refreshes list.txt
```

Outputs go to `build/examples/`. `ctest` runs the check when `HDTRANS_DICTS` is set and skips it
otherwise.

Dictionaries are UTF-8 TSV folders; `dicts/` holds the default set (`dicts/list.txt` lists its
files). Each folder loads its `*.tsv` files in name order; a file's first line `#! layer=NAME`
gives its role (`base`, `words`, `names`, `user`, `kana` or `readings`, see
`src/core/dict/snapshot.h`), and a file without it is a `base` file.
Cache validation trusts source size, timestamps and file identity; after edits that preserve them,
use `-verify-dicts` or `HDTRANS_VERIFY_DICTS=1` to check source contents.
`CacheWrite::Off` never publishes a cache generation, but may write a `.bad` marker and
rename a generation proven corrupt.

## Inspect one input

`printf '%s' '学生です。' | build/mac/hdtrans -dicts dicts/base -stages stages` dumps that input's stages.
Read `stages/input.<TAG>.got.tsv` (`#LINE` headers; escaped TSV records); `HDTRANS_DRIVER_DUMP=stages` also enables dumping.
Add `-raw` for the word-only pipeline, or `-each` for a fresh session per input line.

## Self-baselines

```sh
tools/snapshot.sh <dicts-dir> <name>       # built CLI only; no build or expectation replay
tools/compare.sh <runA> <runB>            # set, line, input, A, B; per-set and total counts
tools/explain.sh <binA> <binB> <dicts> '学生です。'  # first differing stage and records
```

Runs are immutable folders under `${HDTRANS_RUNS:-../hdtrans-data/cpp-runs}` with copied inputs,
outputs, commit/binary/dictionary SHA-256 identities, and per-set timings in `sets.tsv`.
`HDTRANS_DATA` defaults to `../hdtrans-data`; `HDTRANS_INPUTS` defaults to its `hdtrans-golden`
folder (only top-level `.in` files are read). Lyrics from `lyrics/cases.tsv` are grouped by song,
and `syosetu/*.ja.txt` files run as documents. `HDTRANS_BIN` selects the built CLI (default `build/mac/hdtrans`).
Compare accepts run names or folder paths; compare/explain exit 0 for identical, 1 for differences, 2 for invalid inputs.
Snapshot uses default translation settings and a disposable run-local cache; it refuses an existing run name.
`tools/list.txt` catalogs the initial sets (name, input line count, recorded source); it does not constrain set discovery.
