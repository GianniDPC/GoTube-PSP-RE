/* Optional PSP firmware AVC/VME backend.
 *
 * This follows the direct-MP4 contract used by cooleyes' PSP AVC work and
 * Lua Player Plus: MOV supplies length-prefixed AVC samples plus avcC SPS/PPS,
 * sceMpeg turns each NAL sample into an AU, and sceMpegbase performs CSC into
 * an ordinary cached RGBA texture.  Every entry point is fallible; callers
 * retain FFmpeg as the compatibility fallback. */
#include "gotube.h"
#include <pspkernel.h>
#include <pspmpeg.h>
#include <psputility_modules.h>
#include <kubridge.h>
#include <malloc.h>

extern int cooleyesMeBootStart(int version, int type);

typedef struct {
    void *sps_buffer;
    int sps_size;
    void *pps_buffer;
    int pps_size;
    int nal_prefix_size;
    void *nal_buffer;
    int nal_size;
    int mode;
} GoAvcNal;

typedef struct { int unknown[2], width, height, rest[6]; } GoAvcInfo;
typedef struct {
    void *buffer[8];
    int unknown[3];
} GoAvcYuv;
typedef struct {
    int unknown0[4];
    GoAvcInfo *info;
    int unknown1[6];
    GoAvcYuv *yuv;
    int unknown2[12];
} GoAvcDetail2;
typedef struct {
    int height, width, mode0, mode1;
    void *buffer[8];
} GoAvcCsc;

/* Present in the firmware libraries and PSPSDK stubs, but intentionally not
 * exposed by the conservative public pspmpeg.h header. */
extern void sceMpegGetAvcNalAu(SceMpeg *, GoAvcNal *, SceMpegAu *);
extern void sceMpegAvcDecodeDetail2(SceMpeg *, GoAvcDetail2 **);
extern int sceMpegBaseCscAvc(void *, unsigned int, unsigned int, GoAvcCsc *);

typedef struct {
    int active, mpeg_initialized, mpeg_created, first_sample;
    int width, height, output_index;
    SceMpeg mpeg;
    SceMpegRingbuffer ring;
    SceMpegAu *au;
    void *mpeg_memory, *ddr, *au_buffer, *parameter_sets;
    unsigned char *output[2];
    int mpeg_memory_size, sps_size, pps_size, nal_prefix_size;
    unsigned int frames, decode_total_us, decode_max_us;
    unsigned int decode_empty;
} GoAvcHardware;

static GoAvcHardware hw;
static SceUID bridge_module = -1, mpeg_module = -1;

static int load_start_kernel_module(const char *path)
{
    int status = 0;
    SceUID id = kuKernelLoadModule(path, 0, NULL);
    if (id < 0) return id;
    if (sceKernelStartModule(id, 0, NULL, &status, NULL) < 0) return -1;
    return id;
}

static int avcc_parameters(const unsigned char *p, int size,
                           const unsigned char **sps, int *sps_size,
                           const unsigned char **pps, int *pps_size,
                           int *prefix)
{
    int pos, count;
    if (!p || size < 8 || p[0] != 1) return -1;
    *prefix = (p[4] & 3) + 1;
    count = p[5] & 31;
    if (count < 1) return -1;
    pos = 6;
    if (pos + 2 > size) return -1;
    *sps_size = (p[pos] << 8) | p[pos + 1]; pos += 2;
    if (*sps_size <= 0 || pos + *sps_size + 3 > size) return -1;
    *sps = p + pos; pos += *sps_size;
    count = p[pos++];
    if (count < 1 || pos + 2 > size) return -1;
    *pps_size = (p[pos] << 8) | p[pos + 1]; pos += 2;
    if (*pps_size <= 0 || pos + *pps_size > size) return -1;
    *pps = p + pos;
    return 0;
}

