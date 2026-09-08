cmake_minimum_required(VERSION 3.20)
project(siliscope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SILISCOPE_ENABLE_CLANG
       "Link Clang LibTooling (needs an LLVM/Clang dev install)"
       OFF)

add_executable(siliscope src/driver/main.cpp)
target_include_directories(siliscope PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(SILISCOPE_ENABLE_CLANG)
  find_package(LLVM CONFIG REQUIRED)
  find_package(Clang CONFIG REQUIRED)

  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_DIR}")
  message(STATUS "Found Clang in ${Clang_DIR}")

  llvm_map_components_to_libnames(SILISCOPE_LLVM_LIBS support)

  target_sources(siliscope PRIVATE
    src/driver/Frontend.cpp
    src/diag/Report.cpp
    src/catalog/Profile.cpp
    src/checks/NoGoto.cpp
    src/checks/NoSetjmp.cpp
    src/checks/NoHeap.cpp
    src/checks/NoUnboundedString.cpp
    src/checks/Braces.cpp
    src/checks/NoStdio.cpp
    src/checks/NoAssignInCond.cpp
    src/checks/NoOctal.cpp
    src/checks/NoVLA.cpp
    src/checks/NoStdarg.cpp
    src/checks/NoSignal.cpp
    src/checks/NoAtoi.cpp
    src/checks/NoAbortSystem.cpp
    src/checks/NoQsort.cpp
    src/checks/NoRand.cpp
    src/checks/NoSetlocale.cpp
    src/checks/NoComma.cpp
    src/checks/NoFlexibleArray.cpp
    src/checks/NoBlockScope.cpp
    src/checks/IfElseFinal.cpp
    src/checks/NoContinue.cpp
    src/checks/NoNestedTernary.cpp
  )
  target_compile_definitions(siliscope PRIVATE SILISCOPE_WITH_CLANG)
  target_include_directories(siliscope PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
  )
  if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(siliscope PRIVATE -fno-rtti)
  endif()
  if(MINGW)
    # GNU ld often dies (exit 5) on the static Clang archive set.
    target_link_options(siliscope PRIVATE -fuse-ld=lld)
  endif()
  target_link_libraries(siliscope PRIVATE
    clangTooling
    clangASTMatchers
    clangFrontend
    clangDriver
    clangSerialization
    clangParse
    clangSema
    clangAST
    clangLex
    clangBasic
    ${SILISCOPE_LLVM_LIBS}
  )
endif()

install(TARGETS siliscope RUNTIME DESTINATION bin)
install(DIRECTORY ruleset DESTINATION share/siliscope)
