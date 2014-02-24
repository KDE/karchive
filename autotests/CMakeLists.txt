remove_definitions(-DQT_NO_CAST_FROM_ASCII)

include(ECMMarkAsTest)

find_package(Qt5Test ${REQUIRED_QT_VERSION} CONFIG QUIET)

if(NOT Qt5Test_FOUND)
    message(STATUS "Qt5Test not found, autotests will not be built.")
    return()
endif()

macro(KARCHIVE_UNIT_TESTS)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp) # TODO NOGUI
        add_test(karchive-${_testname} ${_testname})
        target_link_libraries(${_testname} PRIVATE KF5::Archive Qt5::Test)
        ecm_mark_as_test(${_testname})
    endforeach(_testname)
endmacro(KARCHIVE_UNIT_TESTS)

karchive_unit_tests(
    karchivetest
    kfiltertest
    deprecatedtest
)

target_compile_definitions(deprecatedtest PRIVATE KARCHIVE_DEPRECATED=)
target_link_libraries(kfiltertest PRIVATE ${ZLIB_LIBRARIES})

########### klimitediodevicetest ###############

add_executable(klimitediodevicetest
    klimitediodevicetest.cpp
    ../src/klimitediodevice.cpp
)
ecm_mark_as_test(klimitediodevicetest)
add_test(karchive-klimitediodevicetest klimitediodevicetest)
target_link_libraries(klimitediodevicetest PRIVATE Qt5::Test)
target_include_directories(klimitediodevicetest
    PRIVATE $<TARGET_PROPERTY:KF5Archive,INTERFACE_INCLUDE_DIRECTORIES>)
