set(HAVE_BZIP2_SUPPORT ${BZIP2_FOUND})
if(BZIP2_FOUND AND BZIP2_NEED_PREFIX)
    set(NEED_BZ2_PREFIX 1)
endif()

set(HAVE_XZ_SUPPORT ${LIBLZMA_FOUND})
set(HAVE_ZSTD_SUPPORT ${LibZstd_FOUND})

configure_file(config-compression.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-compression.h)
set(deprecated_HEADER_NAMES)

add_library(KF5Archive)
add_library(KF5::Archive ALIAS KF5Archive)

ecm_create_qm_loader(KF5Archive karchive5_qt)

if(BZIP2_FOUND)
    target_sources(KF5Archive PRIVATE kbzip2filter.cpp)
    target_link_libraries(KF5Archive PRIVATE BZip2::BZip2)
endif()

if(LIBLZMA_FOUND)
    target_sources(KF5Archive PRIVATE kxzfilter.cpp k7zip.cpp)
    target_link_libraries(KF5Archive PRIVATE LibLZMA::LibLZMA)
endif()

if (LibZstd_FOUND)
    target_sources(KF5Archive PRIVATE kzstdfilter.cpp)
    target_link_libraries(KF5Archive PRIVATE PkgConfig::LibZstd)
endif()


target_sources(KF5Archive PRIVATE karchive.cpp
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

if(NOT EXCLUDE_DEPRECATED_BEFORE_AND_AT STREQUAL "CURRENT" AND
   EXCLUDE_DEPRECATED_BEFORE_AND_AT VERSION_LESS 5.85.0)
    list(APPEND deprecated_HEADER_NAMES KFilterDev)
    target_sources(KF5Archive PRIVATE kfilterdev.cpp)
endif()

ecm_qt_declare_logging_category(KF5Archive
    HEADER loggingcategory.h
    IDENTIFIER KArchiveLog
    CATEGORY_NAME kf.archive
    OLD_CATEGORY_NAMES kf5.karchive
    DEFAULT_SEVERITY Warning
    DESCRIPTION "KArchive"
    EXPORT KARCHIVE
)

ecm_generate_export_header(KF5Archive
    BASE_NAME KArchive
    GROUP_BASE_NAME KF
    VERSION ${KF_VERSION}
    DEPRECATED_BASE_VERSION 0
    DEPRECATION_VERSIONS 5.0 5.85
    EXCLUDE_DEPRECATED_BEFORE_AND_AT ${EXCLUDE_DEPRECATED_BEFORE_AND_AT}
)

target_include_directories(KF5Archive
    INTERFACE "$<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR_KF}/KArchive>"
)

target_link_libraries(KF5Archive
    PUBLIC
        Qt${QT_MAJOR_VERSION}::Core
    PRIVATE
        ZLIB::ZLIB
)

set_target_properties(KF5Archive PROPERTIES
    VERSION     ${KARCHIVE_VERSION}
    SOVERSION   ${KARCHIVE_SOVERSION}
    EXPORT_NAME "Archive"
)

ecm_generate_headers(KArchive_HEADERS
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
    ${deprecated_HEADER_NAMES}

    REQUIRED_HEADERS KArchive_HEADERS
)

install(TARGETS KF5Archive
        EXPORT KF5ArchiveTargets
        ${KF_INSTALL_TARGETS_DEFAULT_ARGS})

if(LIBLZMA_FOUND)
    ecm_generate_headers(KArchive_HEADERS
        HEADER_NAMES
        K7Zip
        REQUIRED_HEADERS KArchive_HEADERS
    )
endif()

install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/karchive_export.h
            ${KArchive_HEADERS}
        DESTINATION ${KDE_INSTALL_INCLUDEDIR_KF}/KArchive
        COMPONENT Devel)

ecm_qt_install_logging_categories(
    EXPORT KARCHIVE
    FILE karchive.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)

if(BUILD_QCH)
    ecm_add_qch(
        KF5Archive_QCH
        NAME KArchive
        BASE_NAME KF5Archive
        VERSION ${KF_VERSION}
        ORG_DOMAIN org.kde
        SOURCES # using only public headers, to cover only public API
            ${KArchive_HEADERS}
        MD_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md"
        LINK_QCHS
            Qt5Core_QCH
        INCLUDE_DIRS
            ${CMAKE_CURRENT_BINARY_DIR}
        BLANK_MACROS
            KARCHIVE_EXPORT
            KARCHIVE_DEPRECATED
            "KARCHIVE_DEPRECATED_VERSION(x, y, t)"
        TAGFILE_INSTALL_DESTINATION ${KDE_INSTALL_QTQCHDIR}
        QCH_INSTALL_DESTINATION ${KDE_INSTALL_QTQCHDIR}
        COMPONENT Devel
    )
endif()

include(ECMGeneratePriFile)
ecm_generate_pri_file(BASE_NAME KArchive LIB_NAME KF5Archive DEPS "core" FILENAME_VAR PRI_FILENAME INCLUDE_INSTALL_DIR ${KDE_INSTALL_INCLUDEDIR_KF}/KArchive)
install(FILES ${PRI_FILENAME}
        DESTINATION ${ECM_MKSPECS_INSTALL_DIR})

