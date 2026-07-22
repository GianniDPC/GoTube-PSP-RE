/*
 * GoTube — JS file evaluation
 * Reads a .js file from the Memory Stick and evaluates it in
 * the SpiderMonkey context. Behavioral role supported by GT12-JS-0002;
 * helper-level error semantics remain UNVERIFIED.
 */
#include "gotube.h"

extern void gt_trace(const char *msg);

/* Log the pending JS exception (if any) to the trace file */
static void log_js_error(JSContext *cx, const char *path)
{
    jsval exc;
    gt_trace(path);
    if (JS_GetPendingException(cx, &exc) && JSVAL_IS_STRING(exc)) {
        char *msg = JS_GetStringBytes(JSVAL_TO_STRING(exc));
        gt_trace(msg);
    } else {
        gt_trace("(js error, no exception string)");
    }
    JS_ClearPendingException(cx);
}

int go_evaluate_script(JSContext *cx, const char *path)
{
    SceUID   fd;
    SceOff   size;
    char    *buf;
    jsval    rval;
    JSBool   ok;

    if (!cx || !path)
        return -1;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) {
        return -1;
    }

    size = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (size < 0) {
        sceIoClose(fd);
        return -1;
    }
    sceIoLseek(fd, 0, PSP_SEEK_SET);

    buf = (char *)malloc(size + 1);
    if (!buf) {
        sceIoClose(fd);
        return -1;
    }

    if (sceIoRead(fd, buf, size) != size) {
        free(buf);
        sceIoClose(fd);
        return -1;
    }
    buf[size] = '\0';
    sceIoClose(fd);

    ok = JS_EvaluateScript(cx, JS_GetGlobalObject(cx),
                           buf, (uintN)size, path, 1, &rval);

    if (!ok)
        log_js_error(cx, path);

    free(buf);
    return ok ? 0 : -1;
}
