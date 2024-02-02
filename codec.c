///
/// @file codec.c   @brief Codec functions
///
/// Copyright (c) 2009 - 2015 by Johns.	 All Rights Reserved.
///
/// Contributor(s):
///
/// License: AGPLv3
///
/// This program is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// $Id: d285eb28485bea02cd205fc8be47320dfe0376cf $
//////////////////////////////////////////////////////////////////////////////

///
/// @defgroup Codec The codec module.
///
/// This module contains all decoder and codec functions.
/// It is uses ffmpeg (http://ffmpeg.org) as backend.
///
/// It may work with libav (http://libav.org), but the tests show
/// many bugs and incompatiblity in it.	 Don't use this shit.
///

/// compile with pass-through support (stable, AC-3, E-AC-3 only)
#define USE_PASSTHROUGH
/// compile audio drift correction support (very experimental)
#define USE_AUDIO_DRIFT_CORRECTION
/// compile AC-3 audio drift correction support (very experimental)
#define USE_AC3_DRIFT_CORRECTION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif

#include <fcntl.h>
#include <libintl.h>
#include <sys/stat.h>
#include <sys/types.h>
#define _(str) gettext(str) ///< gettext shortcut
#define _N(str) str         ///< gettext_noop shortcut

#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>

#include <libswresample/swresample.h>

#include <pthread.h>

// clang-format off
#include "iatomic.h"
#include "misc.h"
#include "video.h"
#include "audio.h"
#include "codec.h"
// clang-format on

//----------------------------------------------------------------------------
//  Global
//----------------------------------------------------------------------------

///
///   ffmpeg lock mutex
///
///   new ffmpeg dislikes simultanous open/close
///   this breaks our code, until this is fixed use lock.
///
static pthread_mutex_t CodecLockMutex;

/// Flag prefer fast channel switch
char CodecUsePossibleDefectFrames;
AVBufferRef *hw_device_ctx;

//----------------------------------------------------------------------------
//  Video
//----------------------------------------------------------------------------

#if 0
///
/// Video decoder typedef.
///
//typedef struct _video_decoder_ Decoder;
#endif
#if 0
///
/// Video decoder structure.
///
struct _video_decoder_
{
    VideoHwDecoder *HwDecoder;		///< video hardware decoder

    int GetFormatDone;			///< flag get format called!
    AVCodec *VideoCodec;		///< video codec
    AVCodecContext *VideoCtx;		///< video codec context
    AVFrame *Frame;			///< decoded video frame
};
#endif
//----------------------------------------------------------------------------
//  Call-backs
//----------------------------------------------------------------------------

/**
**  Callback to negotiate the PixelFormat.
**
**  @param video_ctx	codec context
**  @param fmt		is the list of formats which are supported by
**			the codec, it is terminated by -1 as 0 is a
**			valid format, the formats are ordered by
**			quality.
*/
static enum AVPixelFormat Codec_get_format(AVCodecContext *video_ctx, const enum AVPixelFormat *fmt) {
    VideoDecoder *decoder;
    enum AVPixelFormat fmt1;

    decoder = video_ctx->opaque;
    // bug in ffmpeg 1.1.1, called with zero width or height
    if (!video_ctx->width || !video_ctx->height) {
        Error("codec/video: ffmpeg/libav buggy: width or height zero\n");
    }

    //	decoder->GetFormatDone = 1;
    return Video_get_format(decoder->HwDecoder, video_ctx, fmt);
}

// static void Codec_free_buffer(void *opaque, uint8_t *data);

/**
**  Video buffer management, get buffer for frame.
**
**  Called at the beginning of each frame to get a buffer for it.
**
**  @param video_ctx	Codec context
**  @param frame	Get buffer for this frame
*/
static int Codec_get_buffer2(AVCodecContext *video_ctx, AVFrame *frame, int flags) {
    VideoDecoder *decoder;

    decoder = video_ctx->opaque;

    if (!decoder->GetFormatDone) { // get_format missing
        enum AVPixelFormat fmts[2];

        // fprintf(stderr, "codec: buggy libav, use ffmpeg\n");
        // Warning(_("codec: buggy libav, use ffmpeg\n"));
        fmts[0] = video_ctx->pix_fmt;
        fmts[1] = AV_PIX_FMT_NONE;
        Codec_get_format(video_ctx, fmts);
    }
#if 0
    if (decoder->hwaccel_get_buffer && (AV_PIX_FMT_VDPAU == decoder->hwaccel_pix_fmt
	    || AV_PIX_FMT_CUDA == decoder->hwaccel_pix_fmt || AV_PIX_FMT_VAAPI == decoder->hwaccel_pix_fmt)) {
	// Debug(3,"hwaccel get_buffer\n");
	return decoder->hwaccel_get_buffer(video_ctx, frame, flags);
    }
#endif
    // Debug(3, "codec: fallback to default get_buffer\n");
    return avcodec_default_get_buffer2(video_ctx, frame, flags);
}

//----------------------------------------------------------------------------
//  Test
//----------------------------------------------------------------------------

/**
**  Allocate a new video decoder context.
**
**  @param hw_decoder	video hardware decoder
**
**  @returns private decoder pointer for video decoder.
*/
VideoDecoder *CodecVideoNewDecoder(VideoHwDecoder *hw_decoder) {
    VideoDecoder *decoder;

    if (!(decoder = calloc(1, sizeof(*decoder)))) {
        Fatal(_("codec: can't allocate vodeo decoder\n"));
    }
    decoder->HwDecoder = hw_decoder;

    return decoder;
}

/**
**  Deallocate a video decoder context.
**
**  @param decoder  private video decoder
*/
void CodecVideoDelDecoder(VideoDecoder *decoder) { free(decoder); }

