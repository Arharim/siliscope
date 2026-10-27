# siliscope

Static checker for C/C++ firmware (bare-metal / small RTOS). Clang LibTooling
frontend, own rule ids (`ss.<topic>.<name>`). Catalog: [`ruleset/`](ruleset/).

**0.2.0** – working analyzer, not a complete catalog. 168 checkers are wired.
The YAML catalog lists 192 rules. `--list` is the live count for a profile.
Not a MISRA/CERT clone and not a certified tool.

## Tree

| Path | Role |
| --- | --- |
| `ruleset/` | YAML catalog and profiles (`embedded-c` default) |
| `src/driver/` | CLI, compile_commands rewrite (ARM gcc) |
| `src/checks/` | LibTooling checkers |
| `src/catalog/` | profile / allowlist load |
| `tests/lit/` | `ok.c` / `bad.c` fixtures |
| `tools/` | catalog index, validate, `run_checks.py`, `diff_report.py` |
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

`just build` configures without `-DSILISCOPE_ENABLE_CLANG`. A fresh `build/`
is a stub: `just run --version` prints `frontend: stub`. `just build-clang`
turns LibTooling on and stores that in the CMake cache. A later `just build`,
`just fw`, or `just test` does not pass the flag again and does not turn it
off. `just clean` deletes the cache, so the next `just build` is a stub until
`build-clang` runs again.

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
and `arm-none-eabi-g++` jobs the driver injects `--target`, sysroot, and
Clang's resource directory, drops `--specs=` and GCC flags Clang rejects, and
searches GCC's include after Clang's so `<arm_acle.h>` is Clang's header. A
g++ job also gets that toolchain's libstdc++ include and the multilib
directory selected by `-mcpu` / `-mfloat-abi`, so `<cstdint>` resolves. Do not
put firmware tree paths in this repo.

Profiles: `embedded-c` (default), `embedded-cpp`, `strict`, `style`. A profile's
`languages` drop rules that do not apply, so `embedded-c` does not enable
`ss.cpp.*`. See [`ruleset/README.md`](ruleset/README.md),
[`ruleset/INDEX.md`](ruleset/INDEX.md), [`ruleset/coverage.md`](ruleset/coverage.md).

`--list` prints every rule the profile turned on and whether a checker is
linked. A normal run ends with `findings:` and `no-checker:`. Exit status
follows findings and parse errors. `no-checker` is the catalog gap, not a
defect in the source. `just test` builds once and checks that each `bad.c`
emits its `[ss.*]` id. A file that fails to parse does not count as a hit.

`--suppress <file>` drops every finding in that file. `--suppress <rule>:<file>`
drops one rule there. The path matches the diagnostic file in full, or as a
suffix at a directory boundary, so `inc/foo.h` matches a longer absolute path
and `task.cpp` does not match `can_task.cpp`. A path with no trailing slash is
one file: `dir` does not cover `dir/file.c`. A trailing slash is a directory
prefix. `--suppress <dir>/` drops every finding in files under that directory,
and `--suppress <rule>:<dir>/` drops one rule there. The prefix matches a
directory component, so a moved line or a rewritten message stays quiet, and
`task/` does not match `can_task/`. `.` and `..` collapse, so
`main/../libraries/` is `libraries/`. `/` is rejected. `--baseline <report>`
drops a finding the saved log already lists at the same file, line, column,
severity, message, and rule. What remains is still printed, and the exit
status still follows those findings. The summary then adds `suppressed:` and,
for a baseline, `baseline-missing:`. The accepted set is those files,
directories, and sites. The profile keeps every rule. Nothing is skipped
because of a vendor name.

`python3 tools/diff_report.py previous.txt current.txt` prints diagnostics
that appeared or disappeared. Summary lines are not findings. The exit status
is 0 when both logs parse.

```text
just rules
just fmt
```

[`.clang-format`](.clang-format) is LLVM-based. [`.clangd`](.clangd) is shared:
the include path and `-xc` for `.c` files. The compiler and system includes
belong in the user clangd config.

## What 0.2.0 actually runs

On the default `embedded-c` profile, 149 rules are on and 142 have a checker.
`embedded-cpp` is 157 on, 149 with a checker. `strict` is 176 / 167. `style`
turns on 12 formatting rules and links no checker. A run's `no-checker:` count
is that gap.

Live checkers (see `src/catalog/Registry.cpp` and `just test`):

- Control / style-adjacent AST: goto, setjmp, braces, assignment-in-condition,
  if-else-final, continue, nested ternary, switch labels and default,
  commented fall-through, empty loop (braces and a comment), no-block-scope,
  prototype, unused params
- Memory / libc: no heap after init, VLA, flexible array, unbounded string,
  stdio, stdarg (+ `--allow`), signal, atoi, abort/system, rand, qsort,
  setlocale
- Expressions: octal, comma, `++`/`--` as a statement, sizeof side effects,
  logical-rhs side effects, string literal → `const char *`
