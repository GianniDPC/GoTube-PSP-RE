/* Native Favorites/Playlist sites reconstructed from descriptors at VAs
 * 0x5ee5b4/0x5ee5ec and scanners at 0x12594/0x14758. */
#include "gotube.h"
#include "cp932_reverse.inc"
#include <strings.h>

static char favorite_root[512];
static char favorite_dir[512];
static GTVideo ashx_items[MAX_RESULTS];
static int ashx_count;

static int xml_text(const char *from, const char *limit, const char *tag,
                    char *out, int out_size)
{
    char open[40], close[40];
    const char *a, *b;
    int n;
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    a = strstr(from, open);
    if (!a || a >= limit) return -1;
    a += strlen(open); b = strstr(a, close);
    if (!b || b > limit) return -1;
    n = b - a; if (n >= out_size) n = out_size - 1;
    memcpy(out, a, n); out[n] = 0;
    return 0;
}

/* ASHX is the original's RSS 2.0 special parser (VA 0x12dc8, callbacks
 * 0x12438/0x13098): a channel node owns item title/description/link nodes. */
static int parse_ashx(const char *path, char *channel, int channel_size)
{
    SceUID fd; SceOff size; char *xml; const char *ch, *end, *p;
    ashx_count = 0;
    fd = sceIoOpen(path, PSP_O_RDONLY, 0); if (fd < 0) return -1;
    size = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET);
    if (size < 1 || size > 1024 * 1024) { sceIoClose(fd); return -1; }
    xml = malloc((int)size + 1); if (!xml) { sceIoClose(fd); return -1; }
    if (sceIoRead(fd, xml, (int)size) != size) { free(xml); sceIoClose(fd); return -1; }
    sceIoClose(fd); xml[size] = 0;
    ch = strstr(xml, "<channel"); end = ch ? strstr(ch, "</channel>") : NULL;
    if (!ch || !end) { free(xml); return -1; }
    if (xml_text(ch, strstr(ch, "<item" ) ? strstr(ch, "<item") : end,
                 "title", channel, channel_size) < 0)
        strcpy(channel, "RSS");
    p = ch;
    while (ashx_count < MAX_RESULTS && (p = strstr(p, "<item")) && p < end) {
        const char *item_end = strstr(p, "</item>"); GTVideo *v;
        if (!item_end || item_end > end) break;
        v = &ashx_items[ashx_count]; memset(v, 0, sizeof(*v));
        xml_text(p, item_end, "title", v->title, sizeof(v->title));
        xml_text(p, item_end, "description", v->desc, sizeof(v->desc));
        if (xml_text(p, item_end, "link", v->url, sizeof(v->url)) == 0 && v->url[0])
            { v->local_kind = 4; ashx_count++; }
        p = item_end + 7;
    }
    free(xml); return 0;
}

static int source_named(const char *name)
{
    return g_site_count > 0 && g_site_sel >= 0 && g_site_sel < g_site_count &&
           strcmp(g_site_names[g_site_sel], name) == 0;
}

int go_source_is_favorites(void) { return source_named("Favorites"); }
int go_source_is_playlist(void) { return source_named("Playlist"); }
int go_source_is_onsen(void) { return source_named("Onsen"); }

static char *http_body(const char *url)
{
    void *stream=go_http_stream_open(url); char *body; int used=0,cap=32768,n;
    if(!stream) return NULL;
    body=malloc(cap);
    if(!body){go_http_stream_close(stream);return NULL;}
    while((n=go_http_stream_read(stream,(unsigned char *)body+used,cap-used-1))>0){
        used+=n; if(used+4096>=cap){char *next;if(cap>=524288)break;cap*=2;next=realloc(body,cap);if(!next)break;body=next;}
    }
    go_http_stream_close(stream); body[used]=0; return body;
}

