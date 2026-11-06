cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

# Code shared by both tools: version information now, and later the neutral
# scene model and the glTF reading and writing that both directions use.
add_library(prc_glb_rw_common STATIC
            src/pgr_info.c
            src/pgr_cgltf.c)

target_link_libraries(prc_glb_rw_common PUBLIC nano_prc)
target_include_directories(prc_glb_rw_common PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${CGLTF_DIR})
target_compile_definitions(prc_glb_rw_common PRIVATE
                           PGR_VERSION="${PRC_GLB_RW_VERSION}"
                           PGR_CGLTF_VERSION="${CGLTF_VERSION}")

# cgltf uses fopen and strncpy, which MSVC deprecates in favour of its own
# _s variants; the warnings are about cgltf's unchanged source, not ours.
if(MSVC)
    set_source_files_properties(src/pgr_cgltf.c PROPERTIES
                                COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
endif()

# Only link the math library if we aren't targeting Windows
if(NOT WIN32)
    target_link_libraries(prc_glb_rw_common PUBLIC m)
endif()