- CFG / callgraph: return on all paths, unreachable, noreturn, no recursion,
  ISR not called as a function, no log from ISR, no delay, queue-wait,
  malloc, or flash-erase while a critical section or interrupt mask is held
  (these follow direct calls across every file of the run), critical-section
  pairing, irq-mask restore (not a blind enable). Heap use is still an error
  after init: it is quiet only in static initialization, or when every path
  reaches it from a function named with `--allow ss.mem.no-heap-after-init:NAME`
- Types / decls: ISR no FP, check-return, no-shadow, ptr-null, distinct
  names, const, file-local `static`
- Symbols across one run: an external function or object is defined once.
  An inline function, a template, and a C `inline` without `extern` may
  repeat, and a name with no definition in the run stays quiet. A C
  definition in a source file needs a declaration in a header. Parameter
  names match the other declaration. A shared object is declared `extern`
  in one header, not in each source file
- Conversions: signed bitwise, shift amount, `U` suffix, stripping
  const/volatile, pointer/integer casts, floating equality, implicit narrowing.
  A signed and an unsigned integer in one arithmetic or relational expression
  after integer promotion. An unsigned constant `+`, `-`, or `*` that wraps,
  unless a comment on that line or the comment line above says mask. Unary
  minus on a value that is still unsigned after promotion. A cast between
  unrelated object pointers; `void *`, a character pointer, `std::byte *`, a
  packed record, and a base or derived class stay quiet. A function-pointer
  cast to a different type. An implicit narrowing of a floating value. A
  float-to-integer conversion, suppressed in one helper with
  `--allow ss.conv.fp-to-int-explicit:NAME`. An integer turned into an enum,
  suppressed in one function with `--allow ss.conv.no-int-to-enum:NAME`
- C++ subset: no exceptions, no RTTI (`typeid`, `dynamic_cast`), no C-style
  or functional cast, `nullptr`, no heap-backed standard containers,
  `override` or `final` on an override, a virtual, protected, or deleted
  destructor on a polymorphic base, a non-throwing destructor, constructors
  that initialize every trivial base and member, no using-directive, no
  `using` in a header, explicit conversions, no dynamic exception
  specification, no default arguments, `enum class`, no friend and no
  public data outside a POD, a packed record, or a union, the rule of five,
  no `vector<bool>`, no `std::move` from a const object, and a noexcept swap
  that does not allocate when assignment is written as construct-and-swap
- Preprocessor: no absolute `#include` path, `#if` / `#endif` closed in the
  same file, no keyword macro, no stringify of a parameter that is then pasted,
  an include guard or `#pragma once` on every header, parentheses around a
  function-like macro and each of its parameters, no `/*`, `//`, or trailing
  backslash inside a comment, a function-like macro that can be a static
  inline function, source left behind in a comment, an `#include` in a
  source file that the file does not use, a C source file that does not
  include its own header first, token paste, a recursive macro, and an `#if`
  without a comment
- Dataflow inside one function: an uninitialized read, a constant index
  outside an array, a one-past dereference, a null dereference, a use after
  `free` or `delete`, an overlapping `memcpy` or struct assignment, a copy
  or string write that does not fit the destination, pointer subtraction or
  a relational compare of two different objects, a zero divisor, signed
  overflow of a known value, an unsequenced side effect, a constant Inf or
  NaN, a condition that folds to true or false (`while (0)` stays the empty
  idiom, `for (;;)` stays the superloop, and `if constexpr` is a compile-time
  branch), the address of an automatic
  returned or stored past its lifetime, and a loop with no iteration cap.
  A proven null, an uninitialized address, and a constant index follow a
  direct call when that callee's body is in the run. A missing body, a call
  through a pointer, and a cycle stay opaque
- Pointers and stack frames: no `p + n`, `p - n`, `p += n`, or `++` / `--` on a
  pointer. `a[i]` and `&a[i]` stay the indexing form, and pointer subtraction
  stays with the same-array rule. No arithmetic on `void *` (C). A parameter
  declared `T a[N]` is called with a constant array of at least N elements.
  A declaration nests at most two pointer levels. `sizeof` on a parameter
  that decayed to a pointer is the pointer size. Automatic locals that sum
  past 512 bytes are quiet in one function with
  `--allow ss.mem.no-large-frame:NAME`
- Declarations, conditions, and initialization: a static function, file-scope
  object, or typedef that this file never uses; a file-scope object used from
  only one function, or a local that can move into a tighter block; an object
  whose declarations disagree on `_Alignas` / `alignas`; an `if`, `while`,
  `for`, or `do` test that is not a boolean or a comparison; a floating loop
  counter; an aggregate that leaves an element out, omits the size of a
  designated array, or flattens a nested brace list; and, in C, an `inline`
  function that is not `static`. A file that contains only static functions
  stays quiet. `--allow` names the symbol, or the enclosing function for a
  condition or a loop
