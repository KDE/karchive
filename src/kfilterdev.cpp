/* This file is part of the KDE libraries
   SPDX-FileCopyrightText: 2000, 2006 David Faure <faure@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "kfilterdev.h"

#if KARCHIVE_BUILD_DEPRECATED_SINCE(5, 85)

KFilterDev::KFilterDev(const QString &fileName)
    : KCompressionDevice(fileName)
{
}

KCompressionDevice::CompressionType KFilterDev::compressionTypeForMimeType(const QString &mimeType)
{
    return KCompressionDevice::compressionTypeForMimeType(mimeType);
}

#endif // KARCHIVE_BUILD_DEPRECATED_SINCE(5, 85)
