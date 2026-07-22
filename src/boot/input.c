/*
 * GoTube — input polling
 * Reads the PSP controller and drives UI navigation + search trigger.
 */
#include "gotube.h"

static unsigned int g_prev_buttons = 0;
static int g_current_page = 1;
static int g_source_delay = 0;
static int g_network_page = 0;

static int source_requires_network(void)
{
    /* Descriptor +8 tested by FUN_0001da58 is clear only for the native
     * Favorites/Playlist sources.  Onsen and all JS providers require an
     * infrastructure connection. */
    return !go_source_is_favorites() && !go_source_is_playlist();
}

static int start_video(const GTVideo *video)
{
    if (!video) return -1;
    go_player_set_source_url(video->url);
    if (video->local_kind == 1) return go_player_start_file(video->url);
    if (video->local_kind == 4) return go_player_start(video->url);
    if (video->local_kind == 2 || video->local_kind == 3) return -1;
    g_video_url[0] = 0;
    if (go_source_is_onsen()) {
        if (go_onsen_resolve(video->url,g_video_url,sizeof(g_video_url))<0) return -1;
        return go_player_start(g_video_url);
    }
    if (strncmp(video->url, "yt:", 3) == 0) {
        if (go_modern_resolve(video->url, g_video_url,
                              sizeof(g_video_url)) < 0) return -1;
        return go_player_start(g_video_url);
    }
    if (go_callgate_video_url(g_cx, video->url, g_video_url,
                              sizeof(g_video_url)) < 0) return -1;
    return go_player_start(g_video_url);
}

void go_search_page(int page)
{
    if (page < 1 || g_site_count < 1) return;
    g_current_page = page;
    /* FUN_0001da58: a network provider dispatches immediately when APCTL is
     * online; otherwise, with WLAN enabled, it runs the same CONNECTAP
     * utility used by the bootstrap and resumes only after success. */
    if (source_requires_network() && !g_net_online) {
        g_network_page = 0;
        if (sceWlanGetSwitchState() != 0 && ensure_network() >= 0 &&
            go_netconf_open() == 0)
            g_network_page = page;
        g_screen = SCR_RESULTS;
        return;
    }
    g_screen = SCR_SEARCHING;
    go_thumbnails_reset();
    /* FUN_0001da58 dispatches through the active native descriptor callback.
     * Favorites and Playlist therefore never pass through the JS CallGate;
     * doing so emptied the list after Select and made L/R pagination fail. */
    if (go_source_is_favorites() || go_source_is_playlist())
        g_result_count = go_source_load(page);
    else if (go_source_is_onsen())
        g_result_count = go_onsen_search(g_search_keyword);
    else if (go_modern_is_source())
        g_result_count = go_modern_search(g_search_keyword, page);
    else
        g_result_count = go_callgate_search(g_cx, g_site_names[g_site_sel],
                                            g_search_keyword, page);
    g_result_sel = 0;
    g_screen = SCR_RESULTS;
}

