/* Couch's pages (page.h): the frame every page shares, the header with the
 * clock, the slides, the ambient mode, the bottom bar. The News page
 * is reader.c's since 0.3.1. Every motion is a whole pixel before a text,
 * digits or S/M icon call (design 1.12), except the icons that move
 * (icon_rot binds them bilinear), the ticker's text (marquee.c) and the
 * reader's scrolls (stext.c). Every time comes from sky_time (the system
 * clock since the start: the preview's frame clock). Only the main thread
 * runs here. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fx.h>
#include <ps3gfx/digits.h>
#include <ps3gfx/icons.h>
#include "couch.h"
#include "page.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "parse.h"
#include "spu_fx_run.h"
#include "sky.h"
#include "start.h"
#include "reader.h"

#define DIM_LAYER 0x000000a8         /* RSX only: black at 66 %, the picture at a third of its light */
#define DOT_R 7
#define DOT_STEP 24
#define SLIDE_S 0.28                 /* a page's slide (design 3.8) */
#define PI_F 3.14159265f

int page = PAGE_BOARD;
const char *const page_name[PAGE_N] = {"Board", "News", "Info"};
int ambient;
int page_dx;

static s64 idle_since, amb_since;
static int amb_saved, amb_dx, amb_dy;
static long amb_step = -1;

static float rnd(float x) { return floorf(x + 0.5f); }
static float asc_of(int line) { return (float)(int)((float)line * 0.797f + 0.5f); }

int page_wide(void) { return SH >= 980; }
float page_top(void) { return page_wide() ? 152 : 132; }
float page_bar_y(void) { return (float)SH - (page_wide() ? 52 : 48); }

/* ---- the moves of the canvas ------------------------------------------------------- */

static struct {
    int l, t, r, b, dx;
} shs[4];
static int nsh;

void shift_push(int dx, int dy)
{
    if (nsh >= 4) return;
    shs[nsh].l = safe_l, shs[nsh].t = safe_t, shs[nsh].r = safe_r, shs[nsh].b = safe_b, shs[nsh].dx = page_dx;
    nsh++;
    if (!dx && !dy) return;
    safe_l += dx, safe_r += dx, safe_t += dy, safe_b += dy;
    page_dx += dx;
    safe_apply();
}

void shift_pop(void)
{
    if (nsh <= 0) return;
    nsh--;
    int moved = safe_l != shs[nsh].l || safe_t != shs[nsh].t;
    safe_l = shs[nsh].l, safe_t = shs[nsh].t, safe_r = shs[nsh].r, safe_b = shs[nsh].b, page_dx = shs[nsh].dx;
    if (moved) safe_apply();
}

/* ---- what the pages share ---------------------------------------------------------- */

u32 glass_tint(void) { return GLASS_TINT | (u32)(0x73 + (0x9c - 0x73) * fx_sc.light); }

void glass_tile(float x, float y, float w, float h)
{
    fx_glass(x, y, w, h, TR, glass_tint());
    round_frame(x, y, w, h, TR, 1, GLASS_EDGE);
    rect(x + TR, y + h - 1, w - 2 * TR, 1, GLASS_SHADE);
}

/* The skeleton: the bar, and the light band where it crosses it (a quad
 * pair whose alpha rises to the band's middle and falls: no hard edge, so
 * it moves by fractions of a pixel). */
void skel_bar(float x, float y, float w, float h)
{
    float ox, oy, r = h / 2;
    round_rect(x, y, w, h, r, 0xffffff1a);
    screen_origin(&ox, &oy);
    const float half = 180, period = 1.6f;
    float c = ox - half + (float)fmod(sky_time(), period) / period * (1920 + 2 * half);
    float a0 = c - half, a1 = c + half, x0 = x + r, x1 = x + w - r;
    if (a1 <= x0 || a0 >= x1) return;
    gfx_texture(NULL);
    for (int k = 0; k < 2; k++) {                            /* the rising half, then the falling half */
        float s0 = k ? c : a0, s1 = k ? a1 : c;
        float q0 = s0 < x0 ? x0 : s0, q1 = s1 > x1 ? x1 : s1;
        if (q1 <= q0) continue;
        float f0 = (q0 - a0) / half, f1 = (q1 - a0) / half;     /* 0 to 2 across the band */
        u32 c0 = 0xffffff00u | (u32)(0x2a * (f0 <= 1 ? f0 : 2 - f0)), c1 = 0xffffff00u | (u32)(0x2a * (f1 <= 1 ? f1 : 2 - f1));
        gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
        q = gfx_put(q, q0, y, c0, 0, 0);
        q = gfx_put(q, q1, y, c1, 0, 0);
        q = gfx_put(q, q1, y + h, c1, 0, 0);
        gfx_put(q, q0, y + h, c0, 0, 0);
    }
}

