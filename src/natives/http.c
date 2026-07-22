/*
 * GoTube — HTTP native: GetContents + PostContents
 * Uses PSP's sceHttp library. Matches the decompiled original:
 *   DAT_0001d0d0: GetContents wrapper
 *   DAT_0001c52c: PostContents wrapper
 */
#include "gotube.h"
#include <psphttp.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>

/* Lazy network initialization; native lifecycle evidence begins at VA 0x98ec. */
static volatile int g_net_inited = 0;
static int g_apctl_handler = -1;
/* 0 uninitialized, 1 initializing, 2 ready, -1 failed. */
static volatile int g_http_inited = 0;

static void apctl_handler(int old_state, int new_state, int event, int error,
                          void *arg)
{
    (void)old_state; (void)new_state; (void)event; (void)error; (void)arg;
}

int ensure_network(void)
{
    if (g_net_inited == 2) return 0;
    if (!__sync_bool_compare_and_swap(&g_net_inited, 0, 1)) {
        while (g_net_inited == 1) sceKernelDelayThread(1000);
        return g_net_inited == 2 ? 0 : -1;
    }
    if (sceNetInit(0x40000, 0x12, 0, 0x12, 0) < 0 &&
        sceNetInit(0x40000, 0x12, 0x1000, 0x12, 0x1000) < 0) {
        g_net_inited = -1;
        return -1;
    }
    if (sceNetInetInit() < 0) {
        g_net_inited = -1;
        return -1;
    }
    if (sceNetApctlInit(0x8000, 0x13) < 0) {
        g_net_inited = -1;
        return -1;
    }
    g_apctl_handler = sceNetApctlAddHandler(apctl_handler, NULL);
    if (g_apctl_handler < 0) {
        g_net_inited = -1;
        return -1;
    }
    g_net_inited = 2;
    return 0;
}

static int ensure_http(void)
{
    if (g_http_inited == 2) return 0;
    if (__sync_bool_compare_and_swap(&g_http_inited, 0, 1)) {
        g_http_inited = sceHttpInit(20000) < 0 ? -1 : 2;
        return g_http_inited == 2 ? 0 : -1;
    }
    while (g_http_inited == 1) sceKernelDelayThread(1000);
    return g_http_inited == 2 ? 0 : -1;
}

/* URL parser: extracts host, port, path from http(s)://host[:port]/path */
static int parse_url(const char *url, char *host, int host_sz,
                     int *port, char *path, int path_sz)
{
    const char *p;
    int len;

    if (!url || !host || !path) return -1;

    if (strncmp(url, "http://", 7) == 0) {
        url += 7; *port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        url += 8; *port = 443;
    } else {
        *port = 80;
    }

    /* host */
    p = url;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    len = p - url;
    if (len >= host_sz) len = host_sz - 1;
    memcpy(host, url, len); host[len] = 0;

    /* port */
    if (*p == ':') {
        p++; *port = 0;
        while (*p >= '0' && *p <= '9')
            *port = (*port * 10) + (*p++ - '0');
    }

    /* path */
    if (*p == '/' || *p == '?') {
        const char *q = p;
        while (*q) q++;
        len = q - p;
        if (len >= path_sz) len = path_sz - 1;
        memcpy(path, p, len); path[len] = 0;
    } else {
        strcpy(path, "/");
    }
    return 0;
}

typedef struct { int tmpl, conn, req, modern; void *modern_stream; } GTHttpStream;

void *go_http_stream_open(const char *url)
{
    char host[256], path[1024];
    int port, status = 0;
    GTHttpStream *stream;
    if (url && strncmp(url, "https://", 8) == 0) {
        stream = calloc(1, sizeof(*stream));
        if (!stream) return NULL;
        stream->modern = 1;
        stream->modern_stream = go_curl_stream_open(url);
        if (!stream->modern_stream) { free(stream); return NULL; }
        return stream;
    }
    if (!url || parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0 ||
        ensure_network() < 0 || ensure_http() < 0) return NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->tmpl = stream->conn = stream->req = -1;
    stream->tmpl = sceHttpCreateTemplate("GoTube/1.2 PSP", 0, 0);
    if (stream->tmpl < 0) goto fail;
    sceHttpSetResolveRetry(stream->tmpl, 5);
    sceHttpSetResolveTimeOut(stream->tmpl, 10000000);
    sceHttpSetConnectTimeOut(stream->tmpl, 10000000);
    sceHttpSetSendTimeOut(stream->tmpl, 10000000);
    sceHttpSetRecvTimeOut(stream->tmpl, 10000000);
    sceHttpEnableRedirect(stream->tmpl);
    sceHttpEnableCookie(stream->tmpl);
    stream->conn = sceHttpCreateConnection(stream->tmpl, host, "http", port, 0);
    if (stream->conn < 0) goto fail;
    stream->req = sceHttpCreateRequest(stream->conn, PSP_HTTP_METHOD_GET, path, 0);
    if (stream->req < 0) goto fail;
    sceHttpAddExtraHeader(stream->req, "Accept-Language",
                          "ja,en-us;q=0.7,en;q=0.3", 0);
    if (sceHttpSendRequest(stream->req, NULL, 0) < 0 ||
        sceHttpGetStatusCode(stream->req, &status) < 0 || status != 200) goto fail;
    return stream;
fail:
    go_http_stream_close(stream);
    return NULL;
}

