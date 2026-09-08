# siliscope — common commands. `just` lists recipes.

build_dir := "build"
cxx := if os() == "windows" {
  env("CXX", "C:/msys64/ucrt64/bin/g++.exe")
} else {
  env("CXX", "")
}
cxx_flag := if cxx != "" { "-DCMAKE_CXX_COMPILER=" + cxx } else { "" }
bin := if os() == "windows" {
  build_dir / "siliscope.exe"
} else {
  build_dir / "siliscope"
}

default:
    @just --list --unsorted

# Configure + build the stub (no LibTooling)
build:
    cmake -S . -B {{build_dir}} -G Ninja {{cxx_flag}}
    cmake --build {{build_dir}}

# Link Clang LibTooling (positional paths to *Config.cmake dirs):
#   just build-clang C:/dev/llvm-build/lib/cmake/llvm C:/dev/llvm-build/lib/cmake/clang
build-clang llvm_dir clang_dir:
    cmake -S . -B {{build_dir}} -G Ninja {{cxx_flag}} -DSILISCOPE_ENABLE_CLANG=ON "-DLLVM_DIR={{llvm_dir}}" "-DClang_DIR={{clang_dir}}"
    cmake --build {{build_dir}}

# Run the binary (default: --help)
run *args: build
    {{bin}} {{args}}

# Parse the GNU interrupt/packed fixture (arm-none-eabi)
probe: build
    {{bin}} --probe --target arm-none-eabi tests/lit/frontend/isr_attr.c

# ss.ctrl.no-goto fixtures
test-goto: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-goto/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-goto/bad.c

# ss.ctrl.no-setjmp fixtures
test-setjmp: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-setjmp/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-setjmp/bad.c

# ss.mem.no-heap-after-init fixtures
test-heap: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-heap-after-init/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-heap-after-init/bad.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-heap-after-init/bad.cpp

# ss.libc.no-unbounded-string fixtures
test-unbounded: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-unbounded-string/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-unbounded-string/bad.c

# ss.ctrl.braces fixtures
test-braces: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.braces/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.braces/bad.c

# ss.libc.no-stdio fixtures
test-stdio: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-stdio/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-stdio/bad.c

# ss.ctrl.no-assignment-in-condition fixtures
test-assign-cond: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-assignment-in-condition/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-assignment-in-condition/bad.c

# ss.expr.no-octal fixtures
test-octal: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-octal/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-octal/bad.c

# ss.mem.no-vla fixtures
test-vla: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-vla/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-vla/bad.c

# ss.fn.no-stdarg fixtures
test-stdarg: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.no-stdarg/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.no-stdarg/bad.c

# ss.libc.no-signal fixtures
test-signal: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-signal/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-signal/bad.c

# ss.libc.no-atoi fixtures
test-atoi: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-atoi/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-atoi/bad.c

# ss.libc.no-abort-system fixtures
test-abort: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-abort-system/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-abort-system/bad.c

# ss.libc.no-rand fixtures
test-rand: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-rand/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-rand/bad.c

# ss.libc.no-qsort-bsearch fixtures
test-qsort: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-qsort-bsearch/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-qsort-bsearch/bad.c

# ss.libc.no-setlocale fixtures
test-setlocale: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-setlocale/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.libc.no-setlocale/bad.c

# ss.expr.no-comma fixtures
test-comma: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-comma/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-comma/bad.c

# ss.mem.no-flexible-array fixtures
test-flexarray: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-flexible-array/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.mem.no-flexible-array/bad.c

# ss.fn.no-block-scope fixtures
test-block-scope: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.no-block-scope/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.no-block-scope/bad.c

# ss.ctrl.if-else-final fixtures
test-if-else-final: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.if-else-final/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.if-else-final/bad.c

# ss.ctrl.no-continue: off in embedded-c, on in strict
test-continue: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-continue/ok.c
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-continue/bad.c
    {{bin}} --profile strict --target arm-none-eabi tests/lit/checks/ss.ctrl.no-continue/ok.c
    ! {{bin}} --profile strict --target arm-none-eabi tests/lit/checks/ss.ctrl.no-continue/bad.c

# ss.ctrl.no-nested-ternary: advisory extra in strict only
test-nested-ternary: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-nested-ternary/ok.c
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.ctrl.no-nested-ternary/bad.c
    {{bin}} --profile strict --target arm-none-eabi tests/lit/checks/ss.ctrl.no-nested-ternary/ok.c
    ! {{bin}} --profile strict --target arm-none-eabi tests/lit/checks/ss.ctrl.no-nested-ternary/bad.c

# ss.expr.no-inc-in-expr fixtures
test-inc: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-inc-in-expr/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-inc-in-expr/bad.c

# ss.expr.no-sizeof-side-effect fixtures
test-sizeof: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-sizeof-side-effect/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-sizeof-side-effect/bad.c

# ss.expr.no-logical-rhs-side-effect fixtures
test-logical-rhs: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-logical-rhs-side-effect/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.expr.no-logical-rhs-side-effect/bad.c

# ss.fn.prototype fixtures
test-prototype: build
    {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.prototype/ok.c
    ! {{bin}} --target arm-none-eabi tests/lit/checks/ss.fn.prototype/bad.c

# YAML profile load (default embedded-c still used by the fixtures above)
test-profile: build
    {{bin}} --profile style --target arm-none-eabi tests/lit/checks/ss.ctrl.no-goto/bad.c
    {{bin}} --profile embedded-cpp --target arm-none-eabi tests/lit/checks/ss.mem.no-vla/bad.c
    ! {{bin}} --profile strict --target arm-none-eabi tests/lit/checks/ss.ctrl.no-goto/bad.c
    ! {{bin}} --profile nosuch --target arm-none-eabi tests/lit/checks/ss.ctrl.no-goto/ok.c

fmt:
    clang-format -i include/siliscope/*.h src/driver/*.cpp src/diag/*.cpp src/checks/*.cpp src/catalog/*.cpp

rules:
    python tools/validate_ruleset.py
    python tools/generate_ruleset_index.py

clean:
    cmake -E rm -rf {{build_dir}}
