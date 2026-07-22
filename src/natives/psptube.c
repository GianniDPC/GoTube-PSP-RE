/*
 * GoTube — PSPTube class native methods
 *   encodeURI, decodeURI, decodeHTML, SJIStoUTF8, alert, log
 */
#include <ctype.h>
#include <libccc.h>
#include "gotube.h"
#include <pspiofilemgr.h>
#include <psputility_msgdialog.h>

void go_message_dialog(const char *message)
{
    pspUtilityMsgDialogParams params;
    int status;
    memset(&params,0,sizeof(params));
    params.base.size=sizeof(params);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,&params.base.language);
    params.base.buttonSwap=go_utility_button_swap();
    params.base.graphicsThread=0x11;
    params.base.accessThread=0x13;
    params.base.fontThread=0x12;
    params.base.soundThread=0x10;
    params.mode=PSP_UTILITY_MSGDIALOG_MODE_TEXT;
    strncpy(params.message,message?message:"",sizeof(params.message)-1);
    if(sceUtilityMsgDialogInitStart(&params)<0)return;
    for(;;){
        status=sceUtilityMsgDialogGetStatus();
        if(status==PSP_UTILITY_DIALOG_QUIT)sceUtilityMsgDialogShutdownStart();
        else if(status==PSP_UTILITY_DIALOG_NONE)break;
        sceUtilityMsgDialogUpdate(1);
        sceDisplayWaitVblankStart();
        sceKernelDelayThread(1000);
    }
}

/* --- alert(msg) — debug dialog (UTF-8, max 511 chars matches binary) --- */
JSBool go_alert(JSContext *cx, JSObject *obj, uintN argc,
                jsval *argv, jsval *rval)
{
    char *msg;
    int len;

    if (argc < 1 || !JSVAL_IS_STRING(argv[0]))
        msg = "";
    else
        msg = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));

    len = strlen(msg);
    if (len > 510)
        msg = "Too long string.\nstrings must be under 511 bytes.";

    go_message_dialog(msg);

    *rval = JSVAL_VOID;
    return JS_TRUE;
}

/* --- PSPTube.log(msg) — write debug string to log.txt --- */
JSBool go_log(JSContext *cx, JSObject *obj, uintN argc,
              jsval *argv, jsval *rval)
{
    char *msg;
    int   fd;

    if (argc < 1 || !JSVAL_IS_STRING(argv[0]))
        msg = "(null)";
    else
        msg = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));

    fd = sceIoOpen("ms0:/PSP/GAME/GoTube/log.txt",
                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, strlen(msg));
        sceIoClose(fd);
    }

    *rval = JSVAL_VOID;
    return JS_TRUE;
}

/* --- PSPTube.encodeURI(str) — percent-encode URI component --- */
JSBool go_encodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval)
{
    char       *in, *out, *p;
    int         len;
    JSString   *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    in   = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    len  = strlen(in);
    out  = (char *)malloc(len * 3 + 1);  /* worst case: every byte → %XX */
    if (!out) return JS_TRUE;

    p = out;
    while (*in) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' ||
            c == '*' || c == '(' || c == ')') {
            *p++ = c;
        } else if (c == ' ') {
            *p++ = '+';
        } else {
            *p++ = '%';
            *p++ = "0123456789ABCDEF"[c >> 4];
            *p++ = "0123456789ABCDEF"[c & 15];
        }
    }
    *p = 0;

    jstr = JS_NewStringCopyZ(cx, out);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);
    free(out);
    return JS_TRUE;
}

/* --- PSPTube.decodeURI(str) — percent-decode URI component --- */
JSBool go_decodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval)
{
    char       *in, *out;
    int         len;
    JSString   *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    in  = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    len = strlen(in);
    out = (char *)malloc(len + 1);
    if (!out) return JS_TRUE;

    char *p = out;
    while (*in) {
        if (*in == '%') {
            unsigned char hi = (unsigned char)in[1];
            unsigned char lo = (unsigned char)in[2];
            hi = (hi >= '0' && hi <= '9') ? hi - '0' :
                 (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 :
                 (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : 0;
            lo = (lo >= '0' && lo <= '9') ? lo - '0' :
                 (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 :
                 (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : 0;
            *p++ = (hi << 4) | lo;
            in += 3;
        } else if (*in == '+') {
            *p++ = ' ';
            in++;
        } else {
            *p++ = *in++;
        }
    }
    *p = 0;

    jstr = JS_NewStringCopyZ(cx, out);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);
    free(out);
    return JS_TRUE;
}

/* Exact ordered Cat_HTMLdecode table embedded in the original executable. */
typedef struct { const char *entity; const char *replacement; } GTHtmlEntity;
#include "html_entities.inc"

JSBool go_decodehtml(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval)
{
    char       *in, *out;
    int         len, i;
    JSString   *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    in  = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    len = strlen(in);
    out = (char *)malloc(len + 1);
    if (!out) return JS_TRUE;

    char *p = out;
    while (*in) {
        if (*in == '&') {
            int found = 0;
            for (i = 0; i < (int)(sizeof(gt_html_entities) / sizeof(gt_html_entities[0])); i++) {
                int elen = strlen(gt_html_entities[i].entity);
                if (strncmp(in, gt_html_entities[i].entity, elen) == 0) {
                    int rlen = strlen(gt_html_entities[i].replacement);
                    memcpy(p, gt_html_entities[i].replacement, rlen);
                    p += rlen;
                    in += elen;
                    found = 1;
                    break;
                }
            }
            if (found) continue;
        }
        *p++ = *in++;
    }
    *p = 0;

    jstr = JS_NewStringCopyZ(cx, out);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);
    free(out);
    return JS_TRUE;
}

static int ucs2_to_utf8(cccUCS2 code, unsigned char *out)
{
    if (code < 0x80) {
        out[0] = (unsigned char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = 0xc0 | (code >> 6);
        out[1] = 0x80 | (code & 0x3f);
        return 2;
    }
    out[0] = 0xe0 | (code >> 12);
    out[1] = 0x80 | ((code >> 6) & 0x3f);
    out[2] = 0x80 | (code & 0x3f);
    return 3;
}

JSBool go_sjistoutf8(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval)
{
    char       *in;
    unsigned char *out, *op;
    cccUCS2     *wide;
    int          len, chars, outsz, i;
    JSString    *jstr;

    *rval = JSVAL_NULL;
    if (argc < 1 || !JSVAL_IS_STRING(argv[0])) return JS_TRUE;

    in    = JS_GetStringBytes(JSVAL_TO_STRING(argv[0]));
    len   = strlen(in);
    outsz = len * 3 + 1;  /* worst case: each SJIS char → 3 UTF-8 bytes */
    wide = (cccUCS2 *)malloc((len + 1) * sizeof(*wide));
    op = out = (unsigned char *)malloc(outsz);
    if (!wide || !out) { free(wide); free(out); return JS_TRUE; }

    chars = cccSJIStoUCS2(wide, len + 1, (const cccCode *)in);
    if (chars < 0) chars = 0;
    for (i = 0; i < chars && wide[i]; i++) op += ucs2_to_utf8(wide[i], op);
    *op = 0;

    jstr = JS_NewStringCopyZ(cx, (char *)out);
    if (jstr) *rval = STRING_TO_JSVAL(jstr);
    free(wide);
    free(out);
    return JS_TRUE;
}
