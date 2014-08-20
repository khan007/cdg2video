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

enum
{
  OPTIONID_FORCE_ENCODE_AUDIO  = 500,
  OPTIONID_ACODEC,
  OPTIONID_VCODEC,
  OPTIONID_ASPECT,
  OPTIONID_SHOW_FORMATS,
  OPTIONID_SHOW_CODECS,
  OPTIONID_STDOUT
};

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
    float mux_preload;    	// demux-decode delay in seconds
    int video_stdout;		// use "/dev/stdout" as a video file name

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
    0		// use "/dev/stdout" as a video file name
};


static CDGFile cdgfile;
static VideoFrameSurface frameSurface;

static AVFrame *picture, *tmp_picture;
static struct SwsContext *img_convert_ctx;

static AVAudioFifo *audio_fifo;
static SwrContext *audio_resample_ctx; 

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

// add audio stream, as a copy of is
static AVStream *add_audio_stream(AVFormatContext *oc, AVStream* is)
{
    AVCodecContext *c;
    AVStream *st;

    if (is == NULL) return NULL;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        fprintf(stderr, "Could not alloc audio stream\n");
        exit(1);
    }
    st->id = 1;

    c = st->codec;
    c->codec_id = is->codec->codec_id ;
    c->codec_type = is->codec->codec_type; 
    c->sample_fmt = is->codec->sample_fmt;
    c->channel_layout = is->codec->channel_layout;

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

    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) 
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

// add audio stream by codec_id
static AVStream *add_audio_stream(AVFormatContext *oc, AVCodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;
    AVCodec *codec;

    codec = avcodec_find_encoder(codec_id);
    if (!codec) 
    {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    if (codec->type != AVMEDIA_TYPE_AUDIO)
    {
        fprintf(stderr, "Invalid audio codec\n");
        exit(1);
    }

    st = avformat_new_stream(oc, codec);
    if (!st) {
        fprintf(stderr, "Could not alloc audio stream\n");
        exit(1);
    }
    st->id = 1;

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = AVMEDIA_TYPE_AUDIO;

    /* put sample parameters */

    c->bit_rate = Options.audio_bit_rate;

    c->sample_rate = Options.audio_sample_rate;
    if (codec->supported_samplerates) {
        c->sample_rate = codec->supported_samplerates[0];
        for (int i = 0; codec->supported_samplerates[i]; i++) {
           if (codec->supported_samplerates[i] == Options.audio_sample_rate) {
               c->sample_rate = codec->supported_samplerates[i];
               break;
           }
        }
    }

    c->channel_layout = av_get_default_channel_layout(Options.audio_channels);
    if (codec->channel_layouts) {
        c->channel_layout = codec->channel_layouts[0];
        for (int i = 0; codec->channel_layouts[i]; i++) {
            if (codec->channel_layouts[i] == (uint64_t)av_get_default_channel_layout(Options.audio_channels)) {
                c->channel_layout = codec->channel_layouts[i];
                break;
            }
        }
    }

    c->channels = av_get_channel_layout_nb_channels(c->channel_layout);

    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    if (codec->sample_fmts) {
        c->sample_fmt = codec->sample_fmts[0];
        for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
            if (codec->sample_fmts[i] == AV_SAMPLE_FMT_S16) {
                c->sample_fmt = codec->sample_fmts[i];
                break;
            }
            if (codec->sample_fmts[i] == AV_SAMPLE_FMT_S16P) {
                c->sample_fmt = codec->sample_fmts[i];
                break;
            }
        }
    }

    st->time_base = (AVRational){1, c->sample_rate};

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
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open output audio codec (ID: 0x%08X)\n", c->codec_id);
        exit(1);
    }

    audio_fifo = av_audio_fifo_alloc(st->codec->sample_fmt, st->codec->channels, 1);
}

static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size)
{
    int error;

    if (!(*converted_input_samples = (uint8_t **)calloc(output_codec_context->channels,
                                            sizeof(**converted_input_samples)))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }

    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  output_codec_context->channels,
                                  frame_size,
                                  output_codec_context->sample_fmt, 0)) < 0) {
        fprintf(stderr, "Could not allocate converted input samples\n");
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        *converted_input_samples = NULL;
        return error;
    }
    return 0;
}

