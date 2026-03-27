/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2011 Mario Bensi <mbensi@ipsquad.net>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef __kcompressiondevice_h
#define __kcompressiondevice_h

#include <karchive_export.h>

#include <QFileDevice>
#include <QIODevice>
#include <QMetaType>
#include <QString>

class KCompressionDevicePrivate;

class KFilterBase;

/*!
 * \class KCompressionDevice
 * \inmodule KArchive
 * \brief Reads and writes compressed data.
 *
 * A class for reading and writing compressed data onto a device
 * (e.g. file, but other usages are possible, like a buffer or a socket).
 *
 * Use this class to read/write compressed files.
 *
 * \code
 * KCompressionDevice input("text.tar.zst");
 * if (!input.open(QIODevice::ReadOnly))
 *     return 1;
 * QByteArray data = input.readAll();
 *
 * KTar tar("text.tar.gzip");
 * if(!tar.open(QIODevice::WriteOnly))
 *     return 1;
 * tar.writeData(data);
 * input.close();
 * tar.close();
 * \endcode
 */

class KARCHIVE_EXPORT KCompressionDevice : public QIODevice // KF7 TODO: consider inheriting from QFileDevice, so apps can use error() generically ?
{
    Q_OBJECT
public:
    /*!
     * \value GZip
     * \value BZip2
     * \value Xz
     * \value None
     * \value[since 5.82] Zstd
     * \value[since 6.15] Lz
     */
    enum CompressionType {
        GZip,
        BZip2,
        Xz,
        None,
        Zstd,
        Lz,
    };

    /*!
     * Constructs a KCompressionDevice from an \a inputDevice for a given CompressionType \a type.
     *
     * If \a autoDeleteInputDevice if \c true, \a inputDevice will be deleted automatically.
     */
    KCompressionDevice(QIODevice *inputDevice, bool autoDeleteInputDevice, CompressionType type);

    /*!
     * Constructs a KCompressionDevice from a \a fileName for a given CompressionType \a type.
     */
    KCompressionDevice(const QString &fileName, CompressionType type);

    /*!
     * Constructs a KCompressionDevice from a \a fileName.
     * \since 5.85
     */
    explicit KCompressionDevice(const QString &fileName);

    /*!
     * Constructs a KCompressionDevice from an \a inputDevice for a given CompressionType \a type
     * with an optional \a inputDevice \a size.
     * \since 6.16
     */
    KCompressionDevice(std::unique_ptr<QIODevice> inputDevice, CompressionType type, std::optional<qint64> size = {});

    /*!
     * Destructs the KCompressionDevice.
     *
     * Calls close() if the filter device is still open.
     */
    ~KCompressionDevice() override;

    /*!
     * The compression actually used by this device.
     *
     * If the support for the compression requested in the constructor
     * is not available, then the device will use \l None.
     */
    CompressionType compressionType() const;

    [[nodiscard]] bool open(QIODevice::OpenMode mode) override;

    void close() override;

    qint64 size() const override;

    /*!
     * Set the name of the original \a fileName to be used in the gzip header.
     *
     * Only applicable to gzip-compressed files.
     */
    void setOrigFileName(const QByteArray &fileName);

    /*!
     * Lets this device skip the gzip headers when reading/writing.
     *
     * This way KCompressionDevice (with gzip filter) can be used as a direct wrapper
     * around zlib (used by KZip).
     */
    void setSkipHeaders();

    /*!
     * \reimp
     * That one can be quite slow, when going back. Use with care.
     */
    bool seek(qint64) override;

    bool atEnd() const override;

    /*!
     * Creates the appropriate filter for the CompressionType \a type or returns 0 if not found.
     * \internal
     */
    static KFilterBase *filterForCompressionType(CompressionType type);

    /*!
     * Returns the compression type for the given MIME type, if possible. Otherwise returns \l None.
     *
     * This handles simple cases like application/gzip, but also application/x-compressed-tar, and inheritance.
     * \since 5.85
     */
    static CompressionType compressionTypeForMimeType(const QString &mimetype);

    /*!
     * Returns the error code from the last failing operation.
     * This is especially useful after calling close(), which unfortunately returns void
     * (see https://bugreports.qt.io/browse/QTBUG-70033), to see if the flushing done by close
     * was able to write all the data to disk.
     */
    QFileDevice::FileError error() const;

protected:
    friend class K7Zip;

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

    KFilterBase *filterBase();

private:
    friend KCompressionDevicePrivate;
    KCompressionDevicePrivate *const d;
};

Q_DECLARE_METATYPE(KCompressionDevice::CompressionType)

#endif
