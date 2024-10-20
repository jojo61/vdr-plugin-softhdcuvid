///
/// @file video.c   @brief Video module
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
/// $Id: bacf89f24503be74d113a83139a277ff2290014a $
//////////////////////////////////////////////////////////////////////////////

///
/// @defgroup Video The video module.
///
/// This module contains all video rendering functions.
///
/// @todo disable screen saver support
///
/// Uses Xlib where it is needed for VA-API or cuvid.  XCB is used for
/// everything else.
///
/// - X11
/// - OpenGL rendering
/// - OpenGL rendering with GLX texture-from-pixmap
/// - Xrender rendering
///
/// @todo FIXME: use vaErrorStr for all VA-API errors.
///

#define USE_XLIB_XCB      ///< use xlib/xcb backend
#define noUSE_SCREENSAVER ///< support disable screensaver

#define USE_GRAB ///< experimental grab code
// #define USE_GLX	 ///< outdated GLX code
#define USE_DOUBLEBUFFER ///< use GLX double buffers
#define USE_CUVID        ///< enable cuvid support
// #define AV_INFO	 ///< log a/v sync informations
#ifndef AV_INFO_TIME
#define AV_INFO_TIME (50 * 60) ///< a/v info every minute
#endif

#define USE_VIDEO_THREAD ///< run decoder in an own thread

#include <stdio.h>
#include <sys/ipc.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <errno.h>     /* ERROR Number Definitions	  */
#include <fcntl.h>     /* File Control Definitions	  */
#include <sys/ioctl.h> /* ioctl()   */
#include <termios.h>   /* POSIX Terminal Control Definitions */
#include <unistd.h>    /* UNIX Standard Definitions	      */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libintl.h>
#define _(str) gettext(str) ///< gettext shortcut
#define _N(str) str         ///< gettext_noop shortcut

#ifdef USE_VIDEO_THREAD
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <pthread.h>
#include <signal.h>
#include <time.h>
#ifndef HAVE_PTHREAD_NAME
/// only available with newer glibc
#define pthread_setname_np(thread, name)
#endif
#endif

#ifdef USE_XLIB_XCB
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <xcb/xcb.h>

#ifdef USE_SCREENSAVER
#include <xcb/dpms.h>
#include <xcb/screensaver.h>
#endif

// #include <xcb/shm.h>
// #include <xcb/xv.h>

// #include <xcb/xcb_image.h>
// #include <xcb/xcb_event.h>
// #include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
#include <xcb/xcb_ewmh.h>
#else // compatibility hack for old xcb-util

/**
 * @brief Action on the _NET_WM_STATE property
 */
typedef enum {
    /* Remove/unset property */
    XCB_EWMH_WM_STATE_REMOVE = 0,
    /* Add/set property */
    XCB_EWMH_WM_STATE_ADD = 1,
    /* Toggle property	*/
    XCB_EWMH_WM_STATE_TOGGLE = 2
} xcb_ewmh_wm_state_action_t;
#endif
#endif

#ifdef USE_GLX
#ifndef PLACEBO_GL
#include <GL/glew.h>
#else
#include <epoxy/egl.h>
#endif
// clang-format off
#include <GL/glu.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>
// clang-format on
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>

#ifdef CUVID
// clang-format off
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_loader.h>
#include <libavutil/hwcontext_cuda.h>
#include "drvapi_error_string.h"
// clang-format on
#define __DEVICE_TYPES_H__
#endif

#ifdef VAAPI
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 74, 100)
#include <libavcodec/vaapi.h>
#endif
#include <libavutil/hwcontext_vaapi.h>
#include <libdrm/drm_fourcc.h>
#include <va/va_drmcommon.h>
#define TO_AVHW_DEVICE_CTX(x) ((AVHWDeviceContext *)x->data)
#define TO_AVHW_FRAMES_CTX(x) ((AVHWFramesContext *)x->data)
#define TO_VAAPI_DEVICE_CTX(x) ((AVVAAPIDeviceContext *)TO_AVHW_DEVICE_CTX(x)->hwctx)
#define TO_VAAPI_FRAMES_CTX(x) ((AVVAAPIFramesContext *)TO_AVHW_FRAMES_CTX(x)->hwctx)
#endif

#include <assert.h>
// #define EGL_EGLEXT_PROTOTYPES
#if !defined PLACEBO_GL
#include <EGL/egl.h>
#include <EGL/eglext.h>

#endif

#ifndef GL_OES_EGL_image
typedef void *GLeglImageOES;
#endif
#ifndef EGL_KHR_image
typedef void *EGLImageKHR;
#endif

#ifdef PLACEBO
#ifdef PLACEBO_GL
GLenum glewInit(void);
#include <libplacebo/opengl.h>
#else
#define VK_USE_PLATFORM_XCB_KHR
#include <libplacebo/vulkan.h>
#endif
#if PL_API_VER >= 113
#include <libplacebo/shaders/lut.h>
#endif
#include <libplacebo/renderer.h>
#endif

#include <libswscale/swscale.h>

#if defined(YADIF) || defined(VAAPI)
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#endif

// clang-format off
#include "iatomic.h" // portable atomic_t
#include "misc.h"
#include "video.h"
#include "audio.h"
#include "codec.h"
// clang-format on

#if defined(APIVERSNUM) && APIVERSNUM < 20400
#error "VDR 2.4.0 or greater is required!"
#endif

#define HAS_FFMPEG_3_4_API (LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 107, 100))
#define HAS_FFMPEG_4_API (LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100))

#if !HAS_FFMPEG_3_4_API
#error "FFmpeg 3.4 or greater is required!"
#endif

//----------------------------------------------------------------------------
//  Declarations
//----------------------------------------------------------------------------

///
/// Video resolutions selector.
///
typedef enum _video_resolutions_ {
    VideoResolution576i,      ///< ...x576 interlaced
    VideoResolution720p,      ///< ...x720 progressive
    VideoResolutionFake1080i, ///< 1280x1080 1440x1080 interlaced
    VideoResolution1080i,     ///< 1920x1080 interlaced
    VideoResolutionUHD,       /// UHD progressive
    VideoResolutionMax        ///< number of resolution indexs
} VideoResolutions;

///
/// Video deinterlace modes.
///
typedef enum _video_deinterlace_modes_ {
    VideoDeinterlaceCuda,  ///< Cuda build in deinterlace
    VideoDeinterlaceYadif, ///< Yadif deinterlace
} VideoDeinterlaceModes;

///
/// Video scaleing modes.
///
typedef enum _video_scaling_modes_ {
    VideoScalingNormal,     ///< normal scaling
    VideoScalingFast,       ///< fastest scaling
    VideoScalingHQ,         ///< high quality scaling
    VideoScalingAnamorphic, ///< anamorphic scaling
} VideoScalingModes;

///
/// Video zoom modes.
///
typedef enum _video_zoom_modes_ {
    VideoNormal,       ///< normal
    VideoStretch,      ///< stretch to all edges
    VideoCenterCutOut, ///< center and cut out
    VideoNone,         ///< no scaling
} VideoZoomModes;

///
/// Video color space conversions.
///
typedef enum _video_color_space_ {
    VideoColorSpaceNone,    ///< no conversion
    VideoColorSpaceBt601,   ///< ITU.BT-601 Y'CbCr
    VideoColorSpaceBt709,   ///< ITU.BT-709 HDTV Y'CbCr
    VideoColorSpaceSmpte240 ///< SMPTE-240M Y'PbPr
} VideoColorSpace;

///
/// Video output module structure and typedef.
///
typedef struct _video_module_ {
    const char *Name; ///< video output module name
    char Enabled;     ///< flag output module enabled

    /// allocate new video hw decoder
    VideoHwDecoder *(*const NewHwDecoder)(VideoStream *);
    void (*const DelHwDecoder)(VideoHwDecoder *);
    unsigned (*const GetSurface)(VideoHwDecoder *, const AVCodecContext *);
    void (*const ReleaseSurface)(VideoHwDecoder *, unsigned);
    enum AVPixelFormat (*const get_format)(VideoHwDecoder *, AVCodecContext *, const enum AVPixelFormat *);
    void (*const RenderFrame)(VideoHwDecoder *, const AVCodecContext *, const AVFrame *);
    void *(*const GetHwAccelContext)(VideoHwDecoder *);
    void (*const SetClock)(VideoHwDecoder *, int64_t);
    int64_t (*const GetClock)(const VideoHwDecoder *);
    void (*const SetClosing)(const VideoHwDecoder *);
    void (*const ResetStart)(const VideoHwDecoder *);
    void (*const SetTrickSpeed)(const VideoHwDecoder *, int);
    uint8_t *(*const GrabOutput)(int *, int *, int *, int);
    void (*const GetStats)(VideoHwDecoder *, int *, int *, int *, int *, float *, int *, int *, int *, int *);
    void (*const SetBackground)(uint32_t);
    void (*const SetVideoMode)(void);

    /// module display handler thread
    void (*const DisplayHandlerThread)(void);

    void (*const OsdClear)(void); ///< clear OSD
    /// draw OSD ARGB area
    void (*const OsdDrawARGB)(int, int, int, int, int, const uint8_t *, int, int);
    void (*const OsdInit)(int, int); ///< initialize OSD
    void (*const OsdExit)(void);     ///< cleanup OSD

    int (*const Init)(const char *); ///< initialize video output module
    void (*const Exit)(void);        ///< cleanup video output module
} VideoModule;

typedef struct {

    /** Left X co-ordinate. Inclusive. */
    uint32_t x0;

    /** Top Y co-ordinate. Inclusive. */
    uint32_t y0;

    /** Right X co-ordinate. Exclusive. */
    uint32_t x1;

    /** Bottom Y co-ordinate. Exclusive. */
    uint32_t y1;
} VdpRect;

//----------------------------------------------------------------------------
//  Defines
//----------------------------------------------------------------------------

#define CODEC_SURFACES_MAX 12 //

#define VIDEO_SURFACES_MAX 6 ///< video output surfaces for queue

#define NUM_SHADERS 5 // Number of supported user shaders with placebo

#if defined VAAPI
#define PIXEL_FORMAT AV_PIX_FMT_VAAPI
#define SWAP_BUFFER_SIZE 3
#endif
#ifdef CUVID
#define PIXEL_FORMAT AV_PIX_FMT_CUDA
#define SWAP_BUFFER_SIZE 3
#endif
//----------------------------------------------------------------------------
//  Variables
//----------------------------------------------------------------------------
AVBufferRef *HwDeviceContext; ///< ffmpeg HW device context
char VideoIgnoreRepeatPict;   ///< disable repeat pict warning

int Planes = 2;

unsigned char *posd;

static const char *VideoDriverName = "cuvid"; ///< video output device
static Display *XlibDisplay;                  ///< Xlib X11 display
static xcb_connection_t *Connection;          ///< xcb connection
static xcb_colormap_t VideoColormap;          ///< video colormap
static xcb_window_t VideoWindow;              ///< video window
static xcb_screen_t const *VideoScreen;       ///< video screen
static uint32_t VideoBlankTick;               ///< blank cursor timer
static xcb_pixmap_t VideoCursorPixmap;        ///< blank curosr pixmap
static xcb_cursor_t VideoBlankCursor;         ///< empty invisible cursor

static int VideoWindowX;           ///< video output window x coordinate
static int VideoWindowY;           ///< video outout window y coordinate
static unsigned VideoWindowWidth;  ///< video output window width
static unsigned VideoWindowHeight; ///< video output window height

static const VideoModule NoopModule; ///< forward definition of noop module

/// selected video module
static const VideoModule *VideoUsedModule = &NoopModule;

signed char VideoHardwareDecoder = -1; ///< flag use hardware decoder

static char VideoSurfaceModesChanged; ///< flag surface modes changed

static uint32_t VideoBackground; ///< video background color
char VideoStudioLevels;          ///< flag use studio levels

/// Default deinterlace mode.
static VideoDeinterlaceModes VideoDeinterlace[VideoResolutionMax];

/// Default skip chroma deinterlace flag (CUVID only).
static char VideoSkipChromaDeinterlace[VideoResolutionMax];

/// Default inverse telecine flag (CUVID only).
static char VideoInverseTelecine[VideoResolutionMax];

/// Default amount of noise reduction algorithm to apply (0 .. 1000).
static int VideoDenoise[VideoResolutionMax];

/// Default amount of sharpening, or blurring, to apply (-1000 .. 1000).
static int VideoSharpen[VideoResolutionMax];

/// Default cut top and bottom in pixels
static int VideoCutTopBottom[VideoResolutionMax];

/// Default cut left and right in pixels
static int VideoCutLeftRight[VideoResolutionMax];

/// Default scaling mode
static VideoScalingModes VideoScaling[VideoResolutionMax];

/// Default audio/video delay
int VideoAudioDelay;

/// Default zoom mode for 4:3
static VideoZoomModes Video4to3ZoomMode;

/// Default zoom mode for 16:9 and others
static VideoZoomModes VideoOtherZoomMode;

/// Default Value for DRM Connector
static char *DRMConnector = NULL;

/// Default Value for DRM Refreshrate
static int DRMRefresh = 50;

static char Video60HzMode;                   ///< handle 60hz displays
static char VideoSoftStartSync;              ///< soft start sync audio/video
//static const int VideoSoftStartFrames = 100; ///< soft start frames
static char VideoShowBlackPicture;           ///< flag show black picture

static float VideoBrightness = 0.0f;
static float VideoContrast = 1.0f;
static float VideoSaturation = 1.0f;
static float VideoHue = 0.0f;
static float VideoGamma = 1.0f;
static float VideoTemperature = 0.0f;
static int VulkanTargetColorSpace = 0;
static int VideoScalerTest = 0;
static int VideoColorBlindness = 0;
static float VideoColorBlindnessFaktor = 1.0;

#ifdef PLACEBO
static char *shadersp[NUM_SHADERS];
char MyConfigDir[200];
static int num_shaders = 0;
#endif

static int LUTon = -1;

static xcb_atom_t WmDeleteWindowAtom;   ///< WM delete message atom
static xcb_atom_t NetWmState;           ///< wm-state message atom
static xcb_atom_t NetWmStateFullscreen; ///< fullscreen wm-state message atom
static xcb_atom_t NetWmStateAbove;

#ifdef DEBUG
extern uint32_t VideoSwitch; ///< ticks for channel switch
#endif
extern void AudioVideoReady(int64_t); ///< tell audio video is ready

#ifdef USE_VIDEO_THREAD

static pthread_t VideoThread;          ///< video decode thread
static pthread_cond_t VideoWakeupCond; ///< wakeup condition variable
static pthread_mutex_t VideoMutex;     ///< video condition mutex
pthread_mutex_t VideoLockMutex; ///< video lock mutex
pthread_mutex_t OSDMutex;              ///< OSD update mutex
#endif

static pthread_t VideoDisplayThread; ///< video display thread

// static pthread_cond_t VideoDisplayWakeupCond; ///< wakeup condition variable
// static pthread_mutex_t VideoDisplayMutex; ///< video condition mutex
// static pthread_mutex_t VideoDisplayLockMutex; ///< video lock mutex

static int OsdConfigWidth;  ///< osd configured width
static int OsdConfigHeight; ///< osd configured height
static char OsdShown;       ///< flag show osd
static char Osd3DMode;      ///< 3D OSD mode
static int OsdWidth;        ///< osd width
static int OsdHeight;       ///< osd height
static int OsdDirtyX;       ///< osd dirty area x
static int OsdDirtyY;       ///< osd dirty area y
static int OsdDirtyWidth;   ///< osd dirty area width
static int OsdDirtyHeight;  ///< osd dirty area height

static void (*VideoEventCallback)(void) = NULL; /// callback function to notify VDR about Video Events

static int64_t VideoDeltaPTS; ///< FIXME: fix pts

#ifdef USE_SCREENSAVER
static char DPMSDisabled;            ///< flag we have disabled dpms
static char EnableDPMSatBlackScreen; ///< flag we should enable dpms at black screen
#endif

static int EglEnabled;          ///< use EGL

#ifdef CUVID
static int GlxVSyncEnabled = 1; ///< enable/disable v-sync
static unsigned int Count;
static GLXContext glxSharedContext; ///< shared gl context
static GLXContext glxContext;       ///< our gl context

static GLXContext glxThreadContext; ///< our gl context for the thread

static XVisualInfo *GlxVisualInfo; ///< our gl visual
static void GlxSetupWindow(xcb_window_t window, int width, int height, GLXContext context);
GLXContext OSDcontext;
#else
static EGLContext eglSharedContext;     ///< shared gl context
#ifdef USE_DRM
static EGLContext eglOSDContext = NULL; ///< our gl context for the thread
#endif
static EGLContext eglContext;           ///< our gl context
static EGLConfig eglConfig;
static EGLDisplay eglDisplay;
static EGLSurface eglSurface;
static EGLint eglAttrs[10];
static int eglVersion = 2;
static EGLImageKHR(EGLAPIENTRY *CreateImageKHR)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint *);
static EGLBoolean(EGLAPIENTRY *DestroyImageKHR)(EGLDisplay, EGLImageKHR);
static void(EGLAPIENTRY *EGLImageTargetTexture2DOES)(GLenum, GLeglImageOES);
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;

static EGLContext eglThreadContext; ///< our gl context for the thread

static void GlxSetupWindow(xcb_window_t window, int width, int height, EGLContext context);
EGLContext OSDcontext;
#endif

//----------------------------------------------------------------------------
//  Common Functions
//----------------------------------------------------------------------------

void VideoThreadLock(void);        ///< lock video thread
void VideoThreadUnlock(void);      ///< unlock video thread
static void VideoThreadExit(void); ///< exit/kill video thread

#ifdef USE_SCREENSAVER
static void X11SuspendScreenSaver(xcb_connection_t *, int);
static int X11HaveDPMS(xcb_connection_t *);
static void X11DPMSReenable(xcb_connection_t *);
static void X11DPMSDisable(xcb_connection_t *);
#endif

char *eglErrorString(EGLint error) {
    switch (error) {
        case EGL_SUCCESS:
            return "No error";
        case EGL_NOT_INITIALIZED:
            return "EGL not initialized or failed to initialize";
        case EGL_BAD_ACCESS:
            return "Resource inaccessible";
        case EGL_BAD_ALLOC:
            return "Cannot allocate resources";
        case EGL_BAD_ATTRIBUTE:
            return "Unrecognized attribute or attribute value";
        case EGL_BAD_CONTEXT:
            return "Invalid EGL context";
        case EGL_BAD_CONFIG:
            return "Invalid EGL frame buffer configuration";
        case EGL_BAD_CURRENT_SURFACE:
            return "Current surface is no longer valid";
        case EGL_BAD_DISPLAY:
            return "Invalid EGL display";
        case EGL_BAD_SURFACE:
            return "Invalid surface";
        case EGL_BAD_MATCH:
            return "Inconsistent arguments";
        case EGL_BAD_PARAMETER:
            return "Invalid argument";
        case EGL_BAD_NATIVE_PIXMAP:
            return "Invalid native pixmap";
        case EGL_BAD_NATIVE_WINDOW:
            return "Invalid native window";
        case EGL_CONTEXT_LOST:
            return "Context lost";
    }
    return "Unknown error ";
}

///
/// egl check error.
///
#define EglCheck(void)                                                                                                \
    {                                                                                                                 \
        EGLint err;                                                                                                   \
                                                                                                                      \
        if ((err = eglGetError()) != EGL_SUCCESS) {                                                                   \
            Debug(3, "video/egl: %s:%d error %d %s\n", __FILE__, __LINE__, err, eglErrorString(err));                 \
        }                                                                                                             \
    }

//----------------------------------------------------------------------------
//  DRM Helper Functions
//----------------------------------------------------------------------------
#ifdef USE_DRM
#include "drm.c"
#include "hdr.c"
#endif

///
/// Update video pts.
///
/// @param pts_p    pointer to pts
/// @param interlaced	interlaced flag (frame isn't right)
/// @param frame    frame to display
///
/// @note frame->interlaced_frame can't be used for interlace detection
///
static void VideoSetPts(int64_t *pts_p, int interlaced, const AVCodecContext *video_ctx, const AVFrame *frame) {
    int64_t pts;
    int duration;
    static int64_t lastpts;


    //
    //	Get duration for this frame.
    //	FIXME: using framerate as workaround for av_frame_get_pkt_duration
    //

    // if (video_ctx->framerate.num && video_ctx->framerate.den) {
    // duration = 1000 * video_ctx->framerate.den / video_ctx->framerate.num;
    // } else {
    duration = interlaced ? 40 : 20; // 50Hz -> 20ms default
    // }
    Debug(4, "video: Framerate %d/%d \n", video_ctx->framerate.den,
     video_ctx->framerate.num);

    // update video clock
    if (*pts_p != (int64_t)AV_NOPTS_VALUE) {
        *pts_p += duration * 90;
        // Info("video: %s +pts\n", Timestamp2String(*pts_p));
    }
    // av_opt_ptr(avcodec_get_frame_class(), frame, "best_effort_timestamp");
    // pts = frame->best_effort_timestamp;
    // pts = frame->pkt_pts;
    pts = frame->pts;
    if (pts == (int64_t)AV_NOPTS_VALUE || !pts) {
        // libav: 0.8pre didn't set pts
        pts = frame->pkt_dts;
    }
    // libav: sets only pkt_dts which can be 0
    if (pts && pts != (int64_t)AV_NOPTS_VALUE) {
        // build a monotonic pts
        if (*pts_p != (int64_t)AV_NOPTS_VALUE) {
            int64_t delta;

            delta = pts - *pts_p;
            // ignore negative jumps
            if (delta > -600 * 90 && delta <= -40 * 90) {
                if (-delta > VideoDeltaPTS) {
                    VideoDeltaPTS = -delta;
                    Debug(4, "video: %#012" PRIx64 "->%#012" PRIx64 " delta%+4" PRId64 " pts\n", *pts_p, pts,
                          pts - *pts_p);
                }
                return;
            }
        } else { // first new clock value
            Debug(3, "++++++++++++++++++++++++++++++++++++starte audio\n");
            AudioVideoReady(pts);
        }
        if (*pts_p != pts && lastpts != pts) {
            Debug(4, "video: %#012" PRIx64 "->%#012" PRIx64 " delta=%4" PRId64 " pts\n", *pts_p, pts, pts - *pts_p);
            *pts_p = pts;
        }
    }
    lastpts = pts;
}

int CuvidMessage(int level, const char *format, ...);

///
/// Update output for new size or aspect ratio.
///
/// @param input_aspect_ratio	video stream aspect
///
static void VideoUpdateOutput(AVRational input_aspect_ratio, int input_width, int input_height,
                              VideoResolutions resolution, int video_x, int video_y, int video_width, int video_height,
                              int *output_x, int *output_y, int *output_width, int *output_height, int *crop_x,
                              int *crop_y, int *crop_width, int *crop_height) {
    AVRational display_aspect_ratio;
    AVRational tmp_ratio;

    // input not initialized yet, return immediately
    if (!input_aspect_ratio.num || !input_aspect_ratio.den) {
        *output_width = video_width;
        *output_height = video_height;
        return;
    }
#ifdef USE_DRM
    get_drm_aspect(&display_aspect_ratio.num, &display_aspect_ratio.den);
#else
    display_aspect_ratio.num = VideoScreen->width_in_pixels;
    display_aspect_ratio.den = VideoScreen->height_in_pixels;
#endif
    av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den, display_aspect_ratio.num, display_aspect_ratio.den,
              1024 * 1024);

    Debug(3, "video: input %dx%d (%d:%d)\n", input_width, input_height, input_aspect_ratio.num,
          input_aspect_ratio.den);
    Debug(3, "video: display aspect %d:%d Resolution %d\n", display_aspect_ratio.num, display_aspect_ratio.den,
          resolution);
    Debug(3, "video: video %+d%+d %dx%d\n", video_x, video_y, video_width, video_height);

    *crop_x = VideoCutLeftRight[resolution];
    *crop_y = VideoCutTopBottom[resolution];
    *crop_width = input_width - VideoCutLeftRight[resolution] * 2;
    *crop_height = input_height - VideoCutTopBottom[resolution] * 2;
    CuvidMessage(2, "video: crop to %+d%+d %dx%d\n", *crop_x, *crop_y, *crop_width, *crop_height);

    tmp_ratio.num = 4;
    tmp_ratio.den = 3;
    if (!av_cmp_q(input_aspect_ratio, tmp_ratio)) {
        switch (Video4to3ZoomMode) {
            case VideoNormal:
                goto normal;
            case VideoStretch:
                goto stretch;
            case VideoCenterCutOut:
                goto center_cut_out;
            case VideoNone:
                goto video_none;
        }
    }
    switch (VideoOtherZoomMode) {
        case VideoNormal:
            goto normal;
        case VideoStretch:
            goto stretch;
        case VideoCenterCutOut:
            goto center_cut_out;
        case VideoNone:
            goto video_none;
    }

normal:
    *output_x = video_x;
    *output_y = video_y;
    *output_height = video_height;
    *output_width = (*crop_width * *output_height * input_aspect_ratio.num) / (input_aspect_ratio.den * *crop_height);
    if (*output_width > video_width) {
        *output_width = video_width;
        *output_height =
            (*crop_height * *output_width * input_aspect_ratio.den) / (input_aspect_ratio.num * *crop_width);
        *output_y += (video_height - *output_height) / 2;
    } else if (*output_width < video_width) {
        *output_x += (video_width - *output_width) / 2;
    }
    CuvidMessage(2, "video: normal aspect output %dx%d%+d%+d\n", *output_width, *output_height, *output_x, *output_y);
    return;

stretch:
    *output_x = video_x;
    *output_y = video_y;
    *output_width = video_width;
    *output_height = video_height;
    CuvidMessage(2, "video: stretch output %dx%d%+d%+d\n", *output_width, *output_height, *output_x, *output_y);
    return;

center_cut_out:
    *output_x = video_x;
    *output_y = video_y;
    *output_height = video_height;
    *output_width = (*crop_width * *output_height * input_aspect_ratio.num) / (input_aspect_ratio.den * *crop_height);
    if (*output_width > video_width) {
        // fix height cropping
        *crop_width = (int)((*crop_width * video_width) / (*output_width * 2.0) + 0.5) * 2;
        *crop_x = (input_width - *crop_width) / 2;
        *output_width = video_width;
    } else if (*output_width < video_width) {
        // fix width cropping
        *crop_height = (int)((*crop_height * *output_width) / (video_width * 2.0) + 0.5) * 2;
        *crop_y = (input_height - *crop_height) / 2;
        *output_width = video_width;
    }
    CuvidMessage(2, "video: aspect crop %dx%d%+d%+d\n", *crop_width, *crop_height, *crop_x, *crop_y);
    return;

video_none:
    *output_height = *crop_height;
    *output_width = (*crop_width * input_aspect_ratio.num) / input_aspect_ratio.den; // normalize pixel aspect ratio
    *output_x = video_x + (video_width - *output_width) / 2;
    *output_y = video_y + (video_height - *output_height) / 2;
    CuvidMessage(2, "video: original aspect output %dx%d%+d%+d\n", *output_width, *output_height, *output_x,
                 *output_y);
    return;
}

//static uint64_t test_time = 0;

///
/// Lock video thread.
///
#define VideoThreadLock(void)                                                                                         \
    {                                                                                                                 \
        if (VideoThread) {                                                                                            \
            if (pthread_mutex_lock(&VideoLockMutex)) {                                                                \
                Error(_("video: can't lock thread\n"));                                                               \
            }                                                                                                         \
        }                                                                                                             \
    }
// test_time = GetusTicks();
// printf("Lock start....");
///
/// Unlock video thread.
///
#define VideoThreadUnlock(void)                                                                                       \
    {                                                                                                                 \
        if (VideoThread) {                                                                                            \
            if (pthread_mutex_unlock(&VideoLockMutex)) {                                                              \
                Error(_("video: can't unlock thread\n"));                                                             \
            }                                                                                                         \
        }                                                                                                             \
    }
// printf("Video Locked for  %d\n",(GetusTicks()-test_time)/1000);

#ifdef PLACEBO_GL
#define Lock_and_SharedContext                                                                                        \
    {                                                                                                                 \
        VideoThreadLock();                                                                                            \
        Debug(4,"Lock OSDMutex %s %d\n",__FILE__, __LINE__);                                                          \
        pthread_mutex_lock(&OSDMutex);                                                                                \
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglSharedContext);                                 \
        EglCheck();                                                                                                   \
    }
#define Unlock_and_NoContext                                                                                          \
    {                                                                                                                 \
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);                                   \
        EglCheck();                                                                                                   \
        Debug(4,"UnLock OSDMutex %s %d\n",__FILE__, __LINE__);                                                        \
        pthread_mutex_unlock(&OSDMutex);                                                                              \
        VideoThreadUnlock();                                                                                          \
    }
#define SharedContext                                                                                                 \
    {                                                                                                                 \
        Debug(4,"Lock OSDMutex %s %d\n",__FILE__, __LINE__);                                                          \
        pthread_mutex_lock(&OSDMutex);                                                                                \
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglSharedContext);                                 \
        EglCheck();                                                                                                   \
    }
#define NoContext                                                                                                     \
    {                                                                                                                 \
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);                                   \
        EglCheck();                                                                                                   \
        Debug(4,"UnLock OSDMutex %s %d\n",__FILE__, __LINE__);                                                        \
        pthread_mutex_unlock(&OSDMutex);                                                                              \
    }
#else
#ifdef PLACEBO
#define Lock_and_SharedContext                                                                                        \
    { VideoThreadLock(); }
#define Unlock_and_NoContext                                                                                          \
    { VideoThreadUnlock(); }
#define SharedContext                                                                                                 \
    {}
#define NoContext                                                                                                     \
    {}
#endif
#endif

//----------------------------------------------------------------------------
//  GLX
//----------------------------------------------------------------------------

#ifdef USE_GLX



///
/// GLX check error.
///
#define GlxCheck(void)                                                                                                \
    {                                                                                                                 \
        GLenum err;                                                                                                   \
                                                                                                                      \
        if ((err = glGetError()) != GL_NO_ERROR) {                                                                    \
            Debug(3, "video/glx: error %s:%d %d '%s'\n", __FILE__, __LINE__, err, gluErrorString(err));               \
        }                                                                                                             \
    }


