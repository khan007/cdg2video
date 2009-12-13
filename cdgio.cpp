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

#include <sys/stat.h>
#include "cdgio.h"

int cdgio_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    CdgIoStream* pStream = (CdgIoStream*)opaque;
    return pStream->read(buf, buf_size);
}

int cdgio_write_packet(void *opaque, uint8_t *buf, int buf_size)
{
    CdgIoStream* pStream = (CdgIoStream*)opaque;
    return pStream->write(buf, buf_size);
}

int64_t cdgio_seek(void *opaque, int64_t offset, int whence)
{
    CdgIoStream* pStream = (CdgIoStream*)opaque;
    return pStream->seek((int)offset, whence);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CdgFileIoStream::CdgFileIoStream()
{
    m_file = NULL;
}

CdgFileIoStream::~CdgFileIoStream()
{
    close();
}

bool CdgFileIoStream::open(const char* file, const char* mode)
{
    close();
    m_file = fopen(file, mode);
    return (m_file != NULL);
}

void CdgFileIoStream::close()
{
    if (m_file) 
    {
        fclose(m_file);
        m_file = NULL;
    }
}

int CdgFileIoStream::read(void *buf, int buf_size)
{
    return fread(buf, 1, buf_size, m_file);
}

int CdgFileIoStream::write(const void *buf, int buf_size)
{
    return fwrite(buf, 1, buf_size, m_file);
}

int CdgFileIoStream::seek(int offset, int whence)
{
    return fseek(m_file, offset, whence);
}

int CdgFileIoStream::eof()
{
    return feof(m_file);
}

int CdgFileIoStream::getsize()
{
    struct stat results;
    
    if (fstat(fileno(m_file), &results) == 0)
    {
        return results.st_size;
    }
    
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CdgZipFileIoStream::CdgZipFileIoStream()
{
    m_file = NULL;
    m_fileidx = -1;
    m_filesize = 0;
}

CdgZipFileIoStream::~CdgZipFileIoStream()
{
    close();
}

bool CdgZipFileIoStream::open(struct zip *archive, const char *fname)
{
    close();
    
    if (archive == NULL || fname == NULL)
    {
        return false;
    }
    
    struct zip_stat zs;
    if (zip_stat(archive, fname, 0, &zs)) 
    {
        return false;
    }

    m_fileidx = zs.index;
    m_filesize = zs.size;

    m_file = zip_fopen_index(archive, m_fileidx, 0);
    return m_file != NULL;
}

void CdgZipFileIoStream::close()
{
    if (m_file)
    {
        zip_fclose(m_file);
    }
    
    m_file = NULL;
    m_fileidx = -1;
    m_filesize = 0;
}

int CdgZipFileIoStream::read(void *buf, int buf_size)
{
    return zip_fread(m_file, buf, buf_size);
}

int CdgZipFileIoStream::write(const void *buf, int buf_size)
{
    return 0;
}

int CdgZipFileIoStream::seek(int offset, int whence)
{
    return -1;
}

int CdgZipFileIoStream::eof()
{
    int ze;
    zip_file_error_get(m_file, &ze, NULL);
    
    return ze == ZIP_ER_EOF;
}

int CdgZipFileIoStream::getsize()
{
    return m_filesize;
}

