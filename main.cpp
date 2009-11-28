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
#include <getopt.h>

#include "ffmpeg_headers.h"
#include "cdgfile.h"
#include "help.h"
#include "utils.h"

#define OPTIONID_FORCE_ENCODE_AUDIO 500
#define OPTIONID_ACODEC             501
#define OPTIONID_VCODEC             502
#define OPTIONID_ASPECT             503

#define MAX_AUDIO_PACKET_SIZE (128 * 1024)

class VideoFrameSurface : public ISurface
{
public:
    virtual unsigned long MapRGBColour(int red, int green, int blue)
    {
        return ((uint8_t)red) << 16 | ((uint8_t)green) << 8 | ((uint8_t)blue);
    }
};

typedef struct
{
    AVOutputFormat *format;

    // picture

    int width;
    int height;

    AVRational aspect_ratio;
    AVRational frame_rate;

    enum PixelFormat frame_pix_fmt;

    // audio

    int audio_bit_rate;
    int audio_sample_rate;
    int audio_channels;
    int audio_encode_always;

    // video

    int video_bit_rate;
    int video_max_rate;
    int video_min_rate;
    int video_buffer_size;
    int video_codec_flags;

    // misc

    int packet_size;
    float mux_preload;    // demux-decode delay in seconds

}tOptions;

// defualt options
static tOptions Options =
{
    NULL,
    352, 288,   // PAL/SECAM: 352x288 - Video CD resolution
    {4,	 3},	// display aspect ratio
    {25, 1},    // frame rate - PAL/SECAM: 25 frames per second

    PIX_FMT_YUV420P, 

    192000,     // audio bit rate
    44100,      // audio sample rate
    2,          // audio channels
    0,          // --force-encode-audio

    400000,     // video bit rate
    0,          // video max rate
    0,          // video min rate
    0,          // video buffer size
    0,          // video codec flags

    0,          // packet_size
    0.5,        // demux-decode delay in seconds
};


static CDGFile cdgfile;
static VideoFrameSurface frameSurface;

static AVFrame *picture, *tmp_picture;
static struct SwsContext *img_convert_ctx;

static uint8_t *video_outbuf;
static int video_outbuf_size;

static uint8_t *audio_outbuf;
static int audio_outbuf_size;

static int16_t *audio_samples;
static int audio_samples_size;

static AVFifoBuffer *audio_fifo;
static uint8_t *audio_fifo_samples;

ReSampleContext *audio_resample_buf; 

