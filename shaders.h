// shader
#define SHADER_LENGTH 10000

#ifdef CUVID
const char *gl_version = "#version 330";
#else
#ifdef RASPI
const char *gl_version = "#version 300 es";
#else
const char *gl_version = "#version 300 es ";
#endif
#endif

/* Color conversion matrix: RGB = m * YUV + c
 * m is in row-major matrix, with m[row][col], e.g.:
 *     [ a11 a12 a13 ]     float m[3][3] = { { a11, a12, a13 },
 *     [ a21 a22 a23 ]                       { a21, a22, a23 },
 *     [ a31 a32 a33 ]                       { a31, a32, a33 } };
 * This is accessed as e.g.: m[2-1][1-1] = a21
 * In particular, each row contains all the coefficients for one of R, G, B,
 * while each column contains all the coefficients for one of Y, U, V:
 *     m[r,g,b][y,u,v] = ...
 * The matrix could also be viewed as group of 3 vectors, e.g. the 1st column
 * is the Y vector (1, 1, 1), the 2nd is the U vector, the 3rd the V vector.
 * The matrix might also be used for other conversions and colorspaces.
 */
struct mp_cmat
{
    GLfloat m[3][3];                    // colormatrix
    GLfloat c[3];                       //colormatrix_c
};

struct mp_mat
{
    GLfloat m[3][3];
};

// YUV input limited range (16-235 for luma, 16-240 for chroma)
// ITU-R BT.601 (SD)
struct mp_cmat yuv_bt601 = { {{1.164384, 1.164384, 1.164384},
    						  {0.00000, -0.391762, 2.017232},
    						  {1.596027, -0.812968, 0.000000}},
							  {-0.874202, 0.531668, -1.085631}
};

// ITU-R BT.709 (HD)
struct mp_cmat yuv_bt709 = { {{1.164384, 1.164384, 1.164384},
    						  {0.00000, -0.213249, 2.112402},
    						  {1.792741, -0.532909, 0.000000}},
							  {-0.972945, 0.301483, -1.133402}
};

// ITU-R BT.2020 non-constant luminance system
struct mp_cmat yuv_bt2020ncl = { {{1.164384, 1.164384, 1.164384},
    							  {0.00000, -0.187326, 2.141772},
    							  {1.678674, -0.650424, 0.000000}},
								  {-0.915688, 0.347459, -1.148145}
};

// ITU-R BT.2020 constant luminance system
struct mp_cmat yuv_bt2020cl = { {{0.0000, 1.164384, 0.000000},
    							 {0.00000, 0.000000, 1.138393},
    							 {1.138393, 0.000000, 0.000000}},
							  	 {-0.571429, -0.073059, -0.571429}
};

float cms_matrix[3][3] = { {1.660497, -0.124547, -0.018154},
						 {-0.587657, 1.132895, -0.100597},
						 {-0.072840, -0.008348, 1.118751}
};

// Common constants for SMPTE ST.2084 (PQ)
static const float PQ_M1 = 2610. / 4096 * 1. / 4,
				   PQ_M2 = 2523. / 4096 * 128,
				   PQ_C1 = 3424. / 4096, 
				   PQ_C2 =    2413. / 4096 * 32, 
				   PQ_C3 = 2392. / 4096 * 32;

// Common constants for ARIB STD-B67 (HLG)
static const float HLG_A = 0.17883277,
				   HLG_B = 0.28466892, 
				   HLG_C = 0.55991073;

struct gl_vao_entry
{
    // used for shader / glBindAttribLocation
    const char *name;
    // glVertexAttribPointer() arguments
    int num_elems;                      // size (number of elements)
    GLenum type;
    bool normalized;
    int offset;
};

struct vertex_pt
{
    float x, y;
};

struct vertex_pi
{
    GLint x, y;
};

#define TEXUNIT_VIDEO_NUM 6

struct vertex
{
    struct vertex_pt position;
    struct vertex_pt texcoord[TEXUNIT_VIDEO_NUM];
};