#ifdef CUVID
///
/// GLX extension functions
///@{
#ifdef GLX_MESA_swap_control
static PFNGLXSWAPINTERVALMESAPROC GlxSwapIntervalMESA;
#endif
#ifdef GLX_SGI_video_sync
static PFNGLXGETVIDEOSYNCSGIPROC GlxGetVideoSyncSGI;
#endif
#ifdef GLX_SGI_swap_control
static PFNGLXSWAPINTERVALSGIPROC GlxSwapIntervalSGI;
#endif

///
/// GLX check if a GLX extension is supported.
///
/// @param ext	extension to query
/// @returns true if supported, false otherwise
///
static int GlxIsExtensionSupported(const char *ext) {
    const char *extensions;

    if ((extensions = glXQueryExtensionsString(XlibDisplay, DefaultScreen(XlibDisplay)))) {
        const char *s;
        int l;

        s = strstr(extensions, ext);
        l = strlen(ext);
        return s && (s[l] == ' ' || s[l] == '\0');
    }
    return 0;
}
#endif

///
/// Setup GLX window.
///
/// @param window   xcb window id
/// @param width    window width
/// @param height   window height
/// @param context  GLX context
///
#ifdef CUVID
static void GlxSetupWindow(xcb_window_t window, int width, int height, GLXContext context)
#else
static void GlxSetupWindow(xcb_window_t window, int width, int height, EGLContext context)
#endif
{
#ifdef CUVID
    uint32_t start;
    uint32_t end;
    int i;
    unsigned count;
#endif

#ifdef PLACEBO_
    return;
#endif

    Debug(3, "video/egl: %s %x %dx%d context: %p", __FUNCTION__, window, width, height, context);

    // set gl context
#ifdef CUVID
    if (!glXMakeCurrent(XlibDisplay, window, context)) {
        Fatal(_("video/egl: GlxSetupWindow can't make egl/glx context current\n"));
        EglEnabled = 0;
        return;
    }
#endif
    Debug(3, "video/egl: ok\n");

#ifdef CUVID
    // check if v-sync is working correct
    end = GetMsTicks();
    for (i = 0; i < 10; ++i) {
        start = end;

        glClear(GL_COLOR_BUFFER_BIT);
        glXSwapBuffers(XlibDisplay, window);
        end = GetMsTicks();

        GlxGetVideoSyncSGI(&count);
        Debug(4, "video/glx: %5d frame rate %dms\n", count, end - start);
        // nvidia can queue 5 swaps
        if (i > 5 && (end - start) < 15) {
            Warning(_("video/glx: no v-sync\n"));
        }
    }
    GLenum err = glewInit();

    if (err != GLEW_OK) {
        Debug(3, "Error: %s\n", glewGetErrorString(err));
    }
    GlxCheck();
#endif
    // viewpoint
    glViewport(0, 0, width, height);
    GlxCheck();
#ifdef VAAPI
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif
}

///
/// Initialize GLX.
///
#ifdef CUVID
static void EglInit(void) {

    XVisualInfo *vi = NULL;

#if defined PLACEBO && !defined PLACEBO_GL
    return;
#endif

    // The desired 30-bit color visual
    int attributeList10[] = {GLX_DRAWABLE_TYPE,
                             GLX_WINDOW_BIT,
                             GLX_RENDER_TYPE,
                             GLX_RGBA_BIT,
                             GLX_DOUBLEBUFFER,
                             True,
                             GLX_RED_SIZE,
                             10, /*10bits for R */
                             GLX_GREEN_SIZE,
                             10, /*10bits for G */
                             GLX_BLUE_SIZE,
                             10, /*10bits for B */
                             None};
    int attributeList[] = {GLX_DRAWABLE_TYPE,
                           GLX_WINDOW_BIT,
                           GLX_RENDER_TYPE,
                           GLX_RGBA_BIT,
                           GLX_DOUBLEBUFFER,
                           True,
                           GLX_RED_SIZE,
                           8, /*8 bits for R */
                           GLX_GREEN_SIZE,
                           8, /*8 bits for G */
                           GLX_BLUE_SIZE,
                           8, /*8 bits for B */
                           None};
    int fbcount;

    GLXContext context;
    int major;
    int minor;
    int glx_GLX_EXT_swap_control;
    int glx_GLX_MESA_swap_control;
    int glx_GLX_SGI_swap_control;
    int glx_GLX_SGI_video_sync;
    GLXFBConfig *fbc;
    int redSize, greenSize, blueSize;

    if (!glXQueryVersion(XlibDisplay, &major, &minor)) {
        Fatal(_("video/glx: no GLX support\n"));
    }
    Debug(3, "video/glx: glx version %d.%d\n", major, minor);

    //
    //	check which extension are supported
    //
    glx_GLX_EXT_swap_control = GlxIsExtensionSupported("GLX_EXT_swap_control");
    glx_GLX_MESA_swap_control = GlxIsExtensionSupported("GLX_MESA_swap_control");
    glx_GLX_SGI_swap_control = GlxIsExtensionSupported("GLX_SGI_swap_control");
    glx_GLX_SGI_video_sync = GlxIsExtensionSupported("GLX_SGI_video_sync");

#ifdef GLX_MESA_swap_control
    if (glx_GLX_MESA_swap_control) {
        GlxSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA");
    }
    Debug(3, "video/glx: GlxSwapIntervalMESA=%p\n", GlxSwapIntervalMESA);
#endif
#ifdef GLX_SGI_swap_control
    if (glx_GLX_SGI_swap_control) {
        GlxSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress((const GLubyte *)"wglSwapIntervalEXT");
    }
    Debug(3, "video/glx: GlxSwapIntervalSGI=%p\n", GlxSwapIntervalSGI);
#endif
#ifdef GLX_SGI_video_sync
    if (glx_GLX_SGI_video_sync) {
        GlxGetVideoSyncSGI = (PFNGLXGETVIDEOSYNCSGIPROC)glXGetProcAddress((const GLubyte *)"glXGetVideoSyncSGI");
    }
    Debug(3, "video/glx: GlxGetVideoSyncSGI=%p\n", GlxGetVideoSyncSGI);
#endif

    // create glx context
    glXMakeCurrent(XlibDisplay, None, NULL);

    fbc = glXChooseFBConfig(XlibDisplay, DefaultScreen(XlibDisplay), attributeList10, &fbcount); // try 10 Bit
    if (fbc == NULL) {
        fbc =
            glXChooseFBConfig(XlibDisplay, DefaultScreen(XlibDisplay), attributeList, &fbcount); // fall back to 8 Bit
        if (fbc == NULL)
            Fatal(_("did not get FBconfig"));
    }

    vi = glXGetVisualFromFBConfig(XlibDisplay, fbc[0]);

    glXGetFBConfigAttrib(XlibDisplay, fbc[0], GLX_RED_SIZE, &redSize);
    glXGetFBConfigAttrib(XlibDisplay, fbc[0], GLX_GREEN_SIZE, &greenSize);
    glXGetFBConfigAttrib(XlibDisplay, fbc[0], GLX_BLUE_SIZE, &blueSize);

    Debug(3, "RGB size %d:%d:%d\n", redSize, greenSize, blueSize);
    Debug(3, "Chosen visual ID = 0x%x\n", vi->visualid);

    context = glXCreateContext(XlibDisplay, vi, NULL, GL_TRUE);
    if (!context) {
        Fatal(_("video/glx: can't create glx context\n"));
    }
    glxSharedContext = context;
    context = glXCreateContext(XlibDisplay, vi, glxSharedContext, GL_TRUE);
    if (!context) {
        Fatal(_("video/glx: can't create glx context\n"));
    }
    glxContext = context;

    EglEnabled = 1;
    GlxVisualInfo = vi;
    Debug(3, "video/glx: visual %#02x depth %u\n", (unsigned)vi->visualid, vi->depth);

    //
    //	query default v-sync state
    //
    if (glx_GLX_EXT_swap_control) {
        unsigned tmp;

        tmp = -1;
        glXQueryDrawable(XlibDisplay, DefaultRootWindow(XlibDisplay), GLX_SWAP_INTERVAL_EXT, &tmp);
        GlxCheck();

        Debug(3, "video/glx: default v-sync is %d\n", tmp);
    } else {
        Debug(3, "video/glx: default v-sync is unknown\n");
    }

    //
    //	disable wait on v-sync
    //
    // FIXME: sleep before swap / busy waiting hardware
    // FIXME: 60hz lcd panel
    // FIXME: config: default, on, off
#ifdef GLX_SGI_swap_control
    if (GlxVSyncEnabled < 0 && GlxSwapIntervalSGI) {
        if (GlxSwapIntervalSGI(0)) {
            GlxCheck();
            Warning(_("video/glx: can't disable v-sync\n"));
        } else {
            Info(_("video/glx: v-sync disabled\n"));
        }
    } else
#endif
#ifdef GLX_MESA_swap_control
        if (GlxVSyncEnabled < 0 && GlxSwapIntervalMESA) {
        if (GlxSwapIntervalMESA(0)) {
            GlxCheck();
            Warning(_("video/glx: can't disable v-sync\n"));
        } else {
            Info(_("video/glx: v-sync disabled\n"));
        }
    }
#endif

    //
    //	enable wait on v-sync
    //
#ifdef GLX_SGI_swap_control
    if (GlxVSyncEnabled > 0 && GlxSwapIntervalMESA) {
        if (GlxSwapIntervalMESA(1)) {
            GlxCheck();
            Warning(_("video/glx: can't enable v-sync\n"));
        } else {
            Info(_("video/glx: v-sync enabled\n"));
        }
    } else
#endif
#ifdef GLX_MESA_swap_control
        if (GlxVSyncEnabled > 0 && GlxSwapIntervalSGI) {
        if (GlxSwapIntervalSGI(1)) {
            GlxCheck();
            Warning(_("video/glx: SGI can't enable v-sync\n"));
        } else {
            Info(_("video/glx: SGI v-sync enabled\n"));
        }
    }
#endif
}

#else // VAAPI
extern void make_egl(void);
static void EglInit(void) {
    int redSize, greenSize, blueSize, alphaSize;
    static int glewdone = 0;

#if defined PLACEBO && !defined PLACEBO_GL
    return;
#endif
    EGLContext context;

    // create egl context
    //	 setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 0);
    //	 setenv("V3D_DOUBLE_BUFFER", "1", 0);
    make_egl();

    if (!glewdone) {
        //GLenum err = glewInit();
        glewInit();

        glewdone = 1;
        //	  if (err != GLEW_OK) {
        //	      Debug(3, "Error: %s\n", glewGetErrorString(err));
        //	  }
    }

    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_BLUE_SIZE, &blueSize);
    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_RED_SIZE, &redSize);
    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_GREEN_SIZE, &greenSize);
    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_ALPHA_SIZE, &alphaSize);
    Debug(3, "RGB size %d:%d:%d Alpha %d\n", redSize, greenSize, blueSize, alphaSize);

    eglSharedContext = eglContext;

    context = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, eglAttrs);

    EglCheck();
    if (!context) {
        Fatal(_("video/egl: can't create egl context\n"));
    }
    eglContext = context;
}
#endif

///
/// Cleanup GLX.
///
static void EglExit(void) {
    Debug(3, "video/egl: %s\n", __FUNCTION__);
#if defined PLACEBO && !defined PLACEBO_GL
    return;
#endif

    glFinish();

    // must destroy contet
#ifdef CUVID
    // must destroy glx
    // if (glXGetCurrentContext() == glxContext) {
    // if currently used, set to none
    // glXMakeCurrent(XlibDisplay, None, NULL);
    // }
    if (OSDcontext) {
        glXDestroyContext(XlibDisplay, OSDcontext);
        GlxCheck();
        OSDcontext = NULL;
    }
    if (glxContext) {
        glXDestroyContext(XlibDisplay, glxContext);
        GlxCheck();
        glxContext = NULL;
    }

    if (glxSharedContext) {
        glXDestroyContext(XlibDisplay, glxSharedContext);
        GlxCheck();
        glxSharedContext = NULL;
    }
#else
#ifdef USE_DRM
    drm_clean_up();

#endif
    
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (eglSurface) {
        eglDestroySurface(eglDisplay, eglSurface);
        EglCheck();
        eglSurface = NULL;
    }
    if (eglSharedContext) {
        eglDestroyContext(eglDisplay, eglSharedContext);
        EglCheck();
        eglSharedContext = NULL;
    }

    if (eglContext) {
        eglDestroyContext(eglDisplay, eglContext);
        EglCheck();
        eglContext = NULL;
    }
    eglTerminate(eglDisplay);
    eglDisplay = NULL;

#endif
}

#endif

//----------------------------------------------------------------------------
//  common functions
//----------------------------------------------------------------------------

///
/// Calculate resolution group.
///
/// @param width    video picture raw width
/// @param height   video picture raw height
/// @param interlace	flag interlaced video picture
///
/// @note interlace isn't used yet and probably wrong set by caller.
///
static VideoResolutions VideoResolutionGroup(int width, int height, __attribute__((unused)) int interlace) {
    if (height == 2160) {
        return VideoResolutionUHD;
    }
    if (height <= 576) {
        return VideoResolution576i;
    }
    if (height <= 720) {
        return VideoResolution720p;
    }
    if (height < 1080) {
        return VideoResolutionFake1080i;
    }
    if (width < 1920) {
        return VideoResolutionFake1080i;
    }
    return VideoResolution1080i;
}

//----------------------------------------------------------------------------
//  CUVID
//----------------------------------------------------------------------------

#ifdef USE_CUVID

#ifdef PLACEBO
struct ext_buf {
    int fd;
#ifdef CUVID
    CUexternalMemory mem;
    CUmipmappedArray mma;
    CUexternalSemaphore ss;
    CUexternalSemaphore ws;
    const struct pl_sysnc *sysnc;
#endif
};
#endif

#ifdef VAAPI
static VADisplay *VaDisplay; ///< VA-API display
#endif

///
/// CUVID decoder
///
typedef struct _cuvid_decoder_ {
#ifdef VAAPI
    VADisplay *VaDisplay; ///< VA-API display
#endif

    xcb_window_t Window; ///< output window

    int VideoX;      ///< video base x coordinate
    int VideoY;      ///< video base y coordinate
    int VideoWidth;  ///< video base width
    int VideoHeight; ///< video base height

    int OutputX;      ///< real video output x coordinate
    int OutputY;      ///< real video output y coordinate
    int OutputWidth;  ///< real video output width
    int OutputHeight; ///< real video output height

    enum AVPixelFormat PixFmt;              ///< ffmpeg frame pixfmt
    enum AVColorSpace ColorSpace;           /// ffmpeg ColorSpace
    enum AVColorTransferCharacteristic trc; //
    enum AVColorPrimaries color_primaries;
    int WrongInterlacedWarned; ///< warning about interlace flag issued
    int Interlaced;            ///< ffmpeg interlaced flag
    int TopFieldFirst;         ///< ffmpeg top field displayed first

    int InputWidth;              ///< video input width
    int InputHeight;             ///< video input height
    AVRational InputAspect;      ///< video input aspect ratio
    VideoResolutions Resolution; ///< resolution group

    int CropX;      ///< video crop x
    int CropY;      ///< video crop y
    int CropWidth;  ///< video crop width
    int CropHeight; ///< video crop height

    int grabwidth, grabheight, grab; // Grab Data
    void *grabbase;

    int SurfacesNeeded; ///< number of surface to request
    int SurfaceUsedN;   ///< number of used video surfaces
    /// used video surface ids
    int SurfacesUsed[CODEC_SURFACES_MAX];
    int SurfaceFreeN; ///< number of free video surfaces
    /// free video surface ids
    int SurfacesFree[CODEC_SURFACES_MAX];
    /// video surface ring buffer
    int SurfacesRb[VIDEO_SURFACES_MAX];
    // CUcontext cuda_ctx;

    // cudaStream_t stream;    // make my own cuda stream
    // CUgraphicsResource cuResource;
    int SurfaceWrite;        ///< write pointer
    int SurfaceRead;         ///< read pointer
    atomic_t SurfacesFilled; ///< how many of the buffer is used
    AVFrame *frames[CODEC_SURFACES_MAX + 1];
#ifdef CUVID
    CUarray cu_array[CODEC_SURFACES_MAX + 1][2];
    CUgraphicsResource cu_res[CODEC_SURFACES_MAX + 1][2];
    CUcontext cuda_ctx;
#endif
    GLuint gl_textures[(CODEC_SURFACES_MAX + 1) * 2]; // where we will copy the CUDA result
#ifdef VAAPI
    EGLImageKHR images[(CODEC_SURFACES_MAX + 1) * 2];
    int fds[(CODEC_SURFACES_MAX + 1) * 2];
#endif
#ifdef PLACEBO
    struct pl_frame pl_frames[CODEC_SURFACES_MAX + 1]; // images for Placebo chain
    struct ext_buf ebuf[CODEC_SURFACES_MAX + 1];       // for managing vk buffer
#endif

    int SurfaceField;          ///< current displayed field
    int TrickSpeed;            ///< current trick speed
    int TrickCounter;          ///< current trick speed counter
    struct timespec FrameTime; ///< time of last display
    VideoStream *Stream;       ///< video stream
    int Closing;               ///< flag about closing current stream
    int SyncOnAudio;           ///< flag sync to audio
    int64_t PTS;               ///< video PTS clock

#if defined(YADIF) || defined(VAAPI)
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
#endif
    AVBufferRef *cached_hw_frames_ctx;
    int LastAVDiff;      ///< last audio - video difference
    int SyncCounter;     ///< counter to sync frames
    int StartCounter;    ///< counter for video start
    int FramesDuped;     ///< number of frames duplicated
    int FramesMissed;    ///< number of frames missed
    int FramesDropped;   ///< number of frames dropped
    int FrameCounter;    ///< number of frames decoded
    int FramesDisplayed; ///< number of frames displayed
    float Frameproc;     /// Time to process frame
    int newchannel;
} CuvidDecoder;

static CuvidDecoder *CuvidDecoders[2]; ///< open decoder streams
static int CuvidDecoderN;              ///< number of decoder streams

#ifdef CUVID
static CudaFunctions *cu;
#endif

#ifdef PLACEBO

struct file {
    void *data;
    size_t size;
};

typedef struct priv {
#if PL_API_VER >= 229
    const struct pl_gpu_t *gpu;
    const struct pl_vulkan_t *vk;
    const struct pl_vk_inst_t *vk_inst;
#else
    const struct pl_gpu *gpu;
    const struct pl_vulkan *vk;
    const struct pl_vk_inst *vk_inst;
#endif
    const struct pl_log_t *ctx;
    struct pl_custom_lut *lut;
    struct pl_renderer_t *renderer;
    struct pl_renderer_t *renderertest;
    const struct pl_swapchain_t *swapchain;
    struct pl_log_params context;
#ifndef PLACEBO_GL
    VkSurfaceKHR pSurface;
#endif
    int has_dma_buf;
#ifdef PLACEBO_GL
#if PL_API_VER >= 229
    const struct pl_opengl_t *gl;
#else
    struct pl_opengl *gl;
#endif
#endif
    const struct pl_hook *hook[NUM_SHADERS];
    int num_shaders;

} priv;

static priv *p;
static struct pl_overlay osdoverlay;
#if PL_API_VER >= 229
static struct pl_overlay_part part;
#endif

struct itimerval itimer;
#endif

GLuint vao_buffer;

// GLuint vao_vao[4];
GLuint gl_shader = 0, gl_prog = 0, gl_fbo = 0; // shader programm
GLint gl_colormatrix, gl_colormatrix_c;
GLuint OSDfb = 0;
GLuint OSDtexture, gl_prog_osd = 0;

int OSDx, OSDy, OSDxsize, OSDysize;

static struct timespec CuvidFrameTime; ///< time of last display

int window_width, window_height;

#include "shaders.h"

//----------------------------------------------------------------------------

///
/// Output video messages.
///
/// Reduce output.
///
/// @param level    message level (Error, Warning, Info, Debug, ...)
/// @param format   printf format string (NULL to flush messages)
/// @param ...	printf arguments
///
/// @returns true, if message shown
///
int CuvidMessage(int level, const char *format, ...) {
    if (SysLogLevel > level || DebugLevel > level) {
        static const char *last_format;
        static char buf[256];
        va_list ap;

        va_start(ap, format);
        if (format != last_format) { // don't repeat same message
            if (buf[0]) {            // print last repeated message
                syslog(LOG_ERR, "%s", buf);
                buf[0] = '\0';
            }

            if (format) {
                last_format = format;
                vsyslog(LOG_ERR, format, ap);
            }
            va_end(ap);
            return 1;
        }
        vsnprintf(buf, sizeof(buf), format, ap);
        va_end(ap);
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// These are CUDA Helper functions
#ifdef CUVID
// This will output the proper CUDA error strings in the event that a CUDA host
// call returns an error
#define checkCudaErrors(err) __checkCudaErrors(err, __FILE__, __LINE__)

// These are the inline versions for all of the SDK helper functions
static inline void __checkCudaErrors(CUresult err, const char *file, const int line) {
    if (CUDA_SUCCESS != err) {
        CuvidMessage(2,
                     "checkCudaErrors() Driver API error = %04d >%s< from file "
                     "<%s>, line %i.\n",
                     err, getCudaDrvErrorString(err), file, line);
        exit(EXIT_FAILURE);
    }
}
#endif

//  Surfaces -------------------------------------------------------------
void createTextureDst(CuvidDecoder *decoder, int anz, unsigned int size_x, unsigned int size_y,
                      enum AVPixelFormat PixFmt);
///
/// Create surfaces for CUVID decoder.
///
/// @param decoder  CUVID hw decoder
/// @param width    surface source/video width
/// @param height   surface source/video height
///
static void CuvidCreateSurfaces(CuvidDecoder *decoder, int width, int height, enum AVPixelFormat PixFmt) {
    int i;

#ifdef DEBUG
    if (!decoder->SurfacesNeeded) {
        Error(_("video/cuvid: surface needed not set\n"));
        decoder->SurfacesNeeded = VIDEO_SURFACES_MAX;
    }
#endif
    Debug(3, "video/cuvid: %s: %dx%d * %d \n", __FUNCTION__, width, height, decoder->SurfacesNeeded);

    // allocate only the number of needed surfaces
    decoder->SurfaceFreeN = decoder->SurfacesNeeded;

    createTextureDst(decoder, decoder->SurfacesNeeded, width, height, PixFmt);

    for (i = 0; i < decoder->SurfaceFreeN; ++i) {
        decoder->SurfacesFree[i] = i;
    }

    Debug(4, "video/cuvid: created video surface %dx%d with id %d\n", width, height, decoder->SurfacesFree[i]);
}

///
/// Destroy surfaces of CUVID decoder.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidDestroySurfaces(CuvidDecoder *decoder) {
    int i, j;

    Debug(3, "video/cuvid: %s\n", __FUNCTION__);

#ifndef PLACEBO
#ifdef CUVID
    glXMakeCurrent(XlibDisplay, VideoWindow, glxSharedContext);
    GlxCheck();
#else
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext);
    EglCheck();
#endif
#endif

#ifdef PLACEBO
    pl_gpu_finish(p->gpu);
#if API_VER >= 58
    p->num_shaders = 0;
#endif
#endif

    for (i = 0; i < decoder->SurfacesNeeded; i++) {
        if (decoder->frames[i]) {
            av_frame_free(&decoder->frames[i]);
        }
        for (j = 0; j < Planes; j++) {
#ifdef PLACEBO
            if (decoder->pl_frames[i].planes[j].texture) {

#ifdef VAAPI
                if (p->has_dma_buf && decoder->pl_frames[i].planes[j].texture->params.shared_mem.handle.fd) {
                    close(decoder->pl_frames[i].planes[j].texture->params.shared_mem.handle.fd);
                }
#endif
                SharedContext;
                pl_tex_destroy(p->gpu, &decoder->pl_frames[i].planes[j].texture);
                NoContext;
            }
#else
#ifdef CUVID
            checkCudaErrors(cu->cuGraphicsUnregisterResource(decoder->cu_res[i][j]));
#endif
#ifdef PLACEBO
            if (p->hasdma_buf) {
#endif
#ifdef VAAPI
                if (decoder->images[i * Planes + j]) {
                    DestroyImageKHR(eglGetCurrentDisplay(), decoder->images[i * Planes + j]);
                    if (decoder->fds[i * Planes + j])
                        close(decoder->fds[i * Planes + j]);
                }
                decoder->fds[i * Planes + j] = 0;
                decoder->images[i * Planes + j] = 0;
#endif
#ifdef PLACEBO
            }
#endif
#endif
        }
    }

#ifdef PLACEBO

    //	 pl_renderer_destroy(&p->renderer);
    //	 p->renderer = pl_renderer_create(p->ctx, p->gpu);

#else
    glDeleteTextures(CODEC_SURFACES_MAX * 2, (GLuint *)&decoder->gl_textures);
    GlxCheck();

    if (CuvidDecoderN == 1) { // only wenn last decoder closes
        Debug(3, "Last decoder closes\n");
        glDeleteBuffers(1, (GLuint *)&vao_buffer);
        if (gl_prog)
            glDeleteProgram(gl_prog);
        gl_prog = 0;
    }
#endif

    for (i = 0; i < decoder->SurfaceFreeN; ++i) {
        decoder->SurfacesFree[i] = -1;
    }

    for (i = 0; i < decoder->SurfaceUsedN; ++i) {
        decoder->SurfacesUsed[i] = -1;
    }

    decoder->SurfaceFreeN = 0;
    decoder->SurfaceUsedN = 0;
}

///
/// Get a free surface.
///
/// @param decoder  CUVID hw decoder
///
/// @returns the oldest free surface
///
static int CuvidGetVideoSurface0(CuvidDecoder *decoder) {
    int surface;
    int i;

    if (!decoder->SurfaceFreeN) {
        // Error(_("video/cuvid: out of surfaces\n"));
        return -1;
    }
    // use oldest surface
    surface = decoder->SurfacesFree[0];

    decoder->SurfaceFreeN--;
    for (i = 0; i < decoder->SurfaceFreeN; ++i) {
        decoder->SurfacesFree[i] = decoder->SurfacesFree[i + 1];
    }
    decoder->SurfacesFree[i] = -1;
    // save as used
    decoder->SurfacesUsed[decoder->SurfaceUsedN++] = surface;

    return surface;
}

///
/// Release a surface.
///
/// @param decoder  CUVID hw decoder
/// @param surface  surface no longer used
///
static void CuvidReleaseSurface(CuvidDecoder *decoder, int surface) {
    int i;

    if (decoder->frames[surface]) {
        av_frame_free(&decoder->frames[surface]);
    }
#ifdef PLACEBO
    SharedContext;
    if (p->has_dma_buf) {
        if (decoder->pl_frames[surface].planes[0].texture) {
            if (decoder->pl_frames[surface].planes[0].texture->params.shared_mem.handle.fd) {
                close(decoder->pl_frames[surface].planes[0].texture->params.shared_mem.handle.fd);
            }
            pl_tex_destroy(p->gpu, &decoder->pl_frames[surface].planes[0].texture);
        }
        if (decoder->pl_frames[surface].planes[1].texture) {
            if (decoder->pl_frames[surface].planes[1].texture->params.shared_mem.handle.fd) {
                close(decoder->pl_frames[surface].planes[1].texture->params.shared_mem.handle.fd);
            }
            pl_tex_destroy(p->gpu, &decoder->pl_frames[surface].planes[1].texture);
        }
    }
    NoContext;
#else
#ifdef VAAPI
    if (decoder->images[surface * Planes]) {
        DestroyImageKHR(eglGetCurrentDisplay(), decoder->images[surface * Planes]);
        DestroyImageKHR(eglGetCurrentDisplay(), decoder->images[surface * Planes + 1]);

        if (decoder->fds[surface * Planes]) {
            close(decoder->fds[surface * Planes]);
        }
        if (decoder->fds[surface * Planes + 1]) {
            close(decoder->fds[surface * Planes + 1]);
        }
    }
    decoder->fds[surface * Planes] = 0;
    decoder->fds[surface * Planes + 1] = 0;
    decoder->images[surface * Planes] = 0;
    decoder->images[surface * Planes + 1] = 0;
#endif
#endif
    for (i = 0; i < decoder->SurfaceUsedN; ++i) {
        if (decoder->SurfacesUsed[i] == surface) {
            // no problem, with last used
            decoder->SurfacesUsed[i] = decoder->SurfacesUsed[--decoder->SurfaceUsedN];
            decoder->SurfacesFree[decoder->SurfaceFreeN++] = surface;
            return;
        }
    }
    Fatal(_("video/cuvid: release surface %#08x, which is not in use\n"), surface);
}

///
/// Debug CUVID decoder frames drop...
///
/// @param decoder  CUVID hw decoder
///
static void CuvidPrintFrames(const CuvidDecoder *decoder) {
    Debug(3, "video/cuvid: %d missed, %d duped, %d dropped frames of %d,%d\n", decoder->FramesMissed,
          decoder->FramesDuped, decoder->FramesDropped, decoder->FrameCounter, decoder->FramesDisplayed);
#ifndef DEBUG
    (void)decoder;
#endif
}

int CuvidTestSurfaces() {
    int i = 0;

    if (CuvidDecoders[0] != NULL) {
        if (i = atomic_read(&CuvidDecoders[0]->SurfacesFilled) < VIDEO_SURFACES_MAX - 1)
            return i;
        return 0;
    } else
        return 0;
}

#ifdef VAAPI
struct mp_egl_config_attr {
    int attrib;
    const char *name;
};

