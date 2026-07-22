/*
 * GoTube — On-Screen Keyboard wrapper
 * Wraps sceUtility OSK for entering the search keyword.
 * The OSK renders via the PSP system (works on PPSSPP + hardware).
 */
#include "gotube.h"
#include <psputility.h>
#include <psputility_osk.h>
#include <libccc.h>

/* OSK operating states returned by go_osk_update() */
#define OSK_STATE_IDLE      0
#define OSK_STATE_RUNNING   1
#define OSK_STATE_DONE      2
#define OSK_STATE_CANCELLED 3

static SceUtilityOskParams g_osk_params;
static SceUtilityOskData   g_osk_data;
static unsigned short      g_osk_intext[128];
static unsigned short      g_osk_outtext[128];
static unsigned short      g_osk_desc[64];
static int                 g_osk_active = 0;

int go_utility_button_swap(void)
{
    int value, language;
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP,&value)==0)
        return value != 0;
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,&language)!=0)
        return 1;
    return (language==0 || language==9 || language==10 || language==11) ? 0 : 1;
}

/* ASCII/UTF-8 → UTF-16 helper */
static void to_utf16(const char *src, unsigned short *dst, int max)
{
    int count = cccUTF8toUCS2((cccUCS2 *)dst, max, (const cccCode *)src);
    if (count < 0) count = 0;
    if (count >= max) count = max - 1;
    dst[count] = 0;
}

/* UTF-16 → ASCII helper */
static void from_utf16(const unsigned short *src, char *dst, int max)
{
    int i, used = 0;
    for (i = 0; src[i] && used < max - 1; i++) {
        unsigned int code = src[i];
        if (code < 0x80) dst[used++] = code;
        else if (code < 0x800 && used + 2 < max) {
            dst[used++] = 0xc0 | (code >> 6);
            dst[used++] = 0x80 | (code & 0x3f);
        } else if (used + 3 < max) {
            dst[used++] = 0xe0 | (code >> 12);
            dst[used++] = 0x80 | ((code >> 6) & 0x3f);
            dst[used++] = 0x80 | (code & 0x3f);
        }
    }
    dst[used] = 0;
}

/*
 * Open the OSK. desc = prompt text, initial = initial text buffer.
 */
int go_osk_open(const char *desc, const char *initial)
{
    if (g_osk_active)
        return -1;

    to_utf16(desc, g_osk_desc, 64);
    to_utf16(initial, g_osk_intext, 128);
    g_osk_outtext[0] = 0;

    memset(&g_osk_data, 0, sizeof(g_osk_data));
    g_osk_data.language       = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
    g_osk_data.inputtype      = PSP_UTILITY_OSK_INPUTTYPE_ALL;
    g_osk_data.lines          = 1;
    g_osk_data.desc           = g_osk_desc;
    g_osk_data.intext         = g_osk_intext;
    g_osk_data.outtextlength  = 128;
    g_osk_data.outtext        = g_osk_outtext;
    g_osk_data.outtextlimit   = 127;

    memset(&g_osk_params, 0, sizeof(g_osk_params));
    g_osk_params.base.size        = sizeof(g_osk_params);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,
                                &g_osk_params.base.language);
    g_osk_params.base.buttonSwap  = go_utility_button_swap();
    g_osk_params.base.result      = 0;
    g_osk_params.base.graphicsThread = 0x11;
    g_osk_params.base.accessThread   = 0x20;
    g_osk_params.base.fontThread     = 0x12;
    g_osk_params.base.soundThread    = 0x10;
    g_osk_params.datacount      = 1;
    g_osk_params.data           = &g_osk_data;

    {
        int ret = sceUtilityOskInitStart(&g_osk_params);
        gt_trace_ptr("osk_init_ret", (void *)(long)ret);
        if (ret < 0)
            return -1;
    }

    g_osk_active = 1;
    return 0;
}

/*
 * Per-frame update. Returns OSK_STATE_*.
 */
int go_osk_update(void)
{
    int status;

    if (!g_osk_active)
        return OSK_STATE_IDLE;

    sceUtilityOskUpdate(1);
    status = sceUtilityOskGetStatus();

    /* Trace the OSK dialog status so the user can report what happens
     * on real hardware: 0=NONE, 1=INIT, 2=VISIBLE, 3=QUIT, 4=FINISHED */
    {
        static int last_status = -1;
        if (status != last_status) {
            gt_trace_ptr("osk_status", (void *)(long)status);
            last_status = status;
        }
    }

    if (status == PSP_UTILITY_DIALOG_QUIT) {
        /* User confirmed — OSK is transitioning out. Start shutdown,
         * keep polling (return RUNNING) so next frame sees FINISHED. */
        sceUtilityOskShutdownStart();
    }

    if (status == PSP_UTILITY_DIALOG_FINISHED) {
        /* Read result */
        if (g_osk_data.result == PSP_UTILITY_OSK_RESULT_CHANGED) {
            sceUtilityOskShutdownStart();
            g_osk_active = 0;
            return OSK_STATE_DONE;
        } else {
            sceUtilityOskShutdownStart();
            g_osk_active = 0;
            return OSK_STATE_CANCELLED;
        }
    }
    return OSK_STATE_RUNNING;
}

/*
 * Get the entered text (UTF-8). Only valid after OSK_STATE_DONE.
 */
void go_osk_get_text(char *out, int outsz)
{
    from_utf16(g_osk_outtext, out, outsz);
}

int go_osk_is_active(void)
{
    return g_osk_active;
}
