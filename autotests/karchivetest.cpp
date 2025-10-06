/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2006, 2010 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2012 Mario Bensi <mbensi@ipsquad.net>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "karchivetest.h"

#include "ossfuzz/karchive_fuzzer_common.h"

#include <k7zip.h>
#include <kar.h>
#include <krcc.h>
#include <ktar.h>
#include <kzip.h>

#include <QBuffer>
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <kcompressiondevice.h>

#ifndef Q_OS_WIN
#include <cerrno>
#include <unistd.h> // symlink
#endif

#ifdef Q_OS_WIN
#include <QScopedValueRollback>
#include <Windows.h>
#else
#include <grp.h>
#include <pwd.h>
#endif

QTEST_MAIN(KArchiveTest)

static const int SIZE1 = 100;

/*!
 * Writes test fileset specified archive
 * \a archive archive
 */
static void writeTestFilesToArchive(KArchive *archive)
{
    QVERIFY(archive->writeFile("empty", "", 0100644, "weis", "users"));
    QVERIFY(archive->writeFile("test1", QByteArray("Hallo"), 0100440, QString("weis"), QString("users")));
    // Now let's try with the prepareWriting/writeData/finishWriting API
    QVERIFY(archive->prepareWriting("test2", "weis", "users", 8));
    QVERIFY(archive->writeData("Hallo ", 6));
    QVERIFY(archive->writeData("Du", 2));
    QVERIFY(archive->finishWriting(8));
    // Add local file
    QFile localFile(QStringLiteral("test3"));
    QVERIFY(localFile.open(QIODevice::WriteOnly));
    QVERIFY(localFile.write("Noch so einer", 13) == 13);
    localFile.close();

    QVERIFY(archive->addLocalFile("test3", "z/test3"));

    // writeFile API
    QVERIFY(archive->writeFile("my/dir/test3", "I do not speak German\nDavid.", 0100644, "dfaure", "hackers"));

    // Now a medium file : 100 null bytes
    char medium[SIZE1] = {0};
    QVERIFY(archive->writeFile("mediumfile", QByteArray(medium, SIZE1)));
    // Another one, with an absolute path
    QVERIFY(archive->writeFile("/dir/subdir/mediumfile2", QByteArray(medium, SIZE1)));

    // Now a huge file : 20000 null bytes
    int n = 20000;
    char *huge = new char[n];
    memset(huge, 0, n);
    QVERIFY(archive->writeFile("hugefile", QByteArray(huge, n)));
    delete[] huge;

    // Now an empty directory
    QVERIFY(archive->writeDir("aaaemptydir"));

#ifndef Q_OS_WIN
    // Add local symlink
    QVERIFY(archive->addLocalFile("test3_symlink", "z/test3_symlink"));
#endif

    // Add executable
    QVERIFY(archive->writeFile("executableAll", "#!/bin/sh\necho hi", 0100755));
}

static QString getCurrentUserName()
{
#if defined(Q_OS_UNIX)
    struct passwd *pw = getpwuid(getuid());
    return pw ? QFile::decodeName(pw->pw_name) : QString::number(getuid());
#elif defined(Q_OS_WIN)
    wchar_t buffer[255];
    DWORD size = 255;
    bool ok = GetUserNameW(buffer, &size);
    if (!ok) {
        return QString();
    }
    return QString::fromWCharArray(buffer);
#else
    return QString();
#endif
}

static QString getCurrentGroupName()
{
#if defined(Q_OS_UNIX)
    struct group *grp = getgrgid(getgid());
    return grp ? QFile::decodeName(grp->gr_name) : QString::number(getgid());
#elif defined(Q_OS_WIN)
    return QString();
#else
    return QString();
#endif
}

enum ListingFlags {
    WithUserGroup = 1,
    WithTime = 0x02,
}; // ListingFlags

static QStringList recursiveListEntries(const KArchiveDirectory *dir, const QString &path, int listingFlags)
{
    QStringList ret;
    QStringList l = dir->entries();
    l.sort();
    for (const QString &it : std::as_const(l)) {
        const KArchiveEntry *entry = dir->entry(it);

        QString descr;
        descr += QStringLiteral("mode=") + QString::number(entry->permissions(), 8) + ' ';
        if (listingFlags & WithUserGroup) {
            descr += QStringLiteral("user=") + entry->user() + ' ';
            descr += QStringLiteral("group=") + entry->group() + ' ';
        }
        descr += QStringLiteral("path=") + path + (it) + ' ';
        descr += QStringLiteral("type=") + (entry->isDirectory() ? "dir" : "file");
        if (entry->isFile()) {
            descr += QStringLiteral(" size=") + QString::number(static_cast<const KArchiveFile *>(entry)->size());
        }
        if (!entry->symLinkTarget().isEmpty()) {
            descr += QStringLiteral(" symlink=") + entry->symLinkTarget();
        }

        if (listingFlags & WithTime) {
            descr += QStringLiteral(" time=") + entry->date().toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"));
        }

        // qDebug() << descr;
        ret.append(descr);

        if (entry->isDirectory()) {
            ret += recursiveListEntries((KArchiveDirectory *)entry, path + it + '/', listingFlags);
        }
    }
    return ret;
}

/*!
 * Verifies contents of specified archive against test fileset
 * \a archive archive
 */
static void testFileData(KArchive *archive)
{
    const KArchiveDirectory *dir = archive->directory();

    const KArchiveFile *f = dir->file(QStringLiteral("z/test3"));
    QVERIFY(f);

    QByteArray arr(f->data());
    QCOMPARE(arr.size(), 13);
    QCOMPARE(arr, QByteArray("Noch so einer"));

    // Now test using createDevice()
    QIODevice *dev = f->createDevice();
    QByteArray contents = dev->readAll();
    QCOMPARE(contents, arr);
    delete dev;

    dev = f->createDevice();
    contents = dev->read(5); // test reading in two chunks
    QCOMPARE(contents.size(), 5);
    contents += dev->read(50);
    QCOMPARE(contents.size(), 13);
    QCOMPARE(QString::fromLatin1(contents.constData()), QString::fromLatin1(arr.constData()));
    delete dev;

    // test read/seek/peek work fine
    f = dir->file(QStringLiteral("test1"));
    dev = f->createDevice();
    contents = dev->peek(4);
    QCOMPARE(contents, QByteArray("Hall"));
    contents = dev->peek(2);
    QCOMPARE(contents, QByteArray("Ha"));
    dev->seek(2);
    contents = dev->peek(2);
    QCOMPARE(contents, QByteArray("ll"));
    dev->seek(0);
    contents = dev->read(2);
    QCOMPARE(contents, QByteArray("Ha"));
    contents = dev->peek(2);
    QCOMPARE(contents, QByteArray("ll"));
    dev->seek(1);
    contents = dev->read(1);
    QCOMPARE(contents, QByteArray("a"));
    dev->seek(4);
    contents = dev->read(1);
    QCOMPARE(contents, QByteArray("o"));
    delete dev;

    const KArchiveEntry *e = dir->entry(QStringLiteral("mediumfile"));
    QVERIFY(e && e->isFile());
    f = (KArchiveFile *)e;
    QCOMPARE(f->data().size(), SIZE1);

    f = dir->file(QStringLiteral("hugefile"));
    QCOMPARE(f->data().size(), 20000);

    e = dir->entry(QStringLiteral("aaaemptydir"));
    QVERIFY(e && e->isDirectory());
    QVERIFY(!dir->file("aaaemptydir"));

    e = dir->entry(QStringLiteral("my/dir/test3"));
    QVERIFY(e && e->isFile());
    f = (KArchiveFile *)e;
    dev = f->createDevice();
    QByteArray firstLine = dev->readLine();
    QCOMPARE(QString::fromLatin1(firstLine.constData()), QString::fromLatin1("I do not speak German\n"));
    QByteArray secondLine = dev->read(100);
    QCOMPARE(QString::fromLatin1(secondLine.constData()), QString::fromLatin1("David."));
    delete dev;
#ifndef Q_OS_WIN
    e = dir->entry(QStringLiteral("z/test3_symlink"));
    QVERIFY(e);
    QVERIFY(e->isFile());
    QCOMPARE(e->symLinkTarget(), QString("test3"));
#endif

    // Test "./" prefix for KOffice (xlink:href="./ObjectReplacements/Object 1")
    e = dir->entry(QStringLiteral("./hugefile"));
    QVERIFY(e && e->isFile());
    e = dir->entry(QStringLiteral("./my/dir/test3"));
    QVERIFY(e && e->isFile());

    // Test directory entries
    e = dir->entry(QStringLiteral("my"));
    QVERIFY(e && e->isDirectory());
    e = dir->entry(QStringLiteral("my/"));
    QVERIFY(e && e->isDirectory());
    e = dir->entry(QStringLiteral("./my/"));
    QVERIFY(e && e->isDirectory());
}

static void testReadWrite(KArchive *archive)
{
    QVERIFY(archive->writeFile("newfile", "New File", 0100440, "dfaure", "users"));
}