/**
**  Open video decoder.
**
**  @param decoder  private video decoder
**  @param codec_id video codec id
*/
void CodecVideoOpen(VideoDecoder *decoder, int codec_id) {
    AVCodec *video_codec;
    const char *name;
    int ret, deint = 2;

    Debug(3, "***************codec: Video Open using video codec ID %#06x (%s)\n", codec_id,
          avcodec_get_name(codec_id));

    if (decoder->VideoCtx) {
        Error(_("codec: missing close\n"));
    }

    name = "NULL";
#ifdef CUVID
    if (!strcasecmp(VideoGetDriverName(), "cuvid")) {
        switch (codec_id) {
            case AV_CODEC_ID_MPEG2VIDEO:
                name = "mpeg2_cuvid";
                break;
            case AV_CODEC_ID_H264:
                name = "h264_cuvid";
                break;
            case AV_CODEC_ID_HEVC:
                name = "hevc_cuvid";
                break;
        }
    }
#endif
    if (name && (video_codec = avcodec_find_decoder_by_name(name))) {
        Debug(3, "codec: decoder found\n");
    } else if ((video_codec = avcodec_find_decoder(codec_id)) == NULL) {
        Debug(3, "Decoder %s not supported %p\n", name, video_codec);
        Fatal(_(" No decoder found"));
    }

    decoder->VideoCodec = video_codec;

    Debug(3, "codec: video '%s'\n", decoder->VideoCodec->long_name);

    if (!(decoder->VideoCtx = avcodec_alloc_context3(video_codec))) {
        Fatal(_("codec: can't allocate video codec context\n"));
    }

    if (!HwDeviceContext) {
        Fatal("codec: no hw device context to be used");
    }
    decoder->VideoCtx->hw_device_ctx = av_buffer_ref(HwDeviceContext);

    // FIXME: for software decoder use all cpus, otherwise 1
    decoder->VideoCtx->thread_count = 1;

    decoder->VideoCtx->pkt_timebase.num = 1;
    decoder->VideoCtx->pkt_timebase.den = 90000;
    //decoder->VideoCtx->framerate.num = 50;
    //decoder->VideoCtx->framerate.den = 1;

    pthread_mutex_lock(&CodecLockMutex);
    // open codec
#ifdef YADIF
    deint = 2;
#endif

#if defined VAAPI
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59,8,100)
    // decoder->VideoCtx->extra_hw_frames = 8; // VIDEO_SURFACES_MAX +1
    if (video_codec->capabilities & (AV_CODEC_CAP_AUTO_THREADS)) {
        Debug(3, "codec: auto threads enabled");
        //	  decoder->VideoCtx->thread_count = 0;
    }

    if (video_codec->capabilities & AV_CODEC_CAP_TRUNCATED) {
        Debug(3, "codec: supports truncated packets");
        // decoder->VideoCtx->flags |= CODEC_FLAG_TRUNCATED;
    }
#endif
    // FIXME: own memory management for video frames.
    if (video_codec->capabilities & AV_CODEC_CAP_DR1) {
        Debug(3, "codec: can use own buffer management");
    }
    if (video_codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        Debug(3, "codec: supports frame threads");
        //	  decoder->VideoCtx->thread_count = 0;
        //   decoder->VideoCtx->thread_type |= FF_THREAD_FRAME;
    }
    if (video_codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
        Debug(3, "codec: supports slice threads");
        //	  decoder->VideoCtx->thread_count = 0;
        //   decoder->VideoCtx->thread_type |= FF_THREAD_SLICE;
    }
    //    if (av_opt_set_int(decoder->VideoCtx, "refcounted_frames", 1, 0) < 0)
    //	  Fatal(_("VAAPI Refcounts invalid\n"));
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59,8,100)
    decoder->VideoCtx->thread_safe_callbacks = 0;
#endif

#endif

#ifdef CUVID
    if (strcmp(decoder->VideoCodec->long_name,
               "Nvidia CUVID MPEG2VIDEO decoder") == 0) { // deinterlace for mpeg2 is somehow broken
        if (av_opt_set_int(decoder->VideoCtx->priv_data, "deint", deint, 0) < 0) { // adaptive
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option deint to video codec!\n"));
        }

        if (av_opt_set_int(decoder->VideoCtx->priv_data, "surfaces", 10, 0) < 0) {
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option surfces to video codec!\n"));
        }

        if (av_opt_set(decoder->VideoCtx->priv_data, "drop_second_field", "false", 0) < 0) {
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option drop 2.field to video codec!\n"));
        }
    } else if (strstr(decoder->VideoCodec->long_name, "Nvidia CUVID") != NULL) {
        if (av_opt_set_int(decoder->VideoCtx->priv_data, "deint", deint, 0) < 0) { // adaptive
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option deint to video codec!\n"));
        }
        if (av_opt_set_int(decoder->VideoCtx->priv_data, "surfaces", 13, 0) < 0) {
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option surfces to video codec!\n"));
        }
        if (av_opt_set(decoder->VideoCtx->priv_data, "drop_second_field", "false", 0) < 0) {
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't set option drop 2.field  to video codec!\n"));
        }
    }
#endif

    if ((ret = avcodec_open2(decoder->VideoCtx, video_codec, NULL)) < 0) {
        pthread_mutex_unlock(&CodecLockMutex);
        Fatal(_("codec: can't open video codec!\n"));
    }
    Debug(3, " Codec open %d\n", ret);

    pthread_mutex_unlock(&CodecLockMutex);

    decoder->VideoCtx->opaque = decoder; // our structure

    // decoder->VideoCtx->debug = FF_DEBUG_STARTCODE;
    // decoder->VideoCtx->err_recognition |= AV_EF_EXPLODE;

    // av_log_set_level(AV_LOG_DEBUG);
    av_log_set_level(0);

    decoder->VideoCtx->get_format = Codec_get_format;
    decoder->VideoCtx->get_buffer2 = Codec_get_buffer2;
    // decoder->VideoCtx->active_thread_type = 0;
    decoder->VideoCtx->draw_horiz_band = NULL;
    decoder->VideoCtx->hwaccel_context = VideoGetHwAccelContext(decoder->HwDecoder);

    //
    //	Prepare frame buffer for decoder
    //
