#include <unistd.h>
#include <gbm.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define  DRM_DEBUG

//----------------------------------------------------------------------------
//  DRM
//----------------------------------------------------------------------------

struct _Drm_Render_
{
    int fd_drm;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;
//    drmEventContext ev;
    int bpp;
    uint32_t connector_id, crtc_id, video_plane;
    uint32_t hdr_metadata;
    uint32_t mmWidth,mmHeight;   // Size in mm
	uint32_t hdr_blob_id;

};
typedef struct _Drm_Render_ VideoRender;

struct {
    struct gbm_device *dev;
    struct gbm_surface *surface;
} gbm;

VideoRender *render;

//----------------------------------------------------------------------------
//  Helper functions
//----------------------------------------------------------------------------
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
struct type_name {
    unsigned int type;
    const char *name;
};

static const char *util_lookup_type_name(unsigned int type,
                     const struct type_name *table,
                     unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; i++)
        if (table[i].type == type)
            return table[i].name;

    return NULL;
}

static const struct type_name connector_type_names[] = {
    { DRM_MODE_CONNECTOR_Unknown, "unknown" },
    { DRM_MODE_CONNECTOR_VGA, "VGA" },
    { DRM_MODE_CONNECTOR_DVII, "DVI-I" },
    { DRM_MODE_CONNECTOR_DVID, "DVI-D" },
    { DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
    { DRM_MODE_CONNECTOR_Composite, "composite" },
    { DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
    { DRM_MODE_CONNECTOR_LVDS, "LVDS" },
    { DRM_MODE_CONNECTOR_Component, "component" },
    { DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
    { DRM_MODE_CONNECTOR_DisplayPort, "DP" },
    { DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
    { DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
    { DRM_MODE_CONNECTOR_TV, "TV" },
    { DRM_MODE_CONNECTOR_eDP, "eDP" },
    { DRM_MODE_CONNECTOR_VIRTUAL, "Virtual" },
    { DRM_MODE_CONNECTOR_DSI, "DSI" },
    { DRM_MODE_CONNECTOR_DPI, "DPI" },
};

const char *util_lookup_connector_type_name(unsigned int type)
{
    return util_lookup_type_name(type, connector_type_names,
                     ARRAY_SIZE(connector_type_names));
}

static uint64_t GetPropertyValue(int fd_drm, uint32_t objectID,
                        uint32_t objectType, const char *propName)
{
    uint32_t i;
    int found = 0;
    uint64_t value = 0;
    drmModePropertyPtr Prop;
    drmModeObjectPropertiesPtr objectProps =
        drmModeObjectGetProperties(fd_drm, objectID, objectType);

    for (i = 0; i < objectProps->count_props; i++) {
        if ((Prop = drmModeGetProperty(fd_drm, objectProps->props[i])) == NULL)
            fprintf(stderr, "GetPropertyValue: Unable to query property.\n");

        if (strcmp(propName, Prop->name) == 0) {
            value = objectProps->prop_values[i];
            found = 1;
        }

        drmModeFreeProperty(Prop);

        if (found)
            break;
    }

    drmModeFreeObjectProperties(objectProps);

#ifdef DRM_DEBUG
    if (!found)
        fprintf(stderr, "GetPropertyValue: Unable to find value for property \'%s\'.\n",
            propName);
#endif
    return value;
}
static uint32_t GetPropertyID(int fd_drm, uint32_t objectID,
                        uint32_t objectType, const char *propName)
{
    uint32_t i;
    int found = 0;
    uint32_t value = -1;
    drmModePropertyPtr Prop;
    drmModeObjectPropertiesPtr objectProps =
        drmModeObjectGetProperties(fd_drm, objectID, objectType);

    for (i = 0; i < objectProps->count_props; i++) {
        if ((Prop = drmModeGetProperty(fd_drm, objectProps->props[i])) == NULL)
            fprintf(stderr, "GetPropertyValue: Unable to query property.\n");

        if (strcmp(propName, Prop->name) == 0) {
            value = objectProps->props[i];
            found = 1;
        }
        drmModeFreeProperty(Prop);
        if (found)
            break;
    }
    drmModeFreeObjectProperties(objectProps);

#ifdef DRM_DEBUG
    if (!found)
        Debug(3,"GetPropertyValue: Unable to find ID for property \'%s\'.\n",propName);
#endif
    return value;
}

static int SetPropertyRequest(drmModeAtomicReqPtr ModeReq, int fd_drm,
                    uint32_t objectID, uint32_t objectType,
                    const char *propName, uint64_t value)
{
    uint32_t i;
    uint64_t id = 0;
    drmModePropertyPtr Prop;
    drmModeObjectPropertiesPtr objectProps =
        drmModeObjectGetProperties(fd_drm, objectID, objectType);

    for (i = 0; i < objectProps->count_props; i++) {
        if ((Prop = drmModeGetProperty(fd_drm, objectProps->props[i])) == NULL)
            printf( "SetPropertyRequest: Unable to query property.\n");

        if (strcmp(propName, Prop->name) == 0) {
            id = Prop->prop_id;
            drmModeFreeProperty(Prop);
            break;
        }

        drmModeFreeProperty(Prop);
    }

    drmModeFreeObjectProperties(objectProps);

    if (id == 0)
        printf( "SetPropertyRequest: Unable to find value for property \'%s\'.\n",
            propName);

    return drmModeAtomicAddProperty(ModeReq, objectID, id, value);
}

static void CuvidSetVideoMode(void);
void set_video_mode(int width, int height)
{
	drmModeConnector *connector;
	drmModeModeInfo *mode;
	int ii;
	if (height != 1080 && height != 2160)
		return;
	connector = drmModeGetConnector(render->fd_drm, render->connector_id);
	for (ii = 0; ii < connector->count_modes; ii++) {
		mode = &connector->modes[ii];
		printf("Mode %d %dx%d Rate %d\n",ii,mode->hdisplay,mode->vdisplay,mode->vrefresh);
		if (width == mode->hdisplay &&
				height == mode->vdisplay &&
				mode->vrefresh == DRMRefresh &&
				render->mode.hdisplay != width &&
		        render->mode.vdisplay != height &&
				!(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
			memcpy(&render->mode, mode, sizeof(drmModeModeInfo));
			VideoWindowWidth = mode->hdisplay;
            VideoWindowHeight = mode->vdisplay;
			eglDestroySurface (eglDisplay, eglSurface);
			EglCheck();
    		gbm_surface_destroy (gbm.surface);
			InitBo(render->bpp);
			CuvidSetVideoMode();
			Debug(3,"Set new mode %d:%d\n",mode->hdisplay,mode->vdisplay);
			break;
		}
	}
}

static int FindDevice(VideoRender * render)
{
    drmVersion *version;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder = 0;
    drmModeModeInfo *mode;
    drmModePlane *plane;
    drmModePlaneRes *plane_res;
    drmModeObjectPropertiesPtr props;
    uint32_t j, k;
    uint64_t has_dumb;
    uint64_t has_prime;
    int i,ii=0;
    char connectorstr[10];
    int found = 0;
#ifdef RASPI
    render->fd_drm = open("/dev/dri/card1", O_RDWR);
#else
	render->fd_drm = open("/dev/dri/card0", O_RDWR);
#endif
    if (render->fd_drm < 0) {
        fprintf(stderr, "FindDevice: cannot open /dev/dri/card0: %m\n");
        return -errno;
    }
	drmSetMaster(render->fd_drm);

    version = drmGetVersion(render->fd_drm);
    fprintf(stderr, "FindDevice: open /dev/dri/card0:  %s\n",  version->name);

    // check capability
    if (drmGetCap(render->fd_drm, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || has_dumb == 0)
        fprintf(stderr, "FindDevice: drmGetCap DRM_CAP_DUMB_BUFFER failed or doesn't have dumb buffer\n");

    if (drmSetClientCap(render->fd_drm, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
        fprintf(stderr, "FindDevice: DRM_CLIENT_CAP_UNIVERSAL_PLANES not available.\n");

    if (drmSetClientCap(render->fd_drm, DRM_CLIENT_CAP_ATOMIC, 1) != 0)
        fprintf(stderr, "FindDevice: DRM_CLIENT_CAP_ATOMIC not available.\n");

    if (drmGetCap(render->fd_drm, DRM_CAP_PRIME, &has_prime) < 0)
        fprintf(stderr, "FindDevice: DRM_CAP_PRIME not available.\n");

    if (drmGetCap(render->fd_drm, DRM_PRIME_CAP_EXPORT, &has_prime) < 0)
        fprintf(stderr, "FindDevice: DRM_PRIME_CAP_EXPORT not available.\n");

    if (drmGetCap(render->fd_drm, DRM_PRIME_CAP_IMPORT, &has_prime) < 0)
        fprintf(stderr, "FindDevice: DRM_PRIME_CAP_IMPORT not available.\n");

    if ((resources = drmModeGetResources(render->fd_drm)) == NULL){
        fprintf(stderr, "FindDevice: cannot retrieve DRM resources (%d): %m\n", errno);
        return -errno;
    }

#ifdef DEBUG
    Debug(3,"[FindDevice] DRM have %i connectors, %i crtcs, %i encoders\n",
        resources->count_connectors, resources->count_crtcs,
        resources->count_encoders);
#endif

    // find all available connectors
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(render->fd_drm, resources->connectors[i]);
        if (!connector) {
            fprintf(stderr, "FindDevice: cannot retrieve DRM connector (%d): %m\n", errno);
            return -errno;
        }

        sprintf(connectorstr,"%s-%u",util_lookup_connector_type_name(connector->connector_type),connector->connector_type_id);
        printf("Connector >%s< is %sconnected\n",connectorstr,connector->connection == DRM_MODE_CONNECTED?"":"not ");
        if (DRMConnector && strcmp(DRMConnector,connectorstr))
            continue;

        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
			float aspect = (float)connector->mmWidth / (float)connector->mmHeight;
			if ((aspect > 1.70) && (aspect < 1.85)) {
				render->mmHeight     = 90;
            	render->mmWidth      = 160;
			} else {
            	render->mmHeight     = connector->mmHeight;
            	render->mmWidth      = connector->mmWidth;
			}
			render->connector_id = connector->connector_id;
            // FIXME: use default encoder/crtc pair
            if ((encoder = drmModeGetEncoder(render->fd_drm, connector->encoder_id)) == NULL){
                fprintf(stderr, "FindDevice: cannot retrieve encoder (%d): %m\n", errno);
                return -errno;
            }
            render->crtc_id = encoder->crtc_id;

            render->hdr_metadata = GetPropertyID(render->fd_drm, connector->connector_id,
                            DRM_MODE_OBJECT_CONNECTOR, "HDR_OUTPUT_METADATA");
            printf("ID %d of METADATA in Connector %d connected %d\n",render->hdr_metadata,connector->connector_id,connector->connection);

            memcpy(&render->mode, &connector->modes[0], sizeof(drmModeModeInfo));  // set fallback
            // search Modes for Connector
            for (ii = 0; ii < connector->count_modes; ii++) {
                mode = &connector->modes[ii];

                printf("Mode %d %dx%d Rate %d\n",ii,mode->hdisplay,mode->vdisplay,mode->vrefresh);

                if (VideoWindowWidth && VideoWindowHeight) { // preset by command line
                    if (VideoWindowWidth == mode->hdisplay &&
                            VideoWindowHeight == mode->vdisplay &&
                            mode->vrefresh == DRMRefresh &&
                            !(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
                        memcpy(&render->mode, mode, sizeof(drmModeModeInfo));
                        break;
                    }
                }
                else {
                    if (!(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
                        memcpy(&render->mode, mode, sizeof(drmModeModeInfo));
                        VideoWindowWidth = mode->hdisplay;
                        VideoWindowHeight = mode->vdisplay;
                        break;
                    }
                }
            }
            found = 1;
            i = resources->count_connectors;   // uuuuhh
        }
        VideoWindowWidth = render->mode.hdisplay;
        VideoWindowHeight = render->mode.vdisplay;
        if (found)
            printf("Use Mode %d %dx%d Rate %d\n",ii,render->mode.hdisplay,render->mode.vdisplay,render->mode.vrefresh);
        drmModeFreeConnector(connector);
    }
    if (!found) {
        Debug(3,"Requested Connector not found or not connected\n");
        printf("Requested Connector not found or not connected\n");
        return -1;
    }

    // find first plane
    if ((plane_res = drmModeGetPlaneResources(render->fd_drm)) == NULL)
        fprintf(stderr, "FindDevice: cannot retrieve PlaneResources (%d): %m\n", errno);

    for (j = 0; j < plane_res->count_planes; j++) {
        plane = drmModeGetPlane(render->fd_drm, plane_res->planes[j]);

        if (plane == NULL)
            fprintf(stderr, "FindDevice: cannot query DRM-KMS plane %d\n", j);

        for (i = 0; i < resources->count_crtcs; i++) {
            if (plane->possible_crtcs & (1 << i))
                break;
        }

        uint64_t type = GetPropertyValue(render->fd_drm, plane_res->planes[j],
                            DRM_MODE_OBJECT_PLANE, "type");
        uint64_t zpos = 0;

#ifdef DRM_DEBUG // If more then 2 crtcs this must rewriten!!!
        printf("[FindDevice] Plane id %i crtc_id %i possible_crtcs %i possible CRTC %i type %s\n",
            plane->plane_id, plane->crtc_id, plane->possible_crtcs, resources->crtcs[i],
            (type == DRM_PLANE_TYPE_PRIMARY) ? "primary plane" :
            (type == DRM_PLANE_TYPE_OVERLAY) ? "overlay plane" :
            (type == DRM_PLANE_TYPE_CURSOR) ? "cursor plane" : "No plane type");
#endif

        // test pixel format and plane caps
        for (k = 0; k < plane->count_formats; k++) {
            if (encoder->possible_crtcs & plane->possible_crtcs) {
                switch (plane->formats[k]) {
#ifdef RASPI
                    case DRM_FORMAT_ARGB8888:
#else
					case DRM_FORMAT_XRGB2101010:
#endif
                        if (!render->video_plane) {
                            render->video_plane = plane->plane_id;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(plane_res);

    drmModeFreeEncoder(encoder);
    drmModeFreeResources(resources);

#ifdef DRM_DEBUG
    printf("[FindDevice] DRM setup CRTC: %i video_plane: %i \n",
        render->crtc_id, render->video_plane);
#endif

    // save actual modesetting
    render->saved_crtc = drmModeGetCrtc(render->fd_drm, render->crtc_id);

    return 0;
}

///
/// Initialize video output module.
///
void VideoInitDrm()
{
    int i;


    if (!(render = calloc(1, sizeof(*render)))) {
        Fatal(_("video/DRM: out of memory\n"));
        return;
    }

    if (FindDevice(render)){
        Fatal(_( "VideoInit: FindDevice() failed\n"));
    }

    gbm.dev = gbm_create_device (render->fd_drm);
    assert (gbm.dev != NULL);

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
    get_platform_display =
        (void *) eglGetProcAddress("eglGetPlatformDisplay");
    assert(get_platform_display != NULL);

    eglDisplay = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm.dev, NULL);

    assert (eglDisplay != NULL);
//    return;

    drmModeAtomicReqPtr ModeReq;
    const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    uint32_t modeID = 0;

    if (drmModeCreatePropertyBlob(render->fd_drm, &render->mode, sizeof(render->mode), &modeID) != 0) {
        fprintf(stderr, "Failed to create mode property.\n");
        return;
    }
    if (!(ModeReq = drmModeAtomicAlloc())) {
        fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);
        return;
    }
	printf("set CRTC %d of Connector %d aktiv\n",render->crtc_id,render->connector_id);
    SetPropertyRequest(ModeReq, render->fd_drm, render->crtc_id,
                        DRM_MODE_OBJECT_CRTC, "MODE_ID", modeID);
    SetPropertyRequest(ModeReq, render->fd_drm, render->connector_id,
                        DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", render->crtc_id);
    SetPropertyRequest(ModeReq, render->fd_drm, render->crtc_id,
                        DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);

    if (drmModeAtomicCommit(render->fd_drm, ModeReq, flags, NULL) != 0)
        fprintf(stderr, "cannot set atomic mode (%d): %m\n", errno);

    if (drmModeDestroyPropertyBlob(render->fd_drm, modeID) != 0)
        fprintf(stderr, "cannot destroy property blob (%d): %m\n", errno);

    drmModeAtomicFree(ModeReq);

}

void get_drm_aspect(int *num,int *den)
{
	Debug(3,"mmHeight %d mmWidth %d  VideoHeight %d VideoWidth %d\n",render->mmHeight,render->mmWidth,VideoWindowHeight,VideoWindowWidth);
    *num = VideoWindowWidth * render->mmHeight;
    *den = VideoWindowHeight * render->mmWidth;
}

struct gbm_bo *bo = NULL, *next_bo=NULL;
struct drm_fb *fb;
static int m_need_modeset = 0;
static int old_color=-1,old_trc=-1;

void InitBo(int bpp) {
    // create the GBM and EGL surface
    render->bpp = bpp;
    gbm.surface = gbm_surface_create (gbm.dev, VideoWindowWidth,VideoWindowHeight,
                                      bpp==10?GBM_FORMAT_XRGB2101010:GBM_FORMAT_ARGB8888,
                                      GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
    assert(gbm.surface != NULL);
    eglSurface = eglCreateWindowSurface (eglDisplay, eglConfig, gbm.surface, NULL);
    assert(eglSurface != NULL);

}

static struct gbm_bo *previous_bo = NULL;
static uint32_t previous_fb;

static void drm_swap_buffers () {

    uint32_t fb;

    eglSwapBuffers (eglDisplay, eglSurface);
    struct gbm_bo *bo = gbm_surface_lock_front_buffer (gbm.surface);
#if 1
    if (bo == NULL)
        bo = gbm_surface_lock_front_buffer (gbm.surface);
#endif
    assert (bo != NULL);
    uint32_t handle = gbm_bo_get_handle (bo).u32;
    uint32_t pitch = gbm_bo_get_stride (bo);


    drmModeAddFB (render->fd_drm, VideoWindowWidth,VideoWindowHeight,render->bpp==10? 30:24, 32, pitch, handle, &fb);
//  drmModeSetCrtc (render->fd_drm, render->crtc_id, fb, 0, 0, &render->connector_id, 1, &render->mode);

    if (m_need_modeset) {
        drmModeAtomicReqPtr ModeReq;
        const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
        uint32_t modeID = 0;

        if (drmModeCreatePropertyBlob(render->fd_drm, &render->mode, sizeof(render->mode), &modeID) != 0) {
            fprintf(stderr, "Failed to create mode property.\n");
            return;
        }
        if (!(ModeReq = drmModeAtomicAlloc())) {
            fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);
            return;
        }

        // Need to disable the CRTC in order to submit the HDR data....
        SetPropertyRequest(ModeReq, render->fd_drm, render->crtc_id,
                            DRM_MODE_OBJECT_CRTC, "ACTIVE", 0);
        if (drmModeAtomicCommit(render->fd_drm, ModeReq, flags, NULL) != 0)
            fprintf(stderr, "cannot set atomic mode (%d): %m\n", errno);
        sleep(2);

        SetPropertyRequest(ModeReq, render->fd_drm, render->connector_id,
                            DRM_MODE_OBJECT_CONNECTOR, "Colorspace",old_color==AVCOL_PRI_BT2020?9:2 );
        SetPropertyRequest(ModeReq, render->fd_drm, render->crtc_id,
                            DRM_MODE_OBJECT_CRTC, "MODE_ID", modeID);
        SetPropertyRequest(ModeReq, render->fd_drm, render->connector_id,
                            DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", render->crtc_id);
        SetPropertyRequest(ModeReq, render->fd_drm, render->crtc_id,
                            DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);

        if (drmModeAtomicCommit(render->fd_drm, ModeReq, flags, NULL) != 0)
            fprintf(stderr, "cannot set atomic mode (%d): %m\n", errno);

        if (drmModeDestroyPropertyBlob(render->fd_drm, modeID) != 0)
            fprintf(stderr, "cannot destroy prperty blob (%d): %m\n", errno);

        drmModeAtomicFree(ModeReq);
        m_need_modeset = 0;
    }
    drmModeSetCrtc (render->fd_drm, render->crtc_id, fb, 0, 0, &render->connector_id, 1, &render->mode);

    if (previous_bo) {
        drmModeRmFB (render->fd_drm, previous_fb);
        gbm_surface_release_buffer (gbm.surface, previous_bo);
    }
    previous_bo = bo;
    previous_fb = fb;

}

static void drm_clean_up () {
    // set the previous crtc

    if (!render)
        return;
    Debug(3,"drm clean up\n");

	if (previous_bo) {
        drmModeRmFB (render->fd_drm, previous_fb);
        gbm_surface_release_buffer (gbm.surface, previous_bo);
    }

    drmModeSetCrtc (render->fd_drm, render->saved_crtc->crtc_id, render->saved_crtc->buffer_id,
                    render->saved_crtc->x, render->saved_crtc->y, &render->connector_id, 1, &render->saved_crtc->mode);
    drmModeFreeCrtc (render->saved_crtc);

    if (render->hdr_blob_id)
        drmModeDestroyPropertyBlob(render->fd_drm, render->hdr_blob_id);
	render->hdr_blob_id = 0;

    eglDestroySurface (eglDisplay, eglSurface);
	EglCheck();
    gbm_surface_destroy (gbm.surface);
    eglDestroyContext (eglDisplay, eglContext);
	EglCheck();
	eglDestroyContext (eglDisplay, eglSharedContext);
	EglCheck();
	eglSharedContext = NULL;

    eglTerminate (eglDisplay);
	EglCheck();

    gbm_device_destroy (gbm.dev);
	drmDropMaster(render->fd_drm);
    close (render->fd_drm);
	eglDisplay = NULL;
	free(render);

}