static const struct gl_vao_entry vertex_vao[] = {
    {"position", 2, GL_FLOAT, false, offsetof(struct vertex, position)},
    {"texcoord0", 2, GL_FLOAT, false, offsetof(struct vertex, texcoord[0])},
    {"texcoord1", 2, GL_FLOAT, false, offsetof(struct vertex, texcoord[1])},
    {0}
};

#define GLSL(...)  pl_shader_append(__VA_ARGS__)
#define GLSLV(...)  pl_shader_append_v(__VA_ARGS__)

char sh[SHADER_LENGTH];
char shv[SHADER_LENGTH];

void GL_init()
{
    sh[0] = 0;
}

void GLV_init()
{
    shv[0] = 0;
}

void pl_shader_append(const char *fmt, ...)
{
    char temp[1000];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(temp, fmt, ap);
    va_end(ap);

    if (strlen(sh) + strlen(temp) > SHADER_LENGTH)
        Fatal(_("Shaderlenght fault\n"));
    strcat(sh, temp);

}

void pl_shader_append_v(const char *fmt, ...)
{
    char temp[1000];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(temp, fmt, ap);
    va_end(ap);

    if (strlen(shv) + strlen(temp) > SHADER_LENGTH)
        Fatal(_("Shaderlenght fault\n"));
    strcat(shv, temp);

}

static void compile_attach_shader(GLuint program, GLenum type, const char *source)
{
    GLuint shader;
    GLint status = 1234, log_length;
    char log[4000];
    GLsizei len;

    shader = glCreateShader(type);
    glShaderSource(shader, 1, (const GLchar **)&source, NULL);  // &buffer, NULL);
    glCompileShader(shader);
    status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    glGetShaderInfoLog(shader, 4000, &len, log);
    GlxCheck();
    Debug(3, "compile Status %d loglen %d >%s<\n", status, log_length, log);

    glAttachShader(program, shader);
    glDeleteShader(shader);

}

static void link_shader(GLuint program)
{
    GLint status, log_length;

    glLinkProgram(program);
    status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    Debug(3, "Link Status %d loglen %d\n", status, log_length);
}

static GLuint sc_generate_osd(GLuint gl_prog)
{

    Debug(3, "vor create osd\n");
    gl_prog = glCreateProgram();

    GL_init();
    GLSL("%s\n", gl_version);
    GLSL("in vec2 vertex_position;\n");
    GLSL("in vec2 vertex_texcoord0;\n");
    GLSL("out vec2 texcoord0;\n");
    GLSL("void main() {\n");
    GLSL("gl_Position = vec4(vertex_position, 1.0, 1.0);\n");
    GLSL("texcoord0 = vertex_texcoord0;\n");
    GLSL("}\n");

    Debug(3, "vor compile vertex osd\n");
    compile_attach_shader(gl_prog, GL_VERTEX_SHADER, sh);   // vertex_osd);
    GL_init();
    GLSL("%s\n", gl_version);
    GLSL("#define texture1D texture\n");
    GLSL("precision mediump float; \n");
    GLSL("layout(location = 0) out vec4 out_color;\n");
    GLSL("in vec2 texcoord0;\n");
    GLSL("uniform sampler2D texture0;\n");
    GLSL("void main() {\n");
    GLSL("vec4 color; \n");
    GLSL("color = vec4(texture(texture0, texcoord0));\n");
#ifdef GAMMA
    GLSL("// delinearize gamma                     \n");
    GLSL("color.rgb = clamp(color.rgb, 0.0, 1.0);  \n");    // delinearize gamma
    GLSL("color.rgb = pow(color.rgb, vec3(2.4));   \n");
#endif
    GLSL("out_color = color;\n");
    GLSL("}\n");
    Debug(3, "vor compile fragment osd \n");
    compile_attach_shader(gl_prog, GL_FRAGMENT_SHADER, sh); //fragment_osd);
    glBindAttribLocation(gl_prog, 0, "vertex_position");
    glBindAttribLocation(gl_prog, 1, "vertex_texcoord0");

    link_shader(gl_prog);

    return gl_prog;
}

