/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000-2005 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.h by Roberto Teixeira <maragato@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVE_H
#define KARCHIVE_H

#include <sys/stat.h>
#include <sys/types.h>

#include <QByteArrayView>
#include <QCoreApplication>
#include <QDate>
#include <QHash>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <karchive_export.h>

#ifdef Q_OS_WIN
#include <qplatformdefs.h> // mode_t
#endif

class KArchiveDirectory;
class KArchiveFile;

class KArchivePrivate;
/*!
 * \class KArchive
 * \inmodule KArchive
 *
 * \brief Generic class for reading/writing archives.
 */
class KARCHIVE_EXPORT KArchive
{
    Q_DECLARE_TR_FUNCTIONS(KArchive)

protected:
    /*!
     * Constructs a new KArchive instance from \a fileName.
     *
     * The \a fileName from which the archive will be read from,
     * or into which the archive will be written,
     * depending on the mode given to open().
     */
    explicit KArchive(const QString &fileName);

    /*!
     * Constructs a new KArchive instance from a QIODevice \a dev.
     *
     * This can be a file, but also a data buffer, a compression filter, etc.
     *
     * For a file in writing mode it is better to use the other constructor
     * to benefit from the use of QSaveFile when saving.
     */
    explicit KArchive(QIODevice *dev);

public:
    virtual ~KArchive();

    /*!
     * Opens the archive in reading or writing \a mode.
     *
     * Inherited classes might want to reimplement openArchive instead.
     * \sa close
     */
    [[nodiscard]] virtual bool open(QIODevice::OpenMode mode);

    /*!
     * Closes the archive.
     *
     * Inherited classes might want to reimplement closeArchive instead.
     * \sa open
     */
    virtual bool close();

    /*!
     * Returns a description of the last error.
     * \since 5.29
     */
    QString errorString() const;

    /*!
     * Returns whether the archive is open.
     */
    bool isOpen() const;

    /*!
     * Returns the mode in which the archive was opened.
     * \sa open()
     */
    QIODevice::OpenMode mode() const;

    /*!
     * Returns the underlying device.
     */
    QIODevice *device() const;

    /*!
     * The name of the archive file, as passed to the constructor that takes a
     * fileName, or an empty string if you used the QIODevice constructor.
     */
    QString fileName() const;

    /*!
     * Returns the root directory.
     *
     * If an archive is opened for reading, then the contents
     * of the archive can be accessed via this function.
     * \code
     * const KArchiveDirectory *dir = archive.directory();
     * \endcode
     * \sa KArchiveDirectory
     */
    const KArchiveDirectory *directory() const;

    /*!
     * Writes an existing local file with the given \a fileName
     * into the archive with a new \a destName.
     *
     * This method minimizes memory usage compared to writeFile
     * by not loading the whole file into memory in one go.
     *
     * If \a fileName is a symbolic link, it will be written as is,
     * it will not be resolved.
     */
    bool addLocalFile(const QString &fileName, const QString &destName);

    /*!
     * Writes an existing local directory at the given \a path into the archive,
     * with a new \a destName, including all its contents, recursively.
     *
     * Calls addLocalFile for each file to be added.
     *
     * It will also add a \a path that is a symbolic link to a
     * directory. The symbolic link will be dereferenced and the contents of the
     * directory it is pointing to will be added recursively. However, symbolic links
     * \b under \a path will be stored as is.
     */
    bool addLocalDirectory(const QString &path, const QString &destName);

    /*!
     * If an archive is opened for writing then you can add new directories
     * using this function. KArchive won't write one directory twice.
     *
     * This method also allows some file metadata to be set.
     * However, depending on the archive type not all metadata might be regarded.
     *
     * Parameters:
     * \list
     * \li \a name: the name of the directory
     * \li \a user: the user that owns the directory
     * \li \a group: the group that owns the directory
     * \li \a perm: permissions of the directory
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     */
    bool writeDir(const QString &name,
                  const QString &user = QString(),
                  const QString &group = QString(),
                  mode_t perm = 040755,
                  const QDateTime &atime = QDateTime(),
                  const QDateTime &mtime = QDateTime(),
                  const QDateTime &ctime = QDateTime());

