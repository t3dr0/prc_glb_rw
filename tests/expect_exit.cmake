# Run EXE with the ;-separated ARGS and fail unless it exits with EXPECT.
# CTest alone can only tell zero from non-zero.

execute_process(COMMAND "${EXE}" ${ARGS}
                RESULT_VARIABLE code
                OUTPUT_VARIABLE out
                ERROR_VARIABLE err)

if(NOT code STREQUAL "${EXPECT}")
    message(FATAL_ERROR
        "${EXE} ${ARGS}\nexited with ${code}, expected ${EXPECT}\n"
        "stdout:\n${out}\nstderr:\n${err}")
endif()