u32 source_colour(const char *s)
{
    if (!strncmp(s, "Clar", 4)) return CLARIN;
    if (!strncmp(s, "Infobae", 7)) return INFOBAE;
    if (!strncmp(s, "Ambito", 6) || !strncmp(s, "\xc3\x81mbito", 7)) return AMBITO;
    if (!strncmp(s, "BBC", 3)) return BBC;
    return 0x3a5a8cff;
}

/* The width footer_at gives its items: a face button 44 px, a label its
 * text and 30, each action its text and 36. */
static float footer_w(const char *s)
{
    float w = 0;
    char item_s[64];
    while (*s) {
        const char *e = strstr(s, "  ");
        int n = e ? (int)(e - s) : (int)strlen(s);
        snprintf(item_s, sizeof item_s, "%.*s", n, s);
        char *sp = strchr(item_s, ' ');
        if (sp) *sp = 0;
        w += !strcmp(item_s, "SQUARE") || !strcmp(item_s, "CIRCLE") || !strcmp(item_s, "CROSS") || !strcmp(item_s, "TRIANGLE")
                 ? 44 : (float)text_w(F_SMALL, item_s) + 30;
        if (sp) w += (float)text_w(F_SMALL, sp + 1) + 36;
        s += n;
        while (*s == ' ') s++;
    }
    return w - 36;
}

/* The bottom bar (design 1.2): the left words (and a note after them in
 * YELLOW: board_url.txt's), the buttons on the right. */
void bar_draw(float y, const char *left, u32 color, const char *note, const char *hints)
{
    float hw = footer_w(hints), hx = SW - MG - hw;
    footer_at(hx, y + 4, hints);
    float room = hx - 30 - MG;
    float xe = text_fit(MG, y + 8, F_SMALL, color, room, "%s", left);
    if (note && note[0] && room - (xe - MG) > 200) text_fit(xe + 16, y + 8, F_SMALL, YELLOW, room - (xe - MG) - 16, "%s", note);
}

/* ---- the header: the clock ---------------------------------------------------- */

/* The internet's dot colour and words, from the Board thread's last fetches. */
static u32 net_words(char *buf, int n)
{
    char t[24];
    if (net_state != NET_UP) {
        snprintf(buf, (size_t)n, "No network");
        return RED;
    }
    if (board_fetches && !net_has_ip()) {
        snprintf(buf, (size_t)n, "No network: no IP address");
        return RED;
    }
    switch (board_net_q) {
    case NQ_OK: snprintf(buf, (size_t)n, "Internet ok"); return GREEN;
    case NQ_SLOW: snprintf(buf, (size_t)n, "Internet slow (%.1f s)", (double)board_net_us / 1e6); return YELLOW;
    case NQ_DOWN:
        clock_words(t, sizeof t, board_net_down_utc, 0);
        snprintf(buf, (size_t)n, "No internet since %s", t);
        return RED;
    default: snprintf(buf, (size_t)n, "Internet: checking"); return DIM;
    }
}

/* The right column: the page's name and its dots; under them the city, the
 * wifi icon and the internet's words with a dot. Its left edge. */
