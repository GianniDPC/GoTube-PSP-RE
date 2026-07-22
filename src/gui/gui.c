/*
 * GoTube — GUI subsystem
 * sceGu-based reconstruction with intraFont text.
 *
 * VA 0x0002234c is an initializer, not a renderer (GT12-GUI-0001). The
 * Result-list geometry and strings are recovered from FUN_0001e380.
 */
#include "gotube.h"
#include <pspgu.h>
#include <pspgum.h>
#include <intraFont.h>


#define LCD_W 480
#define LCD_H 272

/* GT12 alternates command lists at BSS 0x6112c0/0x6712c0.  Their address
 * delta is exactly 0x60000 bytes (VAs 0x27448 and 0x270cc). */
static unsigned int __attribute__((aligned(16))) g_lists[2][0x60000 / 4];
static int g_list_index = 0;

intraFont *g_font = NULL;
static int        g_gui_ready = 0;
static int        g_main_state_started = 0;
int               g_osk_mode = 0;
static int g_canvas_w = LCD_W, g_canvas_h = LCD_H, g_buffer_w = 512;
static int g_draw_psm = GU_PSM_8888;
static int g_origin_x = 0, g_origin_y = 0;
static float g_panel_x = 0.0f, g_panel_y = 0.0f;
static float g_menu_x = 480.0f;
static int g_menu_visible_last = 0;
static int g_result_top = 0;
static unsigned int g_result_frame = 0;
static int g_comment_fallback_y = 0;
/* VA 0x27448 submits both alternating display lists through their uncached
 * KSEG1 aliases (list_address | 0x40000000).  This is significant on a real
 * PSP: submitting the cached aliases can make the GE consume earlier command
 * words, which presents as old glyphs surviving inside later UI panels. */
static void *gu_list_uncached(int index)
{
    return (void *)((unsigned int)g_lists[index] | 0x40000000u);
}

int go_gui_width(void) { return g_canvas_w; }
int go_gui_height(void) { return g_canvas_h; }
int go_gui_origin_x(void) { return g_origin_x; }
int go_gui_origin_y(void) { return g_origin_y; }

static void configure_gu(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list_uncached(g_list_index));
    sceGuDrawBuffer(g_draw_psm, (void *)0, g_buffer_w);
    sceGuDispBuffer(g_canvas_w, g_canvas_h,
                    (void *)(g_buffer_w == 768 ? 0xc0000 : 0x88000), g_buffer_w);
    if (g_buffer_w == 512) sceGuDepthBuffer((void *)0x110000, g_buffer_w);
    sceGuOffset(2048 - (g_canvas_w / 2), 2048 - (g_canvas_h / 2));
    sceGuViewport(2048, 2048, g_canvas_w, g_canvas_h);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, g_canvas_w, g_canvas_h);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    /* FUN_00026be8 leaves depth testing disabled.  Cull/clip are never
     * enabled by that initializer; adding them changes the state inherited
     * by intraFont's raw 2-D vertices on real hardware. */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    /* Exact FUN_00026be8 defaults: GE C7 TexWrap(1,1), followed by GE C6
     * TexFilter(1,1).  Clamp is essential for intraFont's glyph atlas on
     * hardware; repeat wrapping turns edge texels into horizontal streaks. */
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuClearColor(0);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuFinish(); sceGuSync(0, 0);
    sceDisplayWaitVblankStart(); sceGuDisplay(1);
    g_list_index = 0;
}

/* --- one-time GUI init --- */
void go_gui_init(void)
{
    configure_gu();
    /* DATA globals 0x5eecd4/0x5eecd8 initialize to 480.0f/32.0f.  The main
     * dispatcher then eases both toward the active layout with 0.4. */
    g_panel_x = 480.0f;
    g_panel_y = 32.0f;

    /* The original boot call loads only jpn0.pgf here (GT12-BOOT-0001). */
    intraFontInit();
    g_font = intraFontLoad("flash0:/font/jpn0.pgf", 0xe000);

    g_gui_ready = 1;
    go_thumbnails_init();
}

void go_gui_set_output(int state)
{
    unsigned int edram;
    if (!g_gui_ready) return;
    sceGuSync(0, 0); sceGuTerm();
    go_video_output_apply(state);
    edram = (unsigned int)sceGeEdramGetAddr() | 0x40000000;
    if (state > 0) {
        g_canvas_w = 720; g_canvas_h = 480; g_buffer_w = 768;
        g_draw_psm = GU_PSM_5650; g_origin_y = 24;
        g_origin_x = state == 1 ? 16 : 40;
        sceDisplaySetFrameBuf((void *)edram, 768, PSP_DISPLAY_PIXEL_FORMAT_565, 1);
    } else {
        g_canvas_w = LCD_W; g_canvas_h = LCD_H; g_buffer_w = 512;
        g_draw_psm = GU_PSM_8888; g_origin_x = g_origin_y = 0;
        sceDisplaySetFrameBuf((void *)edram, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, 1);
    }
    configure_gu();
}

