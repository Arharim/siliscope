# siliscope — common commands. `just` lists recipes.

build_dir := "build"
cxx := if os() == "windows" {
  env_var_or_default("CXX", "C:/msys64/ucrt64/bin/g++.exe")
} else {
  env_var_or_default("CXX", "")
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

fmt:
    clang-format -i include/siliscope/Version.h src/driver/main.cpp

rules:
    python tools/validate_ruleset.py
    python tools/generate_ruleset_index.py

clean:
    cmake -E rm -rf {{build_dir}}