#define MPGL_VER(major, minor) (((major)*100) + (minor)*10)
#define MPGL_VER_GET_MAJOR(ver) ((unsigned)(ver) / 100)
#define MPGL_VER_GET_MINOR(ver) ((unsigned)(ver) % 100 / 10)
#define MP_EGL_ATTRIB(id)                                                                                             \
    { id, #id }


const int mpgl_preferred_gl_versions[] = {460, 440, 430, 400, 330, 320, 310, 300, 210, 0};

static bool create_context_cb(EGLDisplay display, int es_version, EGLContext *out_context, EGLConfig *out_config,
                              int *bpp) {

    EGLenum api;
    EGLint rend, *attribs;
    const char *name;

    switch (es_version) {
        case 0:
            api = EGL_OPENGL_API;
            rend = EGL_OPENGL_BIT;
            name = "Desktop OpenGL";
            break;
        case 2:
            api = EGL_OPENGL_ES_API;
            rend = EGL_OPENGL_ES2_BIT;
            name = "GLES 2.x";
            break;
        case 3:
            api = EGL_OPENGL_ES_API;
            rend = EGL_OPENGL_ES3_BIT;
            name = "GLES 3.x";
            break;
        default:
            Fatal(_("Wrong ES version \n"));
            ;
    }

    if (!eglBindAPI(api)) {
        Fatal(_(" Could not bind API!\n"));
    }

    Debug(3, "Trying to create %s context \n", name);

    EGLint attributes8[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, rend,           EGL_NONE};
    EGLint attributes10[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RED_SIZE,
                             10,
                             EGL_GREEN_SIZE,
                             10,
                             EGL_BLUE_SIZE,
                             10,
                             EGL_ALPHA_SIZE,
                             2,
                             EGL_RENDERABLE_TYPE,
                             rend,
                             EGL_NONE};
    EGLint num_configs = 0;

    attribs = attributes10;
    *bpp = 10;
    if (!eglChooseConfig(display, attributes10, NULL, 0,
                         &num_configs)) { // try 10 Bit
        EglCheck();
        Debug(3, " 10 Bit egl Failed\n");
        attribs = attributes8;
        *bpp = 8;
        if (!eglChooseConfig(display, attributes8, NULL, 0,
                             &num_configs)) { // try 8 Bit
            num_configs = 0;
        }
    } else if (num_configs == 0) {
        EglCheck();
        Debug(3, " 10 Bit egl Failed\n");
        attribs = attributes8;
        *bpp = 8;
        if (!eglChooseConfig(display, attributes8, NULL, 0,
                             &num_configs)) { // try 8 Bit
            num_configs = 0;
        }
    }

    EGLConfig *configs = malloc(sizeof(EGLConfig) * num_configs);

    if (!eglChooseConfig(display, attribs, configs, num_configs, &num_configs))
        num_configs = 0;

    if (!num_configs) {
        free(configs);
        Debug(3, "Could not choose EGLConfig for %s!\n", name);
        return false;
    }

    EGLConfig config = configs[0];

    free(configs);
    EGLContext *egl_ctx = NULL;

    if (es_version) {
        eglAttrs[0] = EGL_CONTEXT_CLIENT_VERSION;
        eglAttrs[1] = es_version;
        eglAttrs[2] = EGL_NONE;
        egl_ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, eglAttrs);
    } else {
        for (int n = 0; mpgl_preferred_gl_versions[n]; n++) {
            int ver = mpgl_preferred_gl_versions[n];

            eglAttrs[0] = EGL_CONTEXT_MAJOR_VERSION;
            eglAttrs[1] = MPGL_VER_GET_MAJOR(ver);
            eglAttrs[2] = EGL_CONTEXT_MINOR_VERSION;
            eglAttrs[3] = MPGL_VER_GET_MINOR(ver);
            eglAttrs[4] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
            eglAttrs[5] = ver >= 320 ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT : 0;
            eglAttrs[6] = EGL_NONE;

            egl_ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, eglAttrs);
            EglCheck();
            if (egl_ctx) {
                Debug(3, "Use %d GLVersion\n", ver);
                break;
            }
        }
    }

    if (!egl_ctx) {
        Debug(3, "Could not create EGL context for %s!\n", name);
        return false;
    }

    *out_context = egl_ctx;
    *out_config = config;
    eglVersion = es_version;
    return true;
}

void make_egl(void) {
    int bpp;

    CreateImageKHR = (void *)eglGetProcAddress("eglCreateImageKHR");
    DestroyImageKHR = (void *)eglGetProcAddress("eglDestroyImageKHR");
    EGLImageTargetTexture2DOES = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    eglCreateSyncKHR = (void *)eglGetProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR = (void *)eglGetProcAddress("eglDestroySyncKHR");
    eglWaitSyncKHR = (void *)eglGetProcAddress("eglWaitSyncKHR");
    eglClientWaitSyncKHR = (void *)eglGetProcAddress("eglClientWaitSyncKHR");
    eglDupNativeFenceFDANDROID = (void *)eglGetProcAddress("eglDupNativeFenceFDANDROID");

    if (!CreateImageKHR || !DestroyImageKHR || !EGLImageTargetTexture2DOES || !eglCreateSyncKHR)
        Fatal(_("Can't get EGL Extentions\n"));
#ifndef USE_DRM
    eglDisplay = eglGetDisplay(XlibDisplay);
#endif
    if (!eglInitialize(eglDisplay, NULL, NULL)) {
        Fatal(_("Could not initialize EGL.\n"));
    }

    if (!create_context_cb(eglDisplay, 0, &eglContext, &eglConfig, &bpp)) {
        Fatal(_("Could not create EGL Context\n"));
    }
    int vID;

    eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vID);
    Debug(3, "chose visual 0x%x bpp %d\n", vID, bpp);
#ifdef USE_DRM
    InitBo(bpp);
#else
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)VideoWindow, NULL);

    if (eglSurface == EGL_NO_SURFACE) {
        Fatal(_("Could not create EGL surface!\n"));
    }
#endif
    if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        Fatal(_("Could not make context current!\n"));
    }
    EglEnabled = 1;
#ifdef USE_DRM
    drm_swap_buffers();
#endif
}
#endif

///
/// Allocate new CUVID decoder.
///
/// @param stream   video stream
///
/// @returns a new prepared cuvid hardware decoder.
///
static CuvidDecoder *CuvidNewHwDecoder(VideoStream *stream) {

    CuvidDecoder *decoder;

    int i = 0;

    // setenv ("DISPLAY", ":0", 0);

    Debug(3, "Cuvid New HW Decoder\n");
    if ((unsigned)CuvidDecoderN >= sizeof(CuvidDecoders) / sizeof(*CuvidDecoders)) {
        Error(_("video/cuvid: out of decoders\n"));
        return NULL;
    }
#ifdef CUVID
    if ((i = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, X11DisplayName, NULL, 0)) != 0) {
        Fatal("codec: can't allocate HW video codec context err %04x", i);
    }
#endif
#if defined(VAAPI)
    // if ((i = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
    // ":0.0" , NULL, 0)) != 0 ) {
    if ((i = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", NULL, 0)) != 0) {
        Fatal("codec: can't allocate HW video codec context err %04x", i);
    }
#endif
    HwDeviceContext = av_buffer_ref(hw_device_ctx);

    if (!(decoder = calloc(1, sizeof(*decoder)))) {
        Error(_("video/cuvid: out of memory\n"));
        return NULL;
    }
#if defined(VAAPI)
    VaDisplay = TO_VAAPI_DEVICE_CTX(HwDeviceContext)->display;
    decoder->VaDisplay = VaDisplay;
#endif
    decoder->Window = VideoWindow;
    // decoder->VideoX = 0;  // done by calloc
    // decoder->VideoY = 0;
    decoder->VideoWidth = VideoWindowWidth;
    decoder->VideoHeight = VideoWindowHeight;

    for (i = 0; i < CODEC_SURFACES_MAX; ++i) {
        decoder->SurfacesUsed[i] = -1;
        decoder->SurfacesFree[i] = -1;
    }

    //
    // setup video surface ring buffer
    //
    atomic_set(&decoder->SurfacesFilled, 0);

    for (i = 0; i < VIDEO_SURFACES_MAX; ++i) {
        decoder->SurfacesRb[i] = -1;
    }

    decoder->OutputWidth = VideoWindowWidth;
    decoder->OutputHeight = VideoWindowHeight;
    decoder->PixFmt = AV_PIX_FMT_NONE;

    decoder->Stream = stream;
    if (!CuvidDecoderN) { // FIXME: hack sync on audio
        decoder->SyncOnAudio = 1;
    }
    decoder->Closing = -300 - 1;
    decoder->PTS = AV_NOPTS_VALUE;

    CuvidDecoders[CuvidDecoderN++] = decoder;

    return decoder;
}

///
/// Cleanup CUVID.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidCleanup(CuvidDecoder *decoder) {
    int i;

    Debug(3, "Cuvid Clean up\n");

    if (decoder->SurfaceFreeN || decoder->SurfaceUsedN) {
        CuvidDestroySurfaces(decoder);
    }
    //
    // reset video surface ring buffer
    //
    atomic_set(&decoder->SurfacesFilled, 0);

    for (i = 0; i < VIDEO_SURFACES_MAX; ++i) {
        decoder->SurfacesRb[i] = -1;
    }
    decoder->SurfaceRead = 0;
    decoder->SurfaceWrite = 0;
    decoder->SurfaceField = 0;

    decoder->SyncCounter = 0;
    decoder->FrameCounter = 0;
    decoder->FramesDisplayed = 0;
    decoder->StartCounter = 0;
    decoder->Closing = 0;
    decoder->PTS = AV_NOPTS_VALUE;
    VideoDeltaPTS = 0;
}

///
/// Destroy a CUVID decoder.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidDelHwDecoder(CuvidDecoder *decoder) {
    int i;

    Debug(3, "cuvid del hw decoder \n");
    if (decoder == CuvidDecoders[0])
        VideoThreadLock();
#ifndef PLACEBO
#ifdef CUVID
    glXMakeCurrent(XlibDisplay, VideoWindow, glxSharedContext);
    GlxCheck();
#else
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext);
    EglCheck();
#endif
#endif
#if defined PLACEBO || defined VAAPI
    if (decoder->SurfaceFreeN || decoder->SurfaceUsedN) {
        CuvidDestroySurfaces(decoder);
    }
#endif
    if (decoder == CuvidDecoders[0])
        VideoThreadUnlock();

    // glXMakeCurrent(XlibDisplay, None, NULL);
    for (i = 0; i < CuvidDecoderN; ++i) {
        if (CuvidDecoders[i] == decoder) {
            CuvidDecoders[i] = NULL;
            // copy last slot into empty slot
            if (i < --CuvidDecoderN) {
                CuvidDecoders[i] = CuvidDecoders[CuvidDecoderN];
            }
            // CuvidCleanup(decoder);
            CuvidPrintFrames(decoder);
#ifdef CUVID
            if (decoder->cuda_ctx && CuvidDecoderN == 1) {
                cuCtxDestroy(decoder->cuda_ctx);
            }
#endif
            free(decoder);
            return;
        }
    }
    Error(_("video/cuvid: decoder not in decoder list.\n"));
}

static int CuvidGlxInit(__attribute__((unused)) const char *display_name) {

#if !defined PLACEBO || defined PLACEBO_GL

    EglInit();
    if (EglEnabled) {
#ifdef CUVID
        GlxSetupWindow(VideoWindow, VideoWindowWidth, VideoWindowHeight, glxContext);
#else
        GlxSetupWindow(VideoWindow, VideoWindowWidth, VideoWindowHeight, eglContext);
#endif
    }

    if (!EglEnabled) {
        Fatal(_("video/egl: egl init error\n"));
    }
#else
    EglEnabled = 0;
#endif

    return 1;
}

///
/// CUVID cleanup.
///
static void CuvidExit(void) {
    int i;

    for (i = 0; i < CuvidDecoderN; ++i) {
        if (CuvidDecoders[i]) {
            CuvidDelHwDecoder(CuvidDecoders[i]);
            CuvidDecoders[i] = NULL;
        }
    }
    CuvidDecoderN = 0;

    Debug(3, "CuvidExit\n");
}

///
/// Update output for new size or aspect ratio.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidUpdateOutput(CuvidDecoder *decoder) {
    VideoUpdateOutput(decoder->InputAspect, decoder->InputWidth, decoder->InputHeight, decoder->Resolution,
                      decoder->VideoX, decoder->VideoY, decoder->VideoWidth, decoder->VideoHeight, &decoder->OutputX,
                      &decoder->OutputY, &decoder->OutputWidth, &decoder->OutputHeight, &decoder->CropX,
                      &decoder->CropY, &decoder->CropWidth, &decoder->CropHeight);
}

void SDK_CHECK_ERROR_GL() {
    GLenum gl_error = glGetError();

    if (gl_error != GL_NO_ERROR) {
        Fatal(_("video/cuvid: SDL error %d\n"), gl_error);
    }
}

#ifdef CUVID
// copy image and process using CUDA
void generateCUDAImage(CuvidDecoder *decoder, int index, const AVFrame *frame, int image_width, int image_height,
                       int bytes) {
    int n;

    for (n = 0; n < 2; n++) {
        // widthInBytes must account for the chroma plane
        // elements being two samples wide.
        CUDA_MEMCPY2D cpy = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .dstMemoryType = CU_MEMORYTYPE_ARRAY,
            .srcDevice = (CUdeviceptr)frame->data[n],
            .srcPitch = frame->linesize[n],
            .srcY = 0,
            .dstArray = decoder->cu_array[index][n],
            .WidthInBytes = image_width * bytes,
            .Height = n == 0 ? image_height : image_height / 2,
        };
        checkCudaErrors(cu->cuMemcpy2D(&cpy));
    }
}

#endif

#ifdef PLACEBO
void createTextureDst(CuvidDecoder *decoder, int anz, unsigned int size_x, unsigned int size_y,
                      enum AVPixelFormat PixFmt) {
    int n, i; 
    const struct pl_fmt_t *fmt;
    //struct pl_tex *tex;
    struct pl_frame *img;
    struct pl_plane *pl;

    SharedContext;
    // printf("Create textures and planes %d %d\n",size_x,size_y);
    Debug(3, "video/vulkan: create %d Textures Format %s w %d h %d \n", anz,
          PixFmt == AV_PIX_FMT_NV12 ? "NV12" : "P010", size_x, size_y);

    for (i = 0; i < anz; i++) { // number of texture
        if (decoder->frames[i]) {
            av_frame_free(&decoder->frames[i]);
            decoder->frames[i] = NULL;
        }
        for (n = 0; n < 2; n++) { // number of planes
            bool ok = true;

            if (PixFmt == AV_PIX_FMT_NV12) {
                fmt = pl_find_named_fmt(p->gpu, n == 0 ? "r8" : "rg8"); // 8 Bit YUV
            } else {
                fmt = pl_find_named_fmt(p->gpu, n == 0 ? "r16" : "rg16"); // 10 Bit YUV
            }
            if (decoder->pl_frames[i].planes[n].texture) {
                // #ifdef VAAPI
                if (decoder->pl_frames[i].planes[n].texture->params.shared_mem.handle.fd) {
                    close(decoder->pl_frames[i].planes[n].texture->params.shared_mem.handle.fd);
                }
                // #endif
                pl_tex_destroy(p->gpu,
                               &decoder->pl_frames[i].planes[n].texture); // delete old texture
            }

            if (p->has_dma_buf == 0) {
                decoder->pl_frames[i].planes[n].texture = pl_tex_create(
                    p->gpu, &(struct pl_tex_params) {
                        .w = n == 0 ? size_x : size_x / 2, .h = n == 0 ? size_y : size_y / 2, .d = 0, .format = fmt,
                        .sampleable = true, .host_writable = true, .blit_dst = true,
#if PL_API_VER < 159
                        .sample_mode = PL_TEX_SAMPLE_LINEAR, .address_mode = PL_TEX_ADDRESS_CLAMP,
#endif
#if !defined PLACEBO_GL
                        .export_handle = PL_HANDLE_FD,
#endif
                    });
            }

            // make planes for image
            pl = &decoder->pl_frames[i].planes[n];
            pl->components = n == 0 ? 1 : 2;
            pl->shift_x = 0.0f;
            pl->shift_y = 0.0f;
            if (n == 0) {
                pl->component_mapping[0] = PL_CHANNEL_Y;
                pl->component_mapping[1] = -1;
                pl->component_mapping[2] = -1;
                pl->component_mapping[3] = -1;
            } else {
                pl->shift_x = -0.5f; // PL_CHROMA_LEFT
                pl->component_mapping[0] = PL_CHANNEL_U;
                pl->component_mapping[1] = PL_CHANNEL_V;
                pl->component_mapping[2] = -1;
                pl->component_mapping[3] = -1;
            }
            if (!ok) {
                Fatal(_("Unable to create placebo textures"));
            }
#ifdef CUVID
            int fd = dup(decoder->pl_frames[i].planes[n].texture->shared_mem.handle.fd);
            CUDA_EXTERNAL_MEMORY_HANDLE_DESC ext_desc = {
                .type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD,
                .handle.fd = fd,
                .size =
                    decoder->pl_frames[i].planes[n].texture->shared_mem.size, // image_width * image_height * bytes,
                .flags = 0,
            };
            checkCudaErrors(
                cu->cuImportExternalMemory(&decoder->ebuf[i * 2 + n].mem, &ext_desc)); // Import Memory segment
            CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC tex_desc = {
                .offset = decoder->pl_frames[i].planes[n].texture->shared_mem.offset,
                .arrayDesc =
                    {
                        .Width = n == 0 ? size_x : size_x / 2,
                        .Height = n == 0 ? size_y : size_y / 2,
                        .Depth = 0,
                        .Format = PixFmt == AV_PIX_FMT_NV12 ? CU_AD_FORMAT_UNSIGNED_INT8 : CU_AD_FORMAT_UNSIGNED_INT16,
                        .NumChannels = n == 0 ? 1 : 2,
                        .Flags = 0,
                    },
                .numLevels = 1,
            };
            checkCudaErrors(cu->cuExternalMemoryGetMappedMipmappedArray(&decoder->ebuf[i * 2 + n].mma,
                                                                        decoder->ebuf[i * 2 + n].mem, &tex_desc));
            checkCudaErrors(cu->cuMipmappedArrayGetLevel(&decoder->cu_array[i][n], decoder->ebuf[i * 2 + n].mma, 0));
#endif
        }
        // make image
        img = &decoder->pl_frames[i];
#if PL_API_VER < 159
        img->signature = i;
        img->width = size_x;
        img->height = size_y;
#endif
        img->num_planes = 2;
        img->repr.sys = PL_COLOR_SYSTEM_BT_709; // overwritten later
        img->repr.levels = PL_COLOR_LEVELS_TV;
        img->repr.alpha = PL_ALPHA_UNKNOWN;
        img->color.primaries = pl_color_primaries_guess(size_x, size_y); // Gammut  overwritten later
        img->color.transfer = PL_COLOR_TRC_BT_1886;                      // overwritten later
        img->num_overlays = 0;
    }
    NoContext;
}

#ifdef VAAPI

// copy image and process using CUDA
void generateVAAPIImage(CuvidDecoder *decoder, int index, const AVFrame *frame, int image_width, int image_height) {
    int n;
    VAStatus status;
    VADRMPRIMESurfaceDescriptor desc;
    
    vaSyncSurface(decoder->VaDisplay, (unsigned int)frame->data[3]);
    status =
        vaExportSurfaceHandle(decoder->VaDisplay, (unsigned int)frame->data[3], VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                              VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &desc);

    if (status != VA_STATUS_SUCCESS) {
        printf("Fehler beim export VAAPI Handle\n");
        return;
    }
    // vaSyncSurface(decoder->VaDisplay, (unsigned int)frame->data[3]);

    Lock_and_SharedContext;

    for (n = 0; n < 2; n++) { //  Set DMA_BUF from VAAPI decoder to Textures
        int id = desc.layers[n].object_index[0];
        int fd = desc.objects[id].fd;
        uint32_t size = desc.objects[id].size;
        uint32_t offset = desc.layers[n].offset[0];

        struct pl_fmt_t *fmt;

        if (fd == -1) {
            printf("Fehler beim Import von Surface %d\n", index);
            return;
        }

        if (!size) {
            size = n == 0 ? desc.width * desc.height : desc.width * desc.height / 2;
        }

        //	fmt = pl_find_fourcc(p->gpu,desc.lsayers[n].drm_format);

        if (decoder->PixFmt == AV_PIX_FMT_NV12) {
            fmt = pl_find_named_fmt(p->gpu, n == 0 ? "r8" : "rg8"); // 8 Bit YUV
        } else {
            fmt = pl_find_fourcc(p->gpu,
                                 n == 0 ? 0x20363152 : 0x32335247); // 10 Bit YUV
        }

        assert(fmt != NULL);
#ifdef PLACEBO_GL
        fmt->fourcc = desc.layers[n].drm_format;
#endif

        struct pl_tex_params tex_params = {
            .w = n == 0 ? image_width : image_width / 2,
            .h = n == 0 ? image_height : image_height / 2,
            .d = 0,
            .format = fmt,
            .sampleable = true,
            .host_writable = false,
            .blit_dst = true,
            .renderable = true,
#if PL_API_VER < 159
            .address_mode = PL_TEX_ADDRESS_CLAMP,
            .sample_mode = PL_TEX_SAMPLE_LINEAR,
#endif
            .import_handle = PL_HANDLE_DMA_BUF,
            .shared_mem =
                (struct pl_shared_mem){
                    .handle =
                        {
                            .fd = fd,
                        },
                    .size = size,
                    .offset = offset,
                    .stride_h = n == 0 ? image_height : image_height / 2,
                    .stride_w = desc.layers[n].pitch[0],
                    .drm_format_mod = desc.objects[id].drm_format_modifier,
                },
        };

        // printf("vor create  Object %d with fd %d import size %u offset  %d
        // %dx%d\n",id,fd,size,offset, tex_params.w,tex_params.h);

        if (decoder->pl_frames[index].planes[n].texture) {
            pl_tex_destroy(p->gpu, &decoder->pl_frames[index].planes[n].texture);
        }

        decoder->pl_frames[index].planes[n].texture = pl_tex_create(p->gpu, &tex_params);
    }

    Unlock_and_NoContext;

}
#endif

#else // no PLACEBO

void createTextureDst(CuvidDecoder *decoder, int anz, unsigned int size_x, unsigned int size_y,
                      enum AVPixelFormat PixFmt) {

    int n, i;

    Debug(3, "video: create %d Textures Format %s w %d h %d \n", anz, PixFmt == AV_PIX_FMT_NV12 ? "NV12" : "P010",
          size_x, size_y);

#ifdef USE_DRM
    // set_video_mode(size_x,size_y);	// switch Mode here (highly
    // experimental)
#endif

#ifdef CUVID
    glXMakeCurrent(XlibDisplay, VideoWindow, glxSharedContext);
    GlxCheck();
#else
#ifdef USE_DRM
    pthread_mutex_lock(&OSDMutex);
#endif
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglSharedContext);
#endif

    glGenBuffers(1, &vao_buffer);
    GlxCheck();
    // create texture planes
    glGenTextures(CODEC_SURFACES_MAX * Planes, decoder->gl_textures);
    GlxCheck();

    for (i = 0; i < anz; i++) {
        for (n = 0; n < Planes; n++) { // number of planes

            glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[i * Planes + n]);
            GlxCheck();
            // set basic parameters
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (PixFmt == AV_PIX_FMT_NV12)
                glTexImage2D(GL_TEXTURE_2D, 0, n == 0 ? GL_R8 : GL_RG8, n == 0 ? size_x : size_x / 2,
                             n == 0 ? size_y : size_y / 2, 0, n == 0 ? GL_RED : GL_RG, GL_UNSIGNED_BYTE, NULL);
            else
                glTexImage2D(GL_TEXTURE_2D, 0, n == 0 ? GL_R16 : GL_RG16, n == 0 ? size_x : size_x / 2,
                             n == 0 ? size_y : size_y / 2, 0, n == 0 ? GL_RED : GL_RG, GL_UNSIGNED_SHORT, NULL);
            SDK_CHECK_ERROR_GL();
            // register this texture with CUDA
#ifdef CUVID
            checkCudaErrors(cu->cuGraphicsGLRegisterImage(&decoder->cu_res[i][n], decoder->gl_textures[i * Planes + n],
                                                          GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
            checkCudaErrors(cu->cuGraphicsMapResources(1, &decoder->cu_res[i][n], 0));
            checkCudaErrors(
                cu->cuGraphicsSubResourceGetMappedArray(&decoder->cu_array[i][n], decoder->cu_res[i][n], 0, 0));
            checkCudaErrors(cu->cuGraphicsUnmapResources(1, &decoder->cu_res[i][n], 0));
#endif
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    GlxCheck();
#ifdef VAAPI
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#ifdef USE_DRM
    pthread_mutex_unlock(&OSDMutex);
#endif
#endif
}

#ifdef VAAPI
#define MP_ARRAY_SIZE(s) (sizeof(s) / sizeof((s)[0]))
#define ADD_ATTRIB(name, value)                                                                                       \
    do {                                                                                                              \
        assert(num_attribs + 3 < MP_ARRAY_SIZE(attribs));                                                             \
        attribs[num_attribs++] = (name);                                                                              \
        attribs[num_attribs++] = (value);                                                                             \
        attribs[num_attribs] = EGL_NONE;                                                                              \
    } while (0)


#define ADD_DMABUF_PLANE_ATTRIBS(plane, fd, offset, stride)         \
    do {                                                            \
        ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _FD_EXT,           \
                   fd);                                             \
        ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _OFFSET_EXT,       \
                   offset);                                         \
        ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _PITCH_EXT,        \
                   stride);                                         \
    } while (0)

#define ADD_DMABUF_PLANE_MODIFIERS(plane, mod)                      \
    do {                                                            \
        ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _MODIFIER_LO_EXT,  \
                   (uint32_t) ((mod) & 0xFFFFFFFFlu));              \
        ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _MODIFIER_HI_EXT,  \
                   (uint32_t) (((mod) >> 32u) & 0xFFFFFFFFlu));     \
    } while (0)

void generateVAAPIImage(CuvidDecoder *decoder, VASurfaceID index, const AVFrame *frame, int image_width,
                        int image_height) {
    VAStatus status;

#if defined(VAAPI)
    VADRMPRIMESurfaceDescriptor desc;

    vaSyncSurface(decoder->VaDisplay, (VASurfaceID)(uintptr_t)frame->data[3]);
    status = vaExportSurfaceHandle(decoder->VaDisplay, (VASurfaceID)(uintptr_t)frame->data[3],
                                   VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                   VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &desc);

    if (status != VA_STATUS_SUCCESS) {
        printf("Fehler beim export VAAPI Handle\n");
        return;
    }
    // vaSyncSurface(decoder->VaDisplay, (VASurfaceID) (uintptr_t)
    // frame->data[3]);
#endif
    pthread_mutex_lock(&OSDMutex);
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglSharedContext);
    EglCheck();

    for (int n = 0; n < Planes; n++) {
        int attribs[20] = {EGL_NONE};
        uint num_attribs = 0;
        int id = desc.layers[n].object_index[0];
        int fd = desc.objects[id].fd;

#if defined(VAAPI)
//Debug(3,"Plane %d w %d h %d layers %d planes %d pitch %d format %04x\n",n,image_width,image_height,desc.num_layers,desc.layers[n].num_planes,desc.layers[n].pitch[0],desc.layers[n].drm_format);
       
        
        ADD_ATTRIB(EGL_WIDTH, n == 0 ? image_width : image_width / 2);
        ADD_ATTRIB(EGL_HEIGHT, n == 0 ? image_height : image_height / 2);
        ADD_DMABUF_PLANE_MODIFIERS(0, desc.objects[id].drm_format_modifier);
        ADD_ATTRIB(EGL_LINUX_DRM_FOURCC_EXT, desc.layers[n].drm_format);
        ADD_DMABUF_PLANE_ATTRIBS(0, fd, desc.layers[n].offset[0],desc.layers[n].pitch[0]);
        
#endif

        decoder->images[index * Planes + n] =
            CreateImageKHR(eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

        if (!decoder->images[index * Planes + n])
            goto esh_failed;

        glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[index * Planes + n]);
        EGLImageTargetTexture2DOES(GL_TEXTURE_2D, decoder->images[index * Planes + n]);
        if (n==0) {
          decoder->fds[index * Planes + n] = fd;
          
        }
        else if (fd == decoder->fds[index * Planes]) {
          decoder->fds[index * Planes + n] = 0;
        }
        else {
            decoder->fds[index * Planes + n] = fd;
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    EglCheck();
    pthread_mutex_unlock(&OSDMutex);
    return;

esh_failed:
    Debug(3, "Failure in generateVAAPIImage\n");
    for (int n = 0; n < Planes; n++)
        close(desc.objects[n].fd);
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    EglCheck();
    pthread_mutex_unlock(&OSDMutex);
}
#endif
#endif

///
/// Configure CUVID for new video format.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidSetupOutput(CuvidDecoder *decoder) {
    // FIXME: need only to create and destroy surfaces for size changes
    //	    or when number of needed surfaces changed!
    decoder->Resolution = VideoResolutionGroup(decoder->InputWidth, decoder->InputHeight, decoder->Interlaced);
    CuvidCreateSurfaces(decoder, decoder->InputWidth, decoder->InputHeight, decoder->PixFmt);

    CuvidUpdateOutput(decoder); // update aspect/scaling

    window_width = decoder->OutputWidth;
    window_height = decoder->OutputHeight;
}

///
/// Get a free surface.	 Called from ffmpeg.
///
/// @param decoder  CUVID hw decoder
/// @param video_ctx	ffmpeg video codec context
///
/// @returns the oldest free surface
///
static unsigned CuvidGetVideoSurface(CuvidDecoder *decoder, const AVCodecContext *video_ctx) {

    (void)video_ctx;

    return CuvidGetVideoSurface0(decoder);
}

#if defined(VAAPI) || defined(YADIF)
static void CuvidSyncRenderFrame(CuvidDecoder *decoder, const AVCodecContext *video_ctx, AVFrame *frame);