/* --- draw a filled rect (used for progress bar, info panel bg, etc.) --- */
static void fill_rect(int x, int y, int w, int h, unsigned int color)
{
    struct { short x, y, z; } *v;
    /* The GE consumes vertex data asynchronously.  Stack-backed vertices can
     * be overwritten before execution and turn later commands into corrupted
     * sprites.  Keep them in the active GU list, as the native helpers do. */
    v = (void *)sceGuGetMemory(2 * sizeof(*v));
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuColor(color);
    v[0].x = x;      v[0].y = y;      v[0].z = 0;
    v[1].x = x + w;  v[1].y = y + h;  v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
    sceGuEnable(GU_TEXTURE_2D);
}

/* GoTube's statically linked GU wrapper at VA 0x4bad38 accepts an absolute
 * lower-right corner and stores (right-1,bottom-1).  Current PSPSDK's
 * sceGuScissor instead accepts width and height.  Keep renderer call sites in
 * the native coordinate convention and translate at the SDK boundary. */
static void native_scissor(int left, int top, int right, int bottom)
{
    int width = right - left;
    int height = bottom - top;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    sceGuScissor(left, top, width, height);
}

static void line_4444(int x0, int y0, int x1, int y1,
                      unsigned short color)
{
    struct { unsigned short color; short x, y, z; } *v;
    v = (void *)sceGuGetMemory(2 * sizeof(*v));
    v[0].color = v[1].color = color;
    v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_LINES,
                   GU_COLOR_4444 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, v);
    sceGuEnable(GU_TEXTURE_2D);
}

static void line_4444_raw(int x0, int y0, int x1, int y1,
                          unsigned short color)
{
    struct { unsigned short color; short x, y, z; } *v;
    v = (void *)sceGuGetMemory(2 * sizeof(*v));
    v[0].color = v[1].color = color;
    v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_LINES,
                   GU_COLOR_4444 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, v);
}

static void fill_rect_4444_raw(int left, int top, int right, int bottom,
                               unsigned short color)
{
    struct { unsigned short color; short x, y, z; } *v;
    v = (void *)sceGuGetMemory(2 * sizeof(*v));
    v[0].color = v[1].color = color;
    v[0].x = left;  v[0].y = top;    v[0].z = 0;
    v[1].x = right; v[1].y = bottom; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES,
                   GU_COLOR_4444 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, v);
}

/* FUN_0000a9f4 emits four GU_LINES followed by a GU_SPRITES interior:
 * top/left use param_6, right/bottom use param_7, and the inset body uses
 * param_5.  The historical colors are ABGR4444. */
static void framed_rect_4444(int left, int top, int right, int bottom,
                             unsigned short fill_color,
                             unsigned short top_left_color,
                             unsigned short bottom_right_color)
{
    /* FUN_0000a9f4 disables texture once, emits all five primitives, then
     * restores it once.  Avoiding ten redundant state commands per card is
     * also required to remain inside the native frame budget on hardware. */
    sceGuDisable(GU_TEXTURE_2D);
    line_4444_raw(left, top, right, top, top_left_color);
    line_4444_raw(right, top, right, bottom, bottom_right_color);
    line_4444_raw(right, bottom, left, bottom, bottom_right_color);
    line_4444_raw(left, bottom, left, top, top_left_color);
    fill_rect_4444_raw(left + 1, top + 1, right, bottom, fill_color);
    sceGuEnable(GU_TEXTURE_2D);
}

/* VAs 0xa79c/0xa624 texture a repeating 16x1 pattern around an inset border.
 * DAT_005eed00 contains two identical four-pixel highlights separated by four
 * transparent pixels: cfff, ffff, ffff, cfff, 0,0,0,0.  Color 0xf001
 * modulates that white texture into the original dark-red activity outline. */
static void playing_outline_pixel(int x, int y, unsigned int phase)
{
    unsigned int p = phase & 7;
    if (p < 4) {
        unsigned int alpha = (p == 0 || p == 3) ? 0xcc : 0xff;
        fill_rect(x, y, 1, 1, (alpha << 24) | 0x00000011);
    }
}

static void render_playing_outline(int left, int top, int right, int bottom)
{
    int x, y;
    unsigned int phase = g_result_frame;
    unsigned int right_phase = phase + (unsigned int)(right - left);
    unsigned int bottom_phase = right_phase + (unsigned int)(bottom - top);
    unsigned int left_phase = bottom_phase + (unsigned int)(right - left);

    /* The four original calls preserve their direction and advance the
     * texture origin by each preceding edge length (VA 0x1ebc8).  Drawing a
     * generic clockwise perimeter duplicated corners and shifted the dash
     * texture at every edge. */
    for (x = left; x <= right; x++)
        playing_outline_pixel(x, top, phase + (unsigned int)(x - left));
    for (y = top; y <= bottom; y++)
        playing_outline_pixel(right, y,
                              right_phase + (unsigned int)(y - top));
    for (x = right; x >= left; x--)
        playing_outline_pixel(x, bottom,
                              bottom_phase + (unsigned int)(right - x));
    for (y = bottom; y >= top; y--)
        playing_outline_pixel(left, y,
                              left_phase + (unsigned int)(bottom - y));
}

static void print_text(float scale, unsigned int color, float x, float y,
                       const char *text)
{
    intraFontSetStyle(g_font, scale, color, 0, 0);
    intraFontPrint(g_font, x, y, text ? text : "");
}

