#ifndef KARCHIVE_P_H
#define KARCHIVE_P_H

#include "karchive.h"

#include <qsavefile.h>

class KArchivePrivate
{
public:
    KArchivePrivate()
        : rootDir(0),
          saveFile(0),
          dev(0),
          fileName(),
          mode(QIODevice::NotOpen),
          deviceOwned(false)
    {}
    ~KArchivePrivate()
    {
        delete saveFile;
        delete rootDir;
    }
    void abortWriting();

    static QDateTime time_tToDateTime(uint time_t);

    KArchiveDirectory *rootDir;
    QSaveFile *saveFile;
    QIODevice *dev;
    QString fileName;
    QIODevice::OpenMode mode;
    bool deviceOwned; // if true, we (KArchive) own dev and must delete it
};

#endif // KARCHIVE_P_H
