if(NOT DEFINED STACKIMPORT_CLI)
    message(FATAL_ERROR "STACKIMPORT_CLI is required")
endif()
if(NOT DEFINED STACKIMPORT_TEST_DIR)
    message(FATAL_ERROR "STACKIMPORT_TEST_DIR is required")
endif()

file(REMOVE_RECURSE "${STACKIMPORT_TEST_DIR}")
file(MAKE_DIRECTORY "${STACKIMPORT_TEST_DIR}")

set(str_input "${STACKIMPORT_TEST_DIR}/loose_STR.bin")
set(converted_output "${STACKIMPORT_TEST_DIR}/loose_STR.txt")
set(native_output "${STACKIMPORT_TEST_DIR}/loose_STR.native")
set(invalid_output "${STACKIMPORT_TEST_DIR}/invalid.out")

string(ASCII 5 72 101 108 108 111 str_payload)
file(WRITE "${str_input}" "${str_payload}")

execute_process(
    COMMAND "${STACKIMPORT_CLI}" convert --type "STR " --id 12 --output "${converted_output}" "${str_input}"
    RESULT_VARIABLE converted_result
    OUTPUT_VARIABLE converted_stdout
    ERROR_VARIABLE converted_stderr
)
if(NOT converted_result EQUAL 0)
    message(FATAL_ERROR "convert STR failed with ${converted_result}\nstdout=${converted_stdout}\nstderr=${converted_stderr}")
endif()
file(READ "${converted_output}" converted_text)
if(NOT converted_text STREQUAL "Hello")
    message(FATAL_ERROR "converted STR output mismatch: '${converted_text}'")
endif()

execute_process(
    COMMAND "${STACKIMPORT_CLI}" convert --type "STR " --id 12 --native --output "${native_output}" "${str_input}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_stdout
    ERROR_VARIABLE native_stderr
)
if(NOT native_result EQUAL 0)
    message(FATAL_ERROR "native STR convert failed with ${native_result}\nstdout=${native_stdout}\nstderr=${native_stderr}")
endif()
file(READ "${native_output}" native_hex HEX)
if(NOT native_hex STREQUAL "0548656c6c6f")
    message(FATAL_ERROR "native STR output hex mismatch: '${native_hex}'")
endif()

execute_process(
    COMMAND "${STACKIMPORT_CLI}" convert --type BAD --output "${invalid_output}" "${str_input}"
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(NOT invalid_result EQUAL 3)
    message(FATAL_ERROR "invalid type returned ${invalid_result}, expected 3\nstdout=${invalid_stdout}\nstderr=${invalid_stderr}")
endif()
if(EXISTS "${invalid_output}")
    message(FATAL_ERROR "invalid convert unexpectedly created ${invalid_output}")
endif()
