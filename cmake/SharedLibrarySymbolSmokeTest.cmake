if(NOT DEFINED STACKIMPORT_SHARED_LIBRARY)
    message(FATAL_ERROR "STACKIMPORT_SHARED_LIBRARY is required")
endif()
if(NOT DEFINED STACKIMPORT_NM_EXECUTABLE)
    message(FATAL_ERROR "STACKIMPORT_NM_EXECUTABLE is required")
endif()

execute_process(
    COMMAND "${STACKIMPORT_NM_EXECUTABLE}" -g "${STACKIMPORT_SHARED_LIBRARY}"
    OUTPUT_VARIABLE stackimport_symbols
    ERROR_VARIABLE stackimport_nm_error
    RESULT_VARIABLE stackimport_nm_result
)
if(NOT stackimport_nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed: ${stackimport_nm_error}")
endif()

set(required_symbols
    stackimport_api_version
    stackimport_status_string
    stackimport_allocator_init
    stackimport_platform_init
    stackimport_import_options_init
    stackimport_context_size
    stackimport_context_alignment
    stackimport_context_init
    stackimport_context_init_with_platform
    stackimport_context_deinit
    stackimport_context_create
    stackimport_context_create_with_platform
    stackimport_context_destroy
    stackimport_import
    stackimport_convert_snd_to_wav
)

foreach(symbol IN LISTS required_symbols)
    if(NOT stackimport_symbols MATCHES "(^|[\n\r])[^\\n\\r]*[ _]${symbol}([\n\r]|$)")
        message(FATAL_ERROR "Missing exported symbol: ${symbol}")
    endif()
endforeach()
