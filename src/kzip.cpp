/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2002 Holger Schroeder <holger-kde@holgis.net>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "kzip.h"
#include "karchive_p.h"
#include "kcompressiondevice.h"
#include "klimitediodevice_p.h"
#include "loggingcategory.h"

#include <QByteArray>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QtEndian>
#include <qplatformdefs.h>

#include <string.h>
#include <time.h>
#include <zlib.h>

#ifndef QT_STAT_LNK
#define QT_STAT_LNK 0120000
#endif // QT_STAT_LNK

static const int max_path_len = 4095; // maximum number of character a path may contain

static quint16 parseUi16(const char *buffer)
{
    return quint16((uchar)buffer[0] | (uchar)buffer[1] << 8);
}

static uint parseUi32(const char *buffer)
{
    return uint((uchar)buffer[0] | (uchar)buffer[1] << 8 | (uchar)buffer[2] << 16 | (uchar)buffer[3] << 24);
}

static quint64 parseUi64(const char *buffer)
{
    const uint a = uint((uchar)buffer[0] | (uchar)buffer[1] << 8 | (uchar)buffer[2] << 16 | (uchar)buffer[3] << 24);
    const uint b = uint((uchar)buffer[4] | (uchar)buffer[5] << 8 | (uchar)buffer[6] << 16 | (uchar)buffer[7] << 24);
    return (a | (quint64)b << 32);
}

static void transformToMsDos(const QDateTime &_dt, char *buffer)
{
    const QDateTime dt = _dt.isValid() ? _dt : QDateTime::currentDateTime();
    /* clang-format off */
    const quint16 time = (dt.time().hour() << 11) // 5 bit hour
                         | (dt.time().minute() << 5) // 6 bit minute
                         | (dt.time().second() >> 1); // 5 bit double seconds
    /* clang-format on */

    buffer[0] = char(time);
    buffer[1] = char(time >> 8);

    /* clang-format off */
    const quint16 date = ((dt.date().year() - 1980) << 9) // 7 bit year 1980-based
                         | (dt.date().month() << 5) // 4 bit month
                         | (dt.date().day()); // 5 bit day
    /* clang-format on */

    buffer[2] = char(date);
    buffer[3] = char(date >> 8);
}

static uint transformFromMsDos(const char *buffer)
{
    const quint16 time = parseUi16(buffer);
    int h = time >> 11;
    int m = (time & 0x7ff) >> 5;
    int s = (time & 0x1f) * 2;
    QTime qt(h, m, s);

    const quint16 date = parseUi16(buffer + 2);
    int y = (date >> 9) + 1980;
    int o = (date & 0x1ff) >> 5;
    int d = (date & 0x1f);
    QDate qd(y, o, d);

    QDateTime dt(qd, qt);
    return dt.toSecsSinceEpoch();
}

// == parsing routines for zip headers

/** all relevant information about parsing file information */
struct ParseFileInfo {
    // file related info
    mode_t perm; // permissions of this file
    // TODO: use quint32 instead of a uint?
    uint atime; // last access time (UNIX format)
    uint mtime; // modification time (UNIX format)
    uint ctime; // creation time (UNIX format)
    int uid; // user id (-1 if not specified)
    int gid; // group id (-1 if not specified)
    QByteArray guessed_symlink; // guessed symlink target
    uint dataoffset = 0; // offset from start of local header to data

    // parsing related info
    bool exttimestamp_seen; // true if extended timestamp extra field
    // has been parsed
    bool newinfounix_seen; // true if Info-ZIP Unix New extra field has
    // been parsed

    // file sizes from a ZIP64 extra field
    quint64 uncompressedSize = 0;
    quint64 compressedSize = 0;
    // position of the Local File Header itself, or from the
    // ZIP64 extra field in the Central Directory
    quint64 localheaderoffset = 0;

    ParseFileInfo()
        : perm(0100644)
        , uid(-1)
        , gid(-1)
        , exttimestamp_seen(false)
        , newinfounix_seen(false)
    {
        ctime = mtime = atime = time(nullptr);
    }
};

/** updates the parse information with the given extended timestamp extra field.
 * @param buffer start content of buffer known to contain an extended
 *  timestamp extra field (without magic & size)
 * @param size size of field content (must not count magic and size entries)
 * @param islocal true if this is a local field, false if central
 * @param pfi ParseFileInfo object to be updated
 * @return true if processing was successful
 */
static bool parseExtTimestamp(const char *buffer, int size, bool islocal, ParseFileInfo &pfi)
{
    if (size < 1) {
        // qCDebug(KArchiveLog) << "premature end of extended timestamp (#1)";
        return false;
    } /*end if*/
    int flags = *buffer; // read flags
    buffer += 1;
    size -= 1;

    if (flags & 1) { // contains modification time
        if (size < 4) {
            // qCDebug(KArchiveLog) << "premature end of extended timestamp (#2)";
            return false;
        } /*end if*/
        pfi.mtime = parseUi32(buffer);
        buffer += 4;
        size -= 4;
    } /*end if*/
    // central extended field cannot contain more than the modification time
    // even if other flags are set
    if (!islocal) {
        pfi.exttimestamp_seen = true;
        return true;
    } /*end if*/

    if (flags & 2) { // contains last access time
        if (size < 4) {
            // qCDebug(KArchiveLog) << "premature end of extended timestamp (#3)";
            return true;
        } /*end if*/
        pfi.atime = parseUi32(buffer);
        buffer += 4;
        size -= 4;
    } /*end if*/

    if (flags & 4) { // contains creation time
        if (size < 4) {
            // qCDebug(KArchiveLog) << "premature end of extended timestamp (#4)";
            return true;
        } /*end if*/
        pfi.ctime = parseUi32(buffer);
        buffer += 4;
    } /*end if*/

    pfi.exttimestamp_seen = true;
    return true;
}

/** updates the parse information with the given Info-ZIP Unix old extra field.
 * @param buffer start of content of buffer known to contain an Info-ZIP
 *  Unix old extra field (without magic & size)
 * @param size size of field content (must not count magic and size entries)
 * @param islocal true if this is a local field, false if central
 * @param pfi ParseFileInfo object to be updated
 * @return true if processing was successful
 */
static bool parseInfoZipUnixOld(const char *buffer, int size, bool islocal, ParseFileInfo &pfi)
{
    // spec mandates to omit this field if one of the newer fields are available
    if (pfi.exttimestamp_seen || pfi.newinfounix_seen) {
        return true;
    }

    if (size < 8) {
        // qCDebug(KArchiveLog) << "premature end of Info-ZIP unix extra field old";
        return false;
    }

    pfi.atime = parseUi32(buffer);
    pfi.mtime = parseUi32(buffer + 4);
    if (islocal && size >= 12) {
        pfi.uid = parseUi16(buffer + 8);
        pfi.gid = parseUi16(buffer + 10);
    } /*end if*/
    return true;
}

#if 0 // not needed yet
/** updates the parse information with the given Info-ZIP Unix new extra field.
 * @param buffer start of content of buffer known to contain an Info-ZIP
 *      Unix new extra field (without magic & size)
 * @param size size of field content (must not count magic and size entries)
 * @param islocal true if this is a local field, false if central
 * @param pfi ParseFileInfo object to be updated
 * @return true if processing was successful
 */
