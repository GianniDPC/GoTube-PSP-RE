/* TLS 1.2 transport for modern services.  sceHttp remains the historical
 * backend; libcurl+mbedTLS is isolated here so legacy behavior is unchanged. */
#include "gotube.h"
#include <curl/curl.h>
#include <malloc.h>
#include <psprtc.h>

#define CA_FILE "ms0:/PSP/GAME/GoTube/cacert.pem"
#define BODY_LIMIT (512 * 1024)
#define RING_SIZE (256 * 1024)

typedef struct {
    char *data;
    int size, capacity, failed;
} CurlBody;

typedef struct {
    char url[2048];
    unsigned char *ring;
    volatile int read_pos, write_pos, used, done, cancel, error;
    SceUID thread;
} CurlStream;

static volatile int curl_state;

int go_modern_clock_valid(void)
{
    ScePspDateTime now;
    memset(&now, 0, sizeof(now));
    if (sceRtcGetCurrentClockLocalTime(&now) < 0) {
        go_modern_trace("CLOCK read failed");
        return 0;
    }
    go_modern_trace("CLOCK local=%04d-%02d-%02d %02d:%02d:%02d",
                    now.year, now.month, now.day, now.hour, now.minute,
                    now.second);
    /* The bundled GTS roots begin in 2016 and expire in 2036. */
    return now.year >= 2016 && now.year <= 2036;
}

void go_modern_trace(const char *format, ...)
{
    static int initialized;
    char line[512];
    va_list args;
    int fd, length, flags = PSP_O_WRONLY | PSP_O_CREAT;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    length = strlen(line);
    if (length + 1 < (int)sizeof(line)) line[length++] = '\n';
    if (!initialized) flags |= PSP_O_TRUNC;
    else flags |= PSP_O_APPEND;
    fd = sceIoOpen("ms0:/PSP/GAME/GoTube/modern.log", flags, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, length); sceIoClose(fd); }
    initialized = 1;
}

static int curl_init_once(void)
{
    if (curl_state == 2) return 0;
    if (__sync_bool_compare_and_swap(&curl_state, 0, 1)) {
        curl_state = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 2 : -1;
        return curl_state == 2 ? 0 : -1;
    }
    while (curl_state == 1) sceKernelDelayThread(1000);
    return curl_state == 2 ? 0 : -1;
}

static void curl_common(CURL *curl, const char *url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url);
    /* The PSPDEV curl package carries its Unix build-time CAPATH
     * (/etc/ssl/certs).  mbedTLS treats a missing configured directory as a
     * fatal CURLE_SSL_CACERT_BADFILE even when CAINFO is valid.  Clear it and
     * use only the certificate bundle shipped beside the EBOOT. */
    curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(curl, CURLOPT_CAINFO, CA_FILE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 256L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "com.google.android.apps.youtube.vr.oculus/1.65.10 "
        "(Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip");
}

static size_t body_write(char *ptr, size_t size, size_t count, void *opaque)
{
    CurlBody *body = opaque;
    int bytes = size * count, wanted;
    char *grown;
    if (bytes <= 0 || body->failed) return 0;
    if (body->size + bytes > BODY_LIMIT) { body->failed = 1; return 0; }
    wanted = body->size + bytes + 1;
    if (wanted > body->capacity) {
        int capacity = body->capacity ? body->capacity * 2 : 16384;
        while (capacity < wanted) capacity *= 2;
        grown = realloc(body->data, capacity);
        if (!grown) { body->failed = 1; return 0; }
        body->data = grown; body->capacity = capacity;
    }
    memcpy(body->data + body->size, ptr, bytes);
    body->size += bytes; body->data[body->size] = 0;
    return bytes;
}

