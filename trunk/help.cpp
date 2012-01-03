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
#include <math.h>

#include "ffmpeg_headers.h"
#include "utils.h"

extern const tVideoFrameRateAbbr frame_rate_abbrs[];
extern const tVideoFrameSizeAbbr frame_size_abbrs[];

static void print_abbreviation(void);
static void print_formats(void);
static void print_codecs(void);

void print_version(void)
{
    printf("%s Version %s\n\n", PACKAGE, VERSION);

    printf("Copyright (C) 2007-2009 Nikolay Nikolov <nknikolov@gmail.com>\n");
    printf("This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n");
}

void print_usage(void)
{
    printf("Usage: %s [OPTION]... CDGFILE...\n\n", PACKAGE);
    printf("Try '%s --help' for more options.\n", PACKAGE);
}

void print_help(const char* optarg)
{
    printf("%s Version %s, convert CDG files to video\n", PACKAGE, VERSION);
    printf("Usage: %s [OPTION]... CDGFILE...\n", PACKAGE);

    if (optarg == NULL)
    {
      printf("\nGeneral options:\n");
      printf(" -V  --version              Display the version of %s and exit.\n", PACKAGE);
      printf(" -h  --help                 Print this help.\n");
      printf("     --show-formats         Show available output file formats.\n");
      printf("     --show-codecs          Show available output codecs.\n");

      printf("\nEncoding options:\n");
      printf(" -f  --format   <type>      Specify the output file format (default: avi)\n");
      
      printf(" -s             <size>      Set frame size (WxH or abbreviation, default: 352x288)\n");
      printf("     --aspect   <ratio>     Set aspect ratio (4:3, 16:9 or 1.3333, 1.7777, default: 4:3)\n");
      
      printf(" -r             <rate>      Set frame rate (Hz value, fraction or abbreviation, default: pal)\n");

      printf("\n");
      printf("     --acodec   <codec>     Force audio codec\n");
      printf("     --vcodec   <codec>     Force video codec\n");
	  
      printf("\n");
      printf("     --stdout               Redirect video output to standard output\n");
      printf("     --force-encode-audio   By default, if the input audio codec is the same as the output one,\n");
      printf("                            the audio is copied. If you specify this option the audio will be\n");
      printf("                            re-encode always. This can helps in case of corrupted audio files,\n");
      printf("                            but it's possible to reduce the audio quality.\n");

      print_abbreviation();
    }
    
    if (optarg && strcmp(optarg, "formats") == 0)
    {
      print_formats();
    }
    
    if (optarg && strcmp(optarg, "codecs") == 0)
    {
      print_codecs();
    }

    printf("\n");
}

static void print_abbreviation(void)
{
    int i = 0;
    printf("\nFrame rate abbreviations:\n");
    
    while (frame_rate_abbrs[i].abbr) 
    {
        printf(" %-15s %.3f fps\n", frame_rate_abbrs[i].abbr, 
            (double)frame_rate_abbrs[i].rate_num/(double)frame_rate_abbrs[i].rate_den);
        i++;
    }

    i = 0;
    printf("\nFrame size abbreviations:\n");

    while (frame_size_abbrs[i].abbr) 
    {
        printf(" %-15s %dx%d\n", frame_size_abbrs[i].abbr, 
            frame_size_abbrs[i].width, frame_size_abbrs[i].height);
        i++;
    }
}

static void print_formats(void)
{
    AVOutputFormat *ofmt;
    printf("\nFile formats:\n");

#if LIBAVFORMAT_VERSION_INT < ((52<<16)+(2<<8)+0)
    for (ofmt = first_oformat; ofmt != NULL; ofmt = ofmt->next) 
#else
    for (ofmt = av_oformat_next(NULL); ofmt != NULL; ofmt = av_oformat_next(ofmt)) 
#endif
    {
        if (ofmt->name == NULL) break;

        if (ofmt->video_codec == CODEC_ID_NONE) continue;
        if (ofmt->audio_codec == CODEC_ID_NONE) continue;

        printf(" %-15s %-40s\n", ofmt->name, ofmt->long_name ? ofmt->long_name:" ");
    }
}

static void print_codecs(void)
{
    AVCodec *codec;
    int i;

    printf("\nAudio Codecs:\n");

#if LIBAVCODEC_VERSION_INT < ((51<<16)+(49<<8)+0)
    for(codec = first_avcodec, i = 0; codec != NULL; codec = codec->next) 
#else
    for(codec = av_codec_next(NULL), i = 0; codec != NULL; codec = av_codec_next(codec)) 
#endif
    {
        if (codec->name == NULL) break;
        if (codec->encode == 0) continue;
        if (codec->type != AVMEDIA_TYPE_AUDIO) continue;

        printf(" %-16s ", codec->name);
     
        i++; 
        if (!(i % 4)) printf("\n");
    }

    if (i % 4) printf("\n");
    printf("\nVideo Codecs:\n");

#if LIBAVCODEC_VERSION_INT < ((51<<16)+(49<<8)+0)
    for(codec = first_avcodec, i = 0; codec != NULL; codec = codec->next) 
#else
    for(codec = av_codec_next(NULL), i = 0; codec != NULL; codec = av_codec_next(codec)) 
#endif
    {
        if (codec->name == NULL) break;
        if (codec->encode == 0) continue;
        if (codec->type != AVMEDIA_TYPE_VIDEO) continue;

        printf(" %-16s ", codec->name);
     
        i++; 
        if (!(i % 4)) printf("\n");
    }

    if (i % 4) printf("\n");
}
