project(PackageTest CXX)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

# TODO: rename to Qt6 after branching KF6
find_package(Qt5 5.6 CONFIG REQUIRED
  Core
)

# TODO: rename to Qt6 after branching KF6
add_executable(example example.cpp)
target_link_libraries(example ${CONAN_LIBS}
  Qt5::Core)