int go_http_stream_read(void *opaque, unsigned char *buffer, int size)
{
    GTHttpStream *stream = opaque;
    if (!stream || !buffer || size < 1) return -1;
    if (stream->modern)
        return go_curl_stream_read(stream->modern_stream, buffer, size);
    if (stream->req < 0) return -1;
    return sceHttpReadData(stream->req, buffer, size);
}

long long go_http_stream_seek(void *opaque, long long offset, int whence)
{
    GTHttpStream *stream = opaque;
    if (!stream || !stream->modern || !stream->modern_stream) return -1;
    return go_curl_stream_seek(stream->modern_stream, offset, whence);
}

void go_http_stream_close(void *opaque)
{
    GTHttpStream *stream = opaque;
    if (!stream) return;
    if (stream->modern) {
        go_curl_stream_close(stream->modern_stream);
        free(stream);
        return;
    }
    if (stream->req >= 0) sceHttpDeleteRequest(stream->req);
    if (stream->conn >= 0) sceHttpDeleteConnection(stream->conn);
    if (stream->tmpl >= 0) sceHttpDeleteTemplate(stream->tmpl);
    free(stream);
}

int go_http_download(const char *url, const char *path,
                     volatile int *progress, volatile int *cancel)
{
    char host[256], request_path[1024], block[16384];
    int port, tmpl = -1, conn = -1, req = -1, fd = -1;
    int got, status = 0, total = 0;
    SceULong64 length = 0;

    if (url && strncmp(url, "https://", 8) == 0)
        return go_curl_download(url, path, progress, cancel);
    if (!url || !path || parse_url(url, host, sizeof(host), &port,
                                    request_path, sizeof(request_path)) < 0)
        return -1;
    if (ensure_network() < 0 || ensure_http() < 0) return -1;
    tmpl = sceHttpCreateTemplate("GoTube/1.2 PSP", 0, 0);
    if (tmpl < 0) goto fail;
    sceHttpEnableRedirect(tmpl);
    sceHttpEnableCookie(tmpl);
    conn = sceHttpCreateConnection(tmpl, host, "http", port, 0);
    if (conn < 0) goto fail;
    req = sceHttpCreateRequest(conn, PSP_HTTP_METHOD_GET, request_path, 0);
    if (req < 0) goto fail;
    sceHttpAddExtraHeader(req, "Accept-Language",
                          "ja,en-us;q=0.7,en;q=0.3", 0);
    if (sceHttpSendRequest(req, NULL, 0) < 0 ||
        sceHttpGetStatusCode(req, &status) < 0 || status != 200)
        goto fail;
    sceHttpGetContentLength(req, &length);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) goto fail;
    while (!cancel || !*cancel) {
        got = sceHttpReadData(req, block, sizeof(block));
        if (got < 0 || (got > 0 && sceIoWrite(fd, block, got) != got)) goto fail;
        if (got == 0) break;
        total += got;
        if (progress && length)
            *progress = (int)(((SceULong64)total * 1000) / length);
        sceKernelDelayThreadCB(100);
    }
    if (fd >= 0) sceIoClose(fd);
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    if (cancel && *cancel) { sceIoRemove(path); return -1; }
    if (progress) *progress = 1000;
    return 0;

fail:
    if (fd >= 0) sceIoClose(fd);
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    sceIoRemove(path);
    return -1;
}

/* Historical JavaScript wrappers are retained in source for the preservation
 * branch but excluded from the native modern build. */
#if 0
/*
 * GetContents(url) - HTTP GET, returns page body as JS string or null.
 */
JSBool go_getcontents(JSContext *cx, JSObject *obj, uintN argc,
                      jsval *argv, jsval *rval)
{
    char      *url, host[256], path[1024];
    int        port, tmpl=-1, conn=-1, req=-1, ret, total=0, bufsz=4096;
    char      *buf = NULL;
    JSString  *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    url = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    /* Cat's historical native deliberately prevents scripts from reading the
     * Memory Stick through either network entry point. */
    if (!url) return JS_TRUE;
    if (strncmp(url, "ms0:", 4) == 0) {
        go_message_dialog("GetContents:\nDeny access ms0:");
        return JS_TRUE;
    }
    if (
        parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
        return JS_TRUE;

    if (ensure_network() < 0 || ensure_http() < 0) return JS_TRUE;

    tmpl = sceHttpCreateTemplate("GoTube/1.2 PSP", 0, 0);
    if (tmpl < 0) goto end;
    sceHttpEnableRedirect(tmpl);
    sceHttpEnableCookie(tmpl);
    sceHttpSetResolveRetry(tmpl, 5);
    sceHttpSetResolveTimeOut(tmpl, 10000000);
    sceHttpSetConnectTimeOut(tmpl, 10000000);
    sceHttpSetSendTimeOut(tmpl, 10000000);
    sceHttpSetRecvTimeOut(tmpl, 10000000);

    conn = sceHttpCreateConnection(tmpl, host, "http", port, 0);
    if (conn < 0) goto end;

    req = sceHttpCreateRequest(conn, 0, path, 0);  /* 0 = GET */
    if (req < 0) goto end;
    sceHttpAddExtraHeader(req, "Accept-Language",
                          "ja,en-us;q=0.7,en;q=0.3", 0);

    if (sceHttpSendRequest(req, NULL, 0) < 0) goto end;

    buf = (char*)malloc(bufsz);
    if (!buf) goto end;

    for (;;) {
        if (total + 1024 >= bufsz) {
            bufsz *= 2;
            char *nb = (char*)realloc(buf, bufsz);
            if (!nb) break; else buf = nb;
        }
        ret = sceHttpReadData(req, buf + total, bufsz - total - 1);
        if (ret <= 0) break;
        total += ret;
    }
    buf[total] = 0;

    jstr = JS_NewStringCopyN(cx, buf, total);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);

end:
    if (buf) free(buf);
    if (req >=0) sceHttpDeleteRequest(req);
    if (conn>=0) sceHttpDeleteConnection(conn);
    if (tmpl>=0) sceHttpDeleteTemplate(tmpl);
    return JS_TRUE;
}