static bool parseInfoZipUnixNew(const char *buffer, int size, bool islocal,
                                ParseFileInfo &pfi)
{
    if (!islocal) { // contains nothing in central field
        pfi.newinfounix = true;
        return true;
    }

    if (size < 4) {
        qCDebug(KArchiveLog) << "premature end of Info-ZIP unix extra field new";
        return false;
    }

    pfi.uid = (uchar)buffer[0] | (uchar)buffer[1] << 8;
    buffer += 2;
    pfi.gid = (uchar)buffer[0] | (uchar)buffer[1] << 8;
    buffer += 2;

    pfi.newinfounix = true;
    return true;
}
#endif

/**
 * parses the extra field
 *
 * the 64-bit values for uncompressed size, compressed size and local header offset
 * are retrieved only if the 32-bit values are 0xFFFFFFFF
 *
 * @param buffer start of buffer where the extra field is to be found
 * @param size size of the extra field
 * @param pfi ParseFileInfo object which to write the results into
 * @param uncompressedSize32 quint32 32-bit value for uncompressed size
 * @param compressedSize32 quint32 32-bit value for compressed size
 * @param localHeaderOffset32 quint32 32-bit value for local header offset
 * @return true if parsing was successful
 */
static bool parseExtraField(const char *buffer, int size, ParseFileInfo &pfi, quint32 uncompressedSize32, quint32 compressedSize32, quint32 localHeaderOffset32)
{
    while (size >= 4) { // as long as a potential extra field can be read
        int magic = parseUi16(buffer);
        int fieldsize = parseUi16(buffer + 2);
        buffer += 4;
        size -= 4;

        if (fieldsize > size) {
            // qCDebug(KArchiveLog) << "fieldsize: " << fieldsize << " size: " << size;
            // qCDebug(KArchiveLog) << "premature end of extra fields reached";
            break;
        }

        switch (magic) {
        case 0x0001: {
            // ZIP64 extended file information
            int offset = 0;
            if (uncompressedSize32 == 0xFFFFFFFF && offset + 8 <= fieldsize) {
                pfi.uncompressedSize = parseUi64(buffer + offset);
                offset += 8;
            }
            if (compressedSize32 == 0xFFFFFFFF && offset + 8 <= fieldsize) {
                pfi.compressedSize = parseUi64(buffer + offset);
                offset += 8;
            }
            if (localHeaderOffset32 == 0xFFFFFFFF && offset + 8 <= fieldsize) {
                pfi.localheaderoffset = parseUi64(buffer + offset);
                offset += 8;
            }
            break;
        }
        case 0x5455: // extended timestamp
            if (!parseExtTimestamp(buffer, fieldsize, true, pfi)) {
                return false;
            }
            break;
        case 0x5855: // old Info-ZIP unix extra field
            if (!parseInfoZipUnixOld(buffer, fieldsize, true, pfi)) {
                return false;
            }
            break;
#if 0 // not needed yet
        case 0x7855:        // new Info-ZIP unix extra field
            if (!parseInfoZipUnixNew(buffer, fieldsize, islocal, pfi)) {
                return false;
            }
            break;
#endif
        default:
            /* ignore everything else */
            ;
        } /*end switch*/

        buffer += fieldsize;
        size -= fieldsize;
    } /*wend*/
    return true;
}

/**
 * Advance the current file position to the next possible header location
 *
 * @param dev device that is read from
 * @param header storage location for the complete header
 * @param minSize required size of the header
 * @return true if a possible header location matching the 'PK' signature
 *     bytes have been found, false if the end of file has been reached
 */
static bool seekAnyHeader(QIODevice *dev, QByteArray &header, qsizetype minSize)
{
    header.resize(1024);
    int n = dev->peek(header.data(), header.size());

    while (n >= minSize) {
        if (auto i = QByteArrayView(header.data(), n).indexOf("PK"); i >= 0) {
            dev->skip(i);
            if ((i + minSize) < n) {
                header.remove(0, i);
                header.resize(minSize);
                return true;
            } else {
                n = dev->peek(header.data(), minSize);
                header.resize(n);
                return n >= minSize;
            }
        }
        dev->skip(n - 1);
        n = dev->peek(header.data(), header.size());
    }
    header.resize(n);
    return false;
}

/**
 * Advance the current file position to the next header following
 * a data descriptor
 *
 * @param dev device that is read from
 * @param isZip64 use Zip64 data descriptor layout
 * @return true if a data descriptor signature has been found, and the
 *     compressed size matches the current position advance
 */
static bool seekPostDataDescriptor(QIODevice *dev, bool isZip64)
{
    // Both size fields are uint64 for Zip64, uint32 otherwise
    const qsizetype descriptorSize = isZip64 ? 24 : 16;

    QByteArray header;
    const qint64 oldPos = dev->pos();

    while (seekAnyHeader(dev, header, descriptorSize)) {
        // qCDebug(KArchiveLog) << "Possible data descriptor header at" << dev->pos() << header;
        if (header.startsWith("PK\x07\x08")) {
            const qint64 compressedSize = isZip64 ? parseUi64(header.constData() + 8) //
                                                  : parseUi32(header.constData() + 8);
            // Do not move compressedSize to the left side of the check
            // we have on the left on purpose to prevent addition overflows
            if ((compressedSize > 0) && (oldPos == dev->pos() - compressedSize)) {
                dev->skip(descriptorSize);
                return true;
            }
            dev->skip(4); // Skip found 'PK\7\8'
        } else {
            dev->skip(2); // Skip found 'PK'
        }
    }
    return false;
}

/**
 * Advance the current file position to the next header
 * @param dev device that is read from
 * @return true if a local or central header token could be reached, false on error
 */
