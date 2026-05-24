cmake_minimum_required(VERSION 3.20)

project(stackimport LANGUAGES C CXX)

option(STACKIMPORT_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(STACKIMPORT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(STACKIMPORT_ENABLE_VENDOR "Expose vendored conversion support targets" ON)
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

if(STACKIMPORT_ENABLE_VENDOR)
    add_subdirectory(vendor)
endif()

set(STACKIMPORT_SOURCES
    woba.cpp
    picture.cpp
    CStackFile.cpp
    Mac68kDisassembly.cpp
    CBuf.cpp
    byteutils.cpp
    stackimport_c.cpp
    stackimport_logging.cpp
    stackimport_platform_internal.cpp
)

add_library(stackimport_static STATIC ${STACKIMPORT_SOURCES})
add_library(stackimport::static ALIAS stackimport_static)
set_target_properties(stackimport_static PROPERTIES OUTPUT_NAME stackimport)
target_compile_definitions(stackimport_static PUBLIC MAC_CODE=0)
target_include_directories(stackimport_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(stackimport_static PROPERTIES
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/stackimport_c.h"
)

if(STACKIMPORT_ENABLE_VENDOR)
    target_link_libraries(stackimport_static PUBLIC vendor_quill vendor_rang vendor_stb_image_write vendor_dr_wav vendor_rapidjson vendor_snd2wav)
    if(STACKIMPORT_BUILD_VENDOR_TOOLS)
        target_compile_definitions(stackimport_static PRIVATE STACKIMPORT_HAS_RESOURCE_DASM=1)
        target_link_libraries(stackimport_static PUBLIC vendor_resource_file_static)
        add_dependencies(stackimport_static vendor_resource_dasm)
    endif()
endif()

add_executable(stackimport main.cpp)
target_link_libraries(stackimport PRIVATE stackimport_static)

add_executable(stackimport-tests TestsMain.cpp Tests.cpp)
target_link_libraries(stackimport-tests PRIVATE stackimport_static)

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

    foreach(target stackimport_static stackimport stackimport-tests)
        target_compile_options(${target} PRIVATE ${STACKIMPORT_WARNING_FLAGS})
        if(STACKIMPORT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endforeach()
endif()

foreach(target stackimport_static stackimport stackimport-tests)
    stackimport_enable_static_analysis(${target})
    stackimport_disable_exceptions(${target})
endforeach()

if(WIN32)
    foreach(target stackimport_static stackimport stackimport-tests)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS RAPIDJSON_ENDIAN=RAPIDJSON_LITTLEENDIAN)
    endforeach()
endif()

enable_testing()
add_test(NAME stackimport-tests COMMAND stackimport-tests)

add_custom_target(import-stacks
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/import_all_stacks.py" --noprogress
    DEPENDS stackimport
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)
