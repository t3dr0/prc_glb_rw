cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# prc_glb_rw builds in one of two ways:
#
#  - as a subdirectory of nanoPRC (a git submodule there): the nano_prc target
#    already exists and is used as is;
#  - on its own: NANOPRC_SOURCE_DIR names a nanoPRC checkout, which is added
#    here with its demos, tests and SDL switched off.
#
# This repository must never contain nanoPRC as a submodule. nanoPRC contains
# this one, and each containing the other would nest copies without end under
# `git submodule update --recursive`.

if(NOT TARGET nano_prc)
    project(prc_glb_rw C)

    set(NANOPRC_SOURCE_DIR "" CACHE PATH "Path to a nanoPRC checkout")
    if(NOT EXISTS "${NANOPRC_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "prc_glb_rw needs nanoPRC: configure with -DNANOPRC_SOURCE_DIR=<path to a nanoPRC checkout>")
    endif()

    set(NANOPRC_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(NANOPRC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NANOPRC_BUILD_SDL   OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NANOPRC_SOURCE_DIR}" nanoprc)

    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()

    # nanoPRC sets its own output directory, so its shared library would land
    # apart from the tools and they would not run from the build directory.
    set_target_properties(nano_prc PROPERTIES
                          RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

set(PRC_GLB_RW_VERSION "0.1.1")

# A function called before it is declared is an implicit declaration, which
# C99 removed: GCC and Clang refuse it, but MSVC only warns (C4013) and the
# build goes on. Make that warning an error here too, for this repository's
# own targets; nanoPRC's, added above, are not affected.
if(MSVC)
    add_compile_options(/we4013)
endif()

# cgltf is a git submodule, pinned at its v1.15 tag. See THIRD_PARTY_NOTICES.md.
set(CGLTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cgltf" CACHE PATH
    "Path to the cgltf sources (git submodule thirdparty/cgltf)")

if(NOT EXISTS "${CGLTF_DIR}/cgltf.h")
    message(WARNING
        "cgltf not found at ${CGLTF_DIR}: prc_glb_rw is not built. "
        "Run `git submodule update --init` in prc_glb_rw.")
    return()
endif()

# The version cgltf states in its own header, so --version reports the copy
# actually built, whatever the submodule is checked out at.
file(STRINGS "${CGLTF_DIR}/cgltf.h" CGLTF_VERSION_LINE REGEX "^ \\* Version: " LIMIT_COUNT 1)
string(REGEX REPLACE "^ \\* Version: *([^ ]+).*$" "\\1" CGLTF_VERSION "${CGLTF_VERSION_LINE}")
if(CGLTF_VERSION STREQUAL "")
    set(CGLTF_VERSION "unknown")
endif()

option(PRC_GLB_RW_BUILD_TESTS "Build prc_glb_rw's CTest tests" ON)

add_subdirectory(common)
add_subdirectory(glb_import)
add_subdirectory(glb_export)

if(PRC_GLB_RW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