static void print_text_center(float scale, unsigned int color, float x, float y,
                              const char *text)
{
    intraFontSetStyle(g_font, scale, color, 0,
                      INTRAFONT_ALIGN_CENTER);
    intraFontPrint(g_font, x, y, text ? text : "");
}

static void print_text_right(float scale, unsigned int color, float x, float y,
                             const char *text)
{
    intraFontSetStyle(g_font, scale, color, 0,
                      INTRAFONT_ALIGN_RIGHT);
    intraFontPrint(g_font, x, y, text ? text : "");
}

static void print_text_right_active(float scale, unsigned int color,
                                    float x, float y, const char *text)
{
    intraFontSetStyle(g_font, scale, color, 0,
                      INTRAFONT_ALIGN_RIGHT | INTRAFONT_ACTIVE);
    intraFontPrint(g_font, x, y, text ? text : "");
}

static int render_thumbnail(int index, int left, int top, int selected)
{
    struct { short u, v; short x, y, z; } *verts;
    int width, height, texture_width, texture_height, stride;
    const unsigned short *pixels = go_thumbnail_get(index, &width, &height,
                                                     &texture_width, &texture_height,
                                                     &stride);
    if (!pixels) return 0;
    verts = (void *)sceGuGetMemory(2 * sizeof(*verts));
    verts[0].u = 0;     verts[0].v = 0;
    verts[0].x = left + 6; verts[0].y = top + 6;  verts[0].z = 0;
    verts[1].u = width; verts[1].v = height;
    verts[1].x = left + 134; verts[1].y = top + 94; verts[1].z = 0;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_5551, 0, 0, 0);
    sceGuTexImage(0, texture_width, texture_height, stride, pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
    sceGuColor(selected ? 0xffffffff : 0xff707070);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, verts);
    sceGuColor(0xffffffff);
    return 1;
}

static void render_results(void)
{
    char line[512];
    int i;
    int rows = g_canvas_h / 100;
    int first;
    int top;
    int content_bottom = g_canvas_h == LCD_H ? LCD_H : 440;
    int base_x = (int)g_panel_x, base_y = (int)g_panel_y;
    g_result_frame++;
    /* Native FUN_0001e380 exits before touching GU state when the panel's
     * left edge has moved beyond the active canvas. */
    if (g_panel_x > (float)(g_canvas_w - 1)) return;

    if (rows < 1) rows = 1;
    if (g_result_sel < g_result_top) g_result_top = g_result_sel;
    if (g_result_sel >= g_result_top + rows)
        g_result_top = g_result_sel - rows + 1;
    if (g_result_sel == 0 || g_result_top >= g_result_count) g_result_top = 0;
    first = g_result_top;
    top = base_y + 28;

    /* Exact FUN_0001e380 entry state.  This is deliberately repeated here:
     * the player, thumbnails and intraFont all leave different GU state. */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    native_scissor(0, 0, g_canvas_w, g_canvas_h);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuColor(0xffffffff);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuDisable(GU_COLOR_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_DEPTH_TEST);

    framed_rect_4444(base_x, base_y,
                     base_x + g_canvas_w - 1, base_y + 27,
                     0xffff, 0xfddd, 0xf888);
    /* FUN_0001e380 begins the header with the active descriptor Name.  With
     * MultiView enabled the recovered site.js expands that Name into the
     * scrolling breadcrumb seen in the historical screenshots.  The query
     * and result count occupy the second line.  Putting the descriptor after
     * an initial newline (as the earlier reconstruction did) pushed the only
     * useful text below the 27-pixel header when Favorites was empty. */
    snprintf(line, sizeof(line), "%s",
             g_site_count ? g_site_names[g_site_sel] : "");
    if (g_result_total != 0) {
        char status[192], range[64];
        if (g_result_total < 0)
            snprintf(status, sizeof(status), "\n%s> ?", g_search_keyword);
        else
            snprintf(status, sizeof(status), "\n%s> %d",
                     g_search_keyword, g_result_total);
        strncat(line, status, sizeof(line) - strlen(line) - 1);
        snprintf(range, sizeof(range), "[%d;%d]", g_result_start, g_result_end);
        strncat(line, range, sizeof(line) - strlen(line) - 1);
    } else {
        char query[160];
        snprintf(query, sizeof(query), "\n%s", g_search_keyword);
        strncat(line, query, sizeof(line) - strlen(line) - 1);
    }
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    print_text(0.5f, 0xff101010, base_x + 4, base_y + 12, line);
    print_text_right(0.5f, 0xff101010,
                     base_x + g_canvas_w - 4, base_y + 12,
                     g_net_online ? "\nOnline" : "\n ");

    if (g_result_count == 0 && g_search_status[0])
        print_text(0.6f, 0xff303030, base_x + 24, top + 28,
                   g_search_status);

    for (i = first; i < g_result_count && top < content_bottom; i++, top += 100) {
        GTVideo *video = &g_results[i];
        int text_x = base_x + 140;
        int y;
        int selected = i == g_result_sel;
        int row = i - first;
        /* local_48 starts at zero for the first visible node and toggles per
         * rendered card; it is not the absolute result index. */
        unsigned short c0 = selected ? ((row & 1) ? 0xefff : 0xedef)
                                     : ((row & 1) ? 0xdaaa : 0xd9ab);

        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuColor(0xffffffff);
        framed_rect_4444(base_x, top,
                         base_x + g_canvas_w - 1, top + 100, c0,
                         selected ? 0xa888 : 0x8444,
                         selected ? 0x8444 : 0x8222);

        if (go_player_matches_source(video->url))
            render_playing_outline(base_x + 3, top + 3,
                                   base_x + g_canvas_w - 4, top + 97);

        /* With neither cached texture nor thumbnail source, FUN_0001e380
         * leaves the card gradient visible; it does not draw a placeholder. */
        render_thumbnail(i, base_x, top, selected);

        print_text(0.5f, 0xff00c000, text_x, top + 17, video->title);
        if (video->length) {
            snprintf(line, sizeof(line), "%02d:%02d", video->length / 60, video->length % 60);
            print_text(0.5f, 0xff303030, text_x, top + 29, line);
        }
        if (video->desc[0]) {
            /* FUN_0001e380 clips Description to x=140..canvas-right and
             * y=item+29..item+53, then reserves through row 65 whether the
             * text contains one line or two. */
            native_scissor(text_x, top + 29,
                           g_canvas_w,
                           top + 53 < g_canvas_h ? top + 53 : g_canvas_h);
            print_text(0.5f, 0xff707070, text_x, top + 41, video->desc);
            native_scissor(0, 0, g_canvas_w, g_canvas_h);
            y = top + 65;
        } else {
            y = top + 41;
        }
        if (video->tags[0]) {
            snprintf(line, sizeof(line), "Tags:%s", video->tags);
            print_text(0.5f, 0xff707070, text_x, y, line);
            y += 12;
        }
        if (video->views || video->comments || video->favs) {
            line[0] = 0;
            if (video->views) snprintf(line + strlen(line), sizeof(line) - strlen(line), "Views:%4d ", video->views);
            if (video->comments) snprintf(line + strlen(line), sizeof(line) - strlen(line), "Comments:%4d ", video->comments);
            if (video->favs) snprintf(line + strlen(line), sizeof(line) - strlen(line), "Favorites:%4d ", video->favs);
            print_text(0.5f, 0xff101010, text_x, y, line);
            y += 12;
        }
        if (video->rating_count) {
            snprintf(line, sizeof(line), "rating:%.2f with %d total votes",
                     video->rating, video->rating_count);
            print_text(0.5f, 0xff101010, text_x, y, line);
        }
    }
}

