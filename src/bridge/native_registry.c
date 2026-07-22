/* Fixed provider/state registry for the standalone modern build. */
#include "gotube.h"

char g_favorites[256] = "/";
int g_video_out_mode = 1;
int g_screen_zoom = 1;

char g_site_names[MAX_SITES][1024];
char g_site_search_desc[MAX_SITES][128];
int g_site_count;
int g_site_sel;

GTVideo g_results[MAX_RESULTS];
int g_result_count;
int g_result_sel;
int g_result_total;
int g_result_start;
int g_result_end;
char g_search_keyword[128] = "";
GTScreen g_screen = SCR_SITELIST;
GTScreen g_menu_return_screen = SCR_RESULTS;
int g_menu_sel;
char g_video_url[2048] = "";

void go_native_registry_init(void)
{
    g_site_count = 0;
    strcpy(g_site_names[g_site_count], "Favorites");
    strcpy(g_site_search_desc[g_site_count++], "");
    strcpy(g_site_names[g_site_count], "Playlist");
    strcpy(g_site_search_desc[g_site_count++], "");
    strcpy(g_site_names[g_site_count], "Onsen");
    strcpy(g_site_search_desc[g_site_count++],
           "\xe9\x9f\xb3\xe6\xb3\x89(Onsen)");
    strcpy(g_site_names[g_site_count], "YouTube");
    strcpy(g_site_search_desc[g_site_count++], "YouTube");
    g_site_sel = 0;
}