#if 0
    if (!(decoder->Frame = av_frame_alloc())) {
	Fatal(_("codec: can't allocate video decoder frame buffer\n"));
    }
#endif

    // reset buggy ffmpeg/libav flag
    decoder->GetFormatDone = 0;
#if defined(YADIF)
    decoder->filter = 0;
#endif
}

/**
**  Close video decoder.
**
**  @param video_decoder    private video decoder
*/
void CodecVideoClose(VideoDecoder *video_decoder) {
    AVFrame *frame;

    // FIXME: play buffered data
    // av_frame_free(&video_decoder->Frame);   // callee does checks

    Debug(3, "CodecVideoClose\n");
    if (video_decoder->VideoCtx) {
        pthread_mutex_lock(&CodecLockMutex);
#if 1
        frame = av_frame_alloc();
        avcodec_send_packet(video_decoder->VideoCtx, NULL);
        while (avcodec_receive_frame(video_decoder->VideoCtx, frame) >= 0)
            ;
        av_frame_free(&frame);
#endif
        avcodec_close(video_decoder->VideoCtx);
        av_freep(&video_decoder->VideoCtx);
        pthread_mutex_unlock(&CodecLockMutex);
    }
}

#if 0

/**
**  Display pts...
**
**  ffmpeg-0.9 pts always AV_NOPTS_VALUE
**  ffmpeg-0.9 pkt_pts nice monotonic (only with HD)
**  ffmpeg-0.9 pkt_dts wild jumping -160 - 340 ms
**
**  libav 0.8_pre20111116 pts always AV_NOPTS_VALUE
**  libav 0.8_pre20111116 pkt_pts always 0 (could be fixed?)
**  libav 0.8_pre20111116 pkt_dts wild jumping -160 - 340 ms
*/
void DisplayPts(AVCodecContext * video_ctx, AVFrame * frame)
{
    int ms_delay;
    int64_t pts;
    static int64_t last_pts;

    pts = frame->pkt_pts;
    if (pts == (int64_t) AV_NOPTS_VALUE) {
	printf("*");
    }
    ms_delay = (1000 * video_ctx->time_base.num) / video_ctx->time_base.den;
    ms_delay += frame->repeat_pict * ms_delay / 2;
    printf("codec: PTS %s%s %" PRId64 " %d %d/%d %d/%d	%dms\n", frame->repeat_pict ? "r" : " ",
	frame->interlaced_frame ? "I" : " ", pts, (int)(pts - last_pts) / 90, video_ctx->time_base.num,
	video_ctx->time_base.den, video_ctx->framerate.num, video_ctx->framerate.den, ms_delay);

    if (pts != (int64_t) AV_NOPTS_VALUE) {
	last_pts = pts;
    }
}

#endif

/**
**  Decode a video packet.
**
**  @param decoder  video decoder data
**  @param avpkt    video packet
*/
extern int CuvidTestSurfaces();

#if defined YADIF || defined(VAAPI)
extern int init_filters(AVCodecContext *dec_ctx, void *decoder, AVFrame *frame);
extern int push_filters(AVCodecContext *dec_ctx, void *decoder, AVFrame *frame);
#endif

#ifdef VAAPI
void CodecVideoDecode(VideoDecoder *decoder, const AVPacket *avpkt) {
    AVCodecContext *video_ctx = decoder->VideoCtx;

    if (video_ctx->codec_type == AVMEDIA_TYPE_VIDEO && CuvidTestSurfaces()) {
        int ret;
        AVPacket pkt[1];
        AVFrame *frame;

        *pkt = *avpkt; // use copy
        ret = avcodec_send_packet(video_ctx, pkt);
        if (ret < 0) {
            return;
        }

        if (!CuvidTestSurfaces())
            usleep(1000);

        ret = 0;
        while (ret >= 0 && CuvidTestSurfaces()) {
            frame = av_frame_alloc();
            ret = avcodec_receive_frame(video_ctx, frame);

            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                Debug(4, "codec: receiving video frame failed");
                av_frame_free(&frame);
                return;
            }
            if (ret >= 0) {
                if (decoder->filter) {
                    if (decoder->filter == 1) {
                        if (init_filters(video_ctx, decoder->HwDecoder, frame) < 0) {
                            Debug(3, "video: Init of VAAPI deint Filter failed\n");
                            decoder->filter = 0;
                        } else {
                            Debug(3, "Init VAAPI deint ok\n");
                            decoder->filter = 2;
                        }
                    }
                    if (decoder->filter == 2) { 
                        push_filters(video_ctx, decoder->HwDecoder, frame);
                        continue;
                    }
                }
                VideoRenderFrame(decoder->HwDecoder, video_ctx, frame);
            } else {
                av_frame_free(&frame);
                return;
            }
        }
    }
}
#endif

#ifdef CUVID