static void render_menu(void)
{
    int i, left;
    unsigned short shade;

    if (!g_menu_visible_last)
        g_menu_x = (float)g_canvas_w;
    if (g_menu_phase == 3) {
        g_menu_x += ((float)g_canvas_w - g_menu_x) * 0.5f;
        if (g_menu_x >= (float)g_canvas_w - 1.0f) {
            g_menu_x = (float)g_canvas_w;
            g_menu_phase = 0;
            g_screen = g_menu_close_target;
        }
    } else if (g_menu_phase == 1) {
        g_menu_x += (400.0f - g_menu_x) * 0.5f;
        if (g_menu_x < 402.0f) {
            g_menu_phase = 2;
        }
    } else if (g_menu_phase == 2) {
        /* State 2 assigns the exact active coordinate before handling input. */
        g_menu_x = 400.0f;
    }
    left = (int)g_menu_x;

    /* FUN_0001ffb8 moves the menu from the active canvas edge toward x=400
     * with DAT_004fadec = 0.5. FUN_0001e0d8 shades from there to the canvas. */
    /* FUN_0001e0d8 starts at ABGR4444 0xf666 and lowers only its alpha
     * nibble on every second column until 0xa666.  The previous 0x90/0x80
     * approximation made the real-PSP menu almost transparent. */
    shade = 0xf000;
    /* FUN_0001e0d8 likewise brackets the complete vertical-stroke loop with
     * one texture disable/restore pair. */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_TEXTURE_2D);
    for (i = left; i < g_canvas_w; i++) {
        line_4444_raw(i, 0, i, g_canvas_h, shade | 0x0666);
        if ((i & 1) && shade > 0xa000) shade -= 0x1000;
    }
    sceGuEnable(GU_TEXTURE_2D);
    for (i = 0; i < g_menu_count; i++) {
        if (g_menu_actions[i] == GT_MENU_TAG_LABEL)
            line_4444(left, 0x28 + g_menu_y[i], g_canvas_w,
                      0x28 + g_menu_y[i], 0xf888);
        else {
            float scale = 0.5f;
            int xoff = 16;
            if (g_menu_actions[i] == GT_MENU_TAG_SEARCH) {
                scale = 0.45f; xoff = 40;
            } else if (g_menu_actions[i] == GT_MENU_SAVE ||
                       g_menu_actions[i] == GT_MENU_RENAME) {
                scale = 0.65f;
            } else if (g_menu_actions[i] == GT_MENU_DELETE) {
                scale = 0.7f;
            }
            if (g_menu_actions[i] == GT_MENU_TAG_SEARCH)
                print_text_center(scale,
                                  i == g_menu_sel ? 0xffffffff : 0xffafafaf,
                                  left + xoff, 0x28 + g_menu_y[i],
                                  g_menu_labels[i]);
            else
                print_text(scale,
                           i == g_menu_sel ? 0xffffffff : 0xffafafaf,
                           left + xoff, 0x28 + g_menu_y[i], g_menu_labels[i]);
        }
    }
    /* Exact tail of FUN_0001e0d8. */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
}