int go_onsen_search(const char *keyword)
{
    char url[512], *body, *p; int count=0;
    snprintf(url,sizeof(url),"http://www.onsen.ag/%s",keyword?keyword:"");
    body=http_body(url); g_result_count=g_result_total=g_result_start=g_result_end=0;
    if(!body) return -1;
    p=body;
    while(count<MAX_RESULTS && (p=strstr(p,"<a href=\"http://onsen.ag/meta/"))){
        char *href=p+9,*quote=strchr(href,'\"'),*title=strstr(p,"<marquee scrollamount=\"2\">"),*end;
        GTVideo *v;
        if(!quote)break;
        v=&g_results[count];memset(v,0,sizeof(*v));
        {int n=quote-href;if(n>=(int)sizeof(v->url))n=sizeof(v->url)-1;memcpy(v->url,href,n);v->url[n]=0;}
        if(title && title<quote+512){title+=strlen("<marquee scrollamount=\"2\">");end=strstr(title,"</marquee>");if(!end)end=strstr(title,"</td>");if(end){int n=end-title;if(n>=(int)sizeof(v->title))n=sizeof(v->title)-1;memcpy(v->title,title,n);v->title[n]=0;}}
        if(!v->title[0])strcpy(v->title,"Onsen");
        v->attr=3; count++; p=quote+1;
    }
    free(body); g_result_count=g_result_total=count;g_result_start=count?1:0;g_result_end=count;g_result_sel=0;return count;
}

int go_onsen_resolve(const char *url, char *out, int out_size)
{
    char *body,*a,*b;int n;
    if(!url||strncmp(url,"http://onsen.ag/meta/",21)||out_size<8)return -1;
    body=http_body(url);if(!body)return -1;a=strstr(body,"\"mms://");if(!a){free(body);return -1;}a++;
    b=strchr(a,'\"');if(!b){free(body);return -1;}n=b-a;if(n>=out_size)n=out_size-1;memcpy(out,a,n);out[n]=0;free(body);return 0;
}

static void configured_root(void)
{
    if (favorite_root[0]) return;
    if (strncmp(g_favorites, "ms0:", 4) == 0)
        snprintf(favorite_root, sizeof(favorite_root), "%s", g_favorites);
    else
        /* Native setter VA 0x1d620 copies PSPTube.favorites verbatim into
         * the directory scanned at VA 0x12594.  A leading '/' is the root
         * of the current ms0: device, not relative to the EBOOT directory. */
        snprintf(favorite_root, sizeof(favorite_root), "ms0:%s",
                 g_favorites[0] ? g_favorites : "/");
    while (strlen(favorite_root) > 5 &&
           favorite_root[strlen(favorite_root) - 1] == '/')
        favorite_root[strlen(favorite_root) - 1] = 0;
    strcpy(favorite_dir, favorite_root);
}

static void filename_utf8(const char *sjis, char *out, int out_size)
{
    int i = 0, used = 0;
    while (sjis[i]) {
        unsigned encoded = (unsigned char)sjis[i++], code = 0;
        int p;
        if ((encoded >= 0x81 && encoded <= 0x9f) ||
            (encoded >= 0xe0 && encoded <= 0xef)) {
            if (!sjis[i]) break;
            encoded = (encoded << 8) | (unsigned char)sjis[i++];
        }
        if (encoded < 0x80) code = encoded;
        else {
            for (p = 0; p < (int)(sizeof(gt_cp932_reverse) /
                                   sizeof(gt_cp932_reverse[0])); p++)
                if (gt_cp932_reverse[p].cp932 == encoded) {
                    code = gt_cp932_reverse[p].ucs2;
                    break;
                }
        }
        if (!code) code = '?';
        if (code < 0x80) {
            if (used + 1 >= out_size) break;
            out[used++] = code;
        } else if (code < 0x800) {
            if (used + 2 >= out_size) break;
            out[used++] = 0xc0 | (code >> 6);
            out[used++] = 0x80 | (code & 0x3f);
        } else {
            if (used + 3 >= out_size) break;
            out[used++] = 0xe0 | (code >> 12);
            out[used++] = 0x80 | ((code >> 6) & 0x3f);
            out[used++] = 0x80 | (code & 0x3f);
        }
    }
    out[used] = 0;
}

