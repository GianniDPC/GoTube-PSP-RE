#include "gotube.h"

#define PLAYLIST_MAX 64

const char *g_menu_labels[16];
int g_menu_actions[16];
int g_menu_y[16];
int g_menu_count = 0;
int g_menu_phase = 0;
GTScreen g_menu_close_target = SCR_RESULTS;

static GTVideo playlist[PLAYLIST_MAX];
static int playlist_count = 0;
static int playlist_cursor = -1;
static char menu_tag_labels[16][128];
static const char menu_tag_separator[] = "";

static void add_item(const char *label, int action, int y)
{
    if (g_menu_count >= 16) return;
    g_menu_labels[g_menu_count] = label;
    g_menu_actions[g_menu_count] = action;
    g_menu_y[g_menu_count++] = y;
}

static int playlist_index(const GTVideo *video)
{
    int i;
    if (!video) return -1;
    for (i = 0; i < playlist_count; i++)
        if (strcmp(playlist[i].url, video->url) == 0) return i;
    return -1;
}

int go_playlist_add(const GTVideo *video)
{
    if (!video || !video->url[0]) return -1;
    if (playlist_index(video) >= 0) return 0;
    if (playlist_count >= PLAYLIST_MAX) return -1;
    playlist[playlist_count++] = *video;
    return 0;
}

int go_playlist_remove(const GTVideo *video)
{
    int i = playlist_index(video);
    if (i < 0) return -1;
    memmove(&playlist[i], &playlist[i + 1],
            (playlist_count - i - 1) * sizeof(playlist[0]));
    playlist_count--;
    return 0;
}

int go_playlist_first(GTVideo *video)
{
    if (!video || playlist_count < 1) return -1;
    playlist_cursor = 0;
    *video = playlist[0];
    return 0;
}

int go_playlist_step(int direction, GTVideo *video)
{
    if (!video || playlist_count < 1 || playlist_cursor < 0) return -1;
    playlist_cursor += direction < 0 ? -1 : 1;
    if (playlist_cursor < 0) playlist_cursor = playlist_count - 1;
    if (playlist_cursor >= playlist_count) playlist_cursor = 0;
    *video = playlist[playlist_cursor];
    return 0;
}

int go_playlist_load_page(int page)
{
    int first, count;
    if (page < 1) page = 1;
    first = (page - 1) * 10;
    count = playlist_count - first;
    if (count < 0) count = 0;
    if (count > 10) count = 10;
    if (count) memcpy(g_results, playlist + first, count * sizeof(g_results[0]));
    g_result_count = count;
    g_result_total = playlist_count;
    g_result_start = count ? first + 1 : 0;
    g_result_end = first + count;
    g_result_sel = 0;
    return count;
}

void go_menu_build(void)
{
    const GTVideo *selected = NULL;
    int y = 28, tag_y = 0;
    g_menu_count = 0;
    g_menu_phase = 1;
    g_menu_close_target = g_menu_return_screen;
    if (g_result_count > 0 && g_result_sel >= 0 && g_result_sel < g_result_count)
        selected = &g_results[g_result_sel];

    /* Action IDs and conditions are direct from menu builder VA 0x1f73c. */
    add_item("Site...", GT_MENU_SITE, 0);
    add_item("OpenURL...", GT_MENU_OPEN_URL, 14);
    if (g_net_online) {
        add_item("NetStatus...", GT_MENU_NET_STATUS, 28);
        y = 42;
    }
    if (go_source_is_playlist()) {
        add_item("Remove\nfrom\nplaylist", GT_MENU_REMOVE_PLAYLIST, y);
        return;
    }
    add_item("Add to\nplaylist", GT_MENU_ADD_PLAYLIST, y);
    add_item("Play\nplaylist", GT_MENU_PLAY_PLAYLIST, y + 24);
    tag_y = y + 48;
    if (selected && (selected->attr & 2)) {
        add_item("Save...", GT_MENU_SAVE, y + 48);
        tag_y = y + 62;
    }
    if (selected && !(selected->attr & 1)) {
        add_item("Rename...", GT_MENU_RENAME, y + ((selected->attr & 2) ? 62 : 48));
        add_item("Delete", GT_MENU_DELETE, y + ((selected->attr & 2) ? 76 : 62));
        tag_y = y + ((selected->attr & 2) ? 90 : 76);
    }

    /* FUN_0001f73c inserts an action-0x70 separator and then appends each
     * whitespace-delimited Tags token as action zero.  FUN_0001ffb8 skips
     * the separator; accepting a tag repeats a page-one search for it. */
    if (selected && selected->tags[0] && g_menu_count < 15) {
        const char *p = selected->tags;
        int tag_count = 0;
        add_item(menu_tag_separator, GT_MENU_TAG_LABEL, tag_y);
        tag_y += 22;
        while (*p && g_menu_count < 16) {
            int n = 0;
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            while (*p && *p != ' ' && *p != '\t' && n < 127)
                menu_tag_labels[tag_count][n++] = *p++;
            menu_tag_labels[tag_count][n] = 0;
            while (*p && *p != ' ' && *p != '\t') p++;
            add_item(menu_tag_labels[tag_count], GT_MENU_TAG_SEARCH,
                     tag_y + tag_count * 10);
            tag_count++;
        }
    }
}

void go_menu_close(GTScreen target)
{
    g_menu_close_target = target;
    g_menu_phase = 3;
    g_screen = SCR_MENU;
}