static unsigned int video_clut[5][256] __attribute__((aligned(64)));
static int video_clut_ready;

static unsigned int video_component(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xff000000U | ((unsigned int)b << 16) |
           ((unsigned int)g << 8) | (unsigned int)r;
}

/* FUN_00021958: five 256-entry ABGR lookup tables used by FUN_00023ef8.
 * The first maps studio-range Y to gray.  The remaining additive/subtractive
 * tables apply the recovered BT.601 V and U coefficients. */
static void init_video_clut(void)
{
    int i;
    if (video_clut_ready) return;
    for (i = 0; i < 256; i++) {
        int c = (int)(1.164f * (float)(i - 16));
        int d = i - 128;
        video_clut[0][i] = video_component(c, c, c);
        video_clut[1][i] = video_component(d > 0 ? (int)(1.596f*d) : 0,
                                           d < 0 ? (int)(-0.813f*d) : 0, 0);
        video_clut[2][i] = video_component(d < 0 ? (int)(-1.596f*d) : 0,
                                           d > 0 ? (int)(0.813f*d) : 0, 0);
        video_clut[3][i] = video_component(0,
                                           d < 0 ? (int)(-0.391f*d) : 0,
                                           d > 0 ? (int)(2.018f*d) : 0);
        video_clut[4][i] = video_component(0,
                                           d > 0 ? (int)(0.391f*d) : 0,
                                           d < 0 ? (int)(-2.018f*d) : 0);
    }
    sceKernelDcacheWritebackRange(video_clut, sizeof(video_clut));
    video_clut_ready = 1;
}

static void draw_t8_part(const unsigned char *pixels, int stride,
                         int source_w, int source_h,
                         int left, int top, int draw_w, int draw_h)
{
    struct { short u, v; short x, y, z; } *verts;
    int texture_h = 1;
    while (texture_h < source_h && texture_h < 512) texture_h <<= 1;
    verts = (void *)sceGuGetMemory(2 * sizeof(*verts));
    verts[0].u = 0; verts[0].v = 0;
    verts[0].x = left; verts[0].y = top; verts[0].z = 0;
    verts[1].u = source_w; verts[1].v = source_h;
    verts[1].x = left + draw_w; verts[1].y = top + draw_h; verts[1].z = 0;
    sceGuTexImage(0, 512, texture_h, stride, pixels);
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, verts);
}

static void draw_t8_plane(const unsigned char *pixels, int stride,
                          int source_w, int source_h,
                          int left, int top, int draw_w, int draw_h)
{
    int first = source_w > 512 ? 512 : source_w;
    int first_draw = draw_w * first / source_w;
    draw_t8_part(pixels, stride, first, source_h,
                 left, top, first_draw, draw_h);
    if (source_w > 512)
        draw_t8_part(pixels + 512, stride, source_w - 512, source_h,
                     left + first_draw, top, draw_w - first_draw, draw_h);
}

static int render_video_frame(void)
{
    const unsigned char *y_plane, *v_plane, *u_plane;
    int width, height, y_stride, uv_stride;
    int draw_w, draw_h, left, top, crop = 0;
    if (!go_player_planes(&y_plane, &v_plane, &u_plane,
                          &width, &height, &y_stride, &uv_stride) ||
        width < 1 || height < 1) return 0;
    if (go_player_render_mode() == 0) {
        draw_w = g_canvas_w;
        draw_h = height * g_canvas_w / width;
        if (draw_h > g_canvas_h) {
            draw_w = draw_w * g_canvas_h / draw_h;
            draw_h = g_canvas_h;
        }
        left = (g_canvas_w - draw_w) / 2;
        top = (g_canvas_h - draw_h) / 2;
    } else {
        /* FUN_00023ef8 modes 1..14 fill the active output and crop
         * (mode*4-4) source rows from both vertical edges. */
        crop = go_player_render_mode() * 4 - 4;
        if (crop * 2 >= height) crop = height / 2 - 1;
        if (crop < 0) crop = 0;
        draw_w = g_canvas_w; draw_h = g_canvas_h;
        left = top = 0;
    }
    init_video_clut();
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuDisable(GU_BLEND);
    sceGuClutLoad(32, video_clut[0]);
    draw_t8_plane(y_plane + crop * y_stride, y_stride,
                  width, height - crop * 2, left, top, draw_w, draw_h);

    /* Native plane order is V then U. Each is applied as a positive table
     * followed by a reverse-subtracted negative table. */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
    sceGuClutLoad(32, video_clut[1]);
    draw_t8_plane(v_plane + (crop >> 1) * uv_stride, uv_stride,
                  (width + 1) >> 1, ((height - crop * 2) + 1) >> 1,
                  left, top, draw_w, draw_h);
    sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX,
                   0xffffffff, 0xffffffff);
    sceGuClutLoad(32, video_clut[2]);
    draw_t8_plane(v_plane + (crop >> 1) * uv_stride, uv_stride,
                  (width + 1) >> 1, ((height - crop * 2) + 1) >> 1,
                  left, top, draw_w, draw_h);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
    sceGuClutLoad(32, video_clut[3]);
    draw_t8_plane(u_plane + (crop >> 1) * uv_stride, uv_stride,
                  (width + 1) >> 1, ((height - crop * 2) + 1) >> 1,
                  left, top, draw_w, draw_h);
    sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX,
                   0xffffffff, 0xffffffff);
    sceGuClutLoad(32, video_clut[4]);
    draw_t8_plane(u_plane + (crop >> 1) * uv_stride, uv_stride,
                  (width + 1) >> 1, ((height - crop * 2) + 1) >> 1,
                  left, top, draw_w, draw_h);
    sceGuDisable(GU_BLEND);
    return 1;
}

