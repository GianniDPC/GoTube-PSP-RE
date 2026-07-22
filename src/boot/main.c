/*
 * GoTube — boot entry point
 * Behavioral reconstruction of user_main VA 0x21538; subsystem-specific
 * claims are tracked in the GT12 evidence ledger.
 *
 * Boot chain:
 *   module_start → user_main thread
 *     1. Overclock PSP to 333/333/166 MHz
 *     2. Init display + controller
 *     3. Allocate 512KB JSRuntime + JSContext
 *     4. Register JS natives (GetContents, PostContents, PSPTube class)
 *     5. Evaluate cfg.js from Memory Stick
 *     6. Evaluate site.js from Memory Stick
 *     7. CallGate_GetSiteList()
 *     8. GUI init → MAIN LOOP
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

/* --- Global JS runtime references (g_cx shared for search) --- */
static JSRuntime *g_rt = NULL;
JSContext *g_cx = NULL;

/* --- Global object class (required before any JS evaluation) --- */
static JSClass global_class = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub,
    JS_PropertyStub, JS_PropertyStub,
    JS_EnumerateStub, JS_ResolveStub,
    JS_ConvertStub,  JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

/* --- JS error reporter: routes errors/exceptions to the trace log --- */
static void go_error_reporter(JSContext *cx, const char *message,
                              JSErrorReport *report)
{
    (void)cx;
    if (message) gt_trace(message);
    if (report) {
        if (report->filename) gt_trace(report->filename);
        if (report->lineno)   gt_trace_ptr("line", (void *)(long)report->lineno);
    }
}

/* --- Paths on Memory Stick --- */
#define APP_ROOT   "ms0:/PSP/GAME/GoTube"
#define SITE_DIR   "ms0:/PSP/GAME/GoTube/site"
#define CFG_FILE   "ms0:/PSP/GAME/GoTube/cfg.js"
#define SITE_FILE  "ms0:/PSP/GAME/GoTube/site.js"

/* --- Directory scan helpers --- */
static char *join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    char  *buf  = (char *)malloc(dlen + 1 + nlen + 1);
    if (!buf) return NULL;
    memcpy(buf, dir, dlen);
    buf[dlen] = '/';
    memcpy(buf + dlen + 1, name, nlen + 1);
    return buf;
}

static int js_cmp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/*
 * user_main — main GoTube thread
 */
