/*
 * GoTube — common header
 * Includes both PSPSDK and SpiderMonkey APIs used across engine modules.
 */
#ifndef GOTUBE_H
#define GOTUBE_H

/* PSPSDK */
#include <pspkernel.h>
#include <pspkerneltypes.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdebug.h>
#include <pspwlan.h>
#include <psphprm.h>
#include <psputility.h>

/* SpiderMonkey 1.7.0 */
#include <jsapi.h>
#include <intraFont.h>

/* Standard C */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* --- bridge --- */
int   register_js_natives(JSContext *cx);
void  go_sync_config(JSContext *cx);
extern char g_favorites[256];
extern int g_video_out_mode;
extern int g_screen_zoom;
void  go_callgate_sitelist(JSContext *cx);
int   go_callgate_search(JSContext *cx, const char *site,
                         const char *keyword, int page);
int   go_callgate_video_url(JSContext *cx, const char *url_expr,
                            char *out, int outsz);
int   go_http_download(const char *url, const char *path,
                       volatile int *progress, volatile int *cancel);
char *go_curl_post_json(const char *url, const char *json,
                        const char *visitor, int *size);
void *go_curl_stream_open(const char *url);
int go_curl_stream_read(void *stream, unsigned char *buffer, int size);
void go_curl_stream_close(void *stream);
int go_curl_download(const char *url, const char *path,
                     volatile int *progress, volatile int *cancel);
void go_modern_trace(const char *format, ...);
int go_modern_clock_valid(void);
void *go_http_stream_open(const char *url);
int go_http_stream_read(void *stream, unsigned char *buffer, int size);
void go_http_stream_close(void *stream);
void go_message_dialog(const char *message);
int go_utility_button_swap(void);
int ensure_network(void);

/* --- GUI state --- */
#define MAX_SITES 64
extern char g_site_names[MAX_SITES][1024];
extern char g_site_search_desc[MAX_SITES][128];
extern int  g_site_count;
extern int  g_site_sel;

/* --- Search results --- */
#define MAX_RESULTS 50
typedef struct {
    char desc[256];
    char uploader[100];
    int  views;
    int  favs;
    float rating;
    char title[160];
    char url[512];
    char thumb[512];
    int  length;
    int  rating_count;
    int  comments;
    int  attr;
    char tags[200];
    char save_filename[200];
    int  local_kind; /* 0 remote, 1 local file, 2 local directory */
} GTVideo;
typedef struct { unsigned short ucs2, cp932; } GTCP932Pair;
extern GTVideo g_results[MAX_RESULTS];
extern int     g_result_count;
extern int     g_result_sel;
extern char    g_search_keyword[128];
extern int     g_result_total;
extern int     g_result_start;
extern int     g_result_end;

/* --- screen state --- */
typedef enum {
    SCR_SITELIST = 0,
    SCR_OSK,
    SCR_SEARCHING,
    SCR_RESULTS,
    SCR_PLAYER,
    SCR_MENU,
} GTScreen;
extern GTScreen g_screen;
extern GTScreen g_menu_return_screen;
extern int g_menu_sel;
extern int g_menu_count;
extern const char *g_menu_labels[16];
extern int g_menu_actions[16];
extern int g_menu_y[16];
enum {
    GT_MENU_SITE = 0x6f, GT_MENU_OPEN_URL = 0x6e,
    GT_MENU_NET_STATUS = 0x6b, GT_MENU_ADD_PLAYLIST = 0x68,
    GT_MENU_REMOVE_PLAYLIST = 0x69, GT_MENU_PLAY_PLAYLIST = 0x6a,
    GT_MENU_TAG_SEARCH = 0, GT_MENU_SAVE = 0x65,
    GT_MENU_RENAME = 0x66, GT_MENU_DELETE = 0x67,
    GT_MENU_TAG_LABEL = 0x70
};
void go_menu_build(void);
extern int g_menu_phase; /* 0 closed, 1 opening, 2 active, 3 closing */
extern GTScreen g_menu_close_target;
void go_menu_close(GTScreen target);
int go_playlist_add(const GTVideo *video);
int go_playlist_remove(const GTVideo *video);
int go_playlist_first(GTVideo *video);
int go_playlist_step(int direction, GTVideo *video);
int go_playlist_load_page(int page);
int go_source_is_favorites(void);
int go_source_is_playlist(void);
int go_source_is_onsen(void);
int go_source_load(int page);
int go_onsen_search(const char *keyword);
int go_onsen_resolve(const char *url, char *out, int out_size);
int go_modern_is_source(void);
int go_modern_search(const char *keyword, int page);
int go_modern_resolve(const char *url, char *out, int out_size);
int go_source_enter(const GTVideo *video);
int go_source_parent(void);
int go_local_rename(const GTVideo *video, const char *filename);
int go_local_delete(const GTVideo *video);
extern int g_osk_mode; /* 0 search, 1 direct URL, 2 save filename, 3 rename */
extern char g_search_status[128];

