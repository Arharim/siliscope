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
| `docs/` | local PDFs only; gitignored, do not commit |

## Build (stub)

Needs a C++17 compiler. The LLVM Windows installer does not provide clangTooling. On this host:

```text
cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build
build/siliscope --version
```

## Build (LibTooling)

Pin LLVM/Clang 19 *dev* (`LLVMConfig.cmake` + `ClangConfig.cmake`). Build LLVM out of tree, then:

```text
cmake -S . -B build -G Ninja \
  -DSILISCOPE_ENABLE_CLANG=ON \
  -DLLVM_DIR=<llvm-build>/lib/cmake/llvm \
  -DClang_DIR=<llvm-build>/lib/cmake/clang
cmake --build build
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
python tools/validate_ruleset.py
python tools/generate_ruleset_index.py
```

Format C++ with [`.clang-format`](.clang-format) (LLVM-based). clangd reads [`.clangd`](.clangd).
