# apack — a standalone project.
#
# This lives outside the Axiom tree on purpose: it is a product in its own right, it is
# shipped on its own, and it versions on its own schedule. It consumes the compiler as a
# library and adds nothing to it.
#
# What it needs from Axiom is the headers and the static libraries, so `ACVM_ROOT` points
# at a checkout and `ACVM_BUILD` at a build of it. Building Axiom is not this project's
# job -- if the libraries are missing, say so rather than half-configuring.
#
#     cmake -S . -B build -G "MinGW Makefiles" \
#           -DCMAKE_CXX_COMPILER=<llvm-mingw>/bin/clang++.exe
#     cmake --build build
#
# `build.sh` does exactly that with the toolchain this machine has.

cmake_minimum_required(VERSION 3.20)
project(apack CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ACVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Axiom-release" CACHE PATH
    "Path to the Axiom source checkout")
set(ACVM_BUILD "${ACVM_ROOT}/build" CACHE PATH
    "Path to a configured build of that checkout")

get_filename_component(ACVM_ROOT "${ACVM_ROOT}" ABSOLUTE)
get_filename_component(ACVM_BUILD "${ACVM_BUILD}" ABSOLUTE)

if(NOT EXISTS "${ACVM_ROOT}/include/acvm/Driver/Driver.h")
    message(FATAL_ERROR
        "ACVM_ROOT does not look like an Axiom checkout: ${ACVM_ROOT}\n"
        "Pass -DACVM_ROOT=<path> to point at one.")
endif()

# The libraries the packer links. Listed so a missing build fails here, with a sentence,
# rather than as a wall of undefined symbols.
set(ACVM_LIBRARIES
    acvm_driver acvm_vm acvm_jit acvm_ir acvm_sema
    acvm_parse acvm_ast acvm_lex acvm_common)

set(ACVM_LIB_PATHS "")
foreach(library IN LISTS ACVM_LIBRARIES)
    if(NOT EXISTS "${ACVM_BUILD}/lib${library}.a")
        message(FATAL_ERROR
            "missing ${ACVM_BUILD}/lib${library}.a\n"
            "Build the Axiom tree first:  cmake --build ${ACVM_BUILD}")
    endif()
    list(APPEND ACVM_LIB_PATHS "${ACVM_BUILD}/lib${library}.a")
endforeach()

add_executable(apack main.cpp Packer.cpp Resources.cpp Sha256.cpp)
target_include_directories(apack PRIVATE "${ACVM_ROOT}/include")

# The resource identifiers (`RT_ICON`, `RT_VERSION`, ...) are defined in terms of
# `MAKEINTRESOURCE`, which resolves to the ANSI or the wide spelling depending on this
# macro. Every call in the source names the wide entry point explicitly, so without it the
# identifiers are `char*` and do not match the functions they are passed to.
if(WIN32)
    target_compile_definitions(apack PRIVATE UNICODE _UNICODE)
endif()

# Where the runtime lives when `--runtime` is not given. Baked in for the same reason the
# compiler bakes in its standard-library path: a tool that cannot find its own siblings is
# a tool every caller has to configure. `--runtime` and `$ACVM_HOME` both override it.
target_compile_definitions(apack PRIVATE
    ACVM_RUNTIME_DIR="${ACVM_BUILD}/bin")

# Grouped rather than ordered. These are static archives with references in both
# directions -- the VM names the JIT, the JIT names the IR -- so any hand-written order
# works until a symbol moves, and then fails for a reason that has nothing to do with the
# change that moved it.
target_link_options(apack PRIVATE
    "LINKER:--start-group"
    ${ACVM_LIB_PATHS}
    "LINKER:--end-group")

if(WIN32)
    target_link_libraries(apack PRIVATE bcrypt)
endif()

install(TARGETS apack RUNTIME DESTINATION .)