/* --- resolved video URL for the player screen --- */
extern char g_video_url[2048];

/* --- shared JS context (owned by main.c, used by input/search) --- */
extern JSContext *g_cx;

/* --- OSK --- */
int  go_osk_open(const char *desc, const char *initial);
int  go_osk_update(void);
void go_osk_get_text(char *out, int outsz);
int  go_osk_is_active(void);

/* --- JS runtime --- */
int   go_evaluate_script(JSContext *cx, const char *path);

/* --- GUI --- */
void  go_gui_init(void);
void  go_gui_render(void);
void  go_gui_set_output(int state);
int   go_gui_width(void);
int   go_gui_height(void);
int   go_gui_origin_x(void);
int   go_gui_origin_y(void);

/* --- subsystem ticks --- */
void  go_input_poll(void);
void  go_search_page(int page);
void  go_audio_tick(void);
void  go_network_tick(void);
int go_video_output_poll(void);
int go_video_output_apply(int state);
extern int g_net_online;

/* --- media player --- */
int go_player_start(const char *url);
int go_player_start_file(const char *path); /* qualification harness only */
void go_player_stop(void);
void go_player_toggle_pause(void);
void go_player_cycle_overlay(void);
void go_player_cycle_render_mode(void);
void go_player_cycle_speed(int direction);
int go_player_overlay_mode(void);
int go_player_render_mode(void);
int go_player_speed_mode(void);
void go_player_set_render_mode(int mode);
int go_player_paused(void);
int go_save_prepare(const GTVideo *video, char *initial, int initial_size);
int go_save_start(JSContext *cx, const char *filename);
int go_save_state(void);
int go_save_progress(void);
int go_filename_sanitize(const char *utf8, char *out, int out_size);
int go_sidecar_path(const char *path, const char *extension,
                    char *out, int out_size);
int go_player_state(void); /* 0 idle, 1 download, 2 playing, 3 done, <0 error */
int go_player_progress(void);
int go_player_planes(const unsigned char **y, const unsigned char **v,
                     const unsigned char **u, int *width, int *height,
                     int *y_stride, int *uv_stride);
int go_player_time_cs(void);
int go_player_duration_cs(void);
void go_player_set_source_url(const char *url);
int go_player_matches_source(const char *url);
typedef struct {
    int vpos;
    unsigned int color;
    short position; /* 0 scrolling, 1 bottom, 2 top */
    short size;     /* 0 medium, 1 small, 2 big */
    char text[192];
} GTComment;
int go_comments_load_for_media(const char *path);
int go_comments_count(void);
const GTComment *go_comments_get(int index);
void go_thumbnails_init(void);
void go_thumbnails_reset(void);
void go_thumbnails_suspend(int suspend);
const unsigned short *go_thumbnail_get(int index, int *width, int *height,
                                       int *texture_width, int *texture_height,
                                       int *stride);
void  go_callback_process(void);

/* --- native implementations --- */
JSBool go_getcontents(JSContext *cx, JSObject *obj, uintN argc,
                      jsval *argv, jsval *rval);
JSBool go_postcontents(JSContext *cx, JSObject *obj, uintN argc,
                       jsval *argv, jsval *rval);
JSBool go_alert(JSContext *cx, JSObject *obj, uintN argc,
                jsval *argv, jsval *rval);
JSBool go_encodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval);
JSBool go_decodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval);
JSBool go_decodehtml(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval);
JSBool go_sjistoutf8(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval);
JSBool go_log(JSContext *cx, JSObject *obj, uintN argc,
              jsval *argv, jsval *rval);

/* --- callbacks --- */
int  setup_callbacks(void);

/* --- boot --- */
int  module_start(SceSize args, void *argp);
int  user_main(SceSize args, void *argp);

#endif /* GOTUBE_H */

/* --- WiFi configuration dialog (sceUtilityNetconf) --- */

/* --- Screen states --- */

/* --- WiFi configuration dialog (sceUtilityNetconf, lazy — triggered by X button) --- */

/* --- Boot splash screen (evidence: GT12 splash renderer VA 0x27b70) --- */
void  go_splash_init(void);
void  go_splash_skip(void);
void  go_splash_render(void);
int   go_splash_is_active(void);
extern int g_network_requested;
extern intraFont *g_font;
extern int ensure_network(void);
void  go_callback_tick(void);
/* --- WiFi config dialog --- */
int  go_netconf_open(void);
int  go_netconf_open_status(void);
int  go_netconf_update(void);
int  go_netconf_is_active(void);
extern int g_netconf_done;
extern int g_netconf_connected;