int push_filters(AVCodecContext *dec_ctx, CuvidDecoder *decoder, AVFrame *frame) {

    int ret;
    AVFrame *filt_frame = av_frame_alloc();

    /* push the decoded frame into the filtergraph */
    if (av_buffersrc_add_frame_flags(decoder->buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
    }

    // printf("Interlaced %d tff
    // %d\n",frame->interlaced_frame,frame->top_field_first);
    /* pull filtered frames from the filtergraph */
    while ((ret = av_buffersink_get_frame(decoder->buffersink_ctx, filt_frame)) >= 0) {
        filt_frame->pts /= 2;
        decoder->Interlaced = 0;
        CuvidSyncRenderFrame(decoder, dec_ctx, filt_frame);
        filt_frame = av_frame_alloc(); // get new frame
    }
    av_frame_free(&filt_frame);
    av_frame_free(&frame);
    return ret;
}

int init_filters(AVCodecContext *dec_ctx, CuvidDecoder *decoder, AVFrame *frame) {
    enum AVPixelFormat format = PIXEL_FORMAT;

#ifdef VAAPI
    const char *filters_descr = "deinterlace_vaapi=rate=field:auto=1";
#endif
#ifdef YADIF
    const char *filters_descr = "yadif_cuda=1:0:1"; // mode=send_field,parity=tff,deint=interlaced";
    enum AVPixelFormat pix_fmts[] = {format, AV_PIX_FMT_NONE};
#endif

    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVBufferSrcParameters *src_params;

    if (decoder->filter_graph)
        avfilter_graph_free(&decoder->filter_graph);

    decoder->filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !decoder->filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted
     * here. */
    snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", dec_ctx->width,
             dec_ctx->height, format, 1, 90000, dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&decoder->buffersrc_ctx, buffersrc, "in", args, NULL, decoder->filter_graph);
    if (ret < 0) {
        Debug(3, "Cannot create buffer source\n");
        goto end;
    }
    src_params = av_buffersrc_parameters_alloc();
    src_params->hw_frames_ctx = frame->hw_frames_ctx;
    src_params->format = format;
    src_params->time_base.num = 1;
    src_params->time_base.den = 90000;
    src_params->width = dec_ctx->width;
    src_params->height = dec_ctx->height;
    src_params->frame_rate.num = 50;
    src_params->frame_rate.den = 1;
    src_params->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;

    // printf("width %d height %d hw_frames_ctx
    // %p\n",dec_ctx->width,dec_ctx->height ,frame->hw_frames_ctx);
    ret = av_buffersrc_parameters_set(decoder->buffersrc_ctx, src_params);
    if (ret < 0) {
        Debug(3, "Cannot set hw_frames_ctx to src\n");
        goto end;
    }
    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&decoder->buffersink_ctx, buffersink, "out", NULL, NULL, decoder->filter_graph);
    if (ret < 0) {
        Debug(3, "Cannot create buffer sink\n");
        goto end;
    }
#ifdef YADIF
    ret = av_opt_set_int_list(decoder->buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        Debug(3, "Cannot set output pixel format\n");
        goto end;
    }
#endif
    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = decoder->buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = decoder->buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(decoder->filter_graph, filters_descr, &inputs, &outputs, NULL)) < 0) {
        Debug(3, "Cannot set graph parse %d\n", ret);
        goto end;
    }

    if ((ret = avfilter_graph_config(decoder->filter_graph, NULL)) < 0) {
        Debug(3, "Cannot set graph config %d\n", ret);
        goto end;
    }

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
#endif

#ifdef VAAPI
static int init_generic_hwaccel(CuvidDecoder *decoder, enum AVPixelFormat hw_fmt, AVCodecContext *video_ctx) {

    AVBufferRef *new_frames_ctx = NULL;

    if (!hw_device_ctx) {
        Debug(3, "Missing device context.\n");
        goto error;
    }

    if (avcodec_get_hw_frames_parameters(video_ctx, hw_device_ctx, hw_fmt, &new_frames_ctx) < 0) {
        Debug(3, "Hardware decoding of this stream is unsupported?\n");
        goto error;
    }

    AVHWFramesContext *new_fctx = (void *)new_frames_ctx->data;

    // We might be able to reuse a previously allocated frame pool.
    if (decoder->cached_hw_frames_ctx) {
        AVHWFramesContext *old_fctx = (void *)decoder->cached_hw_frames_ctx->data;

        Debug(3, "CMP %d:%d %d:%d %d:%d %d:%d %d:%d\n,", new_fctx->format, old_fctx->format, new_fctx->sw_format,
              old_fctx->sw_format, new_fctx->width, old_fctx->width, new_fctx->height, old_fctx->height,
              new_fctx->initial_pool_size, old_fctx->initial_pool_size);
        if (new_fctx->format != old_fctx->format || new_fctx->sw_format != old_fctx->sw_format ||
            new_fctx->width != old_fctx->width || new_fctx->height != old_fctx->height ||
            new_fctx->initial_pool_size != old_fctx->initial_pool_size) {
            Debug(3, "delete old cache");
            if (decoder->filter_graph)
                avfilter_graph_free(&decoder->filter_graph);
            av_buffer_unref(&decoder->cached_hw_frames_ctx);
        }
    }

    if (!decoder->cached_hw_frames_ctx) {
        new_fctx->initial_pool_size = 17;
        if (av_hwframe_ctx_init(new_frames_ctx) < 0) {
            Debug(3, "Failed to allocate hw frames.\n");
            goto error;
        }

        decoder->cached_hw_frames_ctx = new_frames_ctx;
        new_frames_ctx = NULL;
    }

    video_ctx->hw_frames_ctx = av_buffer_ref(decoder->cached_hw_frames_ctx);
    if (!video_ctx->hw_frames_ctx)
        goto error;

    av_buffer_unref(&new_frames_ctx);
    return 0;

error:
    Debug(3, "Error with hwframes\n");
    av_buffer_unref(&new_frames_ctx);
    av_buffer_unref(&decoder->cached_hw_frames_ctx);
    return -1;
}
#endif
///
/// Callback to negotiate the PixelFormat.
///
/// @param fmt	is the list of formats which are supported by the codec,
///	it is terminated by -1 as 0 is a valid format, the
///	formats are ordered by quality.
///

static enum AVPixelFormat Cuvid_get_format(CuvidDecoder *decoder, AVCodecContext *video_ctx,
                                           const enum AVPixelFormat *fmt) {
    const enum AVPixelFormat *fmt_idx;
    int bitformat16 = 0;

    VideoDecoder *ist = video_ctx->opaque;

    //
    //	look through formats
    //
    Debug(3, "%s: codec %d fmts:\n", __FUNCTION__, video_ctx->codec_id);
    for (fmt_idx = fmt; *fmt_idx != AV_PIX_FMT_NONE; fmt_idx++) {
        Debug(3, "\t%#010x %s\n", *fmt_idx, av_get_pix_fmt_name(*fmt_idx));
        if (*fmt_idx == AV_PIX_FMT_P010LE)
            bitformat16 = 1;
    }
#ifdef VAAPI
    if (video_ctx->profile == FF_PROFILE_HEVC_MAIN_10)
        bitformat16 = 1;
#endif

    Debug(3, "%s: codec %d fmts:\n", __FUNCTION__, video_ctx->codec_id);
    for (fmt_idx = fmt; *fmt_idx != AV_PIX_FMT_NONE; fmt_idx++) {
        Debug(3, "\t%#010x %s\n", *fmt_idx, av_get_pix_fmt_name(*fmt_idx));
        // check supported pixel format with entry point
        switch (*fmt_idx) {
            case PIXEL_FORMAT:
                break;
            default:
                continue;
        }
        break;
    }

    Debug(3, "video profile %d codec id %d\n", video_ctx->profile, video_ctx->codec_id);
    if (*fmt_idx == AV_PIX_FMT_NONE) {
        Fatal(_("video: no valid pixfmt found\n"));
    }

    if (*fmt_idx != PIXEL_FORMAT) {
        Fatal(_("video: no valid profile found\n"));
    }

    //	  decoder->newchannel = 1;
#ifdef VAAPI
    init_generic_hwaccel(decoder, PIXEL_FORMAT, video_ctx);
#endif
    if (ist->GetFormatDone) {
        return PIXEL_FORMAT;
    }

    ist->GetFormatDone = 1;

    Debug(3, "video: create decoder 16bit?=%d %dx%d old	 %d %d\n", bitformat16, video_ctx->width, video_ctx->height,
          decoder->InputWidth, decoder->InputHeight);

    if (*fmt_idx == PIXEL_FORMAT) { // HWACCEL used

        //  Check image, format, size
        //
        if (bitformat16) {
            decoder->PixFmt = AV_PIX_FMT_YUV420P; // 10 Bit Planar
            ist->hwaccel_output_format = AV_PIX_FMT_YUV420P;
        } else {
            decoder->PixFmt = AV_PIX_FMT_NV12; // 8 Bit Planar
            ist->hwaccel_output_format = AV_PIX_FMT_NV12;
        }

        if ((video_ctx->width != decoder->InputWidth || video_ctx->height != decoder->InputHeight) &&
            decoder->TrickSpeed == 0) {

            //	     if (decoder->TrickSpeed == 0) {
#ifdef PLACEBO
            VideoThreadLock();
#endif
            decoder->newchannel = 1;
            CuvidCleanup(decoder);
            decoder->InputAspect = video_ctx->sample_aspect_ratio;
            decoder->InputWidth = video_ctx->width;
            decoder->InputHeight = video_ctx->height;
            decoder->Interlaced = 0;
            decoder->SurfacesNeeded = VIDEO_SURFACES_MAX + 1;
            CuvidSetupOutput(decoder);
#ifdef PLACEBO
            VideoThreadUnlock();
            // dont show first frame
#endif
        } else {
            decoder->SyncCounter = 0;
            decoder->FrameCounter = 0;
            decoder->FramesDisplayed = 0;
            decoder->StartCounter = 0;
            decoder->Closing = 0;
            decoder->PTS = AV_NOPTS_VALUE;
            VideoDeltaPTS = 0;
            decoder->InputAspect = video_ctx->sample_aspect_ratio;
            CuvidUpdateOutput(decoder); // update aspect/scaling
        }

#if defined YADIF && defined CUVID
        int deint;
        if (VideoDeinterlace[decoder->Resolution] == VideoDeinterlaceYadif) {
            deint = 0;
            ist->filter = 1; // init yadif_cuda
        } else {
            deint = 2;
            ist->filter = 0;
        }
        CuvidMessage(2, "deint = %s\n", deint == 0 ? "Yadif" : "Cuda");
        if (av_opt_set_int(video_ctx->priv_data, "deint", deint, 0) < 0) { // adaptive
            Fatal(_("codec: can't set option deint to video codec!\n"));
        }
#endif

        CuvidMessage(2, "GetFormat Init ok %dx%d\n", video_ctx->width, video_ctx->height);
        decoder->InputAspect = video_ctx->sample_aspect_ratio;
#ifdef CUVID
        ist->active_hwaccel_id = HWACCEL_CUVID;
#else
        if (video_ctx->codec_id == AV_CODEC_ID_HEVC) {
            ist->filter = 0;
        }
        else if (VideoDeinterlace[decoder->Resolution]) {// need deinterlace
            ist->filter = 1;                       // init deint vaapi
        }
        else {
            ist->filter = 0;
        }

        ist->active_hwaccel_id = HWACCEL_VAAPI;
#endif
        ist->hwaccel_pix_fmt = PIXEL_FORMAT;
        return PIXEL_FORMAT;
    }
    Fatal(_("NO Format valid"));
    return *fmt_idx;
}

#ifdef USE_GRAB
void swapc(unsigned char *x, unsigned char *y) {
    unsigned char temp = *x;

    *x = *y;
    *y = temp;
}

#ifdef PLACEBO
int get_RGB(CuvidDecoder *decoder, struct pl_overlay *ovl) {
#else
int get_RGB(CuvidDecoder *decoder) {
#endif

#ifdef PLACEBO
    struct pl_render_params render_params = pl_render_default_params;
    struct pl_frame target = {0};
    const struct pl_fmt_t *fmt;

    int  x1=0, y1=0, x0=0, y0=0;
    float faktorx, faktory;
#endif

    uint8_t *base;
    int width;
    int height;
    
    int current;
    

    base = decoder->grabbase;
    width = decoder->grabwidth;
    height = decoder->grabheight;

    current = decoder->SurfacesRb[decoder->SurfaceRead];

#ifndef PLACEBO
    GLint texLoc;
    GLuint fb, texture;
    glGenTextures(1, &texture);
    GlxCheck();
    glBindTexture(GL_TEXTURE_2D, texture);
    GlxCheck();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GlxCheck();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GlxCheck();

    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Debug(3, "video/cuvid: grab Framebuffer is not complete!");
        return 0;
    }

    glViewport(0, 0, width, height);
    GlxCheck();

    if (gl_prog == 0)
        gl_prog = sc_generate(gl_prog, decoder->ColorSpace); // generate shader programm

    glUseProgram(gl_prog);
    texLoc = glGetUniformLocation(gl_prog, "texture0");
    glUniform1i(texLoc, 0);
    texLoc = glGetUniformLocation(gl_prog, "texture1");
    glUniform1i(texLoc, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[current * Planes + 0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[current * Planes + 1]);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);

    render_pass_quad(1, 0.0, 0.0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);

    if (OsdShown && decoder->grab == 2) {
        int x, y, h, w;
        GLint texLoc;

        if (OsdShown == 1) {
            if (OSDtexture)
                glDeleteTextures(1, &OSDtexture);
            //	      pthread_mutex_lock(&OSDMutex);
            glGenTextures(1, &OSDtexture);
            glBindTexture(GL_TEXTURE_2D, OSDtexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OSDxsize, OSDysize, 0, GL_RGBA, GL_UNSIGNED_BYTE, posd);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            //	      pthread_mutex_unlock(&OSDMutex);
            OsdShown = 2;
        }

        y = OSDy * height / VideoWindowHeight;
        x = OSDx * width / VideoWindowWidth;
        h = OSDysize * height / VideoWindowHeight;
        w = OSDxsize * width / VideoWindowWidth;
        glViewport(x, (height - h - y), w, h);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (gl_prog_osd == 0)
            gl_prog_osd = sc_generate_osd(gl_prog_osd); // generate shader programm

        glUseProgram(gl_prog_osd);
        texLoc = glGetUniformLocation(gl_prog_osd, "texture0");
        glUniform1i(texLoc, 0);

        glActiveTexture(GL_TEXTURE0);

        //	pthread_mutex_lock(&OSDMutex);
        glBindTexture(GL_TEXTURE_2D, OSDtexture);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        render_pass_quad(0, 0.0, 0.0);
        //	pthread_mutex_unlock(&OSDMutex);

        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0);
    }
    glFlush();
    // Debug(3, "Read pixels %d %d\n", width, height);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, base);
    GlxCheck();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fb);
    glDeleteTextures(1, &texture);

#else // Placebo
    faktorx = (float)width / (float)VideoWindowWidth;
    faktory = (float)height / (float)VideoWindowHeight;
#ifdef PLACEBO_GL
    fmt = pl_find_named_fmt(p->gpu, "rgba8"); // bgra8 not supported
#else
    fmt = pl_find_named_fmt(p->gpu, "bgra8");
#endif
#if PL_API_VER < 159
    target.fbo = pl_tex_create(
        p->gpu, &(struct pl_tex_params) {
#else
    target.num_planes = 1;
    target.planes[0].components = 4;
    target.planes[0].component_mapping[0] = PL_CHANNEL_R;
    target.planes[0].component_mapping[1] = PL_CHANNEL_G;
    target.planes[0].component_mapping[2] = PL_CHANNEL_B;
    target.planes[0].component_mapping[3] = PL_CHANNEL_A;
    target.planes[0].texture = pl_tex_create(
        p->gpu, &(struct pl_tex_params) {

#endif
            .w = width, .h = height, .d = 0, .format = fmt, .sampleable = true, .renderable = true, .blit_dst = true,
            .host_readable = true,
#if PL_API_VER < 159
            .sample_mode = PL_TEX_SAMPLE_LINEAR, .address_mode = PL_TEX_ADDRESS_CLAMP,
#endif
        });
#if PL_API_VER >= 100
    target.crop.x0 = (float)decoder->OutputX * faktorx;
    target.crop.y0 = (float)decoder->OutputY * faktory;
    target.crop.x1 = (float)(decoder->OutputX + decoder->OutputWidth) * faktorx;
    target.crop.y1 = (float)(decoder->OutputY + decoder->OutputHeight) * faktory;
#else
    target.dst_rect.x0 = (float)decoder->OutputX * faktorx;
    target.dst_rect.y0 = (float)decoder->OutputY * faktory;
    target.dst_rect.x1 = (float)(decoder->OutputX + decoder->OutputWidth) * faktorx;
    target.dst_rect.y1 = (float)(decoder->OutputY + decoder->OutputHeight) * faktory;
#endif
    target.repr.sys = PL_COLOR_SYSTEM_RGB;
    target.repr.levels = PL_COLOR_LEVELS_PC;
    target.repr.alpha = PL_ALPHA_UNKNOWN;
    target.repr.bits.sample_depth = 8;
    target.repr.bits.color_depth = 8;
    target.repr.bits.bit_shift = 0;
    target.color.primaries = PL_COLOR_PRIM_BT_709;
    target.color.transfer = PL_COLOR_TRC_BT_1886;

    if (ovl) {
        target.overlays = ovl;
        target.num_overlays = 1;
#if PL_API_VER < 229
#ifdef PLACEBO_GL
        x0 = ovl->rect.x0;
        y1 = ovl->rect.y0;
        x1 = ovl->rect.x1;
        y0 = ovl->rect.y1;
        ovl->rect.x0 = (float)x0 * faktorx;
        ovl->rect.y0 = (float)y0 * faktory;
        ovl->rect.x1 = (float)x1 * faktorx;
        ovl->rect.y1 = (float)y1 * faktory;
#else
        x0 = ovl->rect.x0;
        y0 = ovl->rect.y0;
        x1 = ovl->rect.x1;
        y1 = ovl->rect.y1;
        ovl->rect.x0 = (float)x0 * faktorx;
        ovl->rect.y0 = (float)y0 * faktory;
        ovl->rect.x1 = (float)x1 * faktorx;
        ovl->rect.y1 = (float)y1 * faktory;
#endif
#else
#ifdef PLACEBO_GL
        x0 = part.dst.x0;
        y1 = part.dst.y0;
        x1 = part.dst.x1;
        y0 = part.dst.y1;
        part.dst.x0 = (float)x0 * faktorx;
        part.dst.y0 = (float)y0 * faktory;
        part.dst.x1 = (float)x1 * faktorx;
        part.dst.y1 = (float)y1 * faktory;
#else
        x0 = part.dst.x0;
        y0 = part.dst.y0;
        x1 = part.dst.x1;
        y1 = part.dst.y1;
        part.dst.x0 = (float)x0 * faktorx;
        part.dst.y0 = (float)y0 * faktory;
        part.dst.x1 = (float)x1 * faktorx;
        part.dst.y1 = (float)y1 * faktory;
#endif
#endif


    } else {
        target.overlays = 0;
        target.num_overlays = 0;
    }

    if (!pl_render_image(p->renderer, &decoder->pl_frames[current], &target, &render_params)) {
        Fatal(_("Failed rendering frame!\n"));
    }

    pl_gpu_finish(p->gpu);

    if (ovl) {
#if PL_API_VER < 229
#ifdef PLACEBO_GL
        ovl->rect.x0 = x0;
        ovl->rect.y0 = y1;
        ovl->rect.x1 = x1;
        ovl->rect.y1 = y0;
#else
        ovl->rect.x0 = x0;
        ovl->rect.y0 = y0;
        ovl->rect.x1 = x1;
        ovl->rect.y1 = y1;
#endif
#else
#ifdef PLACEBO_GL
        part.dst.x0 = x0;
        part.dst.y0 = y1;
        part.dst.x1 = x1;
        part.dst.y1 = y0;
#else
        part.dst.x0 = x0;
        part.dst.y0 = y0;
        part.dst.x1 = x1;
        part.dst.y1 = y1;
#endif
#endif
    }

    pl_tex_download(
        p->gpu, &(struct pl_tex_transfer_params) { // download Data
#if PL_API_VER < 159
            .tex = target.fbo,
#else
            .tex = target.planes[0].texture,
#endif
            .ptr = base,
        });
#if PL_API_VER < 159
    pl_tex_destroy(p->gpu, &target.fbo);
#else
    pl_tex_destroy(p->gpu, &target.planes[0].texture);
#endif
#ifdef PLACEBO_GL
    unsigned char *b = base;

    for (int i = 0; i < width * height * 4; i += 4)
        swapc(&b[i + 0], &b[i + 2]);
#endif
#endif
    return 0;
}

///
/// Grab output surface already locked.
///
/// @param ret_size[out]    size of allocated surface copy
/// @param ret_width[in,out]	width of output
/// @param ret_height[in,out]	height of output
///
static uint8_t *CuvidGrabOutputSurfaceLocked(int *ret_size, int *ret_width, int *ret_height, int mitosd) {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint8_t *base;
    VdpRect source_rect;
    CuvidDecoder *decoder;

    decoder = CuvidDecoders[0];
    if (decoder == NULL) // no video aktiv
        return NULL;

        // surface = CuvidSurfacesRb[CuvidOutputSurfaceIndex];

        // get real surface size
#ifdef PLACEBO
    width = decoder->VideoWidth;
    height = decoder->VideoHeight;
#else
    width = decoder->InputWidth;
    height = decoder->InputHeight;
#endif

    // Debug(3, "video/cuvid: grab %dx%d\n", width, height);

    source_rect.x0 = 0;
    source_rect.y0 = 0;
    source_rect.x1 = width;
    source_rect.y1 = height;

    if (ret_width && ret_height) {
        if (*ret_width <= -64) { // this is an Atmo grab service request
            int overscan;

            // calculate aspect correct size of analyze image
            width = *ret_width * -1;
            height = (width * source_rect.y1) / source_rect.x1;

            // calculate size of grab (sub) window
            overscan = *ret_height;

            if (overscan > 0 && overscan <= 200) {
                source_rect.x0 = source_rect.x1 * overscan / 1000;
                source_rect.x1 -= source_rect.x0;
                source_rect.y0 = source_rect.y1 * overscan / 1000;
                source_rect.y1 -= source_rect.y0;
            }
        } else {
            if (*ret_width > 0 && (unsigned)*ret_width < width) {
                width = *ret_width;
            }
            if (*ret_height > 0 && (unsigned)*ret_height < height) {
                height = *ret_height;
            }
        }

        // printf("video/cuvid: grab source  dim %dx%d\n", width, height);

        size = width * height * sizeof(uint32_t);

        base = malloc(size);

        if (!base) {
            Error(_("video/cuvid: out of memory\n"));
            return NULL;
        }
        decoder->grabbase = base;
        decoder->grabwidth = width;
        decoder->grabheight = height;
        if (mitosd)
            decoder->grab = 2;
        else
            decoder->grab = 1;

        while (decoder->grab) {
            usleep(1000); // wait for data
        }
        // Debug(3,"got grab data\n");

        if (ret_size) {
            *ret_size = size;
        }
        if (ret_width) {
            *ret_width = width;
        }
        if (ret_height) {
            *ret_height = height;
        }
        return base;
    }

    return NULL;
}

///
/// Grab output surface.
///
/// @param ret_size[out]    size of allocated surface copy
/// @param ret_width[in,out]	width of output
/// @param ret_height[in,out]	height of output
///
static uint8_t *CuvidGrabOutputSurface(int *ret_size, int *ret_width, int *ret_height, int mitosd) {
    uint8_t *img;

    img = CuvidGrabOutputSurfaceLocked(ret_size, ret_width, ret_height, mitosd);
    return img;
}

#endif

///
/// Queue output surface.
///
/// @param decoder  CUVID hw decoder
/// @param surface  output surface
/// @param softdec  software decoder
///
/// @note we can't mix software and hardware decoder surfaces
///
static void CuvidQueueVideoSurface(CuvidDecoder *decoder, int surface, int softdec) {
    int old;

    ++decoder->FrameCounter;

    // can't wait for output queue empty
    if (atomic_read(&decoder->SurfacesFilled) >= VIDEO_SURFACES_MAX) {
        Warning(_("video/cuvid: output buffer full, dropping frame (%d/%d)\n"), ++decoder->FramesDropped,
                decoder->FrameCounter);
        if (!(decoder->FramesDisplayed % 300)) {
            CuvidPrintFrames(decoder);
        }
        // software surfaces only
        if (softdec) {
            CuvidReleaseSurface(decoder, surface);
        }
        return;
    }
    //
    // Check and release, old surface
    //
    if ((old = decoder->SurfacesRb[decoder->SurfaceWrite]) != -1) {
        // now we can release the surface, software surfaces only
        if (softdec) {
            CuvidReleaseSurface(decoder, old);
        }
    }

    Debug(4, "video/cuvid: yy video surface %#08x@%d ready\n", surface, decoder->SurfaceWrite);

    decoder->SurfacesRb[decoder->SurfaceWrite] = surface;
    decoder->SurfaceWrite = (decoder->SurfaceWrite + 1) % VIDEO_SURFACES_MAX;
    atomic_inc(&decoder->SurfacesFilled);
}

#if 0
extern void Nv12ToBgra32(uint8_t * dpNv12, int nNv12Pitch, uint8_t * dpBgra, int nBgraPitch, int nWidth, int nHeight,
    int iMatrix, cudaStream_t stream);
extern void P016ToBgra32(uint8_t * dpNv12, int nNv12Pitch, uint8_t * dpBgra, int nBgraPitch, int nWidth, int nHeight,
    int iMatrix, cudaStream_t stream);
extern void ResizeNv12(unsigned char *dpDstNv12, int nDstPitch, int nDstWidth, int nDstHeight,
    unsigned char *dpSrcNv12, int nSrcPitch, int nSrcWidth, int nSrcHeight, unsigned char *dpDstNv12UV);
extern void ResizeP016(unsigned char *dpDstP016, int nDstPitch, int nDstWidth, int nDstHeight,
    unsigned char *dpSrcP016, int nSrcPitch, int nSrcWidth, int nSrcHeight, unsigned char *dpDstP016UV);
extern void cudaLaunchNV12toARGBDrv(uint32_t * d_srcNV12, size_t nSourcePitch, uint32_t * d_dstARGB, size_t nDestPitch,
    uint32_t width, uint32_t height, CUstream streamID);
#endif

///
/// Render a ffmpeg frame.
///
/// @param decoder  CUVID hw decoder
/// @param video_ctx	ffmpeg video codec context
/// @param frame    frame to display
///
static void CuvidRenderFrame(CuvidDecoder *decoder, const AVCodecContext *video_ctx, AVFrame *frame) {
    int surface;
    enum AVColorSpace color;

    if (decoder->Closing == 1) {
        av_frame_free(&frame);
        return;
    }

    if (!decoder->Closing) {
        VideoSetPts(&decoder->PTS, decoder->Interlaced, video_ctx, frame);
    }

    // update aspect ratio changes
    if (decoder->InputWidth && decoder->InputHeight && av_cmp_q(decoder->InputAspect, frame->sample_aspect_ratio)) {
        Debug(3, "video/cuvid: aspect ratio changed\n");

        decoder->InputAspect = frame->sample_aspect_ratio;
        // printf("new aspect
        // %d:%d\n",frame->sample_aspect_ratio.num,frame->sample_aspect_ratio.den);
        CuvidUpdateOutput(decoder);
    }

    //	printf("Orig colorspace %d Primaries %d TRC %d	-------
    //",frame->colorspace,frame->color_primaries,frame->color_trc);

    // Fix libav colorspace failure
    color = frame->colorspace;
    if (color == AVCOL_SPC_UNSPECIFIED) { // failure with RTL HD and all SD channels
                                        // with vaapi
        if (frame->width > 720) {
            color = AVCOL_SPC_BT709;
        } else {
            color = AVCOL_SPC_BT470BG;
        }
    }
    if (color == AVCOL_SPC_RGB) { // Cuvid decoder failure with SD channels
        color = AVCOL_SPC_BT470BG;
    }
    frame->colorspace = color;

    // Fix libav Color primaries failures
    if (frame->color_primaries == AVCOL_PRI_UNSPECIFIED) { // failure with RTL HD and all SD channels with
                                                         // vaapi
        if (frame->width > 720) {
            frame->color_primaries = AVCOL_PRI_BT709;
        } else {
            frame->color_primaries = AVCOL_PRI_BT470BG;
        }
    }
    if (frame->color_primaries == AVCOL_PRI_RESERVED0) // cuvid decoder failure with SD channels
        frame->color_primaries = AVCOL_PRI_BT470BG;

    // Fix libav Color TRC failures
    if (frame->color_trc == AVCOL_TRC_UNSPECIFIED) { // failure with RTL HD and all
                                                   // SD channels with vaapi
        if (frame->width > 720) {
            frame->color_trc = AVCOL_TRC_BT709;
        } else {
            frame->color_trc = AVCOL_TRC_SMPTE170M;
        }
    }
    if (frame->color_trc == AVCOL_TRC_RESERVED0) // cuvid decoder failure with SD channels
        frame->color_trc = AVCOL_TRC_SMPTE170M;

    //    printf("Patched  colorspace %d Primaries %d TRC
    //    %d\n",frame->colorspace,frame->color_primaries,frame->color_trc);
    //
    //	Copy data from frame to image
    //
    if (video_ctx->pix_fmt == PIXEL_FORMAT) {
        int w = decoder->InputWidth;
        int h = decoder->InputHeight;
        decoder->ColorSpace = color; // save colorspace
        decoder->trc = frame->color_trc;
        decoder->color_primaries = frame->color_primaries;

        surface = CuvidGetVideoSurface0(decoder);

        if (surface == -1) { // no free surfaces
            Debug(3, "no more surfaces\n");
            av_frame_free(&frame);
            return;
        }

#if defined(VAAPI) && defined(PLACEBO)
        if (p->has_dma_buf) { // Vulkan supports DMA_BUF no copy required
            generateVAAPIImage(decoder, surface, frame, w, h);
        } else { // we need to Copy the frame via RAM
            AVFrame *output;

            VideoThreadLock();
            vaSyncSurface(decoder->VaDisplay, (unsigned int)frame->data[3]);
            output = av_frame_alloc();
            av_hwframe_transfer_data(output, frame, 0);
            av_frame_copy_props(output, frame);
            // printf("Save Surface ID %d %p
            // %p\n",surface,decoder->pl_frames[surface].planes[0].texture,decoder->pl_frames[surface].planes[1].texture);
            bool ok = pl_tex_upload(p->gpu, &(struct pl_tex_transfer_params){
                                                .tex = decoder->pl_frames[surface].planes[0].texture,
#if PL_API_VER < 292
                                                .stride_w = output->linesize[0],
                                                .stride_h = h,
#else
                                                .row_pitch = output->linesize[0],
                                                .depth_pitch = h,
#endif
                                                .ptr = output->data[0],
                                                .rc.x1 = w,
                                                .rc.y1 = h,
                                                .rc.z1 = 0,
                                            });
            ok &= pl_tex_upload(p->gpu, &(struct pl_tex_transfer_params){
                                            .tex = decoder->pl_frames[surface].planes[1].texture,
#if PL_API_VER < 292
                                            .stride_w = output->linesize[0] / 2,
                                            .stride_h = h / 2,
#else
                                            .row_pitch = output->linesize[0] / 2,
                                            .depth_pitch = h,
#endif                                           
                                            .ptr = output->data[1],
                                            .rc.x1 = w / 2,
                                            .rc.y1 = h / 2,
                                            .rc.z1 = 0,
                                        });
            av_frame_free(&output);
            VideoThreadUnlock();
        }
#else
#ifdef CUVID
        // copy to texture
        generateCUDAImage(decoder, surface, frame, w, h, decoder->PixFmt == AV_PIX_FMT_NV12 ? 1 : 2);
#else
        // copy to texture
        generateVAAPIImage(decoder, surface, frame, w, h);
#endif
#endif

        CuvidQueueVideoSurface(decoder, surface, 1);
        decoder->frames[surface] = frame;
        return;
    }

    //	 Debug(3,"video/cuvid: pixel format %d not supported\n",
    // video_ctx->pix_fmt);
    av_frame_free(&frame);
    return;
}

///
/// Get hwaccel context for ffmpeg.
///
/// @param decoder  CUVID hw decoder
///
static void *CuvidGetHwAccelContext(CuvidDecoder *decoder) {
    
    (void)decoder;
    Debug(3, "Initializing cuvid hwaccel thread ID:%ld\n", (long int)syscall(186));
    
#ifdef CUVID
    if (decoder->cuda_ctx) {
        Debug(3, "schon passiert\n");
        return NULL;
    }

    if (!cu) {
        int ret = cuda_load_functions(&cu, NULL);
        if (ret < 0) {
            Error(_("Could not dynamically load CUDA\n"));
            return 0;
        }
    }
    checkCudaErrors(cu->cuInit(0));

    checkCudaErrors(cu->cuCtxCreate(&decoder->cuda_ctx, (unsigned int)CU_CTX_SCHED_BLOCKING_SYNC, (CUdevice)0));

    if (decoder->cuda_ctx == NULL)
        Fatal(_("Kein Cuda device gefunden"));
//    unsigned int version;
//    cu->cuCtxGetApiVersion(decoder->cuda_ctx, &version);
//    Debug(3, "***********CUDA API Version %d\n", version);
#endif
    return NULL;
}

///
/// Create and display a black empty surface.
///
/// @param decoder  CUVID hw decoder
///
/// @FIXME: render only video area, not fullscreen!
/// decoder->Output.. isn't correct setup for radio stations
///
static void CuvidBlackSurface(__attribute__((unused)) CuvidDecoder *decoder) {
#ifndef PLACEBO
    glClear(GL_COLOR_BUFFER_BIT);
#endif
    return;
}

///
/// Advance displayed frame of decoder.
///
/// @param decoder  CUVID hw decoder
///
static void CuvidAdvanceDecoderFrame(CuvidDecoder *decoder) {
    // next surface, if complete frame is displayed (1 -> 0)
    if (decoder->SurfaceField) {
        int filled;

        // FIXME: this should check the caller
        // check decoder, if new surface is available
        // need 2 frames for progressive
        // need 4 frames for interlaced
        filled = atomic_read(&decoder->SurfacesFilled);
        if (filled <= 1 + 2 * decoder->Interlaced) {
            // keep use of last surface
            ++decoder->FramesDuped;
            // FIXME: don't warn after stream start, don't warn during pause
            // printf("video: display buffer empty, duping frame (%d/%d) %d\n",
            // decoder->FramesDuped, decoder->FrameCounter,
            // VideoGetBuffers(decoder->Stream));
            return;
        }

        decoder->SurfaceRead = (decoder->SurfaceRead + 1) % VIDEO_SURFACES_MAX;
        atomic_dec(&decoder->SurfacesFilled);
        decoder->SurfaceField = !decoder->Interlaced;
        return;
    }
    // next field
    decoder->SurfaceField = 1;
}

#if defined PLACEBO && PL_API_VER >= 58

static const struct pl_hook *parse_user_shader(char *shader) {
    char tmp[400];

    if (!shader)
        return NULL;

    const struct pl_hook *hook = NULL;
    char *str = NULL;

    //    Debug(3,"Parse user shader %s/%s\n",MyConfigDir,shader);

    sprintf(tmp, "%s/%s", MyConfigDir, shader);
    FILE *f = fopen(tmp, "rb");

    if (!f) {
        Debug(3, "Failed to open shader file %s: %s\n", tmp, strerror(errno));
        goto error;
    }

    int ret = fseek(f, 0, SEEK_END);

    if (ret == -1)
        goto error;
    long length = ftell(f);

    if (length == -1)
        goto error;
    rewind(f);

    str = malloc(length);
    if (!str)
        goto error;
    ret = fread(str, length, 1, f);
    if (ret != 1)
        goto error;

    hook = pl_mpv_user_shader_parse(p->gpu, str, length);
    // fall through
    Debug(3, "User shader %p\n", hook);
error:
    if (f)
        fclose(f);
    free(str);
    return hook;
}
#endif

///
/// Render video surface to output surface.
///
/// @param decoder  CUVID hw decoder
/// @param level    video surface level 0 = bottom
///
#ifdef PLACEBO
static void CuvidMixVideo(CuvidDecoder *decoder, int level, struct pl_frame *target, struct pl_overlay *ovl)
#else
static void CuvidMixVideo(CuvidDecoder *decoder, __attribute__((unused)) int level)
#endif
{
#ifdef PLACEBO
    struct pl_render_params render_params;
    struct pl_deband_params deband;
    struct pl_color_adjustment colors;
    struct pl_cone_params cone;
    //struct pl_tex_vk *vkp;
    struct pl_plane *pl;
    //struct pl_tex *tex0, *tex1;

    struct pl_frame *img;
    //bool ok;
    VdpRect video_src_rect;
    //VdpRect dst_rect;
    VdpRect dst_video_rect;
#endif

    int current;

#ifdef PLACEBO
    
 
#if 0
    if (level) {
        dst_rect.x0 = decoder->VideoX; // video window output (clip)
        dst_rect.y0 = decoder->VideoY;
        dst_rect.x1 = decoder->VideoX + decoder->VideoWidth;
        dst_rect.y1 = decoder->VideoY + decoder->VideoHeight;
    } else {
        dst_rect.x0 = 0; // complete window (clip)
        dst_rect.y0 = 0;
        dst_rect.x1 = VideoWindowWidth;
        dst_rect.y1 = VideoWindowHeight;
    }
#endif
    video_src_rect.x0 = decoder->CropX; // video source (crop)
    video_src_rect.y0 = decoder->CropY;
    video_src_rect.x1 = decoder->CropX + decoder->CropWidth;
    video_src_rect.y1 = decoder->CropY + decoder->CropHeight;

    dst_video_rect.x0 = decoder->OutputX; // video output (scale)
    dst_video_rect.y0 = decoder->OutputY;
    dst_video_rect.x1 = decoder->OutputX + decoder->OutputWidth;
    dst_video_rect.y1 = decoder->OutputY + decoder->OutputHeight;
#endif

   

    current = decoder->SurfacesRb[decoder->SurfaceRead];

#ifdef USE_DRM
    AVFrame *frame;
    AVFrameSideData *sd, *sd1 = NULL, *sd2 = NULL;
    if (!decoder->Closing) {
        frame = decoder->frames[current];
        sd1 = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        sd2 = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);

        set_hdr_metadata(frame->color_primaries, frame->color_trc, sd1, sd2);
    }
#endif

    // Render Progressive frame
#ifndef PLACEBO
    
    GLint texLoc;
    int y; 
    float xcropf, ycropf;

    xcropf = (float)decoder->CropX / (float)decoder->InputWidth;
    ycropf = (float)decoder->CropY / (float)decoder->InputHeight;

    y = VideoWindowHeight - decoder->OutputY - decoder->OutputHeight;
    if (y < 0)
        y = 0;
    glViewport(decoder->OutputX, y, decoder->OutputWidth, decoder->OutputHeight);

    if (gl_prog == 0)
        gl_prog = sc_generate(gl_prog, decoder->ColorSpace); // generate shader programm

    glUseProgram(gl_prog);
    texLoc = glGetUniformLocation(gl_prog, "texture0");
    glUniform1i(texLoc, 0);
    texLoc = glGetUniformLocation(gl_prog, "texture1");
    glUniform1i(texLoc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[current * Planes + 0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, decoder->gl_textures[current * Planes + 1]);

    render_pass_quad(0, xcropf, ycropf);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);

#else
    img = &decoder->pl_frames[current];
    pl = &decoder->pl_frames[current].planes[1];

    memcpy(&deband, &pl_deband_default_params, sizeof(deband));
    memcpy(&render_params, &pl_render_default_params, sizeof(render_params));
    render_params.deband_params = &deband;

    // provide LUT Table
    if (LUTon)
        render_params.lut = p->lut;
    else
        render_params.lut = NULL;
        
   
    // Fix Color Parameters

    switch (decoder->ColorSpace) {
        case AVCOL_SPC_RGB: // BT 601 is reportet as RGB
        case AVCOL_SPC_BT470BG:
            memcpy(&img->repr, &pl_color_repr_sdtv, sizeof(struct pl_color_repr));
            img->color.primaries = PL_COLOR_PRIM_BT_601_625;
            img->color.transfer = PL_COLOR_TRC_BT_1886;
            pl->shift_x = 0.0f;
            break;
        case AVCOL_SPC_BT709:
        case AVCOL_SPC_UNSPECIFIED: //  comes with UHD
            memcpy(&img->repr, &pl_color_repr_hdtv, sizeof(struct pl_color_repr));
            memcpy(&img->color, &pl_color_space_bt709, sizeof(struct pl_color_space));
            pl->shift_x = -0.5f;
            break;

        case AVCOL_SPC_BT2020_NCL:
            memcpy(&img->repr, &pl_color_repr_uhdtv, sizeof(struct pl_color_repr));
            memcpy(&img->color, &pl_color_space_bt2020_hlg, sizeof(struct pl_color_space));
            deband.grain = 0.0f; // no grain in HDR
            pl->shift_x = -0.5f;
            // Kein LUT bei HDR
            render_params.lut = NULL;
#if 0
            if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE))) {
                img->profile = (struct pl_icc_profile){
                    .data = sd->data,
                    .len = sd->size,
                };

                // Needed to ensure profile uniqueness
                pl_icc_profile_compute_signature(&img->profile);
            }

            if ((sd1 = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL))) {
                const AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;

                img->color.sig_peak = clm->MaxCLL / PL_COLOR_SDR_WHITE;
                img->color.sig_avg = clm->MaxFALL / PL_COLOR_SDR_WHITE;
            }

            // This overrides the CLL values above, if both are present
            if ((sd2 = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA))) {
                const AVMasteringDisplayMetadata *mdm = (AVMasteringDisplayMetadata *)sd->data;

                if (mdm->has_luminance)
                    img->color.sig_peak = av_q2d(mdm->max_luminance) / PL_COLOR_SDR_WHITE;
            }

            // Make sure this value is more or less legal
            if (img->color.sig_peak < 1.0 || img->color.sig_peak > 50.0)
                img->color.sig_peak = 0.0;
