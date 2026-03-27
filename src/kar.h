/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2002 Laurence Anderson <l.d.anderson@warwick.ac.uk>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KAR_H
#define KAR_H

#include <karchive.h>

/*!
 * \class KAr
 * \module KArchive
 *
 * KAr is a class for reading archives in ar format. Writing
 * is not supported. Reading archives that contain files bigger than
 * INT_MAX - 32 bytes is not supported.
 *
 * \brief A class for reading ar archives.
 */
class KARCHIVE_EXPORT KAr : public KArchive
{
    Q_DECLARE_TR_FUNCTIONS(KAr)

public:
    /*!
     * Creates an instance that operates on the given \a filename.
     */
    explicit KAr(const QString &filename);

    /*!
     * Creates an instance that operates on the given device \a dev.
     *
     * The device may be compressed (KCompressionDevice) or not (QFile, etc.).
     */
    explicit KAr(QIODevice *dev);

    /*!
     * If the ar file is still opened, then it will be
     * closed automatically by the destructor.
     */
    ~KAr() override;

protected:
    /*
     * Writing is not supported by this class, will always fail.
     */
    bool doPrepareWriting(const QString &name,
                          const QString &user,
                          const QString &group,
                          qint64 size,
                          mode_t perm,
                          const QDateTime &atime,
                          const QDateTime &mtime,
                          const QDateTime &ctime) override;

    /*
     * Writing is not supported by this class, will always fail.
     */
    bool doFinishWriting(qint64 size) override;

    /*
     * Writing is not supported by this class, will always fail.
     */
    bool doWriteDir(const QString &name,
                    const QString &user,
                    const QString &group,
                    mode_t perm,
                    const QDateTime &atime,
                    const QDateTime &mtime,
                    const QDateTime &ctime) override;

    bool doWriteSymLink(const QString &name,
                        const QString &target,
                        const QString &user,
                        const QString &group,
                        mode_t perm,
                        const QDateTime &atime,
                        const QDateTime &mtime,
                        const QDateTime &ctime) override;

    /*
     * Opens the archive for reading.
     * Parses the directory listing of the archive
     * and creates the KArchiveDirectory/KArchiveFile entries.
     */
    bool openArchive(QIODevice::OpenMode mode) override;
    bool closeArchive() override;

protected:
    void virtual_hook(int id, void *data) override;

private:
    class KArPrivate;
    KArPrivate *const d;
};

#endif
