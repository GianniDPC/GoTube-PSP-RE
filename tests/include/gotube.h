#ifndef GOTUBE_HOST_TEST_H
#define GOTUBE_HOST_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_RESULTS 50
typedef struct {
    char desc[256], uploader[100]; int views, favs; float rating;
    char title[160], url[512], thumb[512]; int length, rating_count, comments, attr;
    char tags[200], save_filename[200]; int local_kind;
} GTVideo;
extern GTVideo g_results[MAX_RESULTS];
extern int g_result_count, g_result_total, g_result_start, g_result_end;
extern char g_site_names[64][1024];
extern int g_site_sel, g_site_count;
char *go_curl_post_json(const char *, const char *, const char *, int *);
void go_modern_trace(const char *, ...);
#endif
