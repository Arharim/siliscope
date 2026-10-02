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
#   just build-clang <llvm-build>/lib/cmake/llvm <llvm-build>/lib/cmake/clang
build-clang llvm_dir clang_dir:
    cmake -S . -B {{build_dir}} -G Ninja {{cxx_flag}} -DSILISCOPE_ENABLE_CLANG=ON "-DLLVM_DIR={{llvm_dir}}" "-DClang_DIR={{clang_dir}}"
    cmake --build {{build_dir}}

# Run the binary (default: --help)
run *args: build
    {{bin}} {{args}}

# Firmware via compile_commands.json (dir with the json, then sources):
#   just fw path/to/firmware path/to/firmware/src/foo.c
fw dir *files: build
    {{bin}} --ruleset-dir ruleset -p {{dir}} {{files}}

# Parse the GNU interrupt/packed fixture (arm-none-eabi)
probe: build
    {{bin}} --probe --target arm-none-eabi tests/lit/frontend/isr_attr.c

# Build once, then every tests/lit fixture. Optional filter: just test ss.ctrl.no-goto
test *ids: build
    python tools/run_checks.py {{bin}} {{ids}}

fmt:
    clang-format -i include/siliscope/*.h src/driver/*.cpp src/diag/*.cpp src/checks/*.cpp src/catalog/*.cpp

rules:
    python tools/validate_ruleset.py
    python tools/generate_ruleset_index.py

clean:
    cmake -E rm -rf {{build_dir}}