static int excluded_extension(const char *name)
{
    static const char *excluded[] = {
        "exe", "com", "dll", "prx", "pbp", "THM", "xml", "bin",
        "cfg", "bak", "txt", "js",
        /* Original dispatch entries point ASX/WVX/WAX at the null parser. */
        "asx", "wvx", "wax", NULL
    };
    const char *dot = strrchr(name, '.');
    int i;
    if (!dot || !dot[1]) return 0;
    for (i = 0; excluded[i]; i++)
        if (strcasecmp(dot + 1, excluded[i]) == 0) return 1;
    return 0;
}

static int append_result(const char *name, const SceIoDirent *entry, int directory)
{
    GTVideo *video;
    char path[512];
    char title[160];
    int length;
    if (g_result_count >= MAX_RESULTS) return -1;
    length = snprintf(path, sizeof(path), "%s%s%s", favorite_dir,
                      favorite_dir[0] && favorite_dir[strlen(favorite_dir) - 1] == '/'
                          ? "" : "/",
                      name);
    if (length < 0 || length >= (int)sizeof(path)) return -1;
    video = &g_results[g_result_count++];
    memset(video, 0, sizeof(*video));
    filename_utf8(name, title, sizeof(title));
    strcpy(video->title, title);
    strcpy(video->url, path);
    video->local_kind = directory ? 2 : 1;
    if (!directory) {
        go_sidecar_path(path, ".THM", video->thumb, sizeof(video->thumb));
        snprintf(video->desc, sizeof(video->desc),
                 "%d/%d/%d %02d:%02d:%02d\r\n%dbytes\r\n",
                 entry->d_stat.sce_st_mtime.year, entry->d_stat.sce_st_mtime.month,
                 entry->d_stat.sce_st_mtime.day, entry->d_stat.sce_st_mtime.hour,
                 entry->d_stat.sce_st_mtime.minute, entry->d_stat.sce_st_mtime.second,
                 (int)entry->d_stat.st_size);
    } else {
        video->attr = 1;
        snprintf(video->desc, sizeof(video->desc),
                 "Folder %.120s\r\n%d/%d/%d %02d:%02d:%02d", path,
                 entry->d_stat.sce_st_mtime.year, entry->d_stat.sce_st_mtime.month,
                 entry->d_stat.sce_st_mtime.day, entry->d_stat.sce_st_mtime.hour,
                 entry->d_stat.sce_st_mtime.minute, entry->d_stat.sce_st_mtime.second);
    }
    return 0;
}

static int append_ashx(const char *name)
{
    char path[512], channel[160]; GTVideo *video;
    if (g_result_count >= MAX_RESULTS) return -1;
    if (snprintf(path, sizeof(path), "%s%s%s", favorite_dir,
                 favorite_dir[0] && favorite_dir[strlen(favorite_dir) - 1] == '/'
                     ? "" : "/",
                 name) >= (int)sizeof(path)) return -1;
    if (parse_ashx(path, channel, sizeof(channel)) < 0) return -1;
    video = &g_results[g_result_count++]; memset(video, 0, sizeof(*video));
    snprintf(video->title, sizeof(video->title), "%s", channel);
    strcpy(video->url, path); video->local_kind = 3;
    snprintf(video->desc, sizeof(video->desc), "%d item%s", ashx_count,
             ashx_count == 1 ? "" : "s");
    return 0;
}

static int load_favorites(int page)
{
    SceUID fd;
    SceIoDirent entry;
    int visible = 0, first = (page < 1 ? 0 : (page - 1) * 10);
    configured_root();
    g_result_count = 0;
    g_result_total = 0;
    fd = sceIoDopen(favorite_dir);
    if (fd < 0) return -1;
    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(fd, &entry) > 0) {
        int directory = FIO_S_ISDIR(entry.d_stat.st_mode);
        if (!strcmp(entry.d_name, ".") ||
            (!strcmp(entry.d_name, "..") && !strcmp(favorite_dir, favorite_root)) ||
            (!directory && excluded_extension(entry.d_name))) {
            memset(&entry, 0, sizeof(entry));
            continue;
        }
        if (visible >= first && visible < first + 10) {
            const char *dot = strrchr(entry.d_name, '.');
            if (!directory && dot && strcasecmp(dot + 1, "ashx") == 0)
                append_ashx(entry.d_name);
            else
                append_result(entry.d_name, &entry, directory);
        }
        visible++;
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(fd);
    g_result_total = visible;
    g_result_start = g_result_count ? first + 1 : 0;
    g_result_end = first + g_result_count;
    g_result_sel = 0;
    g_search_keyword[0] = 0;
    return g_result_count;
}

