/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2002 Holger Schroeder <holger-kde@holgis.net>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KZIP_H
#define KZIP_H

#include <karchive.h>

#include "kzipfileentry.h" // for source compat

class KZipFileEntry;
/*!
 * \class KZip
 * \inmodule KArchive
 *
 * \brief A class for reading/writing zip archives.
 *
 * The entrypoint for modifying a zip archive should be \l writeFile or \l writeDir:
 *
 * \code
 * KZip archive(QStringLiteral("some.zip"));
 * if (archive.open(QIODevice::WriteOnly)) {
 *     archive.writeFile("filename", "Data inside new file");
 *     // ...
 * }
 * \endcode
 *
 * If the older method of manipulating archives using
 * prepareWriting, writeData and finishWriting is used,
 * when appending to a file that is already in the archive, the reference to the
 * old file is dropped and the new one is added to the zip - but the
 * old data from the dropped file itself is not deleted, it is still in the
 * zipfile.
 *
 * To have a small and garbage-free zipfile,
 * read the contents of the appended zip file and write it to a new one
 * in QIODevice::WriteOnly mode.
 *
 * This is especially important when you don't want
 * to leak information of how intermediate versions of files in the zip
 * were looking.
 */
class KARCHIVE_EXPORT KZip : public KArchive
{
    Q_DECLARE_TR_FUNCTIONS(KZip)

public:
    /*!
     * Creates an instance that operates on the given archive \a filename.
     */
    explicit KZip(const QString &filename);

    /*!
     * Creates an instance that operates on the given device \a dev.
     *
     * The device may be compressed (KCompressionDevice) or not (QFile, etc.).
     * \warning Do not assume that giving a QFile here will decompress the file,
     * in case it's compressed!
     */
    explicit KZip(QIODevice *dev);

    /*!
     * If the zip file is still opened, then it will be
     * closed automatically by the destructor.
     */
    ~KZip() override;

    /*!
     * Describes the contents of the extra field for a given file in the Zip archive.
     *
     * An extra field stores extra data not defined by existing Zip specifications
     * into the file header and will be skipped if unrecognized.
     *
     * \value NoExtraField No extra field
     * \value ModificationTime Modification time ("extended timestamp" header)
     * \omitvalue DefaultExtraField
     */
    enum ExtraField {
        NoExtraField = 0,
        ModificationTime = 1,
        DefaultExtraField = 1,
    };

    /*!
     * Call this before writeFile or prepareWriting to define what the next
     * file to be written should have in its extra field \a ef.
     * \sa extraField()
     */
    void setExtraField(ExtraField ef);

    /*!
     * Returns the current type of extra field that will be used for new files.
     * \sa setExtraField()
     */
    ExtraField extraField() const;

    /*!
     * Describes the compression type for a given file in the Zip archive.
     *
     * \value NoCompression Uncompressed
     * \value DeflateCompression Deflate compression method
     */
    enum Compression {
        NoCompression = 0,
        DeflateCompression = 1,
    };

    /*!
     * Call this before writeFile or prepareWriting to define whether the next
     * files to be written should be compressed or not
     * and with which compression mode \a c.
     * \sa compression()
     */
    void setCompression(Compression c);

    /*!
     * The current compression mode that will be used for new files.
     * \sa setCompression()
     */
    Compression compression() const;

protected:
    /// Reimplemented from KArchive
    bool doWriteSymLink(const QString &name,
                        const QString &target,
                        const QString &user,
                        const QString &group,
                        mode_t perm,
                        const QDateTime &atime,
                        const QDateTime &mtime,
                        const QDateTime &ctime) override;
    /// Reimplemented from KArchive
    bool doPrepareWriting(const QString &name,
                          const QString &user,
                          const QString &group,
                          qint64 size,
                          mode_t perm,
                          const QDateTime &atime,
                          const QDateTime &mtime,
                          const QDateTime &creationTime) override;

    /*
     * Write data to a file that has been created using prepareWriting().
     * \a size the size of the file
     * Returns true if successful, false otherwise
     */
    bool doFinishWriting(qint64 size) override;

    /*
     * Write data to a file that has been created using prepareWriting().
     * \a data a pointer to the data
     * \a size the size of the chunk
     * Returns true if successful, false otherwise
     */
    bool doWriteData(const char *data, qint64 size) override;

    /*
     * Opens the archive for reading.
     * Parses the directory listing of the archive
     * and creates the KArchiveDirectory/KArchiveFile entries.
     * \a mode the mode of the file
     */
    bool openArchive(QIODevice::OpenMode mode) override;

    /// Closes the archive
    bool closeArchive() override;

    /// Reimplemented from KArchive
    bool doWriteDir(const QString &name,
                    const QString &user,
                    const QString &group,
                    mode_t perm,
                    const QDateTime &atime,
                    const QDateTime &mtime,
                    const QDateTime &ctime) override;

protected:
    void virtual_hook(int id, void *data) override;

private:
    class KZipPrivate;
    KZipPrivate *const d;
};

#endif