#endif
#if defined VAAPI || defined USE_DRM
            render_params.peak_detect_params = NULL;
            render_params.deband_params = NULL;
            render_params.dither_params = NULL;
            render_params.skip_anti_aliasing = true;
#endif

            break;

        default: // fallback
            memcpy(&img->repr, &pl_color_repr_hdtv, sizeof(struct pl_color_repr));
            memcpy(&img->color, &pl_color_space_bt709, sizeof(struct pl_color_space));
            pl->shift_x = -0.5f;
            break;
    }

    target->repr.sys = PL_COLOR_SYSTEM_RGB;
    if (VideoStudioLevels)
        target->repr.levels = PL_COLOR_LEVELS_PC;
    else
        target->repr.levels = PL_COLOR_LEVELS_TV;
    target->repr.alpha = PL_ALPHA_UNKNOWN;

    // target.repr.bits.sample_depth = 16;
    // target.repr.bits.color_depth = 16;
    // target.repr.bits.bit_shift =0;

#if USE_DRM
    
    frame = decoder->frames[current];

    switch (VulkanTargetColorSpace) {
        case 0: // Monitor
            memcpy(&target->color, &pl_color_space_monitor, sizeof(struct pl_color_space));
            break;
        case 1: // sRGB
            memcpy(&target->color, &pl_color_space_srgb, sizeof(struct pl_color_space));
            break;
        case 2: // HD TV
            set_hdr_metadata(frame->color_primaries, frame->color_trc, sd1, sd2);
            if (decoder->ColorSpace == AVCOL_SPC_BT470BG) {
                target->color.primaries = PL_COLOR_PRIM_BT_601_625;
                target->color.transfer = PL_COLOR_TRC_BT_1886;
            } else {
                memcpy(&target->color, &pl_color_space_bt709, sizeof(struct pl_color_space));
            }
            break;
        case 3: // HDR TV
            set_hdr_metadata(frame->color_primaries, frame->color_trc, sd1, sd2);
            if (decoder->ColorSpace == AVCOL_SPC_BT2020_NCL) {
                memcpy(&target->color, &pl_color_space_bt2020_hlg, sizeof(struct pl_color_space));
            } else if (decoder->ColorSpace == AVCOL_SPC_BT470BG) {
                target->color.primaries = PL_COLOR_PRIM_BT_601_625;
                target->color.transfer = PL_COLOR_TRC_BT_1886;
                ;
            } else {
                memcpy(&target->color, &pl_color_space_bt709, sizeof(struct pl_color_space));
            }
            break;
        default:
            memcpy(&target->color, &pl_color_space_monitor, sizeof(struct pl_color_space));
            break;
    }
#else
    switch (VulkanTargetColorSpace) {
        case 0: // Monitor
            memcpy(&target->color, &pl_color_space_monitor, sizeof(struct pl_color_space));
            break;
        case 1: // sRGB
            memcpy(&target->color, &pl_color_space_srgb, sizeof(struct pl_color_space));
            break;
        case 2: // HD TV
        case 3: // UHD HDR TV
            memcpy(&target->color, &pl_color_space_bt709, sizeof(struct pl_color_space));
            break;
        default:
            memcpy(&target->color, &pl_color_space_monitor, sizeof(struct pl_color_space));
            break;
    }
#endif

    //  Source crop
    if (VideoScalerTest) { // right side defined scaler

        // Input crop
        img->crop.x0 = video_src_rect.x1 / 2 + 1;
        img->crop.y0 = video_src_rect.y0;
        img->crop.x1 = video_src_rect.x1;
        img->crop.y1 = video_src_rect.y1;
        // Output scale
#ifdef PLACEBO_GL
        target->crop.x0 = dst_video_rect.x1 / 2 + dst_video_rect.x0 / 2 + 1;
        target->crop.y0 = VideoWindowHeight - dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1;
        target->crop.y1 = VideoWindowHeight - dst_video_rect.y1;
#else
        target->crop.x0 = dst_video_rect.x1 / 2 + dst_video_rect.x0 / 2 + 1;
        target->crop.y0 = dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1;
        target->crop.y1 = dst_video_rect.y1;
#endif

    } else {

        img->crop.x0 = video_src_rect.x0;
        img->crop.y0 = video_src_rect.y0;
        img->crop.x1 = video_src_rect.x1;
        img->crop.y1 = video_src_rect.y1;

#ifdef PLACEBO_GL
        target->crop.x0 = dst_video_rect.x0;
        target->crop.y0 = VideoWindowHeight - dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1;
        target->crop.y1 = VideoWindowHeight - dst_video_rect.y1;
#else
        target->crop.x0 = dst_video_rect.x0;
        target->crop.y0 = dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1;
        target->crop.y1 = dst_video_rect.y1;
#endif
    }

#if PL_API_VER < 100
    if (level == 0)
        pl_tex_clear(p->gpu, target->fbo, (float[4]){0});
#else
    if (!level && pl_frame_is_cropped(target))
        pl_frame_clear(p->gpu, target, (float[3]){0});
#endif
    if (VideoColorBlindness) {
        switch (VideoColorBlindness) {
            case 1:
                memcpy(&cone, &pl_vision_protanomaly, sizeof(cone));
                break;
            case 2:
                memcpy(&cone, &pl_vision_deuteranomaly, sizeof(cone));
                break;
            case 3:
                memcpy(&cone, &pl_vision_tritanomaly, sizeof(cone));
                break;
            case 4:
                memcpy(&cone, &pl_vision_monochromacy, sizeof(cone));
                break;
            default:
                memcpy(&cone, &pl_vision_normal, sizeof(cone));
                break;
        }
        cone.strength = VideoColorBlindnessFaktor;
        render_params.cone_params = &cone;
    } else {
        render_params.cone_params = NULL;
    }

    // render_params.upscaler = &pl_filter_ewa_lanczos;

    render_params.upscaler = pl_filter_presets[VideoScaling[decoder->Resolution]].filter;
    render_params.downscaler = pl_filter_presets[VideoScaling[decoder->Resolution]].filter;

    if (level)
#if PL_API_VER < 346
        render_params.skip_target_clearing = 1;
#else
        render_params.border = PL_CLEAR_SKIP;
#endif

    render_params.color_adjustment = &colors;

    colors.brightness = VideoBrightness;
    colors.contrast = VideoContrast;
    colors.saturation = VideoSaturation;
    colors.hue = VideoHue;
    colors.gamma = VideoGamma;
#if PL_API_VER >= 119
    colors.temperature = VideoTemperature;
#endif

    if (ovl) {
        target->overlays = ovl;
        target->num_overlays = 1;
    } else {
        target->overlays = 0;
        target->num_overlays = 0;
    }

#if PL_API_VER >= 58
    if (decoder->newchannel == 1 && !level) { // got new textures
        p->num_shaders = 0;
        for (int i = NUM_SHADERS - 1; i >= 0; i--) { // Remove shaders in invers order
            if (p->hook[i]) {
                pl_mpv_user_shader_destroy(&p->hook[i]);
                p->hook[i] = NULL;
                Debug(3, "remove shader %d\n", i);
            }
        }
        for (int i = 0; i < num_shaders; i++) {
            if (p->hook[i] == NULL && shadersp[i]) {
                p->hook[i] = parse_user_shader(shadersp[i]);
                if (!p->hook[i])
                    shadersp[i] = 0;
                else
                    p->num_shaders++;
            }
        }
    }
    render_params.hooks = &p->hook;
    if (level || ovl || (video_src_rect.x1 > dst_video_rect.x1) || (video_src_rect.y1 > dst_video_rect.y1)) {
        render_params.num_hooks = 0; // no user shaders when OSD activ or downward scaling or PIP
    } else {
        render_params.num_hooks = p->num_shaders;
    }
#endif

    


    if (decoder->newchannel && current == 0) {
        colors.brightness = -1.0f;
        colors.contrast = 0.0f;
        if (!pl_render_image(p->renderer, &decoder->pl_frames[current], target, &render_params)) {
            Debug(3, "Failed rendering first frame!\n");
        }
        decoder->newchannel = 2;
        return;
    }

    decoder->newchannel = 0;
    //    uint64_t tt = GetusTicks();
    if (!pl_render_image(p->renderer, &decoder->pl_frames[current], target, &render_params)) {
        Debug(4, "Failed rendering frame!\n");
    }
    
    
    // printf("Rendertime %ld -- \n,",GetusTicks() - tt);

    if (VideoScalerTest) { // left side test scaler

        // Source crop
        img->crop.x0 = video_src_rect.x0;
        img->crop.y0 = video_src_rect.y0;
        img->crop.x1 = video_src_rect.x1 / 2;
        img->crop.y1 = video_src_rect.y1;
#ifdef PLACEBO_GL
        target->crop.x0 = dst_video_rect.x0;
        target->crop.y1 = dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1 / 2 + dst_video_rect.x0 / 2;
        target->crop.y0 = dst_video_rect.y1;
#else
        // Video aspect ratio
        target->crop.x0 = dst_video_rect.x0;
        target->crop.y0 = dst_video_rect.y0;
        target->crop.x1 = dst_video_rect.x1 / 2 + dst_video_rect.x0 / 2;
        target->crop.y1 = dst_video_rect.y1;
#endif

        render_params.upscaler = pl_filter_presets[VideoScalerTest - 1].filter;
        render_params.downscaler = pl_filter_presets[VideoScalerTest - 1].filter;

        //	render_params.lut = NULL;
        render_params.num_hooks = 0;
#if PL_API_VER < 346
        render_params.skip_target_clearing = 1;
#else
        render_params.border = PL_CLEAR_SKIP;
#endif

        if (!p->renderertest)
            p->renderertest = pl_renderer_create(p->ctx, p->gpu);

        if (!pl_render_image(p->renderertest, &decoder->pl_frames[current], target, &render_params)) {
            Debug(4, "Failed rendering frame!\n");
        }
    } else if (p->renderertest) {
        pl_renderer_destroy(&p->renderertest);
        p->renderertest = NULL;
    }
#endif
    
}

#ifdef PLACEBO
void make_osd_overlay(int x, int y, int width, int height) {
    const struct pl_fmt *fmt;
    struct pl_overlay *pl;

    

    fmt = pl_find_named_fmt(p->gpu, "rgba8"); // 8 Bit RGB

    pl = &osdoverlay;

#if PL_API_VER < 229
    if (pl->plane.texture && (pl->plane.texture->params.w != width || pl->plane.texture->params.h != height)) {
        pl_tex_destroy(p->gpu, &pl->plane.texture);
#else
    if (pl->tex && (pl->tex->params.w != width || pl->tex->params.h != height)) {
        pl_tex_destroy(p->gpu, &pl->tex);
#endif
    }

#if PL_API_VER < 229
    // make texture for OSD
    if (pl->plane.texture == NULL) {
        pl->plane.texture = pl_tex_create(
            p->gpu, &(struct pl_tex_params) {
                .w = width, .h = height, .d = 0, .format = fmt, .sampleable = true, .host_writable = true,
                .blit_dst = true,
#if PL_API_VER < 159
                .sample_mode = PL_TEX_SAMPLE_LINEAR, .address_mode = PL_TEX_ADDRESS_CLAMP,
#endif
            });
    }
    // make overlay
    pl_tex_clear(p->gpu, pl->plane.texture, (float[4]){0});
    pl->plane.components = 4;
    pl->plane.shift_x = 0.0f;
    pl->plane.shift_y = 0.0f;
    pl->plane.component_mapping[0] = PL_CHANNEL_R;
    pl->plane.component_mapping[1] = PL_CHANNEL_G;
    pl->plane.component_mapping[2] = PL_CHANNEL_B;
    pl->plane.component_mapping[3] = PL_CHANNEL_A;
#else
 // make texture for OSD
    if (pl->tex == NULL) {
        pl->tex = pl_tex_create(
            p->gpu, &(struct pl_tex_params) {
                .w = width, .h = height, .d = 0, .format = fmt, .sampleable = true, .host_writable = true,
                .blit_dst = true,
            });
    }
    // make overlay
    pl_tex_clear(p->gpu, pl->tex, (float[4]){0});
    part.src.x0 = 0.0f;
    part.src.y0 = 0.0f;
    part.src.x1 = width;
    part.src.y1 = height;
#endif
    pl->mode = PL_OVERLAY_NORMAL;
    pl->repr.sys = PL_COLOR_SYSTEM_RGB;
    pl->repr.levels = PL_COLOR_LEVELS_PC;
    pl->repr.alpha = PL_ALPHA_INDEPENDENT;

    memcpy(&osdoverlay.color, &pl_color_space_srgb, sizeof(struct pl_color_space));
#if PL_API_VER < 229
#ifdef PLACEBO_GL
    pl->rect.x0 = x;
    pl->rect.y1 = VideoWindowHeight - y; // Boden von oben
    pl->rect.x1 = x + width;
    pl->rect.y0 = VideoWindowHeight - height - y;
#else
    int offset = VideoWindowHeight - (VideoWindowHeight - height - y) - (VideoWindowHeight - y);
    pl->rect.x0 = x;
    pl->rect.y0 = VideoWindowHeight - y + offset; // Boden von oben
    pl->rect.x1 = x + width;
    pl->rect.y1 = VideoWindowHeight - height - y + offset;
#endif
#else
    osdoverlay.parts = &part;
    osdoverlay.num_parts = 1;
#ifdef PLACEBO_GL
    part.dst.x0 = x;
    part.dst.y1 = VideoWindowHeight - y; // Boden von oben
    part.dst.x1 = x + width;
    part.dst.y0 = VideoWindowHeight - height - y;
#else
    int offset = VideoWindowHeight - (VideoWindowHeight - height - y) - (VideoWindowHeight - y);
    part.dst.x0 = x;
    part.dst.y0 = VideoWindowHeight - y + offset; // Boden von oben
    part.dst.x1 = x + width;
    part.dst.y1 = VideoWindowHeight - height - y + offset;
#endif
#endif
}
#endif
///
/// Display a video frame.
///

static void CuvidDisplayFrame(void) {

    
    
    int i;

#if defined PLACEBO_GL || defined CUVID
    static uint64_t round_time = 0;
    //static uint64_t first_time = 0;
#endif

    int filled;
    CuvidDecoder *decoder;
    
#ifdef PLACEBO
    //uint64_t diff;
    //static float fdiff = 23000.0;
    struct pl_swapchain_frame frame;
    struct pl_frame target;
    //bool ok;

    //const struct pl_fmt *fmt;
    //const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
#else
    int valid_frame = 0;
#endif

#ifndef PLACEBO
    static uint64_t last_time = 0;
    if (CuvidDecoderN)
        CuvidDecoders[0]->Frameproc = (float)(GetusTicks() - last_time) / 1000000.0;

#ifdef CUVID
    glXMakeCurrent(XlibDisplay, VideoWindow, glxThreadContext);
    glXWaitVideoSyncSGI(2, (Count + 1) % 2,
                        &Count); // wait for previous frame to swap
    last_time = GetusTicks();
#else
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglThreadContext);
    EglCheck();

#ifndef USE_DRM
    usleep(5000);
#endif
#endif

    glClear(GL_COLOR_BUFFER_BIT);

#else // PLACEBO

#ifdef PLACEBO_GL
#ifdef CUVID
    glXMakeCurrent(XlibDisplay, VideoWindow, glxThreadContext);
    glXWaitVideoSyncSGI(2, (Count + 1) % 2,
                        &Count); // wait for previous frame to swap
    last_time = GetusTicks();
#else
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglThreadContext);
    EglCheck();
#endif
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    if (CuvidDecoderN) {
        float ldiff = (float)(GetusTicks() - round_time) / 1000000.0;
        if (ldiff < 100.0 && ldiff > 0.0)
            CuvidDecoders[0]->Frameproc = (CuvidDecoders[0]->Frameproc + ldiff + ldiff) / 3.0;
    }

    round_time = GetusTicks();

    if (!p->swapchain)
        return;

#ifdef CUVID
    VideoThreadLock();
#endif

    //last_time = GetusTicks();

    while (!pl_swapchain_start_frame(p->swapchain, &frame)) { // get new frame wait for previous to swap
        usleep(5);
    }

    if (!frame.fbo) {
#ifdef CUVID
        VideoThreadUnlock();
#endif
        return;
    }
#ifdef VAAPI
    VideoThreadLock();
#endif

    pl_frame_from_swapchain(&target, &frame); // make target frame

    if (VideoSurfaceModesChanged) {
        pl_renderer_destroy(&p->renderer);
        p->renderer = pl_renderer_create(p->ctx, p->gpu);
        if (p->renderertest) {
            pl_renderer_destroy(&p->renderertest);
            p->renderertest = NULL;
        }
        VideoSurfaceModesChanged = 0;
    }

#ifdef GAMMA
//    target.color.transfer = PL_COLOR_TRC_LINEAR;
#endif
#endif
    //
    //	Render videos into output
    //
    ///

    for (i = 0; i < CuvidDecoderN; ++i) {

        decoder = CuvidDecoders[i];
        decoder->FramesDisplayed++;
        decoder->StartCounter++;

        filled = atomic_read(&decoder->SurfacesFilled);
        // printf("Filled %d\n",filled);
        //  need 1 frame for progressive, 3 frames for interlaced
        if (filled < 1 + 2 * decoder->Interlaced) {
            // FIXME: rewrite MixVideo to support less surfaces
            if ((VideoShowBlackPicture && !decoder->TrickSpeed) ||
                (VideoShowBlackPicture && decoder->Closing < -300)) {
                CuvidBlackSurface(decoder);
                CuvidMessage(4, "video/cuvid: black surface displayed Filled %d\n",filled);
            }
            continue;
        }
        
#ifdef PLACEBO
        //pthread_mutex_lock(&OSDMutex);
        if (OsdShown == 1) { // New OSD opened
            
            make_osd_overlay(OSDx, OSDy, OSDxsize, OSDysize);
            if (posd) {
                pl_tex_upload(p->gpu, &(struct pl_tex_transfer_params){
                                          // upload OSD
#if PL_API_VER >= 229
                                          .tex = osdoverlay.tex,
#else
                                          .tex = osdoverlay.plane.texture,
#endif
                                          .ptr = posd,
                                      });
            }
            OsdShown = 2;
            
        }

        if (OsdShown == 2) {
            CuvidMixVideo(decoder, i, &target, &osdoverlay);
        } else {
            CuvidMixVideo(decoder, i, &target, NULL);
        }
        //pthread_mutex_unlock(&OSDMutex);
#else
        valid_frame = 1;
        CuvidMixVideo(decoder, i);
#endif
        if (i == 0 && decoder->grab) { // Grab frame
#ifdef PLACEBO
            if (decoder->grab == 2 && OsdShown == 2) {
                get_RGB(decoder, &osdoverlay);
            } else {
                get_RGB(decoder, NULL);
            }
#else
            get_RGB(decoder);
#endif
            decoder->grab = 0;
        }
    }
#ifdef PLACEBO
    pl_gpu_finish(p->gpu);
#endif