int go_source_load(int page)
{
    go_thumbnails_reset();
    if (go_source_is_favorites()) return load_favorites(page);
    if (go_source_is_playlist()) return go_playlist_load_page(page);
    if (go_source_is_onsen()) return go_onsen_search(g_search_keyword);
    return -1;
}

int go_source_enter(const GTVideo *video)
{
    if (!video || !video->local_kind) return -1;
    if (video->local_kind == 2) {
        if (!strcmp(video->title, "..")) return go_source_parent();
        strncpy(favorite_dir, video->url, sizeof(favorite_dir) - 1);
        favorite_dir[sizeof(favorite_dir) - 1] = 0;
        return load_favorites(1);
    }
    if (video->local_kind == 3) {
        char channel[160];
        if (parse_ashx(video->url, channel, sizeof(channel)) < 0) return -1;
        memcpy(g_results, ashx_items, ashx_count * sizeof(g_results[0]));
        g_result_count = g_result_total = ashx_count;
        g_result_start = ashx_count ? 1 : 0; g_result_end = ashx_count;
        g_result_sel = 0;
        return ashx_count;
    }
    if (video->local_kind == 4) return go_player_start(video->url);
    return go_player_start_file(video->url);
}

int go_source_parent(void)
{
    char *slash;
    configured_root();
    if (strcmp(favorite_dir, favorite_root) == 0) return -1;
    slash = strrchr(favorite_dir, '/');
    if (!slash || slash < favorite_dir + strlen(favorite_root))
        strcpy(favorite_dir, favorite_root);
    else
        *slash = 0;
    return load_favorites(1);
}

int go_local_rename(const GTVideo *video, const char *filename)
{
    char clean[400], target[512], old_side[520], new_side[520];
    const char *slash;
    int prefix;
    if (!video || (video->local_kind != 1 && video->local_kind != 2) ||
        go_filename_sanitize(filename, clean, sizeof(clean)) < 0)
        return -1;
    slash = strrchr(video->url, '/');
    prefix = slash ? slash - video->url + 1 : 0;
    if ((unsigned)prefix + strlen(clean) + 1 > sizeof(target)) return -1;
    memcpy(target, video->url, prefix);
    strcpy(target + prefix, clean);
    if (sceIoRename(video->url, target) < 0) return -1;
    if (video->local_kind == 1) {
        go_sidecar_path(video->url, ".xml", old_side, sizeof(old_side));
        go_sidecar_path(target, ".xml", new_side, sizeof(new_side));
        sceIoRename(old_side, new_side);
        go_sidecar_path(video->url, ".THM", old_side, sizeof(old_side));
        go_sidecar_path(target, ".THM", new_side, sizeof(new_side));
        sceIoRename(old_side, new_side);
    }
    go_playlist_remove(video);
    return load_favorites(1) < 0 ? -1 : 0;
}

int go_local_delete(const GTVideo *video)
{
    char side[520];
    int ret;
    if (!video || (video->local_kind != 1 && video->local_kind != 2)) return -1;
    ret = video->local_kind == 2 ? sceIoRmdir(video->url) : sceIoRemove(video->url);
    if (ret < 0) return -1;
    if (video->local_kind == 1) {
        go_sidecar_path(video->url, ".xml", side, sizeof(side)); sceIoRemove(side);
        go_sidecar_path(video->url, ".THM", side, sizeof(side)); sceIoRemove(side);
    }
    go_playlist_remove(video);
    return load_favorites(1) < 0 ? -1 : 0;
}
