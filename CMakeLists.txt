cmake_minimum_required(VERSION 3.20)
project(siliscope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SILISCOPE_ENABLE_CLANG
       "Link Clang LibTooling (needs an LLVM/Clang *dev* install, not just the compiler)"
       OFF)

add_executable(siliscope src/driver/main.cpp)
target_include_directories(siliscope PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(SILISCOPE_ENABLE_CLANG)
  find_package(LLVM CONFIG REQUIRED)
  find_package(Clang CONFIG REQUIRED)

  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_DIR}")
  message(STATUS "Found Clang in ${Clang_DIR}")

  llvm_map_components_to_libnames(SILISCOPE_LLVM_LIBS support)

  target_compile_definitions(siliscope PRIVATE SILISCOPE_WITH_CLANG)
  target_include_directories(siliscope PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
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
