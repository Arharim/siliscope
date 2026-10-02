# siliscope

Static checker for C/C++ firmware (bare-metal / small RTOS). Clang LibTooling
frontend, own rule ids (`ss.<topic>.<name>`). Catalog: [`ruleset/`](ruleset/).

**0.2.0** — working analyzer, not a complete catalog. About 40 checkers are
wired (syntax/AST, a slice of CFG/callgraph, some types). The YAML catalog
lists 192 rules; dataflow, loop bounds, most C++, and preprocessor checks are
not implemented yet. Not a MISRA/CERT clone and not a certified tool.

## Tree

| Path | Role |
| --- | --- |
| `ruleset/` | YAML catalog and profiles (`embedded-c` default) |
| `src/driver/` | CLI, compile_commands rewrite (ARM gcc) |
| `src/checks/` | LibTooling checkers |
| `src/catalog/` | profile / allowlist load |
| `tests/lit/` | `ok.c` / `bad.c` fixtures |
| `tools/` | catalog index, validate, `run_checks.py` |
| `justfile` | build, `just fw`, `just test` |
| `docs/` | local PDFs only; gitignored, do not commit |

## Build

C++17, [just](https://github.com/casey/just), Ninja. The official LLVM Windows
installer does not ship clangTooling. Default `CXX` in the justfile is MSYS
g++ on Windows (`C:/msys64/ucrt64/bin/g++.exe`); override with `CXX=`.

Stub (no LibTooling):

```text
just build
just run --version
```

LibTooling, LLVM/Clang dev (`LLVMConfig.cmake` + `ClangConfig.cmake`). A
component-library build links `clangTooling` and the rest. A distro package
that sets `CLANG_LINK_CLANG_DYLIB` links `libclang-cpp.so` and `libLLVM.so`
instead (for example `/usr/lib/cmake/llvm` and `/usr/lib/cmake/clang`):

```text
just build-clang <llvm-build>/lib/cmake/llvm <llvm-build>/lib/cmake/clang
```

Typical LLVM configure:

```text
cmake -S llvm -B llvm-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86;ARM;AArch64
```

## Usage

```text
siliscope --target arm-none-eabi file.c
siliscope --profile strict --target arm-none-eabi file.c
siliscope --allow ss.fn.no-stdarg:log_printf file.c
siliscope --list
siliscope --profile embedded-cpp --list
just fw path/to/firmware path/to/firmware/src/foo.c
just probe
just test
just test ss.ctrl.no-goto
```

`just fw DIR SRC...` uses `DIR/compile_commands.json`. For `arm-none-eabi-gcc`
jobs the driver injects `--target` / sysroot / gcc `stddef.h` and drops
`--specs=`. Do not put firmware tree paths in this repo.

Profiles: `embedded-c` (default), `embedded-cpp`, `strict`, `style`. A profile's
`languages` drop rules that do not apply, so `embedded-c` does not enable
`ss.cpp.*`. See [`ruleset/README.md`](ruleset/README.md),
[`ruleset/INDEX.md`](ruleset/INDEX.md), [`ruleset/coverage.md`](ruleset/coverage.md).

`--list` prints every rule the profile turned on and whether a checker is
linked. A normal run ends with `findings:` and `no-checker:`. Exit status
follows findings and parse errors. `no-checker` is the catalog gap, not a
defect in the source. `just test` builds once and checks that each `bad.c`
emits its `[ss.*]` id. A file that fails to parse does not count as a hit.

```text
just rules
just fmt
```

[`.clang-format`](.clang-format) is LLVM-based. [`.clangd`](.clangd) is shared:
the include path and `-xc` for `.c` files. The compiler and system includes
belong in the user clangd config.

## What 0.2.0 actually runs

Live checkers (see `src/catalog/Registry.cpp` and `just test`):

- Control / style-adjacent AST: goto, setjmp, braces, assignment-in-condition,
  if-else-final, continue, nested ternary, no-block-scope, prototype,
  unused params
- Memory / libc: no heap after init, VLA, flexible array, unbounded string,
  stdio, stdarg (+ `--allow`), signal, atoi, abort/system, rand, qsort,
  setlocale
- Expressions: octal, comma, `++`/`--` as a statement, sizeof side effects,
  logical-rhs side effects, string literal → `const char *`
- CFG / callgraph: return on all paths, unreachable, noreturn, no recursion,
  ISR not called as a function, no log from ISR, critical-section pairing,
  irq-mask restore (not a blind enable)
- Types / decls: ISR no FP, check-return, no-shadow, ptr-null, distinct
  names, const, file-local `static`

Not in this release: dataflow (uninit, bounds, dangling), `ss.ctrl.loop-bound`,
most `ss.cpp.*` / `ss.pre.*` / `ss.conv.*`, review-only catalog rows.
