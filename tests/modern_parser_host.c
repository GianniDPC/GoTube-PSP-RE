#include "gotube.h"

GTVideo g_results[MAX_RESULTS];
int g_result_count, g_result_total, g_result_start, g_result_end;
char g_site_names[64][1024];
int g_site_sel, g_site_count;
static const char *search_path, *player_path;

char *go_curl_post_json(const char *url, const char *request,
                        const char *visitor, int *size)
{
    const char *path = strstr(url, "/search") ? search_path : player_path;
    FILE *file = fopen(path, "rb"); char *data; long length;
    (void)request; (void)visitor;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END); length = ftell(file); rewind(file);
    data = malloc(length + 1);
    if (!data || fread(data, 1, length, file) != (size_t)length) {
        fclose(file); free(data); return NULL;
    }
    fclose(file); data[length] = 0; *size = length; return data;
}

int go_modern_search(const char *, int);
int go_modern_resolve(const char *, char *, int);

int main(int argc, char **argv)
{
    char url[2048]; int count, resolved;
    if (argc != 3) return 2;
    search_path = argv[1]; player_path = argv[2];
    strcpy(g_site_names[0], "YouTube"); g_site_count = 1;
    count = go_modern_search("PSP homebrew", 1);
    if (count != 10 || strcmp(g_results[0].url, "yt:Rblwn_KOrYk") != 0 ||
        !g_results[0].title[0] || g_results[0].length != 521 ||
        g_result_total != 11) {
        fprintf(stderr, "search parse failed: count=%d id=%s title=%s length=%d\n",
                count, g_results[0].url, g_results[0].title, g_results[0].length);
        return 1;
    }
    if (go_modern_search("PSP homebrew", 2) != 10) {
        fprintf(stderr, "continuation token was not retained\n");
        return 1;
    }
    resolved = go_modern_resolve(g_results[0].url, url, sizeof(url));
    if (resolved < 100 || strncmp(url, "https://", 8) != 0 || !strstr(url, "itag=18")) {
        fprintf(stderr, "player parse failed: %d %.100s\n", resolved, url);
        return 1;
    }
    printf("parsed %d results; first=%s; stream-bytes=%d\n",
           count, g_results[0].title, resolved);
    return 0;
}
