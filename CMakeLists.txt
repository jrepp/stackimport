cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" STACKIMPORT_VERSION_FULL)
string(STRIP "${STACKIMPORT_VERSION_FULL}" STACKIMPORT_VERSION_FULL)
if(NOT STACKIMPORT_VERSION_FULL MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION.txt must be a SemVer 2.0.0 version")
endif()
string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1" STACKIMPORT_VERSION_CORE "${STACKIMPORT_VERSION_FULL}")

project(stackimport VERSION "${STACKIMPORT_VERSION_CORE}" LANGUAGES C CXX)

include(GNUInstallDirs)

option(STACKIMPORT_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(STACKIMPORT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(STACKIMPORT_ENABLE_VENDOR "Expose vendored conversion support targets" ON)
option(STACKIMPORT_BUILD_TESTS "Build stackimport tests" ON)
option(STACKIMPORT_BUILD_VENDOR_TESTS "Build vendored tests in the top-level test suite" ON)
option(STACKIMPORT_ENABLE_CLANG_TIDY "Run clang-tidy during CMake builds" OFF)
option(STACKIMPORT_ENABLE_CPPCHECK "Run cppcheck during CMake builds" OFF)
option(STACKIMPORT_ENABLE_INCLUDE_WHAT_YOU_USE "Run include-what-you-use during CMake builds" OFF)
option(STACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER "Run Clang static analyzer checks through clang-tidy" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

function(stackimport_enable_static_analysis target)
    if(STACKIMPORT_ENABLE_CLANG_TIDY)
        find_program(STACKIMPORT_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
        set(clang_tidy_command
            "${STACKIMPORT_CLANG_TIDY_EXE}"
            "--quiet"
            "--warnings-as-errors="
        )
        if(STACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER)
            list(APPEND clang_tidy_command
                "--checks=clang-analyzer-*,bugprone-*,performance-*,portability-*,readability-*"
            )
        endif()
        set_target_properties(${target} PROPERTIES
            C_CLANG_TIDY "${clang_tidy_command}"
            CXX_CLANG_TIDY "${clang_tidy_command}"
        )
    endif()

    if(STACKIMPORT_ENABLE_CPPCHECK)
        find_program(STACKIMPORT_CPPCHECK_EXE NAMES cppcheck REQUIRED)
        set_target_properties(${target} PROPERTIES
            C_CPPCHECK "${STACKIMPORT_CPPCHECK_EXE};--enable=warning,style,performance,portability;--inline-suppr;--quiet"
            CXX_CPPCHECK "${STACKIMPORT_CPPCHECK_EXE};--enable=warning,style,performance,portability;--inline-suppr;--quiet"
        )
    endif()

    if(STACKIMPORT_ENABLE_INCLUDE_WHAT_YOU_USE)
        find_program(STACKIMPORT_IWYU_EXE NAMES include-what-you-use iwyu REQUIRED)
        set_target_properties(${target} PROPERTIES
            C_INCLUDE_WHAT_YOU_USE "${STACKIMPORT_IWYU_EXE}"
            CXX_INCLUDE_WHAT_YOU_USE "${STACKIMPORT_IWYU_EXE}"
        )
    endif()
endfunction()

function(stackimport_disable_exceptions target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()
endfunction()

enable_testing()

if(STACKIMPORT_ENABLE_VENDOR)
    add_subdirectory(vendor)
endif()

set(STACKIMPORT_SOURCES
    woba.cpp
    picture.cpp
    CStackFile.cpp
    StackImportResourceFork.cpp
    StackImportSoundConverter.cpp
    StackImportMaceDecoder.cpp
    Mac68kDisassembly.cpp
    RomDasm.cpp
    CBuf.cpp
    byteutils.cpp
    stackimport_c.cpp
    stackimport_logging.cpp
    stackimport_platform_internal.cpp
)

if(STACKIMPORT_ENABLE_VENDOR AND STACKIMPORT_BUILD_VENDOR_TOOLS)
    add_library(stackimport_mace_resource_dasm_adapter OBJECT
        vendor/resource_dasm_stackimport_mace_adapter.cpp
        vendor/resource_dasm/src/AudioCodecs.cc
    )
    target_include_directories(stackimport_mace_resource_dasm_adapter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(stackimport_mace_resource_dasm_adapter PRIVATE vendor_resource_file_static)
    add_dependencies(stackimport_mace_resource_dasm_adapter vendor_resource_dasm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(stackimport_mace_resource_dasm_adapter PRIVATE -fexceptions -w)
    elseif(MSVC)
        target_compile_options(stackimport_mace_resource_dasm_adapter PRIVATE /EHsc /w)
    endif()
endif()

add_library(stackimport_static STATIC ${STACKIMPORT_SOURCES})
add_library(stackimport::static ALIAS stackimport_static)
set_target_properties(stackimport_static PROPERTIES OUTPUT_NAME stackimport)
target_compile_definitions(stackimport_static PUBLIC MAC_CODE=0)
target_include_directories(stackimport_static
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(stackimport_static PROPERTIES
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/stackimport_c.h"
)

if(STACKIMPORT_ENABLE_VENDOR)
    target_link_libraries(stackimport_static PRIVATE vendor_quill vendor_rang vendor_stb_image_write vendor_dr_wav vendor_rapidjson vendor_rsrcd)
    if(STACKIMPORT_BUILD_VENDOR_TOOLS)
        target_sources(stackimport_static PRIVATE $<TARGET_OBJECTS:stackimport_mace_resource_dasm_adapter>)
        target_link_libraries(stackimport_static PRIVATE vendor_resource_file_static)
        target_compile_definitions(stackimport_static PRIVATE STACKIMPORT_HAS_RESOURCE_DASM=1)
        add_dependencies(stackimport_static vendor_resource_dasm)
    endif()
endif()

add_library(stackimport_shared SHARED ${STACKIMPORT_SOURCES})
add_library(stackimport::shared ALIAS stackimport_shared)
set_target_properties(stackimport_shared PROPERTIES
    OUTPUT_NAME stackimport_c
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/stackimport_c.h"
)
target_compile_definitions(stackimport_shared
    PRIVATE STACKIMPORT_BUILD_SHARED=1 STACKIMPORT_SHARED=1
    PUBLIC MAC_CODE=0
)
target_include_directories(stackimport_shared
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(STACKIMPORT_ENABLE_VENDOR)
    target_link_libraries(stackimport_shared PRIVATE vendor_quill vendor_rang vendor_stb_image_write vendor_dr_wav vendor_rapidjson vendor_rsrcd)
    if(STACKIMPORT_BUILD_VENDOR_TOOLS)
        target_sources(stackimport_shared PRIVATE $<TARGET_OBJECTS:stackimport_mace_resource_dasm_adapter>)
        target_link_libraries(stackimport_shared PRIVATE vendor_resource_file_static)
        target_compile_definitions(stackimport_shared PRIVATE STACKIMPORT_HAS_RESOURCE_DASM=1)
        add_dependencies(stackimport_shared vendor_resource_dasm)
    endif()
endif()

add_executable(stackimport main.cpp)
target_link_libraries(stackimport PRIVATE stackimport_static)
set(STACKIMPORT_CORE_TARGETS stackimport_static stackimport_shared stackimport)

if(STACKIMPORT_BUILD_TESTS)
    add_executable(stackimport-tests TestsMain.cpp Tests.cpp)
    target_link_libraries(stackimport-tests PRIVATE stackimport_static)
    list(APPEND STACKIMPORT_CORE_TARGETS stackimport-tests)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stackimport.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/stackimport.pc"
    @ONLY
)

install(TARGETS stackimport stackimport_static stackimport_shared
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stackimport.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

if(STACKIMPORT_ENABLE_VENDOR AND STACKIMPORT_BUILD_VENDOR_TOOLS)
    set(STACKIMPORT_VENDOR_ARCHIVE "${CMAKE_BINARY_DIR}/libstackimport-vendor.a")
    set(STACKIMPORT_COMBINED_STATIC_INPUTS "$<TARGET_FILE:stackimport_static>")
    foreach(vendor_static_library IN LISTS STACKIMPORT_VENDOR_STATIC_LIBRARIES)
        string(APPEND STACKIMPORT_COMBINED_STATIC_INPUTS "|${vendor_static_library}")
    endforeach()

    add_custom_command(
        OUTPUT "${STACKIMPORT_VENDOR_ARCHIVE}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${STACKIMPORT_VENDOR_ARCHIVE}"
            "-DINPUTS=${STACKIMPORT_COMBINED_STATIC_INPUTS}"
            "-DINPUT_SEPARATOR=|"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CombineStaticLibraries.cmake"
        DEPENDS stackimport_static vendor-static-artifacts ${STACKIMPORT_VENDOR_STATIC_LIBRARIES}
        COMMENT "Combining stackimport and vendored static libraries"
        VERBATIM
    )
    add_custom_target(stackimport-vendor-static
        DEPENDS "${STACKIMPORT_VENDOR_ARCHIVE}"
    )
endif()

if(STACKIMPORT_STRICT_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        set(STACKIMPORT_WARNING_FLAGS
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wformat=2
            -Wundef
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wwrite-strings
            -Wmissing-declarations
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Wextra-semi
            -Wzero-as-null-pointer-constant
        )
    elseif(MSVC)
        set(STACKIMPORT_WARNING_FLAGS /W4 /permissive-)
    endif()

    foreach(target ${STACKIMPORT_CORE_TARGETS})
        target_compile_options(${target} PRIVATE ${STACKIMPORT_WARNING_FLAGS})
        if(STACKIMPORT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endforeach()
endif()

foreach(target ${STACKIMPORT_CORE_TARGETS})
    stackimport_enable_static_analysis(${target})
    stackimport_disable_exceptions(${target})
endforeach()

if(WIN32)
    foreach(target ${STACKIMPORT_CORE_TARGETS})
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS RAPIDJSON_ENDIAN=RAPIDJSON_LITTLEENDIAN)
    endforeach()
endif()

if(STACKIMPORT_BUILD_TESTS)
    add_test(NAME stackimport-tests COMMAND stackimport-tests)
    find_program(STACKIMPORT_PKG_CONFIG_EXE NAMES pkg-config)
    if(STACKIMPORT_PKG_CONFIG_EXE)
        add_test(
            NAME stackimport-install-pkg-config-smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DSTACKIMPORT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DSTACKIMPORT_C_COMPILER=${CMAKE_C_COMPILER}"
                "-DSTACKIMPORT_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
                "-DPKG_CONFIG_EXECUTABLE=${STACKIMPORT_PKG_CONFIG_EXE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallPkgConfigSmokeTest.cmake"
        )
    endif()
endif()

add_custom_target(import-stacks
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/import_all_stacks.py" --noprogress
    DEPENDS stackimport
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)