void go_avc_hw_shutdown(void)
{
    if (hw.mpeg_created) sceMpegDelete(hw.mpeg);
    if (hw.au) free(hw.au);
    if (hw.mpeg_memory) free(hw.mpeg_memory);
    if (hw.parameter_sets) free(hw.parameter_sets);
    if (hw.output[0]) free(hw.output[0]);
    if (hw.output[1]) free(hw.output[1]);
    if (hw.ddr) free(hw.ddr);
    if (hw.mpeg_initialized) sceMpegFinish();
    memset(&hw, 0, sizeof(hw));
}

int go_avc_hw_init(const unsigned char *extra, int extra_size,
                   int width, int height)
{
    const unsigned char *sps, *pps;
    int result, mode = 4;
    const char *stage = "avcC";
    int texture_bytes = 512 * ((height + 15) & ~15) * 4;
    go_avc_hw_shutdown();
    if (width <= 0 || width > 480 || height <= 0 || height > 272 ||
        avcc_parameters(extra, extra_size, &sps, &hw.sps_size,
                        &pps, &hw.pps_size, &hw.nal_prefix_size) < 0)
        return -1;
    stage = "avcodec module";
    result = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    /* Raw MP4 AVC requires mpeg_vsh, not the public MPEGBASE utility module.
     * Firmware 5.xx/6.xx supplies it in flash; the kernel bridge selects the
     * firmware-specific Media Engine boot NID. */
    stage = "mpeg_vsh module";
    if (mpeg_module < 0)
        mpeg_module = load_start_kernel_module("flash0:/kd/mpeg_vsh.prx");
    if (mpeg_module < 0) { result = mpeg_module; goto fail; }
    stage = "ME bridge module";
    if (bridge_module < 0)
        bridge_module = load_start_kernel_module(
            "ms0:/PSP/GAME/GoTube/cooleyesBridge.prx");
    if (bridge_module < 0) { result = bridge_module; goto fail; }
    stage = "ME boot";
    result = cooleyesMeBootStart(sceKernelDevkitVersion(),
                                 extra[1] == 0x42 ? 4 : 3);
    if (result < 0) goto fail;
    stage = "sceMpegInit";
    result = sceMpegInit();
    if (result < 0) goto fail;
    hw.mpeg_initialized = 1;
    hw.ddr = memalign(0x400000, 0x400000);
    hw.au = memalign(64, 64);
    hw.parameter_sets = memalign(64, hw.sps_size + hw.pps_size);
    hw.output[0] = memalign(64, texture_bytes);
    hw.output[1] = memalign(64, texture_bytes);
    stage = "sceMpegQueryMemSize";
    hw.mpeg_memory_size = sceMpegQueryMemSize(mode);
    if (hw.mpeg_memory_size <= 0) goto fail;
    hw.mpeg_memory = memalign(64, (hw.mpeg_memory_size + 63) & ~63);
    if (!hw.ddr || !hw.au || !hw.parameter_sets || !hw.output[0] ||
        !hw.output[1] || !hw.mpeg_memory) goto fail;
    memset(&hw.ring, 0, sizeof(hw.ring));
    memset(hw.au, 0xff, 64);
    memset(hw.output[0], 0, texture_bytes);
    memset(hw.output[1], 0, texture_bytes);
    memcpy(hw.parameter_sets, sps, hw.sps_size);
    memcpy((unsigned char *)hw.parameter_sets + hw.sps_size, pps, hw.pps_size);
    hw.au_buffer = (unsigned char *)hw.ddr + 0x10000;
    stage = "sceMpegCreate";
    result = sceMpegCreate(&hw.mpeg, hw.mpeg_memory, hw.mpeg_memory_size,
                           &hw.ring, 512, mode, (int)hw.ddr);
    if (result < 0) goto fail;
    hw.mpeg_created = 1;
    stage = "sceMpegInitAu";
    result = sceMpegInitAu(&hw.mpeg, hw.au_buffer, hw.au);
    if (result < 0) goto fail;
    hw.width = width; hw.height = height; hw.first_sample = 1; hw.active = 1;
    go_modern_trace("AVC_HW init ok size=%dx%d profile=%02x prefix=%d sps=%d pps=%d",
                    width, height, extra[1], hw.nal_prefix_size,
                    hw.sps_size, hw.pps_size);
    return 0;
fail:
    go_modern_trace("AVC_HW init failed stage=%s result=%08x", stage, result);
    go_avc_hw_shutdown();
    return -1;
}

