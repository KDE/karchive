/* This file is part of the KDE libraries
   Copyright (C) 2000 David Faure <faure@kde.org>

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

#ifndef __kbzip2filter__h
#define __kbzip2filter__h

#include <config-compression.h>

#if HAVE_BZIP2_SUPPORT

#include "kfilterbase.h"

/**
 * Internal class used by KFilterDev
 * @internal
 */
class KBzip2Filter : public KFilterBase
{
public:
    KBzip2Filter();
    virtual ~KBzip2Filter();

    virtual bool init(int);
    virtual int mode() const;
    virtual bool terminate();
    virtual void reset();
    virtual bool readHeader() { return true; } // bzip2 handles it by itself ! Cool !
    virtual bool writeHeader( const QByteArray & ) { return true; }
    virtual void setOutBuffer( char * data, uint maxlen );
    virtual void setInBuffer( const char * data, uint size );
    virtual int  inBufferAvailable() const;
    virtual int  outBufferAvailable() const;
    virtual Result uncompress();
    virtual Result compress( bool finish );
private:
    class Private;
    Private* const d;
};

#endif

#endif
