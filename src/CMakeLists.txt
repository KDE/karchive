set(HAVE_BZIP2_SUPPORT ${BZIP2_FOUND})
if(BZIP2_FOUND AND BZIP2_NEED_PREFIX)
    set(NEED_BZ2_PREFIX 1)
endif()

set(HAVE_XZ_SUPPORT ${LIBLZMA_FOUND})
set(HAVE_OPENSSL_SUPPORT ${OpenSSL_FOUND})
set(HAVE_ZSTD_SUPPORT ${LibZstd_FOUND})

configure_file(config-compression.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-compression.h)

add_library(KF6Archive)
add_library(KF6::Archive ALIAS KF6Archive)

set_target_properties(KF6Archive PROPERTIES
    VERSION     ${KARCHIVE_VERSION}
    SOVERSION   ${KARCHIVE_SOVERSION}
    EXPORT_NAME "Archive"
)

ecm_create_qm_loader(KF6Archive karchive6_qt)

if(BZIP2_FOUND)
    target_sources(KF6Archive PRIVATE kbzip2filter.cpp)
    target_link_libraries(KF6Archive PRIVATE BZip2::BZip2)
endif()

if(LIBLZMA_FOUND)
    target_sources(KF6Archive PRIVATE kxzfilter.cpp klzfilter.cpp k7zip.cpp)
    target_link_libraries(KF6Archive PRIVATE LibLZMA::LibLZMA)
endif()

if (OpenSSL_FOUND)
    target_link_libraries(KF6Archive PRIVATE OpenSSL::Crypto)
endif()

if (LibZstd_FOUND)
    target_sources(KF6Archive PRIVATE kzstdfilter.cpp)
    target_link_libraries(KF6Archive PRIVATE PkgConfig::LibZstd)
endif()


target_sources(KF6Archive PRIVATE karchive.cpp
    kar.cpp
    kcompressiondevice.cpp
    kfilterbase.cpp
    kgzipfilter.cpp
    klimitediodevice.cpp
    knonefilter.cpp
    ktar.cpp
    kzip.cpp
    krcc.cpp
)

ecm_qt_declare_logging_category(KF6Archive
    HEADER loggingcategory.h
    IDENTIFIER KArchiveLog
    CATEGORY_NAME kf.archive
    OLD_CATEGORY_NAMES kf5.karchive
    DESCRIPTION "KArchive"
    EXPORT KARCHIVE
)

ecm_generate_export_header(KF6Archive
    BASE_NAME KArchive
    GROUP_BASE_NAME KF
    VERSION ${KF_VERSION}
    USE_VERSION_HEADER
    DEPRECATED_BASE_VERSION 0
    DEPRECATION_VERSIONS 6.13
    EXCLUDE_DEPRECATED_BEFORE_AND_AT ${EXCLUDE_DEPRECATED_BEFORE_AND_AT}
)

target_include_directories(KF6Archive
    INTERFACE "$<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR_KF}/KArchive>"
)

target_link_libraries(KF6Archive
    PUBLIC
        Qt6::Core
    PRIVATE
        ZLIB::ZLIB
)

ecm_generate_headers(KArchive_CamelCase_HEADERS
    HEADER_NAMES
    KArchive
    KArchiveEntry
    KArchiveFile
    KArchiveDirectory
    KAr
    KCompressionDevice
    KFilterBase
    KRcc
    KTar
    KZip
    KZipFileEntry

    REQUIRED_HEADERS KArchive_HEADERS
)

install(TARGETS KF6Archive
        EXPORT KF6ArchiveTargets
        ${KF_INSTALL_TARGETS_DEFAULT_ARGS})

if(LIBLZMA_FOUND)
    ecm_generate_headers(KArchive_CamelCase_HEADERS
        HEADER_NAMES
        K7Zip
        REQUIRED_HEADERS KArchive_HEADERS
    )
endif()

install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/karchive_export.h
            ${KArchive_HEADERS}
            ${KArchive_CamelCase_HEADERS}
        DESTINATION ${KDE_INSTALL_INCLUDEDIR_KF}/KArchive
        COMPONENT Devel)

ecm_qt_install_logging_categories(
    EXPORT KARCHIVE
    FILE karchive.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)

ecm_generate_qdoc(KF6Archive karchive.qdocconf)

ecm_add_installed_library_check(KF6Archive
    PACKAGE_NAME "KF6Archive"
    PACKAGE_VERSION ${KARCHIVE_VERSION}
    PACKAGE_TARGET_NAMESPACE "KF6::"
)

ecm_installed_library_check_include_strings(KF6Archive
    HEADERS ${KArchive_CamelCase_HEADERS}
)

ecm_installed_library_check_cmake_variable(KF6Archive
    NAME "KArchive_HAVE_ZLIB"
    VALUE ${ZLIB_FOUND}
    TYPE BOOL
)
ecm_installed_library_check_cmake_variable(KF6Archive
    NAME "KArchive_HAVE_BZIP2"
    VALUE ${BZIP2_FOUND}
    TYPE BOOL
)
ecm_installed_library_check_cmake_variable(KF6Archive
    NAME "KArchive_HAVE_LZMA"
    VALUE ${LIBLZMA_FOUND}
    TYPE BOOL
)
ecm_installed_library_check_cmake_variable(KF6Archive
    NAME "KArchive_HAVE_OPENSSL"
    VALUE ${OpenSSL_FOUND}
    TYPE BOOL
)
ecm_installed_library_check_cmake_variable(KF6Archive
    NAME "KArchive_HAVE_ZSTD"
    VALUE ${LibZstd_FOUND}
    TYPE BOOL
)

ecm_installed_library_check_version_preprocessor_macros(KF6Archive
    PREFIX KARCHIVE
    VERSION ${KARCHIVE_VERSION}
    SILENT
)
