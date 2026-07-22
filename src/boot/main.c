/*
 * GoTube — boot entry point
 * Behavioral reconstruction of user_main VA 0x21538; subsystem-specific
 * claims are tracked in the GT12 evidence ledger.
 *
 * Boot chain:
 *   module_start → user_main thread
 *     1. Overclock PSP to 333/333/166 MHz
 *     2. Init display + controller
 *     3. Install the fixed native provider registry
 *     4. GUI init → MAIN LOOP
 */
#include "gotube.h"

/* --- PSPSDK module identity --- */
PSP_MODULE_INFO("GoTube", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* A 4 MiB heap starves H.264, while the previous "all free memory minus
 * 4 MiB" policy can leave a real PSP without a sufficiently useful free
 * partition for the 0x40000 player worker and its child threads.  Bound the
 * reconstruction heap instead: the supplied 640x480 Main-profile stream fits
 * with room for FFmpeg/FAAD, and the PSP-2000 retains its remaining partition
 * for PRXs and the original worker-stack contract. */
PSP_HEAP_SIZE_KB(16384);

/*
 * user_main — main GoTube thread
 */
int user_main(SceSize args, void *argp)
{
    /* 1. Overclock to max */
    scePowerSetClockFrequency(333, 333, 166);

    /* 2. Hardware init */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* Native configuration replaces cfg.js. */
    strcpy(g_favorites, "/");
    g_video_out_mode = 1;
    g_screen_zoom = 1;
    go_player_set_render_mode(g_screen_zoom);

    /* A NULL context installs only Favorites, Playlist, Onsen and YouTube. */
    go_native_registry_init();

    /* FUN_0001e05c selects native descriptor zero (Favorites) before the
     * first main-state descriptor is installed.  Its literal "PSP" query is
     * ignored by the filesystem source. */
    g_site_sel = 0;
    strcpy(g_search_keyword, "PSP");
    go_source_load(1);
    g_screen = SCR_RESULTS;

    /* 8. GUI init */
    go_gui_init();
    go_splash_init();  /* boot splash (firmware, hardware detection, credit) */
    setup_callbacks();


    /* 9. MAIN LOOP */
    for (;;) {
        static int output_state = 0;
        int detected_output = go_video_output_poll();
        if (detected_output >= 0 && detected_output != output_state) {
            go_gui_set_output(detected_output);
            output_state = detected_output;
        }
        go_input_poll();
        go_callback_process();
        /* After splash finishes: show network config dialog (PSP SSID selector) */
        if (g_network_requested) {
            g_network_requested = 0;
            /* FUN_000278e0 sends WLAN-off directly to the main descriptor;
             * only state 1 enters the connection utility. */
            if (sceWlanGetSwitchState() != 0) {
                if (ensure_network() >= 0)
                    (void)go_netconf_open();
            }
        }
        go_gui_render();
        go_callback_tick();
        go_audio_tick();
        go_network_tick();
    }

    return 0;
}

/*
 * main — entry point (standard PSPSDK crt0).
 * Matches the decompiled module_start at 0x00021484: spawns user_main
 * thread (128KB stack for the JS engine) and waits for it.
 */
int main(int argc, char *argv[])
{
    SceUID thid;

    thid = sceKernelCreateThread("user_main", user_main, 0x22, 0x20000, 0, NULL);
    if (thid < 0) return thid;

    sceKernelStartThread(thid, 0, NULL);
    sceKernelWaitThreadEnd(thid, NULL);

    sceKernelExitGame();
    return 0;
}
