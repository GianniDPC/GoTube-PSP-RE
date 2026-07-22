/* Lazy result thumbnail cache. Original structure/flow: renderer VA 0x1e380,
 * JPEG loader VAs 0xba98/0xb710 (GT12-THUMB-0001). */
#include "gotube.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <malloc.h>

#define THUMB_PATH "ms0:/PSP/GAME/GoTube/gotube.thumb.jpg"
#define THUMB_MAX_DIM 128

typedef struct {
    volatile int state; /* 0 empty, 1 queued, 2 loading, 3 ready, -1 failed */
    char url[512];
    unsigned short *pixels;
    int width, height, tex_width, tex_height, stride;
} ThumbSlot;

typedef struct {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} ThumbJpegError;

static ThumbSlot slots[MAX_RESULTS];
static volatile int thumb_cancel = 0;
static volatile int thumb_suspended = 0;
static SceUID thumb_thread = -1;

static void jpeg_failure(j_common_ptr info)
{
    ThumbJpegError *error = (ThumbJpegError *)info->err;
    longjmp(error->jump, 1);
}

static int pow2_dimension(int value)
{
    int result = 32;
    while (result < value && result < 512) result <<= 1;
    return result;
}

static int decode_jpeg(const char *path, ThumbSlot *slot)
{
    struct jpeg_decompress_struct jpeg;
    ThumbJpegError error;
    FILE *file = NULL;
    unsigned char *scanline = NULL;
    unsigned short *pixels = NULL;
    int source_width, source_height, output_width, output_height;
    int tex_width, tex_height, y;

    memset(&jpeg, 0, sizeof(jpeg));
    jpeg.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpeg_failure;
    if (setjmp(error.jump)) {
        if (scanline) free(scanline);
        if (pixels) free(pixels);
        if (file) fclose(file);
        jpeg_destroy_decompress(&jpeg);
        return -1;
    }
    file = fopen(path, "rb");
    if (!file) return -1;
    jpeg_create_decompress(&jpeg);
    jpeg_stdio_src(&jpeg, file);
    jpeg_read_header(&jpeg, TRUE);
    jpeg.out_color_space = JCS_RGB;
    jpeg_start_decompress(&jpeg);
    source_width = jpeg.output_width;
    source_height = jpeg.output_height;
    output_width = source_width > THUMB_MAX_DIM ? THUMB_MAX_DIM : source_width;
    output_height = source_height > THUMB_MAX_DIM ? THUMB_MAX_DIM : source_height;
    tex_width = pow2_dimension(output_width);
    tex_height = pow2_dimension(output_height);
    scanline = memalign(16, source_width * jpeg.output_components);
    pixels = memalign(64, tex_width * tex_height * sizeof(*pixels));
    if (!scanline || !pixels) longjmp(error.jump, 1);
    memset(pixels, 0, tex_width * tex_height * sizeof(*pixels));
    y = 0;
    while (jpeg.output_scanline < jpeg.output_height) {
        JSAMPROW row = scanline;
        int target_y = (int)((long long)jpeg.output_scanline * output_height / source_height);
        jpeg_read_scanlines(&jpeg, &row, 1);
        if (target_y >= output_height || target_y < y) continue;
        while (y <= target_y && y < output_height) {
            int x;
            for (x = 0; x < output_width; x++) {
                int sx = x * source_width / output_width;
                unsigned char *rgb = scanline + sx * 3;
                pixels[y * tex_width + x] = 0x8000 | (rgb[0] >> 3) |
                    ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10);
            }
            y++;
        }
    }
    jpeg_finish_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    fclose(file);
    free(scanline);
    sceKernelDcacheWritebackRange(pixels, tex_width * tex_height * sizeof(*pixels));
    slot->pixels = pixels;
    slot->width = output_width;
    slot->height = output_height;
    slot->tex_width = tex_width;
    slot->tex_height = tex_height;
    slot->stride = tex_width;
    return 0;
}

static int thumbnail_worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        int i, found = -1;
        if (thumb_suspended) {
            sceKernelDelayThread(20000);
            continue;
        }
        for (i = 0; i < MAX_RESULTS; i++)
            if (slots[i].state == 1) { found = i; break; }
        if (found < 0) {
            sceKernelDelayThread(20000);
            continue;
        }
        slots[found].state = 2;
        thumb_cancel = 0;
        if (((strncmp(slots[found].url, "ms0:/", 5) == 0 &&
              decode_jpeg(slots[found].url, &slots[found]) == 0) ||
             (strncmp(slots[found].url, "ms0:/", 5) != 0 &&
              go_http_download(slots[found].url, THUMB_PATH, NULL, &thumb_cancel) == 0 &&
              !thumb_cancel && decode_jpeg(THUMB_PATH, &slots[found]) == 0)))
            slots[found].state = 3;
        else
            slots[found].state = thumb_cancel ? 0 : -1;
        if (strncmp(slots[found].url, "ms0:/", 5) != 0) sceIoRemove(THUMB_PATH);
    }
    return 0;
}

void go_thumbnails_init(void)
{
    memset(slots, 0, sizeof(slots));
    thumb_thread = sceKernelCreateThread("GoTube.thumbnail", thumbnail_worker,
                                         0x28, 0x10000,
                                         PSP_THREAD_ATTR_USER, NULL);
    if (thumb_thread >= 0) sceKernelStartThread(thumb_thread, 0, NULL);
#ifdef GT_THUMB_SELFTEST
    if (decode_jpeg("ms0:/PSP/GAME/GoTube/thumbnail-selftest.jpg", &slots[0]) == 0) {
        slots[0].state = 3;
        gt_trace("thumbnail decode complete");
    } else {
        gt_trace("thumbnail decode error");
    }
#endif
}

void go_thumbnails_reset(void)
{
    int i;
    thumb_cancel = 1;
    while (1) {
        int busy = 0;
        for (i = 0; i < MAX_RESULTS; i++) if (slots[i].state == 2) busy = 1;
        if (!busy) break;
        sceKernelDelayThread(10000);
    }
    for (i = 0; i < MAX_RESULTS; i++) {
        if (slots[i].pixels) free(slots[i].pixels);
        memset(&slots[i], 0, sizeof(slots[i]));
    }
}

void go_thumbnails_suspend(int suspend)
{
    int i;
    thumb_suspended = suspend;
    if (suspend) {
        thumb_cancel = 1;
        for (;;) {
            int busy = 0;
            for (i = 0; i < MAX_RESULTS; i++)
                if (slots[i].state == 2) busy = 1;
            if (!busy) break;
            sceKernelDelayThread(10000);
        }
    }
}

const unsigned short *go_thumbnail_get(int index, int *width, int *height,
                                       int *texture_width, int *texture_height,
                                       int *stride)
{
    ThumbSlot *slot;
    if (index < 0 || index >= g_result_count || index >= MAX_RESULTS ||
        !g_results[index].thumb[0]) return NULL;
    slot = &slots[index];
    if (slot->state == 0) {
        strncpy(slot->url, g_results[index].thumb, sizeof(slot->url) - 1);
        slot->url[sizeof(slot->url) - 1] = 0;
        slot->state = 1;
    }
    if (slot->state != 3) return NULL;
    if (width) *width = slot->width;
    if (height) *height = slot->height;
    if (texture_width) *texture_width = slot->tex_width;
    if (texture_height) *texture_height = slot->tex_height;
    if (stride) *stride = slot->stride;
    return slot->pixels;
}