void CodecVideoDecode(VideoDecoder *decoder, const AVPacket *avpkt) {
    AVCodecContext *video_ctx;
    AVFrame *frame;
    int ret, ret1;
    int got_frame;
    int consumed = 0;
    static uint64_t first_time = 0;
    const AVPacket *pkt;

next_part:
    video_ctx = decoder->VideoCtx;

    pkt = avpkt; // use copy
    got_frame = 0;

    ret1 = avcodec_send_packet(video_ctx, pkt);

    // first_time = GetusTicks();

    if (ret1 >= 0) {
        consumed = 1;
    }

    if (!CuvidTestSurfaces())
        usleep(1000);

    //     printf("send packet to decode %s %04x\n",consumed?"ok":"Full",ret1);

    if ((ret1 == AVERROR(EAGAIN) || ret1 == AVERROR_EOF || ret1 >= 0) && CuvidTestSurfaces()) {
        ret = 0;
        while ((ret >= 0) && CuvidTestSurfaces()) { // get frames until empty snd Surfaces avail.
            frame = av_frame_alloc();
            ret = avcodec_receive_frame(video_ctx, frame); // get new frame
            if (ret >= 0) {                                // one is avail.
            first_time = frame->pts;
                got_frame = 1;
            } else {
                got_frame = 0;
            }
            //printf("got %s packet from decoder\n",got_frame?"1":"no");
            if (got_frame) { // frame completed
//		printf("video frame pts %#012" PRIx64 "
//%dms\n",frame->pts,(int)(apts - frame->pts) / 90);
#ifdef YADIF
                if (decoder->filter) {
                    if (decoder->filter == 1) {
                        if (init_filters(video_ctx, decoder->HwDecoder, frame) < 0) {
                            Debug(3,"video: Init of YADIF Filter failed\n");
                            decoder->filter = 0;
                        } else {
                            Debug(3, "Init YADIF ok\n");
                            decoder->filter = 2;
                        }
                    }
                    if (decoder->filter == 2) { 
                        ret = push_filters(video_ctx, decoder->HwDecoder, frame);
                        // av_frame_unref(frame);
                        continue;
                    }
                }
#endif
                //  DisplayPts(video_ctx, frame);
                VideoRenderFrame(decoder->HwDecoder, video_ctx, frame);
                // av_frame_unref(frame);
            } else {
                av_frame_free(&frame);
                // printf("codec: got no frame %d  send %d\n",ret,ret1);
            }
        }
        if (!CuvidTestSurfaces()) {
            usleep(1000);
        }
    } else {
        // consumed = 1;
    }

    if (!consumed) {
        goto next_part; // try again to stuff decoder
    }
}
#endif

/**
**  Flush the video decoder.
**
**  @param decoder  video decoder data
*/
void CodecVideoFlushBuffers(VideoDecoder *decoder) {
    if (decoder->VideoCtx) {
        avcodec_flush_buffers(decoder->VideoCtx);
    }
}

//----------------------------------------------------------------------------
//  Audio
//----------------------------------------------------------------------------

#if 0
///
/// Audio decoder typedef.
///
typedef struct _audio_decoder_ AudioDecoder;
#endif

///
/// Audio decoder structure.
///
struct _audio_decoder_ {
    AVCodec *AudioCodec;      ///< audio codec
    AVCodecContext *AudioCtx; ///< audio codec context

    char Passthrough; ///< current pass-through flags
    int SampleRate;   ///< current stream sample rate
    int Channels;     ///< current stream channels

    int HwSampleRate; ///< hw sample rate
    int HwChannels;   ///< hw channels

    AVFrame *Frame;       ///< decoded audio frame buffer
    SwrContext *Resample; ///< ffmpeg software resample context

    uint16_t Spdif[24576 / 2]; ///< SPDIF output buffer
    int SpdifIndex;            ///< index into SPDIF output buffer
    int SpdifCount;            ///< SPDIF repeat counter

    int64_t LastDelay;        ///< last delay
    struct timespec LastTime; ///< last time
    int64_t LastPTS;          ///< last PTS

    int Drift;     ///< accumulated audio drift
    int DriftCorr; ///< audio drift correction value
    int DriftFrac; ///< audio drift fraction for ac3
};

///
/// IEC Data type enumeration.
///
enum IEC61937 {
    IEC61937_AC3 = 0x01, ///< AC-3 data
    // FIXME: more data types
    IEC61937_EAC3 = 0x15, ///< E-AC-3 data
};

#ifdef USE_AUDIO_DRIFT_CORRECTION
#define CORRECT_PCM 1        ///< do PCM audio-drift correction
#define CORRECT_AC3 2        ///< do AC-3 audio-drift correction
static char CodecAudioDrift; ///< flag: enable audio-drift correction
#else
static const int CodecAudioDrift = 0;
#endif
#ifdef USE_PASSTHROUGH
///
/// Pass-through flags: CodecPCM, CodecAC3, CodecEAC3, ...
///
static char CodecPassthrough;
#else
static const int CodecPassthrough = 0;
#endif
static char CodecDownmix; ///< enable AC-3 decoder downmix

/**
**  Allocate a new audio decoder context.
**
**  @returns private decoder pointer for audio decoder.
*/
AudioDecoder *CodecAudioNewDecoder(void) {
    AudioDecoder *audio_decoder;

    if (!(audio_decoder = calloc(1, sizeof(*audio_decoder)))) {
        Fatal(_("codec: can't allocate audio decoder\n"));
    }
    if (!(audio_decoder->Frame = av_frame_alloc())) {
        Fatal(_("codec: can't allocate audio decoder frame buffer\n"));
    }

    return audio_decoder;
}

/**
**  Deallocate an audio decoder context.
**
**  @param decoder  private audio decoder
*/
void CodecAudioDelDecoder(AudioDecoder *decoder) {
    av_frame_free(&decoder->Frame); // callee does checks
    free(decoder);
}

/**
**  Open audio decoder.
**
**  @param audio_decoder    private audio decoder
**  @param codec_id audio   codec id
*/
void CodecAudioOpen(AudioDecoder *audio_decoder, int codec_id) {
    AVCodec *audio_codec;

    Debug(3, "codec: using audio codec ID %#06x (%s)\n", codec_id, avcodec_get_name(codec_id));
    if (!(audio_codec = avcodec_find_decoder(codec_id))) {
        // if (!(audio_codec = avcodec_find_decoder(codec_id))) {
        Fatal(_("codec: codec ID %#06x not found\n"), codec_id);
        // FIXME: errors aren't fatal
    }
    audio_decoder->AudioCodec = audio_codec;

    if (!(audio_decoder->AudioCtx = avcodec_alloc_context3(audio_codec))) {
        Fatal(_("codec: can't allocate audio codec context\n"));
    }

    if (CodecDownmix) {
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53,61,100)
	    audio_decoder->AudioCtx->request_channels = 2;
#endif
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59,24,100)
	    audio_decoder->AudioCtx->request_channel_layout = AV_CH_LAYOUT_STEREO;