#ifdef Q_OS_WIN
extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
#endif

static void testCopyTo(KArchive *archive)
{
    const KArchiveDirectory *dir = archive->directory();
    QTemporaryDir tmpDir;
    const QString dirName = tmpDir.path() + '/';

    QVERIFY(dir->copyTo(dirName));

    QVERIFY(QFile::exists(dirName + "dir"));
    QVERIFY(QFileInfo(dirName + "dir").isDir());

    QFileInfo fileInfo1(dirName + "dir/subdir/mediumfile2");
    QVERIFY(fileInfo1.exists());
    QVERIFY(fileInfo1.isFile());
    QCOMPARE(fileInfo1.size(), Q_INT64_C(100));

    QFileInfo fileInfo2(dirName + "hugefile");
    QVERIFY(fileInfo2.exists());
    QVERIFY(fileInfo2.isFile());
    QCOMPARE(fileInfo2.size(), Q_INT64_C(20000));

    QFileInfo fileInfo3(dirName + "mediumfile");
    QVERIFY(fileInfo3.exists());
    QVERIFY(fileInfo3.isFile());
    QCOMPARE(fileInfo3.size(), Q_INT64_C(100));

    QFileInfo fileInfo4(dirName + "my/dir/test3");
    QVERIFY(fileInfo4.exists());
    QVERIFY(fileInfo4.isFile());
    QCOMPARE(fileInfo4.size(), Q_INT64_C(28));

#ifndef Q_OS_WIN
    const QString fileName = dirName + "z/test3_symlink";
    const QFileInfo fileInfo5(fileName);
    QVERIFY(fileInfo5.exists());
    QVERIFY(fileInfo5.isFile());
    // Do not use fileInfo.symLinkTarget() for unix symlinks
    // It returns the -full- path to the target, while we want the target string "as is".
    QString symLinkTarget;
    const QByteArray encodedFileName = QFile::encodeName(fileName);
    QByteArray s;
#if defined(PATH_MAX)
    s.resize(PATH_MAX + 1);
#else
    int path_max = pathconf(encodedFileName.data(), _PC_PATH_MAX);
    if (path_max <= 0) {
        path_max = 4096;
    }
    s.resize(path_max);
#endif
    int len = readlink(encodedFileName.data(), s.data(), s.size() - 1);
    if (len >= 0) {
        s[len] = '\0';
        symLinkTarget = QFile::decodeName(s.constData());
    }
    QCOMPARE(symLinkTarget, QString("test3"));
#endif

#ifdef Q_OS_WIN
    QScopedValueRollback<int> ntfsMode(qt_ntfs_permission_lookup);
    qt_ntfs_permission_lookup++;
#endif
    QVERIFY(QFileInfo(dirName + "executableAll").permissions() & (QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther));
}

/*!
 * Prepares dataset for archive filter tests
 */
void KArchiveTest::setupData()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("mimeType");

    QTest::newRow(".tar.gz") << "karchivetest.tar.gz"
                             << "application/gzip";
#if HAVE_BZIP2_SUPPORT
    QTest::newRow(".tar.bz2") << "karchivetest.tar.bz2"
                              << "application/x-bzip";
#endif
#if HAVE_XZ_SUPPORT
    QTest::newRow(".tar.lzma") << "karchivetest.tar.lzma"
                               << "application/x-lzma";
    QTest::newRow(".tar.lz") << "karchivetest.tar.lz"
                             << "application/x-lzip";
    QTest::newRow(".tar.xz") << "karchivetest.tar.xz"
                             << "application/x-xz";
#endif
#if HAVE_ZSTD_SUPPORT
    QTest::newRow(".tar.zst") << "karchivetest.tar.zst"
                              << "application/zstd";
#endif
}

/*!
 * \sa QTest::initTestCase()
 */
void KArchiveTest::initTestCase()
{
#ifndef Q_OS_WIN
    // Prepare local symlink
    QFile::remove(QStringLiteral("test3_symlink"));
    if (::symlink("test3", "test3_symlink") != 0) {
        qDebug() << errno;
        QVERIFY(false);
    }
#endif
    // avoid interference from kdebugsettings on qCWarning
    QStandardPaths::setTestModeEnabled(true);
}

void KArchiveTest::testEmptyFilename()
{
    QTest::ignoreMessage(QtWarningMsg, "KArchive: No file name specified");
    KTar tar(QLatin1String(""));
    QVERIFY(!tar.open(QIODevice::ReadOnly));
    QCOMPARE(tar.errorString(), tr("No filename or device was specified"));
}

void KArchiveTest::testNullDevice()
{
    QIODevice *nil = nullptr;
    QTest::ignoreMessage(QtWarningMsg, "KArchive: Null device specified");
    KTar tar(nil);
    QVERIFY(!tar.open(QIODevice::ReadOnly));
    QCOMPARE(tar.errorString(), tr("No filename or device was specified"));
}

void KArchiveTest::testNonExistentFile()
{
    KTar tar(QStringLiteral("nonexistent.tar.gz"));
    QVERIFY(!tar.open(QIODevice::ReadOnly));
    QCOMPARE(tar.errorString(), tr("File %1 does not exist").arg("nonexistent.tar.gz"));
}

void KArchiveTest::testCreateTar_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow(".tar") << "karchivetest.tar";
}

/*!
 * @dataProvider testCreateTar_data
 */
void KArchiveTest::testCreateTar()
{
    QFETCH(QString, fileName);

    // With    tempfile: 0.7-0.8 ms, 994236 instr. loads
    // Without tempfile:    0.81 ms, 992541 instr. loads
    // Note: use ./karchivetest 2>&1 | grep ms
    //       to avoid being slowed down by the kDebugs.
    QBENCHMARK {
        KTar tar(fileName);
        QVERIFY(tar.open(QIODevice::WriteOnly));

        writeTestFilesToArchive(&tar);

        QVERIFY(tar.close());

        QFileInfo fileInfo(QFile::encodeName(fileName));
        QVERIFY(fileInfo.exists());
        // We can't check for an exact size because of the addLocalFile, whose data is system-dependent
        QVERIFY(fileInfo.size() > 450);
    }

    // NOTE The only .tar test, cleanup here
    // QFile::remove(fileName);
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testCreateTarXXX()
{
    QFETCH(QString, fileName);

    // With    tempfile: 1.3-1.7 ms, 2555089 instr. loads
    // Without tempfile:    0.87 ms,  987915 instr. loads
    QBENCHMARK {
        KTar tar(fileName);
        QVERIFY(tar.open(QIODevice::WriteOnly));

        writeTestFilesToArchive(&tar);

        QVERIFY(tar.close());

        QFileInfo fileInfo(QFile::encodeName(fileName));
        QVERIFY(fileInfo.exists());
        // We can't check for an exact size because of the addLocalFile, whose data is system-dependent
        QVERIFY(fileInfo.size() > 350);
    }
}

// static void compareEntryWithTimestamp(const QString &dateString, const QString &expectedDateString, const QDateTime &expectedDateTime)
// Made it a macro so that line numbers are meaningful on failure

#define compareEntryWithTimestamp(dateString, expectedDateString, expectedDateTime)                                                                            \
    {                                                                                                                                                          \
        /* Take the time from the listing and chop it off */                                                                                                   \
        const QDateTime dt = QDateTime::fromString(dateString.right(19), "dd.MM.yyyy hh:mm:ss");                                                               \
        QString _str(dateString);                                                                                                                              \
        _str.chop(25);                                                                                                                                         \
        QCOMPARE(_str, expectedDateString);                                                                                                                    \
                                                                                                                                                               \
        /* Compare the times separately with allowed 2 sec diversion */                                                                                        \
        if (dt.secsTo(expectedDateTime) > 2) {                                                                                                                 \
            qWarning() << dt << "is too different from" << expectedDateTime;                                                                                   \
        }                                                                                                                                                      \
        QVERIFY(dt.secsTo(expectedDateTime) <= 2);                                                                                                             \
    }

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testReadTar() // testCreateTarGz must have been run first.
{
    QFETCH(QString, fileName);

    QFileInfo localFileData(QStringLiteral("test3"));

    const QString systemUserName = getCurrentUserName();
    const QString systemGroupName = getCurrentGroupName();
    const QString owner = localFileData.owner();
    const QString group = localFileData.group();
    const QString emptyTime = QDateTime().toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"));
    const QDateTime creationTime = QFileInfo(fileName).birthTime();

    // 1.6-1.7 ms per interaction, 2908428 instruction loads
    // After the "no tempfile when writing fix" this went down
    // to 0.9-1.0 ms, 1689059 instruction loads.
    // I guess it finds the data in the kernel cache now that no KTempFile is
    // used when writing.
    QBENCHMARK {
        KTar tar(fileName);

        QVERIFY(tar.open(QIODevice::ReadOnly));

        const KArchiveDirectory *dir = tar.directory();
        QVERIFY(dir != nullptr);
        const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup | WithTime);

#ifndef Q_OS_WIN
        const int expectedCount = 16;
#else
        const int expectedCount = 15;
#endif
        if (listing.count() != expectedCount) {
            qWarning() << listing;
        }
        QCOMPARE(listing.count(), expectedCount);
        compareEntryWithTimestamp(listing[0], QString("mode=40755 user= group= path=aaaemptydir type=dir"), creationTime);

        QCOMPARE(listing[1], QString("mode=40777 user=%1 group=%2 path=dir type=dir time=%3").arg(systemUserName).arg(systemGroupName).arg(emptyTime));
        QCOMPARE(listing[2], QString("mode=40777 user=%1 group=%2 path=dir/subdir type=dir time=%3").arg(systemUserName).arg(systemGroupName).arg(emptyTime));
        compareEntryWithTimestamp(listing[3], QString("mode=100644 user= group= path=dir/subdir/mediumfile2 type=file size=100"), creationTime);
        compareEntryWithTimestamp(listing[4], QString("mode=100644 user=weis group=users path=empty type=file size=0"), creationTime);
        compareEntryWithTimestamp(listing[5], QString("mode=100755 user= group= path=executableAll type=file size=17"), creationTime);
        compareEntryWithTimestamp(listing[6], QString("mode=100644 user= group= path=hugefile type=file size=20000"), creationTime);
        compareEntryWithTimestamp(listing[7], QString("mode=100644 user= group= path=mediumfile type=file size=100"), creationTime);
        QCOMPARE(listing[8], QString("mode=40777 user=%1 group=%2 path=my type=dir time=").arg(systemUserName).arg(systemGroupName));
        QCOMPARE(listing[9], QString("mode=40777 user=%1 group=%2 path=my/dir type=dir time=").arg(systemUserName).arg(systemGroupName));
        compareEntryWithTimestamp(listing[10], QString("mode=100644 user=dfaure group=hackers path=my/dir/test3 type=file size=28"), creationTime);
        compareEntryWithTimestamp(listing[11], QString("mode=100440 user=weis group=users path=test1 type=file size=5"), creationTime);
        compareEntryWithTimestamp(listing[12], QString("mode=100644 user=weis group=users path=test2 type=file size=8"), creationTime);
        QCOMPARE(listing[13], QString("mode=40777 user=%1 group=%2 path=z type=dir time=").arg(systemUserName).arg(systemGroupName));

        // This one was added with addLocalFile, so ignore mode.
        QString str = listing[14];
        str.replace(QRegularExpression(QStringLiteral("mode.*user=")), QStringLiteral("user="));

        compareEntryWithTimestamp(str, QString("user=%1 group=%2 path=z/test3 type=file size=13").arg(owner).arg(group), creationTime);

#ifndef Q_OS_WIN
        str = listing[15];
        str.replace(QRegularExpression(QStringLiteral("mode.*path=")), QStringLiteral("path="));

        compareEntryWithTimestamp(str, QString("path=z/test3_symlink type=file size=0 symlink=test3"), creationTime);
#endif

        QVERIFY(tar.close());
    }
}

