cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

add_executable(nano_prc_glb_import src/glb_import.c)

target_link_libraries(nano_prc_glb_import PRIVATE prc_glb_rw_common)
target_include_directories(nano_prc_glb_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The tool opens its input with fopen, which MSVC deprecates in favour of
# its own _s variant.
if(MSVC)
    target_compile_definitions(nano_prc_glb_import PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
