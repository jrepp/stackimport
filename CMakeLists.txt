cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
    set(STACKIMPORT_ENV_MACOSX_DEPLOYMENT_TARGET "")
    if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(STACKIMPORT_ENV_MACOSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}")
    endif()
    if(NOT STACKIMPORT_ENV_MACOSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${STACKIMPORT_ENV_MACOSX_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS deployment target" FORCE)
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    endif()
endif()

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
set(STACKIMPORT_INSTALL_FRAMEWORKDIR "Frameworks" CACHE PATH "Install destination for Apple frameworks")

if(NOT STACKIMPORT_ENABLE_VENDOR)
    message(FATAL_ERROR "STACKIMPORT_ENABLE_VENDOR=OFF is unsupported; remove unused vendored dependencies instead of compiling without them")
endif()
set(STACKIMPORT_ENABLE_VENDOR ON CACHE BOOL "Expose vendored conversion support targets" FORCE)

set(STACKIMPORT_BUILD_VENDOR_TOOLS_DEFAULT ON)
if(WIN32)
    set(STACKIMPORT_BUILD_VENDOR_TOOLS_DEFAULT OFF)
endif()
option(STACKIMPORT_BUILD_VENDOR_TOOLS "Build vendored conversion tools required by StackImport resource coverage" ${STACKIMPORT_BUILD_VENDOR_TOOLS_DEFAULT})
if(NOT STACKIMPORT_BUILD_VENDOR_TOOLS)
    message(WARNING "Vendored conversion tools are disabled; resource coverage is reduced")
endif()

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
set(STACKIMPORT_COMBINE_STATIC_ARCHIVE OFF)
if(STACKIMPORT_ENABLE_VENDOR AND STACKIMPORT_BUILD_VENDOR_TOOLS AND NOT MSVC)
    set(STACKIMPORT_COMBINE_STATIC_ARCHIVE ON)
endif()

set(STACKIMPORT_SOURCES
    woba.cpp
    picture.cpp
    CStackFile.cpp
    StackImportResourceFork.cpp
    StackImportSoundConverter.cpp
    StackImportMaceDecoder.cpp
    StackImportPngWriter.cpp
    StackImportResourceTransforms.cpp
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
        vendor/phosg_stackimport_hash_adapter.cpp
        vendor/resource_dasm_stackimport_mace_adapter.cpp
        vendor/resource_dasm_stackimport_disassembly_adapter.cpp
        vendor/resource_dasm_stackimport_pict_adapter.cpp
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
if(STACKIMPORT_COMBINE_STATIC_ARCHIVE)
    set_target_properties(stackimport_static PROPERTIES OUTPUT_NAME stackimport_core)
else()
    set_target_properties(stackimport_static PROPERTIES OUTPUT_NAME stackimport)
endif()
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
    set(STACKIMPORT_CORE_VENDOR_PUBLIC_LIBRARIES
        vendor_rsrcd
    )
    set(STACKIMPORT_CORE_VENDOR_PRIVATE_LIBRARIES
        vendor_quill
        vendor_rang
        vendor_stb_image_write
        vendor_dr_wav
        vendor_rapidjson
        vendor_ppcd
        vendor_snd2wav
    )

    target_link_libraries(stackimport_static
        PUBLIC ${STACKIMPORT_CORE_VENDOR_PUBLIC_LIBRARIES}
        PRIVATE ${STACKIMPORT_CORE_VENDOR_PRIVATE_LIBRARIES}
    )
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
    VERSION "${STACKIMPORT_VERSION_CORE}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    MACOSX_RPATH ON
    INSTALL_NAME_DIR "@rpath"
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
    target_link_libraries(stackimport_shared
        PUBLIC ${STACKIMPORT_CORE_VENDOR_PUBLIC_LIBRARIES}
        PRIVATE ${STACKIMPORT_CORE_VENDOR_PRIVATE_LIBRARIES}
    )
    if(STACKIMPORT_BUILD_VENDOR_TOOLS)
        target_sources(stackimport_shared PRIVATE $<TARGET_OBJECTS:stackimport_mace_resource_dasm_adapter>)
        target_link_libraries(stackimport_shared PRIVATE vendor_resource_file_static)
        target_compile_definitions(stackimport_shared PRIVATE STACKIMPORT_HAS_RESOURCE_DASM=1)
        add_dependencies(stackimport_shared vendor_resource_dasm)
    endif()
endif()

if(APPLE)
    set_source_files_properties(stackimport_c.h PROPERTIES MACOSX_PACKAGE_LOCATION Headers)
    add_library(stackimport_framework SHARED ${STACKIMPORT_SOURCES} stackimport_c.h)
    add_library(stackimport::framework ALIAS stackimport_framework)
    set_target_properties(stackimport_framework PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        OUTPUT_NAME StackImport
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        VERSION "${STACKIMPORT_VERSION_CORE}"
        SOVERSION "${PROJECT_VERSION_MAJOR}"
        MACOSX_RPATH ON
        MACOSX_FRAMEWORK_IDENTIFIER "com.jrepp.stackimport"
        MACOSX_FRAMEWORK_BUNDLE_NAME "StackImport"
        MACOSX_FRAMEWORK_BUNDLE_VERSION "${STACKIMPORT_VERSION_FULL}"
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${STACKIMPORT_VERSION_CORE}"
        MACOSX_FRAMEWORK_ICON_FILE ""
        PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/stackimport_c.h"
    )
    target_compile_definitions(stackimport_framework
        PRIVATE STACKIMPORT_BUILD_SHARED=1 STACKIMPORT_SHARED=1
        PUBLIC MAC_CODE=0
    )
    target_include_directories(stackimport_framework
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
            "$<INSTALL_INTERFACE:${STACKIMPORT_INSTALL_FRAMEWORKDIR}/StackImport.framework/Headers>"
    )

    if(STACKIMPORT_ENABLE_VENDOR)
        target_link_libraries(stackimport_framework
            PUBLIC ${STACKIMPORT_CORE_VENDOR_PUBLIC_LIBRARIES}
            PRIVATE ${STACKIMPORT_CORE_VENDOR_PRIVATE_LIBRARIES}
        )
        if(STACKIMPORT_BUILD_VENDOR_TOOLS)
            target_sources(stackimport_framework PRIVATE $<TARGET_OBJECTS:stackimport_mace_resource_dasm_adapter>)
            target_link_libraries(stackimport_framework PRIVATE vendor_resource_file_static)
            target_compile_definitions(stackimport_framework PRIVATE STACKIMPORT_HAS_RESOURCE_DASM=1)
            add_dependencies(stackimport_framework vendor_resource_dasm)
        endif()
    endif()
endif()

add_executable(stackimport main.cpp)
target_link_libraries(stackimport PRIVATE stackimport_static)
set(STACKIMPORT_CORE_TARGETS stackimport_static stackimport_shared stackimport)
if(TARGET stackimport_framework)
    list(APPEND STACKIMPORT_CORE_TARGETS stackimport_framework)
endif()

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

install(TARGETS stackimport stackimport_shared
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
if(TARGET stackimport_framework)
    install(TARGETS stackimport_framework
        FRAMEWORK DESTINATION "${STACKIMPORT_INSTALL_FRAMEWORKDIR}"
    )
endif()
if(STACKIMPORT_COMBINE_STATIC_ARCHIVE)
    set(STACKIMPORT_VENDOR_ARCHIVE "${CMAKE_BINARY_DIR}/libstackimport.a")
    install(FILES "${STACKIMPORT_VENDOR_ARCHIVE}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
else()
    install(TARGETS stackimport_static
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stackimport.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

if(STACKIMPORT_COMBINE_STATIC_ARCHIVE)
    set(STACKIMPORT_COMBINED_STATIC_INPUTS "$<TARGET_FILE:stackimport_static>")
    foreach(vendor_static_library_target IN LISTS STACKIMPORT_VENDOR_STATIC_LIBRARY_TARGETS)
        string(APPEND STACKIMPORT_COMBINED_STATIC_INPUTS "|$<TARGET_FILE:${vendor_static_library_target}>")
    endforeach()
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
            ${STACKIMPORT_VENDOR_STATIC_LIBRARY_TARGETS}
        COMMENT "Combining stackimport and vendored static libraries"
        VERBATIM
    )
    add_custom_target(stackimport-vendor-static ALL
        DEPENDS "${STACKIMPORT_VENDOR_ARCHIVE}"
    )
endif()

set(STACKIMPORT_RELEASE_TARGETS
    stackimport
    stackimport_shared
    stackimport_static
)
if(TARGET stackimport-vendor-static)
    list(APPEND STACKIMPORT_RELEASE_TARGETS stackimport-vendor-static)
endif()
if(TARGET stackimport_framework)
    list(APPEND STACKIMPORT_RELEASE_TARGETS stackimport_framework)
endif()
add_custom_target(stackimport-release
    DEPENDS ${STACKIMPORT_RELEASE_TARGETS}
)
if(APPLE)
    add_custom_target(stackimport-macos-dylib
        DEPENDS stackimport_shared
        COMMENT "Build the macOS-compatible stackimport_c dylib"
    )
    add_custom_target(stackimport-macos-framework
        DEPENDS stackimport_framework
        COMMENT "Build the StackImport.framework bundle"
    )
endif()

set(STACKIMPORT_RELEASE_PLATFORM "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(STACKIMPORT_RELEASE_PACKAGE_NAME "stackimport-${STACKIMPORT_VERSION_FULL}-${STACKIMPORT_RELEASE_PLATFORM}")
set(STACKIMPORT_RELEASE_ROOT "${CMAKE_BINARY_DIR}/release")
set(STACKIMPORT_RELEASE_PACKAGE_DIR "${STACKIMPORT_RELEASE_ROOT}/${STACKIMPORT_RELEASE_PACKAGE_NAME}")
set(STACKIMPORT_RELEASE_PACKAGE_ZIP "${STACKIMPORT_RELEASE_ROOT}/${STACKIMPORT_RELEASE_PACKAGE_NAME}.zip")
file(MAKE_DIRECTORY "${STACKIMPORT_RELEASE_ROOT}")
add_custom_target(stackimport-release-package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${STACKIMPORT_RELEASE_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${STACKIMPORT_RELEASE_ROOT}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG> --prefix "${STACKIMPORT_RELEASE_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${STACKIMPORT_RELEASE_PACKAGE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${STACKIMPORT_RELEASE_PACKAGE_ZIP}" --format=zip -- "${STACKIMPORT_RELEASE_PACKAGE_NAME}"
    WORKING_DIRECTORY "${STACKIMPORT_RELEASE_ROOT}"
    DEPENDS stackimport-release
    COMMENT "Packaging ${STACKIMPORT_RELEASE_PACKAGE_ZIP}"
    VERBATIM
)

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
    if(STACKIMPORT_PKG_CONFIG_EXE AND NOT WIN32)
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
    if(CMAKE_NM)
        add_test(
            NAME stackimport-shared-symbol-smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DSTACKIMPORT_SHARED_LIBRARY=$<TARGET_FILE:stackimport_shared>"
                "-DSTACKIMPORT_NM_EXECUTABLE=${CMAKE_NM}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SharedLibrarySymbolSmokeTest.cmake"
        )
    endif()
endif()

add_custom_target(import-stacks
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/import_all_stacks.py" --noprogress
    DEPENDS stackimport
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
)