#ifndef PLACEBO
    //	add osd to surface

    if (OsdShown && valid_frame) {
        GLint texLoc;
        int x, y, w, h;

        glBindTexture(GL_TEXTURE_2D, 0);
        GlxCheck();

        if (OsdShown == 1) {
            if (OSDtexture)
                glDeleteTextures(1, &OSDtexture);
            pthread_mutex_lock(&OSDMutex);
            glGenTextures(1, &OSDtexture);
            glBindTexture(GL_TEXTURE_2D, OSDtexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OSDxsize, OSDysize, 0, GL_RGBA, GL_UNSIGNED_BYTE, posd);
            GlxCheck();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            GlxCheck();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            glFlush();
            pthread_mutex_unlock(&OSDMutex);
            OsdShown = 2;
        }
        GlxCheck();
        glBindTexture(GL_TEXTURE_2D, 0);
        GlxCheck();
        glEnable(GL_BLEND);
        GlxCheck();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GlxCheck();

        y = OSDy * VideoWindowHeight / OsdHeight;
        x = OSDx * VideoWindowWidth / OsdWidth;
        h = OSDysize * VideoWindowHeight / OsdHeight;
        w = OSDxsize * VideoWindowWidth / OsdWidth;
        glViewport(x, (VideoWindowHeight - h - y), w, h);

        if (gl_prog_osd == 0)
            gl_prog_osd = sc_generate_osd(gl_prog_osd); // generate shader programm

        glUseProgram(gl_prog_osd);
        texLoc = glGetUniformLocation(gl_prog_osd, "texture0");
        glUniform1i(texLoc, 0);

        glActiveTexture(GL_TEXTURE0);

        //	pthread_mutex_lock(&OSDMutex);
        glBindTexture(GL_TEXTURE_2D, OSDtexture);
        render_pass_quad(1, 0, 0);
        //	pthread_mutex_unlock(&OSDMutex);

        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0);
        //	  eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglThreadContext);
    }
#endif

#if defined PLACEBO //  && !defined PLACEBO_GL
    // first_time = GetusTicks();
    if (!pl_swapchain_submit_frame(p->swapchain))
        Fatal(_("Failed to submit swapchain buffer\n")); 
    VideoThreadUnlock();
    pl_swapchain_swap_buffers(p->swapchain); // swap buffers
#ifdef PLACEBO_GL
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    EglCheck();  
#endif
    
   
#else // not PLACEBO
#ifdef CUVID
    glXGetVideoSyncSGI(&Count); // get current frame
    glXSwapBuffers(XlibDisplay, VideoWindow);
    glXMakeCurrent(XlibDisplay, None, NULL);
#else
#ifndef USE_DRM
    eglSwapBuffers(eglDisplay, eglSurface);
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#else
    drm_swap_buffers();
#endif
#endif
#endif

    // FIXME: CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC, &CuvidFrameTime);
    for (i = 0; i < CuvidDecoderN; ++i) {
        // remember time of last shown surface
        CuvidDecoders[i]->FrameTime = CuvidFrameTime;
    }
}

#ifdef PLACEBO_GL
void CuvidSwapBuffer() {
#ifndef USE_DRM
    eglSwapBuffers(eglDisplay, eglSurface);
//    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
//    EGL_NO_CONTEXT);
#else
    drm_swap_buffers();
#endif
}
#endif

///
/// Set CUVID decoder video clock.
///
/// @param decoder  CUVID hardware decoder
/// @param pts	audio presentation timestamp
///
void CuvidSetClock(CuvidDecoder *decoder, int64_t pts) { decoder->PTS = pts; }

///
/// Get CUVID decoder video clock.
///
/// @param decoder  CUVID hw decoder
///
/// FIXME: 20 wrong for 60hz dvb streams
///
static int64_t CuvidGetClock(const CuvidDecoder *decoder) {
    // pts is the timestamp of the latest decoded frame
    if (decoder->PTS == (int64_t)AV_NOPTS_VALUE) {
        return AV_NOPTS_VALUE;
    }
    // subtract buffered decoded frames
    if (decoder->Interlaced) {
        /*
           Info("video: %s =pts field%d #%d\n",
           Timestamp2String(decoder->PTS),
           decoder->SurfaceField,
           atomic_read(&decoder->SurfacesFilled));
         */
        // 1 field is future, 2 fields are past, + 2 in driver queue
        return decoder->PTS - 20 * 90 * (2 * atomic_read(&decoder->SurfacesFilled) - decoder->SurfaceField - 2 + 2);
    }
    // + 2 in driver queue
    return decoder->PTS - 20 * 90 * (atomic_read(&decoder->SurfacesFilled) + SWAP_BUFFER_SIZE + 1); // +2
}

///
/// Set CUVID decoder closing stream flag.
///
/// @param decoder  CUVID decoder
///
static void CuvidSetClosing(CuvidDecoder *decoder) {
    decoder->Closing = 1;
    OsdShown = 0;
}

///
/// Reset start of frame counter.
///
/// @param decoder  CUVID decoder
///
static void CuvidResetStart(CuvidDecoder *decoder) { decoder->StartCounter = 0; }

///
/// Set trick play speed.
///
/// @param decoder  CUVID decoder
/// @param speed    trick speed (0 = normal)
///
static void CuvidSetTrickSpeed(CuvidDecoder *decoder, int speed) {
    decoder->TrickSpeed = speed;
    decoder->TrickCounter = speed;
    if (speed) {
        decoder->Closing = 0;
    }
}

///
/// Get CUVID decoder statistics.
///
/// @param decoder  CUVID decoder
/// @param[out] missed	missed frames
/// @param[out] duped	duped frames
/// @param[out] dropped dropped frames
/// @param[out] count	number of decoded frames
///
void CuvidGetStats(CuvidDecoder *decoder, int *missed, int *duped, int *dropped, int *counter, float *frametime,
                   int *width, int *height, int *color, int *eotf) {
    *missed = decoder->FramesMissed;
    *duped = decoder->FramesDuped;
    *dropped = decoder->FramesDropped;
    *counter = decoder->FrameCounter;
    *frametime = decoder->Frameproc;
    *width = decoder->InputWidth;
    *height = decoder->InputHeight;
    *color = decoder->ColorSpace;
    *eotf = 0;
}

///
/// Sync decoder output to audio.
///
/// trick-speed show frame <n> times
/// still-picture   show frame until new frame arrives
/// 60hz-mode	repeat every 5th picture
/// video>audio slow down video by duplicating frames
/// video<audio speed up video by skipping frames
/// soft-start	show every second frame
///
/// @param decoder  CUVID hw decoder
///
void AudioDelayms(int);
static void CuvidSyncDecoder(CuvidDecoder *decoder) {
    int filled;
    int64_t audio_clock;
    int64_t video_clock;
    static int speedup = 3;

#ifdef GAMMA
    Get_Gamma();
#endif

    video_clock = CuvidGetClock(decoder);

    filled = atomic_read(&decoder->SurfacesFilled);

    if (!decoder->SyncOnAudio) {
        audio_clock = AV_NOPTS_VALUE;
        // FIXME: 60Hz Mode
        goto skip_sync;
    }
    audio_clock = AudioGetClock();
    //     printf("Diff %d %#012" PRIx64 "	%#012" PRIx64"	 filled %d
    //     \n",(video_clock - audio_clock -
    //     VideoAudioDelay)/90,video_clock,audio_clock,filled);
    // 60Hz: repeat every 5th field
    if (Video60HzMode && !(decoder->FramesDisplayed % 6)) {
        if (audio_clock == (int64_t)AV_NOPTS_VALUE || video_clock == (int64_t)AV_NOPTS_VALUE) {
            goto out;
        }
        // both clocks are known
        if (audio_clock + VideoAudioDelay <= video_clock + 25 * 90) {
            goto out;
        }
        // out of sync: audio before video
        if (!decoder->TrickSpeed) {
            goto skip_sync;
        }
    }
    // TrickSpeed
    if (decoder->TrickSpeed) {
        if (decoder->TrickCounter--) {
            goto out;
        }
        decoder->TrickCounter = decoder->TrickSpeed;
        goto skip_sync;
    }
#if 0
    // at start of new video stream, soft or hard sync video to audio
    if (!VideoSoftStartSync && decoder->StartCounter < VideoSoftStartFrames && video_clock != (int64_t) AV_NOPTS_VALUE
	&& (audio_clock == (int64_t) AV_NOPTS_VALUE || video_clock > audio_clock + VideoAudioDelay + 120 * 90)) {
	Debug(4, "video: initial slow down video, frame %d\n", decoder->StartCounter);
	goto skip_sync;
    }
#endif
    if (decoder->SyncCounter && decoder->SyncCounter--) {
        goto skip_sync;
    }

    if (audio_clock != (int64_t)AV_NOPTS_VALUE && video_clock != (int64_t)AV_NOPTS_VALUE) {
        // both clocks are known
        int diff;

        diff = video_clock - audio_clock - VideoAudioDelay;
        //	  diff = (decoder->LastAVDiff + diff) / 2;
        decoder->LastAVDiff = diff;

#if 0
	if (abs(diff / 90) > 0) {
	    printf("	  Diff %d filled %d	     \n", diff / 90, filled);
	}
#endif
        if (abs(diff) > 5000 * 90) { // more than 5s
            CuvidMessage(2, "video: audio/video difference too big %d\n", diff / 90);
            // decoder->SyncCounter = 1;
            // usleep(10);
            goto skip_sync;

        } else if (diff > 100 * 90) {

            CuvidMessage(4, "video: slow down video, duping frame %d\n", diff / 90);
            ++decoder->FramesDuped;
            if ((speedup && --speedup) || VideoSoftStartSync)
                decoder->SyncCounter = 1;
            else
                decoder->SyncCounter = 0;
            goto out;

        } else if (diff > 25 * 90) {
            CuvidMessage(3, "video: slow down video, duping frame %d \n", diff / 90);
            ++decoder->FramesDuped;
            decoder->SyncCounter = 1;
            goto out;
        } else if ((diff < -100 * 90)) {
            if (filled > 2) {
                CuvidMessage(3, "video: speed up video, droping frame %d\n", diff / 90);
                ++decoder->FramesDropped;
                CuvidAdvanceDecoderFrame(decoder);
            } else if ((diff < -100 * 90)) { // give it some time to get frames to drop
                Debug(3, "Delay Audio %d ms\n", abs(diff / 90));
                AudioDelayms(abs(diff / 90));
            }
            decoder->SyncCounter = 1;
        } else {
            speedup = 2;
        }
#if defined(DEBUG) || defined(AV_INFO)
        if (!decoder->SyncCounter && decoder->StartCounter < 1000) {
#ifdef DEBUG
            Debug(3, "video/cuvid: synced after %d frames %dms\n", decoder->StartCounter, GetMsTicks() - VideoSwitch);
#else
            Info("video/cuvid: synced after %d frames\n", decoder->StartCounter);
#endif
            decoder->StartCounter += 1000;
        }
#endif
    }

skip_sync:
    // check if next field is available
    if (decoder->SurfaceField && filled <= 1 + 2 * decoder->Interlaced) {
        if (filled < 1 + 2 * decoder->Interlaced) {
            ++decoder->FramesDuped;
#if 0
	    // FIXME: don't warn after stream start, don't warn during pause
	    err =
		CuvidMessage(1, _("video: decoder buffer empty, duping frame (%d/%d) %d v-buf\n"),
		decoder->FramesDuped, decoder->FrameCounter, VideoGetBuffers(decoder->Stream));
	    // some time no new picture or black video configured
	    if (decoder->Closing < -300 || (VideoShowBlackPicture && decoder->Closing)) {
		// clear ring buffer to trigger black picture
		atomic_set(&decoder->SurfacesFilled, 0);
	    }
#endif
        }
        // Debug(3,"filled zu klein %d	Field %d Interlaced
        // %d\n",filled,decoder->SurfaceField,decoder->Interlaced); goto out;
    }

    CuvidAdvanceDecoderFrame(decoder);
out:
#if 0
    // defined(DEBUG) || defined(AV_INFO)
    // debug audio/video sync
    if (err || !(decoder->FramesDisplayed % AV_INFO_TIME)) {
	if (!err) {
	    CuvidMessage(0, NULL);
	}
	Info("video: %s%+5" PRId64 " %4" PRId64 " %3d/\\ms %3d%+d%+d v-buf\n", Timestamp2String(video_clock),
	    abs((video_clock - audio_clock) / 90) < 8888 ? ((video_clock - audio_clock) / 90) : 8888,
	    AudioGetDelay() / 90, (int)VideoDeltaPTS / 90, VideoGetBuffers(decoder->Stream),
	    decoder->Interlaced ? 2 * atomic_read(&decoder->SurfacesFilled)
	    - decoder->SurfaceField : atomic_read(&decoder->SurfacesFilled), CuvidOutputSurfaceQueued);
	if (!(decoder->FramesDisplayed % (5 * 60 * 60))) {
	    CuvidPrintFrames(decoder);
	}
    }
#endif
    return; // fix gcc bug!
}

///
/// Sync a video frame.
///
static void CuvidSyncFrame(void) {
    int i;

    //
    //	Sync video decoder to audio
    //
    for (i = 0; i < CuvidDecoderN; ++i) {
        CuvidSyncDecoder(CuvidDecoders[i]);
    }
}

///
/// Sync and display surface.
///
static void CuvidSyncDisplayFrame(void) {

    CuvidDisplayFrame();
    CuvidSyncFrame();
}

///
/// Sync and render a ffmpeg frame
///
/// @param decoder  CUVID hw decoder
/// @param video_ctx	ffmpeg video codec context
/// @param frame    frame to display
///
static void CuvidSyncRenderFrame(CuvidDecoder *decoder, const AVCodecContext *video_ctx, AVFrame *frame) {
#if 0
    // FIXME: temp debug
    if (0 && frame->pkt_pts != (int64_t) AV_NOPTS_VALUE) {
	Debug(3, "video: render frame pts %s\n", Timestamp2String(frame->pkt_pts));
    }
#endif
#ifdef DEBUG
    if (!atomic_read(&decoder->SurfacesFilled)) {
        Debug(4, "video: new stream frame %dms\n", GetMsTicks() - VideoSwitch);
    }
#endif

    // if video output buffer is full, wait and display surface.
    // loop for interlace
    if (atomic_read(&decoder->SurfacesFilled) >= VIDEO_SURFACES_MAX) {
        //Fatal("video/cuvid: this code part shouldn't be used\n");
        return;
    }

    //	if (!decoder->Closing) {
    // VideoSetPts(&decoder->PTS, decoder->Interlaced, video_ctx, frame);
    // }
    CuvidRenderFrame(decoder, video_ctx, frame);
}

///
/// Set CUVID background color.
///
/// @param rgba 32 bit RGBA color.
///
static void CuvidSetBackground(__attribute__((unused)) uint32_t rgba) {}

///
/// Set CUVID video mode.
///
static void CuvidSetVideoMode(void) {
    int i;

    Debug(3, "Set video mode %dx%d\n", VideoWindowWidth, VideoWindowHeight);

    if (EglEnabled) {
#ifdef CUVID
        GlxSetupWindow(VideoWindow, VideoWindowWidth, VideoWindowHeight, glxThreadContext);
#else
        GlxSetupWindow(VideoWindow, VideoWindowWidth, VideoWindowHeight, eglContext);
#endif
    }

    for (i = 0; i < CuvidDecoderN; ++i) {
        // reset video window, upper level needs to fix the positions
        CuvidDecoders[i]->VideoX = 0;
        CuvidDecoders[i]->VideoY = 0;
        CuvidDecoders[i]->VideoWidth = VideoWindowWidth;
        CuvidDecoders[i]->VideoHeight = VideoWindowHeight;
        CuvidUpdateOutput(CuvidDecoders[i]);
    }
}

///
/// Handle a CUVID display.
///
static void CuvidDisplayHandlerThread(void) {
    int i;
    int err = 0;
    int allfull;
    int decoded;
    int filled;
    struct timespec nowtime;
    CuvidDecoder *decoder;

    allfull = 1;
    decoded = 0;

    for (i = 0; i < CuvidDecoderN; ++i) {

        decoder = CuvidDecoders[i];
        //
        // fill frame output ring buffer
        //
        filled = atomic_read(&decoder->SurfacesFilled);
        // if (filled <= 1 +  2 * decoder->Interlaced) {
        if (filled < 5) {
            // FIXME: hot polling
            // fetch+decode or reopen
            allfull = 0;
            err = VideoDecodeInput(decoder->Stream, decoder->TrickSpeed);
        } else {
            err = VideoPollInput(decoder->Stream);
        }
        // decoder can be invalid here
        if (err) {
            // nothing buffered?
            if (err == -1 && decoder->Closing) {
                decoder->Closing--;
                if (!decoder->Closing) {
                    Debug(3, "video/cuvid: closing eof\n");
                    decoder->Closing = -1;
                }
            }
            usleep(10 * 1000);
            continue;
        }
        decoded = 1;
    }

    if (!decoded) { // nothing decoded, sleep
        // FIXME: sleep on wakeup
        usleep(1 * 1000);
    }
    usleep(1000);
    // all decoder buffers are full
    // and display is not preempted
    // speed up filling display queue, wait on display queue empty
    if (!allfull && !decoder->TrickSpeed) {
        clock_gettime(CLOCK_MONOTONIC, &nowtime);
        // time for one frame over?
        if ((nowtime.tv_sec - CuvidFrameTime.tv_sec) * 1000 * 1000 * 1000 +
                (nowtime.tv_nsec - CuvidFrameTime.tv_nsec) <
            15 * 1000 * 1000) {
            return;
        }
    }
    return;
}

///
/// Set video output position.
///
/// @param decoder  CUVID hw decoder
/// @param x	video output x coordinate inside the window
/// @param y	video output y coordinate inside the window
/// @param width    video output width
/// @param height   video output height
///
/// @note FIXME: need to know which stream.
///
static void CuvidSetOutputPosition(CuvidDecoder *decoder, int x, int y, int width, int height) {
    Debug(3, "video/cuvid: output %dx%d%+d%+d\n", width, height, x, y);

    decoder->VideoX = x;
    decoder->VideoY = y;
    decoder->VideoWidth = width;
    decoder->VideoHeight = height;

    // next video pictures are automatic rendered to correct position
}

//----------------------------------------------------------------------------
//  CUVID OSD
//----------------------------------------------------------------------------

///
/// CUVID module.
///
static const VideoModule CuvidModule = {
    .Name = "cuvid",
    .Enabled = 1,
    .NewHwDecoder = (VideoHwDecoder * (*const)(VideoStream *)) CuvidNewHwDecoder,
    .DelHwDecoder = (void (*const)(VideoHwDecoder *))CuvidDelHwDecoder,
    .GetSurface = (unsigned (*const)(VideoHwDecoder *, const AVCodecContext *))CuvidGetVideoSurface,
    .ReleaseSurface = (void (*const)(VideoHwDecoder *, unsigned))CuvidReleaseSurface,
    .get_format =
        (enum AVPixelFormat(*const)(VideoHwDecoder *, AVCodecContext *, const enum AVPixelFormat *))Cuvid_get_format,
    .RenderFrame = (void (*const)(VideoHwDecoder *, const AVCodecContext *, const AVFrame *))CuvidSyncRenderFrame,
    .GetHwAccelContext = (void *(*const)(VideoHwDecoder *))CuvidGetHwAccelContext,
    .SetClock = (void (*const)(VideoHwDecoder *, int64_t))CuvidSetClock,
    .GetClock = (int64_t(*const)(const VideoHwDecoder *))CuvidGetClock,
    .SetClosing = (void (*const)(const VideoHwDecoder *))CuvidSetClosing,
    .ResetStart = (void (*const)(const VideoHwDecoder *))CuvidResetStart,
    .SetTrickSpeed = (void (*const)(const VideoHwDecoder *, int))CuvidSetTrickSpeed,
    .GrabOutput = CuvidGrabOutputSurface,
    .GetStats = (void (*const)(VideoHwDecoder *, int *, int *, int *, int *, float *, int *, int *, int *,
                               int *))CuvidGetStats,
    .SetBackground = CuvidSetBackground,
    .SetVideoMode = CuvidSetVideoMode,

    .DisplayHandlerThread = CuvidDisplayHandlerThread,
    // .OsdClear = GlxOsdClear,
    // .OsdDrawARGB = GlxOsdDrawARGB,
    // .OsdInit = GlxOsdInit,
    // .OsdExit = GlxOsdExit,
    // .OsdClear = CuvidOsdClear,
    // .OsdDrawARGB = CuvidOsdDrawARGB,
    // .OsdInit = CuvidOsdInit,
    // .OsdExit = CuvidOsdExit,
    .Exit = CuvidExit,
    .Init = CuvidGlxInit,
};

#endif

//----------------------------------------------------------------------------
//  NOOP
//----------------------------------------------------------------------------

///
/// Allocate new noop decoder.
///
/// @param stream   video stream
///
/// @returns always NULL.
///
static VideoHwDecoder *NoopNewHwDecoder(__attribute__((unused)) VideoStream *stream) { return NULL; }

///
/// Release a surface.
///
/// Can be called while exit.
///
/// @param decoder  noop hw decoder
/// @param surface  surface no longer used
///
static void NoopReleaseSurface(__attribute__((unused)) VideoHwDecoder *decoder,
                               __attribute__((unused)) unsigned surface) {}

///
/// Set noop background color.
///
/// @param rgba 32 bit RGBA color.
///
static void NoopSetBackground(__attribute__((unused)) uint32_t rgba) {}

///
/// Noop initialize OSD.
///
/// @param width    osd width
/// @param height   osd height
///
static void NoopOsdInit(__attribute__((unused)) int width, __attribute__((unused)) int height) {}

///
/// Draw OSD ARGB image.
///
/// @param xi	x-coordinate in argb image
/// @param yi	y-coordinate in argb image
/// @paran height   height in pixel in argb image
/// @paran width    width in pixel in argb image
/// @param pitch    pitch of argb image
/// @param argb 32bit ARGB image data
/// @param x	x-coordinate on screen of argb image
/// @param y	y-coordinate on screen of argb image
///
/// @note looked by caller
///
static void NoopOsdDrawARGB(__attribute__((unused)) int xi, __attribute__((unused)) int yi,
                            __attribute__((unused)) int width, __attribute__((unused)) int height,
                            __attribute__((unused)) int pitch, __attribute__((unused)) const uint8_t *argb,
                            __attribute__((unused)) int x, __attribute__((unused)) int y) {}

///
/// Noop setup.
///
/// @param display_name x11/xcb display name
///
/// @returns always true.
///
static int NoopInit(const char *display_name) {
    Info("video/noop: noop driver running on display '%s'\n", display_name);
    return 1;
}

#ifdef USE_VIDEO_THREAD

///
/// Handle a noop display.
///
static void NoopDisplayHandlerThread(void) {
    // avoid 100% cpu use
    usleep(20 * 1000);
#if 0
    // this can't be canceled
    if (XlibDisplay) {
	XEvent event;

	XPeekEvent(XlibDisplay, &event);
    }
#endif
}

#else

#define NoopDisplayHandlerThread NULL

#endif

///
/// Noop void function.
///
static void NoopVoid(void) {}

///
/// Noop video module.
///
static const VideoModule NoopModule = {
    .Name = "noop",
    .Enabled = 1,
    .NewHwDecoder = NoopNewHwDecoder,
#if 0
    // can't be called:
    .DelHwDecoder = NoopDelHwDecoder,
    .GetSurface = (unsigned (*const) (VideoHwDecoder *,
	    const AVCodecContext *))NoopGetSurface,
#endif
    .ReleaseSurface = NoopReleaseSurface,
#if 0
    .get_format = (enum AVPixelFormat(*const) (VideoHwDecoder *,
	    AVCodecContext *, const enum AVPixelFormat *))Noop_get_format,
    .RenderFrame = (void (*const) (VideoHwDecoder *,
	    const AVCodecContext *, const AVFrame *))NoopSyncRenderFrame,
    .GetHwAccelContext = (void *(*const)(VideoHwDecoder *))
	DummyGetHwAccelContext,
    .SetClock =(void (*const)(VideoHwDecoder *, int64_t))NoopSetClock,
    .GetClock =(int64_t(*const)(const VideoHwDecoder *))NoopGetClock,
    .SetClosing =(void (*const)(const VideoHwDecoder *))NoopSetClosing,
    .SetTrickSpeed =(void (*const)(const VideoHwDecoder *, int))NoopSetTrickSpeed,
    .GrabOutput = NoopGrabOutputSurface,
    .GetStats =(void (*const)(VideoHwDecoder *, int *, int *, int *,
	    int *, float *, int *, int *, int *, int *))NoopGetStats,
#endif
    .ResetStart = (void (*const)(const VideoHwDecoder *))NoopVoid,
    .SetBackground = NoopSetBackground,
    .SetVideoMode = NoopVoid,

    .DisplayHandlerThread = NoopDisplayHandlerThread,
    .OsdClear = NoopVoid,
    .OsdDrawARGB = NoopOsdDrawARGB,
    .OsdInit = NoopOsdInit,
    .OsdExit = NoopVoid,
    .Init = NoopInit,
    .Exit = NoopVoid,
};

//----------------------------------------------------------------------------
//  OSD
//----------------------------------------------------------------------------

///
/// Clear the OSD.
///
/// @todo I use glTexImage2D to clear the texture, are there faster and
/// better ways to clear a texture?
///
void VideoOsdClear(void) {

#ifdef PLACEBO
    OsdShown = 0;
#else
    VideoThreadLock();
    // VideoUsedModule->OsdClear();
    OsdDirtyX = OsdWidth; // reset dirty area
    OsdDirtyY = OsdHeight;
    OsdDirtyWidth = 0;
    OsdDirtyHeight = 0;
    OsdShown = 0;
    VideoThreadUnlock();
#endif
}

///
/// Draw an OSD ARGB image.
///
/// @param xi	x-coordinate in argb image
/// @param yi	y-coordinate in argb image
/// @paran height   height in pixel in argb image
/// @paran width    width in pixel in argb image
/// @param pitch    pitch of argb image
/// @param argb 32bit ARGB image data
/// @param x	x-coordinate on screen of argb image
/// @param y	y-coordinate on screen of argb image
///
void VideoOsdDrawARGB(int xi, int yi, int width, int height, int pitch, const uint8_t *argb, int x, int y) {
    VideoThreadLock();
    // update dirty area
    if (x < OsdDirtyX) {
        if (OsdDirtyWidth) {
            OsdDirtyWidth += OsdDirtyX - x;
        }
        OsdDirtyX = x;
    }
    if (y < OsdDirtyY) {
        if (OsdDirtyHeight) {
            OsdDirtyHeight += OsdDirtyY - y;
        }
        OsdDirtyY = y;
    }
    if (x + width > OsdDirtyX + OsdDirtyWidth) {
        OsdDirtyWidth = x + width - OsdDirtyX;
    }
    if (y + height > OsdDirtyY + OsdDirtyHeight) {
        OsdDirtyHeight = y + height - OsdDirtyY;
    }
    Debug(3, "video: osd dirty %dx%d%+d%+d -> %dx%d%+d%+d\n", width, height, x, y, OsdDirtyWidth, OsdDirtyHeight,
          OsdDirtyX, OsdDirtyY);
    Debug(4," dummy print %d %d %d %s",xi,yi,pitch,argb);
    VideoThreadUnlock();
}

void ActivateOsd(GLuint texture, int x, int y, int xsize, int ysize) {
    // printf("OSD open %d %d %d %d\n",x,y,xsize,ysize);

    OSDfb = texture;
    //    OSDtexture = texture;
    OSDx = x;
    OSDy = y;
    OSDxsize = xsize;
    OSDysize = ysize;
    OsdShown = 1;
}

///
/// Get OSD size.
///
/// @param[out] width	OSD width
/// @param[out] height	OSD height
///
void VideoGetOsdSize(int *width, int *height) {
    *width = 1920;
    *height = 1080; // unknown default

    if (OsdWidth && OsdHeight) {
        *width = OsdWidth;
        *height = OsdHeight;
    }
}

/// Set OSD Size.
///
/// @param width    OSD width
/// @param height   OSD height
///
void VideoSetOsdSize(int width, int height) {

    if (OsdConfigWidth != width || OsdConfigHeight != height) {
        VideoOsdExit();
        OsdConfigWidth = width;
        OsdConfigHeight = height;
        VideoOsdInit();
    }
}

///
/// Set the 3d OSD mode.
///
/// @param mode OSD mode (0=off, 1=SBS, 2=Top Bottom)
///
void VideoSetOsd3DMode(int mode) { Osd3DMode = mode; }

///
/// Setup osd.
///
/// FIXME: looking for BGRA, but this fourcc isn't supported by the
/// drawing functions yet.
///
void VideoOsdInit(void) {
    if (OsdConfigWidth && OsdConfigHeight) {
        OsdWidth = OsdConfigWidth;
        OsdHeight = OsdConfigHeight;
    } else {
        OsdWidth = VideoWindowWidth;
        OsdHeight = VideoWindowHeight;
    }
    // printf("\nset osd %d x %d\n",OsdWidth,OsdHeight);
    if (posd)
        free(posd);
    posd = (unsigned char *)calloc((OsdWidth + 1) * (OsdHeight + 1) * 4, 1);
    //	posd = (unsigned char *)calloc((4096 + 1) * (2160 + 1) * 4, 1);
    VideoOsdClear();
}

///
/// Cleanup OSD.
///
void VideoOsdExit(void) {
    OsdDirtyWidth = 0;
    OsdDirtyHeight = 0;
    VideoOsdClear();
}

//----------------------------------------------------------------------------
//  Events
//----------------------------------------------------------------------------

/// C callback feed key press
extern void FeedKeyPress(const char *, const char *, int, int, const char *);

///
/// Handle XLib I/O Errors.
///
/// @param display  display with i/o error
///
static int VideoIOErrorHandler(__attribute__((unused)) Display *display) {

    Error(_("video: fatal i/o error\n"));
    // should be called from VideoThread
    if (VideoThread && VideoThread == pthread_self()) {
        Debug(3, "video: called from video thread\n");
        VideoUsedModule = &NoopModule;
        XlibDisplay = NULL;
        VideoWindow = XCB_NONE;
#ifdef USE_VIDEO_THREAD
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_cond_destroy(&VideoWakeupCond);
        pthread_mutex_destroy(&VideoLockMutex);
        pthread_mutex_destroy(&VideoMutex);
        VideoThread = 0;
        pthread_exit("video thread exit");
#endif
    }
    do {
        sleep(1000);
    } while (1); // let other threads running

    return -1;
}

