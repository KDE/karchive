# KArchive

## Introduction

It features KCompressionDevice, a QIODevice that can compress/uncompress data on
the fly, in GZIP or BZIP2 or XZ/LZMA format.

It also features support for "archive" formats: ZIP, TAR, AR, 7Zip, through a
common API, KArchive.

Here's how to use karchive in your own program, with cmake:

    cmake_minimum_required(VERSION 2.8)
    project(karchive_package_test)
    find_package(KArchive)
    add_executable(karchive_package_test main.cpp)
    target_link_libraries(karchive_package_test KF5::KArchive)

## Links

- Mailing list: <https://mail.kde.org/mailman/listinfo/kde-frameworks-devel>
- IRC channel: #kde-devel on Freenode
- Git repository: <https://projects.kde.org/projects/frameworks/karchive/repository>