static float header_right(float y)
{
    char net[64];
    float r = SW - MG, dots = PAGE_CYCLE * DOT_STEP - (DOT_STEP - 2 * DOT_R);
    float y1 = y, y2 = y + (float)line_h(F_MED) + 8;
    for (int i = 0; i < PAGE_CYCLE; i++) {   /* the Info page: no dot lit */
        float cx = r - dots + DOT_R + i * DOT_STEP, cy = y1 + (float)line_h(F_MED) / 2;
        if (i == page) aa_disc(cx, cy, DOT_R, BLUE);
        else aa_ring(cx, cy, DOT_R - 1, 2, DIM);
    }
    float w1 = (float)text_w(F_MED, page_name[page]) + 18 + dots;
    text_r(r - dots - 18, y1, F_MED, WHITE, "%s", page_name[page]);
    u32 c = net_words(net, sizeof net);
    text_r(r, y2, F_SMALL, GREY, "%s", net);
    float nw = (float)text_w(F_SMALL, net), my = y2 + (float)line_h(F_SMALL) / 2;
    aa_disc(r - nw - 14, my, 6, c);
    float w2 = nw + 26;
    if (!icons_rc) {
        icon(net_state == NET_UP && board_net_q != NQ_DOWN ? IC_S_WIFI_HIGH : IC_S_WIFI_SLASH, r - w2 - 18, my, c == DIM ? DIM : GREY);
        w2 += 40;
    }
    if (board_have && board_cur.city[0]) {   /* at most 560 px: a long CITY ends in "..." and leaves the clock its room */
        float cw = (float)text_w(F_SMALL, board_cur.city);
        if (cw > 560) cw = 560;
        text_fit(r - w2 - 14 - cw, y2, F_SMALL, WHITE, cw, "%s", board_cur.city);
        w2 += 14 + cw;
    }
    return r - (w1 > w2 ? w1 : w2);
}

/* The clock: HH:MM in the display digits, whole (the loading screen's and
 * the ambient mode's font), no seconds (Lucas, 2026-10-15: the flip cards
 * looked cut in half). At the start it fades in with the header. */
static float big_clock(float x, float y, float right)
{
    int off, src = clock_offset(&off);
    civil_time c;
    const char *ampm;
    char hm[16], line[200];
    civil_from_unix(clock_utc() + off, &c);
    clock_hm(hm, sizeof hm, c.hour, c.min, clock_h24(), &ampm);
    float mid = (float)digits_mid(D_L);
    float xe = digits(x, y, D_L, WHITE, hm);
    float base = y + asc_of(digits_h(D_L)), dy = base - asc_of(line_h(F_MED));
    if (ampm) xe = text(xe + 12, rnd(dy), F_MED, GREY, "%s", ampm);
    float dx = xe + 36, dw = right - 40 - dx, ty = y + mid - (float)line_h(F_MED) + 2;
    char date[64];
    snprintf(date, sizeof date, "%s %d %s", weekday_name[c.wday], c.day, month_name[c.month - 1]);
    if (text_w(F_MED, date) > dw) snprintf(date, sizeof date, "%d %s", c.day, month_name[c.month - 1]);   /* narrow: no weekday */
    text_fit(dx, rnd(ty), F_MED, WHITE, dw, "%s", date);
    clock_source(line, sizeof line);
    text_fit(dx, rnd(ty + (float)line_h(F_MED) + 6), F_SMALL, src == CLK_UTC ? YELLOW : GREY, dw, "%s", line);
    return xe;
}

static void header(void)
{
    int dy = (int)rnd(-SA_DROP * (1 - start_k(SA_HEADER)));
    shift_push(0, dy);
    float right = header_right(page_wide() ? 22 : 16);
    if (!digits_rc) big_clock(MG, page_wide() ? 4 : 0, right);
    else clock_draw(MG, 20, right - 40 - MG);                     /* no display digits: 0.2.0's clock */
    shift_pop();
}

/* ---- the frame ----------------------------------------------------------------------- */

static int sl_from = -1, sl_dir;
static double sl_t0;

void page_slide(int from, int to, int dir)
{
    (void)to;
    if (from == to) return;
    sl_from = from;
    sl_dir = dir;
    sl_t0 = sky_time();
}

void page_slide_end(void) { sl_from = -1; }