// add audio stream, as a copy of is
static AVStream *add_audio_stream(AVFormatContext *oc, AVStream* is)
{
    AVCodecContext *c;
    AVStream *st;

    if (is == NULL) return NULL;

    st = av_new_stream(oc, 1);
    if (!st) {
        fprintf(stderr, "Could not alloc audio stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = is->codec->codec_id ;
    c->codec_type = is->codec->codec_type; 

    // put sample parameters
    c->bit_rate = is->codec->bit_rate; 
    c->sample_rate = is->codec->sample_rate; 
    c->channels = is->codec->channels;

    c->frame_size = is->codec->frame_size;
    c->block_align= is->codec->block_align;

    if (av_q2d(is->codec->time_base) > av_q2d(is->time_base) && av_q2d(is->time_base) < 1.0/1000)
        c->time_base = is->codec->time_base;
    else
        c->time_base = is->time_base;

    return st;
}

// add audio stream by codec_id
static AVStream *add_audio_stream(AVFormatContext *oc, CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 1);
    if (!st) {
        fprintf(stderr, "Could not alloc audio stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = CODEC_TYPE_AUDIO;

    /* put sample parameters */
    c->bit_rate = Options.audio_bit_rate;
    c->sample_rate = Options.audio_sample_rate;
    c->channels = Options.audio_channels;
    //c->time_base= (AVRational){1, c->sample_rate};

    if (oc->oformat->flags & AVFMT_GLOBALHEADER) 
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

// open output audio codec
static void open_audio(AVFormatContext *oc, AVStream *st)
{
    AVCodecContext *c;
    AVCodec *codec;

    c = st->codec;

    // find the audio encoder
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "Output audio codec not found (ID: 0x%08X)\n", c->codec_id);
        exit(1);
    }

    // open it
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "Could not open output audio codec (ID: 0x%08X)\n", c->codec_id);
        exit(1);
    }

    audio_outbuf_size = 4*MAX_AUDIO_PACKET_SIZE;
    audio_outbuf = (uint8_t *)av_malloc(audio_outbuf_size);

    audio_samples_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
    audio_samples = (int16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

    audio_fifo = av_fifo_alloc(2*MAX_AUDIO_PACKET_SIZE);
    audio_fifo_samples = (uint8_t *)av_malloc(2*MAX_AUDIO_PACKET_SIZE);
}

// Read frame from the input stream, decode it, re-encode it and write it in the output stream
// return - 0 if ok and != 0 if eof
static int write_audio_frame(AVFormatContext *ic, AVStream* is, AVFormatContext *oc, AVStream* os)
{
    int ret, data_size, decoded, len;
    uint8_t *ptr;
    int16_t* p_audio_samples;

    AVPacket pkt;
    av_init_packet(&pkt); 

    if (!ic || !is || !oc || !os) 
        return 1;

    ret = av_read_frame(ic, &pkt);

    if (ret == 0) 
    {
        len = pkt.size;
        ptr = pkt.data;

        while (len > 0) {

            // decode 
            data_size = audio_samples_size;
            decoded = avcodec_decode_audio2(is->codec,
                                audio_samples, &data_size,
                                ptr, len);
    
            if (decoded < 0) { // if decoder failed
                av_free_packet(&pkt);
                return 0;
            }
    
            ptr += decoded;
            len -= decoded;        
    
            if (data_size <= 0) // if no audio frame
                continue;

            int frame_bytes = os->codec->frame_size * 2 * os->codec->channels;
            if (!frame_bytes) return 0; // FIXME: add alternative encoding as in ffmpeg

            if (audio_resample_buf) 
            {
                data_size = audio_resample(audio_resample_buf,
                                  (short *)audio_outbuf, (short *)audio_samples,
                                  data_size / (is->codec->channels * 2));
                data_size *= os->codec->channels * 2;
                p_audio_samples = (int16_t*)audio_outbuf;
            }
            else {
                p_audio_samples = audio_samples;
            }

            // we need to encode data_size bytes, using frame_bytes buffer
            // if data_size > frame_bytes we need to encode several times
            // if data_size < frame_bytes we need more data

            av_fifo_generic_write(audio_fifo, (uint8_t*)p_audio_samples, data_size, NULL);

            while (av_fifo_generic_read(audio_fifo, audio_fifo_samples, frame_bytes, NULL) == 0) 
            {
                AVPacket pkt_out;
                av_init_packet(&pkt_out);

                // encode
                pkt_out.size = avcodec_encode_audio(
                                    os->codec, 
                                    audio_outbuf, audio_outbuf_size, 
                                    (short *)audio_fifo_samples);
    
                if (pkt_out.size > 0) {
    
                    if (os->codec->coded_frame && os->codec->coded_frame->pts != (int64_t)AV_NOPTS_VALUE) {
                        pkt_out.pts = av_rescale_q(os->codec->coded_frame->pts, os->codec->time_base, os->time_base);
                    }
    
                    pkt_out.flags |= PKT_FLAG_KEY;
                    pkt_out.stream_index= os->index;
                    pkt_out.data = audio_outbuf;
        
                    ret = av_write_frame(oc, &pkt_out);
                }
            }

        }

        av_free_packet(&pkt);
    }
    else {  
        // End of audio file. 

        AVPacket pkt_out;
        av_init_packet(&pkt_out);
        int fifo_bytes = av_fifo_size(audio_fifo);

        // encode any samples remaining in fifo

        if (fifo_bytes > 0 && os->codec->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME) 
        {
            int fs_tmp = os->codec->frame_size;
            os->codec->frame_size = fifo_bytes / (2 * os->codec->channels);

            if (av_fifo_generic_read(audio_fifo, (uint8_t *)audio_fifo_samples, fifo_bytes, NULL) == 0) 
            {
                pkt_out.size = avcodec_encode_audio(os->codec, 
                                                    audio_outbuf, audio_outbuf_size, 
                                                    (short *)audio_fifo_samples);
            }
        
            os->codec->frame_size = fs_tmp;

            if (pkt_out.size > 0) {
    
                if (os->codec->coded_frame && os->codec->coded_frame->pts != (int64_t)AV_NOPTS_VALUE) {
                    pkt_out.pts = av_rescale_q(os->codec->coded_frame->pts, os->codec->time_base, os->time_base);
                }
    
                pkt_out.flags |= PKT_FLAG_KEY;
                pkt_out.stream_index= os->index;
                pkt_out.data = audio_outbuf;
        
                av_write_frame(oc, &pkt_out);
            }

        }
  
    }

    return ret;
}

// close output audio codec 
static void close_audio(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);

    av_free(audio_outbuf);
    av_free(audio_samples);

    av_fifo_free(audio_fifo);
    av_free(audio_fifo_samples);
}

