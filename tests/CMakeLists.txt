cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(CMAKE_C_STANDARD 99)

# Unit tests of the code the two tools share. They write their files into
# their own directory under the build tree.
set(PGR_UNIT_DIR ${CMAKE_CURRENT_BINARY_DIR}/unit_out)
file(MAKE_DIRECTORY ${PGR_UNIT_DIR})

foreach(name test_info test_cgltf_link test_math test_hash_dedup
             test_model_path test_glb_read_basic test_glb_write_valid test_prc_write
             test_round_trip test_prc_read test_image test_material_map test_appearance_read
             test_draco_common)
    add_executable(${name} unit/${name}.c)
    target_link_libraries(${name} PRIVATE prc_glb_rw_common)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${PGR_UNIT_DIR})
    # The tests open their files with fopen, which MSVC deprecates.
    if(MSVC)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
if(PRC_GLB_WITH_DRACO)
    set(PGR_EXPECTED_DRACO 1)
else()
    set(PGR_EXPECTED_DRACO 0)
endif()
target_compile_definitions(test_info PRIVATE PGR_EXPECTED_CGLTF_VERSION="${CGLTF_VERSION}"
                                             PGR_EXPECTED_DRACO=${PGR_EXPECTED_DRACO})

# Draco: what does not need Draco runs in every build (test_draco_common);
# the shim and the round trips through it only where Draco is built in.
target_sources(test_draco_common PRIVATE fixtures.c)
if(PRC_GLB_WITH_DRACO)
    foreach(name test_draco_shim test_draco_round_trip)
        add_executable(${name} unit/${name}.c)
        target_link_libraries(${name} PRIVATE prc_glb_rw_common)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${PGR_UNIT_DIR})
        if(MSVC)
            target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
    target_sources(test_draco_round_trip PRIVATE fixtures.c scene_compare.c)
endif()

# The read test's glTF files are made by fixtures.c, independently of the
# writer under test.
target_sources(test_glb_read_basic PRIVATE fixtures.c)
target_sources(test_prc_write PRIVATE fixtures.c)
target_sources(test_material_map PRIVATE fixtures.c)
target_sources(test_appearance_read PRIVATE fixtures.c)

# The round trips also read nanoPRC's own example PDFs and test files, found
# from the nano_prc target wherever nanoPRC is.
target_sources(test_round_trip PRIVATE fixtures.c scene_compare.c)
target_sources(test_prc_read PRIVATE scene_compare.c)
target_compile_definitions(test_round_trip PRIVATE
                           PGR_NANOPRC_DIR="$<TARGET_PROPERTY:nano_prc,SOURCE_DIR>")

# The tools' command lines: --version, and the exit code for each kind of
# failure. The placeholder inputs are empty files: right by name, unreadable
# as content.
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
exit_code(import_unreadable_input  2 nano_prc_glb_import placeholder.glb out.prc out.pdf)

exit_code(export_no_output         1 nano_prc_glb_export placeholder.prc)
exit_code(export_wrong_input_type  1 nano_prc_glb_export placeholder.glb out.glb)
exit_code(export_wrong_output_type 1 nano_prc_glb_export placeholder.prc out.prc)
exit_code(export_missing_input     2 nano_prc_glb_export missing.prc out.glb)
exit_code(export_unreadable_input  2 nano_prc_glb_export placeholder.prc out.glb)

# Draco options: tuning without --draco, or out of range, is a usage error;
# --draco in a build without Draco is unsupported.
exit_code(export_draco_tuning_alone 1 nano_prc_glb_export placeholder.prc out.glb --draco-qpos 12)
exit_code(export_draco_qpos_range   1 nano_prc_glb_export placeholder.prc out.glb --draco --draco-qpos 31)
exit_code(export_draco_speed_range  1 nano_prc_glb_export placeholder.prc out.glb --draco --draco-speed 11)
exit_code(export_draco_not_a_number 1 nano_prc_glb_export placeholder.prc out.glb --draco --draco-quv 8x)
if(PRC_GLB_WITH_DRACO)
    # A real model through both tools with Draco: nanoPRC's example cube.
    set(PGR_CUBE_PDF "$<TARGET_PROPERTY:nano_prc,SOURCE_DIR>/examples/cube.pdf")
    exit_code(export_draco_cube 0 nano_prc_glb_export ${PGR_CUBE_PDF} cube_draco.glb --draco)
    exit_code(import_draco_cube 0 nano_prc_glb_import cube_draco.glb cube_draco.prc)
    set_tests_properties(import_draco_cube PROPERTIES DEPENDS export_draco_cube)
    # ...and the file written is Draco: the extension is required.
    add_test(NAME export_draco_cube_is_draco
             COMMAND ${CMAKE_COMMAND} -DFILE=cube_draco.glb
                     "-DTEXT=\"extensionsRequired\""
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_contains.cmake
             WORKING_DIRECTORY ${PGR_TEST_DIR})
    set_tests_properties(export_draco_cube_is_draco PROPERTIES DEPENDS export_draco_cube)
else()
    exit_code(export_draco_unavailable 3 nano_prc_glb_export placeholder.prc out.glb --draco)
endif()