    /*!
     * Writes a symbolic link to the archive if supported.
     * The archive must be opened for writing.
     *
     * Parameters:
     * \list
     * \li \a name: name of symbolic link
     * \li \a target: target of symbolic link
     * \li \a user: the user that owns the directory
     * \li \a group: the group that owns the directory
     * \li \a perm: permissions of the directory
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     */
    bool writeSymLink(const QString &name,
                      const QString &target,
                      const QString &user = QString(),
                      const QString &group = QString(),
                      mode_t perm = 0120755,
                      const QDateTime &atime = QDateTime(),
                      const QDateTime &mtime = QDateTime(),
                      const QDateTime &ctime = QDateTime());

    /*!
     * Writes a new file into the archive.
     *
     * The archive must be opened for writing first.
     *
     * The necessary parent directories are created automatically
     * if needed. For instance, writing "mydir/test1" does not
     * require creating the directory "mydir" first.
     *
     * This method also allows some file metadata to be
     * set. However, depending on the archive type not all metadata might be
     * written out.
     *
     * Parameters:
     * \list
     * \li \a name: the name of the file
     * \li \a data: the data to write
     * \li \a perm: permissions of the file
     * \li \a user: the user that owns the file
     * \li \a group: the group that owns the file
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     * \since 6.0
     */
    bool writeFile(const QString &name,
                   QByteArrayView data,
                   mode_t perm = 0100644,
                   const QString &user = QString(),
                   const QString &group = QString(),
                   const QDateTime &atime = QDateTime(),
                   const QDateTime &mtime = QDateTime(),
                   const QDateTime &ctime = QDateTime());

    /*!
     * Performs the necessary checks for writing a file into an archive.
     *
     * This function should be called once and is supposed to be used together
     * with writeData and finishWriting(totalSize).
     *
     * For tar.gz files, you need to know the \a size beforehand.
     * For zip files, \a size isn't used.
     *
     * This method also allows some file metadata to be
     * set. However, depending on the archive type not all metadata might be
     * regarded.
     *
     * Parameters:
     * \list
     * \li \a name: the name of the file
     * \li \a user: the user that owns the file
     * \li \a group: the group that owns the file
     * \li \a size: the size of the file
     * \li \a perm: permissions of the file
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     */
    bool prepareWriting(const QString &name,
                        const QString &user,
                        const QString &group,
                        qint64 size,
                        mode_t perm = 0100644,
                        const QDateTime &atime = QDateTime(),
                        const QDateTime &mtime = QDateTime(),
                        const QDateTime &ctime = QDateTime());

    /*!
     * Writes \a data with a given chunk \a size into the current file.
     *
     * This function can be called multiple times
     * and is supposed to be used together
     * with prepareWriting and finishWriting(totalSize).
     */
    bool writeData(const char *data, qint64 size);

    /*!
     * \overload writeData(const char *, qint64);
     * \since 6.0
     */
    bool writeData(QByteArrayView data);

    /*!
     * Finishes the process of writing the data using the given file \a size.
     *
     * This function should be called once and is supposed to be used together
     * with prepareWriting and writeData.
     * \sa prepareWriting()
     */
    bool finishWriting(qint64 size);

protected:
    /*!
     * Opens the archive in reading or writing \a mode.
     *
     * Called by open.
     */
    virtual bool openArchive(QIODevice::OpenMode mode) = 0;

    /*!
     * Closes the archive.
     *
     * Called by close.
     */
    virtual bool closeArchive() = 0;

    /*!
     * Sets an error description \a errorStr.
     * \since 5.29
     */
    void setErrorString(const QString &errorStr);

