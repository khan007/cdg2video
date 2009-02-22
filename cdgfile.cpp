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

#include <sys/stat.h>
#include <string.h>
#include "cdgfile.h"

// CDG Command Code
#define CDG_COMMAND                 0x09

// CDG Instruction Codes
#define CDG_INST_MEMORY_PRESET      1
#define CDG_INST_BORDER_PRESET      2
#define CDG_INST_TILE_BLOCK         6
#define CDG_INST_SCROLL_PRESET      20
#define CDG_INST_SCROLL_COPY        24
#define CDG_INST_DEF_TRANSP_COL     28
#define CDG_INST_LOAD_COL_TBL_LO    30
#define CDG_INST_LOAD_COL_TBL_HIGH  31
#define CDG_INST_TILE_BLOCK_XOR     38

// Bitmask for all CDG fields
#define CDG_MASK                    0x3F
#define CDG_PACKET_SIZE             24
#define TILE_HEIGHT                 12
#define TILE_WIDTH                  6

CDGFile::CDGFile()
{
    m_pSurface = NULL;
}

CDGFile::~CDGFile()
{
    close();
}

// Open CDG file

bool CDGFile::open(const char* file)
{
    close();    
    if (m_pSurface == NULL) return false; // set surface before to call open

    m_file.open(file, ios::binary|ios::in);
    m_positionMs = 0;

    struct stat results;
    if (stat(file, &results) == 0)
        m_duration = ((results.st_size / CDG_PACKET_SIZE) * 1000) / 300;
    else
        m_duration = 0; // An error occurred

    return m_file.is_open();
}

// Close currently open file

void CDGFile::close()
{
    if (m_file.is_open()) 
    {
        m_file.close();
    }

    reset();
}

// Reinitialize local members

void CDGFile::reset()
{
    memset(m_pixelColours, 0, CDG_FULL_WIDTH*CDG_FULL_HEIGHT*sizeof(char));
    memset(m_colourTable,  0, COLOUR_TABLE_SIZE*sizeof(int));

    m_presetColourIndex = 0;
    m_borderColourIndex = 0;
    m_transparentColour = 0;
    m_hOffset = 0;
    m_vOffset = 0;

    m_duration = 0;
    m_positionMs = 0;

    // clear surface 
    if (m_pSurface)
    {
        memset(m_pSurface->rgbData, 0, 
                    CDG_FULL_WIDTH*CDG_FULL_HEIGHT*sizeof(long));
    }
}

// Render a frame at specific position within the CDG file
// Parameters:
//  int ms - position in miliseconds
// Return:
//  true  - if the frame was rendered successfully
//  false - if the end of the file is reached, the file is not opened or surface is not set

bool CDGFile::renderAtPosition(long ms)
{
    CdgPacket pack;
    long numPacks = 0;
    bool res = true;

    if (!m_file.is_open()) return false;

    if (ms < m_positionMs)
    {
        m_file.clear(); // clear eof flag
        m_file.seekg(0, std::ios_base::beg);
        m_positionMs = 0;
    }

    // duration of one packet is 1/300 seconds (4 packets per sector, 75 sectors per second)

    numPacks = ms - m_positionMs;
    numPacks /= 10;

    m_positionMs += numPacks * 10;
    numPacks *= 3;

    while (numPacks-- > 0 && (res = readPacket(pack)) && m_pSurface)
    {
        processPacket(&pack);
    }

    render();
    return res;
}

bool CDGFile::readPacket(CdgPacket& pack)
{
    if (!m_file.is_open() || m_file.eof())
    {
        return false;
    }

    m_file.read((char*)&pack.command, 1);
    m_file.read((char*)&pack.instruction, 1);
    m_file.read((char*)pack.parityQ, 2);
    m_file.read((char*)pack.data, 16);
    m_file.read((char*)pack.parityP, 4);

    return true;
}

void CDGFile::processPacket(const CdgPacket *pack) 
{
    int inst_code;

    if ((pack->command & CDG_MASK) == CDG_COMMAND) 
    {
        inst_code = (pack->instruction & CDG_MASK);
        switch (inst_code) 
        {
        case CDG_INST_MEMORY_PRESET:
            memoryPreset(pack);
            break;

        case CDG_INST_BORDER_PRESET:
            borderPreset(pack);
            break;
      
        case CDG_INST_TILE_BLOCK:
            tileBlock(pack, false);
            break;

        case CDG_INST_SCROLL_PRESET:
            scroll(pack, false);
            break;

        case CDG_INST_SCROLL_COPY:
            scroll(pack, true);
            break;

        case CDG_INST_DEF_TRANSP_COL:
            defineTransparentColour(pack);
            break;

        case CDG_INST_LOAD_COL_TBL_LO:
            loadColorTable(pack, 0);
            break;

        case CDG_INST_LOAD_COL_TBL_HIGH:
            loadColorTable(pack, 1);
            break;

        case CDG_INST_TILE_BLOCK_XOR:
            tileBlock(pack, true);
            break;

        default:
            // Ignore the unsupported commands
            break;
        }
    }
}