int user_main(SceSize args, void *argp)
{
    int       dir_fd;
    SceIoDirent entry;
    char     *js_files[256];
    int       js_count = 0;
    int       i, ret;

    gt_trace("user_main start");

    /* 1. Overclock to max */
    scePowerSetClockFrequency(333, 333, 166);
    gt_trace("power ok");

    /* 2. Hardware init */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    gt_trace("ctrl ok");

    /* 3. JS runtime: 512KB (0x80000 matches decompiled) */
    g_rt = JS_NewRuntime(0x80000);
    gt_trace_ptr("runtime", g_rt);
    if (!g_rt) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("JavaScript Init error.\n");
        sceKernelExitGame();
        return 1;
    }

    g_cx = JS_NewContext(g_rt, 0x2000);
    gt_trace_ptr("context", g_cx);
    if (!g_cx) {
        JS_DestroyRuntime(g_rt);
        sceKernelExitGame();
        return 1;
    }
    JS_SetErrorReporter(g_cx, go_error_reporter);

    /* 3b. Create global object + standard classes (Object, Array, String...)
     * Required before any JS_DefineFunction / JS evaluation.            */
    {
        JSObject *glob = JS_NewObject(g_cx, &global_class, NULL, NULL);
        if (!glob) {
            JS_DestroyContext(g_cx);
            JS_DestroyRuntime(g_rt);
            sceKernelExitGame();
            return 1;
        }
        JS_SetGlobalObject(g_cx, glob);
        {
            JSBool sok = JS_InitStandardClasses(g_cx, glob);
            gt_trace_ptr("stdclasses", (void *)(long)sok);
            if (!sok) {
                jsval exc;
                if (JS_GetPendingException(g_cx, &exc)) {
                    JSString *es = JS_ValueToString(g_cx, exc);
                    if (es) gt_trace(JS_GetStringBytes(es));
                    JS_ClearPendingException(g_cx);
                } else {
                    gt_trace("(stdclass fail, no exc)");
                }
            }
        }
        gt_trace("global obj done");
    }

    /* 4. Register JS natives */
    ret = register_js_natives(g_cx);
    gt_trace("natives done");
    if (ret < 0) {
        JS_DestroyContext(g_cx);
        JS_DestroyRuntime(g_rt);
        sceKernelExitGame();
        return 1;
    }
    /* 5. Scan site/ directory for .js site plugins (skip cfg.js, site.js) */
    gt_trace("opening site dir");
    dir_fd = sceIoDopen(SITE_DIR);
    if (dir_fd < 0) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("ERROR: %s not found.\n", SITE_DIR);
        JS_DestroyContext(g_cx);
        JS_DestroyRuntime(g_rt);
        sceKernelExitGame();
        return 1;
    }

    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(dir_fd, &entry) > 0) {
        size_t len = strlen(entry.d_name);
        char   e0, e1, e2;
        gt_trace(entry.d_name);
        if ((entry.d_stat.st_attr & 0x1000)) { memset(&entry,0,sizeof(entry)); continue; }
        if (len < 3) { memset(&entry,0,sizeof(entry)); continue; }

        /* Case-insensitive ".js" check — FAT returns short 8.3 names
         * uppercased (e.g. "ext.js" → "EXT.JS").                          */
        e0 = entry.d_name[len-3];
        e1 = entry.d_name[len-2] | 0x20;   /* to lowercase */
        e2 = entry.d_name[len-1] | 0x20;
        if (e0 != '.' || e1 != 'j' || e2 != 's') { memset(&entry,0,sizeof(entry)); continue; }

        if (strcmp(entry.d_name, "cfg.js")  == 0) { memset(&entry,0,sizeof(entry)); continue; }
        if (strcmp(entry.d_name, "site.js") == 0) { memset(&entry,0,sizeof(entry)); continue; }

        if (js_count < 256) {
            js_files[js_count] = join_path(SITE_DIR, entry.d_name);
            if (js_files[js_count])
                js_count++;
        }
        memset(&entry, 0, sizeof(entry));   /* re-zero for next read */
    }
    sceIoDclose(dir_fd);
    gt_trace("scan done");
    gt_trace_ptr("js_count", (void *)(long)js_count);

    /* Sort alphabetically (matches binary qsort) */
    if (js_count > 1)
        qsort(js_files, js_count, sizeof(char *), js_cmp);
    gt_trace("qsort done");

    /* 6a. Evaluate cfg.js (always first) */
    gt_trace("eval cfg.js");
    ret = go_evaluate_script(g_cx, CFG_FILE);
    gt_trace_ptr("cfg ret", (void *)(long)ret);
    gt_trace("cfg done");
    if (ret < 0) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("JavaScript Load error %s\n", CFG_FILE);
        sceKernelExitGame();
        return 1;
    }
    go_sync_config(g_cx);

    /* 6b. Evaluate each site plugin */
    for (i = 0; i < js_count; i++) {
        gt_trace(js_files[i]);
        ret = go_evaluate_script(g_cx, js_files[i]);
        if (ret < 0) {
            gt_trace("plugin FAILED");
            pspDebugScreenInit();
            pspDebugScreenPrintf("JavaScript Load error %s\n", js_files[i]);
            sceKernelExitGame();
            return 1;
        }
        free(js_files[i]);
    }
    gt_trace("plugins done");

    /* 6c. Evaluate site.js (always last) */
    gt_trace("eval site.js");
    ret = go_evaluate_script(g_cx, SITE_FILE);
    gt_trace("site done");
    if (ret < 0) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("JavaScript Load error %s\n", SITE_FILE);
        sceKernelExitGame();
        return 1;
    }

    /* 7. CallGate_GetSiteList */
    gt_trace("callgate");
    go_callgate_sitelist(g_cx);
    gt_trace("callgate done");

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

#ifdef GT_PLAYER_SELFTEST
    /* Test-only build: bypass dead historical services and exercise the
     * PSP decoder/render/audio path against a deterministic local fixture. */
    go_player_start_file("ms0:/PSP/GAME/GoTube/player-selftest.flv");
    go_splash_skip();
    g_screen = SCR_PLAYER;
    gt_trace_ptr("comment count", (void *)(long)go_comments_count());
#endif
#ifdef GT_STREAM_SELFTEST
    go_player_start("http://127.0.0.1:18080/player-selftest.flv");
    go_splash_skip();
    g_screen = SCR_PLAYER;
#endif
#ifdef GT_SAVE_SELFTEST
    {
        GTVideo fixture;
        char initial[64];
        memset(&fixture, 0, sizeof(fixture));
        strcpy(fixture.url, "\"http://127.0.0.1:18080/player-selftest.flv\"");
        strcpy(fixture.thumb, "http://127.0.0.1:18080/thumbnail-selftest.jpg");
        strcpy(fixture.save_filename, "保存:試験.flv");
        if (go_save_prepare(&fixture, initial, sizeof(initial)) == 0)
            gt_trace_ptr("save selftest start", (void *)(long)go_save_start(g_cx, initial));
        go_splash_skip();
        g_screen = SCR_RESULTS;
    }