#else
        AVChannelLayout dmlayout = AV_CHANNEL_LAYOUT_STEREO;
        av_opt_set_chlayout(audio_decoder->AudioCtx->priv_data, "downmix", &dmlayout, 0);
#endif
    }
    pthread_mutex_lock(&CodecLockMutex);
    // open codec
    if (1) {
        AVDictionary *av_dict;

        av_dict = NULL;
        // FIXME: import settings
        // av_dict_set(&av_dict, "dmix_mode", "0", 0);
        // av_dict_set(&av_dict, "ltrt_cmixlev", "1.414", 0);
        // av_dict_set(&av_dict, "loro_cmixlev", "1.414", 0);
        if (avcodec_open2(audio_decoder->AudioCtx, audio_codec, &av_dict) < 0) {
            pthread_mutex_unlock(&CodecLockMutex);
            Fatal(_("codec: can't open audio codec\n"));
        }
        av_dict_free(&av_dict);
    }
    pthread_mutex_unlock(&CodecLockMutex);
    Debug(3, "codec: audio '%s'\n", audio_decoder->AudioCodec->long_name);

    audio_decoder->SampleRate = 0;
    audio_decoder->Channels = 0;
    audio_decoder->HwSampleRate = 0;
    audio_decoder->HwChannels = 0;
    audio_decoder->LastDelay = 0;
}

/**
**  Close audio decoder.
**
**  @param audio_decoder    private audio decoder
*/
void CodecAudioClose(AudioDecoder *audio_decoder) {
    // FIXME: output any buffered data

    if (audio_decoder->Resample) {
        swr_free(&audio_decoder->Resample);
    }
    if (audio_decoder->AudioCtx) {
        pthread_mutex_lock(&CodecLockMutex);
        avcodec_close(audio_decoder->AudioCtx);
        av_freep(&audio_decoder->AudioCtx);
        pthread_mutex_unlock(&CodecLockMutex);
    }
}

/**
**  Set audio drift correction.
**
**  @param mask enable mask (PCM, AC-3)
*/
void CodecSetAudioDrift(int mask) {
#ifdef USE_AUDIO_DRIFT_CORRECTION
    CodecAudioDrift = mask & (CORRECT_PCM | CORRECT_AC3);
#endif
    (void)mask;
}

/**
**  Set audio pass-through.
**
**  @param mask enable mask (PCM, AC-3, E-AC-3)
*/
void CodecSetAudioPassthrough(int mask) {
#ifdef USE_PASSTHROUGH
    CodecPassthrough = mask & (CodecPCM | CodecAC3 | CodecEAC3);
#endif
    (void)mask;
}

/**
**  Set audio downmix.
**
**  @param onoff    enable/disable downmix.
*/
void CodecSetAudioDownmix(int onoff) {
    if (onoff == -1) {
        CodecDownmix ^= 1;
        return;
    }
    CodecDownmix = onoff;
}

/**
**  Reorder audio frame.
**
**  ffmpeg L  R	 C  Ls Rs	-> alsa L R  Ls Rs C
**  ffmpeg L  R	 C  LFE Ls Rs	-> alsa L R  Ls Rs C  LFE
**  ffmpeg L  R	 C  LFE Ls Rs Rl Rr -> alsa L R	 Ls Rs C  LFE Rl Rr
**
**  @param buf[IN,OUT]	sample buffer
**  @param size		size of sample buffer in bytes
**  @param channels	number of channels interleaved in sample buffer
*/
static void CodecReorderAudioFrame(int16_t *buf, int size, int channels) {
    int i;
    int c;
    int ls;
    int rs;
    int lfe;

    switch (channels) {
        case 5:
            size /= 2;
            for (i = 0; i < size; i += 5) {
                c = buf[i + 2];
                ls = buf[i + 3];
                rs = buf[i + 4];
                buf[i + 2] = ls;
                buf[i + 3] = rs;
                buf[i + 4] = c;
            }
            break;
        case 6:
            size /= 2;
            for (i = 0; i < size; i += 6) {
                c = buf[i + 2];
                lfe = buf[i + 3];
                ls = buf[i + 4];
                rs = buf[i + 5];
                buf[i + 2] = ls;
                buf[i + 3] = rs;
                buf[i + 4] = c;
                buf[i + 5] = lfe;
            }
            break;
        case 8:
            size /= 2;
            for (i = 0; i < size; i += 8) {
                c = buf[i + 2];
                lfe = buf[i + 3];
                ls = buf[i + 4];
                rs = buf[i + 5];
                buf[i + 2] = ls;
                buf[i + 3] = rs;
                buf[i + 4] = c;
                buf[i + 5] = lfe;
            }
            break;
    }
}