/*
 * PostContents(url, data) - HTTP POST, returns page body as JS string.
 */
JSBool go_postcontents(JSContext *cx, JSObject *obj, uintN argc,
                       jsval *argv, jsval *rval)
{
    char      *url, *post="", host[256], path[1024];
    int        port, tmpl=-1, conn=-1, req=-1, ret, status=0;
    int        total=0, bufsz=4096;
    char      *buf = NULL;
    JSString  *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    url = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    if (argc>=2 && JSVAL_IS_STRING(argv[1]))
        post = JS_GetStringBytes(JSVAL_TO_STRING(argv[1]));
    if (!url) return JS_TRUE;
    if (strncmp(url,"ms0:",4)==0) {
        go_message_dialog("PostContents:\nDeny access ms0:");
        return JS_TRUE;
    }
    if (strncmp(url,"https://",8)==0) {
        go_message_dialog("PostContents:\nnot yet support. perhaps soon");
        return JS_TRUE;
    }
    if (strncmp(url,"http://",7)!=0) {
        go_message_dialog("PostContents:\nUnknown protocol");
        return JS_TRUE;
    }
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0 ||
        !host[0]) {
        go_message_dialog("PostContents:\nerror:Cat_ResolverURL");
        return JS_TRUE;
    }

    if (ensure_network() < 0 || ensure_http() < 0) {
        go_message_dialog("PostContents:\nerror:socket()");
        return JS_TRUE;
    }

    tmpl = sceHttpCreateTemplate("GoTube/1.2 PSP", 0, 0);
    if (tmpl < 0) {
        go_message_dialog("PostContents:\nerror:internal 1");
        goto end;
    }
    sceHttpEnableRedirect(tmpl);
    sceHttpEnableCookie(tmpl);
    sceHttpSetResolveRetry(tmpl, 5);
    sceHttpSetResolveTimeOut(tmpl, 10000000);
    sceHttpSetConnectTimeOut(tmpl, 10000000);
    sceHttpSetSendTimeOut(tmpl, 10000000);
    sceHttpSetRecvTimeOut(tmpl, 10000000);

    conn = sceHttpCreateConnection(tmpl, host, "http", port, 0);
    if (conn < 0) {
        go_message_dialog("PostContents:\nerror:connect()");
        goto end;
    }

    req = sceHttpCreateRequest(conn, 1, path, 0);  /* 1 = POST */
    if (req < 0) {
        go_message_dialog("PostContents:\nerror send header");
        goto end;
    }

    sceHttpAddExtraHeader(req, "Content-Type",
                          "application/x-www-form-urlencoded", 0);
    sceHttpAddExtraHeader(req, "Accept-Language",
                          "ja,en-us;q=0.7,en;q=0.3", 0);

    if (sceHttpSendRequest(req, post, strlen(post)) < 0) {
        go_message_dialog("PostContents:\nerror:http post");
        goto end;
    }
    if (sceHttpGetStatusCode(req, &status) < 0 ||
        (status != 200 && status != 204)) {
        go_message_dialog("PostContents:\nerror RC");
        goto end;
    }
    if (status == 204) goto end;

    buf = (char*)malloc(bufsz);
    if (!buf) goto end;

    for (;;) {
        if (total + 1024 >= bufsz) {
            bufsz *= 2;
            char *nb = (char*)realloc(buf, bufsz);
            if (!nb) break; else buf = nb;
        }
        ret = sceHttpReadData(req, buf + total, bufsz - total - 1);
        if (ret <= 0) break;
        total += ret;
    }
    buf[total] = 0;

    jstr = JS_NewStringCopyN(cx, buf, total);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);

end:
    if (buf) free(buf);
    if (req >=0) sceHttpDeleteRequest(req);
    if (conn>=0) sceHttpDeleteConnection(conn);
    if (tmpl>=0) sceHttpDeleteTemplate(tmpl);
    return JS_TRUE;
}
#endif
