/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000-2005 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.h by Roberto Teixeira <maragato@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVEDIRECTORY_H
#define KARCHIVEDIRECTORY_H

#include <sys/stat.h>
#include <sys/types.h>

#include <QDate>
#include <QString>
#include <QStringList>

#include <karchiveentry.h>

class KArchiveDirectoryPrivate;
class KArchiveFile;
/*!
 * \class KArchiveDirectory
 * \inmodule KArchive
 *
 * \brief A directory in an archive.
 *
 * \sa KArchive
 * \sa KArchiveFile
 * \sa KArchiveEntry
 */
class KARCHIVE_EXPORT KArchiveDirectory : public KArchiveEntry
{
public:
    /*!
     * Creates a new directory entry.
     *
     * This is commonly used to retrieve directories
     * from other functions that return a KArchiveDirectory.
     *
     * \code
     * const KArchiveDirectory *dir = archive.directory();
     * \endcode
     *
     * Parameters:
     * \list
     * \li \a archive: the entry's archive
     * \li \a name: the name of the entry
     * \li \a access: the permissions in unix format
     * \li \a date: the date (in seconds since 1970)
     * \li \a user: the user that owns the entry
     * \li \a group: the group that owns the entry
     * \li \a symlink: the symlink, or QString()
     * \endlist
     * \sa KArchive::directory
     * \sa KArchive::rootDir
     * \sa KArchive::findOrCreate
     */
    KArchiveDirectory(KArchive *archive,
                      const QString &name,
                      int access,
                      const QDateTime &date,
                      const QString &user,
                      const QString &group,
                      const QString &symlink);

    ~KArchiveDirectory() override;

    /*!
     * Returns a list of sub-entry filenames in this directory.
     *
     * The list is not recursive or sorted, it's even in random order (due to using a hashtable).
     * Use sort() on the result to sort the list by filename.
     * \code
     * QStringList subEntries = dir->entries();
     * subEntries.sort();
     * \endcode
     */
    QStringList entries() const;

    /*!
     * Returns the entry in the archive with the given \a name
     * or nullptr if the entry doesn't exist.
     *
     * The entry could be a file or a directory, use isFile() to find out which one it is.
     *
     * The \a name may be "test1", "mydir/test3", "mydir/mysubdir/test3", etc.
     */
    const KArchiveEntry *entry(const QString &name) const;

    /*!
     * Returns the file entry in the archive with the given \a name
     * or nullptr if the entry doesn't exist.
     *
     * This is a convenience method for entry(), when we know the entry is expected to be a file.
     *
     * The \a name may be "test1", "mydir/test3", "mydir/mysubdir/test3", etc.
     *
     * \code
     * if (archive.open(QIODevice::ReadOnly)) {
     *     const KArchiveDirectory *dir = archive.directory();
     *     const KArchiveFile *file = dir->file("somedir/subdir/somefile");
     *     // ...
     * }
     * \endcode
     * \since 5.3
     */
    const KArchiveFile *file(const QString &name) const;

#if KARCHIVE_ENABLE_DEPRECATED_SINCE(6, 13)
    /*!
     * \internal
     * Adds a new entry to the directory.
     *
     * Note: this can delete the entry if another one with the same name is already present
     *
     * \deprecated[6.13]
     *
     * Use addEntryV2() instead.
     */
    KARCHIVE_DEPRECATED_VERSION(6, 13, "Use addEntryV2() instead.")
    void addEntry(KArchiveEntry *); // KF7 TODO: remove
#endif

    /*!
     * \internal
     * Adds a new entry to the directory.
     *
     * Returns whether the entry was added or not. Non added entries are deleted
     * \since 6.13
     *
     * Returns whether the entry was added or not. Non added entries are deleted
     */
    [[nodiscard]] bool addEntryV2(KArchiveEntry *); // KF7 TODO: rename to addEntry

#if KARCHIVE_ENABLE_DEPRECATED_SINCE(6, 13)
    /*!
     * \internal
     *
     * Removes an entry from the directory.
     *
     * \deprecated[6.13]
     * Use removeEntryV2() instead.
     */
    KARCHIVE_DEPRECATED_VERSION(6, 13, "Use removeEntryV2() instead.")
    void removeEntry(KArchiveEntry *); // KF7 TODO: remove
#endif

    /*!
     * Removes an entry from the directory.
     * \since 6.13
     */
    [[nodiscard]] bool removeEntryV2(KArchiveEntry *); // KF7 TODO: rename to removeEntry

    /*
     * Returns true, since this entry is a directory
     */
    bool isDirectory() const override;

    /*!
     * Extracts all entries in this archive directory to a filesystem directory \a dest.
     *
     * If \a recursive is set to true, subdirectories are extracted as well.
     *
     * Returns false if the directory (dest + '/' + name()) couldn't be created.
     */
    bool copyTo(const QString &dest, bool recursive = true) const;

protected:
    void virtual_hook(int id, void *data) override;

private:
    friend class KArchiveDirectoryPrivate;
    KArchiveDirectoryPrivate *const d;
};

#endif
