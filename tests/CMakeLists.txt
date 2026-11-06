cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

# Unit tests of the code the two tools share.
foreach(name test_info test_cgltf_link)
    add_executable(${name} unit/${name}.c)
    target_link_libraries(${name} PRIVATE prc_glb_rw_common)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endforeach()
target_compile_definitions(test_info PRIVATE PGR_EXPECTED_CGLTF_VERSION="${CGLTF_VERSION}")

# The tools' command lines: --version, and the exit code for each kind of
# failure. Placeholder inputs are written here, because the skeleton only
# checks that its input opens.
set(PGR_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/cli)
file(MAKE_DIRECTORY ${PGR_TEST_DIR})
file(WRITE ${PGR_TEST_DIR}/placeholder.glb "")
file(WRITE ${PGR_TEST_DIR}/placeholder.prc "")

string(REPLACE "." "\\." CGLTF_VERSION_RE "${CGLTF_VERSION}")
foreach(tool nano_prc_glb_import nano_prc_glb_export)
    add_test(NAME ${tool}_version COMMAND ${tool} --version)
    set_tests_properties(${tool}_version PROPERTIES
        PASS_REGULAR_EXPRESSION "${tool} [0-9]+\\.[0-9]+\\.[0-9]+.*nanoPRC .*cgltf ${CGLTF_VERSION_RE}")
endforeach()

# exit_code(<test name> <expected exit code> <tool> [arguments...])
function(exit_code name expected tool)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND}
                     -DEXE=$<TARGET_FILE:${tool}>
                     "-DARGS=${ARGN}"
                     -DEXPECT=${expected}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_exit.cmake
             WORKING_DIRECTORY ${PGR_TEST_DIR})
endfunction()

exit_code(import_no_arguments      1 nano_prc_glb_import)
exit_code(import_unknown_option    1 nano_prc_glb_import --no-such-option placeholder.glb)
exit_code(import_wrong_input_type  1 nano_prc_glb_import placeholder.prc)
exit_code(import_wrong_output_type 1 nano_prc_glb_import placeholder.glb out.glb)
exit_code(import_missing_input     2 nano_prc_glb_import missing.glb out.prc)
exit_code(import_not_implemented   3 nano_prc_glb_import placeholder.glb out.prc out.pdf)

exit_code(export_no_output         1 nano_prc_glb_export placeholder.prc)
exit_code(export_wrong_input_type  1 nano_prc_glb_export placeholder.glb out.glb)
exit_code(export_wrong_output_type 1 nano_prc_glb_export placeholder.prc out.prc)
exit_code(export_missing_input     2 nano_prc_glb_export missing.prc out.glb)
exit_code(export_not_implemented   3 nano_prc_glb_export placeholder.prc out.glb)
