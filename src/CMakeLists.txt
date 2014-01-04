set(HAVE_BZIP2_SUPPORT ${BZIP2_FOUND})
if(BZIP2_FOUND AND BZIP2_NEED_PREFIX)
    set(NEED_BZ2_PREFIX 1)
endif()

set(HAVE_XZ_SUPPORT ${LIBLZMA_FOUND})

configure_file(config-compression.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-compression.h)
add_definitions(-DQT_NO_CAST_FROM_ASCII)

if(BZIP2_FOUND)
   include_directories(${BZIP2_INCLUDE_DIR})
   set(karchive_OPTIONAL_SRCS ${karchive_OPTIONAL_SRCS} kbzip2filter.cpp )
   set(karchive_OPTIONAL_LIBS ${karchive_OPTIONAL_LIBS} ${BZIP2_LIBRARIES})
endif()

if(LIBLZMA_FOUND)
   include_directories(${LIBLZMA_INCLUDE_DIRS})
   set(karchive_OPTIONAL_SRCS ${karchive_OPTIONAL_SRCS} kxzfilter.cpp k7zip.cpp )
   set(karchive_OPTIONAL_LIBS ${karchive_OPTIONAL_LIBS} ${LIBLZMA_LIBRARIES})
endif()


set(karchive_SRCS
   karchive.cpp
   kar.cpp
   kcompressiondevice.cpp
   kfilterbase.cpp
   kfilterdev.cpp
   kgzipfilter.cpp
   klimitediodevice.cpp
   knonefilter.cpp
   ktar.cpp
   kzip.cpp
)

add_library(KF5Archive ${karchive_SRCS} ${karchive_OPTIONAL_SRCS})
generate_export_header(KF5Archive BASE_NAME KArchive)
add_library(KF5::Archive ALIAS KF5Archive)

target_include_directories(KF5Archive INTERFACE "$<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}/KArchive>")

target_link_libraries(KF5Archive PUBLIC Qt5::Core)
target_link_libraries(KF5Archive PRIVATE ${karchive_OPTIONAL_LIBS} ${ZLIB_LIBRARY} )

set_target_properties(KF5Archive PROPERTIES VERSION   ${KARCHIVE_VERSION_STRING}
                                            SOVERSION ${KARCHIVE_SOVERSION}
                                            EXPORT_NAME "Archive"
)

ecm_generate_headers(
  KArchive
  KArchiveEntry
  KArchiveFile
  KArchiveDirectory
  KAr
  KCompressionDevice
  KFilterBase
  KFilterDev
  KTar
  KZip
  KZipFileEntry

  MODULE_NAME KArchive
  REQUIRED_HEADERS KArchive_HEADERS
)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/KArchive DESTINATION ${INCLUDE_INSTALL_DIR} COMPONENT Devel)

install(TARGETS KF5Archive EXPORT KF5ArchiveTargets ${INSTALL_TARGETS_DEFAULT_ARGS})

if(LIBLZMA_FOUND)
   ecm_generate_headers(
     K7Zip
     MODULE_NAME KArchive
     REQUIRED_HEADERS KArchive_HEADERS
   )
endif()

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/karchive_export.h
  ${KArchive_HEADERS}
  DESTINATION ${INCLUDE_INSTALL_DIR}/KArchive COMPONENT Devel
)


### qmake support - TODO, make a ecm macro for this
set(PRI_TARGET_BASENAME KArchive)
set(PRI_TARGET_LIBNAME KF5Archive)
set(PRI_TARGET_QTDEPS core)
set(PRI_VERSION_STRING "${KARCHIVE_VERSION_STRING}")
set(PRI_VERSION_MAJOR "${KARCHIVE_VERSION_MAJOR}")
set(PRI_VERSION_MINOR "${KARCHIVE_VERSION_MINOR}")
set(PRI_VERSION_PATCH "${KARCHIVE_VERSION_PATCH}")
set(PRI_TARGET_INCLUDES "${CMAKE_INSTALL_PREFIX}/${INCLUDE_INSTALL_DIR}/${PRI_TARGET_BASENAME}")
set(PRI_TARGET_DEFINES "")

file(GENERATE
   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/qt_${PRI_TARGET_BASENAME}.pri
   CONTENT
"QT.@PRI_TARGET_BASENAME@.VERSION = @PRI_VERSION_STRING@
QT.@PRI_TARGET_BASENAME@.MAJOR_VERSION = @PRI_VERSION_MAJOR@
QT.@PRI_TARGET_BASENAME@.MINOR_VERSION = @PRI_VERSION_MINOR@
QT.@PRI_TARGET_BASENAME@.PATCH_VERSION = @PRI_VERSION_PATCH@
QT.@PRI_TARGET_BASENAME@.name = @PRI_TARGET_LIBNAME@
QT.@PRI_TARGET_BASENAME@.defines = @PRI_TARGET_DEFINES@
QT.@PRI_TARGET_BASENAME@.includes = @PRI_TARGET_INCLUDES@
QT.@PRI_TARGET_BASENAME@.private_includes =
QT.@PRI_TARGET_BASENAME@.libs = @CMAKE_INSTALL_PREFIX@/@LIB_INSTALL_DIR@
QT.@PRI_TARGET_BASENAME@.depends = @PRI_TARGET_QTDEPS@"
)

# This default value makes "installing qt and kf5 into the same prefix" work out of the box.
# Packagers who use -DCMAKE_INSTALL_PREFIX=/usr will certainly want to set ECM_MKSPECS_INSTALL_DIR to something like share/qt5/mkspecs/modules
set(ECM_MKSPECS_INSTALL_DIR mkspecs/modules CACHE PATH "The directory where mkspecs will be installed to.")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qt_${PRI_TARGET_BASENAME}.pri DESTINATION ${ECM_MKSPECS_INSTALL_DIR})
#### end of qmake support

