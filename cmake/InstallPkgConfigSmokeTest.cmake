if(NOT DEFINED STACKIMPORT_BUILD_DIR)
    message(FATAL_ERROR "STACKIMPORT_BUILD_DIR is required")
endif()
if(NOT DEFINED STACKIMPORT_C_COMPILER)
    message(FATAL_ERROR "STACKIMPORT_C_COMPILER is required")
endif()
if(NOT DEFINED STACKIMPORT_INSTALL_LIBDIR)
    message(FATAL_ERROR "STACKIMPORT_INSTALL_LIBDIR is required")
endif()
if(NOT DEFINED PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "PKG_CONFIG_EXECUTABLE is required")
endif()

set(prefix "${STACKIMPORT_BUILD_DIR}/install-smoke")
set(work_dir "${STACKIMPORT_BUILD_DIR}/install-smoke-build")
file(REMOVE_RECURSE "${prefix}" "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${STACKIMPORT_BUILD_DIR}" --prefix "${prefix}"
    COMMAND_ERROR_IS_FATAL ANY
)

set(pc_path "${prefix}/${STACKIMPORT_INSTALL_LIBDIR}/pkgconfig")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PKG_CONFIG_PATH=${pc_path}" "${PKG_CONFIG_EXECUTABLE}" --cflags --libs stackimport
    OUTPUT_VARIABLE stackimport_pkg_config_flags
    COMMAND_ERROR_IS_FATAL ANY
)
string(STRIP "${stackimport_pkg_config_flags}" stackimport_pkg_config_flags)
separate_arguments(stackimport_pkg_config_flags UNIX_COMMAND "${stackimport_pkg_config_flags}")

set(source_file "${work_dir}/stackimport_smoke.c")
set(executable_file "${work_dir}/stackimport_smoke")
file(WRITE "${source_file}" [=[
#include <stdint.h>
#include "stackimport_c.h"

int main(void)
{
    const uint32_t version = stackimport_api_version();
    return stackimport_status_string(STACKIMPORT_STATUS_OK) != 0 && version != 0u ? 0 : 1;
}
]=])

execute_process(
    COMMAND "${STACKIMPORT_C_COMPILER}" "${source_file}" ${stackimport_pkg_config_flags} -o "${executable_file}"
    COMMAND_ERROR_IS_FATAL ANY
)
