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

#ifndef _CDG2VIDEO_UTILS_H
#define _CDG2VIDEO_UTILS_H

typedef struct {
    const char *abbr;
    int rate_num, rate_den;
} tVideoFrameRateAbbr;

typedef struct {
    const char *abbr;
    int width, height;
} tVideoFrameSizeAbbr;

char* get_audio_filename(const char* cdgfile);
bool  is_supported_audio(const char* ext);
int   get_frame_rate(int *frame_rate_num, int *frame_rate_den, const char *arg);
int   get_frame_size(int *width_ptr, int *height_ptr, const char *str);
int   get_aspect_ratio(AVRational *aspect_ratio, const char *arg);

#endif // #define _CDG2VIDEO_UTILS_H

