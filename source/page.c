/* Couch's pages (page.h): the frame every page shares, the header with the
 * clock, the slides, the ambient mode, the bottom bar. The News page
 * is reader.c's since 0.3.1. Every motion is a whole pixel before a text,
 * digits or S/M icon call, except the icons that move
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
#include "stext.h"
#include "fade.h"                 /* last: it wraps ps3gfx's draws */

#define DIM_LAYER 0x000000a8         /* RSX only: black at 66 %, the picture at a third of its light */
#define DOT_R 7
#define DOT_STEP 24
#define SLIDE_S 0.28                 /* a page's slide */
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
    static const u32 pal[6] = {0xe5484dff, 0xf28c28ff, 0x2f72d6ff, 0x2fa36bff, 0x8e5bd9ff, 0xc94f9bff};
    u32 h = 2166136261u;                                     /* FNV-1a of the label: one colour per source, every start */
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return pal[h % 6];
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

/* The bottom bar: the left words (and a note after them in
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
 * the ambient mode's font), no seconds (flip cards looked cut in half).
 * At the start it fades in with the header. */
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
    /* the second line: the next holiday (0.3.2) while the clock is the console's own; the clock's
     * source otherwise (the Info page always shows it) */
    if (src != CLK_CONSOLE || !board_have || !holiday_words(&board_cur, clock_utc() + off, line, sizeof line)) clock_source(line, sizeof line);
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
    if (k >= 0) {                                            /* the old page out, the new one in */
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
                  !ambient_aurora() ? "RSX only: dimmed" : ambient_day() ? "the sky" : "the aurora");
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

/* The ambient mode's day or night (0.3.2, a theme that follows the sun):
 * the Board's sky while the sun is up, the aurora at night. The
 * sun's elevation with 2 degrees of hysteresis, so a dusk does not flicker. */
static int amb_day = -1;
int ambient_day(void)
{
    double e = fx_sc.elev;
    if (amb_day < 0) amb_day = e > -3;
    else if (amb_day && e < -4) amb_day = 0;
    else if (!amb_day && e > -2) amb_day = 1;
    return amb_day;
}

#define AMB_ROT_S 10.0                       /* one headline at a time, the next every 10 s */
#define AMB_FADE_S 0.6                       /* out and in over 0.6 s */
#define AMB_MOVE_S 120.0                     /* the block moves every 2 min: out, a new place, in (0.4.1) */
#define aw_MAX 1520                       /* the block's width, at most (1240 before 0.4.1) */

float amb_ga = 1.0f;                         /* fade.h: the block's fade, 1 outside the waiting screen */
#define AMB_R 28                             /* a card's corners */

/* A card of the waiting screen: a light, translucent rounded panel with a
 * 1 px edge (the glass of the Board needs the sky's blur, which the aurora
 * does not make). */
static void amb_card(float x, float y, float w, float h, int day)
{
    u32 fill = day ? 0xffffff24 : 0xffffff14;
    round_rect(x, y, w, h, AMB_R, fill);
    aa_corners(x, y, w, h, AMB_R, fill);
    round_frame(x, y, w, h, AMB_R, 1, day ? 0xffffff38 : 0xffffff22);
}