///
/// Handle X11 events.
///
/// @todo   Signal WmDeleteMessage to application.
///
static void VideoEvent(void) {
    XEvent event;
    KeySym keysym;
    const char *keynam;
    char buf[64];
    char letter[64];
    int letter_len;
    uint32_t values[1];

    VideoThreadLock();
    XNextEvent(XlibDisplay, &event);
    VideoThreadUnlock();
    switch (event.type) {
        case ClientMessage:
            Debug(3, "video/event: ClientMessage\n");
            if (event.xclient.data.l[0] == (long)WmDeleteWindowAtom) {
                Debug(3, "video/event: wm-delete-message\n");
                FeedKeyPress("XKeySym", "Close", 0, 0, NULL);
            }
            break;

        case MapNotify:
            Debug(3, "video/event: MapNotify\n");
            // wm workaround
            VideoThreadLock();
            xcb_change_window_attributes(Connection, VideoWindow, XCB_CW_CURSOR, &VideoBlankCursor);
            VideoThreadUnlock();
            VideoBlankTick = 0;
            break;
        case Expose:
            // Debug(3, "video/event: Expose\n");
            break;
        case ReparentNotify:
            Debug(3, "video/event: ReparentNotify\n");
            break;
        case ConfigureNotify:
            // Debug(3, "video/event: ConfigureNotify\n");
            VideoSetVideoMode(event.xconfigure.x, event.xconfigure.y, event.xconfigure.width, event.xconfigure.height);
            break;
        case ButtonPress:
            VideoSetFullscreen(-1);
            break;
        case KeyPress:
            VideoThreadLock();
            letter_len = XLookupString(&event.xkey, letter, sizeof(letter) - 1, &keysym, NULL);
            VideoThreadUnlock();
            if (letter_len < 0) {
                letter_len = 0;
            }
            letter[letter_len] = '\0';
            if (keysym == NoSymbol) {
                Warning(_("video/event: No symbol for %d\n"), event.xkey.keycode);
                break;
            }
            VideoThreadLock();
            keynam = XKeysymToString(keysym);
            VideoThreadUnlock();
            // check for key modifiers (Alt/Ctrl)
            if (event.xkey.state & (Mod1Mask | ControlMask)) {
                if (event.xkey.state & Mod1Mask) {
                    strcpy(buf, "Alt+");
                } else {
                    buf[0] = '\0';
                }
                if (event.xkey.state & ControlMask) {
                    strcat(buf, "Ctrl+");
                }
                strncat(buf, keynam, sizeof(buf) - 10);
                keynam = buf;
            }
            FeedKeyPress("XKeySym", keynam, 0, 0, letter);
            break;
        case KeyRelease:
        case ButtonRelease:
            break;
        case MotionNotify:
            values[0] = XCB_NONE;
            VideoThreadLock();
            xcb_change_window_attributes(Connection, VideoWindow, XCB_CW_CURSOR, values);
            VideoThreadUnlock();
            VideoBlankTick = GetMsTicks();
            break;
        default:
#if 0
	    if (XShmGetEventBase(XlibDisplay) + ShmCompletion == event.type) {
		// printf("ShmCompletion\n");
	    }
#endif
            Debug(3, "Unsupported event type %d\n", event.type);
            break;
    }
}

///
/// Poll all x11 events.
///
void VideoPollEvent(void) {
    // hide cursor, after xx ms
    if (VideoBlankTick && VideoWindow != XCB_NONE && VideoBlankTick + 200 < GetMsTicks()) {
        VideoThreadLock();
        xcb_change_window_attributes(Connection, VideoWindow, XCB_CW_CURSOR, &VideoBlankCursor);
        VideoThreadUnlock();
        VideoBlankTick = 0;
    }
    while (XlibDisplay) {
        VideoThreadLock();
        if (!XPending(XlibDisplay)) {
            VideoThreadUnlock();
            break;
        }
        VideoThreadUnlock();
        VideoEvent();
    }
}

void VideoSetVideoEventCallback(void (*videoEventCallback)(void)) { VideoEventCallback = videoEventCallback; }

//----------------------------------------------------------------------------
//  Thread
//----------------------------------------------------------------------------

#ifdef USE_VIDEO_THREAD

#ifdef PLACEBO

static bool open_file(const char *path, struct file *out) {
    if (!path || !path[0]) {
        *out = (struct file){0};
        return true;
    }

    FILE *fp = NULL;
    bool success = false;

    fp = fopen(path, "rb");
    if (!fp)
        goto done;

    if (fseeko(fp, 0, SEEK_END))
        goto done;
    off_t size = ftello(fp);

    if (size < 0)
        goto done;
    if (fseeko(fp, 0, SEEK_SET))
        goto done;

    void *data = malloc(size);

    if (!fread(data, size, 1, fp))
        goto done;

    *out = (struct file){
        .data = data,
        .size = size,
    };

    success = true;
done:
    if (fp)
        fclose(fp);
    return success;
}

static void close_file(struct file *file) {
    if (!file->data)
        return;

    free(file->data);
    *file = (struct file){0};
}

void pl_log_intern(void *stream, enum pl_log_level level, const char *msg) {
    static const char *prefix[] = {
        [PL_LOG_FATAL] = "fatal", [PL_LOG_ERR] = "error",   [PL_LOG_WARN] = "warn",
        [PL_LOG_INFO] = "info",   [PL_LOG_DEBUG] = "debug", [PL_LOG_TRACE] = "trace",
    };
    (void)stream;
    printf("%5s: %s\n", prefix[level], msg);
}

void InitPlacebo() {

    static const char *lut_file = "lut/lut.cube";

    CuvidMessage(2, "Init Placebo mit API %d\n", PL_API_VER);
#ifdef PLACEBO_GL
    CuvidMessage(2, "Placebo mit opengl\n");
#else
    CuvidMessage(2, "Placebo mit vulkan\n");
#endif
    p = calloc(1, sizeof(struct priv));
    if (!p)
        Fatal(_("Cant get memory for PLACEBO struct"));

    // Create context
    p->context.log_cb = &pl_log_intern;
    p->context.log_level = PL_LOG_WARN; // WARN

    p->ctx = pl_log_create(PL_API_VER, &p->context);
    if (!p->ctx) {
        Fatal(_("Failed initializing libplacebo\n"));
    }

#ifdef PLACEBO_GL
    //  eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
    //  eglSharedContext);
    struct pl_opengl_params params = pl_opengl_default_params;

    params.egl_display = eglDisplay;
    params.egl_context = eglContext;

    p->gl = pl_opengl_create(p->ctx, &params);

    if (!p->gl)
        Fatal(_("Failed to create placebo opengl \n"));

    p->swapchain = pl_opengl_create_swapchain(p->gl, &(struct pl_opengl_swapchain_params){
                                                         .swap_buffers = (void (*)(void *))CuvidSwapBuffer,
                                                         .framebuffer.flipped = true,
                                                         .framebuffer.id = 0,
                                                         .max_swapchain_depth = 3,
                                                         .priv = VideoWindow,
                                                     });

    p->gpu = p->gl->gpu;

#else
    struct pl_vulkan_params params = {0};
    struct pl_vk_inst_params iparams = pl_vk_inst_default_params;

    VkXcbSurfaceCreateInfoKHR xcbinfo;

    char xcbext[] = {"VK_KHR_xcb_surface"};
    char surfext[] = {"VK_KHR_surface"};
    char *ext[2] = {&xcbext, &surfext};

    // create Vulkan instance
    //    memcpy(&iparams, &pl_vk_inst_default_params, sizeof(iparams));
    // iparams.debug = true;

    iparams.num_extensions = 2; // extensions_count;
    iparams.extensions = &ext;
    iparams.debug = false;

    p->vk_inst = pl_vk_inst_create(p->ctx, &iparams);

    // create XCB surface for swapchain
    xcbinfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    xcbinfo.pNext = NULL;
    xcbinfo.flags = 0;
    xcbinfo.connection = Connection;
    xcbinfo.window = VideoWindow;

    if (vkCreateXcbSurfaceKHR(p->vk_inst->instance, &xcbinfo, NULL, &p->pSurface) != VK_SUCCESS) {
        Fatal(_("Failed to create XCB Surface\n"));
    }

    // create Vulkan device
    memcpy(&params, &pl_vulkan_default_params, sizeof(params));
    params.instance = p->vk_inst->instance;
    params.async_transfer = true;
    params.async_compute = true;
    params.queue_count = 16;
    params.surface = p->pSurface;
    params.allow_software = false;

    p->vk = pl_vulkan_create(p->ctx, &params);
    if (!p->vk)
        Fatal(_("Failed to create Vulkan Device"));

    p->gpu = p->vk->gpu;
    // Create initial swapchain
    p->swapchain = pl_vulkan_create_swapchain(p->vk, &(struct pl_vulkan_swapchain_params){
                                                         .surface = p->pSurface,
                                                         .present_mode = VK_PRESENT_MODE_FIFO_KHR,
                                                         .swapchain_depth = SWAP_BUFFER_SIZE,
#if PL_API_VER < 229
                                                         .prefer_hdr = true,
#endif
                                                     });

#endif

    if (!p->swapchain) {
        Fatal(_("Failed creating vulkan swapchain!"));
    }
    
#ifdef VAAPI
    if (!(p->gpu->import_caps.tex & PL_HANDLE_DMA_BUF)) {
        p->has_dma_buf = 0;
        Debug(3, "No support for dma_buf import \n");
    } else {
        p->has_dma_buf = 1;
        Debug(3, "dma_buf support available\n");
    }
#else
    p->has_dma_buf = 0;
    Debug(3, "No support for dma_buf import \n");
#endif

#ifdef PLACEBO_GL
    if (!pl_swapchain_resize(p->swapchain, (int *)&VideoWindowWidth, (int *)&VideoWindowHeight)) {
        Fatal(_("libplacebo: failed initializing swapchain\n"));
    }
#endif
#if PL_API_VER >= 113
    // load LUT File
    struct file lutf;
    char tmp[400];

    sprintf(tmp, "%s/%s", MyConfigDir, lut_file);
    if (open_file(tmp, &lutf) && lutf.size) {
        if (!(p->lut = pl_lut_parse_cube(p->ctx, lutf.data, lutf.size)))
            fprintf(stderr, "Failed parsing LUT.. continuing anyway\n");
        close_file(&lutf);
    } else {
        Debug(3, "Placebo: No LUT File used\n");
    }
#endif
    // create renderer
    p->renderer = pl_renderer_create(p->ctx, p->gpu);
    if (!p->renderer) {
        Fatal(_("Failed initializing libplacebo renderer\n"));
    }

    Debug(3, "Placebo: init ok");
}
#endif

///
/// Video render thread.
///

void delete_decode() { Debug(3, "decoder thread exit\n"); }

static void *VideoDisplayHandlerThread(void *dummy) {

    prctl(PR_SET_NAME, "video decoder", 0, 0, 0);
    sleep(2);
    pthread_cleanup_push(delete_decode, NULL);
    for (;;) {
        // fix dead-lock with CuvidExit
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        VideoUsedModule->DisplayHandlerThread();
    }
    pthread_cleanup_pop(NULL);
    return dummy;
}

void exit_display() {

#ifdef GAMMA
    Exit_Gamma();
#endif

#ifdef PLACEBO
    Debug(3, "delete placebo\n");
    if (p == NULL) {
        Debug(3, "Placebo not initialised\n");
        return;
    }
    pl_gpu_finish(p->gpu);
#if PL_API_VER >= 229
    if (osdoverlay.tex)
        pl_tex_destroy(p->gpu, &osdoverlay.tex);
#else
    if (osdoverlay.plane.texture)
        pl_tex_destroy(p->gpu, &osdoverlay.plane.texture);
#endif

    //	 pl_renderer_destroy(&p->renderer);
    if (p->renderertest) {
        pl_renderer_destroy(&p->renderertest);
        p->renderertest = NULL;
    }

    pl_swapchain_destroy(&p->swapchain);

#ifdef PLACEBO_GL
    pl_opengl_destroy(&p->gl);
#else
    //    pl_vulkan_destroy(&p->vk);
    vkDestroySurfaceKHR(p->vk_inst->instance, p->pSurface, NULL);
    pl_vk_inst_destroy(&p->vk_inst);
#endif

    pl_log_destroy(&p->ctx);
#if PL_API_VER >= 113
    pl_lut_free(&p->lut);
#endif
    free(p);
    p = NULL;
#endif

#ifdef CUVID
    if (glxThreadContext) {
        glXDestroyContext(XlibDisplay, glxThreadContext);
        GlxCheck();
        glxThreadContext = NULL;
    }
#else
    if (eglThreadContext) {
        eglDestroyContext(eglDisplay, eglThreadContext);
        EglCheck();
        eglThreadContext = NULL;
    }
#endif
    Debug(3, "display thread exit\n");
}

static void *VideoHandlerThread(void *dummy) {
#if defined VAAPI && !defined PLACEBO_GL
    EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
#endif

    prctl(PR_SET_NAME, "video display", 0, 0, 0);

#ifdef GAMMA
    Init_Gamma();
    Set_Gamma(0.0, 6500);
#endif

#if (defined CUVID && !defined PLACEBO) || (defined CUVID && defined PLACEBO_GL)
    if (EglEnabled) {
        glxThreadContext = glXCreateContext(XlibDisplay, GlxVisualInfo, glxSharedContext, GL_TRUE);
        GlxSetupWindow(VideoWindow, VideoWindowWidth, VideoWindowHeight, glxThreadContext);
    }
#endif
#if (defined VAAPI && !defined PLACEBO) || (defined VAAPI && defined PLACEBO_GL)
#ifdef PLACEBO_GL
    if (!eglBindAPI(EGL_OPENGL_API)) {
        Fatal(_(" Could not bind API!\n"));
    }
    eglThreadContext = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, eglAttrs);
#else
    eglThreadContext = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, contextAttrs);
#endif
    if (!eglThreadContext) {
        EglCheck();
        Fatal(_("video/egl: can't create thread egl context\n"));
        return NULL;
    }
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglThreadContext);
#endif

#ifdef PLACEBO
    InitPlacebo();
#endif

    pthread_cleanup_push(exit_display, NULL);
    while (1) {

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#ifndef USE_DRM
        VideoPollEvent();
#endif
        // first_time = GetusTicks();
        CuvidSyncDisplayFrame();
        // printf("syncdisplayframe exec %d\n",(GetusTicks()-first_time)/1000);
    }
    pthread_cleanup_pop(NULL);

    return dummy;
}

///
/// Initialize video threads.
///
static void VideoThreadInit(void) {

#ifndef PLACEBO
#ifdef CUVID
    glXMakeCurrent(XlibDisplay, None, NULL);
#else
//    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext);
#endif
#endif

    pthread_mutex_init(&VideoMutex, NULL);
    pthread_mutex_init(&VideoLockMutex, NULL);
    pthread_mutex_init(&OSDMutex, NULL);
    pthread_cond_init(&VideoWakeupCond, NULL);
    pthread_create(&VideoThread, NULL, VideoDisplayHandlerThread, NULL);

    pthread_create(&VideoDisplayThread, NULL, VideoHandlerThread, NULL);
}

///
/// Exit and cleanup video threads.
///
static void VideoThreadExit(void) {
    if (VideoThread) {
        void *retval;

        Debug(3, "video: video thread canceled\n");

        // FIXME: can't cancel locked
        if (pthread_cancel(VideoThread)) {
            Debug(3, "video: can't queue cancel video display thread\n");
        }
        usleep(200000); // 200ms
        if (pthread_join(VideoThread, &retval) || retval != PTHREAD_CANCELED) {
            Debug(3, "video: can't cancel video decoder thread\n");
        }

        if (VideoDisplayThread) {
            if (pthread_cancel(VideoDisplayThread)) {
                Debug(3, "video: can't queue cancel video display thread\n");
            }
            usleep(200000); // 200ms
            if (pthread_join(VideoDisplayThread, &retval) || retval != PTHREAD_CANCELED) {
                Debug(3, "video: can't cancel video display thread\n");
            }
            VideoDisplayThread = 0;
        }

        VideoThread = 0;
        pthread_cond_destroy(&VideoWakeupCond);
        pthread_mutex_destroy(&VideoLockMutex);
        pthread_mutex_destroy(&VideoMutex);
        pthread_mutex_destroy(&OSDMutex);

#ifndef PLACEBO

        if (OSDtexture)
            glDeleteTextures(1, &OSDtexture);

        if (gl_prog_osd) {
            glDeleteProgram(gl_prog_osd);
            gl_prog_osd = 0;
        }
        if (gl_prog) {
            glDeleteProgram(gl_prog);
            gl_prog = 0;
        }

#endif
    }
}

///
/// Video display wakeup.
///
/// New video arrived, wakeup video thread.
///
void VideoDisplayWakeup(void) {
#ifndef USE_DRM
    if (!XlibDisplay) { // not yet started
        return;
    }
#endif

    if (!VideoThread) { // start video thread, if needed
        VideoThreadInit();
    }
}

#endif

//----------------------------------------------------------------------------
//  Video API
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

///
/// Table of all video modules.
///
static const VideoModule *VideoModules[] = {

    &CuvidModule, &NoopModule};

///
/// Video hardware decoder
///
struct _video_hw_decoder_ {
    union {
        CuvidDecoder Cuvid; ///< cuvid decoder structure
    };
};

///
/// Allocate new video hw decoder.
///
/// @param stream   video stream
///
/// @returns a new initialized video hardware decoder.
///
VideoHwDecoder *VideoNewHwDecoder(VideoStream *stream) {
    VideoHwDecoder *hw;

    VideoThreadLock();
    hw = VideoUsedModule->NewHwDecoder(stream);
    VideoThreadUnlock();

    return hw;
}

///
/// Destroy a video hw decoder.
///
/// @param hw_decoder	video hardware decoder
///
void VideoDelHwDecoder(VideoHwDecoder *hw_decoder) {
    if (hw_decoder) {
#ifdef DEBUG
        if (!pthread_equal(pthread_self(), VideoThread)) {
            Debug(3, "video: should only be called from inside the thread\n");
        }
#endif
        // only called from inside the thread
        // VideoThreadLock();
        VideoUsedModule->DelHwDecoder(hw_decoder);
        // VideoThreadUnlock();
    }
}

///
/// Get a free hardware decoder surface.
///
/// @param hw_decoder	video hardware decoder
/// @param video_ctx	ffmpeg video codec context
///
/// @returns the oldest free surface or invalid surface
///
unsigned VideoGetSurface(VideoHwDecoder *hw_decoder, const AVCodecContext *video_ctx) {
    return VideoUsedModule->GetSurface(hw_decoder, video_ctx);
}

///
/// Release a hardware decoder surface.
///
/// @param hw_decoder	video hardware decoder
/// @param surface  surface no longer used
///
void VideoReleaseSurface(VideoHwDecoder *hw_decoder, unsigned surface) {
    // FIXME: must be guarded against calls, after VideoExit
    VideoUsedModule->ReleaseSurface(hw_decoder, surface);
}

///
/// Callback to negotiate the PixelFormat.
///
/// @param hw_decoder	video hardware decoder
/// @param video_ctx	ffmpeg video codec context
/// @param fmt	    is the list of formats which are supported by
///	the codec, it is terminated by -1 as 0 is a
///	valid format, the formats are ordered by
///	quality.
///
enum AVPixelFormat Video_get_format(VideoHwDecoder *hw_decoder, AVCodecContext *video_ctx,
                                    const enum AVPixelFormat *fmt) {
#ifdef DEBUG
    int ms_delay;

    // FIXME: use frame time
    ms_delay = (1000 * video_ctx->time_base.num * video_ctx->ticks_per_frame) / video_ctx->time_base.den;

    Debug(3, "video: ready %s %2dms/frame %dms\n", Timestamp2String(VideoGetClock(hw_decoder)), ms_delay,
          GetMsTicks() - VideoSwitch);
#endif

    return VideoUsedModule->get_format(hw_decoder, video_ctx, fmt);
}

///
/// Display a ffmpeg frame
///
/// @param hw_decoder	video hardware decoder
/// @param video_ctx	ffmpeg video codec context
/// @param frame    frame to display
///
void VideoRenderFrame(VideoHwDecoder *hw_decoder, const AVCodecContext *video_ctx, const AVFrame *frame) {
#if 0
    fprintf(stderr, "video: render frame pts %s closing %d\n", Timestamp2String(frame->pkt_pts),
	hw_decoder->Cuvid.Closing);
#endif
    if (frame->repeat_pict && !VideoIgnoreRepeatPict) {
        Warning(_("video: repeated pict %d found, but not handled\n"), frame->repeat_pict);
    }
    VideoUsedModule->RenderFrame(hw_decoder, video_ctx, frame);
}

///
/// Get hwaccel context for ffmpeg.
///
/// FIXME: new ffmpeg supports cuvid hw context
///
/// @param hw_decoder	video hardware decoder (must be VA-API)
///
void *VideoGetHwAccelContext(VideoHwDecoder *hw_decoder) { return VideoUsedModule->GetHwAccelContext(hw_decoder); }

///
/// Set video clock.
///
/// @param hw_decoder	video hardware decoder
/// @param pts	    audio presentation timestamp
///
void VideoSetClock(VideoHwDecoder *hw_decoder, int64_t pts) {
    Debug(3, "video: set clock %s\n", Timestamp2String(pts));
    if (hw_decoder) {
        VideoUsedModule->SetClock(hw_decoder, pts);
    }
}

///
/// Get video clock.
///
/// @param hw_decoder	video hardware decoder
///
/// @note this isn't monoton, decoding reorders frames, setter keeps it
/// monotonic
///
int64_t VideoGetClock(const VideoHwDecoder *hw_decoder) {
    if (hw_decoder) {
        return VideoUsedModule->GetClock(hw_decoder);
    }
    return AV_NOPTS_VALUE;
}

///
/// Set closing stream flag.
///
/// @param hw_decoder	video hardware decoder
///
void VideoSetClosing(VideoHwDecoder *hw_decoder) {
    Debug(3, "video: set closing\n");
    VideoUsedModule->SetClosing(hw_decoder);
    // clear clock to avoid further sync
    VideoSetClock(hw_decoder, AV_NOPTS_VALUE);
}

///
/// Reset start of frame counter.
///
/// @param hw_decoder	video hardware decoder
///
void VideoResetStart(VideoHwDecoder *hw_decoder) {

    Debug(3, "video: reset start\n");

    VideoUsedModule->ResetStart(hw_decoder);
    // clear clock to trigger new video stream
    VideoSetClock(hw_decoder, AV_NOPTS_VALUE);
}

///
/// Set trick play speed.
///
/// @param hw_decoder	video hardware decoder
/// @param speed    trick speed (0 = normal)
///
void VideoSetTrickSpeed(VideoHwDecoder *hw_decoder, int speed) {
    Debug(3, "video: set trick-speed %d\n", speed);
    VideoUsedModule->SetTrickSpeed(hw_decoder, speed);
}

///
/// Grab full screen image.
///
/// @param size[out]	size of allocated image
/// @param width[in,out]    width of image
/// @param height[in,out]   height of image
///
uint8_t *VideoGrab(int *size, int *width, int *height, int write_header) {
    Debug(3, "video: grab\n");

#ifdef USE_GRAB
    if (VideoUsedModule->GrabOutput) {
        uint8_t *data;
        uint8_t *rgb;
        char buf[64];
        int i;
        int n;
        int scale_width;
        int scale_height;
        int x;
        int y;
        double src_x;
        double src_y;
        double scale_x;
        double scale_y;

        scale_width = *width;
        scale_height = *height;
        n = 0;
        data = VideoUsedModule->GrabOutput(size, width, height, 1);
        if (data == NULL)
            return NULL;

        if (scale_width <= 0) {
            scale_width = *width;
        }
        if (scale_height <= 0) {
            scale_height = *height;
        }
        // hardware didn't scale for us, use simple software scaler
        if (scale_width != *width && scale_height != *height) {
            if (write_header) {
                n = snprintf(buf, sizeof(buf), "P6\n%d\n%d\n255\n", scale_width, scale_height);
            }
            rgb = malloc(scale_width * scale_height * 3 + n);
            if (!rgb) {
                Error(_("video: out of memory\n"));
                free(data);
                return NULL;
            }
            *size = scale_width * scale_height * 3 + n;
            memcpy(rgb, buf, n); // header

            scale_x = (double)*width / scale_width;
            scale_y = (double)*height / scale_height;

            src_y = 0.0;
            for (y = 0; y < scale_height; y++) {
                int o;

                src_x = 0.0;
                o = (int)src_y * *width;

                for (x = 0; x < scale_width; x++) {
                    i = 4 * (o + (int)src_x);

                    rgb[n + (x + y * scale_width) * 3 + 0] = data[i + 2];
                    rgb[n + (x + y * scale_width) * 3 + 1] = data[i + 1];
                    rgb[n + (x + y * scale_width) * 3 + 2] = data[i + 0];

                    src_x += scale_x;
                }

                src_y += scale_y;
            }

            *width = scale_width;
            *height = scale_height;

            // grabed image of correct size convert BGRA -> RGB
        } else {
            if (write_header) {
                n = snprintf(buf, sizeof(buf), "P6\n%d\n%d\n255\n", *width, *height);
            }
            rgb = malloc(*width * *height * 3 + n);
            if (!rgb) {
                Error(_("video: out of memory\n"));
                free(data);
                return NULL;
            }
            memcpy(rgb, buf, n); // header

            for (i = 0; i < *size / 4; ++i) { // convert bgra -> rgb
                rgb[n + i * 3 + 0] = data[i * 4 + 2];
                rgb[n + i * 3 + 1] = data[i * 4 + 1];
                rgb[n + i * 3 + 2] = data[i * 4 + 0];
            }

            *size = *width * *height * 3 + n;
        }
        free(data);

        return rgb;
    } else
#endif
    {
        Warning(_("softhddev: grab unsupported\n"));
    }

    (void)size;
    (void)width;
    (void)height;
    (void)write_header;
    return NULL;
}

///
/// Grab image service.
///
/// @param size[out]	size of allocated image
/// @param width[in,out]    width of image
/// @param height[in,out]   height of image
///
uint8_t *VideoGrabService(int *size, int *width, int *height) {
    // Debug(3, "video: grab service\n");

#ifdef USE_GRAB
    if (VideoUsedModule->GrabOutput) {
        return VideoUsedModule->GrabOutput(size, width, height, 0);
    } else
#endif
    {
        Warning(_("softhddev: grab unsupported\n"));
    }

    (void)size;
    (void)width;
    (void)height;
    return NULL;
}

///
/// Get decoder statistics.
///
/// @param hw_decoder	video hardware decoder
/// @param[out] missed	missed frames
/// @param[out] duped	duped frames
/// @param[out] dropped dropped frames
/// @param[out] count	number of decoded frames
///
void VideoGetStats(VideoHwDecoder *hw_decoder, int *missed, int *duped, int *dropped, int *counter, float *frametime,
                   int *width, int *height, int *color, int *eotf) {
    VideoUsedModule->GetStats(hw_decoder, missed, duped, dropped, counter, frametime, width, height, color, eotf);
}

///
/// Get decoder video stream size.
///
/// @param hw_decoder	video hardware decoder
/// @param[out] width	video stream width
/// @param[out] height	video stream height
/// @param[out] aspect_num  video stream aspect numerator
/// @param[out] aspect_den  video stream aspect denominator
///
void VideoGetVideoSize(VideoHwDecoder *hw_decoder, int *width, int *height, int *aspect_num, int *aspect_den) {
    *width = 1920;
    *height = 1080;
    *aspect_num = 16;
    *aspect_den = 9;
    // FIXME: test to check if working, than make module function

    if (VideoUsedModule == &CuvidModule) {
        *width = hw_decoder->Cuvid.InputWidth;
        *height = hw_decoder->Cuvid.InputHeight;
        av_reduce(aspect_num, aspect_den, hw_decoder->Cuvid.InputWidth * hw_decoder->Cuvid.InputAspect.num,
                  hw_decoder->Cuvid.InputHeight * hw_decoder->Cuvid.InputAspect.den, 1024 * 1024);
    }
}

#ifdef USE_SCREENSAVER

//----------------------------------------------------------------------------
//  DPMS / Screensaver
//----------------------------------------------------------------------------

///
/// Suspend X11 screen saver.
///
/// @param connection	X11 connection to enable/disable screensaver
/// @param suspend  True suspend screensaver,
///	false enable screensaver
///
static void X11SuspendScreenSaver(xcb_connection_t *connection, int suspend) {
    const xcb_query_extension_reply_t *query_extension_reply;

    query_extension_reply = xcb_get_extension_data(connection, &xcb_screensaver_id);
    if (query_extension_reply && query_extension_reply->present) {
        xcb_screensaver_query_version_cookie_t cookie;
        xcb_screensaver_query_version_reply_t *reply;

        Debug(3, "video: screen saver extension present\n");

        cookie = xcb_screensaver_query_version_unchecked(connection, XCB_SCREENSAVER_MAJOR_VERSION,
                                                         XCB_SCREENSAVER_MINOR_VERSION);
        reply = xcb_screensaver_query_version_reply(connection, cookie, NULL);
        if (reply && (reply->server_major_version >= XCB_SCREENSAVER_MAJOR_VERSION) &&
            (reply->server_minor_version >= XCB_SCREENSAVER_MINOR_VERSION)) {
            xcb_screensaver_suspend(connection, suspend);
        }
        free(reply);
    }
}

///
/// DPMS (Display Power Management Signaling) extension available.
///
/// @param connection	X11 connection to check for DPMS
///
static int X11HaveDPMS(xcb_connection_t *connection) {
    static int have_dpms = -1;
    const xcb_query_extension_reply_t *query_extension_reply;

    if (have_dpms != -1) { // already checked
        return have_dpms;
    }

    have_dpms = 0;
    query_extension_reply = xcb_get_extension_data(connection, &xcb_dpms_id);
    if (query_extension_reply && query_extension_reply->present) {
        xcb_dpms_get_version_cookie_t cookie;
        xcb_dpms_get_version_reply_t *reply;
        int major;
        int minor;

        Debug(3, "video: dpms extension present\n");

        cookie = xcb_dpms_get_version_unchecked(connection, XCB_DPMS_MAJOR_VERSION, XCB_DPMS_MINOR_VERSION);
        reply = xcb_dpms_get_version_reply(connection, cookie, NULL);
        // use locals to avoid gcc warning
        major = XCB_DPMS_MAJOR_VERSION;
        minor = XCB_DPMS_MINOR_VERSION;
        if (reply && (reply->server_major_version >= major) && (reply->server_minor_version >= minor)) {
            have_dpms = 1;
        }
        free(reply);
    }
    return have_dpms;
}