static int copy_audio_frame(AVFormatContext *ic, AVStream* is, AVFormatContext *oc, AVStream* os)
{
    int ret;
    AVPacket pkt;
    av_init_packet(&pkt); 

    if (!ic || !is || !oc || !os) 
        return 1;

    ret = av_read_frame(ic, &pkt);

    if (ret == 0) {

        // convert paket pts and dts from input time base to the output time base
        if (pkt.pts != (int64_t)AV_NOPTS_VALUE)
            pkt.pts = av_rescale_q(pkt.pts, is->time_base, os->time_base);

        if (pkt.dts != (int64_t)AV_NOPTS_VALUE)
            pkt.dts = av_rescale_q(pkt.dts, is->time_base, os->time_base);

        pkt.flags |= PKT_FLAG_KEY;
        pkt.stream_index= os->index;

        //av_pkt_dump(stderr, &pkt, 0);
        av_write_frame(oc, &pkt);
        av_free_packet(&pkt);   

    }

    return ret;
}
 
static AVStream* open_input_audio(const char* afile, AVFormatContext **ic)
{
    AVStream *st;
    int  audioStreamIdx;

    *ic = NULL;

    // Open audio file
    if (av_open_input_file(ic, afile, NULL, 0, NULL) != 0)
        return NULL; // Couldn't open file

    // Retrieve stream information
    if (av_find_stream_info(*ic) < 0)
        return NULL; // Couldn't find stream information

    // Dump information about file onto standard error
    dump_format(*ic, 0, afile, false);

    // Find the first audio stream
    audioStreamIdx = -1;
    for (unsigned int i = 0; i < (*ic)->nb_streams; i++) {
        if ((*ic)->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
            audioStreamIdx = i;
            break;
        }
    }

    if (audioStreamIdx == -1)
        return NULL; // Didn't find a audio stream

    // Get a pointer to the input audio stream
    st = (*ic)->streams[audioStreamIdx];

    // find the audio encoder
    AVCodec *codec = avcodec_find_decoder(st->codec->codec_id);
    if (!codec) {
        fprintf(stderr, "Input audio codec not found (ID: 0x%08X)\n", st->codec->codec_id);
        exit(1);
    }

    // open it
    if (avcodec_open(st->codec, codec) < 0) {
        fprintf(stderr, "Could not open input audio codec (ID: 0x%08X)\n", st->codec->codec_id);
        exit(1);
    }

    return st;
}

static void close_input_audio(AVFormatContext *ic, AVStream* is)
{
    if (is) avcodec_close(is->codec);
    if (ic) av_close_input_file(ic);
}