/*!
 * This tests the decompression using kfilterdev, basically.
 * To debug KTarPrivate::fillTempFile().
 *
 * @dataProvider setupData
 */
void KArchiveTest::testUncompress()
{
    QFETCH(QString, fileName);
    QFETCH(QString, mimeType);

    // testCreateTar must have been run first.
    QVERIFY(QFile::exists(fileName));
    KCompressionDevice filterDev(fileName);
    QByteArray buffer;
    buffer.resize(8 * 1024);
    // qDebug() << "buffer.size()=" << buffer.size();
    QVERIFY(filterDev.open(QIODevice::ReadOnly));

    qint64 totalSize = 0;
    qint64 len = -1;
    while (!filterDev.atEnd() && len != 0) {
        len = filterDev.read(buffer.data(), buffer.size());
        QVERIFY(len >= 0);
        totalSize += len;
        // qDebug() << "read len=" << len << " totalSize=" << totalSize;
    }
    filterDev.close();
    // qDebug() << "totalSize=" << totalSize;
    QVERIFY(totalSize > 26000); // 27648 here when using gunzip
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testTarFileData()
{
    QFETCH(QString, fileName);

    // testCreateTar must have been run first.
    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));

    testFileData(&tar);

    QVERIFY(tar.close());
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testTarCopyTo()
{
    QFETCH(QString, fileName);

    // testCreateTar must have been run first.
    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));

    testCopyTo(&tar);

    QVERIFY(tar.close());
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testTarReadWrite()
{
    QFETCH(QString, fileName);

    // testCreateTar must have been run first.
    {
        KTar tar(fileName);
        QVERIFY(tar.open(QIODevice::ReadWrite));

        testReadWrite(&tar);
        testFileData(&tar);

        QVERIFY(tar.close());
    }

    // Reopen it and check it
    {
        KTar tar(fileName);
        QVERIFY(tar.open(QIODevice::ReadOnly));
        testFileData(&tar);
        const KArchiveDirectory *dir = tar.directory();
        const KArchiveEntry *e = dir->entry(QStringLiteral("newfile"));
        QVERIFY(e && e->isFile());
        const KArchiveFile *f = (KArchiveFile *)e;
        QCOMPARE(f->data().size(), 8);
    }

    // NOTE This is the last test for this dataset. so cleanup here
    QFile::remove(fileName);
}

void KArchiveTest::testTarMaxLength_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow("maxlength.tar.gz") << "karchivetest-maxlength.tar.gz";
}

/*!
 * @dataProvider testTarMaxLength_data
 */
void KArchiveTest::testTarMaxLength()
{
    QFETCH(QString, fileName);

    KTar tar(fileName);

    QVERIFY(tar.open(QIODevice::WriteOnly));

    // Generate long filenames of each possible length bigger than 98...
    // Also exceed 512 byte block size limit to see how well the ././@LongLink
    // implementation fares
    for (int i = 98; i < 514; i++) {
        QString str;
        QString num;
        str.fill('a', i - 10);
        num.setNum(i);
        num = num.rightJustified(10, '0');
        tar.writeFile(str + num, "hum");
    }
    // Result of this test : works perfectly now (failed at 482 formerly and
    // before that at 154).
    QVERIFY(tar.close());

    QVERIFY(tar.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);

    QCOMPARE(listing[0],
             QString("mode=100644 user= group= path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000098 "
                     "type=file size=3"));
    QCOMPARE(listing[3],
             QString("mode=100644 user= group= path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000101 "
                     "type=file size=3"));
    QCOMPARE(listing[4],
             QString("mode=100644 user= group= path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000102 "
                     "type=file size=3"));

    QCOMPARE(listing.count(), 416);

    QVERIFY(tar.close());

    // NOTE Cleanup here
    QFile::remove(fileName);
}

void KArchiveTest::testTarGlobalHeader()
{
    const QString fileName = QFINDTESTDATA("data/global_header_test.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY2(tar.open(QIODevice::ReadOnly), "data/global_header_test.tar.gz");

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);

    QCOMPARE(listing.count(), 2);

    QCOMPARE(listing[0], QString("mode=40775 user=root group=root path=Test type=dir"));
    QCOMPARE(listing[1], QString("mode=664 user=root group=root path=Test/test.txt type=file size=0"));

    QVERIFY(tar.close());
}