static GLuint sc_generate(GLuint gl_prog, enum AVColorSpace colorspace)
{

    char vname[80];
    int n;
    GLint cmsLoc;
    float *m, *c, *cms;
    char *frag;

    GL_init();
    GLSL("%s\n", gl_version);
    GLSL("in vec2 vertex_position;      \n");
    GLSL("in vec2 vertex_texcoord0;     \n");
    GLSL("out vec2 texcoord0;           \n");
    GLSL("in vec2 vertex_texcoord1;     \n");
    GLSL("out vec2 texcoord1;           \n");
    if (Planes == 3) {
        GLSL("in vec2 vertex_texcoord2; \n");
        GLSL("out vec2 texcoord2;       \n");
    }
    GLSL("void main() {                 \n");
    GLSL("gl_Position = vec4(vertex_position, 1.0, 1.0);\n");
    GLSL("texcoord0 = vertex_texcoord0; \n");
    GLSL("texcoord1 = vertex_texcoord1; \n");
    if (Planes == 3) {
        GLSL("texcoord2 = vertex_texcoord1; \n");   // texcoord1 ist hier richtig
    }
    GLSL("}                             \n");

    Debug(3, "vor create\n");
    gl_prog = glCreateProgram();
    Debug(3, "vor compile vertex\n");
//  printf("%s",sh);
    compile_attach_shader(gl_prog, GL_VERTEX_SHADER, sh);

    switch (colorspace) {
        case AVCOL_SPC_RGB:
		case AVCOL_SPC_BT470BG:
            m = &yuv_bt601.m[0][0];
            c = &yuv_bt601.c[0];
            Debug(3, "BT601 Colorspace used\n");
            break;
        case AVCOL_SPC_BT709:
        case AVCOL_SPC_UNSPECIFIED:    //  comes with UHD
            m = &yuv_bt709.m[0][0];
            c = &yuv_bt709.c[0];
            Debug(3, "BT709 Colorspace used\n");
            break;
        case AVCOL_SPC_BT2020_NCL:
            m = &yuv_bt2020ncl.m[0][0];
            c = &yuv_bt2020ncl.c[0];
            cms = &cms_matrix[0][0];
            Debug(3, "BT2020NCL Colorspace used\n");
            break;
        default:                       // fallback
            m = &yuv_bt709.m[0][0];
            c = &yuv_bt709.c[0];
            Debug(3, "default BT709 Colorspace used  %d\n", colorspace);
            break;
    }

    GL_init();

    GLSL("%s\n", gl_version);
    GLSL("precision mediump float;    \n");
    GLSL("layout(location = 0) out vec4 out_color;\n");
    GLSL("in vec2 texcoord0;          \n");
    GLSL("in vec2 texcoord1;          \n");
    if (Planes == 3)
        GLSL("in vec2 texcoord2;      \n");
    GLSL("uniform mat3 colormatrix;   \n");
    GLSL("uniform vec3 colormatrix_c; \n");
    if (colorspace == AVCOL_SPC_BT2020_NCL)
        GLSL("uniform mat3 cms_matrix;\n");
    GLSL("uniform sampler2D texture0; \n");
    GLSL("uniform sampler2D texture1; \n");
    if (Planes == 3)
        GLSL("uniform sampler2D texture2; \n");
    GLSL("void main() {               \n");
    GLSL("vec4 color;                 \n");

    if (colorspace == AVCOL_SPC_BT2020_NCL) {
        GLSL("color.r = 1.003906 * vec4(texture(texture0, texcoord0)).r;     \n");
        if (Planes == 3) {
            GLSL("color.g = 1.003906 * vec4(texture(texture1, texcoord1)).r;  \n");
            GLSL("color.b = 1.003906 * vec4(texture(texture2, texcoord2)).r;  \n");
        } else {
            GLSL("color.gb = 1.003906 * vec4(texture(texture1, texcoord1)).rg;\n");
        }
        GLSL("// color conversion\n");
        GLSL("color.rgb = mat3(colormatrix) * color.rgb  + colormatrix_c;     \n");
        GLSL("color.a = 1.0;                             \n");

        GLSL("// pl_shader_linearize                     \n");
        GLSL("color.rgb = max(color.rgb, 0.0);           \n");
//      GLSL("color.rgb = clamp(color.rgb, 0.0, 1.0);    \n");
//      GLSL("color.rgb = pow(color.rgb, vec3(2.4));     \n");
//      GLSL("color.rgb = mix(vec3(4.0) * color.rgb * color.rgb,exp((color.rgb - vec3(%f))         * vec3(1.0/%f))         + vec3(%f)        , bvec3(lessThan(vec3(0.5), color.rgb)));\n",HLG_C, HLG_A, HLG_B);
        GLSL("color.rgb = mix(vec3(4.0) * color.rgb * color.rgb,exp((color.rgb - vec3(0.55991073)) * vec3(1.0/0.17883277)) + vec3(0.28466892), bvec3(lessThan(vec3(0.5), color.rgb)));\n");
		GLSL("color.rgb *= vec3(1.0/3.17955);            \n");  // PL_COLOR_SDR_WHITE_HLG
        GLSL("// color mapping                           \n");
        GLSL("color.rgb = cms_matrix * color.rgb;        \n");
#ifndef GAMMA
        GLSL("// pl_shader_delinearize                   \n");
        GLSL("color.rgb = max(color.rgb, 0.0);           \n");
//      GLSL("color.rgb = clamp(color.rgb, 0.0, 1.0);    \n");
//      GLSL("color.rgb = pow(color.rgb, vec3(1.0/2.4)); \n");
		GLSL("color.rgb *= vec3(3.17955);                \n");  // PL_COLOR_SDR_WHITE_HLG
        GLSL("color.rgb = mix(vec3(0.5) * sqrt(color.rgb), vec3(0.17883277) * log(color.rgb - vec3(0.28466892)) + vec3(0.55991073), bvec3(lessThan(vec3(1.0), color.rgb))); \n");

#endif
        GLSL("out_color = color;                         \n");
        GLSL("} \n");
    } else {

        GLSL("color.r =  1.000000 * vec4(texture(texture0, texcoord0)).r;  \n");
        if (Planes == 3) {
            GLSL("color.g = 1.000000 * vec4(texture(texture1, texcoord1)).r;\n");
            GLSL("color.b = 1.000000 * vec4(texture(texture2, texcoord2)).r;\n");
        } else {
            GLSL("color.gb = 1.000000 * vec4(texture(texture1, texcoord1)).rg; \n");
        }
        GLSL("// color conversion         \n");
        GLSL("color.rgb = mat3(colormatrix) * color.rgb  + colormatrix_c;  \n");
        GLSL("color.a = 1.0;              \n");

        GLSL("// linearize gamma                     \n");
        GLSL("color.rgb = clamp(color.rgb, 0.0, 1.0);  \n");    // linearize gamma
        GLSL("color.rgb = pow(color.rgb, vec3(2.4));   \n");
#ifndef GAMMA
        GLSL("// delinearize gamma to sRGB               \n");
        GLSL("color.rgb = max(color.rgb, 0.0);         \n");
        GLSL("color.rgb = mix(color.rgb * vec3(12.92), vec3(1.055) * pow(color.rgb, vec3(1.0/2.4)) - vec3(0.055), bvec3(lessThanEqual(vec3(0.0031308), color.rgb))); \n");
#endif
        GLSL("// color mapping            \n");
        GLSL("out_color = color;          \n");
        GLSL("} \n");
    }
//printf(">%s<",sh);
    Debug(3, "vor compile fragment\n");
    compile_attach_shader(gl_prog, GL_FRAGMENT_SHADER, sh);
    glBindAttribLocation(gl_prog, 0, "vertex_position");

    for (n = 0; n < 6; n++) {
        sprintf(vname, "vertex_texcoord%1d", n);
        glBindAttribLocation(gl_prog, n + 1, vname);
    }

    link_shader(gl_prog);

    gl_colormatrix = glGetUniformLocation(gl_prog, "colormatrix");
    Debug(3, "get uniform colormatrix %d \n", gl_colormatrix);
    if (gl_colormatrix != -1)
        glProgramUniformMatrix3fv(gl_prog, gl_colormatrix, 1, 0, m);
    GlxCheck();
    Debug(3, "nach set colormatrix\n");

    gl_colormatrix_c = glGetUniformLocation(gl_prog, "colormatrix_c");
    Debug(3, "get uniform colormatrix_c %d %f\n", gl_colormatrix_c, *c);
    if (gl_colormatrix_c != -1)
        glProgramUniform3fv(gl_prog, gl_colormatrix_c, 1, c);
    GlxCheck();

    if (colorspace == AVCOL_SPC_BT2020_NCL) {
        cmsLoc = glGetUniformLocation(gl_prog, "cms_matrix");
        if (cmsLoc != -1)
            glProgramUniformMatrix3fv(gl_prog, cmsLoc, 1, 0, cms);
        GlxCheck();
    }

    return gl_prog;
}