// Add video output stream
static AVStream *add_video_stream(AVFormatContext *oc, CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 0);
    if (!st) {
        fprintf(stderr, "Could not alloc video stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = CODEC_TYPE_VIDEO;
    c->flags |= Options.video_codec_flags;

    c->bit_rate           = Options.video_bit_rate;
    c->bit_rate_tolerance = c->bit_rate * 20;

    c->rc_max_rate    = Options.video_max_rate;
    c->rc_min_rate    = Options.video_min_rate;
    c->rc_buffer_size = Options.video_buffer_size;

    // resolution must be a multiple of two
    c->width   = Options.width;
    c->height  = Options.height;
    c->pix_fmt = Options.frame_pix_fmt;

    // calculate pixel aspect ratio
    c->sample_aspect_ratio = av_d2q(av_q2d(Options.aspect_ratio)*Options.height/Options.width, 255);
    st->sample_aspect_ratio = c->sample_aspect_ratio;

    // time base: this is the fundamental unit of time (in seconds) in terms
    // of which frame timestamps are represented. for fixed-fps content,
    // timebase should be 1/framerate and timestamp increments should be
    // identically 1.
    c->time_base.den = Options.frame_rate.num;
    c->time_base.num = Options.frame_rate.den;

    //c->sample_aspect_ratio = av_d2q(frame_aspect_ratio*c->height/c->width, 255);

    c->gop_size = 12; // emit one intra frame every twelve frames at most

    if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
    }

    if (c->codec_id == CODEC_ID_MPEG1VIDEO){
        // Needed to avoid using macroblocks in which some coeffs overflow.
        // This does not happen with normal video, it just happens here as
        // the motion of the chroma plane does not match the luma plane.
        c->mb_decision = FF_MB_DECISION_RD; // rate distoration
    }

    // Fix "rc buffer underflow" warning on the first encoding frame
    c->rc_initial_buffer_occupancy = c->rc_buffer_size*3/4;

    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

static AVFrame *alloc_picture(PixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = avcodec_alloc_frame();
    if (!picture)
        return NULL;
    size = avpicture_get_size(pix_fmt, width, height);
    picture_buf = (uint8_t*)av_malloc(size);
    if (!picture_buf) {
        av_free(picture);
        return NULL;
    }

    avpicture_fill((AVPicture *)picture, picture_buf,
                    pix_fmt, width, height);
    return picture;
}

// Open output video stram
static void open_video(AVFormatContext *oc, AVStream *st)
{
    AVCodec *codec;
    AVCodecContext *c;

    c = st->codec;

    // find the video encoder
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "Video codec not found (ID: 0x%08X)\n", c->codec_id);
        exit(1);
    }

    // open the codec
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "Could not open video codec (ID: 0x%08X)\n", c->codec_id);
        exit(1);
    }

    video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
        /* allocate output buffer */
        /* XXX: API change will be done */
        /* buffers passed into lav* can be allocated any way you prefer,
           as long as they're aligned enough for the architecture, and
           they're freed appropriately (such as using av_free for buffers
           allocated with av_malloc) */
        video_outbuf_size = 2000000;
        video_outbuf = (uint8_t*)av_malloc(video_outbuf_size);
    }

    // allocate the encoded raw picture
    picture = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!picture) {
        fprintf(stderr, "Could not allocate picture\n");
        exit(1);
    }

    // tmp_picture is used for conversion between internal frame format,
    // wich is RGB32 with a constant size and the output frame format
    tmp_picture = alloc_picture(PIX_FMT_RGB24, CDG_FULL_WIDTH, CDG_FULL_HEIGHT);
    if (!tmp_picture) {
        fprintf(stderr, "Could not allocate temporary picture\n");
        exit(1);
    }

    // create image convert context used to convert between tmp_picture and picture
    img_convert_ctx = sws_getContext(CDG_FULL_WIDTH, CDG_FULL_HEIGHT,
                                     PIX_FMT_RGB24,
                                     c->width, c->height,
                                     c->pix_fmt,
                                     SWS_BICUBIC, NULL, NULL, NULL);

    if (img_convert_ctx == NULL) {
        fprintf(stderr, "Cannot initialize the conversion context\n");
        exit(1);
    }
}

static void write_video_frame(AVFormatContext *oc, AVStream *st)
{
    int out_size, ret;
    AVCodecContext *c;

    c = st->codec;

    // Copy CD+G frame to a temporary frame object - tmp_picture
    for (int height = 0; height < CDG_FULL_HEIGHT; height++) {
        for (int width = 0, x=0; width < CDG_FULL_WIDTH; width++, x+=3) {
            tmp_picture->data[0][height*tmp_picture->linesize[0] + x]     =
                                    (uint8_t)(frameSurface.rgbData[height][width] >> 16);
            tmp_picture->data[0][height*tmp_picture->linesize[0] + x + 1] =
                                    (uint8_t)(frameSurface.rgbData[height][width] >> 8);
            tmp_picture->data[0][height*tmp_picture->linesize[0] + x + 2] =
                                    (uint8_t)(frameSurface.rgbData[height][width]);
        }
    }

    // As the CDG frame is RGB, convert it to the output color format and scale the image
    sws_scale(img_convert_ctx, tmp_picture->data, tmp_picture->linesize,
                      0, c->height, picture->data, picture->linesize);

    // Encode frame
    out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);
    // if zero size, it means the image was buffered
    if (out_size > 0) {
        AVPacket pkt;
        av_init_packet(&pkt);

        if (c->coded_frame && c->coded_frame->pts != (int64_t)AV_NOPTS_VALUE) {
            pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
        }

        if(c->coded_frame->key_frame)
            pkt.flags |= PKT_FLAG_KEY;

        pkt.stream_index= st->index;
        pkt.data= video_outbuf;
        pkt.size= out_size;

        //av_pkt_dump(stderr, &pkt, 0);
        // write the compressed frame in the media file
        ret = av_write_frame(oc, &pkt);
    } else {
        ret = 0;
    }

    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        exit(1);
    }
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);

    av_free(picture->data[0]);
    av_free(picture);
    av_free(tmp_picture->data[0]);
    av_free(tmp_picture);

    av_free(video_outbuf); 

    sws_freeContext(img_convert_ctx);
}

