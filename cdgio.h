/*
    Copyright (C) 2009 by Nikolay Nikolov <nknikolov@gmail.com>

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

#ifndef __INC_CDGIO_H__
#define __INC_CDGIO_H__

#include <inttypes.h>
#include <stdio.h>

class CdgIoStream
{
public:
      virtual int read(void *buf, int buf_size) = 0;
      virtual int write(const void *buf, int buf_size) = 0;
      virtual int seek(int offset, int whence) = 0;
      virtual int eof() = 0;
      virtual int getsize() = 0;
};

class CdgFileIoStream : public CdgIoStream
{
public:
      CdgFileIoStream();
      virtual ~CdgFileIoStream();
      bool open(const char* file, const char* mode);
      void close();

      virtual int read(void *buf, int buf_size);
      virtual int write(const void *buf, int buf_size);
      virtual int seek(int offset, int whence);
      virtual int eof();
      virtual int getsize();
      
protected:
      FILE*  m_file;
};

class CdgZipFileIoStream : public CdgIoStream
{
public:
      CdgZipFileIoStream();
      virtual ~CdgZipFileIoStream();
  
      virtual int read(void *buf, int buf_size);
      virtual int write(const void *buf, int buf_size);
      virtual int seek(int offset, int whence);
};

int cdgio_read_packet(void *opaque, uint8_t *buf, int buf_size);
int cdgio_write_packet(void *opaque, uint8_t *buf, int buf_size);
int64_t cdgio_seek(void *opaque, int64_t offset, int whence);

#endif