static bool seekToNextHeaderToken(QIODevice *dev)
{
    QByteArray header;

    while (seekAnyHeader(dev, header, 4)) {
        // qCDebug(KArchiveLog) << "Possible header at" << dev->pos() << header;
        // PK34 for the next local header in case there is no data descriptor
        // PK12 for the central header in case there is no data descriptor
        if (header.startsWith("PK\x03\x04") || header.startsWith("PK\x01\x02")) {
            return true;
        } else {
            dev->skip(2); // Skip found 'PK'
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////
/////////////////////////// KZip ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////

class Q_DECL_HIDDEN KZip::KZipPrivate
{
public:
    KZipPrivate()
        : m_crc(0)
        , m_currentFile(nullptr)
        , m_currentDev(nullptr)
        , m_compression(8)
        , m_extraField(KZip::NoExtraField)
        , m_offset(0)
    {
    }

    unsigned long m_crc; // checksum
    KZipFileEntry *m_currentFile; // file currently being written
    QIODevice *m_currentDev; // filterdev used to write to the above file
    QList<KZipFileEntry *> m_fileList; // flat list of all files, for the index (saves a recursive method ;)
    int m_compression;
    KZip::ExtraField m_extraField;
    // m_offset holds the offset of the place in the zip,
    // where new data can be appended. after openarchive it points to 0, when in
    // writeonly mode, or it points to the beginning of the central directory.
    // each call to writefile updates this value.
    quint64 m_offset;
    // Position of the first Local File Header
    quint64 m_startPos = 0;
};

KZip::KZip(const QString &fileName)
    : KArchive(fileName)
    , d(new KZipPrivate)
{
}

KZip::KZip(QIODevice *dev)
    : KArchive(dev)
    , d(new KZipPrivate)
{
}

KZip::~KZip()
{
    // qCDebug(KArchiveLog) << this;
    if (isOpen()) {
        close();
    }
    delete d;
}

bool KZip::openArchive(QIODevice::OpenMode mode)
{
    // qCDebug(KArchiveLog);
    d->m_fileList.clear();

    if (mode == QIODevice::WriteOnly) {
        return true;
    }

    char buffer[47];

    // Check that it's a valid ZIP file
    // KArchive::open() opened the underlying device already.

    // contains information gathered from the local file headers
    QHash<QByteArray, ParseFileInfo> pfi_map;

    QIODevice *dev = device();

    // We set a bool for knowing if we are allowed to skip the start of the file
    bool startOfFile = true;

    qint64 expectedStartPos = 0;

    for (;;) { // repeat until 'end of entries' signature is reached
        // qCDebug(KArchiveLog) << "loop starts";
        // qCDebug(KArchiveLog) << "dev->pos() now : " << dev->pos();
        int n = dev->read(buffer, 4);

        if (n < 4) {
            setErrorString(tr("Invalid ZIP file. Unexpected end of file. (Error code: %1)").arg(1));
            return false;
        }

        if (!memcmp(buffer, "PK\6\6", 4)) { // 'Zip64 end of central directory record'
            startOfFile = false;
            break;
        }

        if (!memcmp(buffer, "PK\5\6", 4)) { // 'end of entries'
            // qCDebug(KArchiveLog) << "PK56 found end of archive at offset" << dev->pos();
            startOfFile = false;
            break;
        }

        if (!memcmp(buffer, "PK\3\4", 4)) { // local file header
            // qCDebug(KArchiveLog) << "PK34 found local file header at offset" << dev->pos();
            startOfFile = false;

            ParseFileInfo pfi;
            pfi.localheaderoffset = dev->pos() - 4;

            // read static header stuff
            n = dev->read(buffer, 26);
            if (n < 26) {
                setErrorString(tr("Invalid ZIP file. Unexpected end of file. (Error code: %1)").arg(4));
                return false;
            }
            int neededVersion = parseUi16(buffer);
            bool isZip64 = neededVersion >= 45;

            int gpf = (uchar)buffer[2]; // "general purpose flag" not "general protection fault" ;-)
            int compression_mode = parseUi16(buffer + 4);
            uint mtime = transformFromMsDos(buffer + 6);

            qint64 compr_size = parseUi32(buffer + 14);
            qint64 uncomp_size = parseUi32(buffer + 18);
            const int namelen = parseUi16(buffer + 22);
            const int extralen = parseUi16(buffer + 24);

            /*
              qCDebug(KArchiveLog) << "general purpose bit flag: " << gpf;
              qCDebug(KArchiveLog) << "compressed size: " << compr_size;
              qCDebug(KArchiveLog) << "uncompressed size: " << uncomp_size;
              qCDebug(KArchiveLog) << "namelen: " << namelen;
              qCDebug(KArchiveLog) << "extralen: " << extralen;
              qCDebug(KArchiveLog) << "archive size: " << dev->size();
            */

            // read fileName
            if (namelen <= 0) {
                setErrorString(tr("Invalid ZIP file. Negative name length"));
                return false;
            }
            QByteArray fileName = dev->read(namelen);
            if (fileName.size() < namelen) {
                setErrorString(tr("Invalid ZIP file. Name not completely read (#2)"));
                return false;
            }

            pfi.mtime = mtime;
            pfi.dataoffset = 30 + extralen + namelen;

            // read and parse the beginning of the extra field,
            // skip rest of extra field in case it is too long
            quint64 extraFieldEnd = dev->pos() + extralen;
            int handledextralen = qMin(extralen, (int)sizeof buffer);

            // if (handledextralen)
            //    qCDebug(KArchiveLog) << "handledextralen: " << handledextralen;

            n = dev->read(buffer, handledextralen);
            // no error msg necessary as we deliberately truncate the extra field
            if (!parseExtraField(buffer, n, pfi, uncomp_size, compr_size, 0)) {
                setErrorString(tr("Invalid ZIP File. Broken ExtraField."));
                return false;
            }

            if (compr_size == 0xFFFFFFFF) {
                compr_size = pfi.compressedSize;
            }
            if (uncomp_size == 0xFFFFFFFF) {
                uncomp_size = pfi.uncompressedSize;
            }

            // jump to end of extra field
            dev->seek(extraFieldEnd);

            // we have to take care of the 'general purpose bit flag'.
            // if bit 3 is set, the header doesn't contain the length of
            // the file and we look for the signature 'PK\7\8'.
            if (gpf & 8) {
                // here we have to read through the compressed data to find
                // the next PKxx
                if (!seekPostDataDescriptor(dev, isZip64)) {
                    setErrorString(tr("Could not seek to next header token"));
                    return false;
                }
            } else {
                // here we skip the compressed data and jump to the next header
                // qCDebug(KArchiveLog) << "general purpose bit flag indicates, that local file header contains valid size";
                bool foundSignature = false;
                // check if this could be a symbolic link
                if (compression_mode == NoCompression //
                    && uncomp_size <= max_path_len //
                    && uncomp_size > 0) {
                    // read content and store it
                    // If it's not a symlink, then we'll just discard the data for now.
                    pfi.guessed_symlink = dev->read(uncomp_size);
                    if (pfi.guessed_symlink.size() < uncomp_size) {
                        setErrorString(tr("Invalid ZIP file. Unexpected end of file. (#5)"));
                        return false;
                    }
                } else {
                    if (compr_size > dev->size()) {
                        // here we cannot trust the compressed size, so scan through the compressed
                        // data to find the next header
                        if (!seekToNextHeaderToken(dev)) {
                            setErrorString(tr("Could not seek to next header token"));
                            return false;
                        }
                        foundSignature = true;
                    } else {
                        //          qCDebug(KArchiveLog) << "before interesting dev->pos(): " << dev->pos();
                        if (dev->skip(compr_size) != compr_size) {
                            setErrorString(tr("Could not seek to file compressed size"));
                            return false;
                        }
                        /*          qCDebug(KArchiveLog) << "after interesting dev->pos(): " << dev->pos();
                                                if (success)
                                                qCDebug(KArchiveLog) << "dev->at was successful... ";
                                                else
                                                qCDebug(KArchiveLog) << "dev->at failed... ";*/
                    }
                }
                // test for optional data descriptor
                if (!foundSignature) {
                    //                     qCDebug(KArchiveLog) << "Testing for optional data descriptor";
                    // read static data descriptor
                    n = dev->peek(buffer, 4);
                    if (n < 4) {
                        setErrorString(tr("Invalid ZIP file. Unexpected end of file. (#1)"));
                        return false;
                    }

                    QByteArrayView currentHead(buffer, 4);
                    // qCDebug(KArchiveLog) << "Testing for optional data descriptor @ " << dev->pos() << currentHead;
                    if (currentHead.startsWith("PK\x07\x08")) {
                        dev->skip(16); // skip rest of the 'data_descriptor'
                    } else if (!(currentHead.startsWith("PK\x03\x04") || currentHead.startsWith("PK\x01\x02"))) {
                        // assume data descriptor without signature
                        dev->skip(12); // skip rest of the 'data_descriptor'
                    }
                }

                // not needed any more
                /*                // here we calculate the length of the file in the zip
                // with headers and jump to the next header.
                uint skip = compr_size + namelen + extralen;
                offset += 30 + skip;*/
            }
            pfi_map.insert(fileName, pfi);
        } else if (!memcmp(buffer, "PK\1\2", 4)) { // central block
            // qCDebug(KArchiveLog) << "PK12 found central block at offset" << dev->pos();
            startOfFile = false;

            // so we reached the central header at the end of the zip file
            // here we get all interesting data out of the central header
            // of a file
            auto offset = dev->pos() - 4;

            // set offset for appending new files
            if (d->m_offset == 0) {
                d->m_offset = offset;
            }

            n = dev->read(buffer + 4, 42);
            if (n < 42) {
                setErrorString(tr("Invalid ZIP file, central entry too short (not long enough for valid entry)"));
                return false;
            }

            // length of extra attributes
            const int extralen = parseUi16(buffer + 30);
            // length of comment for this file
            const int commlen = parseUi16(buffer + 32);
            // compression method of this file
            const int cmethod = parseUi16(buffer + 10);
            // int gpf =  parseUi16(buffer + 8);
            // qCDebug(KArchiveLog) << "general purpose flag=" << gpf;
            // length of the fileName (well, pathname indeed)
            const int namelen = parseUi16(buffer + 28);
            if (namelen <= 0) {
                setErrorString(tr("Invalid ZIP file, file path name length is zero"));
                return false;
            }
            const int varDataDesiredLength = namelen + extralen;
            const QByteArray varData = dev->read(varDataDesiredLength);
            if (varData.length() < varDataDesiredLength) {
                setErrorString(tr("Invalid ZIP file, unable to read %1 + %2 bytes for filename and extra data at offset %3")
                                   .arg(namelen, extralen, dev->pos() - varData.size()));
                return false;
            }

            // crc32 of the file
            uint crc32 = parseUi32(buffer + 16);
            // 32 bit uncompressed file size
            quint32 ucsize32 = parseUi32(buffer + 24);
            // 32 bit compressed file size
            quint32 csize32 = parseUi32(buffer + 20);
            // 32 bit offset of local header
            quint32 localheaderoffset_32 = parseUi32(buffer + 42);

            ParseFileInfo extrafi;
            if (extralen) {
                parseExtraField(varData.constData() + namelen, extralen, extrafi, ucsize32, csize32, localheaderoffset_32);
            }

            // uncompressed file size
            quint64 ucsize = (ucsize32 == 0xFFFFFFFF) ? extrafi.uncompressedSize : ucsize32;
            // compressed file size
            quint64 csize = (csize32 == 0xFFFFFFFF) ? extrafi.compressedSize : csize32;
            // offset of local header
            quint64 localheaderoffset = (localheaderoffset_32 == 0xFFFFFFFF) ? extrafi.localheaderoffset : localheaderoffset_32;

            QByteArray bufferName(varData.constData(), namelen);

            ParseFileInfo pfi = pfi_map.value(bufferName, ParseFileInfo());

            QString name(QFile::decodeName(bufferName));

            // qCDebug(KArchiveLog) << "name: " << name;
            // qCDebug(KArchiveLog) << "cmethod: " << cmethod;
            // qCDebug(KArchiveLog) << "extralen: " << extralen;
            // qCDebug(KArchiveLog) << "localheader dataoffset: " << pfi.dataoffset;

            // offset, where the real data for uncompression starts
            qint64 dataoffset = localheaderoffset + pfi.dataoffset;
            if (pfi.localheaderoffset != expectedStartPos + localheaderoffset) {
                if (pfi.localheaderoffset == d->m_startPos + localheaderoffset) {
                    qCDebug(KArchiveLog) << "warning:" << d->m_startPos << "extra bytes at beginning of zipfile";
                    expectedStartPos = d->m_startPos;
                } else {
                    setErrorString(tr("Invalid ZIP file, inconsistent Local Header Offset in Central Directory at %1").arg(offset));
                    return false;
                }
                dataoffset = localheaderoffset + pfi.dataoffset + d->m_startPos;
            }

            // qCDebug(KArchiveLog) << "csize: " << csize;

            int os_madeby = (uchar)buffer[5];
            bool isdir = false;
            int access = 0100644;

            if (os_madeby == 3) { // good ole unix
                access = parseUi16(buffer + 40);
            }

            QString entryName;

            if (name.endsWith(QLatin1Char('/'))) { // Entries with a trailing slash are directories
                isdir = true;
                name = name.left(name.length() - 1);
                if (os_madeby != 3) {
                    access = S_IFDIR | 0755;
                } else {
                    access |= S_IFDIR | 0700;
                }
            }

            int pos = name.lastIndexOf(QLatin1Char('/'));
            if (pos == -1) {
                entryName = name;
            } else {
                entryName = name.mid(pos + 1);
            }
            if (entryName.isEmpty()) {
                setErrorString(tr("Invalid ZIP file, found empty entry name"));
                return false;
            }

            KArchiveEntry *entry;
            if (isdir) {
                QString path = QDir::cleanPath(name);
                const KArchiveEntry *ent = rootDir()->entry(path);
                if (ent && ent->isDirectory()) {
                    // qCDebug(KArchiveLog) << "Directory already exists, NOT going to add it again";
                    entry = nullptr;
                } else {
                    QDateTime mtime = KArchivePrivate::time_tToDateTime(pfi.mtime);
                    entry = new KArchiveDirectory(this, entryName, access, mtime, rootDir()->user(), rootDir()->group(), QString());
                    // qCDebug(KArchiveLog) << "KArchiveDirectory created, entryName= " << entryName << ", name=" << name;
                }
            } else {
                QString symlink;
                if ((access & QT_STAT_MASK) == QT_STAT_LNK) {
                    symlink = QFile::decodeName(pfi.guessed_symlink);
                }
                QDateTime mtime = KArchivePrivate::time_tToDateTime(pfi.mtime);
                entry =
                    new KZipFileEntry(this, entryName, access, mtime, rootDir()->user(), rootDir()->group(), symlink, name, dataoffset, ucsize, cmethod, csize);
                static_cast<KZipFileEntry *>(entry)->setHeaderStart(localheaderoffset);
                static_cast<KZipFileEntry *>(entry)->setCRC32(crc32);
                // qCDebug(KArchiveLog) << "KZipFileEntry created, entryName= " << entryName << ", name=" << name;
                d->m_fileList.append(static_cast<KZipFileEntry *>(entry));
            }

            if (entry) {
                if (pos == -1) {
                    // We don't want to fail opening potentially malformed files, so void the return value
                    (void)rootDir()->addEntryV2(entry);
                } else {
                    // In some tar files we can find dir/./file => call cleanPath
                    QString path = QDir::cleanPath(name.left(pos));
                    // Ensure container directory exists, create otherwise
                    KArchiveDirectory *tdir = findOrCreate(path);
                    if (tdir) {
                        (void)tdir->addEntryV2(entry);
                    } else {
                        setErrorString(tr("File %1 is in folder %2, but %3 is actually a file.").arg(entryName, path, path));
                        delete entry;
                        return false;
                    }
                }
            }

            // calculate offset to next entry
            offset += 46 + commlen + extralen + namelen;
            const bool b = dev->seek(offset);
            if (!b) {
                setErrorString(tr("Could not seek to next entry"));
                return false;
            }
        } else if (startOfFile) {
            // The file does not start with any ZIP header (e.g. self-extractable ZIP files)
            // Therefore we need to find the first PK\003\004 (local header)
            // qCDebug(KArchiveLog) << "Try to skip start of file";
            startOfFile = false;
            bool foundSignature = false;

            QByteArray header;
            while (seekAnyHeader(dev, header, 4)) {
                // We have to detect the magic token for a local header: PK\003\004
                /*
                 * Note: we do not need to check the other magics, if the ZIP file has no
                 * local header, then it has not any files!
                 */
                if (header.startsWith("PK\x03\x04")) {
                    foundSignature = true;
                    d->m_startPos = dev->pos();
                    break;
                }
                if (dev->pos() > 4 * 1024 * 1024) {
                    break;
                }
                dev->skip(2); // Skip found 'PK'
            }

            if (!foundSignature) {
                setErrorString(tr("Not a ZIP file, no Local File Header found."));
                return false;
            }
        } else {
            setErrorString(tr("Invalid ZIP file. Unrecognized header at offset %1").arg(dev->pos() - 4));
            return false;
        }
    }
    // qCDebug(KArchiveLog) << "*** done *** ";
    return true;
}

bool KZip::closeArchive()
{
    if (!(mode() & QIODevice::WriteOnly)) {
        // qCDebug(KArchiveLog) << "readonly";
        return true;
    }

    // ReadWrite or WriteOnly
    // write all central dir file entries

    // to be written at the end of the file...
    uLong crc = crc32(0L, nullptr, 0);

    qint64 centraldiroffset = device()->pos();
    // qCDebug(KArchiveLog) << "closearchive: centraldiroffset: " << centraldiroffset;
    qint64 atbackup = centraldiroffset;
    for (KZipFileEntry *entry : d->m_fileList) {
        // set crc and compressed size in each local file header
        if (!device()->seek(entry->headerStart() + 14)) {
            setErrorString(tr("Could not seek to next file header: %1").arg(device()->errorString()));
            return false;
        }
        // qCDebug(KArchiveLog) << "closearchive setcrcandcsize: fileName:"
        //    << entry->path()
        //    << "encoding:" << entry->encoding();

        QByteArray path = QFile::encodeName(entry->path());
        uLong mycrc = entry->crc32();
        qint64 compressedSize = entry->compressedSize();
        qint64 size = entry->size();

        bool compressedSizeNeeds64Bit = compressedSize >= 0xFFFFFFFF;
        bool uncompressedSizeNeeds64Bit = size >= 0xFFFFFFFF;
        bool isZip64File = (compressedSizeNeeds64Bit || uncompressedSizeNeeds64Bit);

        char buffer[12];

        // crc checksum
        qToLittleEndian<quint32>(mycrc, buffer);

        if (!isZip64File) {
            qToLittleEndian<quint32>(compressedSize, buffer + 4);
            qToLittleEndian<quint32>(size, buffer + 8);

            if (device()->write(buffer, 12) != 12) {
                setErrorString(tr("Could not write file header: %1").arg(device()->errorString()));
                return false;
            }
        } else {
            // compressed file size
            qToLittleEndian<quint32>(0xFFFFFFFF, buffer + 4);
            // uncompressed file size
            qToLittleEndian<quint32>(0xFFFFFFFF, buffer + 8);

            if (device()->write(buffer, 12) != 12) {
                setErrorString(tr("Could not write file header: %1").arg(device()->errorString()));
                return false;
            }

            // jump to the Local File Header Zip64 extra field to fill the 64-bit sizes
            qint64 extraFieldOffset = entry->headerStart() + 30 + path.length();
            if (d->m_extraField == ModificationTime) {
                extraFieldOffset += 17;
            }
            // skip Zip64 extra field header id and field size
            extraFieldOffset += 4;
            if (!device()->seek(extraFieldOffset)) {
                setErrorString(tr("Could not seek to Zip64 extra field: %1").arg(device()->errorString()));
                return false;
            }

            char z64Buffer[16];
            qToLittleEndian<quint64>(size, z64Buffer);
            qToLittleEndian<quint64>(compressedSize, z64Buffer + 8);
            if (device()->write(z64Buffer, 16) != 16) {
                setErrorString(tr("Could not write Zip64 compressed size: %1").arg(device()->errorString()));
                return false;
            }
        }
    }

    device()->seek(atbackup);

    for (KZipFileEntry *entry : d->m_fileList) {
        // qCDebug(KArchiveLog) << "fileName:" << entry->path()
        //              << "encoding:" << entry->encoding();

        QByteArray path = QFile::encodeName(entry->path());
        qint64 compressedSize = entry->compressedSize();
        qint64 size = entry->size();
        qint64 myhst = entry->headerStart();

        bool compressedSizeNeeds64Bit = compressedSize >= 0xFFFFFFFF;
        bool uncompressedSizeNeeds64Bit = size >= 0xFFFFFFFF;
        bool offsetNeeds64Bit = myhst >= 0xFFFFFFFF;
        bool entryNeedsZip64 = (compressedSizeNeeds64Bit || uncompressedSizeNeeds64Bit || offsetNeeds64Bit);

        int extra_field_len = (d->m_extraField == ModificationTime) ? 9 : 0;
        uint zip64FieldSize = 0;
        if (entryNeedsZip64) {
            // 2 header id + 2 field size +
            // 8 uncompressed size + 8 compressed size + 8 offset
            zip64FieldSize = 28;
        }
        extra_field_len += zip64FieldSize;

        const int bufferSize = extra_field_len + path.length() + 46;
        char *buffer = new char[bufferSize];

        memset(buffer, 0, 46); // zero is a nice default for most header fields

        // Central header signature
        buffer[0] = 'P';
        buffer[1] = 'K';
        buffer[2] = 1;
        buffer[3] = 2;

        // Version made by (UNIX)
        buffer[4] = 0x14;
        buffer[5] = 3;
        // Version needed to extract
        buffer[6] = entryNeedsZip64 ? 45 : 0x14;
        buffer[7] = 0;

        // General purpose bit flag
        // qToLittleEndian<quint16>(0, buffer + 8);

        // compression method
        qToLittleEndian<quint16>(char(entry->encoding()), buffer + 10);

        transformToMsDos(entry->date(), &buffer[12]);

        uLong mycrc = entry->crc32();
        // crc checksum
        qToLittleEndian<quint32>(mycrc, buffer + 16);

        // compressed file size
        if (entryNeedsZip64) {
            qToLittleEndian<quint32>(0xFFFFFFFF, buffer + 20);
        } else {
            qToLittleEndian<quint32>(compressedSize, buffer + 20);
        }

        // uncompressed file size
        if (entryNeedsZip64) {
            qToLittleEndian<quint32>(0xFFFFFFFF, buffer + 24);
        } else {
            qToLittleEndian<quint32>(size, buffer + 24);
        }

        // fileName length
        qToLittleEndian<quint16>(path.length(), buffer + 28);

        // extra field length
        qToLittleEndian<quint16>(extra_field_len, buffer + 30);

        // file comment length
        // qToLittleEndian<quint16>(0, buffer + 32);

        // disk number where file starts
        // qToLittleEndian<quint16>(0, buffer + 34);

        // internal file attributes
        // none
        // external file attributes
        qToLittleEndian<quint32>(entry->permissions(), buffer + 40);

        // relative offset of local header
        if (entryNeedsZip64) {
            qToLittleEndian<quint32>(0xFFFFFFFF, buffer + 42);
        } else {
            qToLittleEndian<quint32>(myhst, buffer + 42);
        }

        // file name
        strncpy(buffer + 46, path.constData(), path.length());
        // qCDebug(KArchiveLog) << "closearchive length to write: " << bufferSize;

        // extra field
        char *extfield = buffer + 46 + path.length();
        if (d->m_extraField == ModificationTime) {
            // "Extended timestamp" header (0x5455)
            extfield[0] = 'U';
            extfield[1] = 'T';
            extfield[2] = 5; // data size
            extfield[3] = 0;
            extfield[4] = 1 | 2 | 4; // specify flags from local field
            // (unless I misread the spec)
            // provide only modification time
            unsigned long time = (unsigned long)entry->date().toSecsSinceEpoch();
            qToLittleEndian<quint32>(time, extfield + 5);
            extfield += 9;
        }

        if (entryNeedsZip64) {
            // zip64 header
            extfield[0] = 0x01;
            extfield[1] = 0x00;

            // zip64 field size
            qToLittleEndian<quint16>(24, extfield + 2);

            // uncompressed file size
            qToLittleEndian<quint64>(size, extfield + 4);

            // compressed file size
            qToLittleEndian<quint64>(compressedSize, extfield + 12);

            // relative offset of local header
            qToLittleEndian<quint64>(myhst, extfield + 20);

            // number of the disk on which this file starts
            // qToLittleEndian<quint32>(0, extfield + 28);
        }

        crc = crc32(crc, (Bytef *)buffer, bufferSize);
        bool ok = (device()->write(buffer, bufferSize) == bufferSize);
        delete[] buffer;
        if (!ok) {
            setErrorString(tr("Could not write file header: %1").arg(device()->errorString()));
            return false;
        }
    }
    qint64 centraldirendoffset = device()->pos();
    qint64 cdsize = centraldirendoffset - centraldiroffset;
    // total number of entries in central dir
    qint64 count = d->m_fileList.count();

    bool archiveNeedsZip64 = (count >= 0xFFFF || cdsize >= 0xFFFFFFFF || centraldiroffset >= 0xFFFFFFFF);

    if (archiveNeedsZip64) {
        // Write Zip64 End of Central Directory Record (PK\6\6)
        char zip64Eocd[56];
        zip64Eocd[0] = 'P';
        zip64Eocd[1] = 'K';
        zip64Eocd[2] = 6;
        zip64Eocd[3] = 6;

        // size of the EOCD64 minus 12
        quint64 eocdSize = 56 - 12;
        qToLittleEndian<quint64>(eocdSize, zip64Eocd + 4);

        // Version made by
        qToLittleEndian<quint16>(45, zip64Eocd + 12);
        // Version needed to extract
        qToLittleEndian<quint16>(45, zip64Eocd + 14);
        // Number of this disk
        qToLittleEndian<quint32>(0, zip64Eocd + 16);
        // Disk where central directory starts
        qToLittleEndian<quint32>(0, zip64Eocd + 20);
        // Number of central directory records on this disk
        qToLittleEndian<quint64>(count, zip64Eocd + 24);
        // Total number of central directory records
        qToLittleEndian<quint64>(count, zip64Eocd + 32);
        // Size of central directory in bytes
        qToLittleEndian<quint64>(cdsize, zip64Eocd + 40);
        // Offset of start of central directory, relative to start of archive
        qToLittleEndian<quint64>(centraldiroffset, zip64Eocd + 48);
        // Comment (up to the size of EOCD64)
        // no comment

        if (device()->write(zip64Eocd, 56) != 56) {
            setErrorString(tr("Could not write Zip64 EOCD record: %1").arg(device()->errorString()));
            return false;
        }

        // Write Zip64 End of Central Directory Locator (PK\6\7)
        char zip64EocdLocator[20];
        zip64EocdLocator[0] = 'P';
        zip64EocdLocator[1] = 'K';
        zip64EocdLocator[2] = 6;
        zip64EocdLocator[3] = 7;

        // Disk where EOCD64 starts
        qToLittleEndian<quint32>(0, zip64EocdLocator + 4);
        // Offset to start of EOCD64, relative to start of archive
        qToLittleEndian<quint64>(centraldirendoffset, zip64EocdLocator + 8);
        // Total number of disks
        qToLittleEndian<quint32>(1, zip64EocdLocator + 16);

        if (device()->write(zip64EocdLocator, 20) != 20) {
            setErrorString(tr("Could not write Zip64 EOCD locator: %1").arg(device()->errorString()));
            return false;
        }
    }

    // qCDebug(KArchiveLog) << "closearchive: centraldirendoffset: " << centraldirendoffset;
    // qCDebug(KArchiveLog) << "closearchive: device()->pos(): " << device()->pos();

    // write end of central dir record.
    char eocd[22];
    eocd[0] = 'P'; // end of central dir signature
    eocd[1] = 'K';
    eocd[2] = 5;
    eocd[3] = 6;

    // Number of this disk (or FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint16>(0xFFFF, eocd + 4);
    } else {
        qToLittleEndian<quint16>(0, eocd + 4);
    }
    // Disk where central directory starts (or FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint16>(0xFFFF, eocd + 6);
    } else {
        qToLittleEndian<quint16>(0, eocd + 6);
    }
    // Number of central directory records on this disk (or FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint16>(0xFFFF, eocd + 8);
    } else {
        qToLittleEndian<quint16>(count, eocd + 8);
    }
    // Total number of central directory records (or FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint16>(0xFFFF, eocd + 10);
    } else {
        qToLittleEndian<quint16>(count, eocd + 10);
    }
    // qCDebug(KArchiveLog) << "number of files (count): " << count;

    // Size of central directory in bytes (or FF FF FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint32>(0xFFFFFFFF, eocd + 12);
    } else {
        qToLittleEndian<quint32>(cdsize, eocd + 12);
    }
    // qCDebug(KArchiveLog) << "end : centraldirsize: " << cdsize;

    // Offset of start of central directory, relative to start of archive (or FF FF FF FF for ZIP64)
    if (archiveNeedsZip64) {
        qToLittleEndian<quint32>(0xFFFFFFFF, eocd + 16);
    } else {
        qToLittleEndian<quint32>(centraldiroffset, eocd + 16);
    }
    // qCDebug(KArchiveLog) << "end : centraldiroffset: " << centraldiroffset;

    // Comment length
    qToLittleEndian<quint16>(0, eocd + 20);
    // Comment (none)

    if (device()->write(eocd, 22) != 22) {
        setErrorString(tr("Could not write central dir record: %1").arg(device()->errorString()));
        return false;
    }

    return true;
}

