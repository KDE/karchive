/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000-2005 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.h by Roberto Teixeira <maragato@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVEENTRY_H
#define KARCHIVEENTRY_H

#include <sys/stat.h>
#include <sys/types.h>

#include <karchive_export.h>

#include <QDateTime>
#include <QString>

#ifdef Q_OS_WIN
#include <qplatformdefs.h> // mode_t
#endif

class KArchiveDirectory;
class KArchiveFile;
class KArchive;

class KArchiveEntryPrivate;
/*!
 * \class KArchiveEntry
 * \inmodule KArchive
 *
 * \brief Base class for the archive-file's directory structure.
 *
 * An entry may be a file or directory.
 * In practice, this allows to specify behavior in case the kind of entry is unknown,
 * and static_cast to the children classes:
 *
 * \code
 * if (entry->isFile()) {
 *     const KArchiveFile *file = static_cast<const KArchiveFile *>(entry);
 *     // ...
 * }
 * \endcode
 *
 * \sa KArchiveFile
 * \sa KArchiveDirectory
 */
class KARCHIVE_EXPORT KArchiveEntry
{
public:
    /*!
     * Creates a new entry.
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
     */
    KArchiveEntry(KArchive *archive, const QString &name, int access, const QDateTime &date, const QString &user, const QString &group, const QString &symlink);

    virtual ~KArchiveEntry();

    /*!
     * Returns the creation date of the file.
     */
    QDateTime date() const;

    /*!
     * Returns the file name without path.
     */
    QString name() const;
    /*!
     * Returns the permissions and mode flags as returned by the stat() function
     * in st_mode.
     */
    mode_t permissions() const;
    /*!
     * Returns the user who created the file.
     */
    QString user() const;
    /*!
     * Returns the group of the user who created the file.
     */
    QString group() const;

    /*!
     * Returns the symlink if there is one.
     */
    QString symLinkTarget() const;

    /*!
     * Returns whether the entry is a file.
     */
    virtual bool isFile() const;

    /*!
     * Returns whether the entry is a directory.
     */
    virtual bool isDirectory() const;

protected:
    KArchive *archive() const;

protected:
    virtual void virtual_hook(int id, void *data);

private:
    KArchiveEntryPrivate *const d;
};

#endif
