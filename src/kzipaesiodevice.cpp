/*
 * SPDX-FileCopyrightText: 2026 Kai Uwe Broulik <kde@broulik.de>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "kzipaesiodevice_p.h"

#include <QScopeGuard>

#include <openssl/evp.h>

#include "loggingcategory.h"

KZipAesIODevice::KZipAesIODevice(std::unique_ptr<QIODevice> device, const QByteArray &aesKey)
    : m_device(device.release())
    , m_aesKey(aesKey)
{
}

KZipAesIODevice::~KZipAesIODevice()
{
    if (isOpen()) {
        close();
    }
    delete m_device;
}

bool KZipAesIODevice::isSequential() const
{
    return true;
}

bool KZipAesIODevice::open(QIODevice::OpenMode mode)
{
    if (!mode.testFlag(QIODevice::ReadOnly)) {
        qCWarning(KArchiveLog) << "KZipAesIODevice::open only supports QIODevice::ReadOnly!";
        return false;
    }

    if (!m_device->isOpen() && !m_device->open(QIODevice::ReadOnly)) {
        return false;
    }

    if (!m_device->openMode().testFlag(QIODevice::ReadOnly)) {
        return false;
    }

    // No concurrent access!
    m_device->seek(0);

    m_counter.fill(0);
    m_counter[0] = 1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qCWarning(KArchiveLog) << "Failed to create OpenSSL cipher context";
        return false;
    }

    auto cleanup = qScopeGuard([&ctx] {
        EVP_CIPHER_CTX_free(ctx);
    });

    const int keyStrength = m_aesKey.size() * 8;

    const EVP_CIPHER *cipher = nullptr;
    switch (keyStrength) {
    case 128:
        cipher = EVP_aes_128_ecb();
        break;
    case 192:
        cipher = EVP_aes_192_ecb();
        break;
    case 256:
        cipher = EVP_aes_256_ecb();
        break;
    }

    if (!cipher) {
        qCWarning(KArchiveLog) << "Unsupported AES key strength" << keyStrength;
        return false;
    }

    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, reinterpret_cast<const unsigned char *>(m_aesKey.constData()), nullptr) != 1) {
        qCWarning(KArchiveLog) << "EVP_EncryptInit_ex failed";
        return false;
    }

    EVP_CIPHER_CTX_set_padding(ctx, 0);

    cleanup.dismiss();
    m_ctx = ctx;

    setOpenMode(QIODevice::ReadOnly);
    return true;
}

void KZipAesIODevice::close()
{
    if (m_ctx) {
        EVP_CIPHER_CTX_free(m_ctx);
        m_ctx = nullptr;
    }
}

static constexpr qsizetype AesChunkSize = 16;

// Unfortunately, WinZip AES uses a counter layout that is
// incompatible with the counter increment performed by
// OpenSSL's EVP_aes_*_ctr().
// Therefore CTR is implemented manually using AES-ECB.
static void incrementCounter(uchar counter[AesChunkSize])
{
    for (int i = 0; i < AesChunkSize; ++i) {
        if (++counter[i] != 0) {
            break;
        }
    }
}

qint64 KZipAesIODevice::readData(char *data, qint64 maxlen)
{
    const auto readBytes = m_device->read(data, maxlen);

    if (readBytes <= 0) {
        return readBytes;
    }

    qsizetype pos = 0;
    while (pos < readBytes) {
        uchar keystream[AesChunkSize];
        int outLen = 0;
        if (EVP_EncryptUpdate(m_ctx, keystream, &outLen, m_counter.data(), AesChunkSize) != 1) {
            qCWarning(KArchiveLog) << "EVP_EncryptUpdate failed";
            // If the first call failed, communicate an error, otherwise
            // however many we have read until now.
            if (!pos) {
                return -1;
            } else {
                return pos;
            }
        }
        Q_ASSERT(outLen == AesChunkSize);

        incrementCounter(m_counter.data());

        const int bytesToDecrypt = std::min(AesChunkSize, readBytes - pos);
        for (int i = 0; i < bytesToDecrypt; ++i) {
            data[pos + i] ^= keystream[i];
        }

        pos += bytesToDecrypt;
    }

    return pos;
}

qint64 KZipAesIODevice::writeData(const char *data, qint64 maxlen)
{
    // Not supported.
    // In AES encrypt == decrypt, so this would be trivial using the code above
    // but it also needs to write a new salt etc.
    Q_UNUSED(data);
    Q_UNUSED(maxlen);
    return -1;
}

qint64 KZipAesIODevice::pos() const
{
    return m_device->pos();
}

qint64 KZipAesIODevice::size() const
{
    return m_device->size();
}

qint64 KZipAesIODevice::bytesAvailable() const
{
    return m_device->bytesAvailable();
}

#include "moc_kzipaesiodevice_p.cpp"