bool KZip::doWriteDir(const QString &name,
                      const QString &user,
                      const QString &group,
                      mode_t perm,
                      const QDateTime &atime,
                      const QDateTime &mtime,
                      const QDateTime &ctime)
{
    // Zip files have no explicit directories, they are implicitly created during extraction time
    // when file entries have paths in them.
    // However, to support empty directories, we must create a dummy file entry which ends with '/'.
    QString dirName = name;
    if (!name.endsWith(QLatin1Char('/'))) {
        dirName = dirName.append(QLatin1Char('/'));
    }
    return writeFile(dirName, QByteArrayView(), perm, user, group, atime, mtime, ctime);
}

bool KZip::doPrepareWriting(const QString &name,
                            const QString &user,
                            const QString &group,
                            qint64 size,
                            mode_t perm,
                            const QDateTime &accessTime,
                            const QDateTime &modificationTime,
                            const QDateTime &creationTime)
{
    // qCDebug(KArchiveLog);
    if (!isOpen()) {
        setErrorString(tr("Application error: ZIP file must be open before being written into"));
        qCWarning(KArchiveLog) << "doPrepareWriting failed: !isOpen()";
        return false;
    }

    if (!(mode() & QIODevice::WriteOnly)) { // accept WriteOnly and ReadWrite
        setErrorString(tr("Application error: attempted to write into non-writable ZIP file"));
        qCWarning(KArchiveLog) << "doPrepareWriting failed: !(mode() & QIODevice::WriteOnly)";
        return false;
    }

    if (!device()) {
        setErrorString(tr("Cannot create a device. Disk full?"));
        return false;
    }

    // set right offset in zip.
    if (!device()->seek(d->m_offset)) {
        setErrorString(tr("Cannot seek in ZIP file. Disk full?"));
        return false;
    }

    uint atime = accessTime.toSecsSinceEpoch();
    uint mtime = modificationTime.toSecsSinceEpoch();
    uint ctime = creationTime.toSecsSinceEpoch();

    // Find or create parent dir
    KArchiveDirectory *parentDir = rootDir();
    QString fileName(name);
    int i = name.lastIndexOf(QLatin1Char('/'));
    if (i != -1) {
        QString dir = name.left(i);
        fileName = name.mid(i + 1);
        // qCDebug(KArchiveLog) << "ensuring" << dir << "exists. fileName=" << fileName;
        parentDir = findOrCreate(dir);
    }

    // delete entries in the filelist with the same fileName as the one we want
    // to save, so that we don't have duplicate file entries when viewing the zip
    // with konqi...
    // CAUTION: the old file itself is still in the zip and won't be removed !!!
    // qCDebug(KArchiveLog) << "fileName to write: " << name;
    for (auto it = d->m_fileList.begin(); it != d->m_fileList.end();) {
        // qCDebug(KArchiveLog) << "prepfileName: " << entry->path();
        if (name == (*it)->path()) {
            // also remove from the parentDir
            if (!parentDir->removeEntryV2(*it)) {
                return false;
            }
            // qCDebug(KArchiveLog) << "removing following entry: " << entry->path();
            delete *it;
            it = d->m_fileList.erase(it);
        } else {
            it++;
        }
    }

    bool isZip64Archive = size >= 0xFFFFFFFF;

    int extra_field_len = 0;
    if (d->m_extraField == ModificationTime) {
        extra_field_len = 17; // value also used in finishWriting()
    }
    // if isZip64Archive is true we write both sizes to the zip64 field
    if (isZip64Archive) {
        // 2 header id + 2 field size +
        // 8 uncompressed size + 8 compressed size
        extra_field_len += 20;
    }

    QByteArray encodedName = QFile::encodeName(name);

    // construct a KZipFileEntry and add it to list
    KZipFileEntry *e = new KZipFileEntry(this,
                                         fileName,
                                         perm,
                                         modificationTime,
                                         user,
                                         group,
                                         QString(),
                                         name,
                                         device()->pos() + 30 + encodedName.length() + extra_field_len, // start
                                         0 /*size unknown yet*/,
                                         d->m_compression,
                                         0 /*csize unknown yet*/);
    e->setHeaderStart(device()->pos());
    // qCDebug(KArchiveLog) << "wrote file start: " << e->position() << " name: " << name;
    if (!parentDir->addEntryV2(e)) {
        return false;
    }

    d->m_currentFile = e;
    d->m_fileList.append(e);

    // write out zip header
    int bufferSize = extra_field_len + encodedName.length() + 30;
    // qCDebug(KArchiveLog) << "bufferSize=" << bufferSize;
    char *buffer = new char[bufferSize];

    buffer[0] = 'P'; // local file header signature
    buffer[1] = 'K';
    buffer[2] = 3;
    buffer[3] = 4;

    // version needed to extract
    qToLittleEndian<quint16>(isZip64Archive ? 45 : 0x14, buffer + 4);

    // general purpose bit flag
    qToLittleEndian<quint16>(0, buffer + 6);

    // compression method
    qToLittleEndian<quint16>(char(e->encoding()), buffer + 8);

    transformToMsDos(e->date(), &buffer[10]);

    buffer[14] = 'C'; // dummy crc
    buffer[15] = 'R';
    buffer[16] = 'C';
    buffer[17] = 'q';

    buffer[18] = 'C'; // compressed file size
    buffer[19] = 'S';
    buffer[20] = 'I';
    buffer[21] = 'Z';

    buffer[22] = 'U'; // uncompressed file size
    buffer[23] = 'S';
    buffer[24] = 'I';
    buffer[25] = 'Z';

    // fileName length
    qToLittleEndian<quint16>(encodedName.length(), buffer + 26);

    // extra field length
    qToLittleEndian<quint16>(extra_field_len, buffer + 28);

    // file name
    strncpy(buffer + 30, encodedName.constData(), encodedName.length());

    // extra field
    char *extfield = buffer + 30 + encodedName.length();
    if (d->m_extraField == ModificationTime) {
        // "Extended timestamp" header (0x5455)
        extfield[0] = 'U';
        extfield[1] = 'T';
        extfield[2] = 13; // data size
        extfield[3] = 0;
        extfield[4] = 1 | 2 | 4; // contains mtime, atime, ctime

        qToLittleEndian<quint32>(mtime, extfield + 5);
        qToLittleEndian<quint32>(atime, extfield + 9);
        qToLittleEndian<quint32>(ctime, extfield + 13);
        extfield += 17;
    }

    if (isZip64Archive) {
        // zip64 header
        extfield[0] = 0x01;
        extfield[1] = 0x00;
        // size of uncompressed and compressed fields
        qToLittleEndian<quint16>(16, extfield + 2);
        // uncompressed size
        qToLittleEndian<quint64>(size, extfield + 4);
        // compressed size
        qToLittleEndian<quint64>(0, extfield + 12);
    }

    // Write header
    bool b = (device()->write(buffer, bufferSize) == bufferSize);
    d->m_crc = 0;
    delete[] buffer;

    if (!b) {
        setErrorString(tr("Could not write to the archive. Disk full?"));
        return false;
    }

    // Prepare device for writing the data
    // Either device() if no compression, or a KCompressionDevice to compress
    if (d->m_compression == 0) {
        d->m_currentDev = device();
        return true;
    }

    auto compressionDevice = new KCompressionDevice(device(), false, KCompressionDevice::GZip);
    d->m_currentDev = compressionDevice;
    compressionDevice->setSkipHeaders(); // Just zlib, not gzip

    b = d->m_currentDev->open(QIODevice::WriteOnly);
    Q_ASSERT(b);

    if (!b) {
        setErrorString(tr("Could not open compression device: %1").arg(d->m_currentDev->errorString()));
    }

    return b;
}