/* 0 to 1 of the slide, eased; -1 when none */
static float slide_k(void)
{
    if (sl_from < 0) return -1;
    double x = (sky_time() - sl_t0) / SLIDE_S;
    if (x >= 1 || fx_reduced()) {
        sl_from = -1;
        return -1;
    }
    if (x < 0) x = 0;
    x = 1 - x;
    return (float)(1 - x * x * x);
}

static void body(int pg, int dx)
{
    shift_push(dx, 0);
    if (pg == PAGE_INFO) info_body();
    else if (pg == PAGE_NEWS) news_body();
    else board_body();
    shift_pop();
}

static void bar_of(int pg, const char *msg, u32 msg_color)
{
    float y = page_bar_y();
    int dy = (int)rnd(SA_RISE * (1 - start_k(SA_BOTTOM)));
    shift_push(0, dy);
    if (pg == PAGE_INFO) info_bar(y);
    else if (pg == PAGE_NEWS) news_bar(y);
    else board_bar(y, msg, msg_color);
    shift_pop();
}

/* The sky: the gradient, the sky's objects (the stars, the moon, the sun's
 * glow and rays), the clouds; the rain; a bolt and its flash; the scrims
 * behind the header and the bottom bar. */
static void sky(void)
{
    fx_draw_gradient();
    sky_behind();
    fx_draw_clouds();
    fx_draw_rain(1.0f);
    sky_front();
    if (fx_state != FXS_NONE) sky_scrims();
}

static void ambient_frame(void);

static void draw_frame(const char *msg, u32 msg_color)
{
    if (ambient_aurora()) {
        ambient_frame();
        return;
    }
    int shifted = ambient && (amb_dx || amb_dy);             /* RSX only: 0.2.0's mode, the whole picture moves */
    if (shifted) shift_push(amb_dx, amb_dy);
    begin_frame();
    sky();
    header();
    float k = slide_k();
    if (k >= 0) {                                            /* the old page out, the new one in (design 3.8) */
        int d = (int)rnd((float)SW * k);
        body(sl_from, -sl_dir * d);
        body(page, sl_dir * (SW - d));
    } else body(page, 0);
    bar_of(page, msg, msg_color);
    if (start_anim_on()) {                                   /* the start: the sky from the loading screen's colours */
        float f = start_k(SA_FADE), ox, oy;
        u32 a = (u32)(255 * (1 - f));
        if (a) {
            screen_origin(&ox, &oy);
            rect_v(ox, oy, 1920, 1080, (LOADING_TOP & ~0xffu) | a, (LOADING_BOT & ~0xffu) | a);
        }
    }
    if (ambient) {
        float ox, oy;
        screen_origin(&ox, &oy);
        rect(ox - AMBIENT_PX, oy - AMBIENT_PX, 1920 + 2 * AMBIENT_PX, 1080 + 2 * AMBIENT_PX, DIM_LAYER);
    }
    ui_flip();
    if (shifted) shift_pop();
}

void page_draw(void) { draw_frame(NULL, 0); }
void page_draw_msg(const char *msg, u32 color) { draw_frame(msg, color); }

/* ---- the ambient mode ---------------------------------------------------------------- */

int ambient_aurora(void) { return ambient && fx_state == FXS_SPU; }

int ambient_input(void)
{
    s64 now = sysGetSystemTime();
    if (!idle_since) idle_since = now;
    if (!pressed) return 0;
    idle_since = now;
    if (!ambient) return 0;
    ambient = 0;
    amb_step = -1;
    amb_dx = amb_dy = 0;
    page = amb_saved;
    page_slide_end();
    couch_log("ambient: off after %.0f s, a button", (double)(now - amb_since) / 1e6);
    pressed = 0;                             /* the press woke the screen: no action, no exit */
    armed = 0;
    return 1;
}

