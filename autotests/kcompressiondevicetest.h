/* This file is part of the KDE project
   Copyright (C) 2015 Luiz Romário Santana Rios <luizromario@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef KCOMPRESSIONDEVICETEST_H
#define KCOMPRESSIONDEVICETEST_H

#include <QObject>

#include <QNetworkAccessManager>
#include <QScopedPointer>

#include <KTar>
#include <KCompressionDevice>

class QNetworkReply;

class KCompressionDeviceTest : public QObject
{
    Q_OBJECT

private:
    QNetworkReply *getArchive(const QString &extension);
    QString formatExtension(KCompressionDevice::CompressionType type) const;

    void setDeviceToArchive(
            QIODevice *d,
            KCompressionDevice::CompressionType type);

    void testBufferedDevice(KCompressionDevice::CompressionType type);
    void testExtraction();

    QNetworkAccessManager qnam;
    QScopedPointer<KCompressionDevice> device;
    QScopedPointer<KTar> archive;

private Q_SLOTS:
    void regularKTarUsage();
    void testGZipBufferedDevice();
    void testBZip2BufferedDevice();
    void testXzBufferedDevice();

    void testWriteErrorOnOpen();
    void testWriteErrorOnClose();

    void testSeekReadUncompressedBuffer_data();
    void testSeekReadUncompressedBuffer();
};

#endif