static const char *const wday3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const mon3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* The waiting screen (redone in 0.3.2: a centred clock, the weather and
 * more news, in cards): the sky by day under a dim layer, the aurora by night; one block
 * up to AMB_W_MAX px wide in the middle, at a new place every AMB_MOVE_S
 * (ambient_spot), faded out and in (fade.h). The
 * date small over the clock (D_XL); under it two cards, the weather (the
 * hero's icon, the temperature, the words, the high, the low, the rain) and
 * the next holiday (else the sunrise and the sunset); a card of the next
 * hours (board_ambient_hours); then one card with a
 * headline, its source and age, the next every AMB_ROT_S, and a dot per
 * headline. */
static void ambient_frame(void)
{
    int off;
    civil_time c;
    const char *ampm;
    char hm[16], date[64], a[32];
    const board_data *b = &board_cur;
    begin_frame();
    int day = ambient_day();
    float ox, oy;
    screen_origin(&ox, &oy);
    if (day) {
        fx_draw_gradient();
        sky_behind();
        fx_draw_clouds();
        fx_draw_rain(0.7f);
        sky_front();
        rect(ox, oy, 1920, 1080, 0x00000050);   /* calmer than the Board */
    } else {
        if (!fx_draw_aurora()) rect_v(ox, oy, 1920, 1080, 0x040814ff, 0x0a1222ff);   /* no picture yet: the night sky's colours */
        fx_draw_rain(0.5f);
    }
    double t = (double)(sysGetSystemTime() - amb_since) / 1e6;
    float px, py;
    /* the place of this 2-minute span; the block fades out over its last AMB_FADE_S and in over the
     * next span's first: a move, never a visible step (0.3.2 crept a whole pixel every few seconds) */
    long span = (long)(t / AMB_MOVE_S);
    double mt = t - (double)span * AMB_MOVE_S;
    float ga = mt > AMB_MOVE_S - AMB_FADE_S ? (float)((AMB_MOVE_S - mt) / AMB_FADE_S) : span > 0 && mt < AMB_FADE_S ? (float)(mt / AMB_FADE_S) : 1.0f;
    ambient_spot(span, &px, &py);
    float aw = (float)SW - 2 * (AMBIENT_DRIFT_X + 60);
    if (aw > aw_MAX) aw = aw_MAX;
    aw = rnd(aw);
    u32 c1 = day ? WHITE : 0xdfe5eeff, c2 = day ? 0xdde3ecd0 : 0xaab5c6ff;
    clock_offset(&off);
    long local = clock_utc() + off;
    civil_from_unix(local, &c);
    clock_hm(hm, sizeof hm, c.hour, c.min, clock_h24(), &ampm);
    snprintf(date, sizeof date, "%s %d %s", weekday_name[c.wday], c.day, month_name[c.month - 1]);
    long hd = 0;
    int hw = 0, has_hol = board_have && holiday_parts(b, local, &hd, &hw);
    int has_wx = board_have && b->has_wx, has_side = has_hol || (board_have && b->has_sun);
    int nn = board_have ? b->n_news : 0;
    int big = !digits_rc ? D_XL : -1;
    float ch = big >= 0 ? (float)digits_h(D_XL) : (float)line_h(F_LARGE);
    int nl = 1;                                  /* the headline's lines: two when any of them needs two (no jump at a change) */
    for (int i = 0; i < nn; i++)
        if (text_w(F_LARGE, b->news[i].title) > aw - 72) nl = 2;
    float cardh = 192, newsh = 92 + (float)nl * (float)wrap_step(F_LARGE);
    float hrsh = 220;
    int has_hrs = has_wx && board_hours_left(c.hour) >= 2;
    float rest = (has_wx || has_side ? 28 + cardh : 0) + (nn ? 20 + newsh : 0), room = (float)SH - 64;   /* 64: the drift's 2 x 24 and a margin */
    if (has_hrs && big >= 0 && (float)line_h(F_MED) + ch + rest + 20 + hrsh > room) {
        big = D_L;                               /* a narrow area: a smaller clock first */
        ch = (float)digits_h(D_L);
    }
    if (has_hrs && (float)line_h(F_MED) + ch + rest + 20 + hrsh > room) {   /* still too tall: no hourly card, the large clock */
        has_hrs = 0;
        big = D_XL;
        ch = (float)digits_h(D_XL);
    }
    float hgt = (float)line_h(F_MED) + ch + rest + (has_hrs ? 20 + hrsh : 0);
    float cx = rnd((float)SW / 2 + px), x0 = rnd(cx - aw / 2), y = rnd((float)SH / 2 - hgt / 2 + py);
    amb_ga = ga;                                /* fade.h: the block's draws from here */
    /* the date over the clock, then the clock */
    text_c(cx, y, F_MED, c2, "%s%s%s", date, ampm ? "  " : "", ampm ? ampm : "");
    y += (float)line_h(F_MED);
    if (big >= 0) digits(rnd(cx - (float)digits_w(big, hm) / 2), y, big, c1, hm);
    else text_c(cx, y, F_LARGE | TNUM, c1, "%s", hm);
    y += ch;
    if (has_wx || has_side) {
        y += 28;
        float sw = has_side ? 460 : 0, ww = has_wx ? aw - (has_side ? sw + 24 : 0) : 0;
        if (!has_wx) sw = aw;
        if (has_wx) {
            amb_card(x0, y, ww, cardh, day);
            board_ambient_wx(x0, y, ww, cardh, c1, c2);
        }
        if (has_side) {
            float sx = rnd(x0 + aw - sw), ix = sx + 36, iw = sw - 72;
            amb_card(sx, y, sw, cardh, day);
            if (has_hol) {
                text(ix, y + 24, F_SMALL, c2, "%s", hd == 0 ? "Holiday today" : hd == 1 ? "Holiday tomorrow" : "Next holiday");
                if (hd > 1) {
                    snprintf(a, sizeof a, "in %ld days", hd);
                    text_r(ix + iw, y + 24, F_SMALL, 0xffc27aff, "%s", a);
                }
                text_fit(ix, y + 24 + (float)line_h(F_SMALL) + 6, F_LARGE, c1, iw, "%s %d %s", wday3[hw], b->hol_day, mon3[b->hol_month - 1]);
                text_wrap(ix, y + 24 + (float)line_h(F_SMALL) + 12 + (float)line_h(F_LARGE), F_SMALL, c2, iw, 2, b->hol_name);
            } else {
                float lh = (float)line_h(F_LARGE), yy = rnd(y + (cardh - 2 * lh - 12) / 2);
                text(ix, yy + (lh - (float)line_h(F_SMALL)) / 2, F_SMALL, c2, "Sunrise");
                text_r(ix + iw, yy, F_LARGE, c1, "%s", b->sunrise);
                text(ix, yy + lh + 12 + (lh - (float)line_h(F_SMALL)) / 2, F_SMALL, c2, "Sunset");
                text_r(ix + iw, yy + lh + 12, F_LARGE, c1, "%s", b->sunset);
            }
        }
        y += cardh;
    }
    if (has_hrs) {
        y += 20;
        amb_card(x0, y, aw, hrsh, day);
        board_ambient_hours(x0, y, aw, hrsh, c.hour, c1, c2);
        y += hrsh;
    }
    if (nn) {
        y += 20;
        amb_card(x0, y, aw, newsh, day);
        double ph = fmod(t, AMB_ROT_S);
        float k = ph < AMB_FADE_S ? (float)(ph / AMB_FADE_S) : ph > AMB_ROT_S - AMB_FADE_S ? (float)((AMB_ROT_S - ph) / AMB_FADE_S) : 1.0f;
        if (t < AMB_FADE_S) k = 1.0f;            /* the first headline shows at once */
        int i = (int)((long)(t / AMB_ROT_S) % nn);
        const board_news *n = &b->news[i];
        float ix = x0 + 36, ty = y + 26, phh = 36;
        float pw = (float)text_w(F_SMALL, n->source) + 28;
        if (pw > 220) pw = 220;
        u32 pc = alpha_times(source_colour(n->source), k);
        round_rect(ix, ty, pw, phh, phh / 2, pc);
        aa_corners(ix, ty, pw, phh, phh / 2, pc);
        text_fit(ix + 14, rnd(ty + (phh - (float)line_h(F_SMALL)) / 2), F_SMALL, alpha_times(WHITE, k), pw - 26, "%s", n->source);
        if (n->age >= 0) {
            age_words(a, sizeof a, n->age + board_age());
            text(ix + pw + 16, rnd(ty + (phh - (float)line_h(F_SMALL)) / 2), F_SMALL, alpha_times(c2, k), "%s ago", a);
        }
        for (int d = 0; d < nn; d++)               /* a dot per headline, the shown one lit */
            aa_disc(x0 + aw - 36 - (float)(nn - 1 - d) * 18, ty + phh / 2, 4, d == i ? c1 : 0xffffff40);
        text_wrap(ix, ty + phh + 16, F_LARGE, alpha_times(c1, k), aw - 72, nl, n->title);
    }
    amb_ga = 1.0f;
    ui_flip();
}
