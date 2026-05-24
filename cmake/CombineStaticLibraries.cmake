if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

if(NOT DEFINED INPUTS)
    message(FATAL_ERROR "INPUTS is required")
endif()

if(NOT DEFINED INPUT_SEPARATOR)
    set(INPUT_SEPARATOR "|")
endif()

string(REPLACE "${INPUT_SEPARATOR}" ";" INPUT_LIST "${INPUTS}")

foreach(input IN LISTS INPUT_LIST)
    if(NOT EXISTS "${input}")
        message(FATAL_ERROR "Static library input does not exist: ${input}")
    endif()
endforeach()

file(REMOVE "${OUTPUT}")

if(APPLE)
    find_program(LIBTOOL_EXECUTABLE NAMES libtool REQUIRED)
    execute_process(
        COMMAND "${LIBTOOL_EXECUTABLE}" -static -no_warning_for_no_symbols -o "${OUTPUT}" ${INPUT_LIST}
        RESULT_VARIABLE combine_result
    )
else()
    find_program(AR_EXECUTABLE NAMES ar REQUIRED)
    set(mri_script "${OUTPUT}.mri")
    file(WRITE "${mri_script}" "CREATE ${OUTPUT}\n")
    foreach(input IN LISTS INPUT_LIST)
        file(APPEND "${mri_script}" "ADDLIB ${input}\n")
    endforeach()
    file(APPEND "${mri_script}" "SAVE\nEND\n")
    execute_process(
        COMMAND "${AR_EXECUTABLE}" -M
        INPUT_FILE "${mri_script}"
        RESULT_VARIABLE combine_result
    )
    file(REMOVE "${mri_script}")
endif()

if(NOT combine_result EQUAL 0)
    message(FATAL_ERROR "Failed to combine static libraries into ${OUTPUT}")
endif()