/**
**  Handle audio format changes helper.
**
**  @param audio_decoder    audio decoder data
**  @param[out] passthrough pass-through output
*/
static int CodecAudioUpdateHelper(AudioDecoder *audio_decoder, int *passthrough) {
    const AVCodecContext *audio_ctx;
    int err;

    audio_ctx = audio_decoder->AudioCtx;
    Debug(3, "codec/audio: Chanlayout %lx format change %s %dHz *%d channels%s%s%s%s%s\n",audio_ctx->channel_layout,
          av_get_sample_fmt_name(audio_ctx->sample_fmt), audio_ctx->sample_rate, audio_ctx->channels,
          CodecPassthrough & CodecPCM ? " PCM" : "", CodecPassthrough & CodecMPA ? " MPA" : "",
          CodecPassthrough & CodecAC3 ? " AC-3" : "", CodecPassthrough & CodecEAC3 ? " E-AC-3" : "",
          CodecPassthrough ? " pass-through" : "");

    *passthrough = 0;
    audio_decoder->SampleRate = audio_ctx->sample_rate;
    audio_decoder->HwSampleRate = audio_ctx->sample_rate;
    audio_decoder->Channels = audio_ctx->channels;
    audio_decoder->HwChannels = audio_ctx->channels;
    audio_decoder->Passthrough = CodecPassthrough;

    // SPDIF/HDMI pass-through
    if ((CodecPassthrough & CodecAC3 && audio_ctx->codec_id == AV_CODEC_ID_AC3) ||
        (CodecPassthrough & CodecEAC3 && audio_ctx->codec_id == AV_CODEC_ID_EAC3)) {
        if (audio_ctx->codec_id == AV_CODEC_ID_EAC3) {
            // E-AC-3 over HDMI some receivers need HBR
            //audio_decoder->HwSampleRate *= 4;
        }
        audio_decoder->HwChannels = 2;
        audio_decoder->SpdifIndex = 0; // reset buffer
        audio_decoder->SpdifCount = 0;
        *passthrough = 1;
    }
    
    if (audio_decoder->HwChannels > 2 && CodecDownmix) {
        audio_decoder->HwChannels = 2;
    }
    // channels/sample-rate not support?
    if ((err = AudioSetup(&audio_decoder->HwSampleRate, &audio_decoder->HwChannels, *passthrough))) {

        // try E-AC-3 none HBR
        audio_decoder->HwSampleRate /= 4;
        if (audio_ctx->codec_id != AV_CODEC_ID_EAC3 ||
            (err = AudioSetup(&audio_decoder->HwSampleRate, &audio_decoder->HwChannels, *passthrough))) {

            Debug(3, "codec/audio: audio setup error\n");
            // FIXME: handle errors
            audio_decoder->HwChannels = 0;
            audio_decoder->HwSampleRate = 0;
            return err;
        }
    }

    Debug(3, "codec/audio: resample %s %dHz *%d -> %s %dHz *%d\n", av_get_sample_fmt_name(audio_ctx->sample_fmt),
          audio_ctx->sample_rate, audio_ctx->channels, av_get_sample_fmt_name(AV_SAMPLE_FMT_S16),
          audio_decoder->HwSampleRate, audio_decoder->HwChannels);

    return 0;
}

/**
**  Audio pass-through decoder helper.
**
**  @param audio_decoder    audio decoder data
**  @param avpkt	    undecoded audio packet
*/
static int CodecAudioPassthroughHelper(AudioDecoder *audio_decoder, const AVPacket *avpkt) {
#ifdef USE_PASSTHROUGH
    const AVCodecContext *audio_ctx;

    audio_ctx = audio_decoder->AudioCtx;
    // SPDIF/HDMI passthrough
    if (CodecPassthrough & CodecAC3 && audio_ctx->codec_id == AV_CODEC_ID_AC3) {
        uint16_t *spdif;
        int spdif_sz;

        spdif = audio_decoder->Spdif;
        spdif_sz = 6144;

#ifdef USE_AC3_DRIFT_CORRECTION
        // FIXME: this works with some TVs/AVReceivers
        // FIXME: write burst size drift correction, which should work with all
        if (CodecAudioDrift & CORRECT_AC3) {
            int x;

            x = (audio_decoder->DriftFrac + (audio_decoder->DriftCorr * spdif_sz)) /
                (10 * audio_decoder->HwSampleRate * 100);
            audio_decoder->DriftFrac = (audio_decoder->DriftFrac + (audio_decoder->DriftCorr * spdif_sz)) %
                                       (10 * audio_decoder->HwSampleRate * 100);
            // round to word border
            x *= audio_decoder->HwChannels * 4;
            if (x < -64) { // limit correction
                x = -64;
            } else if (x > 64) {
                x = 64;
            }
            spdif_sz += x;
        }
#endif

        // build SPDIF header and append A52 audio to it
        // avpkt is the original data
        if (spdif_sz < avpkt->size + 8) {
            Error(_("codec/audio: decoded data smaller than encoded\n"));
            return -1;
        }
        spdif[0] = htole16(0xF872); // iec 61937 sync word
        spdif[1] = htole16(0x4E1F);
        spdif[2] = htole16(IEC61937_AC3 | (avpkt->data[5] & 0x07) << 8);
        spdif[3] = htole16(avpkt->size * 8);
        // copy original data for output
        // FIXME: not 100% sure, if endian is correct on not intel hardware
        swab(avpkt->data, spdif + 4, avpkt->size);
        // FIXME: don't need to clear always
        memset(spdif + 4 + avpkt->size / 2, 0, spdif_sz - 8 - avpkt->size);
        // don't play with the ac-3 samples
        AudioEnqueue(spdif, spdif_sz);
        return 1;
    }
    if (CodecPassthrough & CodecEAC3 && audio_ctx->codec_id == AV_CODEC_ID_EAC3) {
        uint16_t *spdif;
        int spdif_sz;
        int repeat;

        // build SPDIF header and append A52 audio to it
        // avpkt is the original data
        spdif = audio_decoder->Spdif;
        spdif_sz = 24576; // 4 * 6144
        if (audio_decoder->HwSampleRate == 48000) {
            spdif_sz = 6144;
        }
        if (spdif_sz < audio_decoder->SpdifIndex + avpkt->size + 8) {
            Error(_("codec/audio: decoded data smaller than encoded\n"));
            return -1;
        }
        // check if we must pack multiple packets
        repeat = 1;
        if ((avpkt->data[4] & 0xc0) != 0xc0) { // fscod
            static const uint8_t eac3_repeat[4] = {6, 3, 2, 1};

            // fscod2
            repeat = eac3_repeat[(avpkt->data[4] & 0x30) >> 4];
        }
        // fprintf(stderr, "repeat %d %d\n", repeat, avpkt->size);

        // copy original data for output
        // pack upto repeat EAC-3 pakets into one IEC 61937 burst
        // FIXME: not 100% sure, if endian is correct on not intel hardware
        swab(avpkt->data, spdif + 4 + audio_decoder->SpdifIndex, avpkt->size);
        audio_decoder->SpdifIndex += avpkt->size;
        if (++audio_decoder->SpdifCount < repeat) {
            return 1;
        }

        spdif[0] = htole16(0xF872); // iec 61937 sync word
        spdif[1] = htole16(0x4E1F);
        spdif[2] = htole16(IEC61937_EAC3);
        spdif[3] = htole16(audio_decoder->SpdifIndex * 8);
        memset(spdif + 4 + audio_decoder->SpdifIndex / 2, 0, spdif_sz - 8 - audio_decoder->SpdifIndex);

        // don't play with the eac-3 samples
        AudioEnqueue(spdif, spdif_sz);

        audio_decoder->SpdifIndex = 0;
        audio_decoder->SpdifCount = 0;
        return 1;
    }
#endif
    return 0;
}

