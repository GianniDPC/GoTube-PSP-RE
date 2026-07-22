/*
 * GoTube — Boot splash screen
 * Behavioral reconstruction of VA 0x27b70 (GT12-SPLASH-0001).
 * Shows firmware version, hardware detection messages, and intraFont credit
 * for ~30 frames before transitioning to the main app.
 */
#include "gotube.h"
#include <pspdisplay.h>
#include <psppower.h>
#include <pspkernel.h>
#include <psphprm.h>
#include <pspsdk.h>
#include <kubridge.h>
#include <psputility_netmodules.h>

static int  g_splash_frames = 30;   /* GT12-SPLASH-0001: immediate 0x1e */
static int  g_splash_active = 1;
static int  g_splash_error = 0;
static char g_splash_msg[1024] = ""; /* original allocates 0x400 bytes */

/* Hardware/status branches recovered under GT12-SPLASH-0001. */
static void splash_detect_hardware(void)
{
    char buf[128];
    int fw = sceKernelDevkitVersion();

    if (fw < 0x02000000 ||
        (sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON) >= 0 &&
         sceUtilityLoadNetModule(PSP_NET_MODULE_INET) >= 0)) {
        /* These are the first two branches in FUN_00027b70. Network module
         * failure is intentionally handled by the state transition later. */
    }

    if (fw > 0x0307010f && kuKernelGetModel() == 1) {
        int module;
        strcat(g_splash_msg, "PSP-2000 detected\n");
        module = pspSdkLoadStartModule("ms0:/PSP/GAME/GoTube/dvemgr.prx",
                                       PSP_MEMORY_PARTITION_KERNEL);
        if (module < 0) {
            strcat(g_splash_msg, "try to load dvemgr.prx for video out.\n");
            snprintf(buf, sizeof(buf),
                     "error:ms0:/PSP/GAME/GoTube/dvemgr.prx pspSdkLoadStartModule 0x%08X\n",
                     module);
            strncat(g_splash_msg, buf, sizeof(g_splash_msg) - strlen(g_splash_msg) - 1);
            /* Original enters state 2 and leaves the diagnostic on screen. */
            g_splash_error = 1;
        } else {
            int cable = go_video_output_poll();
            if (cable > 0) {
                snprintf(buf, sizeof(buf), "video out cable (%d) detected.\n", cable);
                strncat(g_splash_msg, buf,
                        sizeof(g_splash_msg) - strlen(g_splash_msg) - 1);
            }
        }
    }
    if (sceHprmIsRemoteExist())
        strcat(g_splash_msg, "Remote detected\n");
    strcat(g_splash_msg, "Favorites Folder:");
    strncat(g_splash_msg, g_favorites,
            sizeof(g_splash_msg) - strlen(g_splash_msg) - 2);
    {
        char path[512];
        SceUID dir;
        if (strncmp(g_favorites, "ms0:", 4) == 0)
            snprintf(path, sizeof(path), "%s", g_favorites);
        else
            snprintf(path, sizeof(path), "ms0:%s",
                     g_favorites[0] ? g_favorites : "/");
        dir = sceIoDopen(path);
        if (dir >= 0) sceIoDclose(dir);
        else {
            strncat(g_splash_msg, "\nnot exist.",
                    sizeof(g_splash_msg) - strlen(g_splash_msg) - 1);
            g_splash_error = 1;
        }
    }
    strcat(g_splash_msg, "\n");
}

/* Initialize splash screen — call before main loop */
void go_splash_init(void)
{
    g_splash_frames = 30;
    g_splash_active = 1;
    g_splash_error = 0;
    g_splash_msg[0] = 0;
    splash_detect_hardware();
}

/* Render the splash screen. Should be called from go_gui_render
 * when go_splash_is_active() returns true. */
void go_splash_render(void)
{
    if (!g_splash_active)
        return;

    if (g_font) {
        /* Literal recovered at VA 0x501258. */
        intraFontSetStyle(g_font, 0.8f, 0xFFFFFFFF, 0xFF000000, 0);
        int ox = go_gui_origin_x(), oy = go_gui_origin_y();
        intraFontPrint(g_font, ox, oy + 16, "GoTube 1.2 091002 thank to Sofiya\x94L  ");
        intraFontSetStyle(g_font, 0.5f, 0xFFFFFFFF, 0xFF000000, 0);
        {
            char firmware[64];
            snprintf(firmware, sizeof(firmware), "firmware version:%08x\n", sceKernelDevkitVersion());
            intraFontPrint(g_font, ox, oy + 32, firmware);
        }
        intraFontPrint(g_font, ox, oy + 48, g_splash_msg);

        intraFontSetStyle(g_font, 0.8f, 0xFFFFFFFF, 0xFF000000,
                          INTRAFONT_ALIGN_RIGHT);
        /* VA 0x27b70 uses DAT_005eed68/-6c: LCD 480x272,
         * component 704x464, composite 680x464. */
        intraFontPrint(g_font,
                       go_gui_width() - go_gui_origin_x() - 10,
                       go_gui_height() - (go_gui_origin_y() ? 16 : 0) - 2,
                       "Uses intraFont by BenHur");
    }

    /* Decrement frame counter, transition when done */
    if (g_splash_error) return;
    g_splash_frames--;
    if (g_splash_frames <= 0) {
        g_splash_active = 0;
        /* Transition into the network bootstrap state after the timed splash. */
        g_network_requested = 1;  /* flag for main loop to init network */
    }
}

int go_splash_is_active(void) { return g_splash_active; }
void go_splash_skip(void) { g_splash_active = 0; }

/* Flag: network should be initialized after splash (set by splash_render when done) */
int g_network_requested = 0;
