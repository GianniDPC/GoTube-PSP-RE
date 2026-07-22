/*
 * GoTube — CallGate bridge (C → JS)
 * The engine calls into JavaScript via these CallGate entry points.
 * Also populates the C-side site list used by the GUI.
 */
#include "gotube.h"

/* --- GUI-facing site list (populated from JS SiteList) --- */
char g_site_names[MAX_SITES][1024];
char g_site_search_desc[MAX_SITES][128];
int  g_site_count = 0;
int  g_site_sel   = 0;

/*
 * Extract the "Name" string property of a JS site object into a C buffer.
 */
static void extract_string(JSContext *cx, JSObject *siteobj,
                           const char *property, char *out, int outsz)
{
    jsval  v;
    char  *s;
    int    n;

    out[0] = 0;
    if (!JS_GetProperty(cx, siteobj, property, &v) || !JSVAL_IS_STRING(v))
        return;
    s = JS_GetStringBytes(JSVAL_TO_STRING(v));
    if (!s) return;
    n = strlen(s);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

void go_callgate_sitelist(JSContext *cx)
{
    jsval    rval;
    JSObject *arr;
    uint32    len, i;

    /* The native descriptor vector at VA 0x5ee6f4 begins with these three
     * pointers, in this order, before FUN_0001b03c appends JS descriptors. */
    g_site_count = 0;
    strcpy(g_site_names[g_site_count], "Favorites");
    strcpy(g_site_search_desc[g_site_count++], "");
    strcpy(g_site_names[g_site_count], "Playlist");
    strcpy(g_site_search_desc[g_site_count++], "");
    strcpy(g_site_names[g_site_count], "Onsen");
    strcpy(g_site_search_desc[g_site_count++], "\xe9\x9f\xb3\xe6\xb3\x89(Onsen)");
    if (!cx) return;

    /* CallGate_GetSiteList() is defined in site.js and returns SiteList[] */
    if (!JS_CallFunctionName(cx, JS_GetGlobalObject(cx),
                             "CallGate_GetSiteList", 0, NULL, &rval)) {
        return;
    }

    if (!JSVAL_IS_OBJECT(rval)) return;
    arr = JSVAL_TO_OBJECT(rval);
    if (!JS_GetArrayLength(cx, arr, &len)) return;

    if (len > MAX_SITES - 3) len = MAX_SITES - 3;

    for (i = 0; i < len; i++) {
        jsval val;
        if (JS_GetElement(cx, arr, i, &val) && JSVAL_IS_OBJECT(val)) {
            extract_string(cx, JSVAL_TO_OBJECT(val), "Name",
                           g_site_names[g_site_count], 1024);
            extract_string(cx, JSVAL_TO_OBJECT(val), "SearchDesc",
                           g_site_search_desc[g_site_count], 128);
            if (g_site_names[g_site_count][0])
                g_site_count++;
        }
    }
}

/* --- Search result storage --- */
GTVideo  g_results[MAX_RESULTS];
int      g_result_count = 0;
int      g_result_sel   = 0;
int      g_result_total = 0;
int      g_result_start = 0;
int      g_result_end   = 0;
char     g_search_keyword[128] = "";
GTScreen g_screen = SCR_SITELIST;
GTScreen g_menu_return_screen = SCR_RESULTS;
int      g_menu_sel = 0;
char     g_video_url[512] = "";

/* Extract a string property from a JS object into a C buffer */
static void prop_str(JSContext *cx, JSObject *obj, const char *name,
                     char *out, int outsz)
{
    jsval  v;
    char  *s;
    int    n;

    out[0] = 0;
    if (!JS_GetProperty(cx, obj, name, &v) || !JSVAL_IS_STRING(v))
        return;
    s = JS_GetStringBytes(JSVAL_TO_STRING(v));
    if (!s) return;
    n = strlen(s);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

/* Extract an integer-ish property */
static int prop_int(JSContext *cx, JSObject *obj, const char *name)
{
    jsval    v;
    jsdouble d;

    if (!JS_GetProperty(cx, obj, name, &v)) return 0;
    if (JSVAL_IS_INT(v)) return JSVAL_TO_INT(v);
    /* The recovered YouTube parser leaves total/count XML fields as strings;
     * the original bridge performs JavaScript numeric coercion here. */
    if (JS_ValueToNumber(cx, v, &d)) return (int)d;
    return 0;
}

/*
 * go_callgate_search — invoke CallGate_SearchSite(site,keyword,page) in JS,
 * parse the returned { VideoInfo: [...] } object into g_results[].
 * Returns number of results parsed, or <0 on failure.
 */
int go_callgate_search(JSContext *cx, const char *site,
                       const char *keyword, int page)
{
    jsval    rval, vinfo, el, argv[5];
    JSObject *resobj, *arr, *vi;
    uint32   len, i;

    g_result_count = 0;
    g_result_total = 0;
    g_result_start = 0;
    g_result_end = 0;
    if (!cx || !site || !keyword) return -1;

    /* Original VA 0x1b338 calls the dispatcher with five arguments. The
     * historical JS consumes the first three; the final values are 10 and 0.
     * Its offset is 1,11,21... rather than a one-based page number. */
    {
        JSString *site_str = JS_NewStringCopyZ(cx, site);
        JSString *keyword_str = JS_NewStringCopyZ(cx, keyword);
        if (!site_str || !keyword_str) return -1;
        argv[0] = STRING_TO_JSVAL(site_str);
        argv[1] = STRING_TO_JSVAL(keyword_str);
        argv[2] = INT_TO_JSVAL((page * 10) - 9);
        argv[3] = INT_TO_JSVAL(10);
        argv[4] = INT_TO_JSVAL(0);
    }

    if (!JS_CallFunctionName(cx, JS_GetGlobalObject(cx),
                             "CallGate_SearchSite", 5, argv, &rval)) {
        /* A provider may throw when a historical endpoint is unavailable.
         * Clear the exception and expose an empty result set. */
        if (JS_IsExceptionPending(cx))
            JS_ClearPendingException(cx);
        return 0;
    }
    if (!JSVAL_IS_OBJECT(rval)) {
        return -1;
    }

    resobj = JSVAL_TO_OBJECT(rval);
    g_result_total = prop_int(cx, resobj, "total");
    g_result_start = prop_int(cx, resobj, "start");
    g_result_end = prop_int(cx, resobj, "end");
    if (!JS_GetProperty(cx, resobj, "VideoInfo", &vinfo) ||
        !JSVAL_IS_OBJECT(vinfo)) {
        return -1;
    }

    arr = JSVAL_TO_OBJECT(vinfo);
    if (!JS_GetArrayLength(cx, arr, &len)) return -1;
    if (len > MAX_RESULTS) len = MAX_RESULTS;

    for (i = 0; i < len; i++) {
        if (!JS_GetElement(cx, arr, i, &el) || !JSVAL_IS_OBJECT(el))
            continue;
        vi = JSVAL_TO_OBJECT(el);
        memset(&g_results[g_result_count], 0, sizeof(g_results[g_result_count]));
        prop_str(cx, vi, "Title",       g_results[g_result_count].title, 160);
        prop_str(cx, vi, "URL",         g_results[g_result_count].url,   512);
        prop_str(cx, vi, "ThumbnailURL",g_results[g_result_count].thumb, 512);
        prop_str(cx, vi, "Description", g_results[g_result_count].desc,
                 sizeof(g_results[g_result_count].desc));
        prop_str(cx, vi, "Author",     g_results[g_result_count].uploader, 100);
        prop_str(cx, vi, "Tags",        g_results[g_result_count].tags, 200);
        prop_str(cx, vi, "SaveFilename",g_results[g_result_count].save_filename, 200);
        g_results[g_result_count].length = prop_int(cx, vi, "LengthSeconds");
        g_results[g_result_count].views = prop_int(cx, vi, "ViewCount");
        g_results[g_result_count].favs  = prop_int(cx, vi, "MylistCount");
        g_results[g_result_count].comments = prop_int(cx, vi, "CommentCount");
        g_results[g_result_count].rating_count = prop_int(cx, vi, "RatingCount");
        g_results[g_result_count].attr = prop_int(cx, vi, "attr");
        /* Remote video nodes are protected from Rename/Delete (bit 0) and
         * gain Save when the site supplies both URL and SaveFilename (bit 1).
         * These are the two flags consumed by menu builder VA 0x1f73c. */
        if (g_results[g_result_count].url[0])
            g_results[g_result_count].attr |= 1;
        if (g_results[g_result_count].url[0] &&
            g_results[g_result_count].save_filename[0])
            g_results[g_result_count].attr |= 2;
        {
            jsval rv;
            jsdouble rating;
            if (JS_GetProperty(cx, vi, "RatingAvg", &rv) &&
                JS_ValueToNumber(cx, rv, &rating)) {
                g_results[g_result_count].rating = (float)rating;
            }
        }
        g_result_count++;
    }
    return g_result_count;
}

/*
 * go_callgate_video_url — invoke CallGate_VideoURLResolver(url_expr) in JS.
 * The VideoInfo.URL field holds an eval-able expression (e.g. 'YouTube.play("id")')
 * that the engine resolves to a real stream URL via the JS bridge.
 * Writes the resolved URL into out (outsz). Returns length, or <0 on failure.
 */
int go_callgate_video_url(JSContext *cx, const char *url_expr,
                          char *out, int outsz)
{
    jsval  rval, arg;
    int    n = 0;

    out[0] = 0;
    if (!cx || !url_expr || !url_expr[0]) return -1;

    /* VA 0x1af74 passes URL as a JS string. CallGate_VideoURLResolver performs
     * the eval itself; pre-evaluating it here changes behavior. */
    {
        JSString *expr = JS_NewStringCopyZ(cx, url_expr);
        if (!expr) return -1;
        arg = STRING_TO_JSVAL(expr);
    }
    if (!JS_CallFunctionName(cx, JS_GetGlobalObject(cx),
                             "CallGate_VideoURLResolver", 1, &arg, &rval)) {
        if (JS_IsExceptionPending(cx))
            JS_ClearPendingException(cx);
        return -1;
    }
    if (!JSVAL_IS_STRING(rval)) {
        return -1;
    }
    {
        char *s = JS_GetStringBytes(JSVAL_TO_STRING(rval));
        if (!s) return -1;
        n = strlen(s);
        if (n >= outsz) n = outsz - 1;
        memcpy(out, s, n);
        out[n] = 0;
    }
    return n;
}
