# cmake helper for rsrcdumppp
# Provides STACKIMPORT_CXX23_WARNING_FLAGS for use in child projects

if(NOT DEFINED STACKIMPORT_CXX23_WARNING_FLAGS)
    set(STACKIMPORT_CXX23_WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wshadow
        -Wcast-align
        -Wcast-qual
        -Wwrite-strings
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wzero-as-null-pointer-constant
    )
endif()