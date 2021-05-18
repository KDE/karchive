project(Unzipper)

find_package(KF5Archive ${KF_VERSION} REQUIRED)

add_executable(unzipper main.cpp)
target_link_libraries(unzipper KF5::Archive)