bool KZip::doFinishWriting(qint64 size)
{
    if (d->m_currentFile->encoding() == 8) {
        // Finish
        (void)d->m_currentDev->write(nullptr, 0);
        delete d->m_currentDev;
    }
    // If 0, d->m_currentDev was device() - don't delete ;)
    d->m_currentDev = nullptr;

    Q_ASSERT(d->m_currentFile);
    // qCDebug(KArchiveLog) << "fileName: " << d->m_currentFile->path();
    // qCDebug(KArchiveLog) << "getpos (at): " << device()->pos();
    d->m_currentFile->setSize(size);

    const QByteArray encodedName = QFile::encodeName(d->m_currentFile->path());
    qint64 csize = device()->pos() - d->m_currentFile->position();
    d->m_currentFile->setCompressedSize(csize);
    // qCDebug(KArchiveLog) << "usize: " << d->m_currentFile->size();
    // qCDebug(KArchiveLog) << "csize: " << d->m_currentFile->compressedSize();
    // qCDebug(KArchiveLog) << "headerstart: " << d->m_currentFile->headerStart();

    // qCDebug(KArchiveLog) << "crc: " << d->m_crc;
    d->m_currentFile->setCRC32(d->m_crc);

    d->m_currentFile = nullptr;

    // update saved offset for appending new files
    d->m_offset = device()->pos();
    return true;
}

