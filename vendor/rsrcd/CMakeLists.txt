cmake_minimum_required(VERSION 3.20)

project(rsrcd
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RSRCD_BUILD_TESTS "Build rsrcd tests" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/STACKIMPORT_CXX23.cmake)

set(RSRCD_HEADERS
    include/rsrcd.hpp
)

add_library(rsrcd INTERFACE)
add_library(rsrcd::rsrcd ALIAS rsrcd)

target_include_directories(rsrcd INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(rsrcd INTERFACE
    ${STACKIMPORT_CXX23_WARNING_FLAGS}
)

if(RSRCD_BUILD_TESTS)
    enable_testing()

    add_executable(rsrcd-tests
        tests/test_main.cpp
    )
    target_link_libraries(rsrcd-tests PRIVATE rsrcd)
    target_compile_options(rsrcd-tests PRIVATE
        ${STACKIMPORT_CXX23_WARNING_FLAGS}
    )

    add_test(NAME rsrcd-tests COMMAND rsrcd-tests)
endif()

install(TARGETS rsrcd
    EXPORT rsrcd-targets
)

install(EXPORT rsrcd-targets
    FILE rsrcd-targets.cmake
    DESTINATION lib/cmake/rsrcd
)

install(FILES ${RSRCD_HEADERS}
    DESTINATION include
)

install(FILES LICENSE
    DESTINATION share/rsrcd
)
