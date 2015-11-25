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

#include "kcompressiondevicetest.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QTest>
#include <QNetworkReply>
#include <QNetworkRequest>

QTEST_MAIN(KCompressionDeviceTest)

QNetworkReply *KCompressionDeviceTest::getArchive(const QString &extension)
{
    const QString kcompressionTest = QString("kcompressiondevice_test.%1").arg(extension);
    QNetworkReply *r = qnam.get(
                QNetworkRequest(
                    QUrl::fromLocalFile(
                        QFINDTESTDATA(kcompressionTest))));

    QEventLoop l;
    connect(&qnam, &QNetworkAccessManager::finished, &l, &QEventLoop::quit);
    l.exec();

    return r;
}

QString KCompressionDeviceTest::formatExtension(KCompressionDevice::CompressionType type) const
{
    switch (type) {
    case KCompressionDevice::GZip:
        return "tar.gz";
    case KCompressionDevice::BZip2:
        return "tar.bz2";
    case KCompressionDevice::Xz:
        return "tar.xz";
    default:
        return QString();
    }
}

void KCompressionDeviceTest::setDeviceToArchive(
        QIODevice *d,
        KCompressionDevice::CompressionType type)
{
    KCompressionDevice *devRawPtr = new KCompressionDevice(d, true, type);
    archive.reset(new KTar(devRawPtr));
    device.reset(devRawPtr);
}

void KCompressionDeviceTest::testBufferedDevice(KCompressionDevice::CompressionType type)
{
    QNetworkReply *r = getArchive(formatExtension(type));
    const QByteArray data = r->readAll();
    QBuffer *b = new QBuffer;
    b->setData(data);

    setDeviceToArchive(b, type);
    testExtraction();
}

void KCompressionDeviceTest::testExtraction()
{
    QTemporaryDir temp;
    QDir::setCurrent(temp.path());

    QVERIFY(archive->open(QIODevice::ReadOnly));
    QVERIFY(archive->directory()->copyTo("."));
    QVERIFY(QDir("examples").exists());
    QVERIFY(QDir("examples/bzip2gzip").exists());
    QVERIFY(QDir("examples/helloworld").exists());
    QVERIFY(QDir("examples/tarlocalfiles").exists());
    QVERIFY(QDir("examples/unzipper").exists());

    QVector<QString> fileList;
    fileList
            << "examples/bzip2gzip/CMakeLists.txt"
            << "examples/bzip2gzip/main.cpp"
            << "examples/helloworld/CMakeLists.txt"
            << "examples/helloworld/helloworld.pro"
            << "examples/helloworld/main.cpp"
            << "examples/tarlocalfiles/CMakeLists.txt"
            << "examples/tarlocalfiles/main.cpp"
            << "examples/unzipper/CMakeLists.txt"
            << "examples/unzipper/main.cpp";

    foreach (const QString s, fileList) {
        QFileInfo extractedFile(s);
        QFileInfo sourceFile(QFINDTESTDATA("../" + s));

        QVERIFY(extractedFile.exists());
        QCOMPARE(extractedFile.size(), sourceFile.size());
    }
}

void KCompressionDeviceTest::regularKTarUsage()
{
    archive.reset(new KTar(QFINDTESTDATA("kcompressiondevice_test.tar.gz")));
    device.reset();

    testExtraction();
}

void KCompressionDeviceTest::testGZipBufferedDevice()
{
    testBufferedDevice(KCompressionDevice::GZip);
}

void KCompressionDeviceTest::testBZip2BufferedDevice()
{
    testBufferedDevice(KCompressionDevice::BZip2);
}

void KCompressionDeviceTest::testXzBufferedDevice()
{
    testBufferedDevice(KCompressionDevice::Xz);
}