bool KZip::doWriteSymLink(const QString &name,
                          const QString &target,
                          const QString &user,
                          const QString &group,
                          mode_t perm,
                          const QDateTime &atime,
                          const QDateTime &mtime,
                          const QDateTime &ctime)
{
    // reassure that symlink flag is set, otherwise strange things happen on
    // extraction
    perm |= QT_STAT_LNK;
    Compression c = compression();
    setCompression(NoCompression); // link targets are never compressed

    if (!doPrepareWriting(name, user, group, 0, perm, atime, mtime, ctime)) {
        setCompression(c);
        return false;
    }

    QByteArray symlink_target = QFile::encodeName(target);
    if (!writeData(symlink_target.constData(), symlink_target.length())) {
        setCompression(c);
        return false;
    }

    if (!finishWriting(symlink_target.length())) {
        setCompression(c);
        return false;
    }

    setCompression(c);
    return true;
}

void KZip::virtual_hook(int id, void *data)
{
    KArchive::virtual_hook(id, data);
}

bool KZip::doWriteData(const char *data, qint64 size)
{
    Q_ASSERT(d->m_currentFile);
    Q_ASSERT(d->m_currentDev);
    if (!d->m_currentFile || !d->m_currentDev) {
        setErrorString(tr("No file or device"));
        return false;
    }

    // crc to be calculated over uncompressed stuff...
    // and they didn't mention it in their docs...
    d->m_crc = crc32(d->m_crc, (const Bytef *)data, size);

    qint64 written = d->m_currentDev->write(data, size);
    // qCDebug(KArchiveLog) << "wrote" << size << "bytes.";
    const bool ok = written == size;

    if (!ok) {
        setErrorString(tr("Error writing data: %1").arg(d->m_currentDev->errorString()));
    }

    return ok;
}

