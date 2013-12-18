remove_definitions(-DQT_NO_CAST_FROM_ASCII)

include(ECMMarkAsTest)

find_package(Qt5 5.2.0 CONFIG REQUIRED Test)

macro(KARCHIVE_UNIT_TESTS)
   foreach(_testname ${ARGN})
      add_executable(${_testname} ${_testname}.cpp) # TODO NOGUI
      add_test(karchive-${_testname} ${_testname})
      target_link_libraries(${_testname} KF5::Archive Qt5::Test)
      ecm_mark_as_test(${_testname})
   endforeach(_testname)
endmacro(KARCHIVE_UNIT_TESTS)

karchive_unit_tests(
  karchivetest
)

########### klimitediodevicetest ###############

add_executable(klimitediodevicetest klimitediodevicetest.cpp ../src/klimitediodevice.cpp)
ecm_mark_as_test(klimitediodevicetest)
add_test(karchive-klimitediodevicetest klimitediodevicetest)
target_link_libraries(klimitediodevicetest Qt5::Test )
target_include_directories(klimitediodevicetest PRIVATE $<TARGET_PROPERTY:KF5Archive,INTERFACE_INCLUDE_DIRECTORIES>)

########### kfiltertest ###############

add_executable(kfiltertest kfiltertest.cpp)
ecm_mark_as_test(kfiltertest)
add_test(karchive-kfiltertest kfiltertest)
target_link_libraries(kfiltertest Qt5::Test KF5::Archive ${ZLIB_LIBRARIES})
