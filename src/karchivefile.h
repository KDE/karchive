/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000-2005 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.h by Roberto Teixeira <maragato@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVEFILE_H
#define KARCHIVEFILE_H

#include <karchiveentry.h>

class KArchiveFilePrivate;
/*!
 * \class KArchiveFile
 * \inmodule KArchive
 *
 * \brief A file in an archive.
 *
 * \sa KArchive
 * \sa KArchiveDirectory
 * \sa KArchiveEntry
 */
class KARCHIVE_EXPORT KArchiveFile : public KArchiveEntry
{
public:
    /*!
     * Creates a new file entry.
     *
     * Parameters:
     * \list
     * \li \a archive: the entries archive
     * \li \a name: the name of the entry
     * \li \a access: the permissions in unix format
     * \li \a date: the date (in seconds since 1970)
     * \li \a user: the user that owns the entry
     * \li \a group: the group that owns the entry
     * \li \a symlink: the symlink, or QString()
     * \li \a pos: the position of the file in the directory
     * \li \a size: the size of the file
     * \endlist
     */
    KArchiveFile(KArchive *archive,
                 const QString &name,
                 int access,
                 const QDateTime &date,
                 const QString &user,
                 const QString &group,
                 const QString &symlink,
                 qint64 pos,
                 qint64 size);

    /*!
     * Destructor. Do not call this, KArchive takes care of it.
     */
    ~KArchiveFile() override;

    /*!
     * Returns the position of the data in the \b uncompressed archive.
     */
    qint64 position() const;
    /*!
     * Returns the size of the data.
     */
    qint64 size() const;
    /*!
     * Sets the size \a s of data, usually after writing the file.
     */
    void setSize(qint64 s);

    /*!
     * Returns the contents of this file.
     *
     * \note The data returned by this call is not cached.
     *
     * \warning This method loads the entire file content into memory at once. For large files or untrusted archives, this could cause excessive memory
     * allocation. Consider reading in chunks using createDevice() instead when dealing with archives from untrusted sources.
     */
    virtual QByteArray data() const;

    /*!
     * This method returns QIODevice (internal class: KLimitedIODevice)
     * on top of the underlying QIODevice. This is obviously for reading only.
     *
     * The returned device auto-opens (in readonly mode), no need to open it.
     *
     * \warning The ownership of the device is being transferred to the caller,
     * who will have to delete it.
     *
     * \code
     * std::unique_ptr<QIODevice> device(file->createDevice());
     * while (!device.get()->atEnd()) {
     *     qInfo() << device->readLine();
     * }
     * \endcode
     */
    virtual QIODevice *createDevice() const;

    /*!
     * Returns whether this entry is a file.
     */
    bool isFile() const override;

    /*!
     * Extracts the file to a filesystem directory \a dest.
     *
     * Requires the filesystem directory to exist before copying to.
     *
     * Returns false if the file (dest + '/' + name()) couldn't be created.
     */
    bool copyTo(const QString &dest) const;

protected:
    void virtual_hook(int id, void *data) override;

private:
    KArchiveFilePrivate *const d;
};

#endif