void KZip::setCompression(Compression c)
{
    d->m_compression = (c == NoCompression) ? 0 : 8;
}

KZip::Compression KZip::compression() const
{
    return (d->m_compression == 8) ? DeflateCompression : NoCompression;
}

void KZip::setExtraField(ExtraField ef)
{
    d->m_extraField = ef;
}

KZip::ExtraField KZip::extraField() const
{
    return d->m_extraField;
}

////////////////////////////////////////////////////////////////////////
////////////////////// KZipFileEntry////////////////////////////////////
////////////////////////////////////////////////////////////////////////
class Q_DECL_HIDDEN KZipFileEntry::KZipFileEntryPrivate
{
public:
    KZipFileEntryPrivate()
        : crc(0)
        , compressedSize(0)
        , headerStart(0)
        , encoding(0)
    {
    }
    unsigned long crc;
    qint64 compressedSize;
    qint64 headerStart;
    int encoding;
    QString path;
};

KZipFileEntry::KZipFileEntry(KZip *zip,
                             const QString &name,
                             int access,
                             const QDateTime &date,
                             const QString &user,
                             const QString &group,
                             const QString &symlink,
                             const QString &path,
                             qint64 start,
                             qint64 uncompressedSize,
                             int encoding,
                             qint64 compressedSize)
    : KArchiveFile(zip, name, access, date, user, group, symlink, start, uncompressedSize)
    , d(new KZipFileEntryPrivate)
{
    d->path = path;
    d->encoding = encoding;
    d->compressedSize = compressedSize;
}