#endif
#ifdef GT_LOCAL_SELFTEST
    {
        int site, item;
        for (site = 0; site < g_site_count; site++)
            if (strcmp(g_site_names[site], "Favorites") == 0) break;
        if (site < g_site_count) {
            g_site_sel = site;
            gt_trace_ptr("favorites load", (void *)(long)go_source_load(1));
            for (item = 0; item < g_result_count; item++)
                if (g_results[item].local_kind == 2 &&
                    strcmp(g_results[item].title, "PSP") == 0) break;
            if (item < g_result_count) {
                gt_trace_ptr("favorites enter dir", (void *)(long)
                    go_source_enter(&g_results[item]));
                gt_trace_ptr("favorites parent", (void *)(long)
                    go_source_parent());
            }
            for (item = 0; item < g_result_count; item++)
                if (strcmp(g_results[item].title, "favorite-selftest.flv") == 0) break;
            if (item < g_result_count) {
                gt_trace_ptr("favorites rename", (void *)(long)
                    go_local_rename(&g_results[item], "favorite-renamed.flv"));
                for (item = 0; item < g_result_count; item++)
                    if (strcmp(g_results[item].title, "favorite-renamed.flv") == 0) break;
                if (item < g_result_count)
                    gt_trace_ptr("favorites delete", (void *)(long)
                        go_local_delete(&g_results[item]));
            }
        }
        go_splash_skip();
        g_screen = SCR_RESULTS;
    }
#endif
#ifdef GT_CONTROL_SELFTEST
    {
        int i;
        GTVideo a, b, stepped;
        memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        strcpy(a.url, "control-a"); strcpy(b.url, "control-b");
        go_playlist_add(&a); go_playlist_add(&b); go_playlist_first(&stepped);
        go_playlist_step(1, &stepped);
        gt_trace(stepped.url);
        go_playlist_step(1, &stepped);
        gt_trace(stepped.url);
        for (i = 0; i < 5; i++) go_player_cycle_overlay();
        gt_trace_ptr("overlay cycle", (void *)(long)go_player_overlay_mode());
        for (i = 0; i < 15; i++) go_player_cycle_render_mode();
        gt_trace_ptr("render cycle", (void *)(long)go_player_render_mode());
        memset(&g_results[0], 0, sizeof(g_results[0]));
        strcpy(g_results[0].tags, "alpha  beta\tgamma");
        g_results[0].attr = 1;
        g_result_count = 1; g_result_sel = 0;
        go_menu_build();
        gt_trace_ptr("menu count", (void *)(long)g_menu_count);
        for (i = 0; i < g_menu_count; i++) {
            gt_trace_ptr(g_menu_labels[i], (void *)(long)g_menu_actions[i]);
        }
        go_splash_skip();
        g_screen = SCR_RESULTS;
    }
#endif
#ifdef GT_ASHX_SELFTEST
    {
        int site, item;
        for (site = 0; site < g_site_count; site++)
            if (strcmp(g_site_names[site], "Favorites") == 0) break;
        if (site < g_site_count) {
            g_site_sel = site; go_source_load(1);
            for (item = 0; item < g_result_count; item++)
                if (g_results[item].local_kind == 3) break;
            if (item < g_result_count) {
                gt_trace(g_results[item].title);
                gt_trace_ptr("ashx children", (void *)(long)go_source_enter(&g_results[item]));
                if (g_result_count > 1) {
                    gt_trace(g_results[0].title); gt_trace(g_results[0].url);
                    gt_trace(g_results[1].title); gt_trace(g_results[1].url);
                }
            }
        }
        go_splash_skip(); g_screen = SCR_RESULTS;
    }
#endif
#ifdef GT_TV_SELFTEST
    go_gui_set_output(1);
    gt_trace_ptr("tv component width", (void *)(long)go_gui_width());
    gt_trace_ptr("tv component height", (void *)(long)go_gui_height());
    go_gui_set_output(2);
    gt_trace_ptr("tv composite x", (void *)(long)go_gui_origin_x());
    go_gui_set_output(0);
    gt_trace_ptr("tv lcd width", (void *)(long)go_gui_width());
    go_splash_skip(); g_screen = SCR_RESULTS;
#endif
#ifdef GT_CALLGATE_SELFTEST
    {
        int count = go_callgate_search(g_cx, "Fixture", "query", 2);
        gt_trace_ptr("callgate count", (void *)(long)count);
        gt_trace_ptr("callgate total", (void *)(long)g_result_total);
        gt_trace_ptr("callgate start", (void *)(long)g_result_start);
        gt_trace_ptr("callgate end", (void *)(long)g_result_end);
        if (count > 0) {
            gt_trace(g_results[0].title);
            gt_trace_ptr("callgate length", (void *)(long)g_results[0].length);
            gt_trace_ptr("callgate views", (void *)(long)g_results[0].views);
            gt_trace_ptr("callgate comments", (void *)(long)g_results[0].comments);
        }
        go_splash_skip(); g_screen = SCR_RESULTS;
    }
#endif

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
                if (ensure_network() < 0 || go_netconf_open() < 0)
                    gt_trace("network configuration init error");
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
