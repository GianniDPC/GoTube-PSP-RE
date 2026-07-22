/* Save action reconstructed from dispatcher VA 0x2097c and completion VA
 * 0x1f2e0: OSK filename, video file plus `.THM` sidecar. */
#include "gotube.h"
#include <libccc.h>

#include "cp932_reverse.inc"

static GTVideo pending_video;
static char save_url[2048];
static char save_thumb[512];
static char save_path[512];
static char save_thumb_path[520];
static volatile int save_state = 0;
static volatile int save_progress = 0;
static volatile int save_cancel = 0;
static SceUID save_thread = -1;

static int append_byte(char *out, int out_size, int *used, unsigned char value)
{
    if (*used + 1 >= out_size) return -1;
    out[(*used)++] = value;
    out[*used] = 0;
    return 0;
}

static unsigned short ucs2_to_cp932(unsigned short code)
{
    int low = 0, high = sizeof(gt_cp932_reverse) / sizeof(gt_cp932_reverse[0]) - 1;
    if (code < 0x80) return code;
    while (low <= high) {
        int middle = (low + high) / 2;
        if (gt_cp932_reverse[middle].ucs2 == code) return gt_cp932_reverse[middle].cp932;
        if (gt_cp932_reverse[middle].ucs2 < code) low = middle + 1;
        else high = middle - 1;
    }
    return 0;
}

int go_filename_sanitize(const char *utf8, char *out, int out_size)
{
    char sjis[400];
    cccUCS2 wide[400];
    int chars, target_used = 0;
    int i = 0, used = 0;
    chars = cccUTF8toUCS2(wide, sizeof(wide) / sizeof(wide[0]),
                           (const cccCode *)utf8);
    if (chars < 0) return -1;
    for (i = 0; i < chars && wide[i]; i++) {
        unsigned short encoded = ucs2_to_cp932(wide[i]);
        if (!encoded || target_used + (encoded > 0xff ? 2 : 1) >= (int)sizeof(sjis))
            return -1;
        if (encoded > 0xff) sjis[target_used++] = encoded >> 8;
        sjis[target_used++] = encoded & 0xff;
    }
    sjis[target_used] = 0;
    out[0] = 0;
    i = 0;
    while (sjis[i]) {
        unsigned char c = sjis[i++];
        if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xef)) {
            if (append_byte(out, out_size, &used, c) < 0 || !sjis[i] ||
                append_byte(out, out_size, &used, sjis[i++]) < 0) return -1;
        } else {
            const unsigned char *replacement = NULL;
            static const unsigned char quote[] = {0x81,0x68,0};
            static const unsigned char star[]  = {0x81,0x96,0};
            static const unsigned char slash[] = {0x81,0x5e,0};
            static const unsigned char colon[] = {0x81,0x46,0};
            static const unsigned char semi[]  = {0x81,0x47,0};
            static const unsigned char less[]  = {0x81,0x83,0};
            static const unsigned char more[]  = {0x81,0x84,0};
            static const unsigned char quest[] = {0x81,0x48,0};
            static const unsigned char back[]  = {0x81,0x8f,0};
            static const unsigned char pipe[]  = {0x81,0x62,0};
            switch (c) {
            case '"': replacement=quote; break; case '*': replacement=star; break;
            case '/': replacement=slash; break; case ':': replacement=colon; break;
            case ';': replacement=semi; break; case '<': replacement=less; break;
            case '>': replacement=more; break; case '?': replacement=quest; break;
            case '\\': replacement=back; break; case '|': replacement=pipe; break;
            default: break;
            }
            if (replacement) {
                if (append_byte(out,out_size,&used,replacement[0]) < 0 ||
                    append_byte(out,out_size,&used,replacement[1]) < 0) return -1;
            } else if (append_byte(out, out_size, &used, c) < 0) return -1;
        }
    }
    return used > 0 ? 0 : -1;
}

/* Original helper VA 0xc208 removes the final extension after the last path
 * separator, then appends the requested extension. */
int go_sidecar_path(const char *path, const char *extension,
                    char *out, int out_size)
{
    const char *slash, *dot;
    int base_length;
    if (!path || !extension || !out || out_size < 2) return -1;
    slash = strrchr(path, '/');
    dot = strrchr(path, '.');
    if (dot && (!slash || dot > slash)) base_length = dot - path;
    else base_length = strlen(path);
    if (base_length + strlen(extension) + 1 > (unsigned)out_size) return -1;
    memcpy(out, path, base_length);
    out[base_length] = 0;
    strcat(out, extension);
    return 0;
}

static int save_worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    save_state = 1;
    if (save_thumb[0])
        (void)go_http_download(save_thumb, save_thumb_path,
                               NULL, &save_cancel);
    if (!save_cancel && go_http_download(save_url, save_path,
                                         &save_progress, &save_cancel) == 0)
        save_state = 2;
    else
        save_state = -1;
    go_thumbnails_suspend(0);
    sceKernelExitThread(0);
    return 0;
}

int go_save_prepare(const GTVideo *video, char *initial, int initial_size)
{
    if (!video || !video->url[0] || !video->save_filename[0]) return -1;
    pending_video = *video;
    strncpy(initial, video->save_filename, initial_size - 1);
    initial[initial_size - 1] = 0;
    return 0;
}

int go_save_start(const char *filename)
{
    char clean[400], root[400];
    int ret;
    if (save_state == 1) return -1;
    if (go_filename_sanitize(filename, clean, sizeof(clean)) < 0) {
        return -1;
    }
    if (strncmp(pending_video.url, "yt:", 3) == 0) {
        if (go_modern_resolve(pending_video.url, save_url, sizeof(save_url)) < 0)
            return -1;
    } else if (go_source_is_onsen()) {
        if (go_onsen_resolve(pending_video.url, save_url,
                             sizeof(save_url)) < 0) return -1;
    } else return -1;
    strncpy(save_thumb, pending_video.thumb, sizeof(save_thumb) - 1);
    save_thumb[sizeof(save_thumb) - 1] = 0;
    if (strncmp(g_favorites, "ms0:", 4) == 0)
        snprintf(root, sizeof(root), "%s", g_favorites);
    else
        snprintf(root, sizeof(root), "ms0:%s",
                 g_favorites[0] ? g_favorites : "/");
    sceIoMkdir(root, 0777);
    if (strlen(root) + (root[strlen(root)-1] == '/' ? 0 : 1) +
        strlen(clean) + 1 > sizeof(save_path)) return -1;
    strcpy(save_path, root);
    if (save_path[strlen(save_path) - 1] != '/') strcat(save_path, "/");
    strcat(save_path, clean);
    if (go_sidecar_path(save_path, ".THM", save_thumb_path,
                        sizeof(save_thumb_path)) < 0) return -1;
    if (save_thread >= 0) {
        sceKernelWaitThreadEnd(save_thread, NULL);
        sceKernelDeleteThread(save_thread);
        save_thread = -1;
    }
    save_cancel = 0;
    save_progress = 0;
    go_thumbnails_suspend(1);
    save_thread = sceKernelCreateThread("GoTube.save", save_worker, 0x25,
                                        0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (save_thread < 0) { go_thumbnails_suspend(0); return save_thread; }
    save_state = 1;
    ret = sceKernelStartThread(save_thread, 0, NULL);
    if (ret < 0) {
        sceKernelDeleteThread(save_thread);
        save_thread = -1;
        save_state = -1;
        go_thumbnails_suspend(0);
    }
    return ret;
}

int go_save_state(void) { return save_state; }
int go_save_progress(void) { return save_progress; }