/**
**  Set/update audio pts clock.
**
**  @param audio_decoder    audio decoder data
**  @param pts		    presentation timestamp
*/
static void CodecAudioSetClock(AudioDecoder *audio_decoder, int64_t pts) {
#ifdef USE_AUDIO_DRIFT_CORRECTION
    struct timespec nowtime;
    int64_t delay;
    int64_t tim_diff;
    int64_t pts_diff;
    int drift;
    int corr;

    AudioSetClock(pts);

    delay = AudioGetDelay();
    if (!delay) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &nowtime);
    if (!audio_decoder->LastDelay) {
        audio_decoder->LastTime = nowtime;
        audio_decoder->LastPTS = pts;
        audio_decoder->LastDelay = delay;
        audio_decoder->Drift = 0;
        audio_decoder->DriftFrac = 0;
        Debug(3, "codec/audio: inital drift delay %" PRId64 "ms\n", delay / 90);
        return;
    }
    // collect over some time
    pts_diff = pts - audio_decoder->LastPTS;
    if (pts_diff < 10 * 1000 * 90) {
        return;
    }

    tim_diff = (nowtime.tv_sec - audio_decoder->LastTime.tv_sec) * 1000 * 1000 * 1000 +
               (nowtime.tv_nsec - audio_decoder->LastTime.tv_nsec);

    drift = (tim_diff * 90) / (1000 * 1000) - pts_diff + delay - audio_decoder->LastDelay;

    // adjust rounding error
    nowtime.tv_nsec -= nowtime.tv_nsec % (1000 * 1000 / 90);
    audio_decoder->LastTime = nowtime;
    audio_decoder->LastPTS = pts;
    audio_decoder->LastDelay = delay;

    if (0) {
        Debug(3, "codec/audio: interval P:%5" PRId64 "ms T:%5" PRId64 "ms D:%4" PRId64 "ms %f %d\n", pts_diff / 90,
              tim_diff / (1000 * 1000), delay / 90, drift / 90.0, audio_decoder->DriftCorr);
    }
    // underruns and av_resample have the same time :(((
    if (abs(drift) > 10 * 90) {
        // drift too big, pts changed?
        Debug(3, "codec/audio: drift(%6d) %3dms reset\n", audio_decoder->DriftCorr, drift / 90);
        audio_decoder->LastDelay = 0;
#ifdef DEBUG
        corr = 0; // keep gcc happy
#endif
    } else {

        drift += audio_decoder->Drift;
        audio_decoder->Drift = drift;
        corr = (10 * audio_decoder->HwSampleRate * drift) / (90 * 1000);
        // SPDIF/HDMI passthrough
        if ((CodecAudioDrift & CORRECT_AC3) &&
            (!(CodecPassthrough & CodecAC3) || audio_decoder->AudioCtx->codec_id != AV_CODEC_ID_AC3) &&
            (!(CodecPassthrough & CodecEAC3) || audio_decoder->AudioCtx->codec_id != AV_CODEC_ID_EAC3)) {
            audio_decoder->DriftCorr = -corr;
        }

        if (audio_decoder->DriftCorr < -20000) { // limit correction
            audio_decoder->DriftCorr = -20000;
        } else if (audio_decoder->DriftCorr > 20000) {
            audio_decoder->DriftCorr = 20000;
        }
    }

    if (audio_decoder->Resample && audio_decoder->DriftCorr) {
        int distance;

        // try workaround for buggy ffmpeg 0.10
        if (abs(audio_decoder->DriftCorr) < 2000) {
            distance = (pts_diff * audio_decoder->HwSampleRate) / (900 * 1000);
        } else {
            distance = (pts_diff * audio_decoder->HwSampleRate) / (90 * 1000);
        }
        if (swr_set_compensation(audio_decoder->Resample, audio_decoder->DriftCorr / 10, distance)) {
            Debug(3, "codec/audio: swr_set_compensation failed\n");
        }
    }
    if (1) {
        static int c;

        if (!(c++ % 10)) {
            Debug(3, "codec/audio: drift(%6d) %8dus %5d\n", audio_decoder->DriftCorr, drift * 1000 / 90, corr);
        }
    }
#else
    AudioSetClock(pts);
#endif
}