static void render_comments(void)
{
    int i, now = go_player_time_cs();
    int scroll_lane[16];
    int top_cursor = g_origin_y;
    int bottom_cursor = g_canvas_h - (g_origin_y ? 16 : 0) - 7;
    for (i = 0; i < 16; i++) scroll_lane[i] = -1;
    if (!(go_player_overlay_mode() & 2)) return;
    for (i = 0; i < go_comments_count(); i++) {
        const GTComment *c = go_comments_get(i);
        int elapsed, lane = -1, y, height;
        int safe_width = g_canvas_w - 2 * g_origin_x;
        float scale, width, x;
        if (!c || now < c->vpos || now >= c->vpos + 400) continue;
        elapsed = now - c->vpos;
        scale = c->size == 1 ? 0.625f : (c->size == 2 ? 2.0f : 1.0f);
        intraFontSetStyle(g_font, scale, c->color, 0xff000000, 0);
        width = intraFontMeasureText(g_font, c->text);
        if (width > safe_width && width > 0.0f) {
            scale *= (float)safe_width / width;
            intraFontSetStyle(g_font, scale, c->color, 0xff000000, 0);
            width = intraFontMeasureText(g_font, c->text);
        }
        height = c->size == 1 ? 10 : (c->size == 2 ? 32 : 16);
        if (c->position == 2) {
            x=(g_canvas_w-width)*0.5f;
            y=top_cursor + (c->size == 1 ? 8 :
                            (c->size == 2 ? 32 : 16));
            top_cursor += height;
        }
        else if (c->position == 1) {
            x=(g_canvas_w-width)*0.5f;
            y=bottom_cursor;
            bottom_cursor -= height;
        }
        else {
            int candidate;
            float new_speed = (g_canvas_w + width) / 400.0f;
            /* VA 0x29aa8 keeps sixteen 16-pixel scrolling lanes.  A lane is
             * reusable only if the earlier comment cannot overlap the new
             * trajectory before it leaves the screen.  Big comments reserve
             * two adjacent lanes. */
            for (candidate = 0; candidate < 16; candidate++) {
                int need_two = c->size == 2;
                int old_index = scroll_lane[candidate];
                int safe = 1;
                if (need_two && candidate == 15) continue;
                if (old_index >= 0) {
                    const GTComment *old = go_comments_get(old_index);
                    float old_scale = old->size == 1 ? 0.625f :
                                      (old->size == 2 ? 2.0f : 1.0f);
                    float old_width, old_speed, old_x, gap, horizon;
                    intraFontSetStyle(g_font, old_scale, old->color, 0, 0);
                    old_width = intraFontMeasureText(g_font, old->text);
                    if (old_width > safe_width && old_width > 0.f)
                        old_width = safe_width;
                    old_speed = (g_canvas_w + old_width) / 400.0f;
                    old_x = g_canvas_w - old_speed * (now - old->vpos);
                    gap = g_canvas_w - (old_x + old_width);
                    horizon = (float)(old->vpos + 400 - now);
                    if (gap < 0.f ||
                        (new_speed > old_speed &&
                         gap < (new_speed - old_speed) * horizon)) safe = 0;
                }
                if (safe && need_two && scroll_lane[candidate + 1] >= 0)
                    safe = 0;
                if (safe) { lane = candidate; break; }
            }
            if (lane < 0) {
                int span = (g_canvas_h == LCD_H ? LCD_H : 440) - 24;
                y = g_comment_fallback_y;
                g_comment_fallback_y = (g_comment_fallback_y + 16) % span;
            } else {
                scroll_lane[lane] = i;
                if (c->size == 2 && lane < 15) scroll_lane[lane + 1] = i;
                y = g_origin_y + lane * 16;
            }
            x=g_canvas_w-(g_canvas_w+width)*(float)elapsed/400.0f;
            y += c->size == 1 ? 8 : (c->size == 2 ? 32 : 16);
        }
        if (!(c->position == 0 && c->size == 1)) {
            int shadow = c->size == 2 ? 2 : 1;
            intraFontSetStyle(g_font,scale,0xff000000,0,0);
            intraFontPrint(g_font,x+shadow,y+shadow,c->text);
        }
        intraFontSetStyle(g_font,scale,c->color,0,0);
        intraFontPrint(g_font,x,y,c->text);
    }
}

