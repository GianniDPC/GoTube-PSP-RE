/* Standalone modern YouTube provider.
 * Uses the current JS-less Android VR Innertube client and accepts only itag
 * 18 (progressive H.264 Baseline + AAC), which the recovered player can decode. */
#include "gotube.h"
#include <ctype.h>

#define SEARCH_API "https://www.youtube.com/youtubei/v1/search"
#define PLAYER_API "https://www.youtube.com/youtubei/v1/player"
#define CLIENT_JSON "\"clientName\":\"ANDROID_VR\",\"clientVersion\":\"1.65.10\",\"deviceMake\":\"Oculus\",\"deviceModel\":\"Quest 3\",\"androidSdkVersion\":32,\"osName\":\"Android\",\"osVersion\":\"12L\",\"hl\":\"en\",\"gl\":\"US\""

static char visitor_data[1024];
static char continuation[51][1024];

static const char *bounded_find(const char *p, const char *end, const char *text)
{
    int length = strlen(text);
    while (p && p + length <= end) {
        p = strstr(p, text);
        if (!p || p + length > end) return NULL;
        return p;
    }
    return NULL;
}

static int utf8_put(char *out, int size, int *used, unsigned value)
{
    if (value < 0x80) {
        if (*used + 1 >= size) return -1;
        out[(*used)++] = value;
    } else if (value < 0x800) {
        if (*used + 2 >= size) return -1;
        out[(*used)++] = 0xc0 | (value >> 6); out[(*used)++] = 0x80 | (value & 63);
    } else {
        if (*used + 3 >= size) return -1;
        out[(*used)++] = 0xe0 | (value >> 12);
        out[(*used)++] = 0x80 | ((value >> 6) & 63); out[(*used)++] = 0x80 | (value & 63);
    }
    return 0;
}

static int hex4(const char *p)
{
    int i, value = 0;
    for (i = 0; i < 4; i++) {
        int c = p[i]; value <<= 4;
        if (c >= '0' && c <= '9') value |= c - '0';
        else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
        else return -1;
    }
    return value;
}

static int json_string_at(const char *p, const char *end, char *out, int size)
{
    int used = 0;
    if (!p || p >= end || *p != '"') return -1;
    p++;
    while (p < end && *p != '"') {
        unsigned char c = *p++;
        if (c == '\\' && p < end) {
            c = *p++;
            if (c == 'u' && p + 4 <= end) {
                int value = hex4(p); p += 4;
                if (value >= 0 && utf8_put(out, size, &used, value) < 0) break;
                continue;
            }
            if (c == 'n' || c == 'r' || c == 't') c = ' ';
            else if (c == 'b' || c == 'f') continue;
        }
        if (used + 1 >= size) break;
        out[used++] = c;
    }
    out[used] = 0;
    return used;
}

static int json_value(const char *start, const char *end, const char *key,
                      char *out, int size)
{
    char pattern[80]; const char *p;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = bounded_find(start, end, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    return json_string_at(p, end, out, size);
}

static const char *object_end(const char *start, const char *limit)
{
    int depth = 0, quoted = 0, escaped = 0;
    const char *p;
    for (p = start; p < limit; p++) {
        if (quoted) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == '"') quoted = 0;
        } else if (*p == '"') quoted = 1;
        else if (*p == '{') depth++;
        else if (*p == '}' && --depth == 0) return p + 1;
    }
    return NULL;
}

static const char *nested_value(const char *start, const char *end,
                                const char *section, const char *key,
                                char *out, int size)
{
    char pattern[80]; const char *p, *section_end;
    snprintf(pattern, sizeof(pattern), "\"%s\"", section);
    p = bounded_find(start, end, pattern);
    if (!p || !(p = strchr(p, '{')) || p >= end) return NULL;
    section_end = object_end(p, end);
    if (!section_end || json_value(p, section_end, key, out, size) < 0) return NULL;
    return p;
}

static void remember_visitor(const char *json, const char *end)
{
    char value[1024];
    if (json_value(json, end, "visitorData", value, sizeof(value)) > 0) {
        strncpy(visitor_data, value, sizeof(visitor_data) - 1);
        visitor_data[sizeof(visitor_data) - 1] = 0;
    }
}

static void json_escape(const char *in, char *out, int size)
{
    int used = 0;
    while (*in && used + 7 < size) {
        unsigned char c = *in++;
        if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = c; }
        else if (c < 0x20) used += snprintf(out + used, size - used, "\\u%04x", c);
        else out[used++] = c;
    }
    out[used] = 0;
}

static int duration_seconds(const char *text)
{
    int value = 0, part = 0;
    const char *p = text;
    while (*p) {
        if (isdigit((unsigned char)*p)) part = part * 10 + (*p - '0');
        else if (*p == ':') { value = value * 60 + part; part = 0; }
        p++;
    }
    return value * 60 + part;
}

static int number_digits(const char *text)
{
    int value = 0;
    while (*text) { if (isdigit((unsigned char)*text)) value = value * 10 + (*text - '0'); text++; }
    return value;
}

int go_modern_is_source(void)
{
    return g_site_sel >= 0 && g_site_sel < g_site_count &&
           strncmp(g_site_names[g_site_sel], "YouTube", 7) == 0;
}

