/*
    Copyright (C) 2007 by Nikolay Nikolov <nknikolov@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __INC_CDGFILE_H__
#define __INC_CDGFILE_H__

#include "cdgio.h"

// This is the size of the display as defined by the CDG specification.
// The pixels in this region can be painted, and scrolling operations
// rotate through this number of pixels.
#define CDG_FULL_WIDTH              300
#define CDG_FULL_HEIGHT             216

// This is the size of the screen that is actually intended to be
// visible.  It is the center area of CDG_FULL.  
#define CDG_DISPLAY_WIDTH           294
#define CDG_DISPLAY_HEIGHT          204

#define COLOUR_TABLE_SIZE           16

class ISurface
{
public:
    ISurface() {}
    virtual ~ISurface() {}
public:
    virtual unsigned long MapRGBColour(int red, int green, int blue) = 0;

public:
     // This is an array of the actual RGB values.  
    unsigned long rgbData[CDG_FULL_HEIGHT][CDG_FULL_WIDTH];
};

class CDGFile
{
protected:
    // This struct just represents a single 24-byte packet
    // read from the CDG stream.  
    typedef struct {
        unsigned char command;
        unsigned char instruction;
        unsigned char parityQ[2];
        unsigned char data[16];
        unsigned char parityP[4];
    } CdgPacket;

public:
    CDGFile();
    virtual ~CDGFile();

    bool open(CdgIoStream* pStream, ISurface* pSurface);
    void close();

    bool renderAtPosition(long ms);
    long getTotalDuration() { return m_duration; }

protected:
    bool readPacket(CdgPacket& pack);
    void processPacket(const CdgPacket *packd);
    void render();
    void reset();

    void memoryPreset(const CdgPacket *pack);
    void borderPreset(const CdgPacket *pack);
    void loadColorTable(const CdgPacket *pack, int table);
    void tileBlock(const CdgPacket *pack, bool bXor); 
    void defineTransparentColour(const CdgPacket *pack);
    void scroll(const CdgPacket *pack, bool copy);

protected:
    unsigned char m_pixelColours[CDG_FULL_HEIGHT][CDG_FULL_WIDTH];
    int m_colourTable[COLOUR_TABLE_SIZE];
    int m_presetColourIndex;
    int m_borderColourIndex;
    int m_transparentColour;

    int m_hOffset;
    int m_vOffset;

    CdgIoStream* m_pStream;
    ISurface* m_pSurface;
    long m_positionMs;
    long m_duration;
};

#endif // __INC_CDGFILE_H__
