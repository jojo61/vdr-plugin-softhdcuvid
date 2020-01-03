#include <libavutil/mastering_display_metadata.h>

/**
 * struct hdr_metadata_infoframe - HDR Metadata Infoframe Data.
 *
 * HDR Metadata Infoframe as per CTA 861.G spec. This is expected
 * to match exactly with the spec.
 *
 * Userspace is expected to pass the metadata information as per
 * the format described in this structure.
 */
struct hdr_metadata_infoframe {
    /**
     * @eotf: Electro-Optical Transfer Function (EOTF)
     * used in the stream.
     */
    __u8 eotf;
    /**
     * @metadata_type: Static_Metadata_Descriptor_ID.
     */
    __u8 metadata_type;
    /**
     * @display_primaries: Color Primaries of the Data.
     * These are coded as unsigned 16-bit values in units of
     * 0.00002, where 0x0000 represents zero and 0xC350
     * represents 1.0000.
     * @display_primaries.x: X cordinate of color primary.
     * @display_primaries.y: Y cordinate of color primary.
     */
    struct {
        __u16 x, y;
        } display_primaries[3];
    /**
     * @white_point: White Point of Colorspace Data.
     * These are coded as unsigned 16-bit values in units of
     * 0.00002, where 0x0000 represents zero and 0xC350
     * represents 1.0000.
     * @white_point.x: X cordinate of whitepoint of color primary.
     * @white_point.y: Y cordinate of whitepoint of color primary.
     */
    struct {
        __u16 x, y;
        } white_point;
    /**
     * @max_display_mastering_luminance: Max Mastering Display Luminance.
     * This value is coded as an unsigned 16-bit value in units of 1 cd/m2,
     * where 0x0001 represents 1 cd/m2 and 0xFFFF represents 65535 cd/m2.
     */
    __u16 max_display_mastering_luminance;
    /**
     * @min_display_mastering_luminance: Min Mastering Display Luminance.
     * This value is coded as an unsigned 16-bit value in units of
     * 0.0001 cd/m2, where 0x0001 represents 0.0001 cd/m2 and 0xFFFF
     * represents 6.5535 cd/m2.
     */
    __u16 min_display_mastering_luminance;
    /**
     * @max_cll: Max Content Light Level.
     * This value is coded as an unsigned 16-bit value in units of 1 cd/m2,
     * where 0x0001 represents 1 cd/m2 and 0xFFFF represents 65535 cd/m2.
     */
    __u16 max_cll;
    /**
     * @max_fall: Max Frame Average Light Level.
     * This value is coded as an unsigned 16-bit value in units of 1 cd/m2,
     * where 0x0001 represents 1 cd/m2 and 0xFFFF represents 65535 cd/m2.
     */
    __u16 max_fall;
};

/**
 * struct hdr_output_metadata - HDR output metadata
 *
 * Metadata Information to be passed from userspace
 */
struct hdr_output_metadata {
    /**
     * @metadata_type: Static_Metadata_Descriptor_ID.
     */
    __u32 metadata_type;
    /**
     * @hdmi_metadata_type1: HDR Metadata Infoframe.
     */
    union {
        struct hdr_metadata_infoframe hdmi_metadata_type1;
    };
};



enum hdr_metadata_eotf {
    EOTF_TRADITIONAL_GAMMA_SDR,
    EOTF_TRADITIONAL_GAMMA_HDR,
    EOTF_ST2084,
    EOTF_HLG,
};


enum metadata_id {
    METADATA_TYPE1,
};

void
weston_hdr_metadata(void *data,
            uint16_t display_primary_r_x,
            uint16_t display_primary_r_y,
            uint16_t display_primary_g_x,
            uint16_t display_primary_g_y,
            uint16_t display_primary_b_x,
            uint16_t display_primary_b_y,
            uint16_t white_point_x,
            uint16_t white_point_y,
            uint16_t min_luminance,
            uint16_t max_luminance,
            uint16_t max_cll,
            uint16_t max_fall,
            enum hdr_metadata_eotf eotf)
{
    uint8_t *data8;
    uint16_t *data16;

    data8 = data;

    *data8++ = eotf;
    *data8++ = METADATA_TYPE1;

    data16 = (void*)data8;