void CDGFile::memoryPreset(const CdgPacket *pack) 
{
    int colour;
    int ri, ci;
    int repeat;

    colour = pack->data[0] & 0x0F;
    repeat = pack->data[1] & 0x0F; 
  
    // Our new interpretation of CD+G Revealed is that memory preset
    // commands should also change the border
    m_presetColourIndex = colour;
    m_borderColourIndex = colour;

    // we have a reliable data stream, so the repeat command 
    // is executed only the first time

    if (repeat == 0)
    {
        // Note that this may be done before any load colour table
        // commands by some CDGs. So the load colour table itself
        // actual recalculates the RGB values for all pixels when
        // the colour table changes.
    
        // Set the preset colour for every pixel. Must be stored in 
        // the pixel colour table indeces array
    
        for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) 
        {
            for (ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
            {
                m_pixelColours[ri][ci] = colour;
            }
        }
    }
}

void CDGFile::borderPreset(const CdgPacket *pack) 
{
    int colour;
    int ri, ci;

    colour = pack->data[0] & 0x0F;
    m_borderColourIndex = colour;

    // The border area is the area contained with a rectangle 
    // defined by (0,0,300,216) minus the interior pixels which are contained
    // within a rectangle defined by (6,12,294,204).

    for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) 
    {
        for (ci = 0; ci < 6; ++ci) {
            m_pixelColours[ri][ci] = colour;
        }

        for (ci = CDG_FULL_WIDTH - 6; ci < CDG_FULL_WIDTH; ++ci) {
            m_pixelColours[ri][ci] = colour;
        }
    }

    for (ci = 6; ci < CDG_FULL_WIDTH - 6; ++ci) 
    {
        for (ri = 0; ri < 12; ++ri) {
            m_pixelColours[ri][ci] = colour;
        }
    
        for (ri = CDG_FULL_HEIGHT - 12; ri < CDG_FULL_HEIGHT; ++ri) {
            m_pixelColours[ri][ci] = colour;
        }
    }
}

void CDGFile::loadColorTable(const CdgPacket *pack, int table)
{
    unsigned short red, green, blue;
    unsigned short colour;

    for (int i = 0; i < 8; ++i) 
    {
        // [---high byte---]   [---low byte----]
        // 7 6 5 4 3 2 1 0     7 6 5 4 3 2 1 0
        // X X r r r r g g     X X g g b b b b

        colour = (pack->data[2*i] << 6) + (pack->data[2*i + 1] & 0x3F);

        red   = (colour >> 8)  & 0x000F;
        green = (colour >> 4)  & 0x000F;
        blue  = (colour     )  & 0x000F;

        red   *= 17;
        green *= 17;
        blue  *= 17;

        if (m_pSurface)
        {
            m_colourTable[i + table*8] = 
                        m_pSurface->MapRGBColour(red, green, blue);
        }
    }
}

void CDGFile::tileBlock(const CdgPacket *packd, bool bXor) 
{
    int colour0, colour1;
    int column_index, row_index;
    int byte, pixel, xor_col, currentColourIndex, new_col;

    colour0 = packd->data[0] & 0x0f;
    colour1 = packd->data[1] & 0x0f;
    row_index = ((packd->data[2] & 0x1f) * 12);
    column_index = ((packd->data[3] & 0x3f) * 6);

    if (row_index > (CDG_FULL_HEIGHT - TILE_HEIGHT)) return;
    if (column_index > (CDG_FULL_WIDTH - TILE_WIDTH)) return;

    //  Set the pixel array for each of the pixels in the 12x6 tile.
    //  Normal = Set the colour to either colour0 or colour1 depending
    //  on whether the pixel value is 0 or 1.
    //  XOR = XOR the colour with the colour index currently there.

    for (int i = 0; i < 12; ++i) 
    {
        byte = (packd->data[4 + i] & 0x3F);
        for (int j = 0; j < 6; ++j) 
        {
            pixel = (byte >> (5 - j)) & 0x01;
            if (bXor) 
            {
                // Tile Block XOR 
                if (pixel == 0) 
                {
                    xor_col = colour0;
                } 
                else 
                {
                    xor_col = colour1;
                }
        
                // Get the colour index currently at this location, and xor with it 
                currentColourIndex = m_pixelColours[row_index + i][column_index + j];
                new_col = currentColourIndex ^ xor_col;
            } 
            else 
            {
                if (pixel == 0) 
                {
                    new_col = colour0;
                } 
                else 
                {
                    new_col = colour1;
                }
            }

            // Set the pixel with the new colour. We set both the surfarray
            // containing actual RGB values, as well as our array containing
            // the colour indexes into our colour table. 
            m_pixelColours[row_index + i][column_index + j] = new_col;      
        }
    }
}

