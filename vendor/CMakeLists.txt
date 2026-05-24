include(ExternalProject)

option(STACKIMPORT_BUILD_VENDOR_TOOLS "Enable local builds for vendored conversion tools" ON)
option(STACKIMPORT_VENDOR_STRICT_WARNINGS "Build vendored tools with stricter compiler warnings where supported" ON)
option(STACKIMPORT_VENDOR_WARNINGS_AS_ERRORS "Treat local vendored library warnings as errors" ${STACKIMPORT_WARNINGS_AS_ERRORS})

find_package(Threads REQUIRED)
find_package(ZLIB)

set(STACKIMPORT_VENDOR_CXX_WARNING_FLAGS)
set(STACKIMPORT_VENDOR_CMAKE_WARNING_FLAGS)
set(STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS)
if(STACKIMPORT_VENDOR_STRICT_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        set(STACKIMPORT_VENDOR_CXX_WARNING_FLAGS
            -Wall
            -Wextra
            -Wpedantic
            -Wformat=2
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wwrite-strings
            -Wundef
        )
        set(STACKIMPORT_VENDOR_CMAKE_WARNING_FLAGS
            -Wpedantic
            -Wformat=2
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
            # resource_dasm models variable-length binary records with zero-length arrays.
            list(APPEND STACKIMPORT_VENDOR_CMAKE_WARNING_FLAGS -Wno-zero-length-array)
        endif()
    elseif(MSVC)
        set(STACKIMPORT_VENDOR_CXX_WARNING_FLAGS /W4)
    endif()
endif()

if(STACKIMPORT_VENDOR_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        set(STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS -Werror)
    elseif(MSVC)
        set(STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS /WX)
    endif()
endif()

function(stackimport_configure_vendor_cxx_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()

add_library(vendor_quill INTERFACE)
target_include_directories(vendor_quill SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/quill/include")
target_compile_features(vendor_quill INTERFACE cxx_std_23)
target_compile_definitions(vendor_quill INTERFACE
    QUILL_NO_EXCEPTIONS
    QUILL_DISABLE_NON_PREFIXED_MACROS
)
target_link_libraries(vendor_quill INTERFACE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(vendor_quill INTERFACE -Wno-gnu-zero-variadic-macro-arguments)
endif()

add_library(vendor_rang INTERFACE)
target_include_directories(vendor_rang SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/rang/include")
target_compile_features(vendor_rang INTERFACE cxx_std_23)

add_library(vendor_stb_image_write INTERFACE)
target_include_directories(vendor_stb_image_write SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
target_compile_features(vendor_stb_image_write INTERFACE c_std_17 cxx_std_23)

add_library(vendor_dr_wav INTERFACE)
target_include_directories(vendor_dr_wav SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dr_wav")
target_compile_features(vendor_dr_wav INTERFACE c_std_17 cxx_std_23)

add_library(vendor_rapidjson INTERFACE)
target_include_directories(vendor_rapidjson SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include")
target_compile_features(vendor_rapidjson INTERFACE cxx_std_23)

add_library(vendor_ppcd STATIC
    ppcd/ppcd.cpp
)
target_include_directories(vendor_ppcd SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ppcd")
stackimport_configure_vendor_cxx_target(vendor_ppcd)
target_compile_options(vendor_ppcd PRIVATE
    ${STACKIMPORT_VENDOR_CXX_WARNING_FLAGS}
    ${STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    # PPCD is C++98-era code; keep the shared baseline and suppress legacy noise.
    target_compile_options(vendor_ppcd PRIVATE
        -Wno-writable-strings
        -Wno-c++11-long-long
        -Wno-cast-align
        -Wno-deprecated-declarations
        -Wno-format
        -Wno-misleading-indentation
        -Wno-unused-but-set-variable
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-unused-variable
    )
elseif(MSVC)
    target_compile_options(vendor_ppcd PRIVATE /Zc:strictStrings-)
endif()

add_library(vendor_snd2wav STATIC
    snd2wav/snd2wav/snd2wav.cpp
)
target_include_directories(vendor_snd2wav SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/snd2wav/snd2wav")
stackimport_configure_vendor_cxx_target(vendor_snd2wav)
target_compile_options(vendor_snd2wav PRIVATE
    ${STACKIMPORT_VENDOR_CXX_WARNING_FLAGS}
    ${STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    # snd2wav is an upstream snapshot; suppress known legacy-noise after the baseline.
    target_compile_options(vendor_snd2wav PRIVATE
        -Wno-old-style-cast
        -Wno-conversion
        -Wno-shadow
        -Wno-sign-compare
        -Wno-sign-conversion
    )
endif()

add_library(vendor_rsrcd INTERFACE)
target_include_directories(vendor_rsrcd SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/rsrcd/include")
target_compile_options(vendor_rsrcd INTERFACE
    ${STACKIMPORT_VENDOR_CXX_WARNING_FLAGS}
    ${STACKIMPORT_VENDOR_WARNING_ERROR_FLAGS}
)

add_custom_target(vendor-header-libraries
    DEPENDS vendor_ppcd vendor_snd2wav
    COMMENT "Header-only and local vendored conversion libraries are available"
)

if(STACKIMPORT_BUILD_VENDOR_TOOLS)
    set(VENDOR_BUILD_DIR "${CMAKE_BINARY_DIR}/vendor-build")
    set(VENDOR_INSTALL_DIR "${CMAKE_BINARY_DIR}/vendor-install")
    file(MAKE_DIRECTORY "${VENDOR_INSTALL_DIR}/include" "${VENDOR_INSTALL_DIR}/lib")
    set(STACKIMPORT_VENDOR_BUILD_DIR "${VENDOR_BUILD_DIR}" PARENT_SCOPE)
    set(STACKIMPORT_VENDOR_INSTALL_DIR "${VENDOR_INSTALL_DIR}" PARENT_SCOPE)
    set(STACKIMPORT_VENDOR_STATIC_LIBRARIES
        "${VENDOR_INSTALL_DIR}/lib/libresource_file.a"
        "${VENDOR_INSTALL_DIR}/lib/libphosg.a"
        "${VENDOR_BUILD_DIR}/deark/src/deark1.a"
        "${VENDOR_BUILD_DIR}/deark/src/deark2.a"
        "${VENDOR_BUILD_DIR}/deark/modules/mods-ab.a"
        "${VENDOR_BUILD_DIR}/deark/modules/mods-ch.a"
        "${VENDOR_BUILD_DIR}/deark/modules/mods-io.a"
        "${VENDOR_BUILD_DIR}/deark/modules/mods-pq.a"
        "${VENDOR_BUILD_DIR}/deark/modules/mods-rz.a"
        PARENT_SCOPE
    )
    add_library(vendor_phosg_static STATIC IMPORTED GLOBAL)
    set_target_properties(vendor_phosg_static PROPERTIES
        IMPORTED_LOCATION "${VENDOR_INSTALL_DIR}/lib/libphosg.a"
        INTERFACE_INCLUDE_DIRECTORIES "${VENDOR_INSTALL_DIR}/include"
    )

    add_library(vendor_resource_file_static STATIC IMPORTED GLOBAL)
    set_target_properties(vendor_resource_file_static PROPERTIES
        IMPORTED_LOCATION "${VENDOR_INSTALL_DIR}/lib/libresource_file.a"
        INTERFACE_INCLUDE_DIRECTORIES "${VENDOR_INSTALL_DIR}/include"
        INTERFACE_LINK_LIBRARIES "vendor_phosg_static;z"
    )

    find_program(VENDOR_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

    set(VENDOR_CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${VENDOR_INSTALL_DIR}"
        "-DCMAKE_C_STANDARD=17"
        "-DCMAKE_C_STANDARD_REQUIRED=ON"
        "-DCMAKE_C_EXTENSIONS=OFF"
        "-DCMAKE_CXX_STANDARD=23"
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
        "-DCMAKE_CXX_EXTENSIONS=OFF"
        "-DCMAKE_BUILD_TYPE=$<CONFIG>"
    )

    if(STACKIMPORT_VENDOR_CMAKE_WARNING_FLAGS)
        # The CMake-based vendor projects already use -Werror, so keep forwarded
        # flags focused on high-signal diagnostics that they currently tolerate.
        list(JOIN STACKIMPORT_VENDOR_CMAKE_WARNING_FLAGS " " VENDOR_CMAKE_WARNING_FLAGS)
        list(APPEND VENDOR_CMAKE_ARGS
            "-DCMAKE_C_FLAGS=${VENDOR_CMAKE_WARNING_FLAGS}"
            "-DCMAKE_CXX_FLAGS=${VENDOR_CMAKE_WARNING_FLAGS}"
        )
    endif()

    if(STACKIMPORT_ENABLE_CLANG_TIDY)
        find_program(STACKIMPORT_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
        set(VENDOR_CLANG_TIDY_COMMAND "${STACKIMPORT_CLANG_TIDY_EXE};--quiet;--warnings-as-errors=")
        if(STACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER)
            string(APPEND VENDOR_CLANG_TIDY_COMMAND ";--checks=clang-analyzer-*,bugprone-*,performance-*,portability-*,readability-*")
        endif()
        list(APPEND VENDOR_CMAKE_ARGS
            "-DCMAKE_C_CLANG_TIDY=${VENDOR_CLANG_TIDY_COMMAND}"
            "-DCMAKE_CXX_CLANG_TIDY=${VENDOR_CLANG_TIDY_COMMAND}"
        )
    endif()

    if(STACKIMPORT_ENABLE_CPPCHECK)
        find_program(STACKIMPORT_CPPCHECK_EXE NAMES cppcheck REQUIRED)
        list(APPEND VENDOR_CMAKE_ARGS
            "-DCMAKE_C_CPPCHECK=${STACKIMPORT_CPPCHECK_EXE};--enable=warning,style,performance,portability;--inline-suppr;--quiet"
            "-DCMAKE_CXX_CPPCHECK=${STACKIMPORT_CPPCHECK_EXE};--enable=warning,style,performance,portability;--inline-suppr;--quiet"
        )
    endif()

    set(DEARK_CFLAGS "-std=c17 -g -O2 -Wall -Wextra -Wmissing-prototypes -Wformat-security -Wno-unused-parameter")
    set(DEARK_LDFLAGS "-Wall")
    if(STACKIMPORT_VENDOR_STRICT_WARNINGS)
        # Deark intentionally performs many byte/color narrowing conversions and
        # uses nested locals in old-style decoder code; keep strict C hygiene flags
        # that the snapshot currently tolerates cleanly.
        string(APPEND DEARK_CFLAGS " -Wpedantic -Wstrict-prototypes -Wcast-align")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
            string(APPEND DEARK_CFLAGS " -Wno-empty-translation-unit")
        endif()
        string(APPEND DEARK_LDFLAGS " -Wextra")
    endif()
    if(STACKIMPORT_VENDOR_WARNINGS_AS_ERRORS)
        string(APPEND DEARK_CFLAGS " -Werror")
    endif()

    ExternalProject_Add(vendor_phosg
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/phosg"
        BINARY_DIR "${VENDOR_BUILD_DIR}/phosg"
        INSTALL_DIR "${VENDOR_INSTALL_DIR}"
        CMAKE_ARGS
            ${VENDOR_CMAKE_ARGS}
            "-DPHOSG_SKIP_PROCESS_TEST=1"
            "-DPHOSG_WARNINGS_AS_ERRORS=${STACKIMPORT_VENDOR_WARNINGS_AS_ERRORS}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
        EXCLUDE_FROM_ALL TRUE
    )

    ExternalProject_Add(vendor_resource_dasm
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource_dasm"
        BINARY_DIR "${VENDOR_BUILD_DIR}/resource_dasm"
        INSTALL_DIR "${VENDOR_INSTALL_DIR}"
        CMAKE_ARGS
            ${VENDOR_CMAKE_ARGS}
            "-DCMAKE_PREFIX_PATH=${VENDOR_INSTALL_DIR}"
            "-DDISABLE_SDL=1"
            "-DRESOURCE_DASM_WARNINGS_AS_ERRORS=${STACKIMPORT_VENDOR_WARNINGS_AS_ERRORS}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
        DEPENDS vendor_phosg
        EXCLUDE_FROM_ALL TRUE
    )

    add_custom_target(vendor_deark
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${VENDOR_BUILD_DIR}/deark"
        COMMAND "${VENDOR_MAKE_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}/deark" "DEARK_OBJDIR=${VENDOR_BUILD_DIR}/deark" "CFLAGS=${DEARK_CFLAGS}" "LDFLAGS=${DEARK_LDFLAGS}" dep
        COMMAND "${VENDOR_MAKE_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}/deark" "DEARK_OBJDIR=${VENDOR_BUILD_DIR}/deark" "CFLAGS=${DEARK_CFLAGS}" "LDFLAGS=${DEARK_LDFLAGS}"
        BYPRODUCTS "${VENDOR_BUILD_DIR}/deark/deark"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deark"
        COMMENT "Building vendored Deark"
        VERBATIM
    )

    add_custom_target(vendor-tools
        DEPENDS vendor-header-libraries vendor_deark vendor_resource_dasm
    )

    add_custom_target(vendor-static-artifacts
        DEPENDS vendor_deark vendor_resource_dasm
    )
    add_dependencies(vendor_phosg_static vendor_phosg)
    add_dependencies(vendor_resource_file_static vendor_resource_dasm)
else()
    add_custom_target(vendor-tools
        DEPENDS vendor-header-libraries
        COMMENT "Vendored tool builds are disabled"
    )
    add_custom_target(vendor-static-artifacts
        DEPENDS vendor-header-libraries
    )
endif()
