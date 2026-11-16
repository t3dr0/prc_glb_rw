# Google Draco (C++, Apache-2.0), for KHR_draco_mesh_compression. Included
# by the top-level CMakeLists only when PRC_GLB_WITH_DRACO is ON; defines
# PRC_GLB_DRACO_TARGET, the target to link.
#
# An installed Draco (find_package, e.g. from vcpkg, apt or Homebrew) is used
# when there is one. Otherwise the pinned release below is downloaded and
# built as a static library, with its tools, tests and transcoder off. To
# build from a copy already on disk, configure with
#   -DFETCHCONTENT_SOURCE_DIR_DRACO=<path to the unpacked draco-1.5.9>

set(PRC_GLB_DRACO_VERSION 1.5.9)
set(PRC_GLB_DRACO_URL
    "https://github.com/google/draco/archive/refs/tags/${PRC_GLB_DRACO_VERSION}.tar.gz")
set(PRC_GLB_DRACO_SHA256 5b84e0e212ccc7b03ace681760a65a66fe2481a5f54df65154cfc8f0cbb87148)

find_package(draco CONFIG QUIET)

if(draco_FOUND AND TARGET draco::draco)
    message(STATUS "prc_glb_rw: using the installed Draco ${draco_VERSION}")
    set(PRC_GLB_DRACO_TARGET draco::draco)
else()
    include(FetchContent)

    # Only the library is wanted, and as a static one, whatever the
    # project around it builds.
    set(DRACO_JS_GLUE               OFF CACHE BOOL "" FORCE)
    set(DRACO_TESTS                 OFF CACHE BOOL "" FORCE)
    set(DRACO_TRANSCODER_SUPPORTED  OFF CACHE BOOL "" FORCE)
    set(DRACO_INSTALL               OFF CACHE BOOL "" FORCE)
    set(DRACO_BUILD_EXECUTABLES     OFF CACHE BOOL "" FORCE)
    set(DRACO_UNITY_PLUGIN          OFF CACHE BOOL "" FORCE)
    set(DRACO_MAYA_PLUGIN           OFF CACHE BOOL "" FORCE)
    set(PRC_GLB_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    # Draco asks for C++11, but draco/mesh/mesh.h includes <optional>,
    # which MSVC warns about below C++17.
    set(PRC_GLB_SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD 17)

    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)   # extracted files take the time of extraction
    endif()
    FetchContent_Declare(draco
                         URL      ${PRC_GLB_DRACO_URL}
                         URL_HASH SHA256=${PRC_GLB_DRACO_SHA256})
    FetchContent_MakeAvailable(draco)

    set(BUILD_SHARED_LIBS ${PRC_GLB_SAVED_BUILD_SHARED_LIBS})
    set(CMAKE_CXX_STANDARD ${PRC_GLB_SAVED_CXX_STANDARD})
    set(PRC_GLB_DRACO_TARGET draco::draco)
    message(STATUS "prc_glb_rw: building Draco ${PRC_GLB_DRACO_VERSION} from source")
endif()

# Draco's headers are someone else's: keep their warnings out of ours.
get_target_property(PRC_GLB_DRACO_REAL ${PRC_GLB_DRACO_TARGET} ALIASED_TARGET)
if(NOT PRC_GLB_DRACO_REAL)
    set(PRC_GLB_DRACO_REAL ${PRC_GLB_DRACO_TARGET})
endif()
get_target_property(PRC_GLB_DRACO_INCLUDES ${PRC_GLB_DRACO_REAL} INTERFACE_INCLUDE_DIRECTORIES)
if(PRC_GLB_DRACO_INCLUDES)
    get_target_property(PRC_GLB_DRACO_IMPORTED ${PRC_GLB_DRACO_REAL} IMPORTED)
    if(NOT PRC_GLB_DRACO_IMPORTED)
        set_target_properties(${PRC_GLB_DRACO_REAL} PROPERTIES
                              INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PRC_GLB_DRACO_INCLUDES}")
    endif()
endif()
