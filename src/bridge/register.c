/*
 * GoTube — JS native registration
 * Reconstruction of the native registration surface at VA 0x0001bdd4.
 * Names, arities and the seven-entry property table are supported by
 * GT12-JS-0001 and raw table VA 0x5eeb28.
 *
 * Contract from binary analysis + JS corpus:
 *
 *   GLOBAL:
 *     GetContents(url)       → HTTP GET  (sceHttp)
 *     PostContents(url,data) → HTTP POST (sceHttp)
 *     alert(msg)             → debug dialog
 *
 *   PSPTube object (JS_DefineObject, class "ClassPSPTube"):
 *     PSPTube.encodeURI(str)
 *     PSPTube.decodeURI(str)
 *     PSPTube.decodeHTML(str)
 *     PSPTube.SJIStoUTF8(str)
 *     PSPTube.alert(msg)
 *     PSPTube.log(msg)       → writes to log.txt  (PSPTube++ docs confirm)
 *
 * The property names, tiny IDs and permanent flags are recovered directly.
 */
#include <jsapi.h>
#include <string.h>

extern void go_player_set_render_mode(int mode);

char g_favorites[256] = "";
int g_video_out_mode = 0;
int g_screen_zoom = 0;
static char g_nicomail[256], g_nicopass[256];
static char g_dailyname[256], g_dailypass[256];

static JSBool psptube_set_property(JSContext *cx, JSObject *obj, jsval id,
                                   jsval *vp)
{
    int tiny = JSVAL_IS_INT(id) ? JSVAL_TO_INT(id) : -1;
    if (tiny >= 0 && tiny <= 4) {
        JSString *s = JS_ValueToString(cx, *vp);
        const char *text = s ? JS_GetStringBytes(s) : NULL;
        char *dst = tiny == 0 ? g_favorites : tiny == 1 ? g_nicomail :
                    tiny == 2 ? g_nicopass : tiny == 3 ? g_dailyname :
                    g_dailypass;
        if (text && text[0]) {
            strncpy(dst, text, 255);
            dst[255] = 0;
        }
    } else if (tiny == 5 || tiny == 6) {
        int32 value;
        if (JS_ValueToECMAInt32(cx, *vp, &value)) {
            if (tiny == 5) g_video_out_mode = value;
            else {
                g_screen_zoom = value;
                go_player_set_render_mode(value);
            }
        }
    }
    return JS_TRUE;
}

static JSBool psptube_resolve(JSContext *cx, JSObject *obj, jsval id,
                              uintN flags, JSObject **objp)
{
    *objp = NULL;
    return JS_TRUE;
}

void go_sync_config(JSContext *cx)
{
    JSObject *global = JS_GetGlobalObject(cx);
    jsval ctorval, value;
    JSObject *ctor;
    if (!global || !JS_GetProperty(cx, global, "PSPTube", &ctorval) ||
        !JSVAL_IS_OBJECT(ctorval)) return;
    ctor = JSVAL_TO_OBJECT(ctorval);
    if (JS_GetProperty(cx, ctor, "favorites", &value) && JSVAL_IS_STRING(value)) {
        const char *text = JS_GetStringBytes(JSVAL_TO_STRING(value));
        if (text && text[0]) {
            strncpy(g_favorites, text, sizeof(g_favorites) - 1);
            g_favorites[sizeof(g_favorites) - 1] = 0;
        }
    }
    if (JS_GetProperty(cx, ctor, "VideoOutMode", &value) && JSVAL_IS_INT(value))
        g_video_out_mode = JSVAL_TO_INT(value);
    if (JS_GetProperty(cx, ctor, "ScreenZoom", &value) && JSVAL_IS_INT(value)) {
        g_screen_zoom = JSVAL_TO_INT(value);
        go_player_set_render_mode(g_screen_zoom);
    }
}

/* --- Forward declarations for native implementations --- */
JSBool go_getcontents(JSContext *cx, JSObject *obj, uintN argc,
                      jsval *argv, jsval *rval);
JSBool go_postcontents(JSContext *cx, JSObject *obj, uintN argc,
                       jsval *argv, jsval *rval);
JSBool go_alert(JSContext *cx, JSObject *obj, uintN argc,
                jsval *argv, jsval *rval);
JSBool go_encodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval);
JSBool go_decodeuri(JSContext *cx, JSObject *obj, uintN argc,
                    jsval *argv, jsval *rval);
JSBool go_decodehtml(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval);
JSBool go_sjistoutf8(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval);
JSBool go_log(JSContext *cx, JSObject *obj, uintN argc,
              jsval *argv, jsval *rval);

/* --- PSPTube class definition --- */
static JSClass psptube_class = {
    "ClassPSPTube",
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub, JS_PropertyStub,
    JS_PropertyStub, psptube_set_property,
    JS_EnumerateStub, (JSResolveOp)psptube_resolve,
    JS_ConvertStub,  JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static JSPropertySpec psptube_properties[] = {
    { "favorites",    0, JSPROP_PERMANENT, NULL, NULL },
    { "nicomail",     1, JSPROP_PERMANENT, NULL, NULL },
    { "nicopass",     2, JSPROP_PERMANENT, NULL, NULL },
    { "dailyname",    3, JSPROP_PERMANENT, NULL, NULL },
    { "dailypass",    4, JSPROP_PERMANENT, NULL, NULL },
    { "VideoOutMode", 5, JSPROP_PERMANENT, NULL, NULL },
    { "ScreenZoom",   6, JSPROP_PERMANENT, NULL, NULL },
    { NULL, 0, 0, NULL, NULL }
};

/* --- Native registration entry point --- */
int register_js_natives(JSContext *cx)
{
    JSObject *global;
    JSObject *psptube;

    if (!cx)
        return -1;

    global = JS_GetGlobalObject(cx);
    if (!global)
        return -1;

    /* Global natives */
    JS_DefineFunction(cx, global, "GetContents",  go_getcontents,  1, 0);
    JS_DefineFunction(cx, global, "PostContents", go_postcontents, 2, 0);
    JS_DefineFunction(cx, global, "alert",        go_alert,        1, 0);

    /* FUN_0001bdd4 calls the SpiderMonkey JS_DefineObject-shaped helper with
     * public name PSPTube, internal class ClassPSPTube, null prototype and
     * permanent (4) attributes.  PSPTube is an object, not a constructor. */
    psptube = JS_DefineObject(cx, global, "PSPTube", &psptube_class,
                              NULL, JSPROP_PERMANENT);
    if (!psptube) {
        return -1;
    }

    /* Define the recovered property table and methods directly on that
     * object, as the original registration wrapper does. */
    {
        if (!JS_DefineProperties(cx, psptube, psptube_properties))
            return -1;

        JS_DefineFunction(cx, psptube, "encodeURI",  go_encodeuri,  1, 0);
        JS_DefineFunction(cx, psptube, "decodeURI",  go_decodeuri,  1, 0);
        JS_DefineFunction(cx, psptube, "decodeHTML", go_decodehtml, 1, 0);
        JS_DefineFunction(cx, psptube, "SJIStoUTF8", go_sjistoutf8, 1, 0);
        JS_DefineFunction(cx, psptube, "alert",      go_alert,      1, 0);
        JS_DefineFunction(cx, psptube, "log",        go_log,        1, 0);
    }

    return 0;
}