void CDGFile::defineTransparentColour(const CdgPacket *pack) 
{
    m_transparentColour = pack->data[0] & 0x0F;
}

void CDGFile::scroll(const CdgPacket *pack, bool copy)
{
    int colour, hScroll, vScroll;
    int hSCmd, hOffset, vSCmd, vOffset;
    int vScrollPixels, hScrollPixels;
    
    // Decode the scroll command parameters
    colour  = pack->data[0] & 0x0F;
    hScroll = pack->data[1] & 0x3F;
    vScroll = pack->data[2] & 0x3F;

    hSCmd = (hScroll & 0x30) >> 4;
    hOffset = (hScroll & 0x07);
    vSCmd = (vScroll & 0x30) >> 4;
    vOffset = (vScroll & 0x0F);

    m_hOffset = hOffset < 5 ? hOffset : 5;
    m_vOffset = vOffset < 11 ? vOffset : 11;

    // Scroll Vertical - Calculate number of pixels

    vScrollPixels = 0;
    if (vSCmd == 2) 
    {
        vScrollPixels = - 12;
    } 
    else 
    if (vSCmd == 1) 
    {
        vScrollPixels = 12;
    }

    // Scroll Horizontal- Calculate number of pixels

    hScrollPixels = 0;
    if (hSCmd == 2) 
    {
        hScrollPixels = - 6;
    } 
    else 
    if (hSCmd == 1) 
    {
        hScrollPixels = 6;
    }

    if (hScrollPixels == 0 && vScrollPixels == 0) 
    {
        return;
    }

    // Perform the actual scroll.

    unsigned char temp[CDG_FULL_HEIGHT][CDG_FULL_WIDTH];
    int vInc = vScrollPixels + CDG_FULL_HEIGHT;
    int hInc = hScrollPixels + CDG_FULL_WIDTH;
    int ri; // row index
    int ci; // column index

    for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) 
    {
        for (ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
        {   
            temp[(ri + vInc) % CDG_FULL_HEIGHT][(ci + hInc) % CDG_FULL_WIDTH] = 
                    m_pixelColours[ri][ci];
        }
    }

    // if copy is false, we were supposed to fill in the new pixels
    // with a new colour. Go back and do that now.

    if (!copy)
    {
        if (vScrollPixels > 0) 
        {
            for (ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
            {
                for (ri = 0; ri < vScrollPixels; ++ri) {
                    temp[ri][ci] = colour;
                }
            }
        }
        else if (vScrollPixels < 0) 
        {
            for (ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
            {
                for (ri = CDG_FULL_HEIGHT + vScrollPixels; ri < CDG_FULL_HEIGHT; ++ri) {
                    temp[ri][ci] = colour;
                }
            }
        }
        
        if (hScrollPixels > 0) 
        {
            for (ci = 0; ci < hScrollPixels; ++ci) 
            {
                for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) {
                    temp[ri][ci] = colour;
                }
            }
        } 
        else if (hScrollPixels < 0) 
        {
            for (ci = CDG_FULL_WIDTH + hScrollPixels; ci < CDG_FULL_WIDTH; ++ci) 
            {
                for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) {
                    temp[ri][ci] = colour;
                }
            }
        }
    }

    // Now copy the temporary buffer back to our array

    for (ri = 0; ri < CDG_FULL_HEIGHT; ++ri) 
    {
        for (ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
        {
            m_pixelColours[ri][ci] = temp[ri][ci];
        }
    }
}

void CDGFile::render()
{
    if (m_pSurface == NULL) return;

    for (int ri = 0; ri < CDG_FULL_HEIGHT; ++ri) 
    {
        for (int ci = 0; ci < CDG_FULL_WIDTH; ++ci) 
        {
            if (ri < TILE_HEIGHT || ri >= CDG_FULL_HEIGHT-TILE_HEIGHT ||
                ci < TILE_WIDTH  || ci >= CDG_FULL_WIDTH-TILE_WIDTH)
            {
                m_pSurface->rgbData[ri][ci] = m_colourTable[m_borderColourIndex];
            }
            else
            {
                m_pSurface->rgbData[ri][ci] = 
                    m_colourTable[m_pixelColours[ri+m_vOffset][ci+m_hOffset]];
            }
        }
    }
}