    /*!
     * Retrieves or creates the root directory if it doesn't exist.
     *
     * The default implementation assumes that openArchive() did the parsing,
     * so it creates a dummy rootdir if none was set (write mode, or no '/' in the archive).
     *
     * Reimplement this to provide parsing/listing on demand.
     */
    virtual KArchiveDirectory *rootDir();

    /*!
     * Performs the actual writing for writeDir.
     *
     * Depending on the archive type not all metadata might be used.
     *
     * Parameters:
     * \list
     * \li \a name: the name of the directory
     * \li \a user: the user that owns the directory
     * \li \a group: the group that owns the directory
     * \li \a perm: permissions of the directory. Use 040755 if you don't have any other information.
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     * \sa writeDir
     */
    virtual bool doWriteDir(const QString &name,
                            const QString &user,
                            const QString &group,
                            mode_t perm,
                            const QDateTime &atime,
                            const QDateTime &mtime,
                            const QDateTime &ctime) = 0;

    /*!
     * This performs the actual writing for writeSymLink.
     *
     * \list
     * \li \a name: name of symbolic link
     * \li \a target: target of symbolic link
     * \li \a user: the user that owns the directory
     * \li \a group: the group that owns the directory
     * \li \a perm: permissions of the directory
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     * \sa writeSymLink
     */
    virtual bool doWriteSymLink(const QString &name,
                                const QString &target,
                                const QString &user,
                                const QString &group,
                                mode_t perm,
                                const QDateTime &atime,
                                const QDateTime &mtime,
                                const QDateTime &ctime) = 0;

    /*!
     * This performs the actual preparations for doPrepareWriting.
     *
     * Depending on the archive type not all metadata might be used.
     *
     * Parameters:
     * \list
     * \li \a name: the name of the file
     * \li \a user: the user that owns the file
     * \li \a group: the group that owns the file
     * \li \a size: the size of the file
     * \li \a perm: permissions of the file. Use 0100644 if you don't have any more specific permissions to set.
     * \li \a atime: time the file was last accessed
     * \li \a mtime: modification time of the file
     * \li \a ctime: time of last status change
     * \endlist
     * \sa prepareWriting
     */
    virtual bool doPrepareWriting(const QString &name,
                                  const QString &user,
                                  const QString &group,
                                  qint64 size,
                                  mode_t perm,
                                  const QDateTime &atime,
                                  const QDateTime &mtime,
                                  const QDateTime &ctime) = 0;

    /*!
     * This performs the actual writing for writeData.
     * using a pointer to \a data with corresponding chunk \a size.
     * \sa writeData
     * \since 6.0
     */
    virtual bool doWriteData(const char *data, qint64 size);

    /*!
     * This performs the actual writing for finishWriting using the file \a size.
     * \sa finishWriting()
     */
    virtual bool doFinishWriting(qint64 size) = 0;

    /*!
     * Returns \a path if it exists, creates it otherwise.
     *
     * This handles e.g. tar files missing directory entries, like mico-2.3.0.tar.gz.
     */
    KArchiveDirectory *findOrCreate(const QString &path);

    /*!
     * Can be reimplemented in order to change the creation of the device
     * (when using the fileName constructor).
     *
     * By default this method uses QSaveFile when saving,
     * and a simple QFile on reading.
     *
     * This method is called by open().
     */
    virtual bool createDevice(QIODevice::OpenMode mode);

    /*!
     * Can be called by derived classes in order to set the underlying device \a dev.
     *
     * Note that KArchive will \b not own the device, it must be deleted by the derived class.
     */
    void setDevice(QIODevice *dev);

    /*!
     * Derived classes call setRootDir from openArchive,
     * to set the root directory after parsing an existing archive.
     */
    void setRootDir(KArchiveDirectory *rootDir);

protected:
    /*! */
    virtual void virtual_hook(int id, void *data);

private:
    friend class KArchivePrivate;
    KArchivePrivate *const d;
};

// for source compat
#include "karchivedirectory.h"
#include "karchivefile.h"

#endif