void go_input_poll(void)
{
    SceCtrlData   pad;
    unsigned int  pressed, buttons;
    unsigned int  remote = 0;
    static int button_swap = -1;

    /* Splash (descriptor VA 0x5eed9c) and network bootstrap (0x5eed84)
     * precede main-state initialization; result input is unreachable there. */
    if (go_splash_is_active() || go_netconf_is_active())
        return;

    if (g_network_page != 0 && g_netconf_done) {
        int page = g_network_page;
        g_network_page = 0;
        g_netconf_done = 0;
        if (g_netconf_connected) {
            /* The utility has completed the APCTL connection even though the
             * once-per-frame cached flag may not be refreshed until later in
             * this loop. */
            g_net_online = 1;
            go_search_page(page);
        }
    }

    if (g_source_delay > 0 && --g_source_delay == 0)
        go_search_page(1);

    sceCtrlPeekBufferPositive(&pad, 1);
    buttons = pad.Buttons;
    if (button_swap < 0) button_swap = go_utility_button_swap();
    /* FUN_0000a258 normalizes the physical Cross/Circle pair according to
     * FUN_000080a4 before the dispatcher reads logical button masks. */
    if (button_swap) {
        unsigned int pair = buttons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE);
        buttons &= ~(PSP_CTRL_CROSS | PSP_CTRL_CIRCLE);
        if (pair & PSP_CTRL_CROSS) buttons |= PSP_CTRL_CIRCLE;
        if (pair & PSP_CTRL_CIRCLE) buttons |= PSP_CTRL_CROSS;
    }
    pressed = buttons & ~g_prev_buttons;   /* edge-detect */
    g_prev_buttons = buttons;
    if (sceHprmIsRemoteExist()) {
        u32 latch[4] = {0, 0, 0, 0};
        if (sceHprmReadLatch(latch) > 0) remote = latch[2];
    }
    if (remote & PSP_HPRM_PLAYPAUSE)
        pressed |= g_screen == SCR_PLAYER ? PSP_CTRL_START : PSP_CTRL_CIRCLE;
    if (remote & PSP_HPRM_VOL_UP) pressed |= PSP_CTRL_SQUARE;
    if (remote & PSP_HPRM_FORWARD)
        pressed |= g_screen == SCR_PLAYER ? PSP_CTRL_RTRIGGER : PSP_CTRL_DOWN;
    if (remote & PSP_HPRM_BACK)
        pressed |= g_screen == SCR_PLAYER ? PSP_CTRL_LTRIGGER : PSP_CTRL_UP;

    /* Dispatcher VA 0x2097c handles these outside its view-mode branches. */
    if (g_screen != SCR_PLAYER && (pressed & PSP_CTRL_START) &&
        (go_player_state() == 1 || go_player_state() == 2))
        go_player_toggle_pause();
    switch (g_screen) {
    case SCR_SITELIST:
        if (pressed & PSP_CTRL_SELECT) go_player_cycle_render_mode();
        if (pressed & PSP_CTRL_CROSS)
            g_screen = SCR_RESULTS;
        /* Dispatcher mode 1 is the 160-pixel multiview pane.  Square advances
         * it to mode 0 (video with the list off-screen); it does not jump
         * back to the full listing. */
        if (pressed & PSP_CTRL_SQUARE) g_screen = SCR_PLAYER;
        break;

    case SCR_OSK:
        /* OSK handles its own input via sceUtilityOskUpdate (rendered in gui).
         * Transitions are handled in go_gui_render. */
        break;

    case SCR_RESULTS:
        /* List-view Select calls FUN_0001ad8c: advance the descriptor index
         * modulo the complete native+JS registry and launch page one. */
        if ((pressed & PSP_CTRL_SELECT) && g_site_count > 0) {
            g_site_sel = (g_site_sel + 1) % g_site_count;
            g_current_page = 1;
            go_thumbnails_reset();
            g_result_count = g_result_total = 0;
            g_result_start = g_result_end = g_result_sel = 0;
            g_source_delay = 15;
        }
        if (pressed & PSP_CTRL_UP) {
            if (g_result_sel > 0) g_result_sel--;
        }
        if (pressed & PSP_CTRL_DOWN) {
            if (g_result_sel < g_result_count - 1) g_result_sel++;
        }
        if ((pressed & PSP_CTRL_LTRIGGER) && g_result_start > 1)
            go_search_page(g_current_page > 1 ? g_current_page - 1 : 1);
        if ((pressed & PSP_CTRL_RTRIGGER) &&
            g_result_end > 0 && g_result_end < g_result_total)
            go_search_page(g_current_page + 1);
        /* FUN_0002097c uses 0x2000 (Circle) to activate the selected node.
         * GoTube follows the Japanese accept-button convention. */
        if (pressed & PSP_CTRL_CIRCLE) {
            if (g_result_count > 0) {
                if (g_results[g_result_sel].local_kind) {
                    if (g_results[g_result_sel].local_kind == 2 ||
                        g_results[g_result_sel].local_kind == 3) {
                        go_source_enter(&g_results[g_result_sel]);
                        g_current_page = 1;
                    } else {
                        go_thumbnails_suspend(1);
                        if (start_video(&g_results[g_result_sel]) == 0)
                            g_screen = SCR_PLAYER;
                        else go_thumbnails_suspend(0);
                    }
                } else {
                    go_thumbnails_suspend(1);
                    if (start_video(&g_results[g_result_sel]) == 0)
                        g_screen = SCR_PLAYER;
                    else go_thumbnails_suspend(0);
                }
            }
        }
        if (pressed & PSP_CTRL_CROSS) {
            /* Descriptor field +4 is null for Favorites/Playlist; the
             * original opens search OSK only when SearchDesc is present. */
            if (g_site_search_desc[g_site_sel][0]) {
                g_osk_mode = 0;
                if (go_osk_open(g_site_search_desc[g_site_sel],
                                g_search_keyword) == 0)
                    g_screen = SCR_OSK;
            }
        }
        if (pressed & PSP_CTRL_TRIANGLE) {
            g_menu_return_screen = SCR_RESULTS;
            g_menu_sel = 0;
            go_menu_build();
            g_screen = SCR_MENU;
        }
        if (pressed & PSP_CTRL_SQUARE) g_screen = SCR_SITELIST;
        break;

    case SCR_PLAYER:
        if (pressed & PSP_CTRL_START)
            go_player_toggle_pause();
        if (pressed & PSP_CTRL_TRIANGLE)
            go_player_cycle_overlay();
        if (pressed & PSP_CTRL_SELECT)
            go_player_cycle_render_mode();
        if (pressed & PSP_CTRL_SQUARE) {
            /* Original leaves the decoder alive and returns to its list pane. */
            go_thumbnails_suspend(0);
            g_screen = SCR_RESULTS;
        }
        if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
            GTVideo video;
            int direction = (pressed & PSP_CTRL_LTRIGGER) ? -1 : 1;
            go_player_cycle_speed(direction);
            if (go_playlist_step(direction, &video) == 0) {
                go_player_stop();
                start_video(&video);
            }
        }
        if (pressed & PSP_CTRL_CROSS) {
            go_player_stop();
            go_thumbnails_suspend(0);
            g_screen = SCR_RESULTS;
        }
        break;

    case SCR_MENU: {
        if (g_menu_phase == 1) {
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE))
                go_menu_close(g_menu_return_screen);
            break;
        }
        if (g_menu_phase != 2) break;
        if (pressed & PSP_CTRL_UP) {
            if (--g_menu_sel < 0) g_menu_sel = g_menu_count - 1;
            if (g_menu_count > 1 &&
                g_menu_actions[g_menu_sel] == GT_MENU_TAG_LABEL) {
                if (--g_menu_sel < 0) g_menu_sel = g_menu_count - 1;
            }
        }
        if (pressed & PSP_CTRL_DOWN) {
            if (++g_menu_sel >= g_menu_count) g_menu_sel = 0;
            if (g_menu_count > 1 &&
                g_menu_actions[g_menu_sel] == GT_MENU_TAG_LABEL) {
                if (++g_menu_sel >= g_menu_count) g_menu_sel = 0;
            }
        }
        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE))
            go_menu_close(g_menu_return_screen);
        if (pressed & PSP_CTRL_CIRCLE) {
            int action = g_menu_actions[g_menu_sel];
            if (action == GT_MENU_TAG_SEARCH) {
                strncpy(g_search_keyword, g_menu_labels[g_menu_sel],
                        sizeof(g_search_keyword) - 1);
                g_search_keyword[sizeof(g_search_keyword) - 1] = 0;
                go_search_page(1);
                go_menu_close(SCR_RESULTS);
            } else if (action == GT_MENU_SITE) {
                /* Action 0x6f only starts the menu close animation; source
                 * view switching is the Square branch in VA 0x2097c. */
                go_menu_close(g_menu_return_screen);
            } else if (action == GT_MENU_OPEN_URL) {
                g_osk_mode = 1;
                if (go_osk_open("Open URL", "http://") == 0)
                    go_menu_close(SCR_OSK);
            } else if (action == GT_MENU_NET_STATUS) {
                go_netconf_open_status();
                go_menu_close(g_menu_return_screen);
            } else if (action == GT_MENU_ADD_PLAYLIST && g_result_count > 0) {
                go_playlist_add(&g_results[g_result_sel]);
                go_menu_close(g_menu_return_screen);
            } else if (action == GT_MENU_REMOVE_PLAYLIST && g_result_count > 0) {
                go_playlist_remove(&g_results[g_result_sel]);
                go_menu_close(g_menu_return_screen);
            } else if (action == GT_MENU_PLAY_PLAYLIST) {
                GTVideo video;
                if (go_playlist_first(&video) == 0) {
                    go_thumbnails_suspend(1);
                    if (start_video(&video) == 0)
                        go_menu_close(SCR_PLAYER);
                    else
                        go_thumbnails_suspend(0);
                }
            } else if (action == GT_MENU_SAVE && g_result_count > 0) {
                char initial[200];
                if (go_save_prepare(&g_results[g_result_sel], initial,
                                    sizeof(initial)) == 0) {
                    g_osk_mode = 2;
                    if (go_osk_open("SaveAs", initial) == 0)
                        go_menu_close(SCR_OSK);
                }
            } else if (action == GT_MENU_RENAME && g_result_count > 0 &&
                       g_results[g_result_sel].local_kind) {
                g_osk_mode = 3;
                if (go_osk_open("Rename", g_results[g_result_sel].title) == 0)
                    go_menu_close(SCR_OSK);
            } else if (action == GT_MENU_DELETE && g_result_count > 0 &&
                       g_results[g_result_sel].local_kind) {
                go_local_delete(&g_results[g_result_sel]);
                go_menu_close(g_menu_return_screen);
            } else {
                go_menu_close(g_menu_return_screen);
            }
        }
        break;
    }

    default:
        break;
    }
}
