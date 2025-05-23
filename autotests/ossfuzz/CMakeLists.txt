# SPDX-FileCopyrightText: 2025 Azhar Momin <azhar.momin@kdemail.net>
# SPDX-License-Identifier: BSD-2-Clause

if(DEFINED ENV{LIB_FUZZING_ENGINE})
    set(fuzzing_engine $ENV{LIB_FUZZING_ENGINE})
else()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(fuzzing_engine -fsanitize=fuzzer)
    else()
        message(FATAL_ERROR "Fuzzing engine not supported")
    endif()
endif()

function(add_fuzzer target_name source_file define_name define_value)
    add_executable(${target_name} ${source_file})

    target_compile_definitions(
        ${target_name}
        PRIVATE ${define_name}=${define_value}
    )

    target_link_libraries(
        ${target_name}
        PRIVATE KF6Archive ${fuzzing_engine}
    )

    set_target_properties(
        ${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/fuzzers
    )
endfunction()


if(LIBLZMA_FOUND)
    add_fuzzer(k7z_fuzzer karchive_fuzzer.cc HANDLER K7Zip)
    if(OpenSSL_FOUND)
        target_compile_definitions(k7z_fuzzer PRIVATE USE_PASSWORD=1)
    endif()
endif()

add_fuzzer(kar_fuzzer karchive_fuzzer.cc HANDLER KAr)
add_fuzzer(ktar_fuzzer karchive_fuzzer.cc HANDLER KTar)
add_fuzzer(kzip_fuzzer karchive_fuzzer.cc HANDLER KZip)

if(BZIP2_FOUND)
    add_fuzzer(ktar_bz2_fuzzer kcompressiondevice_fuzzer.cc DEVICE BZip2)
endif()

add_fuzzer(ktar_gz_fuzzer kcompressiondevice_fuzzer.cc DEVICE GZip)

if(LIBLZMA_FOUND)
    add_fuzzer(ktar_lz_fuzzer kcompressiondevice_fuzzer.cc DEVICE Lz)
    add_fuzzer(ktar_xz_fuzzer kcompressiondevice_fuzzer.cc DEVICE Xz)
endif()

if(LibZstd_FOUND)
    add_fuzzer(ktar_zst_fuzzer kcompressiondevice_fuzzer.cc DEVICE Zstd)
endif()