char *go_curl_post_json(const char *url, const char *json,
                        const char *visitor, int *size)
{
    CURL *curl;
    CURLcode result;
    long status = 0;
    CurlBody body;
    struct curl_slist *headers = NULL;
    char visitor_header[1024];
    char error[CURL_ERROR_SIZE] = "";
    SceIoStat ca_stat;
    int ca_status;
    if (size) *size = 0;
    memset(&body, 0, sizeof(body));
    if (!url || !json || ensure_network() < 0 || curl_init_once() < 0) return NULL;
    curl = curl_easy_init();
    if (!curl) return NULL;
    memset(&ca_stat, 0, sizeof(ca_stat));
    ca_status = sceIoGetstat(CA_FILE, &ca_stat);
    go_modern_trace("TLS CA file status=%d size=%u",
                    ca_status, (unsigned int)ca_stat.st_size);
    curl_common(curl, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "X-YouTube-Client-Name: 28");
    headers = curl_slist_append(headers, "X-YouTube-Client-Version: 1.65.10");
    if (visitor && visitor[0]) {
        snprintf(visitor_header, sizeof(visitor_header), "X-Goog-Visitor-Id: %s", visitor);
        headers = curl_slist_append(headers, visitor_header);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    go_modern_trace("HTTP POST begin visitor=%d bytes=%d", visitor && visitor[0],
                    (int)strlen(json));
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
    go_modern_trace("HTTP POST end curl=%d status=%ld response=%d limit=%d error=%s",
                    result, status, body.size, body.failed, error);
    if (result != CURLE_OK || status != 200 || body.failed) {
        free(body.data); return NULL;
    }
    if (!body.data) body.data = calloc(1, 1);
    if (size) *size = body.size;
    return body.data;
}

static size_t stream_write(char *ptr, size_t size, size_t count, void *opaque)
{
    CurlStream *stream = opaque;
    int total = size * count, copied = 0;
    while (copied < total && !stream->cancel) {
        int free_bytes = RING_SIZE - stream->used;
        int chunk;
        if (free_bytes <= 0) { sceKernelDelayThreadCB(1000); continue; }
        chunk = total - copied;
        if (chunk > free_bytes) chunk = free_bytes;
        if (chunk > RING_SIZE - stream->write_pos)
            chunk = RING_SIZE - stream->write_pos;
        memcpy(stream->ring + stream->write_pos, ptr + copied, chunk);
        __sync_synchronize();
        stream->write_pos = (stream->write_pos + chunk) % RING_SIZE;
        __sync_add_and_fetch(&stream->used, chunk);
        copied += chunk;
    }
    return stream->cancel ? 0 : total;
}

static int stream_thread(SceSize args, void *argp)
{
    CurlStream *stream = *(CurlStream **)argp;
    CURL *curl = curl_easy_init();
    CURLcode result = CURLE_FAILED_INIT;
    go_modern_trace("STREAM worker begin");
    if (curl) {
        curl_common(curl, stream->url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, stream);
        result = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    stream->error = !stream->cancel && result != CURLE_OK;
    go_modern_trace("STREAM worker end curl=%d cancel=%d", result, stream->cancel);
    stream->done = 1;
    sceKernelExitThread(0);
    return 0;
}

void *go_curl_stream_open(const char *url)
{
    CurlStream *stream;
    if (!url || ensure_network() < 0 || curl_init_once() < 0) return NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->ring = memalign(64, RING_SIZE);
    if (!stream->ring) { free(stream); return NULL; }
    strncpy(stream->url, url, sizeof(stream->url) - 1);
    stream->thread = sceKernelCreateThread("GoTube.curl", stream_thread,
                      0x24, 0x20000, PSP_THREAD_ATTR_USER, NULL);
    if (stream->thread < 0 ||
        sceKernelStartThread(stream->thread, sizeof(stream), &stream) < 0) {
        if (stream->thread >= 0) sceKernelDeleteThread(stream->thread);
        free(stream->ring); free(stream); return NULL;
    }
    return stream;
}

int go_curl_stream_read(void *opaque, unsigned char *buffer, int size)
{
    CurlStream *stream = opaque;
    int copied = 0;
    if (!stream || !buffer || size < 1) return -1;
    while (copied == 0) {
        int available = stream->used, chunk;
        if (available > 0) {
            chunk = available < size ? available : size;
            if (chunk > RING_SIZE - stream->read_pos)
                chunk = RING_SIZE - stream->read_pos;
            memcpy(buffer, stream->ring + stream->read_pos, chunk);
            stream->read_pos = (stream->read_pos + chunk) % RING_SIZE;
            __sync_sub_and_fetch(&stream->used, chunk);
            copied = chunk;
        } else if (stream->done) return stream->error ? -1 : 0;
        else sceKernelDelayThreadCB(1000);
    }
    return copied;
}

void go_curl_stream_close(void *opaque)
{
    CurlStream *stream = opaque;
    if (!stream) return;
    stream->cancel = 1;
    sceKernelWaitThreadEnd(stream->thread, NULL);
    sceKernelDeleteThread(stream->thread);
    free(stream->ring); free(stream);
}

typedef struct { FILE *file; volatile int *progress, *cancel; } Download;
static size_t file_write(char *ptr, size_t size, size_t count, void *opaque)
{
    Download *download = opaque;
    if (download->cancel && *download->cancel) return 0;
    return fwrite(ptr, size, count, download->file);
}
static int download_progress(void *opaque, double total, double now,
                             double ultotal, double ulnow)
{
    Download *download = opaque;
    if (download->progress && total > 0)
        *download->progress = (int)(now * 1000.0 / total);
    return download->cancel && *download->cancel;
}
int go_curl_download(const char *url, const char *path,
                     volatile int *progress, volatile int *cancel)
{
    CURL *curl; CURLcode result; Download download;
    if (!url || !path || ensure_network() < 0 || curl_init_once() < 0) return -1;
    download.file = fopen(path, "wb"); download.progress = progress; download.cancel = cancel;
    if (!download.file) return -1;
    curl = curl_easy_init();
    if (!curl) { fclose(download.file); return -1; }
    curl_common(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, download_progress);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &download);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    result = curl_easy_perform(curl);
    curl_easy_cleanup(curl); fclose(download.file);
    if (result != CURLE_OK) { sceIoRemove(path); return -1; }
    if (progress) *progress = 1000;
    return 0;
}
