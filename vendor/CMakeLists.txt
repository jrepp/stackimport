include(ExternalProject)

option(STACKIMPORT_BUILD_VENDOR_TOOLS "Enable local builds for vendored conversion tools" ON)
option(STACKIMPORT_VENDOR_STRICT_WARNINGS "Build vendored tools with stricter compiler warnings where supported" ON)

add_library(vendor_stb_image_write INTERFACE)
target_include_directories(vendor_stb_image_write INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
target_compile_features(vendor_stb_image_write INTERFACE c_std_17 cxx_std_23)

add_library(vendor_dr_wav INTERFACE)
target_include_directories(vendor_dr_wav INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dr_wav")
target_compile_features(vendor_dr_wav INTERFACE c_std_17 cxx_std_23)

add_library(vendor_rapidjson INTERFACE)
target_include_directories(vendor_rapidjson SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include")
target_compile_features(vendor_rapidjson INTERFACE cxx_std_23)

add_library(vendor_snd2wav STATIC
    snd2wav/snd2wav/snd2wav.cpp
)
target_include_directories(vendor_snd2wav PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/snd2wav/snd2wav")
target_compile_features(vendor_snd2wav PUBLIC cxx_std_23)
set_target_properties(vendor_snd2wav PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(vendor_snd2wav PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-old-style-cast
        -Wno-conversion
        -Wno-sign-conversion
    )
endif()

add_custom_target(vendor-header-libraries
    DEPENDS vendor_snd2wav
    COMMENT "Header-only and local vendored conversion libraries are available"
)

if(STACKIMPORT_BUILD_VENDOR_TOOLS)
    set(VENDOR_BUILD_DIR "${CMAKE_BINARY_DIR}/vendor-build")
    set(VENDOR_INSTALL_DIR "${CMAKE_BINARY_DIR}/vendor-install")
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
        string(APPEND DEARK_CFLAGS " -Wpedantic -Wstrict-prototypes -Wformat=2 -Wshadow -Wconversion -Wsign-conversion -Wcast-align -Wcast-qual -Wwrite-strings")
        string(APPEND DEARK_LDFLAGS " -Wextra")
    endif()

    ExternalProject_Add(vendor_phosg
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/phosg"
        BINARY_DIR "${VENDOR_BUILD_DIR}/phosg"
        INSTALL_DIR "${VENDOR_INSTALL_DIR}"
        CMAKE_ARGS
            ${VENDOR_CMAKE_ARGS}
            "-DPHOSG_SKIP_PROCESS_TEST=1"
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
else()
    add_custom_target(vendor-tools
        DEPENDS vendor-header-libraries
        COMMENT "Vendored tool builds are disabled"
    )
    add_custom_target(vendor-static-artifacts
        DEPENDS vendor-header-libraries
    )
endif()