///
/// Disable DPMS (Display Power Management Signaling)
///
/// @param connection	X11 connection to disable DPMS
///
static void X11DPMSDisable(xcb_connection_t *connection) {
    if (X11HaveDPMS(connection)) {
        xcb_dpms_info_cookie_t cookie;
        xcb_dpms_info_reply_t *reply;

        cookie = xcb_dpms_info_unchecked(connection);
        reply = xcb_dpms_info_reply(connection, cookie, NULL);
        if (reply) {
            if (reply->state) {
                Debug(3, "video: dpms was enabled\n");
                xcb_dpms_disable(connection); // monitor powersave off
            }
            free(reply);
        }
        DPMSDisabled = 1;
    }
}

///
/// Reenable DPMS (Display Power Management Signaling)
///
/// @param connection	X11 connection to enable DPMS
///
static void X11DPMSReenable(xcb_connection_t *connection) {
    if (DPMSDisabled && X11HaveDPMS(connection)) {
        xcb_dpms_enable(connection); // monitor powersave on
        xcb_dpms_force_level(connection, XCB_DPMS_DPMS_MODE_ON);
        DPMSDisabled = 0;
    }
}

#else

/// dummy function: Suspend X11 screen saver.
#define X11SuspendScreenSaver(connection, suspend)
/// dummy function: Disable X11 DPMS.
#define X11DPMSDisable(connection)
/// dummy function: Reenable X11 DPMS.
#define X11DPMSReenable(connection)

#endif

//----------------------------------------------------------------------------
//  Setup
//----------------------------------------------------------------------------

///
/// Create main window.
///
/// @param parent   parent of new window
/// @param visual   visual of parent
/// @param depth    depth of parent
///
static void VideoCreateWindow(xcb_window_t parent, xcb_visualid_t visual, uint8_t depth) {
    uint32_t values[4];
    xcb_intern_atom_reply_t *reply;
    xcb_pixmap_t pixmap;
    xcb_cursor_t cursor;

    Debug(3, "video: visual %#0x depth %d\n", visual, depth);

    // Color map
    VideoColormap = xcb_generate_id(Connection);
    xcb_create_colormap(Connection, XCB_COLORMAP_ALLOC_NONE, VideoColormap, parent, visual);

    values[0] = 0;
    values[1] = 0;
    values[2] = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    values[3] = VideoColormap;
    VideoWindow = xcb_generate_id(Connection);
    xcb_create_window(Connection, depth, VideoWindow, parent, VideoWindowX, VideoWindowY, VideoWindowWidth,
                      VideoWindowHeight, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, values);
    Debug(3, "Create Window at %d,%d\n", VideoWindowX, VideoWindowY);
    // define only available with xcb-utils-0.3.8
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_icccm_set_wm_name(Connection, VideoWindow, XCB_ATOM_STRING, 8, sizeof("softhdcuvid") - 1, "softhdcuvid");
    xcb_icccm_set_wm_icon_name(Connection, VideoWindow, XCB_ATOM_STRING, 8, sizeof("softhdcuvid") - 1, "softhdcuvid");
#endif
    // define only available with xcb-utils-0.3.6
#ifdef XCB_NUM_WM_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_set_wm_name(Connection, VideoWindow, XCB_ATOM_STRING, sizeof("softhdcuvid") - 1, "softhdcuvid");
    xcb_set_wm_icon_name(Connection, VideoWindow, XCB_ATOM_STRING, sizeof("softhdcuvid") - 1, "softhdcuvid");
#endif

    // FIXME: size hints

    // register interest in the delete window message
    if ((reply = xcb_intern_atom_reply(
             Connection, xcb_intern_atom(Connection, 0, sizeof("WM_DELETE_WINDOW") - 1, "WM_DELETE_WINDOW"), NULL))) {
        WmDeleteWindowAtom = reply->atom;
        free(reply);
        if ((reply = xcb_intern_atom_reply(
                 Connection, xcb_intern_atom(Connection, 0, sizeof("WM_PROTOCOLS") - 1, "WM_PROTOCOLS"), NULL))) {
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
            xcb_icccm_set_wm_protocols(Connection, VideoWindow, reply->atom, 1, &WmDeleteWindowAtom);
#endif
#ifdef XCB_NUM_WM_HINTS_ELEMENTS
            xcb_set_wm_protocols(Connection, reply->atom, VideoWindow, 1, &WmDeleteWindowAtom);
#endif
            free(reply);
        }
    }

    //
    //	prepare fullscreen.
    //
    if ((reply = xcb_intern_atom_reply(
             Connection, xcb_intern_atom(Connection, 0, sizeof("_NET_WM_STATE") - 1, "_NET_WM_STATE"), NULL))) {
        NetWmState = reply->atom;
        free(reply);
    }
    if ((reply = xcb_intern_atom_reply(
             Connection,
             xcb_intern_atom(Connection, 0, sizeof("_NET_WM_STATE_FULLSCREEN") - 1, "_NET_WM_STATE_FULLSCREEN"),
             NULL))) {
        NetWmStateFullscreen = reply->atom;
        free(reply);
    }

    if ((reply = xcb_intern_atom_reply(
             Connection, xcb_intern_atom(Connection, 0, sizeof("_NET_WM_STATE_ABOVE") - 1, "_NET_WM_STATE_ABOVE"),
             NULL))) {
        NetWmStateAbove = reply->atom;
        free(reply);
    }

    xcb_map_window(Connection, VideoWindow);
    xcb_flush(Connection);

    //
    //	hide cursor
    //
    pixmap = xcb_generate_id(Connection);
    xcb_create_pixmap(Connection, 1, pixmap, parent, 1, 1);
    cursor = xcb_generate_id(Connection);
    xcb_create_cursor(Connection, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 1, 1);

    values[0] = cursor;
    xcb_change_window_attributes(Connection, VideoWindow, XCB_CW_CURSOR, values);
    VideoCursorPixmap = pixmap;
    VideoBlankCursor = cursor;
    VideoBlankTick = 0;
}

///
/// Set video device.
///
/// Currently this only choose the driver.
///
void VideoSetDevice(const char *device) { VideoDriverName = device; }

void VideoSetConnector(char *c) { DRMConnector = c; }

void VideoSetRefresh(char *r) { DRMRefresh = atoi(r); }

int VideoSetShader(char *s) {
#if defined PLACEBO && PL_API_VER >= 58
    if (num_shaders == NUM_SHADERS)
        return -1;
    char *p = malloc(strlen(s) + 1);
    memcpy(p, s, strlen(s) + 1);
    shadersp[num_shaders++] = p;
    CuvidMessage(2, "Use Shader %s\n", s);
    return 0;
#else
    printf("Shaders are only support with placebo (%s)\n",s);
    return -1;
#endif
}

///
/// Get video driver name.
///
/// @returns name of current video driver.
///
const char *VideoGetDriverName(void) {
    if (VideoUsedModule) {
        return VideoUsedModule->Name;
    }
    return "";
}

///
/// Set video geometry.
///
/// @param geometry  [=][<width>{xX}<height>][{+-}<xoffset>{+-}<yoffset>]
///
int VideoSetGeometry(const char *geometry) {
    XParseGeometry(geometry, &VideoWindowX, &VideoWindowY, &VideoWindowWidth, &VideoWindowHeight);

    return 0;
}

///
/// Set 60hz display mode.
///
/// Pull up 50 Hz video for 60 Hz display.
///
/// @param onoff    enable / disable the 60 Hz mode.
///
void VideoSet60HzMode(int onoff) { Video60HzMode = onoff; }

///
/// Set soft start audio/video sync.
///
/// @param onoff    enable / disable the soft start sync.
///
void VideoSetSoftStartSync(int onoff) { VideoSoftStartSync = onoff; }

///
/// Set show black picture during channel switch.
///
/// @param onoff    enable / disable black picture.
///
void VideoSetBlackPicture(int onoff) { VideoShowBlackPicture = onoff; }

///
/// Set brightness adjustment.
///
/// @param brightness	between -1000and 100.
///	0 represents no modification
///
void VideoSetBrightness(int brightness) { VideoBrightness = (float)brightness / 100.0f; }

///
/// Set contrast adjustment.
///
/// @param contrast between 0 and 100.
///	1000 represents no modification
///
void VideoSetContrast(int contrast) { VideoContrast = (float)contrast / 100.0f; }

///
/// Set saturation adjustment.
///
/// @param saturation	between 0 and 100.
///	100 represents no modification
///
void VideoSetSaturation(int saturation) { VideoSaturation = (float)saturation / 100.0f; }

///
/// Set Gamma adjustment.
///
/// @param saturation	between 0 and 100.
///	100 represents no modification
///
void VideoSetGamma(int gamma) { VideoGamma = (float)gamma / 100.0f; }

///
/// Set Color Temperature adjustment.
///
/// @param offset   between -3500k and 3500k.
///	100 represents no modification
///
void VideoSetTemperature(int temp) { VideoTemperature = (float)temp / 35.0f; }

///
/// Set TargetColorSpace.
///
/// @param TargetColorSpace
///
void VideoSetTargetColor(int color) { VulkanTargetColorSpace = color; }

///
/// Set hue adjustment.
///
/// @param hue	between -PI*100 and PI*100.
///	0 represents no modification
///
void VideoSetHue(int hue) { VideoHue = (float)hue / 100.0f; }

///
/// Set video output position.
///
/// @param hw_decoder	video hardware decoder
/// @param x	    video output x coordinate OSD relative
/// @param y	    video output y coordinate OSD relative
/// @param width    video output width
/// @param height   video output height
///
void VideoSetOutputPosition(VideoHwDecoder *hw_decoder, int x, int y, int width, int height) {
    if (!OsdWidth || !OsdHeight) {
        return;
    }
    if (!width || !height) {
        // restore full size
        width = VideoWindowWidth;
        height = VideoWindowHeight;
    } else {
        // convert OSD coordinates to window coordinates
        x = (x * VideoWindowWidth) / OsdWidth;
        width = (width * VideoWindowWidth) / OsdWidth;
        y = (y * VideoWindowHeight) / OsdHeight;
        height = (height * VideoWindowHeight) / OsdHeight;
    }

    // FIXME: add function to module class

    if (VideoUsedModule == &CuvidModule) {
        // check values to be able to avoid
        // interfering with the video thread if possible

        if (x == hw_decoder->Cuvid.VideoX && y == hw_decoder->Cuvid.VideoY && width == hw_decoder->Cuvid.VideoWidth &&
            height == hw_decoder->Cuvid.VideoHeight) {
            // not necessary...
            return;
        }
        VideoThreadLock();
        CuvidSetOutputPosition(&hw_decoder->Cuvid, x, y, width, height);
        CuvidUpdateOutput(&hw_decoder->Cuvid);
        VideoThreadUnlock();
    }

    (void)hw_decoder;
}

///
/// Set video window position.
///
/// @param x	window x coordinate
/// @param y	window y coordinate
/// @param width    window width
/// @param height   window height
///
/// @note no need to lock, only called from inside the video thread
///
void VideoSetVideoMode(__attribute__((unused)) int x, __attribute__((unused)) int y, int width, int height) {
    Debug(4, "video: %s %dx%d%+d%+d\n", __FUNCTION__, width, height, x, y);

    if ((unsigned)width == VideoWindowWidth && (unsigned)height == VideoWindowHeight) {
        return; // same size nothing todo
    }

    if (VideoEventCallback) {
        sleep(1);
        VideoEventCallback();
        Debug(3, "call back set video mode %d %d\n", width, height);
    }

    VideoThreadLock();
    VideoWindowWidth = width;
    VideoWindowHeight = height;
#ifdef PLACEBO
    VideoSetOsdSize(width, height);
#ifdef PLACEBO_GL
    if (!pl_swapchain_resize(p->swapchain, &width, &height)) {
        Fatal(_("libplacebo: failed initializing swapchain\n"));
    }
#endif
#endif
    VideoUsedModule->SetVideoMode();
    VideoThreadUnlock();
}

///
/// Set 4:3 video display format.
///
/// @param format   video format (stretch, normal, center cut-out)
///
void VideoSet4to3DisplayFormat(int format) {
    // convert api to internal format
    switch (format) {
        case -1: // rotate settings
            format = (Video4to3ZoomMode + 1) % (VideoCenterCutOut + 1);
            break;
        case 0: // pan&scan (we have no pan&scan)
            format = VideoStretch;
            break;
        case 1: // letter box
            format = VideoNormal;
            break;
        case 2: // center cut-out
            format = VideoCenterCutOut;
            break;
    }

    if ((unsigned)format == Video4to3ZoomMode) {
        return; // no change, no need to lock
    }

    VideoOsdExit();
    // FIXME: must tell VDR that the OsdSize has been changed!

    VideoThreadLock();
    Video4to3ZoomMode = format;
    // FIXME: need only VideoUsedModule->UpdateOutput();
    VideoUsedModule->SetVideoMode();
    VideoThreadUnlock();

    VideoOsdInit();
}

///
/// Set other video display format.
///
/// @param format   video format (stretch, normal, center cut-out)
///
void VideoSetOtherDisplayFormat(int format) {
    // convert api to internal format
    switch (format) {
        case -1: // rotate settings
            format = (VideoOtherZoomMode + 1) % (VideoCenterCutOut + 1);
            break;
        case 0: // pan&scan (we have no pan&scan)
            format = VideoStretch;
            break;
        case 1: // letter box
            format = VideoNormal;
            break;
        case 2: // center cut-out
            format = VideoCenterCutOut;
            break;
    }

    if ((unsigned)format == VideoOtherZoomMode) {
        return; // no change, no need to lock
    }

    VideoOsdExit();
    // FIXME: must tell VDR that the OsdSize has been changed!

    VideoThreadLock();
    VideoOtherZoomMode = format;
    // FIXME: need only VideoUsedModule->UpdateOutput();
    VideoUsedModule->SetVideoMode();
    VideoThreadUnlock();

    VideoOsdInit();
}

///
/// Send fullscreen message to window.
///
/// @param onoff    -1 toggle, true turn on, false turn off
///
void VideoSetFullscreen(int onoff) {
    if (XlibDisplay) { // needs running connection
        xcb_client_message_event_t event;

        memset(&event, 0, sizeof(event));
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = VideoWindow;
        event.type = NetWmState;
        if (onoff < 0) {
            event.data.data32[0] = XCB_EWMH_WM_STATE_TOGGLE;
        } else if (onoff) {
            event.data.data32[0] = XCB_EWMH_WM_STATE_ADD;
        } else {
            event.data.data32[0] = XCB_EWMH_WM_STATE_REMOVE;
        }
        event.data.data32[1] = NetWmStateFullscreen;
        event.data.data32[2] = NetWmStateAbove;

        xcb_send_event(Connection, XCB_SEND_EVENT_DEST_POINTER_WINDOW, DefaultRootWindow(XlibDisplay),
                       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (void *)&event);
        Debug(3, "video/x11: send fullscreen message %x %x\n", event.data.data32[0], event.data.data32[1]);
    }
}

void VideoSetAbove() {
    if (XlibDisplay) { // needs running connection
        xcb_client_message_event_t event;

        memset(&event, 0, sizeof(event));
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = VideoWindow;
        event.type = NetWmState;
        event.data.data32[0] = XCB_EWMH_WM_STATE_ADD;
        event.data.data32[1] = NetWmStateAbove;

        xcb_send_event(Connection, XCB_SEND_EVENT_DEST_POINTER_WINDOW, DefaultRootWindow(XlibDisplay),
                       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (void *)&event);
        Debug(3, "video/x11: send fullscreen message %x %x\n", event.data.data32[0], event.data.data32[1]);
    }
}

///
/// Set deinterlace mode.
///
void VideoSetDeinterlace(int mode[]) {
    
#ifdef CUVID
    VideoDeinterlace[0] = mode[0]; // 576i
    VideoDeinterlace[1] = 0;       // mode[1];  // 720p
    VideoDeinterlace[2] = mode[2]; // fake 1080
    VideoDeinterlace[3] = mode[3]; // 1080
    VideoDeinterlace[4] = 0;       // mode[4];  2160p
#else
    (void)mode;
    VideoDeinterlace[0] = 1; // 576i
    VideoDeinterlace[1] = 0; // mode[1];  // 720p
    VideoDeinterlace[2] = 1; // fake 1080
    VideoDeinterlace[3] = 1; // 1080
    VideoDeinterlace[4] = 0; // mode[4];  2160p
#endif
    VideoSurfaceModesChanged = 1;
}

///
/// Set skip chroma deinterlace on/off.
///
void VideoSetSkipChromaDeinterlace(int onoff[]) {
    VideoSkipChromaDeinterlace[0] = onoff[0];
    VideoSkipChromaDeinterlace[1] = onoff[1];
    VideoSkipChromaDeinterlace[2] = onoff[2];
    VideoSkipChromaDeinterlace[3] = onoff[3];
    VideoSkipChromaDeinterlace[4] = onoff[4];
    VideoSurfaceModesChanged = 1;
}

///
/// Set inverse telecine on/off.
///
void VideoSetInverseTelecine(int onoff[]) {
    VideoInverseTelecine[0] = onoff[0];
    VideoInverseTelecine[1] = onoff[1];
    VideoInverseTelecine[2] = onoff[2];
    VideoInverseTelecine[3] = onoff[3];
    VideoInverseTelecine[4] = onoff[4];
    VideoSurfaceModesChanged = 1;
}

///
/// Set denoise level (0 .. 1000).
///
void VideoSetDenoise(int level[]) {
    VideoDenoise[0] = level[0];
    VideoDenoise[1] = level[1];
    VideoDenoise[2] = level[2];
    VideoDenoise[3] = level[3];
    VideoDenoise[4] = level[4];
    VideoSurfaceModesChanged = 1;
}

///
/// Set sharpness level (-1000 .. 1000).
///
void VideoSetSharpen(int level[]) {
    VideoSharpen[0] = level[0];
    VideoSharpen[1] = level[1];
    VideoSharpen[2] = level[2];
    VideoSharpen[3] = level[3];
    VideoSharpen[4] = level[4];
    VideoSurfaceModesChanged = 1;
}

///
/// Set scaling mode.
///
/// @param mode table with VideoResolutionMax values
///
void VideoSetScaling(int mode[]) {
    VideoScaling[0] = mode[0];
    VideoScaling[1] = mode[1];
    VideoScaling[2] = mode[2];
    VideoScaling[3] = mode[3];
    VideoScaling[4] = mode[4];
    VideoSurfaceModesChanged = 1;
}

///
/// Set cut top and bottom.
///
/// @param pixels table with VideoResolutionMax values
///
void VideoSetCutTopBottom(int pixels[]) {
    VideoCutTopBottom[0] = pixels[0];
    VideoCutTopBottom[1] = pixels[1];
    VideoCutTopBottom[2] = pixels[2];
    VideoCutTopBottom[3] = pixels[3];
    VideoCutTopBottom[4] = pixels[4];
    // FIXME: update output
}

///
/// Set cut left and right.
///
/// @param pixels   table with VideoResolutionMax values
///
void VideoSetCutLeftRight(int pixels[]) {
    VideoCutLeftRight[0] = pixels[0];
    VideoCutLeftRight[1] = pixels[1];
    VideoCutLeftRight[2] = pixels[2];
    VideoCutLeftRight[3] = pixels[3];
    VideoCutLeftRight[4] = pixels[4];
    // FIXME: update output
}

///
/// Set studio levels.
///
/// @param onoff    flag on/off
///
void VideoSetStudioLevels(int onoff) {
    VideoStudioLevels = onoff;
#ifdef GAMMA
    Set_Gamma(2.4, 6500);
#endif
}

///
/// Set scaler test.
///
/// @param onoff    flag on/off
///
void VideoSetScalerTest(int onoff) {
    VideoScalerTest = onoff;
    VideoSurfaceModesChanged = 1;
}

void ToggleLUT() { LUTon ^= -1; }

///
/// Set Color Blindness.
///
void VideoSetColorBlindness(int value) {
    VideoColorBlindness = value;
    ;
}

///
/// Set Color Blindness Faktor.
///
void VideoSetColorBlindnessFaktor(int value) { VideoColorBlindnessFaktor = (float)value / 100.0f + 1.0f; }

///
/// Set background color.
///
/// @param rgba 32 bit RGBA color.
///
void VideoSetBackground(uint32_t rgba) {
    VideoBackground = rgba; // saved for later start
    VideoUsedModule->SetBackground(rgba);
}

///
/// Set audio delay.
///
/// @param ms	delay in ms
///
void VideoSetAudioDelay(int ms) { VideoAudioDelay = ms * 90; }

///
/// Set EnableDPMSatBlackScreen
///
/// Currently this only choose the driver.
///
void SetDPMSatBlackScreen(int enable) {
#ifdef USE_SCREENSAVER
    EnableDPMSatBlackScreen = enable;
#endif
}

///
/// Raise video window.
///
int VideoRaiseWindow(void) {
    static const uint32_t values[] = {XCB_STACK_MODE_ABOVE};

    xcb_configure_window(Connection, VideoWindow, XCB_CONFIG_WINDOW_STACK_MODE, values);

    return 1;
}

///
/// Initialize video output module.
///
/// @param display_name X11 display name
///
void VideoInit(const char *display_name) {
    int screen_nr;
    int i;
    xcb_screen_iterator_t screen_iter;
    xcb_screen_t const *screen;

#ifdef VAAPI
    VideoDeinterlace[0] = 1; // 576i
    VideoDeinterlace[1] = 0; // mode[1];  // 720p
    VideoDeinterlace[2] = 1; // fake 1080
    VideoDeinterlace[3] = 1; // 1080
    VideoDeinterlace[4] = 0; // mode[4];  2160p
#endif

#ifdef USE_DRM
    VideoInitDrm();
#else

    if (XlibDisplay) { // allow multiple calls
        Debug(3, "video: x11 already setup\n");
        return;
    }
#ifdef USE_GLX
    if (!XInitThreads()) {
        Error(_("video: Can't initialize X11 thread support on '%s'\n"), display_name);
    }
#endif
    // Open the connection to the X server.
    // use the DISPLAY environment variable as the default display name
    if (!display_name && !(display_name = getenv("DISPLAY"))) {
        // if no environment variable, use :0.0 as default display name
        display_name = ":0.0";
    }
    if (!getenv("DISPLAY")) {
        // force set DISPLAY environment variable, otherwise nvidia driver
        // has problems at libplace-swapchain-init
        Debug(3, "video: setting ENV DISPLAY=%s\n", display_name);
        setenv("DISPLAY", display_name, 0);
        // Debug(3, "video: ENV:(%s)\n",getenv("DISPLAY"));
    }

    if (!(XlibDisplay = XOpenDisplay(display_name))) {
        Error(_("video: Can't connect to X11 server on '%s'\n"), display_name);
        // FIXME: we need to retry connection
        return;
    }

    // Register error handler
    XSetIOErrorHandler(VideoIOErrorHandler);

    // Convert XLIB display to XCB connection
    if (!(Connection = XGetXCBConnection(XlibDisplay))) {
        Error(_("video: Can't convert XLIB display to XCB connection\n"));
        VideoExit();
        return;
    }
    // prefetch extensions
    // xcb_prefetch_extension_data(Connection, &xcb_big_requests_id);
#ifdef xcb_USE_GLX
    xcb_prefetch_extension_data(Connection, &xcb_glx_id);
#endif
    // xcb_prefetch_extension_data(Connection, &xcb_randr_id);
#ifdef USE_SCREENSAVER
    xcb_prefetch_extension_data(Connection, &xcb_screensaver_id);
    xcb_prefetch_extension_data(Connection, &xcb_dpms_id);
#endif
    // xcb_prefetch_extension_data(Connection, &xcb_shm_id);
    // xcb_prefetch_extension_data(Connection, &xcb_xv_id);

    // Get the requested screen number
    screen_nr = DefaultScreen(XlibDisplay);
    screen_iter = xcb_setup_roots_iterator(xcb_get_setup(Connection));
    for (i = 0; i < screen_nr; ++i) {
        xcb_screen_next(&screen_iter);
    }
    screen = screen_iter.data;
    VideoScreen = screen;

    //
    //	Default window size
    //
    if (!VideoWindowHeight) {
        if (VideoWindowWidth) {
            VideoWindowHeight = (VideoWindowWidth * 9) / 16;
        } else { // default to fullscreen
            VideoWindowHeight = screen->height_in_pixels;
            VideoWindowWidth = screen->width_in_pixels;
            //***********************************************************************************************
#if DEBUG_no
            if (strcmp(":0.0", display_name) == 0) {
                VideoWindowHeight = 1080;
                VideoWindowWidth = 1920;
            }
#endif
        }
    }
    if (!VideoWindowWidth) {
        VideoWindowWidth = (VideoWindowHeight * 16) / 9;
    }

    //
    // Create output window
    //

    VideoCreateWindow(screen->root, screen->root_visual, screen->root_depth);

    Debug(3, "video: window prepared\n");
#endif
    //
    //	prepare hardware decoder
    //
    for (i = 0; i < (int)(sizeof(VideoModules) / sizeof(*VideoModules)); ++i) {
        // FIXME: support list of drivers and include display name
        // use user device or first working enabled device driver
        if ((VideoDriverName && !strcasecmp(VideoDriverName, VideoModules[i]->Name)) ||
            (!VideoDriverName && VideoModules[i]->Enabled)) {
            if (VideoModules[i]->Init(display_name)) {
                VideoUsedModule = VideoModules[i];
                goto found;
            }
        }
    }
    Error(_("video: '%s' output module isn't supported\n"), VideoDriverName);
    VideoUsedModule = &NoopModule;

found:;
#ifndef USE_DRM
    // FIXME: make it configurable from gui
    if (getenv("NO_MPEG_HW")) {
        VideoHardwareDecoder = 1;
    }
    if (getenv("NO_HW")) {
        VideoHardwareDecoder = 0;
    }
    // disable x11 screensaver
    X11SuspendScreenSaver(Connection, 1);
    X11DPMSDisable(Connection);

    // xcb_prefetch_maximum_request_length(Connection);
    xcb_flush(Connection);
#endif
#ifdef PLACEBO_NOT
    InitPlacebo();
#endif
}

///
/// Cleanup video output module.
///
void VideoExit(void) {
    Debug(3, "Video Exit\n");
#ifndef USE_DRM
    if (!XlibDisplay) { // no init or failed
        return;
    }

    //
    //	Reenable screensaver / DPMS.
    //
    X11DPMSReenable(Connection);
    X11SuspendScreenSaver(Connection, 0);
#endif
    VideoUsedModule->Exit();
    VideoUsedModule = &NoopModule;

#ifdef USE_VIDEO_THREAD
    VideoThreadExit(); // destroy all mutexes
#endif

#ifdef USE_GLX
    if (EglEnabled) {
        EglExit(); // delete all contexts
    }
#endif
#ifndef USE_DRM
    //
    //	FIXME: cleanup.
    //
    // RandrExit();

    //
    //	X11/xcb cleanup
    //
    if (VideoWindow != XCB_NONE) {
        xcb_destroy_window(Connection, VideoWindow);
        VideoWindow = XCB_NONE;
    }
    if (VideoColormap != XCB_NONE) {
        xcb_free_colormap(Connection, VideoColormap);
        VideoColormap = XCB_NONE;
    }
    if (VideoBlankCursor != XCB_NONE) {
        xcb_free_cursor(Connection, VideoBlankCursor);
        VideoBlankCursor = XCB_NONE;
    }
    if (VideoCursorPixmap != XCB_NONE) {
        xcb_free_pixmap(Connection, VideoCursorPixmap);
        VideoCursorPixmap = XCB_NONE;
    }
    xcb_flush(Connection);
    if (XlibDisplay) {
        if (XCloseDisplay(XlibDisplay)) {
            Error(_("video: error closing display\n"));
        }
        XlibDisplay = NULL;
        Connection = 0;
    }
#endif
}

#ifdef USE_DRM
int GlxInitopengl() {
    EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    while (!eglSharedContext)
        sleep(1);

    if (!eglOSDContext) {
        eglOSDContext = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, contextAttrs);
        if (!eglOSDContext) {
            EglCheck();
            Fatal(_("video/egl: can't create thread egl context\n"));
            return 1;
        }
    }
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglOSDContext);
    return 0;
}

int GlxDrawopengl() {
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglSharedContext);
    return 0;
}

void GlxDestroy() {
    eglDestroyContext(eglDisplay, eglOSDContext);
    eglOSDContext = NULL;
}

#endif

#if 0 // for debug only
#include <sys/stat.h>
extern uint8_t *CreateJpeg(uint8_t *, int *, int, int, int);

void makejpg(uint8_t * data, int width, int height)
{
    static int count = 0;
    int i, n = 0, gpu = 0;;
    char buf[32], FileName[32];
    uint8_t *rgb;
    uint8_t *jpg_image;
    int size, size1;

    if (data == NULL) {
	data = malloc(width * height * 4);
	gpu = 1;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, data);
    }

    //	n = snprintf(buf, sizeof(buf), "P6\n%d\n%d\n255\n", width, height);
    sprintf(FileName, "/tmp/test%d.jpg", count++);

    rgb = malloc(width * height * 3 + n);
    if (!rgb) {
	printf("Unable to get RGB Memory \n");
	return;
    }
//  memcpy(rgb, buf, n);	// header
    size = width * height * 4;

    for (i = 0; i < size / 4; ++i) {	// convert bgra -> rgb
	rgb[n + i * 3 + 0] = data[i * 4 + 2];
	rgb[n + i * 3 + 1] = data[i * 4 + 1];
	rgb[n + i * 3 + 2] = data[i * 4 + 0];
    }

    if (gpu)
	free(data);

    jpg_image = CreateJpeg(rgb, &size1, 90, width, height);
    int fd = open(FileName, O_WRONLY | O_CREAT | O_NOFOLLOW | O_TRUNC, DEFFILEMODE);

    write(fd, jpg_image, size1);
    close(fd);
    free(rgb);
}
#endif
