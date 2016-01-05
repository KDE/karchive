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

#include <config-compression.h>

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QTest>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVector>

QTEST_MAIN(KCompressionDeviceTest)

static QString archiveFileName(const QString &extension)
{
    return QFINDTESTDATA(QString("kcompressiondevice_test.%1").arg(extension));
}

QNetworkReply *KCompressionDeviceTest::getArchive(const QString &extension)
{
    const QString kcompressionTest = archiveFileName(extension);
    QNetworkReply *r = qnam.get(QNetworkRequest(QUrl::fromLocalFile(kcompressionTest)));

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
    case KCompressionDevice::None:
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
    QVERIFY(!data.isEmpty());
    const int expectedSize = QFileInfo(archiveFileName(formatExtension(type))).size();
    QVERIFY(expectedSize > 0);
    QCOMPARE(data.size(), expectedSize);
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
            << QLatin1String("examples/bzip2gzip/CMakeLists.txt")
            << QLatin1String("examples/bzip2gzip/main.cpp")
            << QLatin1String("examples/helloworld/CMakeLists.txt")
            << QLatin1String("examples/helloworld/helloworld.pro")
            << QLatin1String("examples/helloworld/main.cpp")
            << QLatin1String("examples/tarlocalfiles/CMakeLists.txt")
            << QLatin1String("examples/tarlocalfiles/main.cpp")
            << QLatin1String("examples/unzipper/CMakeLists.txt")
            << QLatin1String("examples/unzipper/main.cpp");

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
#if HAVE_BZIP2_SUPPORT
    testBufferedDevice(KCompressionDevice::BZip2);
#else
    QSKIP("This test needs bzip2 support");
#endif
}

void KCompressionDeviceTest::testXzBufferedDevice()
{
#if HAVE_XZ_SUPPORT
    testBufferedDevice(KCompressionDevice::Xz);
#else
    QSKIP("This test needs xz support");
#endif
}