static void render_player_status(void)
{
    char elapsed[32], total[32];
    const char *speed = go_player_speed_mode() == 5 ? "x0.5" :
                        go_player_speed_mode() == 20 ? "x2" : "";
    int now = go_player_time_cs();
    int duration = go_player_duration_cs();
    int safe_right = g_canvas_w - g_origin_x;
    int right = g_canvas_w;
    int bottom = g_canvas_h - (g_origin_y ? 16 : 0);
    int panel = (int)g_panel_x;
    int bar_left;
    int bar_width;
    /* Dispatcher VA 0x2097c exposes the animated panel edge as the player
     * layer's right bound while it lies inside the safe display. */
    if (panel < g_canvas_w - 1 && panel > g_origin_x) right = panel;
    if (right > safe_right) right = safe_right;
    bar_left = right - 240;
    if (bar_left < 8) bar_left = 8;
    bar_width = right - bar_left - 8;
    if (bar_width > 9) {
        int progress = 0;
        /* FUN_0000acf8: white beveled track, optional buffered portion in
         * 0xffbb, then the playback portion in green ABGR4444. */
        framed_rect_4444(bar_left, bottom - 4,
                         bar_left + bar_width, bottom - 1,
                         0xffff, 0xfbbb, 0xf777);
        if (duration > 0) {
            progress = (int)((long long)(bar_width - 2) * now / duration);
            if (progress < 0) progress = 0;
            if (progress > bar_width - 2) progress = bar_width - 2;
        }
        if (progress != 0) {
            framed_rect_4444(bar_left + 1, bottom - 3,
                             bar_left + 1 + progress, bottom - 2,
                             0xff00, 0xff88, 0xf800);
        }
    }
    snprintf(total, sizeof(total), "/ %02d:%02d",
             duration / 6000, (duration / 100) % 60);
    snprintf(elapsed, sizeof(elapsed), "%02d:%02d%s",
             now / 6000, (now / 100) % 60, speed);
    print_text_right(0.5f, 0xff000000, right - 7, bottom - 5, total);
    print_text_right_active(0.5f, 0xffffffff, right - 8, bottom - 6, total);
    print_text_right_active(0.7f, 0x80000000, right - 47, bottom - 5, elapsed);
    print_text_right_active(0.7f, 0x80000000, right - 49, bottom - 5, elapsed);
    print_text_right_active(0.7f, 0x80000000, right - 47, bottom - 7, elapsed);
    print_text_right_active(0.7f, 0x80000000, right - 49, bottom - 7, elapsed);
    print_text_right_active(0.7f, 0xffffefef, right - 48, bottom - 6, elapsed);
}