static void render_pass_quad(int flip, float xcrop, float ycrop)
{
    struct vertex va[4];
    int n;
    const struct gl_vao_entry *e;

    // uhhhh what a hack
    if (!flip) {
        va[0].position.x = (float)-1.0;
        va[0].position.y = (float)1.0;
        va[1].position.x = (float)-1.0;
        va[1].position.y = (float)-1.0;
        va[2].position.x = (float)1.0;
        va[2].position.y = (float)1.0;
        va[3].position.x = (float)1.0;
        va[3].position.y = (float)-1.0;
    } else {
        va[0].position.x = (float)-1.0;
        va[0].position.y = (float)-1.0;
        va[1].position.x = (float)-1.0;
        va[1].position.y = (float)1.0;
        va[2].position.x = (float)1.0;
        va[2].position.y = (float)-1.0;
        va[3].position.x = (float)1.0;
        va[3].position.y = (float)1.0;
    }

    va[0].texcoord[0].x = (float)0.0 + xcrop;
    va[0].texcoord[0].y = (float)0.0 + ycrop;   // abgeschnitten von links oben
    va[0].texcoord[1].x = (float)0.0 + xcrop;
    va[0].texcoord[1].y = (float)0.0 + ycrop;   // abgeschnitten von links oben
    va[1].texcoord[0].x = (float)0.0 + xcrop;
    va[1].texcoord[0].y = (float)1.0 - ycrop;   // abgeschnitten links unten 1.0 - Wert
    va[1].texcoord[1].x = (float)0.0 + xcrop;
    va[1].texcoord[1].y = (float)1.0 - ycrop;   // abgeschnitten links unten 1.0 - Wert
    va[2].texcoord[0].x = (float)1.0 - xcrop;
    va[2].texcoord[0].y = (float)0.0 + ycrop;   // abgeschnitten von rechts oben
    va[2].texcoord[1].x = (float)1.0 - xcrop;
    va[2].texcoord[1].y = (float)0.0 + ycrop;   // abgeschnitten von rechts oben
    va[3].texcoord[0].x = (float)1.0 - xcrop;
    va[3].texcoord[0].y = (float)1.0 - ycrop;   // abgeschnitten von rechts unten 1.0 - wert
    va[3].texcoord[1].x = (float)1.0 - xcrop;
    va[3].texcoord[1].y = (float)1.0 - ycrop;   // abgeschnitten von rechts unten 1.0 - wert

    glBindBuffer(GL_ARRAY_BUFFER, vao_buffer);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(struct vertex), va, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // enable attribs
    glBindBuffer(GL_ARRAY_BUFFER, vao_buffer);
    for (n = 0; vertex_vao[n].name; n++) {
        e = &vertex_vao[n];
        glEnableVertexAttribArray(n);
        glVertexAttribPointer(n, e->num_elems, e->type, e->normalized, sizeof(struct vertex),
            (void *)(intptr_t) e->offset);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // draw quad
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (n = 0; vertex_vao[n].name; n++)
        glDisableVertexAttribArray(n);
}
