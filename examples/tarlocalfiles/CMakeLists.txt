project(TarLocalFiles)

find_package(KF5Archive ${KF_VERSION} REQUIRED)

add_executable(tarlocalfiles main.cpp)
target_link_libraries(tarlocalfiles KF5::Archive)
