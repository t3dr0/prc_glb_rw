cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

# Code shared by both tools: the neutral scene model, the glTF reading and
# writing both directions use, and version information.
add_library(prc_glb_rw_common STATIC
            src/pgr_info.c
            src/pgr_message.c
            src/pgr_scene.c
            src/pgr_math.c
            src/pgr_hash.c
            src/model_path.c
            src/pgr_glb_read.c
            src/pgr_glb_write.c
            src/pgr_prc_write.c
            src/pgr_cgltf.c)

target_link_libraries(prc_glb_rw_common PUBLIC nano_prc)
target_include_directories(prc_glb_rw_common PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${CGLTF_DIR})
target_compile_definitions(prc_glb_rw_common PRIVATE
                           PGR_VERSION="${PRC_GLB_RW_VERSION}"
                           PGR_CGLTF_VERSION="${CGLTF_VERSION}")

# cgltf uses fopen and strncpy, and the writer fopen, which MSVC deprecates
# in favour of its own _s variants.
if(MSVC)
    target_compile_definitions(prc_glb_rw_common PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Only link the math library if we aren't targeting Windows
if(NOT WIN32)
    target_link_libraries(prc_glb_rw_common PUBLIC m)
endif()