int go_modern_search(const char *keyword, int page)
{
    char escaped[384], *request, *json, *p, *end;
    int size = 0, count = 0, first;
    go_modern_trace("SEARCH begin page=%d keyword_bytes=%d visitor=%d", page,
                    keyword ? (int)strlen(keyword) : 0, visitor_data[0] != 0);
    if (page < 1 || page > 50) return 0;
    json_escape(keyword && keyword[0] ? keyword : "PSP", escaped, sizeof(escaped));
    request = malloc(4096); if (!request) return -1;
    if (page == 1) {
        memset(continuation, 0, sizeof(continuation));
        snprintf(request, 4096, "{\"query\":\"%s\",\"context\":{\"client\":{%s%s%s%s}}}",
                 escaped, CLIENT_JSON, visitor_data[0] ? ",\"visitorData\":\"" : "",
                 visitor_data[0] ? visitor_data : "", visitor_data[0] ? "\"" : "");
    } else {
        if (!continuation[page - 1][0]) { free(request); return 0; }
        snprintf(request, 4096, "{\"continuation\":\"%s\",\"context\":{\"client\":{%s,\"visitorData\":\"%s\"}}}",
                 continuation[page - 1], CLIENT_JSON, visitor_data);
    }
    json = go_curl_post_json(SEARCH_API, request, visitor_data, &size);
    free(request);
    if (!json) { go_modern_trace("SEARCH failed transport"); return -1; }
    end = json + size; remember_visitor(json, end);
    p = json;
    while (count < 10 && (p = (char *)bounded_find(p, end, "\"compactVideoRenderer\""))) {
        char *obj = strchr(p, '{'); const char *obj_end;
        GTVideo *v; char temp[160], video_id[16];
        if (!obj || obj >= end || !(obj_end = object_end(obj, end))) break;
        v = &g_results[count]; memset(v, 0, sizeof(*v));
        if (json_value(obj, obj_end, "videoId", video_id, sizeof(video_id)) < 6) { p = (char *)obj_end; continue; }
        snprintf(v->url, sizeof(v->url), "yt:%s", video_id);
        nested_value(obj, obj_end, "title", "text", v->title, sizeof(v->title));
        nested_value(obj, obj_end, "shortBylineText", "text", v->uploader, sizeof(v->uploader));
        if (nested_value(obj, obj_end, "publishedTimeText", "text", temp, sizeof(temp)))
            strncpy(v->desc, temp, sizeof(v->desc) - 1);
        if (nested_value(obj, obj_end, "lengthText", "text", temp, sizeof(temp)))
            v->length = duration_seconds(temp);
        if (nested_value(obj, obj_end, "viewCountText", "text", temp, sizeof(temp)))
            v->views = number_digits(temp);
        snprintf(v->thumb, sizeof(v->thumb), "https://i.ytimg.com/vi/%s/mqdefault.jpg", video_id);
        strncpy(temp, v->title, sizeof(temp) - 1); temp[sizeof(temp) - 1] = 0;
        snprintf(v->save_filename, sizeof(v->save_filename), "%.150s.mp4", temp);
        v->attr = 3; count++; p = (char *)obj_end;
    }
    if (page < 50) {
        const char *ci = bounded_find(json, end, "\"nextContinuationData\"");
        if (ci) json_value(ci, end, "continuation", continuation[page], sizeof(continuation[page]));
    }
    free(json);
    first = (page - 1) * 10 + 1;
    g_result_count = count; g_result_start = count ? first : 0;
    g_result_end = count ? first + count - 1 : 0;
    g_result_total = g_result_end + (continuation[page][0] ? 1 : 0);
    go_modern_trace("SEARCH end results=%d range=%d-%d more=%d visitor=%d",
                    count, g_result_start, g_result_end,
                    continuation[page][0] != 0, visitor_data[0] != 0);
    return count;
}

int go_modern_resolve(const char *value, char *out, int out_size)
{
    char request[3072], *json, *end, *itag, visitor_before[1024];
    int size, attempt;
    const char *id = value;
    if (!value || !out || out_size < 16) return -1;
    if (strncmp(id, "yt:", 3) == 0) id += 3;
    if (strlen(id) != 11) return -1;
    go_modern_trace("RESOLVE begin id_length=%d visitor=%d", (int)strlen(id),
                    visitor_data[0] != 0);
    for (attempt = 0; attempt < 2; attempt++) {
        snprintf(request, sizeof(request), "{\"videoId\":\"%s\",\"context\":{\"client\":{%s%s%s%s}}}",
                 id, CLIENT_JSON, visitor_data[0] ? ",\"visitorData\":\"" : "",
                 visitor_data[0] ? visitor_data : "", visitor_data[0] ? "\"" : "");
        strcpy(visitor_before, visitor_data);
        json = go_curl_post_json(PLAYER_API, request, visitor_data, &size);
        if (!json) return -1;
        end = json + size; remember_visitor(json, end);
        itag = json;
        while ((itag = (char *)bounded_find(itag, end, "\"itag\""))) {
            char *number = itag + 6;
            while (number < end && (*number == ' ' || *number == ':' || *number == '\t')) number++;
            if (atoi(number) == 18) break;
            itag = number;
        }
        if (itag && json_value(itag, end, "url", out, out_size) > 0) {
            go_modern_trace("RESOLVE itag18 url_bytes=%d attempt=%d",
                            (int)strlen(out), attempt + 1);
            free(json); return strlen(out);
        }
        free(json);
        if (!visitor_data[0] || strcmp(visitor_before, visitor_data) == 0) break;
    }
    out[0] = 0;
    go_modern_trace("RESOLVE unsupported no_itag18");
    return -1;
}