- C strings and ctype: `memcmp` on a plain `char` or `signed char` buffer
  belongs with `strcmp` / `strncmp`. An `unsigned char` record, a struct, and
  a `void *` stay a packed layout. A cast to `void *` does not hide the
  buffer. `isdigit`, `toupper`, and the other one-argument `<ctype.h>`
  functions take an `unsigned char` value or EOF (`-1`). A `signed char`, or
  a plain `char` when `char` is signed, is the defect. A constant in range
  and a cast to `unsigned char` stay quiet. An `int` stays quiet, because
  that is where EOF lives. `--allow` names the enclosing function
- Types: `short` and `long` are not a width contract, and a typedef such as
  `int64_t` stays the fixed-width name. Arithmetic on plain `char` is integer
  data; a string and a character literal stay character data. A cast to
  `bool` or `_Bool` belongs as a comparison. In C, a declaration names its
  type, and `restrict` does not appear. A plain `int` bit-field names
  `signed` or `unsigned`, and a one-bit field is not signed. An enumerator
  list initializes none, only the first, or every value, and two values do
  not collide. An array names its bound; a parameter `T a[]` and an
  initializer that fixes the bound stay quiet. A boolean is not a counter.
  `--allow` names the symbol, or the enclosing function for a cast or an
  operator
- Syntax: a vector entry carries the interrupt attribute, or a CMSIS
  `*_IRQHandler` / system `*_Handler` name. A house `*_isr` without the
  attribute is a defect. `#pragma`, `__attribute__`, `__declspec`, and inline
  asm in the main file stay in the hardware layer. `errno`, locale, and a
  host file call stay out. A packed struct has a static assert of its size
  and of each field offset. Trigraphs and digraphs are unused. An initializer
  list does not increment, call, or assign. A name does not start with `__`
  or `_` plus an uppercase letter, and an external definition does not reuse
  a libc name. One declaration statement names one identifier. `--allow`
  names the symbol, or the enclosing function for a call, an operator, or an
  extension inside a function
- Advisory syntax: an `if` nested more than two levels (an `else if` chain
  stays one level). A `for` / `while` / `do` bound written as a bare number
  other than 0 or 1. A function body longer than 80 lines of code, and a
  parameter list longer than eight. On the strict profile, a mixed-precedence
  expression and an `&&` / `||` operand that is not a name or a parenthesized
  expression. A numeric literal other than 0, 1, or -1 in a condition, a
  delay or sleep call, an array bound, a designated field initializer, or a
  packed-struct initializer. A macro that spells the literal is already a
  name. `<time.h>` and its calls, and in C `<tgmath.h>`, `<fenv.h>`, and the
  `fenv.h` functions. A non-sequential atomic order without a comment on that
  line or the line above. `--allow` names the function, or the declared
  object for an array bound
- Control flow: a `for` with a condition has one counter, and that counter
  changes only in the for-clause. The condition does not assign. An infinite
  `for (;;)` has no counter. An expression statement that cannot affect
  observable behaviour is dead code. A cast to `void` is a deliberate discard,
  and a volatile access stays. A label named by a goto, a computed goto, or
  an asm goto is in use. A function with more than 15 independent paths
  warns: each `if`, loop, `case`, `catch`, `&&`, `||`, and `?:` adds one.
  `--allow` names the function
- Review hints: a non-volatile object written from an ISR, `main`, or one
  `*_task` / `*_thread` and read from another of those is a shared object
  without a synchronization story. A comment on its declaration that names a
  critical section, queue, mutex, lock, or atomic keeps it. `volatile` is not
  a lock: a read-modify-write of a volatile scalar, or a test followed by
  storing 1, is the hint. A vector table with a null slot needs a default
  handler. An interrupt handler does not block, allocate, or loop forever;
  `*FromISR` stays. A function declared in a project header rejects a null
  pointer it dereferences and checks an integer it uses as an index. A
  hardware or wire struct (a volatile field, or a name such as register,
  packet, or wire) is packed or has a `sizeof` static assert. On the strict
  profile, reading a second member of a union is a representation pun.
  `--allow` names the object, the record, the vector table, or the function

Not linked yet:

- The `style` profile: `ss.style.*`, `ss.emb.isr-naming`, `ss.emb.thread-naming`
- Build policy, not a source checker: `ss.proc.all-warnings`,
  `ss.proc.static-analysis`
- Catalog rows still without a checker: `ss.conv.no-assign-as-value`,
  `ss.ctrl.single-break-in-loop`, `ss.fn.single-exit` (default off),
  `ss.ptr.no-fn-pointer`, `ss.type.no-fp-unless-needed`, `ss.type.no-union`,
  `ss.conc.no-recursive-mutex` (C++ profile), `ss.conv.no-mixed-categories`
  (strict), `ss.expr.float-literal-suffix` (strict),
  `ss.expr.literal-suffix-case` (default off)

## License

siliscope is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. The text is in `LICENSE`. Copyright (C) 2026 Arharim,
also known as De1MOS and DM.

There is no warranty. The firmware siliscope analyzes is a separate work and
is not covered by this license.
