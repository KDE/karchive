/* This file is part of the KDE project
   Copyright (C) 2002-2019 David Faure <faure@kde.org>

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

#include "kar.h"
#include <stdio.h>
#include <QDebug>

void recursive_print(const KArchiveDirectory *dir, const QString &path)
{
    QStringList l = dir->entries();
    l.sort();
    QStringList::ConstIterator it = l.constBegin();
    for (; it != l.constEnd(); ++it) {
        const KArchiveEntry *entry = dir->entry((*it));
        printf("mode=%07o %s %s %s%s %lld isdir=%d\n", entry->permissions(), entry->user().toLatin1().constData(), entry->group().toLatin1().constData(), path.toLatin1().constData(), (*it).toLatin1().constData(),
               entry->isFile() ? static_cast<const KArchiveFile *>(entry)->size() : 0,
               entry->isDirectory());
        if (!entry->symLinkTarget().isEmpty()) {
            printf("  (symlink to %s)\n", qPrintable(entry->symLinkTarget()));
        }
        if (entry->isDirectory()) {
            recursive_print((KArchiveDirectory *)entry, path + (*it) + '/');
        }
    }
}

// See karchivetest.cpp for the unittest that covers KAr.

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("\n"
               " Usage :\n"
               " ./kartest /path/to/existing_file.a       tests listing an existing archive\n");
        return 1;
    }

    KAr archive(argv[1]);

    if (!archive.open(QIODevice::ReadOnly)) {
        printf("Could not open %s for reading\n", argv[1]);
        return 1;
    }

    const KArchiveDirectory *dir = archive.directory();

    //printf("calling recursive_print\n");
    recursive_print(dir, QLatin1String(""));
    //printf("recursive_print called\n");

    archive.close();

    return 0;
}