static int convert_samples(const uint8_t **input_data,
                           uint8_t **converted_data, const int frame_size,
                           SwrContext *resample_context)
{
    int error;

    if ((error = swr_convert(resample_context,
                             converted_data, frame_size,
                             input_data    , frame_size)) < 0) {
        fprintf(stderr, "Could not convert input samples\n");
        return error;
    }

    return 0;
}

static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size)
{
    int error;

    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }

    if (av_audio_fifo_write(fifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

static int decode_audio_frame(AVFormatContext *ic, AVStream* is, AVStream* os, int *finished)
{
    int error = AVERROR_EXIT;
    int data_present = 0;
    uint8_t **converted_input_samples = NULL;
    AVFrame *input_frame = NULL;
    AVPacket input_packet;
    
    av_init_packet(&input_packet);
    input_packet.data = NULL;
    input_packet.size = 0;

    *finished = 0;
    if ((error = av_read_frame(ic, &input_packet)) < 0) {
        if (error == AVERROR_EOF) {
            *finished = 1;
        }
        else {
            fprintf(stderr, "Could not read audio frame \n");
            goto cleanup;
        }
    }

    input_frame = av_frame_alloc();
    if (input_frame == NULL) {
        fprintf(stderr, "Could not allocate input frame\n");
        error = AVERROR_EXIT;
        goto cleanup;
    }

    if ((error = avcodec_decode_audio4(is->codec, input_frame, &data_present, &input_packet)) < 0) {
        fprintf(stderr, "Could not decode audi oframe\n");
        av_free_packet(&input_packet);
        goto cleanup;
    }

    if (*finished && data_present) {
        *finished = 0;
    }

    if (*finished && !data_present) {
        error = AVERROR_EOF;
        goto cleanup;
    }

    if (data_present) {

        if (init_converted_samples(&converted_input_samples, os->codec, input_frame->nb_samples))
            goto cleanup;

        if (convert_samples((const uint8_t**)input_frame->extended_data, converted_input_samples,
                            input_frame->nb_samples, audio_resample_ctx))
            goto cleanup;

        if (add_samples_to_fifo(audio_fifo, converted_input_samples,
                                input_frame->nb_samples))
            goto cleanup;

    }

    error = 0;

cleanup:

    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }

    av_frame_free(&input_frame);
    av_free_packet(&input_packet);

    return error;
}

static int encode_audio_frame(AVFrame *frame,
                              AVFormatContext *oc,
                              AVStream* os,
                              int *data_present)
{
    int error;
    AVPacket output_packet;

    av_init_packet(&output_packet);
    output_packet.data = NULL;
    output_packet.size = 0;

    if ((error = avcodec_encode_audio2(os->codec, &output_packet, frame, data_present)) < 0) {
        fprintf(stderr, "Could not encode audio frame\n");
        av_free_packet(&output_packet);
        return error;
    }

    if (*data_present) {
        if ((error = write_frame(oc, &os->codec->time_base, os, &output_packet)) < 0) {
            fprintf(stderr, "Could not write audio frame\n");
            av_free_packet(&output_packet);
            return error;
        }

        av_free_packet(&output_packet);
    }

    return 0;
}

static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size)
{
    int error;

    /** Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }

    (*frame)->nb_samples     = frame_size;
    (*frame)->channel_layout = output_codec_context->channel_layout;
    (*frame)->format         = output_codec_context->sample_fmt;
    (*frame)->sample_rate    = output_codec_context->sample_rate;

    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        fprintf(stderr, "Could allocate output frame samples\n");
        av_frame_free(frame);
        return error;
    }

    return 0;
}

static int encode_audio_from_fifo(AVAudioFifo *fifo,
                                 AVFormatContext *oc,
                                 AVStream *os)
{
    AVFrame *output_frame;
    int data_written;
    const int frame_size = FFMIN(av_audio_fifo_size(fifo), os->codec->frame_size);

    if (init_output_frame(&output_frame, os->codec, frame_size))
        return AVERROR_EXIT;

    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    if (encode_audio_frame(output_frame, oc, os, &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    av_frame_free(&output_frame);
    return 0;
}

// Read frame from the input stream, decode it, re-encode it and write it in the output stream
// return - 0 if ok and != 0 if eof
static int write_audio_frame(AVFormatContext *ic, AVStream* is, AVFormatContext *oc, AVStream* os)
{
    if (!ic || !is || !oc || !os) return 1;

    int finished = 0;
    int ret = decode_audio_frame(ic, is, os, &finished);

    while (av_audio_fifo_size(audio_fifo) >= os->codec->frame_size || 
            (finished && av_audio_fifo_size(audio_fifo) > 0))
    {
        if (encode_audio_from_fifo(audio_fifo, oc, os)) break;
    }

    if (finished) {
        // Flush the encoder as it may have delayed frames.
        int data_written;
        do {
            if (encode_audio_frame(NULL, oc, os, &data_written)) break;
        } while (data_written);        
    }

    return ret;
}

// close output audio codec 
static void close_audio(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_audio_fifo_free(audio_fifo);
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
        write_frame(oc, &is->time_base, os, &pkt);
        av_free_packet(&pkt);   

    }

    return ret;
}
 
static AVStream* open_input_audio(CdgIoStream* pAudioStream, AVFormatContext **ic)
{
    AVStream *st;
    int  audioStreamIdx;

    *ic = avformat_alloc_context();
    if (*ic == NULL) return NULL;

    (*ic)->pb = pAudioStream->get_avio();

    // Open audio file
    if (avformat_open_input(ic, NULL, NULL, NULL) != 0)
        return NULL; // Couldn't open file

    // Retrieve stream information
    if (avformat_find_stream_info(*ic, NULL) < 0)
        return NULL; // Couldn't find stream information

    // Dump information about file onto standard error
    av_dump_format(*ic, 0, pAudioStream->getfilename(), false);

    // Find the first audio stream
    audioStreamIdx = -1;
    for (unsigned int i = 0; i < (*ic)->nb_streams; i++) {
        if ((*ic)->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
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
    if (avcodec_open2(st->codec, codec, NULL) < 0) {
        fprintf(stderr, "Could not open input audio codec (ID: 0x%08X)\n", st->codec->codec_id);
        exit(1);
    }

    return st;
}

static void close_input_audio(AVFormatContext *ic, AVStream* is)
{
    if (is) avcodec_close(is->codec);
    if (ic) avformat_close_input(&ic);
}

// Add video output stream
static AVStream *add_video_stream(AVFormatContext *oc, AVCodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        fprintf(stderr, "Could not alloc video stream\n");
        exit(1);
    }
    st->id = 0;

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = AVMEDIA_TYPE_VIDEO;
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
    st->time_base = (AVRational){Options.frame_rate.den, Options.frame_rate.num};
    c->time_base = st->time_base;

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

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    if (av_image_alloc(picture->data, picture->linesize, width, height, pix_fmt, 16) < 0) {
        av_frame_free(&picture);
        return NULL;
    }

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
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open video codec (ID: 0x%08X)\n", c->codec_id);
        exit(1);
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
                      0, CDG_FULL_HEIGHT, picture->data, picture->linesize);

    // Encode frame
    int got_packet = 0;
    AVPacket pkt;

    av_init_packet(&pkt);
    pkt.data= NULL;
    pkt.size= 0;

    if (avcodec_encode_video2(c, &pkt, picture, &got_packet) == 0 && got_packet == 1) {
        // write the compressed frame in the media file
        if (write_frame(oc, &c->time_base, st, &pkt) < 0) {
            fprintf(stderr, "Error while writing video frame\n");
            exit(1);            
        }
    } 

    av_free_packet(&pkt);
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);

    av_free(picture->data[0]);
    av_frame_free(&picture);
    av_free(tmp_picture->data[0]);
    av_frame_free(&tmp_picture);

    sws_freeContext(img_convert_ctx);
}

int cdg2avi(const char* avifile, CdgIoStream* pAudioStream)
{
    AVFormatContext *oc = NULL;
    AVFormatContext *ic = NULL;
    AVStream *video_st = NULL;
    AVStream *audio_st = NULL;
    AVStream *in_audio_st = NULL;
    bool copy_audio = false;

    if (pAudioStream) {
        in_audio_st = open_input_audio(pAudioStream, &ic);
        if (in_audio_st == NULL) {
            if (ic == NULL)
                fprintf(stderr, "WARNING: Unable to allocate context for audio file: %s\n", pAudioStream->getfilename());
            else
                fprintf(stderr, "WARNING: Unable to find input audio stream in %s\n", pAudioStream->getfilename());
        }
    }

    // allocate the output media context
    oc = avformat_alloc_context();
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

    av_dump_format(oc, 0, avifile, 1);

    if (video_st)
        open_video(oc, video_st);

    audio_resample_ctx = NULL;

    if (copy_audio == false && audio_st) {
        open_audio(oc, audio_st);

        // Create a resampler context for the conversion
        audio_resample_ctx = swr_alloc_set_opts(NULL, 
                                    audio_st->codec->channel_layout,    
                                    audio_st->codec->sample_fmt,    
                                    audio_st->codec->sample_rate,
                                    in_audio_st->codec->channel_layout, 
                                    in_audio_st->codec->sample_fmt, 
                                    in_audio_st->codec->sample_rate,
                                    0, NULL);

        if (!audio_resample_ctx) {
            fprintf(stderr, "Can't resample audio.  Aborting.\n");
            exit(1);
        }

        // initialize the resampling context
        if (swr_init(audio_resample_ctx) < 0) {
            fprintf(stderr, "Failed to initialize the resampling context\n");
            exit(1);
        }

    }

    // open the output file, if needed
    if (!(Options.format->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, avifile, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Could not open '%s'\n", avifile);
            exit(1);
        }
    }

    // Set context options
    oc->packet_size = Options.packet_size;
    oc->max_delay = (int)(0.7 * AV_TIME_BASE);

    // add meta data to the output file
    av_dict_set(&oc->metadata, "encoded_by", PACKAGE " " VERSION, 0);
    if (ic) {
        AVDictionaryEntry *tag;

        tag = av_dict_get(ic->metadata, "title", NULL, 0);
        if (tag) av_dict_set(&oc->metadata, tag->key, tag->value, 0);

        tag = av_dict_get(ic->metadata, "artist", NULL, 0);
        if (tag) av_dict_set(&oc->metadata, tag->key, tag->value, 0);
    }

    // write the stream header, if any
    avformat_write_header(oc, NULL);

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

        if (duration) 
        {
            fprintf(stderr, "Progress: %d %%\r", (int)((video_pts * 100) / duration));
        }
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
	   avio_close(oc->pb);
    }

    if (audio_resample_ctx)
        swr_free(&audio_resample_ctx);

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
    {"show-formats",        no_argument,        0, OPTIONID_SHOW_FORMATS},
    {"show-codecs",         no_argument,        0, OPTIONID_SHOW_CODECS}, 
    {"version",             no_argument,        0, 'V'},
    
    {"format",              required_argument,  0, 'f'},
    {"force-encode-audio",  no_argument,        0, OPTIONID_FORCE_ENCODE_AUDIO},
    {"acodec",              required_argument,  0, OPTIONID_ACODEC},
    {"vcodec",              required_argument,  0, OPTIONID_VCODEC},
    {"aspect",              required_argument,  0, OPTIONID_ASPECT},
    
    {"stdout",              no_argument,        0, OPTIONID_STDOUT},
    
    {0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
    int c, option_index = 0;

    // initialize libavcodec, and register all codecs and formats
    av_register_all();

    // set default output format - xvid avi with mp3 audio
    Options.format = av_guess_format("avi", NULL, NULL);
    if (Options.format) {
        //if (avcodec_find_encoder(CODEC_ID_XVID)) 
        //    Options.format->video_codec = CODEC_ID_XVID;
        if (avcodec_find_encoder(CODEC_ID_MP3))  
            Options.format->audio_codec = CODEC_ID_MP3;
    }

    // parse command line options
    while ((c = getopt_long(argc, argv, "hVs:r:f:", long_options, &option_index)) != -1) 
    {
        switch (c) 
        {
        case 'h':
            print_help(NULL);
            return 0;
    
        case OPTIONID_SHOW_FORMATS:
            print_help("formats");
            return 0;
  
        case OPTIONID_SHOW_CODECS:
            print_help("codecs");
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
            Options.format = av_guess_format(optarg, NULL, NULL);

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
                if (p && p->type == AVMEDIA_TYPE_AUDIO) Options.format->audio_codec = p->id;
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
                if (p && p->type == AVMEDIA_TYPE_VIDEO) Options.format->video_codec = p->id;
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
            
        case OPTIONID_STDOUT:
            Options.video_stdout = 1;
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

    for (int files = optind; files < argc; files++) 
    {
        bool extcdg = false;
        bool extzip = false;
        
        char* p = strrchr(argv[files], '.');
        
        if (p && strcasecmp(p+1, "cdg") == 0) extcdg = true;
        else
        if (p && strcasecmp(p+1, "zip") == 0) extzip = true;
            
        if (extcdg == false && extzip == false)
        {
            fprintf(stderr, "File is ignored (unsupported file type) : %s\n", argv[files]);
            continue;
        }
        
        CdgIoStream* pCdgStream = NULL;
        CdgIoStream* pAudioStream = NULL;

        CdgFileIoStream cdgfilestream;
        CdgFileIoStream audiofilestream;
        CdgZipFileIoStream cdgzipstream;
        CdgZipFileIoStream audiozipstream;
        
        if (extcdg && cdgfilestream.open(argv[files], "r"))
        {
            pCdgStream = &cdgfilestream;
        }
        
        struct zip* zipfile = NULL;
        if (extzip)
        {
            int error;
            zipfile = zip_open(argv[files], 0, &error);
            if (zipfile) 
            {
                // find cdg file
                int cdgidx = 0; 
                const char* filename = NULL;
                
                do 
                {
                    filename = zip_get_name(zipfile, cdgidx++, 0);
                    if (filename) 
                    {
                        const char* p = strrchr(filename, '.');
                        if (p) 
                        {
                            if (pCdgStream == NULL && strcasecmp(p+1, "cdg") == 0 && 
                                cdgzipstream.open(zipfile, filename)) 
                            {
                                pCdgStream = &cdgzipstream;
                            }
                            else
                            if (pAudioStream == NULL && is_supported_audio(p+1) &&
                                audiozipstream.open(zipfile, filename))
                            {
                                pAudioStream = &audiozipstream;
                            }
                        }
                    }
                } while (filename && (pCdgStream == NULL || pAudioStream == NULL));
                
            }
            else 
            {
                fprintf(stderr, "Zip error %d on file: %s\n", error, argv[files]);
            }
        }
        
        if (pCdgStream && cdgfile.open(pCdgStream, &frameSurface)) 
        {
            fprintf(stderr, "Converting: %s\n", argv[files]);

            // find corresponding audio file
            char* audiofile = get_audio_filename(argv[files]);

            if (pAudioStream == NULL) {
                if (audiofile != NULL && audiofilestream.open(audiofile, "r")) {
                    pAudioStream = &audiofilestream;
                }
                else {
                    fprintf(stderr, "WARNING: Can't find audio file (*.mp3)\n");
                }
            }

            // generate avi file name
            char* avifile = (char*)malloc(strlen(argv[files]) + 64);
            strcpy(avifile, argv[files]);
            p = strrchr(avifile, '.'); p++;

            if (Options.video_stdout)
            {
                strcpy(avifile, "/dev/stdout");
            }
            else
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
            cdg2avi(avifile, pAudioStream);

            // free allocated memory
            free(avifile);
            if (audiofile) free(audiofile);
        }
        else 
        {
            fprintf(stderr, "Unable to open file: %s\n", argv[files]);
        }
        
        cdgzipstream.close();
        audiozipstream.close();
        if (zipfile) zip_close(zipfile);
    }

    return 0;
}