/**
**  Handle audio format changes.
**
**  @param audio_decoder    audio decoder data
*/
static void CodecAudioUpdateFormat(AudioDecoder *audio_decoder) {
    int passthrough;
    const AVCodecContext *audio_ctx;

    if (CodecAudioUpdateHelper(audio_decoder, &passthrough)) {
        // FIXME: handle swresample format conversions.
        return;
    }
    if (passthrough) { // pass-through no conversion allowed
        return;
    }

    audio_ctx = audio_decoder->AudioCtx;

#ifdef DEBUG
    if (audio_ctx->sample_fmt == AV_SAMPLE_FMT_S16 && audio_ctx->sample_rate == audio_decoder->HwSampleRate &&
        !CodecAudioDrift) {
        // FIXME: use Resample only, when it is needed!
        fprintf(stderr, "no resample needed\n");
    }
#endif

#if LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4,5,100)
    if (audio_decoder->Channels > 2 && CodecDownmix) { 
        audio_decoder->Resample = swr_alloc_set_opts(audio_decoder->Resample, 
                                    AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, audio_decoder->HwSampleRate,
	                                audio_ctx->channel_layout, audio_ctx->sample_fmt,audio_ctx->sample_rate,
                                    0, NULL);
    } else {
        audio_decoder->Resample = swr_alloc_set_opts(audio_decoder->Resample, audio_ctx->channel_layout,
	                                            AV_SAMPLE_FMT_S16, audio_decoder->HwSampleRate,
	                                            audio_ctx->channel_layout, audio_ctx->sample_fmt,
                                                audio_ctx->sample_rate, 0, NULL);
    }
#else
    if (audio_decoder->Channels > 2 && CodecDownmix) {  // Codec does not Support Downmix
    //printf("last ressort downmix Layout in %lx Lyout out: %llx \n",audio_ctx->channel_layout,AV_CH_LAYOUT_STEREO);
        audio_decoder->Resample = swr_alloc();
        av_opt_set_channel_layout(audio_decoder->Resample, "in_channel_layout",audio_ctx->channel_layout, 0);
        av_opt_set_channel_layout(audio_decoder->Resample, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
        av_opt_set_int(audio_decoder->Resample, "in_sample_rate",     audio_ctx->sample_rate,                0);
        av_opt_set_int(audio_decoder->Resample, "out_sample_rate",    audio_ctx->sample_rate,                0);
        av_opt_set_sample_fmt(audio_decoder->Resample, "in_sample_fmt",  audio_ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(audio_decoder->Resample, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    }
    else {
        swr_alloc_set_opts2(&audio_decoder->Resample, &audio_ctx->ch_layout,
                            AV_SAMPLE_FMT_S16, audio_decoder->HwSampleRate,
                            &audio_ctx->ch_layout, audio_ctx->sample_fmt,
                            audio_ctx->sample_rate, 0, NULL);
    }
#endif
    if (audio_decoder->Resample) {
	    swr_init(audio_decoder->Resample);
    } else {
	    Error(_("codec/audio: can't setup resample\n"));
    }


}

/**
**  Decode an audio packet.
**
**  PTS must be handled self.
**
**  @note the caller has not aligned avpkt and not cleared the end.
**
**  @param audio_decoder    audio decoder data
**  @param avpkt	    audio packet
*/

void CodecAudioDecode(AudioDecoder *audio_decoder, const AVPacket *avpkt) {
    AVCodecContext *audio_ctx = audio_decoder->AudioCtx;

    if (audio_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        int ret;
        AVPacket pkt[1];
        AVFrame *frame = audio_decoder->Frame;

        av_frame_unref(frame);
        *pkt = *avpkt; // use copy
        ret = avcodec_send_packet(audio_ctx, pkt);
        if (ret < 0) {
            Debug(3, "codec: sending audio packet failed");
            return;
        }
        ret = avcodec_receive_frame(audio_ctx, frame);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            Debug(3, "codec: receiving audio frame failed");
            return;
        }

        if (ret >= 0) {
            // update audio clock
            if (avpkt->pts != (int64_t)AV_NOPTS_VALUE) {
                CodecAudioSetClock(audio_decoder, avpkt->pts);
            }
            // format change
            if (audio_decoder->Passthrough != CodecPassthrough ||
                audio_decoder->SampleRate != audio_ctx->sample_rate ||
                audio_decoder->Channels != audio_ctx->channels) {
                CodecAudioUpdateFormat(audio_decoder);
            }
            if (!audio_decoder->HwSampleRate || !audio_decoder->HwChannels) {
                return; // unsupported sample format
            }
            if (CodecAudioPassthroughHelper(audio_decoder, avpkt)) {
                return;
            }
            
            if (audio_decoder->Resample) {
                uint8_t outbuf[8192  * 2 * 8];
                uint8_t *out[1];

                out[0] = outbuf;
                ret = swr_convert(audio_decoder->Resample, out, sizeof(outbuf) / (2 * audio_decoder->HwChannels),
                                  (const uint8_t **)frame->extended_data, frame->nb_samples);
                                  
                if (ret > 0) {
                    if (!(audio_decoder->Passthrough & CodecPCM)) {
                        CodecReorderAudioFrame((int16_t *)outbuf, ret * 2 * audio_decoder->HwChannels,
                                               audio_decoder->HwChannels);
                    }
                    AudioEnqueue(outbuf, ret * 2 * audio_decoder->HwChannels);
                }
                return;
            }
        }
    }
}

/**
**  Flush the audio decoder.
**
**  @param decoder  audio decoder data
*/
void CodecAudioFlushBuffers(AudioDecoder *decoder) { avcodec_flush_buffers(decoder->AudioCtx); }

//----------------------------------------------------------------------------
//  Codec
//----------------------------------------------------------------------------

/**
**  Empty log callback
*/
static void CodecNoopCallback(__attribute__((unused)) void *ptr, __attribute__((unused)) int level,
                              __attribute__((unused)) const char *fmt, __attribute__((unused)) va_list vl) {}

/**
**  Codec init
*/
void CodecInit(void) {
    pthread_mutex_init(&CodecLockMutex, NULL);
#ifndef DEBUG
    // disable display ffmpeg error messages
    av_log_set_callback(CodecNoopCallback);
#else
    (void)CodecNoopCallback;
#endif
}

/**
**  Codec exit.
*/
void CodecExit(void) { pthread_mutex_destroy(&CodecLockMutex); }