KZipFileEntry::~KZipFileEntry()
{
    delete d;
}

int KZipFileEntry::encoding() const
{
    return d->encoding;
}

qint64 KZipFileEntry::compressedSize() const
{
    return d->compressedSize;
}

void KZipFileEntry::setCompressedSize(qint64 compressedSize)
{
    d->compressedSize = compressedSize;
}

void KZipFileEntry::setHeaderStart(qint64 headerstart)
{
    d->headerStart = headerstart;
}

qint64 KZipFileEntry::headerStart() const
{
    return d->headerStart;
}

unsigned long KZipFileEntry::crc32() const
{
    return d->crc;
}

void KZipFileEntry::setCRC32(unsigned long crc32)
{
    d->crc = crc32;
}

const QString &KZipFileEntry::path() const
{
    return d->path;
}

QByteArray KZipFileEntry::data() const
{
    QIODevice *dev = createDevice();
    QByteArray arr;
    if (dev) {
        const qint64 devSize = dev->size();
        if (devSize > kMaxQByteArraySize) {
            qCWarning(KArchiveLog) << "KZipFileEntry::data: Failed to allocate memory for file of size" << devSize;
            delete dev;
            return {};
        }
        arr = dev->readAll();
        delete dev;
    }
    return arr;
}

QIODevice *KZipFileEntry::createDevice() const
{
    if (compressedSize() < 0) {
        qCWarning(KArchiveLog) << "KZipFileEntry::createDevice: Entry with negative size" << path() << compressedSize();
        return nullptr;
    }
    // qCDebug(KArchiveLog) << "creating iodevice limited to pos=" << position() << ", csize=" << compressedSize();
    // Limit the reading to the appropriate part of the underlying device (e.g. file)
    std::unique_ptr limitedDev = std::make_unique<KLimitedIODevice>(archive()->device(), position(), compressedSize());
    if (encoding() == 0 || compressedSize() == 0) { // no compression (or even no data)
        return limitedDev.release();
    }

    if (encoding() == 8) {
        // On top of that, create a device that uncompresses the zlib data
        KCompressionDevice *filterDev = new KCompressionDevice(std::move(limitedDev), KCompressionDevice::GZip, size());

        if (!filterDev) {
            return nullptr; // ouch
        }
        filterDev->setSkipHeaders(); // Just zlib, not gzip
        bool b = filterDev->open(QIODevice::ReadOnly);
        Q_UNUSED(b);
        Q_ASSERT(b);
        return filterDev;
    }

    qCCritical(KArchiveLog) << "This zip file contains files compressed with method" << encoding() << ", this method is currently not supported by KZip,"
                            << "please use a command-line tool to handle this file.";
    return nullptr;
}
