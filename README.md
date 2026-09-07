# siliscope

Static checker for C/C++ firmware (bare-metal / small RTOS). Parse with Clang LibTooling. Rule catalog: [`ruleset/`](ruleset/).

**Status:** phase 0. Stub binary only. Analysis is not implemented (`siliscope <file>` exits 2).

## Tree

| Path | Role |
| --- | --- |
| `ruleset/` | rule catalog and profiles |
| `src/driver/` | CLI |
| `src/checks/` | checkers (empty) |
| `src/catalog/` | YAML load (empty) |
| `tests/lit/` | source fixtures |
| `tools/` | catalog index / validate (python) |
| `justfile` | `just build`, `just run`, `just fmt`, `just rules`, `just build-clang` |
| `docs/` | local PDFs only; gitignored, do not commit |

## Build (stub)

Needs a C++17 compiler and [just](https://github.com/casey/just). The LLVM Windows installer does not provide clangTooling. On this host MSYS g++ is the default in the justfile:

```text
just build
just run --version
```

## Build (LibTooling)

Pin LLVM/Clang 19 *dev* (`LLVMConfig.cmake` + `ClangConfig.cmake`). Build LLVM out of tree, then:

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

## Usage (not wired)

```text
siliscope --profile embedded-c -p <builddir> --target arm-none-eabi file.c
```

Profiles: `embedded-c`, `embedded-cpp`, `strict`, `style`. See [`ruleset/README.md`](ruleset/README.md), [`ruleset/coverage.md`](ruleset/coverage.md).

```text
just rules
just fmt
```

[`.clang-format`](.clang-format) is LLVM-based. clangd reads [`.clangd`](.clangd).