void KArchiveTest::testTarPrefix()
{
    const QString fileName = QFINDTESTDATA("data/tar_prefix_test.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY2(tar.open(QIODevice::ReadOnly), "data/tar_prefix_test.tar.gz");

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);

    QCOMPARE(listing[0], QString("mode=40775 user=root group=root path=Test type=dir"));
    QCOMPARE(listing[1], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7 type=dir"));
    QCOMPARE(listing[2], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples type=dir"));
    QCOMPARE(listing[3], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator type=dir"));
    QCOMPARE(listing[4], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original type=dir"));
    QCOMPARE(listing[5], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original/java type=dir"));
    QCOMPARE(listing[6], QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original/java/com type=dir"));
    QCOMPARE(listing[7],
             QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original/java/com/trolltech type=dir"));
    QCOMPARE(
        listing[8],
        QString("mode=40775 user=root group=root path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original/java/com/trolltech/examples type=dir"));
    QCOMPARE(
        listing[9],
        QString("mode=664 user=root group=root "
                "path=Test/qt-jambi-qtjambi-4_7/examples/generator/trolltech_original/java/com/trolltech/examples/GeneratorExample.html type=file size=43086"));

    QCOMPARE(listing.count(), 10);

    QVERIFY(tar.close());
}

void KArchiveTest::testTarDirectoryForgotten()
{
    const QString fileName = QFINDTESTDATA("data/tar_directory_forgotten.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY2(tar.open(QIODevice::ReadOnly), "data/tar_directory_forgotten.tar.gz");

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);

    QVERIFY(listing[9].contains("trolltech/examples/generator"));
    QVERIFY(listing[10].contains("trolltech/examples/generator/GeneratorExample.html"));

    QCOMPARE(listing.count(), 11);

    QVERIFY(tar.close());
}

void KArchiveTest::testTarEmptyFileMissingDir()
{
    const QString fileName = QFINDTESTDATA("data/tar_emptyfile_missingdir.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing[0], QString("mode=40777 path=dir type=dir"));
    QCOMPARE(listing[1], QString("mode=40777 path=dir/foo type=dir"));
    QCOMPARE(listing[2], QString("mode=644 path=dir/foo/file type=file size=0"));
    QCOMPARE(listing.count(), 3);

    QVERIFY(tar.close());
}

void KArchiveTest::testTarRootDir() // bug 309463
{
    const QString fileName = QFINDTESTDATA("data/tar_rootdir.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY2(tar.open(QIODevice::ReadOnly), qPrintable(tar.fileName()));

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);
    // qDebug() << listing.join("\n");

    QVERIFY(listing[0].contains("%{APPNAME}.cpp"));
    QVERIFY(listing[1].contains("%{APPNAME}.h"));
    QVERIFY(listing[5].contains("main.cpp"));

    QCOMPARE(listing.count(), 10);
}

void KArchiveTest::testTarLongNonASCIINames() // bug 266141
{
#ifdef Q_OS_WIN
    QSKIP("tar test file encoding not windows compatible");
#endif

    const QString tarName = QString("karchive-long-non-ascii-names.tar");
    const QString longName = QString::fromUtf8("раз-два-три-четыре-пять-вышел-зайчик-погулять-вдруг-охотник-выбегает-прямо-в-зайчика.txt");

    {
        KTar tar(tarName);
        QVERIFY(tar.open(QIODevice::WriteOnly));
        QVERIFY(tar.writeFile(longName, "", 0644, "user", "users"));
        QVERIFY(tar.close());
    }

    {
        KTar tar(tarName);

        QVERIFY(tar.open(QIODevice::ReadOnly));
        const KArchiveDirectory *dir = tar.directory();
        QVERIFY(dir != nullptr);

        const QStringList listing = recursiveListEntries(dir, QString(""), 0);

        const QString expectedListingEntry = QString("mode=644 path=") + longName + QString(" type=file size=0");

        QCOMPARE(listing.count(), 1);

        QCOMPARE(listing[0], expectedListingEntry);
        QVERIFY(tar.close());
    }
}

void KArchiveTest::testTarShortNonASCIINames() // bug 266141
{
#ifdef Q_OS_WIN
    QSKIP("tar test file encoding not windows compatible");
#endif
    const QString fileName = QFINDTESTDATA("data/tar_non_ascii_file_name.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);

    QVERIFY(tar.open(QIODevice::ReadOnly));
    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QString(""), 0);

    QCOMPARE(listing.count(), 1);
    QCOMPARE(listing[0],
             QString("mode=644 path=абвгдеёжзийклмнопрстуфхцчшщъыьэюя.txt"
                     " type=file size=0"));
    QVERIFY(tar.close());
}

void KArchiveTest::testTarDirectoryTwice() // bug 206994
{
    const QString fileName = QFINDTESTDATA("data/tar_directory_twice.tar.gz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = tar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), WithUserGroup);
    // qDebug() << listing.join("\n");

    QVERIFY(listing[0].contains("path=d"));
    QVERIFY(listing[1].contains("path=d/f1.txt"));
    QVERIFY(listing[2].contains("path=d/f2.txt"));

    QCOMPARE(listing.count(), 3);
}

void KArchiveTest::testTarGzHugeMemoryUsage()
{
    const QString filePath = QFINDTESTDATA("data/ossfuzz_testcase_6581632288227328.tar.gz");
    QVERIFY(!filePath.isEmpty());

    KTar tar(filePath);
    tar.open(QIODevice::ReadOnly);
}

void KArchiveTest::testTarIgnoreRelativePathOutsideArchive()
{
#if HAVE_BZIP2_SUPPORT
    // This test extracts a Tar archive that contains a relative path "../foo" pointing
    // outside of the archive directory. For security reasons extractions should only
    // be allowed within the extracted directory as long as not specifically asked.

    const QString fileName = QFINDTESTDATA("data/tar_relative_path_outside_archive.tar.bz2");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = tar.directory();
    QTemporaryDir tmpDir;
    const QString dirName = tmpDir.path() + "/subdir"; // use a subdir so /tmp/foo doesn't break the test :)
    QDir().mkdir(dirName);

    QVERIFY(dir->copyTo(dirName));
    QVERIFY(!QFile::exists(dirName + "../foo"));
    QVERIFY(QFile::exists(dirName + "/foo"));
#else
    QSKIP("Test data is in bz2 format and karchive is built without bzip2 format");
#endif
}

void KArchiveTest::testTarDeepDirHierarchy()
{
#if HAVE_XZ_SUPPORT
    const QString fileName = QFINDTESTDATA("data/tar_deep_hierarchy.tar.xz");
    QVERIFY(!fileName.isEmpty());

    KTar tar(fileName);
    QVERIFY(tar.open(QIODevice::ReadOnly));
    traverseArchive(tar.directory());
#else
    QSKIP("Test data is in xz format and karchive is built without xz format");
#endif
}

void KArchiveTest::benchmarkTarDeepDirHierarchy()
{
#if HAVE_XZ_SUPPORT
    const QString fileName = QFINDTESTDATA("data/tar_deep_hierarchy.tar.xz");
    QVERIFY(!fileName.isEmpty());

    QBENCHMARK {
        KTar tar(fileName);
        QVERIFY(tar.open(QIODevice::ReadOnly));
    }
#else
    QSKIP("Test data is in xz format and karchive is built without xz format");
#endif
}

///

static const char s_zipFileName[] = "karchivetest.zip";
static const char s_zipMaxLengthFileName[] = "karchivetest-maxlength.zip";
static const char s_zipLocaleFileName[] = "karchivetest-locale.zip";
static const char s_zipMimeType[] = "application/vnd.oasis.opendocument.text";

void KArchiveTest::testCreateZip()
{
    KZip zip(s_zipFileName);

    QVERIFY(zip.open(QIODevice::WriteOnly));

    zip.setExtraField(KZip::NoExtraField);

    zip.setCompression(KZip::NoCompression);
    QByteArray zipMimeType(s_zipMimeType);
    zip.writeFile(QStringLiteral("mimetype"), zipMimeType);
    zip.setCompression(KZip::DeflateCompression);

    writeTestFilesToArchive(&zip);

    QVERIFY(zip.close());

    QFile zipFile(QFile::encodeName(s_zipFileName));
    QFileInfo fileInfo(zipFile);
    QVERIFY(fileInfo.exists());
    QVERIFY(fileInfo.size() > 300);

    // Check that the header with no-compression and no-extrafield worked.
    // (This is for the "magic" for koffice documents)
    QVERIFY(zipFile.open(QIODevice::ReadOnly));
    QByteArray arr = zipFile.read(4);
    QCOMPARE(arr, QByteArray("PK\003\004"));
    QVERIFY(zipFile.seek(30));
    arr = zipFile.read(8);
    QCOMPARE(arr, QByteArray("mimetype"));
    arr = zipFile.read(zipMimeType.size());
    QCOMPARE(arr, zipMimeType);
}

void KArchiveTest::testCreateZipError()
{
    // Giving a directory name to kzip must give an error case in close(), see #136630.
    // Otherwise we just lose data.
    KZip zip(QDir::currentPath());

    QVERIFY(!zip.open(QIODevice::WriteOnly));
    QCOMPARE(zip.errorString(), tr("QSaveFile creation for %1 failed: Filename refers to a directory").arg(QDir::currentPath()));
}

void KArchiveTest::testReadZipError()
{
    QFile brokenZip(QStringLiteral("broken.zip"));
    QVERIFY(brokenZip.open(QIODevice::WriteOnly));

    // incomplete magic
    brokenZip.write(QByteArray("PK\003"));

    brokenZip.close();
    {
        KZip zip(QStringLiteral("broken.zip"));

        QVERIFY(!zip.open(QIODevice::ReadOnly));
        QCOMPARE(zip.errorString(), tr("Invalid ZIP file. Unexpected end of file. (Error code: %1)").arg(1));

        QVERIFY(brokenZip.open(QIODevice::WriteOnly | QIODevice::Append));

        // add rest of magic, but still incomplete header
        brokenZip.write(QByteArray("\004\000\000\000\000"));

        brokenZip.close();

        QVERIFY(!zip.open(QIODevice::ReadOnly));
        QCOMPARE(zip.errorString(), tr("Invalid ZIP file. Unexpected end of file. (Error code: %1)").arg(4));
    }

    QVERIFY(brokenZip.remove());
}

void KArchiveTest::testReadZip()
{
    // testCreateZip must have been run first.
    KZip zip(s_zipFileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = zip.directory();
    QVERIFY(dir != nullptr);

    // ZIP has no support for per-file user/group, so omit them from the listing
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

#ifndef Q_OS_WIN
    QCOMPARE(listing.count(), 17);
#else
    QCOMPARE(listing.count(), 16);
#endif
    QCOMPARE(listing[0], QString("mode=40755 path=aaaemptydir type=dir"));
    QCOMPARE(listing[1], QString("mode=40777 path=dir type=dir"));
    QCOMPARE(listing[2], QString("mode=40777 path=dir/subdir type=dir"));
    QCOMPARE(listing[3], QString("mode=100644 path=dir/subdir/mediumfile2 type=file size=100"));
    QCOMPARE(listing[4], QString("mode=100644 path=empty type=file size=0"));
    QCOMPARE(listing[5], QString("mode=100755 path=executableAll type=file size=17"));
    QCOMPARE(listing[6], QString("mode=100644 path=hugefile type=file size=20000"));
    QCOMPARE(listing[7], QString("mode=100644 path=mediumfile type=file size=100"));
    QCOMPARE(listing[8], QString("mode=100644 path=mimetype type=file size=%1").arg(strlen(s_zipMimeType)));
    QCOMPARE(listing[9], QString("mode=40777 path=my type=dir"));
    QCOMPARE(listing[10], QString("mode=40777 path=my/dir type=dir"));
    QCOMPARE(listing[11], QString("mode=100644 path=my/dir/test3 type=file size=28"));
    QCOMPARE(listing[12], QString("mode=100440 path=test1 type=file size=5"));
    QCOMPARE(listing[13], QString("mode=100644 path=test2 type=file size=8"));
    QCOMPARE(listing[14], QString("mode=40777 path=z type=dir"));
    // This one was added with addLocalFile, so ignore mode
    QString str = listing[15];
    str.replace(QRegularExpression(QStringLiteral("mode.*path=")), QStringLiteral("path="));
    QCOMPARE(str, QString("path=z/test3 type=file size=13"));
#ifndef Q_OS_WIN
    str = listing[16];
    str.replace(QRegularExpression(QStringLiteral("mode.*path=")), QStringLiteral("path="));
    QCOMPARE(str, QString("path=z/test3_symlink type=file size=5 symlink=test3"));
#endif

    QVERIFY(zip.close());
}

void KArchiveTest::testZipFileData()
{
    // testCreateZip must have been run first.
    KZip zip(s_zipFileName);
    QVERIFY(zip.open(QIODevice::ReadOnly));

    testFileData(&zip);

    QVERIFY(zip.close());
}

void KArchiveTest::testZipCopyTo()
{
    // testCreateZip must have been run first.
    KZip zip(s_zipFileName);
    QVERIFY(zip.open(QIODevice::ReadOnly));

    testCopyTo(&zip);

    QVERIFY(zip.close());
}

void KArchiveTest::testZipMaxLength()
{
    KZip zip(s_zipMaxLengthFileName);

    QVERIFY(zip.open(QIODevice::WriteOnly));

    // Similar to testTarMaxLength just to make sure, but of course zip doesn't have
    // those limitations in the first place.
    for (int i = 98; i < 514; i++) {
        QString str;
        QString num;
        str.fill('a', i - 10);
        num.setNum(i);
        num = num.rightJustified(10, '0');
        zip.writeFile(str + num, "hum");
    }
    QVERIFY(zip.close());

    QVERIFY(zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing[0],
             QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000098 type=file size=3"));
    QCOMPARE(
        listing[3],
        QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000101 type=file size=3"));
    QCOMPARE(
        listing[4],
        QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000102 type=file size=3"));

    QCOMPARE(listing.count(), 514 - 98);

    QVERIFY(zip.close());
}

void KArchiveTest::testZipWithNonLatinFileNames()
{
    KZip zip(s_zipLocaleFileName);

    QVERIFY(zip.open(QIODevice::WriteOnly));

    const QByteArray fileData("Test of data with a russian file name");
    const QString fileName = QString::fromUtf8("Архитектура.okular");
    const QString recodedFileName = QFile::decodeName(QFile::encodeName(fileName));
    QVERIFY(zip.writeFile(fileName, fileData));

    QVERIFY(zip.close());

    QVERIFY(zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 1);
    QCOMPARE(listing[0], QString::fromUtf8("mode=100644 path=%1 type=file size=%2").arg(recodedFileName).arg(fileData.size()));

    const KArchiveFile *fileEntry = static_cast<const KArchiveFile *>(dir->entry(dir->entries()[0]));
    QCOMPARE(fileEntry->data(), fileData);
}

void KArchiveTest::testZipWithOverwrittenFileName()
{
    KZip zip(s_zipFileName);

    QVERIFY(zip.open(QIODevice::WriteOnly));

    const QByteArray fileData1("There could be a fire, if there is smoke.");
    const QString fileName = QStringLiteral("wisdom");
    QVERIFY(zip.writeFile(fileName, fileData1, 0100644, "konqi", "dragons"));

    // now overwrite it
    const QByteArray fileData2("If there is smoke, there could be a fire.");
    QVERIFY(zip.writeFile(fileName, fileData2, 0100644, "konqi", "dragons"));

    QVERIFY(zip.close());

    QVERIFY(zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 1);
    QCOMPARE(listing[0], QString::fromUtf8("mode=100644 path=%1 type=file size=%2").arg(fileName).arg(fileData2.size()));

    const KArchiveFile *fileEntry = static_cast<const KArchiveFile *>(dir->entry(dir->entries()[0]));
    QCOMPARE(fileEntry->data(), fileData2);
}

static bool writeFile(const QString &dirName, const QString &fileName, const QByteArray &data)
{
    Q_ASSERT(dirName.endsWith('/'));
    QFile file(dirName + fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(data);
    return true;
}

void KArchiveTest::testZipAddLocalDirectory()
{
    // Prepare local dir
    QTemporaryDir tmpDir;
    const QString dirName = tmpDir.path() + '/';

    const QByteArray file1Data = "Hello Shantanu";
    const QString file1 = QStringLiteral("file1");
    QVERIFY(writeFile(dirName, file1, file1Data));

    const QString emptyDir = QStringLiteral("emptyDir");
    QDir(dirName).mkdir(emptyDir);

    {
        KZip zip(s_zipFileName);

        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.addLocalDirectory(dirName, "."));
        QVERIFY(zip.close());
    }
    {
        KZip zip(s_zipFileName);

        QVERIFY(zip.open(QIODevice::ReadOnly));

        const KArchiveDirectory *dir = zip.directory();
        QVERIFY(dir != nullptr);

        const KArchiveEntry *e = dir->entry(file1);
        QVERIFY(e && e->isFile());
        const KArchiveFile *f = static_cast<const KArchiveFile *>(e);
        QCOMPARE(f->data(), file1Data);

        const KArchiveEntry *empty = dir->entry(emptyDir);
        QVERIFY(empty && empty->isDirectory());
    }
}

void KArchiveTest::testZipReadRedundantDataDescriptor_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow("noSignature") << "data/redundantDataDescriptorsNoSignature.zip";
    QTest::newRow("withSignature") << "data/redundantDataDescriptorsWithSignature.zip";
}

/*!
 * @dataProvider testZipReadRedundantDataDescriptor_data
 */
void KArchiveTest::testZipReadRedundantDataDescriptor()
{
    QFETCH(QString, fileName);

    const QString redundantDataDescriptorZipFileName = QFINDTESTDATA(fileName);
    QVERIFY(!redundantDataDescriptorZipFileName.isEmpty());

    KZip zip(redundantDataDescriptorZipFileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = zip.directory();
    QVERIFY(dir != nullptr);

    const QByteArray fileData("aaaaaaaaaaaaaaa");

    // ZIP has no support for per-file user/group, so omit them from the listing
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 2);
    QCOMPARE(listing[0], QString::fromUtf8("mode=100644 path=compressed type=file size=%2").arg(fileData.size()));
    QCOMPARE(listing[1], QString::fromUtf8("mode=100644 path=uncompressed type=file size=%2").arg(fileData.size()));

    const KArchiveFile *fileEntry = static_cast<const KArchiveFile *>(dir->entry(dir->entries()[0]));
    QCOMPARE(fileEntry->data(), fileData);
    fileEntry = static_cast<const KArchiveFile *>(dir->entry(dir->entries()[1]));
    QCOMPARE(fileEntry->data(), fileData);
}

void KArchiveTest::testZipDirectoryPermissions()
{
    const QString fileName = QFINDTESTDATA("data/dirpermissions.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));
    const KArchiveDirectory *dir = zip.directory();
    const QStringList listing = recursiveListEntries(dir, QString(), 0);
    QCOMPARE(listing.join(' '), QString::fromUtf8("mode=40700 path=700 type=dir mode=40750 path=750 type=dir mode=40755 path=755 type=dir"));
}

void KArchiveTest::testZipWithinZip()
{
    const QString fileName = QFINDTESTDATA("data/zip_within_zip.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));
    const KArchiveDirectory *dir = zip.directory();
    const QStringList listing = recursiveListEntries(dir, QString(), 0);
    QCOMPARE(listing.count(), 2);
    QCOMPARE(listing[0], QString::fromUtf8("mode=100644 path=foo type=file size=7"));
    QCOMPARE(listing[1], QString::fromUtf8("mode=100644 path=test.epub type=file size=525154"));
}

void KArchiveTest::testZipPrependedData()
{
    const QString fileName = QFINDTESTDATA("data/zip64_datadescriptor.zip");
    QVERIFY(!fileName.isEmpty());

    QFile origFile(fileName);
    QVERIFY2(origFile.open(QIODevice::ReadOnly), qPrintable(origFile.errorString()));
    auto zipData = origFile.readAll();
    QVERIFY(zipData.startsWith("PK"));

    zipData.insert(0, QByteArrayLiteral("PrependedPK\x01\x02Garbage"));
    QBuffer zipBuffer(&zipData);

    KZip zip(&zipBuffer);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 1);
    QCOMPARE(zip.directory()->entries(), QList{QStringLiteral("-")});

    auto entry = zip.directory()->file(QStringLiteral("-"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 4);
    QCOMPARE(entry->data(), "abcd");
}

void KArchiveTest::testZipUnusualButValid()
{
    const QString fileName = QFINDTESTDATA("data/unusual_but_valid_364071.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));
    const KArchiveDirectory *dir = zip.directory();
    const QStringList listing = recursiveListEntries(dir, QString(), 0);
    QCOMPARE(listing.join(' '), QLatin1String("mode=40744 path=test type=dir mode=744 path=test/os-release type=file size=199"));
}

void KArchiveTest::testZipDuplicateNames()
{
    const QString fileName = QFINDTESTDATA("data/out.epub");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));

    int metaInfCount = 0;
    for (const QString &entryName : zip.directory()->entries()) {
        if (entryName.startsWith("META-INF")) {
            metaInfCount++;
        }
    }

    QVERIFY2(metaInfCount == 1, "Archive root directory contains duplicates");
}

void KArchiveTest::testZip64()
{
    const QString fileName = QFINDTESTDATA("data/zip64.pkpass");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY(zip.open(QIODevice::ReadOnly));

    // NOTE the actual content of this file has been wiped, we can only verify the file meta data here
    QCOMPARE(zip.directory()->entries().size(), 7);
    auto entry = static_cast<const KZipFileEntry *>(zip.directory()->entry(QStringLiteral("manifest.json")));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 305);
    QCOMPARE(entry->compressedSize(), 194);
    QCOMPARE(entry->position(), 21417);
    entry = static_cast<const KZipFileEntry *>(zip.directory()->entry(QStringLiteral("logo.png")));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 3589);
    QCOMPARE(entry->compressedSize(), 3594);
    QCOMPARE(entry->position(), 8943);
}

void KArchiveTest::testZipReopenWithoutDoubleDeletion()
{
    // testCreateZip must have been run first.
    KZip zip(s_zipFileName);

    QVERIFY(zip.open(QIODevice::ReadOnly));
    QVERIFY(zip.close());

    QVERIFY(zip.open(QIODevice::WriteOnly));
    QVERIFY(zip.close()); // should not crash
}

void KArchiveTest::testZip64NestedStored()
{
    const QString fileName = QFINDTESTDATA("data/dirpermissions.zip");
    QVERIFY(!fileName.isEmpty());
    QFile nested(fileName);
    QVERIFY(nested.open(QIODevice::ReadOnly));
    auto nestedData = nested.readAll();

    QBuffer archive;

    KZip writer(&archive);
    QVERIFY(writer.open(QIODevice::WriteOnly));
    // Write nested zip file
    writer.setCompression(KZip::NoCompression);
    QVERIFY(writer.prepareWriting(QStringLiteral("nested.zip"), {}, {}, nestedData.size()));
    QVERIFY(writer.writeData(nestedData));
    QVERIFY(writer.finishWriting(nestedData.size()));

    QVERIFY(writer.close());

    KZip reader(&archive);
    QVERIFY(reader.open(QIODevice::ReadOnly));

    QCOMPARE(reader.directory()->entries().size(), 1);
    QCOMPARE(reader.directory()->entries(), QList{QStringLiteral("nested.zip")});

    auto entry = reader.directory()->file(QStringLiteral("nested.zip"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 430);
    QCOMPARE(entry->size(), nestedData.size());

    QVERIFY(reader.close());
}

void KArchiveTest::testZip64NestedStoredStreamed()
{
    const QString fileName = QFINDTESTDATA("data/zip64_nested_stored_streamed.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 2);

    auto entry = zip.directory()->file(QStringLiteral("zip64_datadescriptor.zip"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 150);

    entry = zip.directory()->file(QStringLiteral("zip64_end_of_central_directory.zip"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 200);

    QVERIFY(zip.close());
}

void KArchiveTest::testZip64EndOfCentralDirectory()
{
    const QString fileName = QFINDTESTDATA("data/zip64_end_of_central_directory.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 1);
    QCOMPARE(zip.directory()->entries(), QList{QStringLiteral("-")});

    auto entry = zip.directory()->file(QStringLiteral("-"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 4);

    QVERIFY(zip.close());
}

void KArchiveTest::testZip64DataDescriptor()
{
    const QString fileName = QFINDTESTDATA("data/zip64_datadescriptor.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 1);
    QCOMPARE(zip.directory()->entries(), QList{QStringLiteral("-")});

    auto entry = zip.directory()->file(QStringLiteral("-"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 4);
    QCOMPARE(entry->data(), "abcd");

    QVERIFY(zip.close());
}

void KArchiveTest::testZip64ExtraZip64Size()
{
    const QString fileName = QFINDTESTDATA("data/zip64_extra_zip64_size.zip.gz");
    QVERIFY(!fileName.isEmpty());

    KCompressionDevice gzfilter(fileName);
    QVERIFY2(gzfilter.open(QIODevice::ReadOnly), qPrintable(gzfilter.errorString()));

    // Get the actual test data, which is compressed so the repository does
    // not have to store 4 MByte of binary data
    auto zipData = gzfilter.read(5 * 1000 * 1000);
    QVERIFY(zipData.startsWith("PK"));

    QBuffer zipBuffer(&zipData);
    KZip zip(&zipBuffer);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 1);
    QCOMPARE(zip.directory()->entries(), QList{QStringLiteral("-")});

    auto entry = zip.directory()->file(QStringLiteral("-"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 4404019208);

    auto readDev = std::unique_ptr<QIODevice>(entry->createDevice());
    auto head = readDev->read(8);
    QCOMPARE(head, QByteArrayLiteral("abcd\0\0\0\0"));
    readDev->seek(entry->size() - 8);
    auto tail = readDev->read(8);
    QCOMPARE(tail, QByteArrayLiteral("\0\0\0\0ABCD"));

    QVERIFY(zip.close());
}

void KArchiveTest::testZip64ExtraZip64SizeFirst()
{
    const QString fileName = QFINDTESTDATA("data/zip64_extra_zip64_size_first.zip.gz");
    QVERIFY(!fileName.isEmpty());

    KCompressionDevice gzfilter(fileName);
    QVERIFY2(gzfilter.open(QIODevice::ReadOnly), qPrintable(gzfilter.errorString()));

    // Get the actual test data, which is compressed so the repository does
    // not have to store 4 MByte of binary data
    auto zipData = gzfilter.read(5 * 1000 * 1000);
    QVERIFY(zipData.startsWith("PK"));

    QBuffer zipBuffer(&zipData);
    KZip zip(&zipBuffer);
    QEXPECT_FAIL("", "Zip64 extra at front incorrectly parsed", Abort);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 2);
    QCOMPARE(zip.directory()->entries(), (QList{QStringLiteral("4200M.bin"), QStringLiteral("4.bin")}));

    auto entry = zip.directory()->file(QStringLiteral("4200M.bin"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 4404019208);

    auto readDev = std::unique_ptr<QIODevice>(entry->createDevice());
    auto head = readDev->read(8);
    QCOMPARE(head, QByteArrayLiteral("abcd\0\0\0\0"));
    readDev->seek(entry->size() - 8);
    auto tail = readDev->read(8);
    QCOMPARE(tail, QByteArrayLiteral("\0\0\0\0ABCD"));

    QVERIFY(zip.close());
}

void KArchiveTest::testZip64ExtraZip64Offset()
{
    const QString fileName = QFINDTESTDATA("data/zip64_extra_zip64_localheader.oxps");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(zip.errorString()));

    QCOMPARE(zip.directory()->entries().size(), 6);

    auto entry = zip.directory()->file(QStringLiteral("_rels/.rels"));
    QVERIFY(entry);
    QCOMPARE(entry->size(), 576);

    const auto data = entry->data();
    QCOMPARE(data.sliced(0, 6), "<?xml ");

    QVERIFY(zip.close());
}

void KArchiveTest::testZipOssFuzzIssue433303801()
{
    const QString fileName = QFINDTESTDATA("data/ossfuzz_issue_433303801.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY(zip.open(QIODevice::ReadOnly));
    traverseArchive(zip.directory());
}

void KArchiveTest::testZipDeepDirHierarchy()
{
    const QString fileName = QFINDTESTDATA("data/zip_deep_hierarchy.zip");
    QVERIFY(!fileName.isEmpty());

    KZip zip(fileName);
    QVERIFY(zip.open(QIODevice::ReadOnly));
    traverseArchive(zip.directory());
}

void KArchiveTest::benchmarkZipDeepDirHierarchy()
{
    const QString fileName = QFINDTESTDATA("data/zip_deep_hierarchy.zip");
    QVERIFY(!fileName.isEmpty());

    QBENCHMARK {
        KZip zip(fileName);
        QVERIFY(zip.open(QIODevice::ReadOnly));
    }
}

void KArchiveTest::testRcc()
{
    const QString rccFile = QFINDTESTDATA("data/runtime_resource.rcc"); // was copied from qtbase/tests/auto/corelib/io/qresourceengine
    QVERIFY(!rccFile.isEmpty());
    KRcc rcc(rccFile);
    QVERIFY(rcc.open(QIODevice::ReadOnly));
    const KArchiveDirectory *rootDir = rcc.directory();
    QVERIFY(rootDir != nullptr);
    const KArchiveEntry *rrEntry = rootDir->entry(QStringLiteral("runtime_resource"));
    QVERIFY(rrEntry && rrEntry->isDirectory());
    const KArchiveDirectory *rrDir = static_cast<const KArchiveDirectory *>(rrEntry);
    const KArchiveEntry *fileEntry = rrDir->entry(QStringLiteral("search_file.txt"));
    QVERIFY(fileEntry && fileEntry->isFile());
    const KArchiveFile *searchFile = static_cast<const KArchiveFile *>(fileEntry);
    const QByteArray fileData = searchFile->data();
    QCOMPARE(QString::fromLatin1(fileData), QString::fromLatin1("root\n"));
}

void KArchiveTest::testAr()
{
    const QString artestFile = QFINDTESTDATA("data/artest.a");
    KAr ar(artestFile);

    QVERIFY(ar.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = ar.directory();
    QVERIFY(dir != nullptr);

    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    const QStringList expected = {"mode=100644 path=at_quick_exit.oS type=file size=3456",
                                  "mode=100644 path=atexit.oS type=file size=3436",
                                  "mode=100644 path=elf-init.oS type=file size=3288",
                                  "mode=100644 path=fstat.oS type=file size=3296",
                                  "mode=100644 path=fstat64.oS type=file size=3304",
                                  "mode=100644 path=fstatat.oS type=file size=3352",
                                  "mode=100644 path=fstatat64.oS type=file size=3380",
                                  "mode=100644 path=lstat.oS type=file size=3320",
                                  "mode=100644 path=lstat64.oS type=file size=3332",
                                  "mode=100644 path=mknod.oS type=file size=2840",
                                  "mode=100644 path=mknodat.oS type=file size=2848",
                                  "mode=100644 path=stack_chk_fail_local.oS type=file size=2288",
                                  "mode=100644 path=stat.oS type=file size=3316",
                                  "mode=100644 path=stat64.oS type=file size=3312",
                                  "mode=100644 path=warning-nop.oS type=file size=1976"};

    QCOMPARE(listing.count(), expected.count());
    QCOMPARE(listing, expected);

    QVERIFY(ar.close());
}

/*!
 * \sa QTest::cleanupTestCase()
 */
void KArchiveTest::cleanupTestCase()
{
    QFile::remove(s_zipMaxLengthFileName);
    QFile::remove(s_zipFileName);
    QFile::remove(s_zipLocaleFileName);
#ifndef Q_OS_WIN
    QFile::remove(QStringLiteral("test3_symlink"));
#endif
}

///

#if HAVE_XZ_SUPPORT

/*
 * Prepares dataset for 7zip tests
 */
void KArchiveTest::setup7ZipData()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow(".7z") << "karchivetest.7z";
}

/*
 * @dataProvider testCreate7Zip_data
 */
void KArchiveTest::testCreate7Zip()
{
    QFETCH(QString, fileName);

    QBENCHMARK {
        K7Zip k7zip(fileName);
        QVERIFY(k7zip.open(QIODevice::WriteOnly));

        writeTestFilesToArchive(&k7zip);

        QVERIFY(k7zip.close());

        QFileInfo fileInfo(QFile::encodeName(fileName));
        QVERIFY(fileInfo.exists());
        // qDebug() << "fileInfo.size()" << fileInfo.size();
        // We can't check for an exact size because of the addLocalFile, whose data is system-dependent
        QVERIFY(fileInfo.size() > 390);
    }
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::testRead7Zip() // testCreate7Zip must have been run first.
{
    QFETCH(QString, fileName);

    QBENCHMARK {
        K7Zip k7zip(fileName);

        QVERIFY(k7zip.open(QIODevice::ReadOnly));

        const KArchiveDirectory *dir = k7zip.directory();
        QVERIFY(dir != nullptr);
        const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

#ifndef Q_OS_WIN
        QCOMPARE(listing.count(), 16);
#else
        QCOMPARE(listing.count(), 15);
#endif
        QCOMPARE(listing[0], QString("mode=40755 path=aaaemptydir type=dir"));
        QCOMPARE(listing[1], QString("mode=40777 path=dir type=dir"));
        QCOMPARE(listing[2], QString("mode=40777 path=dir/subdir type=dir"));
        QCOMPARE(listing[3], QString("mode=100644 path=dir/subdir/mediumfile2 type=file size=100"));
        QCOMPARE(listing[4], QString("mode=100644 path=empty type=file size=0"));
        QCOMPARE(listing[5], QString("mode=100755 path=executableAll type=file size=17"));
        QCOMPARE(listing[6], QString("mode=100644 path=hugefile type=file size=20000"));
        QCOMPARE(listing[7], QString("mode=100644 path=mediumfile type=file size=100"));
        QCOMPARE(listing[8], QString("mode=40777 path=my type=dir"));
        QCOMPARE(listing[9], QString("mode=40777 path=my/dir type=dir"));
        QCOMPARE(listing[10], QString("mode=100644 path=my/dir/test3 type=file size=28"));
        QCOMPARE(listing[11], QString("mode=100440 path=test1 type=file size=5"));
        QCOMPARE(listing[12], QString("mode=100644 path=test2 type=file size=8"));
        QCOMPARE(listing[13], QString("mode=40777 path=z type=dir"));
        // This one was added with addLocalFile, so ignore mode/user/group.
        QString str = listing[14];
        str.replace(QRegularExpression(QStringLiteral("mode.*path=")), QStringLiteral("path="));
        QCOMPARE(str, QString("path=z/test3 type=file size=13"));
#ifndef Q_OS_WIN
        str = listing[15];
        str.replace(QRegularExpression(QStringLiteral("mode.*path=")), QStringLiteral("path="));
        QCOMPARE(str, QString("path=z/test3_symlink type=file size=0 symlink=test3"));
#endif

        QVERIFY(k7zip.close());
    }
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::test7ZipFileData()
{
    QFETCH(QString, fileName);

    // testCreate7Zip must have been run first.
    K7Zip k7zip(fileName);
    QVERIFY(k7zip.open(QIODevice::ReadOnly));

    testFileData(&k7zip);

    QVERIFY(k7zip.close());
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::test7ZipCopyTo()
{
    QFETCH(QString, fileName);

    // testCreateTar must have been run first.
    K7Zip k7zip(fileName);
    QVERIFY(k7zip.open(QIODevice::ReadOnly));

    testCopyTo(&k7zip);

    QVERIFY(k7zip.close());
}

/*!
 * @dataProvider setupData
 */
void KArchiveTest::test7ZipReadWrite()
{
    QFETCH(QString, fileName);

    // testCreate7zip must have been run first.
    {
        K7Zip k7zip(fileName);
        QVERIFY(k7zip.open(QIODevice::ReadWrite));

        testReadWrite(&k7zip);
        testFileData(&k7zip);

        QVERIFY(k7zip.close());
    }

    // Reopen it and check it
    {
        K7Zip k7zip(fileName);
        QVERIFY(k7zip.open(QIODevice::ReadOnly));
        testFileData(&k7zip);
        const KArchiveDirectory *dir = k7zip.directory();
        const KArchiveEntry *e = dir->entry(QStringLiteral("newfile"));
        QVERIFY(e && e->isFile());
        const KArchiveFile *f = (KArchiveFile *)e;
        QCOMPARE(f->data().size(), 8);
    }

    // NOTE This is the last test for this dataset. so cleanup here
    QFile::remove(fileName);
}

/*!
 * @dataProvider test7ZipMaxLength_data
 */
void KArchiveTest::test7ZipMaxLength()
{
    QFETCH(QString, fileName);

    K7Zip k7zip(fileName);

    QVERIFY(k7zip.open(QIODevice::WriteOnly));

    // Generate long filenames of each possible length bigger than 98...
    for (int i = 98; i < 514; i++) {
        QString str;
        QString num;
        str.fill('a', i - 10);
        num.setNum(i);
        num = num.rightJustified(10, '0');
        k7zip.writeFile(str + num, "hum");
    }
    QVERIFY(k7zip.close());

    QVERIFY(k7zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = k7zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing[0],
             QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000098 type=file size=3"));
    QCOMPARE(
        listing[3],
        QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000101 type=file size=3"));
    QCOMPARE(
        listing[4],
        QString("mode=100644 path=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa0000000102 type=file size=3"));

    QCOMPARE(listing.count(), 416);

    QVERIFY(k7zip.close());

    // NOTE Cleanup here
    QFile::remove(fileName);
}

void KArchiveTest::test7ZipNamelessFile()
{
    QBuffer archive;

    K7Zip writer(&archive);

    QVERIFY(writer.open(QIODevice::WriteOnly));
    QVERIFY(writer.writeFile(QString(), "Hello from a nameless file.", 0100644, "azhar", "users"));
    QVERIFY(writer.close());

    K7Zip reader(&archive);

    QVERIFY(reader.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = reader.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 1);
    QCOMPARE(listing[0], "mode=100644 path=contents type=file size=27");

    QVERIFY(reader.close());
}

void KArchiveTest::test7ZipMultipleNamelessFiles()
{
    const QString fileName = QFINDTESTDATA("data/multiple_nameless_files.7z");
    QVERIFY(!fileName.isEmpty());

    K7Zip k7zip(fileName);
    QVERIFY2(k7zip.open(QIODevice::ReadOnly), "data/multiple_nameless_files.7z");

    const KArchiveDirectory *dir = k7zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 2);

    QCOMPARE(listing[0], QString("mode=10600 path=multiple_nameless_files_1 type=file size=27"));
    QCOMPARE(listing[1], QString("mode=10600 path=multiple_nameless_files_2 type=file size=33"));

    QVERIFY(k7zip.close());
}

// This test was added to verify K7ZipPrivate::readNumber() boundary check.
// See: https://invent.kde.org/frameworks/karchive/-/merge_requests/105
void KArchiveTest::test7ZipReadNumber()
{
    const QString fileName = QFINDTESTDATA("data/readnumber_check.7z");
    QVERIFY(!fileName.isEmpty());

    K7Zip k7zip(fileName);
    QVERIFY2(k7zip.open(QIODevice::ReadOnly), "data/readnumber_check.7z");

    const KArchiveDirectory *dir = k7zip.directory();
    QVERIFY(dir != nullptr);
    const QStringList listing = recursiveListEntries(dir, QLatin1String(""), 0);

    QCOMPARE(listing.count(), 1);
    QCOMPARE(listing[0], QString("mode=644 path=file.txt type=file size=5"));

    QVERIFY(k7zip.close());
}

void KArchiveTest::test7ZipFileNameEndsInSlash()
{
    const QString fileName = QFINDTESTDATA("data/filename_ends_in_slash.7z");
    QVERIFY(!fileName.isEmpty());

    K7Zip k7zip(fileName);
    QVERIFY(!k7zip.open(QIODevice::ReadOnly));
}

void KArchiveTest::test7ZipOssFuzzIssues_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow("issue_440829292") << "data/ossfuzz_issue_440829292.7z";
    QTest::newRow("issue_441906077") << "data/ossfuzz_issue_441906077.7z";
    QTest::newRow("testcase_4566647131406336") << "data/ossfuzz_testcase_4566647131406336.7z";
    QTest::newRow("testcase_4784793343819776") << "data/ossfuzz_testcase_4784793343819776.7z";
    QTest::newRow("testcase_5082404562993152") << "data/ossfuzz_testcase_5082404562993152.7z";
    QTest::newRow("testcase_5125023070486528") << "data/ossfuzz_testcase_5125023070486528.7z";
    QTest::newRow("testcase_5132547098214400") << "data/ossfuzz_testcase_5132547098214400.7z";
    QTest::newRow("testcase_5141245598171136") << "data/ossfuzz_testcase_5141245598171136.7z";
    QTest::newRow("testcase_5560695602348032") << "data/ossfuzz_testcase_5560695602348032.7z";
    QTest::newRow("testcase_5581009572921344") << "data/ossfuzz_testcase_5581009572921344.7z";
    QTest::newRow("testcase_5821682553257984") << "data/ossfuzz_testcase_5821682553257984.7z";
    QTest::newRow("testcase_5991129817612288") << "data/ossfuzz_testcase_5991129817612288.7z";
    QTest::newRow("testcase_6077171694370816") << "data/ossfuzz_testcase_6077171694370816.7z";
    QTest::newRow("testcase_6096742417498112") << "data/ossfuzz_testcase_6096742417498112.7z";
    QTest::newRow("testcase_6213340184772608") << "data/ossfuzz_testcase_6213340184772608.7z";
    QTest::newRow("testcase_6248361801089024") << "data/ossfuzz_testcase_6248361801089024.7z";
    QTest::newRow("testcase_6366650283917312") << "data/ossfuzz_testcase_6366650283917312.7z";
    QTest::newRow("testcase_6532014901886976") << "data/ossfuzz_testcase_6532014901886976.7z";
    QTest::newRow("testcase_6593198198947840") << "data/ossfuzz_testcase_6593198198947840.7z";
    QTest::newRow("testcase_6653406484955136") << "data/ossfuzz_testcase_6653406484955136.7z";
}

void KArchiveTest::test7ZipOssFuzzIssues()
{
    QFETCH(QString, fileName);

    const QString filePath = QFINDTESTDATA(fileName);
    QVERIFY(!filePath.isEmpty());

    K7Zip k7zip(filePath);
    if (k7zip.open(QIODevice::ReadOnly)) {
        k7zip.close();
    }
}

#if HAVE_OPENSSL_SUPPORT
void KArchiveTest::test7ZipPasswordProtected()
{
    const QString fileName = QFINDTESTDATA("data/password_protected.7z");
    QVERIFY(!fileName.isEmpty());

    K7Zip k7zip(fileName);
    QVERIFY(!k7zip.open(QIODevice::ReadOnly)); // Should fail without a password

    QVERIFY(k7zip.passwordNeeded());
    k7zip.setPassword("youshallnotpass");

    QVERIFY2(k7zip.open(QIODevice::ReadOnly), "data/password_protected.7z");

    const KArchiveDirectory *dir = k7zip.directory();
    QVERIFY(dir != nullptr);

    const KArchiveEntry *entry = dir->entry("file.txt");
    QVERIFY(entry != nullptr);

    QVERIFY(entry->name() == "file.txt");
    QVERIFY(entry->isFile());

    const QByteArray fileData = QByteArray("Hello from an encrypted file in a 7z archive!\n");

    const KArchiveFile *file = static_cast<const KArchiveFile *>(entry);
    QCOMPARE(file->data(), fileData);

    QVERIFY(k7zip.close());
}
#endif

void KArchiveTest::test7ZipReadCoder_data()
{
    QTest::addColumn<QString>("fileName");

    QTest::newRow("copy") << "data/7z_coder_test_copy.7z";
    QTest::newRow("lzma") << "data/7z_coder_test_lzma.7z";
    QTest::newRow("lzma2") << "data/7z_coder_test_lzma2.7z";
    QTest::newRow("deflate") << "data/7z_coder_test_deflate.7z";
    QTest::newRow("bcj") << "data/7z_coder_test_bcj.7z";
    QTest::newRow("bcj2") << "data/7z_coder_test_bcj2.7z";
    QTest::newRow("bzip2") << "data/7z_coder_test_bzip2.7z";
    QTest::newRow("zstd") << "data/7z_coder_test_zstd.7z";
}

/*!
 * @dataProvider test7ZipReadCoder_data
 */
void KArchiveTest::test7ZipReadCoder()
{
    QFETCH(QString, fileName);

    const QString filePath = QFINDTESTDATA(fileName);
    QVERIFY(!filePath.isEmpty());

    K7Zip k7zip(filePath);
    QVERIFY(k7zip.open(QIODevice::ReadOnly));

    const KArchiveDirectory *dir = k7zip.directory();
    QVERIFY(dir != nullptr);

    const KArchiveEntry *entry = dir->entry("hello.txt");
    QVERIFY(entry != nullptr);
    QVERIFY(entry->isFile());

    const QByteArray fileData = QByteArray("Hello, World!\n");

    const KArchiveFile *file = static_cast<const KArchiveFile *>(entry);

    if (fileName.contains("bcj2")) {
        QEXPECT_FAIL("", "bcj2 decoding fails, see https://bugs.kde.org/show_bug.cgi?id=509201", Continue);
    }
    QCOMPARE(file->data(), fileData);

    QVERIFY(k7zip.close());
}

#endif

#include "moc_karchivetest.cpp"