int go_avc_hw_decode(const unsigned char *data, int size,
                     const unsigned char **rgba, int *stride)
{
    GoAvcNal nal;
    GoAvcDetail2 *detail = NULL;
    GoAvcCsc csc;
    int result, i;
    SceInt32 pictures = 0;
    uint64_t started;
    if (!hw.active || !data || size <= 0) return -1;
    started = sceKernelGetSystemTimeWide();
    memset(&nal, 0, sizeof(nal));
    nal.sps_buffer = hw.parameter_sets; nal.sps_size = hw.sps_size;
    nal.pps_buffer = (unsigned char *)hw.parameter_sets + hw.sps_size;
    nal.pps_size = hw.pps_size; nal.nal_prefix_size = hw.nal_prefix_size;
    nal.nal_buffer = (void *)data; nal.nal_size = size;
    nal.mode = hw.first_sample ? 3 : 0;
    sceKernelDcacheWritebackRange((void *)data, size);
    /* These two firmware calls are void in the original cooleyes interface;
     * treating the leftover v0 register as a status creates false failures. */
    sceMpegGetAvcNalAu(&hw.mpeg, &nal, hw.au);
    result = sceMpegAvcDecode(&hw.mpeg, hw.au, 512, NULL, &pictures);
    if (result < 0) {
        /* 0x80628002 occurs at valid stream boundaries in the established
         * direct-MP4 path. The reference player ignores it and continues.
         * Keep a guard so a genuinely wedged decoder still reaches FFmpeg. */
        if ((unsigned int)result == 0x80628002U && ++hw.decode_empty <= 30) {
            if (hw.decode_empty == 1)
                go_modern_trace("AVC_HW transient empty size=%d", size);
            hw.first_sample = 0;
            return 0;
        }
        goto fail;
    }
    hw.first_sample = 0;
    if (pictures <= 0) return 0;
    hw.decode_empty = 0;
    sceMpegAvcDecodeDetail2(&hw.mpeg, &detail);
    if (!detail || !detail->info || !detail->yuv) { result = -1; goto fail; }
    memset(&csc, 0, sizeof(csc));
    csc.height = (detail->info->height + 15) >> 4;
    csc.width = (detail->info->width + 15) >> 4;
    for (i = 0; i < 8; i++) csc.buffer[i] = detail->yuv->buffer[i];
    hw.output_index ^= 1;
    result = sceMpegBaseCscAvc(hw.output[hw.output_index], 0, 512, &csc);
    if (result < 0) goto fail;
    sceKernelDcacheInvalidateRange(hw.output[hw.output_index],
                                  512 * ((hw.height + 15) & ~15) * 4);
    {
        unsigned int elapsed = (unsigned int)(sceKernelGetSystemTimeWide() - started);
        hw.frames++;
        hw.decode_total_us += elapsed;
        if (elapsed > hw.decode_max_us) hw.decode_max_us = elapsed;
        if ((hw.frames % 120) == 0) {
            go_modern_trace("AVC_HW perf frames=%u avg_us=%u max_us=%u",
                            hw.frames, hw.decode_total_us / hw.frames,
                            hw.decode_max_us);
        }
    }
    if (rgba) *rgba = hw.output[hw.output_index];
    if (stride) *stride = 512;
    return 1;
fail:
    go_modern_trace("AVC_HW decode failed result=%08x size=%d first=%d",
                    result, size, hw.first_sample);
    hw.active = 0;
    return -1;
}

int go_avc_hw_active(void) { return hw.active; }
