/*
 * SPDX-FileCopyrightText: 2026 Kai Uwe Broulik <kde@broulik.de>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KAESIODEVICE_P_H
#define KAESIODEVICE_P_H

#include <QIODevice>

#include <array>

#include <openssl/types.h>

/*!
 * A readonly device that reads from an underlying device
 * and decrypts AES on the fly.
 *
 * \internal - used by KArchive
 */
class KZipAesIODevice : public QIODevice
{
    Q_OBJECT

public:
    KZipAesIODevice(std::unique_ptr<QIODevice> device, const QByteArray &aesKey);

    ~KZipAesIODevice() override;

    [[nodiscard]] bool open(QIODevice::OpenMode mode) override;
    void close() override;

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override;

    bool isSequential() const override;
    qint64 pos() const override;
    qint64 size() const override;
    qint64 bytesAvailable() const override;

private:
    QIODevice *m_device;
    QByteArray m_aesKey;

    EVP_CIPHER_CTX *m_ctx = nullptr;
    std::array<uchar, 16> m_counter;
};

#endif // KAESIODEVICE_P_H