int cdg2avi(const char* avifile, const char* audiofile)
{
    AVFormatContext *oc = NULL;
    AVFormatContext *ic = NULL;
    AVStream *video_st = NULL;
    AVStream *audio_st = NULL;
    AVStream *in_audio_st = NULL;
    bool copy_audio = false;

    if (audiofile) {
        in_audio_st = open_input_audio(audiofile, &ic);
        if (in_audio_st == NULL) {
            if (ic == NULL)
                fprintf(stderr, "WARNING: Unable to open audio file: %s\n", audiofile);
            else
                fprintf(stderr, "WARNING: Unable to find input audio stream in %s\n", audiofile);
        }
    }

    // allocate the output media context
    oc = av_alloc_format_context();
    if (!oc) {
        fprintf(stderr, "Memory error\n");
        exit(1);
    } 

    oc->oformat = Options.format;
    snprintf(oc->filename, sizeof(oc->filename), "%s", avifile);

    if (Options.format->video_codec != CODEC_ID_NONE) {
        video_st = add_video_stream(oc, Options.format->video_codec);
    }

    if (in_audio_st && Options.format->audio_codec != CODEC_ID_NONE) 
    {
        if (Options.format->audio_codec == in_audio_st->codec->codec_id) {
            copy_audio = true;
        }
        
        if (Options.audio_encode_always) {
            copy_audio = false; 
        }
    
        if (copy_audio) {
            audio_st = add_audio_stream(oc, in_audio_st);
        }
        else {
            audio_st = add_audio_stream(oc, Options.format->audio_codec);
        }
    }

    // set the output parameters (must be done even if no
    // parameters).
    if (av_set_parameters(oc, NULL) < 0) {
        fprintf(stderr, "Invalid output format parameters\n");
        exit(1);
    }

    dump_format(oc, 0, avifile, 1);

    if (video_st)
        open_video(oc, video_st);

    audio_resample_buf = NULL;

    if (copy_audio == false && audio_st) {
        open_audio(oc, audio_st);

        // check if audio resampling is needed
        if (audio_st->codec->channels != in_audio_st->codec->channels ||
            audio_st->codec->sample_rate != in_audio_st->codec->sample_rate) 
        {
            audio_resample_buf = audio_resample_init(
                                    audio_st->codec->channels, in_audio_st->codec->channels,
                                    audio_st->codec->sample_rate, in_audio_st->codec->sample_rate);

            if (!audio_resample_buf) {
                fprintf(stderr, "Can't resample audio.  Aborting.\n");
                exit(1);
            }
        }
    }

    // open the output file, if needed
    if (!(Options.format->flags & AVFMT_NOFILE)) {
        if (url_fopen(&oc->pb, avifile, URL_WRONLY) < 0) {
            fprintf(stderr, "Could not open '%s'\n", avifile);
            exit(1);
        }
    }

    // Set context options
    oc->packet_size = Options.packet_size;
    // Fix "buffer underflow" warning when converting to mpeg formats
    oc->preload = (int)(Options.mux_preload * AV_TIME_BASE);
    oc->max_delay = (int)(0.7 * AV_TIME_BASE);

    // write the stream header, if any
    av_write_header(oc);

    // write avi file
    int duration = cdgfile.getTotalDuration(); // in miliseconds
    int64_t video_pts = 1000 * video_st->pts.val * video_st->time_base.num / video_st->time_base.den;;

    while (cdgfile.renderAtPosition(video_pts))
    {
        write_video_frame(oc, video_st);
        video_pts = 1000 * video_st->pts.val * video_st->time_base.num / video_st->time_base.den;;

        if (audio_st) {
            int audio_ok = 0;
            int64_t audio_pts;
            do {

                if (copy_audio) {
                    audio_ok = copy_audio_frame(ic, in_audio_st, oc, audio_st);
                }
                else {
                    audio_ok = write_audio_frame(ic, in_audio_st, oc, audio_st);
                }

                audio_pts = 1000 * audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;

            } while (audio_ok == 0 && audio_pts < video_pts);
        }

        fprintf(stderr, "Progress: %d %%\r", (int)((video_pts * 100) / duration));
    }
    fprintf(stderr, "\n"); // save the status line

    // close each codec
    if (video_st)
        close_video(oc, video_st);

    if (copy_audio == false && audio_st)
        close_audio(oc, audio_st);

    // write the trailer, if any
    av_write_trailer(oc);

    // free the streams
    for(unsigned int i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    if (!(Options.format->flags & AVFMT_NOFILE)) {
        // close the output file
	#if (LIBAVFORMAT_VERSION_INT < (52 << 16))
        url_fclose(&oc->pb);
	#else
	url_fclose(oc->pb);
	#endif
    }

    if (audio_resample_buf)
        audio_resample_close(audio_resample_buf);

    // free the stream
    av_free(oc);

    close_input_audio(ic, in_audio_st);
    return 0;
}

static void set_audio_codec_defaults()
{
    if (!Options.format) return ;

    switch(Options.format->audio_codec)
    {
    case CODEC_ID_AMR_NB:
        Options.audio_bit_rate      = 12200;
        Options.audio_sample_rate   = 8000;
        Options.audio_channels      = 1;
        break;
    case CODEC_ID_AMR_WB:
        Options.audio_bit_rate      = 23850;
        Options.audio_sample_rate   = 16000;
        Options.audio_channels      = 1;
        break;

    //case CODEC_ID_AC3:
    //    Options.audio_bit_rate      = 192000;
    //    Options.audio_sample_rate   = 48000;
    //    Options.audio_channels      = 2;
    //    break;

    default:
        break;
    }
}

static void set_video_codec_defaults()
{
}

static void set_format_defaults(const char* format)
{
    set_audio_codec_defaults();
    set_video_codec_defaults();

    if (strcmp(format, "vcd")  == 0) 
    {
        Options.video_bit_rate      = 1150000;
        Options.video_max_rate      = 1150000;
        Options.video_min_rate      = 1150000;
        Options.video_buffer_size   = 327680; // 40*1024*8;
        Options.audio_bit_rate      = 224000;
        Options.audio_sample_rate   = 44100;
        Options.audio_channels      = 2;

        Options.packet_size         = 2324;
        Options.mux_preload         = (36000+3*1200) / 90000.0; //0.44
    }
    else
    if (strcmp(format, "svcd") == 0)    
    {
        Options.video_bit_rate      = 2040000;
        Options.video_max_rate      = 2516000;
        Options.video_min_rate      = 0;
        Options.video_buffer_size   = 1835008; // 224*1024*8;
        Options.video_codec_flags   = CODEC_FLAG_SVCD_SCAN_OFFSET;

        Options.audio_bit_rate      = 224000;
        Options.audio_sample_rate   = 44100;
        Options.audio_channels      = 2;
        Options.packet_size         = 2324;
    }
    else
    if ((strcmp(format, "dvd") == 0) ||
        (strcmp(format, "vob") == 0))
    {
        Options.video_bit_rate      = 6000000;
        Options.video_max_rate      = 9000000;
        Options.video_min_rate      = 0;
        Options.video_buffer_size   = 1835008; // 224*1024*8;
        Options.audio_bit_rate      = 224000;
        Options.audio_sample_rate   = 44100;
        Options.audio_channels      = 2;
        Options.packet_size         = 2048;
    }
}

static struct option long_options[] =
{
    {"help",                no_argument,        0, 'h'},
    {"version",             no_argument,        0, 'V'},
    {"format",              required_argument,  0, 'f'},
    {"force-encode-audio",  no_argument,        0, OPTIONID_FORCE_ENCODE_AUDIO},
    {"acodec",              required_argument,  0, OPTIONID_ACODEC},
    {"vcodec",              required_argument,  0, OPTIONID_VCODEC},
    {"aspect",              required_argument,  0, OPTIONID_ASPECT},
    {0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
    char* p;
    char* avifile, *audiofile;
    int c, option_index = 0;

    // initialize libavcodec, and register all codecs and formats
    av_register_all();

    // set default output format - xvid avi with mp3 audio
    Options.format = guess_format("avi", NULL, NULL);
    if (Options.format) {
        if (avcodec_find_encoder(CODEC_ID_XVID)) 
            Options.format->video_codec = CODEC_ID_XVID;
        if (avcodec_find_encoder(CODEC_ID_MP3))  
            Options.format->audio_codec = CODEC_ID_MP3;
    }

    // set global surface
    cdgfile.setSurface(&frameSurface);

    // parse command line options
    while ((c = getopt_long(argc, argv, "hVs:r:f:", long_options, &option_index)) != -1) {
        switch (c) {
        case 'h':
            print_help();
            return 0;
        case 'V':
            print_version();
            return 0;

        case 's':
            if (get_frame_size(&Options.width, &Options.height, optarg)) {
                fprintf(stderr, "Incorrect frame size\n");
                return 1;
            }

            if ((Options.width % 2) != 0 || (Options.height % 2) != 0) {
                fprintf(stderr, "Frame size must be a multiple of 2\n");
                return 1;
            }
            break;

        case 'r':
            if (get_frame_rate(&Options.frame_rate.num, &Options.frame_rate.den, optarg)) {
                fprintf(stderr, "Incorrect frame rate\n");
                return 1;
            }
            break;

        case 'f':
            Options.format = guess_format(optarg, NULL, NULL);

            if (Options.format == NULL) {
                fprintf(stderr, "Could not find suitable output format\n");
                return 1;
            }

            set_format_defaults(optarg);
            break;

        case OPTIONID_FORCE_ENCODE_AUDIO:
            Options.audio_encode_always = 1;
            break;

        case OPTIONID_ACODEC:
            if (Options.format) {
                AVCodec *p = avcodec_find_encoder_by_name(optarg);
                if (p && p->type == CODEC_TYPE_AUDIO) Options.format->audio_codec = p->id;
                else {
                    fprintf(stderr, "Could not find '%s' audio codec\n", optarg);
                    return 1;
                }
                set_audio_codec_defaults();
            }
            break;

        case OPTIONID_VCODEC:
            if (Options.format) {
                AVCodec *p = avcodec_find_encoder_by_name(optarg);
                if (p && p->type == CODEC_TYPE_VIDEO) Options.format->video_codec = p->id;
                else {
                    fprintf(stderr, "Could not find '%s' video codec\n", optarg);
                    return 1;
                }
                set_video_codec_defaults();
            }
            break;

        case OPTIONID_ASPECT:
	    if (get_aspect_ratio(&Options.aspect_ratio, optarg)) {
                fprintf(stderr, "Incorrect aspect ratio\n");
                return 1;
            }
            break;

        default:
            print_usage();
            return 1;
        }
    }

    if (argc <= optind) {
        printf("%s: missing CDGFILE\n", PACKAGE);
        print_usage();
        return -1;
    }

    if (Options.format == NULL) {
        fprintf(stderr, "Could not find suitable output format\n");
        return 1;
    }

    if ((Options.format->video_codec == CODEC_ID_NONE) ||
        (Options.format->audio_codec == CODEC_ID_NONE))
    {
        fprintf(stderr, "Could not find suitable output format\n");
        return 1;
    }

    for (int files = optind; files < argc; files++) {

        // check that file extension is cdg
        p = strrchr(argv[files], '.');
        if (p == NULL || strcasecmp(p+1, "cdg") != 0) 
        {
            fprintf(stderr, "File is ignored (no cdg exetension) : %s\n", argv[files]);
            continue;
        }

        if (cdgfile.open(argv[files])) {
            fprintf(stdout, "Converting: %s\n", argv[files]);

            // find corresponding audio file
            audiofile = get_audio_filename(argv[files]);

            if (audiofile == NULL) {
                fprintf(stderr, "WARNING: Can't find audio file (*.mp3)\n");
            }

            // generate avi file name
            avifile = (char*)malloc(strlen(argv[files]) + 64);
            strcpy(avifile, argv[files]);
            p = strrchr(avifile, '.'); p++;

            if (Options.format->extensions == NULL) {
                strcpy(p, "mpg");
            }
            else
            if (Options.format->extensions[0] == 0) {
                p--; *p = 0;
            }
            else {
                strcpy(p, Options.format->extensions);
                p = strchr(p, ',');
                if (p) *p = 0;
            }

            // perform actual conversion
            cdg2avi(avifile, audiofile);

            // free allocated memory
            free(avifile);
            if (audiofile) free(audiofile);
        }
        else {
            fprintf(stderr, "Unable to open file: %s\n", argv[files]);
        }
    }

    return 0;
}