    *data16++ = display_primary_r_x;
    *data16++ = display_primary_r_y;
    *data16++ = display_primary_g_x;
    *data16++ = display_primary_g_y;
    *data16++ = display_primary_b_x;
    *data16++ = display_primary_b_y;
    *data16++ = white_point_x;
    *data16++ = white_point_y;

    *data16++ = max_luminance;
    *data16++ = min_luminance;
    *data16++ = max_cll;
    *data16++ = max_fall;
}

struct weston_vector {
    float f[4];
};

struct weston_colorspace {
    struct weston_vector r, g, b;
    struct weston_vector whitepoint;
    const char *name;
    const char *whitepoint_name;
};

struct weston_colorspace hdr10;

static const struct weston_colorspace bt470m = {
    .r = {{ 0.670f, 0.330f, }},
    .g = {{ 0.210f, 0.710f, }},
    .b = {{ 0.140f, 0.080f, }},
    .whitepoint = {{ 0.3101f, 0.3162f, }},
    .name = "BT.470 M",
    .whitepoint_name = "C",
};

static const struct weston_colorspace bt470bg = {
    .r = {{ 0.640f, 0.330f, }},
    .g = {{ 0.290f, 0.600f, }},
    .b = {{ 0.150f, 0.060f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "BT.470 B/G",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace smpte170m = {
    .r = {{ 0.630f, 0.340f, }},
    .g = {{ 0.310f, 0.595f, }},
    .b = {{ 0.155f, 0.070f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "SMPTE 170M",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace smpte240m = {
    .r = {{ 0.630f, 0.340f, }},
    .g = {{ 0.310f, 0.595f, }},
    .b = {{ 0.155f, 0.070f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "SMPTE 240M",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace bt709 = {
    .r = {{ 0.640f, 0.330f, }},
    .g = {{ 0.300f, 0.600f, }},
    .b = {{ 0.150f, 0.060f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "BT.709",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace bt2020 = {
    .r = {{ 0.708f, 0.292f, }},
    .g = {{ 0.170f, 0.797f, }},
    .b = {{ 0.131f, 0.046f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "BT.2020",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace srgb = {
    .r = {{ 0.640f, 0.330f, }},
    .g = {{ 0.300f, 0.600f, }},
    .b = {{ 0.150f, 0.060f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "sRGB",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace adobergb = {
    .r = {{ 0.640f, 0.330f, }},
    .g = {{ 0.210f, 0.710f, }},
    .b = {{ 0.150f, 0.060f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "AdobeRGB",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace dci_p3 = {
    .r = {{ 0.680f, 0.320f, }},
    .g = {{ 0.265f, 0.690f, }},
    .b = {{ 0.150f, 0.060f, }},
    .whitepoint = {{ 0.3127f, 0.3290f, }},
    .name = "DCI-P3 D65",
    .whitepoint_name = "D65",
};

static const struct weston_colorspace prophotorgb = {
    .r = {{ 0.7347f, 0.2653f, }},
    .g = {{ 0.1596f, 0.8404f, }},
    .b = {{ 0.0366f, 0.0001f, }},
    .whitepoint = {{ .3457, .3585 }},
    .name = "ProPhoto RGB",
    .whitepoint_name = "D50",
};

static const struct weston_colorspace ciergb = {
    .r = {{ 0.7347f, 0.2653f, }},
    .g = {{ 0.2738f, 0.7174f, }},
    .b = {{ 0.1666f, 0.0089f, }},
    .whitepoint = {{ 1.0f / 3.0f, 1.0f / 3.0f, }},
    .name = "CIE RGB",
    .whitepoint_name = "E",
};

static const struct weston_colorspace ciexyz = {
    .r = {{ 1.0f, 0.0f, }},
    .g = {{ 0.0f, 1.0f, }},
    .b = {{ 0.0f, 0.0f, }},
    .whitepoint = {{ 1.0f / 3.0f, 1.0f / 3.0f, }},
    .name = "CIE XYZ",
    .whitepoint_name = "E",
};

const struct weston_colorspace ap0 = {
    .r = {{ 0.7347f,  0.2653f, }},
    .g = {{ 0.0000f,  1.0000f, }},
    .b = {{ 0.0001f, -0.0770f, }},
    .whitepoint = {{ .32168f, .33767f, }},
    .name = "ACES primaries #0",
    .whitepoint_name = "D60",
};

const struct weston_colorspace ap1 = {
    .r = {{ 0.713f, 0.393f, }},
    .g = {{ 0.165f, 0.830f, }},
    .b = {{ 0.128f, 0.044f, }},
    .whitepoint = {{ 0.32168f, 0.33767f, }},
    .name = "ACES primaries #1",
    .whitepoint_name = "D60",
};

static const struct weston_colorspace * const colorspaces[] = {
    &bt470m,
    &bt470bg,
    &smpte170m,
    &smpte240m,
    &bt709,
    &bt2020,
    &srgb,
    &adobergb,
    &dci_p3,
    &prophotorgb,
    &ciergb,
    &ciexyz,
    &ap0,
    &ap1,
};
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof(a)[0])
const struct weston_colorspace *
weston_colorspace_lookup(const char *name)
{
    unsigned i;

    if (!name)
        return NULL;

    for (i = 0; i < ARRAY_LENGTH(colorspaces); i++) {
        const struct weston_colorspace *c = colorspaces[i];

        if (!strcmp(c->name, name))
            return c;
    }

    return NULL;
}

static int cleanup=0;


static uint16_t encode_xyy(float xyy)
{
    return xyy * 50000;
}
static AVMasteringDisplayMetadata md_save = {0};
static AVContentLightMetadata ld_save = {0};
static void set_hdr_metadata(int color,int trc, AVFrameSideData *sd1, AVFrameSideData *sd2)
{
  drmModeAtomicReqPtr ModeReq;
    struct weston_colorspace *cs;
    enum hdr_metadata_eotf eotf;
    struct hdr_output_metadata data;
    static uint32_t blob_id = 0;
    int ret,MaxCLL=1500,MaxFALL=400;
    int max_lum=4000,min_lum=0050;
    struct AVMasteringDisplayMetadata *md = NULL;
    struct AVContentLightMetadata *ld = NULL;
	
    if (render->hdr_metadata == -1) { // Metadata not supported
		return;
	}
    
    // clean up FFMEPG stuff
    if (trc == AVCOL_TRC_BT2020_10)
        trc = AVCOL_TRC_ARIB_STD_B67;
    if (trc == AVCOL_TRC_UNSPECIFIED)
        trc = AVCOL_TRC_BT709;
    if (color == AVCOL_PRI_UNSPECIFIED)
        color = AVCOL_PRI_BT709;
    
    if ((old_color == color && old_trc == trc && !sd1 && !sd2) || !render->hdr_metadata)
        return;  // nothing to do
    
    if (sd1)
        md = sd1->data;
        
    if (sd2)
        ld = sd2->data;
    
    if (md && !memcmp(md,&md_save,sizeof(md_save)))
        if (ld && !memcmp(ld,&ld_save,sizeof(ld_save))) {
            return;
        }
    else if (ld && !memcmp(ld,&ld_save,sizeof(ld_save))) {
        return;
    }
        
    if (ld)
        memcpy(&ld_save,ld,sizeof(ld_save));
    if (md)
        memcpy(&md_save,md,sizeof(md_save));
        
    Debug(3,"Update HDR to TRC %d color %d\n",trc,color);

    if (trc == AVCOL_TRC_BT2020_10)
        trc = AVCOL_TRC_ARIB_STD_B67;
    
    old_color = color;
    old_trc = trc;
    
    if (blob_id)
        drmModeDestroyPropertyBlob(render->fd_drm, blob_id);
    
    switch(trc) {
        case AVCOL_TRC_BT709:                                   // 1
        case AVCOL_TRC_UNSPECIFIED:                             // 2
            eotf = EOTF_TRADITIONAL_GAMMA_SDR;
            break;
        case AVCOL_TRC_BT2020_10:                               // 14
        case AVCOL_TRC_BT2020_12:
        case AVCOL_TRC_ARIB_STD_B67:                            // 18 HLG           
            eotf = EOTF_HLG;
            break;
        case AVCOL_TRC_SMPTE2084:                               // 16
            eotf = EOTF_ST2084;
        default:
            eotf = EOTF_TRADITIONAL_GAMMA_SDR;
            break;
    }
            
    switch (color) {
        case AVCOL_PRI_BT709:                                   // 1
        case AVCOL_PRI_UNSPECIFIED:                             // 2
            cs = weston_colorspace_lookup("BT.709");
            break;
        case AVCOL_PRI_BT2020:                                  // 9
            cs = weston_colorspace_lookup("BT.2020");
            break;
        case AVCOL_PRI_BT470BG:                                 // 5
            cs = weston_colorspace_lookup("BT.470 B/G");        // BT.601
            break;
        default:
            cs = weston_colorspace_lookup("BT.709");
            break;
    }
    
    if (md) {       // we got Metadata
        if (md->has_primaries) {
            Debug(3,"Mastering Display Metadata,\n has_primaries:%d has_luminance:%d \n"
                   "r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f) \n"
                   "min_luminance=%f, max_luminance=%f\n",
              md->has_primaries, md->has_luminance,
              av_q2d(md->display_primaries[0][0]),
              av_q2d(md->display_primaries[0][1]),
              av_q2d(md->display_primaries[1][0]),
              av_q2d(md->display_primaries[1][1]),
              av_q2d(md->display_primaries[2][0]),
              av_q2d(md->display_primaries[2][1]),
              av_q2d(md->white_point[0]), av_q2d(md->white_point[1]),
              av_q2d(md->min_luminance), av_q2d(md->max_luminance));

            cs = &hdr10;
            cs->r.f[0] = (float)md->display_primaries[0][0].num / (float)md->display_primaries[0][0].den;
            cs->r.f[1] = (float)md->display_primaries[0][1].num / (float)md->display_primaries[0][1].den;
            cs->g.f[0] = (float)md->display_primaries[1][0].num / (float)md->display_primaries[1][0].den;
            cs->g.f[1] = (float)md->display_primaries[1][1].num / (float)md->display_primaries[1][1].den;
            cs->b.f[0] = (float)md->display_primaries[2][0].num / (float)md->display_primaries[2][0].den;
            cs->b.f[1] = (float)md->display_primaries[2][1].num / (float)md->display_primaries[2][1].den;
            cs->whitepoint.f[0] = (float)md->white_point[0].num / (float)md->white_point[0].den;
            cs->whitepoint.f[1] = (float)md->white_point[1].num / (float)md->white_point[1].den;
        }
        if (md->has_luminance) {
            max_lum = av_q2d(md->max_luminance);
            min_lum = av_q2d(md->min_luminance) * 10000 ;
            printf("max_lum %d min_lum %d\n",max_lum,min_lum);
        }
    }
    if (ld) {
        Debug(3,"Has MaxCLL %d MaxFALL %d\n",ld->MaxCLL,ld->MaxFALL);
        MaxCLL = ld->MaxCLL;
        MaxFALL = ld->MaxFALL;
    }
    data.metadata_type = 7;   // ????????????????????????
    weston_hdr_metadata(&data.hdmi_metadata_type1,
                encode_xyy(cs->r.f[0]),
                encode_xyy(cs->r.f[1]),
                encode_xyy(cs->g.f[0]),
                encode_xyy(cs->g.f[1]),
                encode_xyy(cs->b.f[0]),
                encode_xyy(cs->b.f[1]),
                encode_xyy(cs->whitepoint.f[0]),
                encode_xyy(cs->whitepoint.f[1]),
                max_lum,        // max_display_mastering_luminance
                min_lum,        // min_display_mastering_luminance
                MaxCLL,         // Maximum Content Light Level (MaxCLL)
                MaxFALL,        // Maximum Frame-Average Light Level (MaxFALL)
                eotf);
    

    
    ret = drmModeCreatePropertyBlob(render->fd_drm, &data, sizeof(data), &blob_id);
    if (ret) {
        printf("DRM: HDR metadata: failed blob create \n");
		blob_id = 0;
        return;
    }

    ret = drmModeConnectorSetProperty(render->fd_drm, render->connector_id,
                      render->hdr_metadata, blob_id);
    if (ret) {
        printf("DRM: HDR metadata: failed property set %d\n",ret);
             
        if (blob_id)
            drmModeDestroyPropertyBlob(render->fd_drm, blob_id);
		blob_id = 0;
        return;
    }
    m_need_modeset = 1;
        
    Debug(3,"DRM: HDR metadata: prop set\n");
           
}