/* --- per-frame render --- */
void go_gui_render(void)
{
    float panel_target;
    if (!g_gui_ready)
        return;

    if (g_screen == SCR_SITELIST)
        panel_target = (float)(g_canvas_w - g_origin_x - 160);
    else if (g_screen == SCR_PLAYER)
        panel_target = (float)(g_canvas_w + 2);
    else
        panel_target = (float)g_origin_x;
    g_panel_x += (panel_target - g_panel_x) * 0.4f;
    g_panel_y += ((float)g_origin_y - g_panel_y) * 0.4f;

    sceGuStart(GU_DIRECT, gu_list_uncached(g_list_index));
    /* FUN_00026be8 calls the recovered sceGuClearColor wrapper at VA
     * 0x4b9f3c with zero.  Preserve that black clear behind header/cards. */
    sceGuClearColor(0x00000000);
    sceGuClearDepth(0);
    /* Clear obeys the current scissor rectangle.  Reassert the native full
     * canvas before clearing so a font/description clip from the prior list
     * can never preserve old header or row glyphs in either draw buffer. */
    native_scissor(0, 0, g_canvas_w, g_canvas_h);
    sceGuEnable(GU_SCISSOR_TEST);
    /* Exact argument at VA 0x27448: color buffer plus GU_FAST_CLEAR_BIT. */
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    /* 2D UI: no depth testing so text always draws on top */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* Boot hardware/status descriptor and geometry: GT12-SPLASH-0001. */
    if (go_splash_is_active()) {
        go_splash_render();
        /* Finish frame (skip normal GUI) */
        sceGuFinish();
        /* Frame finalizer VA 0x26784 performs both callback-capable waits,
         * then sceGuSync, then the LCD swap—in precisely this order. */
        sceDisplayWaitVblankStartCB();
        sceDisplayWaitVblankStartCB();
        sceGuSync(0, 0);
        sceGuSwapBuffers();
        g_list_index ^= 1;
        return;
    }

    if (g_screen != SCR_MENU) g_menu_visible_last = 0;
    /* PSP utility dialogs own the visible surface.  GT12 leaves the cleared
     * black GU frame behind Netconf/OSK instead of redrawing its list there;
     * redrawing caused the alternating app/system frames seen on hardware. */
    if (g_font && !go_netconf_is_active()) {
        int video_drawn = 0;
        /* The original loop calls FUN_00023ef8 before the active state's
         * renderer every frame.  Thus video/overlays remain beneath a list
         * panel while it eases among full, multiview and off-screen targets. */
        if (g_screen == SCR_RESULTS || g_screen == SCR_MENU ||
            g_screen == SCR_SITELIST || g_screen == SCR_PLAYER) {
            if (go_player_state() == 1 || go_player_state() == 2 ||
                go_player_state() == 3) {
                video_drawn = render_video_frame();
                if (video_drawn) {
                    /* FUN_00023ef8 restores the 2D/font renderer after its
                     * planar YUV passes and before drawing comments/status.
                     * intraFontPrint() does not activate its atlas itself;
                     * without this, it samples the final T8 video plane and
                     * CLUT, producing the blocky white status glyphs seen on
                     * real hardware. */
                    intraFontActivate(g_font);
                    sceGuEnable(GU_BLEND);
                    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA,
                                   GU_ONE_MINUS_SRC_ALPHA, 0, 0);
                    sceGuColor(0xffffffff);
                    if (go_player_overlay_mode() & 2) render_comments();
                    if (go_player_overlay_mode() & 1) render_player_status();
                }
            }
            render_results();
        }
        if (g_screen == SCR_MENU) {
            render_menu();
            g_menu_visible_last = 1;
        }
        else if (g_screen == SCR_SEARCHING) {
            /* Modern TLS requests run off the render thread.  Keep the PSP
             * responsive and make a slow/failing network operation explicit. */
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA,
                           GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            print_text(0.7f, 0xffffffff, g_origin_x + 150,
                       g_origin_y + 136,
                       g_search_status[0] ? g_search_status : "Searching...");
        }
    }

    sceGuFinish();

    /* OSK overlay: update + handle transitions OUTSIDE the GU frame
     * (after sceGuFinish, before vblank/swap — common homebrew pattern
     * to avoid GU state conflicts on real hardware) */
    if (g_screen == SCR_OSK) {
        int st = go_osk_update();
        if (st == 2) {                    /* OSK_STATE_DONE */
            char entered[384];
            go_osk_get_text(entered, sizeof(entered));
            if (g_osk_mode == 1) {
                strncpy(g_search_keyword, entered, sizeof(g_search_keyword) - 1);
                g_search_keyword[sizeof(g_search_keyword) - 1] = 0;
                strncpy(g_video_url, g_search_keyword, sizeof(g_video_url) - 1);
                g_video_url[sizeof(g_video_url) - 1] = 0;
                go_thumbnails_suspend(1);
                go_player_set_source_url(NULL);
                if (go_player_start(g_video_url) == 0)
                    g_screen = SCR_PLAYER;
                else {
                    go_thumbnails_suspend(0);
                    g_screen = g_menu_return_screen;
                }
            } else if (g_osk_mode == 2) {
                go_save_start(g_cx, entered);
                g_screen = g_menu_return_screen;
            } else if (g_osk_mode == 3) {
                if (g_result_count > 0)
                    go_local_rename(&g_results[g_result_sel], entered);
                g_screen = g_menu_return_screen;
            } else {
                strncpy(g_search_keyword, entered, sizeof(g_search_keyword) - 1);
                g_search_keyword[sizeof(g_search_keyword) - 1] = 0;
                go_search_page(1);
            }
            g_osk_mode = 0;
        } else if (st == 3) {             /* OSK_STATE_CANCELLED */
            g_screen = g_osk_mode != 0 ? g_menu_return_screen : SCR_RESULTS;
            g_osk_mode = 0;
        } else if (st == 0) {             /* OSK_STATE_IDLE (failed) */
            if (g_osk_mode != 0) {
                g_screen = g_menu_return_screen;
                g_osk_mode = 0;
            } else {
                strcpy(g_search_keyword, "test");
                go_search_page(1);
            }
        }
    }

    /* WiFi configuration dialog overlay: shown after splash ends, before
     * main screen. Renders the PSP system SSID selector via sceUtility. */
    if (go_netconf_is_active()) {
        int st = go_netconf_update();
        if (st == 2 || st == 3) {  /* DONE (connected) or CANCELLED */
            g_netconf_done = 1;
        }
    }
    if (g_netconf_done && !g_main_state_started) {
        g_main_state_started = 1;
        g_screen = SCR_RESULTS;
        if (g_netconf_connected && g_site_count > 0) {
            /* Main-state init VA 0x1e05c selects site zero and dispatches the
             * literal initial keyword at VA 0x50098c: "PSP", page 1. */
            strcpy(g_search_keyword, "PSP");
            go_thumbnails_reset();
            g_site_sel = 0;
            if (go_source_is_favorites() || go_source_is_playlist())
                g_result_count = go_source_load(1);
            else if (go_source_is_onsen())
                g_result_count = go_onsen_search(g_search_keyword);
            else
                g_result_count = go_callgate_search(g_cx, g_site_names[0],
                                                    g_search_keyword, 1);
            g_result_sel = 0;
        }
    }

    /* FUN_00026784 uses wait, wait, sync, swap.  Synchronizing before the
     * waits changed which completed draw buffer was exposed beneath the
     * translucent menu and produced alternating text remnants on hardware. */
    sceDisplayWaitVblankStartCB();
    sceDisplayWaitVblankStartCB();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
    g_list_index ^= 1;
}

void go_gui_shutdown(void)
{
    if (g_font) {
        intraFontUnload(g_font);
        g_font = NULL;
    }
    intraFontShutdown();
    sceGuTerm();
    g_gui_ready = 0;
}
