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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define __STDC_CONSTANT_MACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "ffmpeg_headers.h"
#include "utils.h"

#define MAX_FRAME_RATE_BASE 1001000	// maximum allowed frame rate numerator and denominator 

extern const tVideoFrameRateAbbr frame_rate_abbrs[]= 
{
    { "ntsc",      30000, 1001 },
    { "pal",          25,    1 },
    { "film",         24,    1 },
    { "ntsc-film", 24000, 1001 },
    { NULL,            0,    0 }
};

extern const tVideoFrameSizeAbbr frame_size_abbrs[]= 
{
    { "dvd-ntsc",      720, 480 },
    { "dvd-pal",       720, 576 },
    { "svcd-ntsc",     480, 480 },
    { "svcd-pal",      480, 576 },
    { "vcd-ntsc",      352, 240 },
    { "vcd-pal",       352, 288 },
    { NULL,            0,   0   }
};

static const char* audio_files[] = {
    "mp3",  "MP3",  "Mp3",
    "ogg",  "OGG",  "Ogg",
    "flac", "FLAC", "Flac",
    NULL
};

char* get_audio_filename(const char* cdgfile)
{
    char* p;
    char* audiofile = NULL;

    audiofile = (char*)malloc(strlen(cdgfile) + 8);
    strcpy(audiofile, cdgfile);
    p = strrchr(audiofile, '.');

    if (p == NULL) {
        free(audiofile);
        return NULL;
    }

    p++;

    for (int i = 0; audio_files[i] != NULL; i++)
    {
        strcpy(p, audio_files[i]);
        if (access(audiofile, R_OK) == 0) return audiofile;
    }

    free(audiofile);
    return NULL;
}

int get_frame_rate(int *frame_rate_num, int *frame_rate_den, const char *arg)
{
    int i = 0;
    const char* cp;

    if (frame_rate_num == NULL || frame_rate_den == NULL || arg == NULL)
        return -1;

    // First, we check our abbreviation table
    while (frame_rate_abbrs[i].abbr) {
        if (!strcmp(frame_rate_abbrs[i].abbr, arg)) {
            *frame_rate_num = frame_rate_abbrs[i].rate_num;
            *frame_rate_den = frame_rate_abbrs[i].rate_den;
            return 0;
        }
        i++;
    }

    // Then, we try to parse it as fraction
    cp = strchr(arg, '/');
    if (!cp)
        cp = strchr(arg, ':');

    if (cp) {
        char* cpp;
        *frame_rate_num = strtol(arg, &cpp, 10);
        if (cpp != arg || cpp == cp)
            *frame_rate_den = strtol(cp+1, &cpp, 10);
        else
           *frame_rate_num = 0;
    }
    else {
        // Finally we give up and parse it as double
        AVRational time_base = av_d2q(strtod(arg, 0), MAX_FRAME_RATE_BASE);
        *frame_rate_den = time_base.den;
        *frame_rate_num = time_base.num;
    }

    if (*frame_rate_num == 0 || *frame_rate_den == 0)
        return -1;
    else
        return 0;
}

int get_frame_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i = 0;
    const char *p;

    if (width_ptr == NULL || height_ptr == NULL || str == NULL) 
        return -1;

    while (frame_size_abbrs[i].abbr) {
        if (!strcmp(frame_size_abbrs[i].abbr, str)) {
            *width_ptr = frame_size_abbrs[i].width;
            *height_ptr = frame_size_abbrs[i].height;
            return 0;
        }
        i++;
    }

    p = str;
    *width_ptr = strtol(p, (char **)&p, 10);

    if (*p) p++;
    *height_ptr = strtol(p, (char **)&p, 10);

    if (*width_ptr <= 0 || *height_ptr <= 0)
        return -1;

    return 0;
}

int get_aspect_ratio(AVRational *aspect_ratio, const char *arg)
{
    const char* cp;

    if (aspect_ratio == NULL || arg == NULL)
        return -1;

    // Try to parse it as fraction
    cp = strchr(arg, '/');
    if (!cp)
        cp = strchr(arg, ':');

    if (cp) {
        char* cpp;
        aspect_ratio->num = strtol(arg, &cpp, 10);
        if (cpp != arg || cpp == cp)
            aspect_ratio->den = strtol(cp+1, &cpp, 10);
        else
           aspect_ratio->num = 0;
    }
    else {
        // Finally we give up and parse it as double
        *aspect_ratio = av_d2q(strtod(arg, 0), MAX_FRAME_RATE_BASE);
    }

    if (aspect_ratio->num == 0 || aspect_ratio->den == 0)
        return -1;
    else
        return 0;
}