void ambient_tick(void)
{
    s64 now = sysGetSystemTime();
    if (!idle_since) idle_since = now;
    int wait = reader_reading() ? AMBIENT_READ_SEC : AMBIENT_SEC;   /* an open article: 20 min (stage 11) */
    if (!ambient && now - idle_since >= wait * 1000000LL) {
        ambient = 1;
        amb_since = now;
        amb_saved = page;
        amb_step = -1;
        page_slide_end();
        couch_log("ambient: on after %d min with no button%s (%s)", wait / 60, wait != AMBIENT_SEC ? ", an article open" : "",
                  ambient_aurora() ? "the aurora" : "RSX only: dimmed");
    }
    if (!ambient) return;
    long step = (long)((now - amb_since) / (AMBIENT_STEP_SEC * 1000000LL));
    if (step == amb_step) return;
    amb_step = step;
    if (ambient_aurora()) {                  /* the aurora: the block moves (ambient_frame), the page stays */
        amb_dx = amb_dy = 0;
        page = amb_saved;
        return;
    }
    ambient_shift(step, &amb_dx, &amb_dy);
    page = ambient_page(step, board_have && board_cur.n_news > 0) ? PAGE_NEWS : PAGE_BOARD;
}

/* The aurora (design 3.12): the SPUs' picture over the screen (its mean
 * brightness at most 20 %), the rain or the snow dimmer over it, and the
 * block: the clock in D_XL GREY, the date in F_MED, a line in F_BODY that
 * changes each minute (the weather, Blue's price, the newest headline). The
 * block moves to the next of 9 places each minute, gliding there over 6 s
 * (parse.c's ambient_place). */
static void ambient_frame(void)
{
    int off;
    civil_time c;
    const char *ampm;
    char hm[16], date[64], line[360];
    const board_data *b = &board_cur;
    begin_frame();
    if (!fx_draw_aurora()) {                 /* no picture yet (its first frames): the night sky's colours */
        float ox, oy;
        screen_origin(&ox, &oy);
        rect_v(ox, oy, 1920, 1080, 0x040814ff, 0x0a1222ff);
    }
    fx_draw_rain(0.5f);
    double t = (double)(sysGetSystemTime() - amb_since) / 1e6;
    float px, py;
    ambient_place(t, &px, &py);
    clock_offset(&off);
    civil_from_unix(clock_utc() + off, &c);
    clock_hm(hm, sizeof hm, c.hour, c.min, clock_h24(), &ampm);
    snprintf(date, sizeof date, "%s %d %s", weekday_name[c.wday], c.day, month_name[c.month - 1]);
    unsigned has = 0;
    const board_usd *blue = NULL;
    for (int i = 0; board_have && i < b->n_usd; i++)
        if (!strcmp(b->usd[i].label, "Blue")) blue = &b->usd[i];
    if (board_have && b->has_wx) has |= 1;
    if (blue && strcmp(blue->sell, "-")) has |= 2;
    if (board_have && b->n_news) has |= 4;
    int l = ambient_line((long)(t / AMBIENT_STEP_SEC), has);
    line[0] = 0;
    if (l == 0) snprintf(line, sizeof line, "%s, %s", b->temp, b->words);
    else if (l == 1) snprintf(line, sizeof line, "Blue %s", blue->sell);
    else if (l == 2) snprintf(line, sizeof line, "%s: %s", b->news[0].source, b->news[0].title);
    float cx = rnd((float)SW / 2 + px), cy = rnd((float)SH / 2 + py);
    int big = !digits_rc ? D_XL : -1;
    float h = big >= 0 ? (float)digits_h(D_XL) : (float)line_h(F_LARGE), y = cy - (h + 60 + 40) / 2;
    if (big >= 0) digits(rnd(cx - (float)digits_w(D_XL, hm) / 2), y, D_XL, GREY, hm);
    else text_c(cx, y, F_LARGE | TNUM, GREY, "%s", hm);
    y += h + 4;
    text_c(cx, y, F_MED, DIM, "%s%s%s", date, ampm ? "  " : "", ampm ? ampm : "");
    if (line[0]) {
        float w = (float)text_w(F_BODY, line), mw = 1100;
        if (w > mw) w = mw;
        text_fit(rnd(cx - w / 2), y + 52, F_BODY, DIM, mw, "%s", line);
    }
    ui_flip();
}
