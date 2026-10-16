/* The Board page (page.h's board_body and board_bar; design 2026-10-13,
 * section 1.4): dashboard tiles of frosted glass over the live sky. Left,
 * the weather: the hero tile (the icon, the temperature, the words, feels
 * and humidity, the min/max bar, the wind dial, the rain line, the 12 h
 * curve with its rain columns), the sky tile (the sun arc, the moon), the
 * three days. Right, the money: Blue's tile (the price, the change chip,
 * the 30 days' sparkline and range), then a grid of small tiles (the other
 * rates, the money extras). Under them the news ticker; the bottom bar is
 * page.c's.
 *
 * The layout comes from SW and SH each frame, in two forms: wide (SH 980
 * and up: the TV area and the default area, the heights scaled by the
 * content's height over 800) and narrow (the narrow area, 1600 x 900).
 * Every value has its "-" form when the reply lacks its key (a Worker
 * before 0.3.0); before the first reply each tile shows the skeleton
 * (page.c's skel_bar); every tile is a solid panel in RSX-only mode
 * (fx_glass). A value that changed at the last good reply counts from its
 * old value in 400 ms and its tile gets a halo: up in 200 ms, two pulses of
 * 500 ms, then a fade in 400 ms to a lower glow that stays until the next
 * reply. Every motion reads sky_time (real time, not a count of frames).
 * A text or digits call takes a whole pixel (design 1.12); since stage 10
 * every icon that moves goes through icon_rot (bilinear: the S and M sets
 * too), the anti-aliased lines move by fractions, and the ticker's text is
 * marquee.c's bilinear picture, so nothing moves in whole-pixel steps. The
 * lines, the rings, the discs and the arrow are anti-aliased (fx.h's aa_*:
 * the round of 2026-10-14 found them rough). The two columns and the
 * ticker rise into place in the start animation (start.h). The main thread
 * only. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fx.h>
#include <ps3gfx/digits.h>
#include <ps3gfx/icons.h>
#include "couch.h"
#include "board.h"
#include "clock.h"
#include "page.h"
#include "parse.h"
#include "astro.h"
#include "sky.h"
#include "spu_fx_run.h"
#include "marquee.h"
#include "start.h"

#define HALO 24                      /* the glow outside a changed tile */
#define COUNT_S 0.4                  /* the counting's time (stage 9: 0.8 s read as slow) */
#define HALO_UP 0.2f                 /* a changed tile's halo: up in 200 ms, */
#define HALO_PULSE 0.5f              /* two pulses of 500 ms, */
#define HALO_FADE 0.4f               /* a fade to the resting glow in 400 ms (stage 9: 3 s of pulses, a fade to 6 s) */
/* The ticker's speed in px a second (Lucas, 2026-10-14: "the text marquee is
 * fast", "an slower marquee"; stage 9b moved 2 px a frame, 119.9 px a
 * second). Any value: the text moves by fractions of a pixel (marquee.c),
 * so no speed judders at 59.94 Hz. The one number to tune after the
 * console's round. */
#define TICKER_PX_S 55.0
#define PI_F 3.14159265f

static int wide;                     /* SH >= 980 */
static float sc;                     /* wide: the content's height over 800; narrow: 1 */
static double T;                     /* sky_time() of this frame */
static const char *deg;              /* the degree sign for numbers Couch writes: UTF-8, or nothing when folded */

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float rnd(float x) { return floorf(x + 0.5f); }
static int miss(const char *s) { return !s[0] || !strcmp(s, "-"); }
/* The ascent of a font or digits line: Inter's, 0.797 of the line height (the atlases' asc / line). */
static float asc_of(int line) { return (float)(int)((float)line * 0.797f + 0.5f); }

/* The display digits, or F_LARGE in tabular digits when digits_init failed. */
static float big(float x, float y, int size, u32 c, const char *s)
{
    if (!digits_rc) return digits(x, y, size, c, s);
    return text(x, y + asc_of(digits_h(size) ? digits_h(size) : 64) - asc_of(line_h(F_LARGE)), F_LARGE | TNUM, c, "%s", s);
}
static int big_h(int size) { return digits_rc ? line_h(F_LARGE) : digits_h(size); }
/* The ascent of the big numbers: the baseline is y plus this. */
static float big_asc(int size) { return asc_of(big_h(size)); }

/* A temperature of the reply ("19" and the degree sign, "19" folded, "-2" and the sign) as its number;
 * 0 when it is not one. */
static int temp_num(const char *s, int *v)
{
    int sign = 1, n = 0, d = 0;
    if (*s == '-') sign = -1, s++;
    while (*s >= '0' && *s <= '9' && d < 3) n = n * 10 + (*s++ - '0'), d++;
    if (!d) return 0;
    *v = sign * n;
    return 1;
}

/* The same as the display digits draw it: the number and the byte B0 (the
 * degree sign of digits.h, also in the folded build), or "-". */
static void temp_digits(char *out, int n, const char *s)
{
    int v;
    if (temp_num(s, &v) && digits_rc) snprintf(out, (size_t)n, "%d%s", v, deg);   /* F_LARGE in place of the digits */
    else if (temp_num(s, &v)) snprintf(out, (size_t)n, "%d\xb0", v);
    else snprintf(out, (size_t)n, "-");
}

/* ---- the tiles ----------------------------------------------------------------------- */

#define tile glass_tile                  /* page.c's glass tile (design 3.4) */

/* ---- the changes since the reply before: the counting and the glow ------------------- */

typedef struct {
    char label[48];
    board_num from, to;
    int dir;                         /* CHG_UP or CHG_DOWN */
} change;
static change chg[BOARD_USD + BOARD_MONEY];
static int n_chg, seen_gen;
static double chg_t0;

static double num_val(const board_num *x)
{
    double v = (double)x->v;
    for (int i = 0; i < x->dec; i++) v /= 10;
    return v;
}

static void change_add(const char *label, const char *was, const char *now)
{
    board_num a, b;
    if (n_chg >= (int)(sizeof chg / sizeof chg[0]) || !num_parse(was, &a) || !num_parse(now, &b)) return;
    double va = num_val(&a), vb = num_val(&b);
    if (va == vb) return;
    change *c = &chg[n_chg++];
    snprintf(c->label, sizeof c->label, "%s", label);
    c->from = a;
    c->to = b;
    c->dir = vb > va ? CHG_UP : CHG_DOWN;
}

/* At each new good reply: the values that differ from the reply before (by label). */
static void changes(void)
{
    if (board_gen == seen_gen) return;
    seen_gen = board_gen;
    n_chg = 0;
    chg_t0 = T;
    if (!board_prev_have) return;
    const board_data *b = &board_cur, *p = &board_prev;
    for (int i = 0; i < b->n_usd; i++)
        for (int j = 0; j < p->n_usd; j++)
            if (!strcmp(b->usd[i].label, p->usd[j].label)) change_add(b->usd[i].label, p->usd[j].sell, b->usd[i].sell);
    for (int i = 0; i < b->n_money; i++)
        for (int j = 0; j < p->n_money; j++)
            if (!strcmp(b->money[i].label, p->money[j].label)) change_add(b->money[i].label, p->money[j].value, b->money[i].value);
}

static const change *change_of(const char *label)
{
    for (int i = 0; i < n_chg; i++)
        if (!strcmp(chg[i].label, label)) return &chg[i];
    return NULL;
}

/* The value as it shows now: counting from the old one for COUNT_S (ease-out cubic). */
static const char *shown(const char *label, const char *value, char *buf, int n)
{
    const change *c = change_of(label);
    double t = (T - chg_t0) / COUNT_S;
    if (!c || t >= 1) return value;
    board_num x;
    double e = 1 - (1 - t) * (1 - t) * (1 - t);
    num_mix(&c->from, &c->to, e, &x);
    num_format(buf, n, &x);
    return buf;
}

/* The halo of a changed tile: up in HALO_UP (ease-out), two pulses of
 * HALO_PULSE, a fade in HALO_FADE (ease-out) to a lower glow that stays
 * until the next reply ("changed at the last refresh"). */
static void halo(float x, float y, float w, float h, const char *label)
{
    const change *c = change_of(label);
    const gfx_tex *t = sky_halo_tex();
    if (!c || !t) return;
    float s = (float)(T - chg_t0), a, e, p = HALO_UP + 2 * HALO_PULSE;
    if (s < HALO_UP) {
        e = s / HALO_UP;
        a = 0xd0 * (1 - (1 - e) * (1 - e));
    } else if (s < p) a = 0xd0 * (0.6f + 0.4f * cosf(2 * PI_F * (s - HALO_UP) / HALO_PULSE));
    else if (s < p + HALO_FADE) {
        e = (s - p) / HALO_FADE;
        a = 0xd0 + (0x90 - 0xd0) * (1 - (1 - e) * (1 - e));
    } else a = 0x90;
    glow9(t, x, y, w, h, HALO, ((c->dir == CHG_UP ? UP : DOWN) & ~0xffu) | (u32)a);
}

/* ---- small pieces -------------------------------------------------------------------- */

/* The change chip (design 1.7) right-aligned at xr: caret and the change in
 * UP or DOWN, a grey dash for flat, nothing for none. Its left x. */
static float chip(float xr, float y, const board_chg *c, int money)
{
    if (!c || c->dir == CHG_NONE || c->dir == CHG_FLAT) return xr;   /* 0.3.2: a chip only for a move (no grey minus) */
    u32 col = c->dir == CHG_UP ? UP : c->dir == CHG_DOWN ? DOWN : GREY;
    int ic = c->dir == CHG_UP ? IC_S_CARET_UP : c->dir == CHG_DOWN ? IC_S_CARET_DOWN : IC_S_MINUS;
    const char *s = c->dir == CHG_FLAT ? "" : money ? c->abs : c->pct;
    if (miss(s)) s = "";
    float w = 34 + (s[0] ? (float)text_w(F_SMALL, s) + 10 : 0), x = rnd(xr - w);
    round_rect(x, y, w, 30, 15, (col & ~0xffu) | 0x26);
    icon(ic, x + 17, y + 15, col);
    if (s[0]) text(x + 30, y + 1, F_SMALL, col, "%s", s);
    return x;
}

/* A value with its decimals smaller: "1.540" big, ",00" in dec_font GREY;
 * a value with '%' whole. Its end x. */
static float value(float x, float y, int big_size, int font, int dec_font, u32 c, const char *v)
{
    char whole[32];
    const char *comma = strchr(v, '%') ? NULL : strrchr(v, ',');
    int n = comma ? (int)(comma - v) : (int)strlen(v);
    snprintf(whole, sizeof whole, "%.*s", n, v);
    float base, xe;
    if (big_size >= 0) {
        xe = big(x, y, big_size, c, whole);
        base = y + big_asc(big_size);
    } else {
        xe = text(x, y, font | TNUM, c, "%s", whole);
        base = y + asc_of(line_h(font));
    }
    if (comma) xe = text(xe + 1, base - asc_of(line_h(dec_font)), dec_font | TNUM, GREY, "%s", comma);
    return xe;
}

/* ---- the weather hero ---------------------------------------------------------------- */

static const int m_icon[9] = {IC_M_SUN_FILL,        IC_M_MOON_STARS_FILL, IC_M_CLOUD_SUN_FILL,  IC_M_CLOUD_MOON_FILL,      IC_M_CLOUD_FILL,
                              IC_M_CLOUD_FOG_FILL,  IC_M_CLOUD_RAIN_FILL, IC_M_CLOUD_SNOW_FILL, IC_M_CLOUD_LIGHTNING_FILL};

/* An M icon of the weather (the fill tinted, the outline WHITE), turned by
 * angle and scaled: icon_rot binds it bilinear, so it moves by fractions of
 * a pixel (the round of 2026-10-14: the day icons' 2 px bob on whole pixels
 * moved in steps a few seconds apart, "in installments"). */
static void wx_icon_m(int wi, float cx, float cy, float angle, float scale)
{
    if (wi < 0 || wi > WI_CLOUD_LIGHTNING) return;
    u32 fill = wi == WI_SUN || wi == WI_CLOUD_SUN ? 0xffd36b60 : wi == WI_MOON_STARS || wi == WI_CLOUD_MOON ? 0xe9edf550
               : wi == WI_CLOUD_LIGHTNING                     ? 0xc9a4ff60
                                                              : 0xb8c4d660;
    icon_rot(m_icon[wi], cx, cy, angle, scale, fill);
    icon_rot(m_icon[wi] + 1, cx, cy, angle, scale, WHITE);
}

static void hero_icon(int k, int day, float cx, float cy, float box);

/* The weather icon of now (the hero's) for the ambient mode: 0 when there is none. */
int board_wx_icon(float cx, float cy, float box)
{
    const board_data *b = &board_cur;
    int has = board_have && b->has_wx;
    int k = board_have && b->has_fx ? wmo_kind(b->fx_code) : WK_NONE, day = has ? b->is_day : fx_sc.elev > 0;
    if (k == WK_NONE) return 0;
    hero_icon(k, day, cx, cy, box);
    return 1;
}

/* The waiting screen's weather card (0.3.2) in the box x, y, w, h: the
 * hero's moving icon (120 px), the temperature in D_M, then a column with
 * the words (F_LARGE), today's high and low and the rain's chance (F_MED). */
void board_ambient_wx(float x, float y, float w, float h, u32 c1, u32 c2)
{
    const board_data *b = &board_cur;
    if (!board_have || !b->has_wx) return;
    char t[16], hl[64], rain[32];
    int k = b->has_fx ? wmo_kind(b->fx_code) : WK_NONE;
    temp_digits(t, sizeof t, b->temp);
    hl[0] = rain[0] = 0;
    if (b->n_day > 0 && !strcmp(b->day[0].label, "Today")) {
        const board_day *d = &b->day[0];
        if (!miss(d->max) && !miss(d->min)) snprintf(hl, sizeof hl, "H %s   L %s", d->max, d->min);
        if (!miss(d->rain)) snprintf(rain, sizeof rain, "Rain %s", d->rain);
    }
    float ic = k == WK_NONE || icons_rc ? 0 : 120, cx = x + 40;
    if (ic > 0) {
        hero_icon(k, b->is_day, cx + ic / 2, y + h / 2, ic);
        cx += ic + 28;
    }
    float xe = big(rnd(cx), rnd(y + (h - (float)big_h(D_M)) / 2), D_M, c1, t), col = rnd(xe + 40), mw = x + w - 36 - col;
    float l1 = (float)line_h(F_LARGE), l2 = (float)line_h(F_MED), ch = l1 + (hl[0] || rain[0] ? 8 + l2 : 0), cy = rnd(y + (h - ch) / 2);
    text_fit(col, cy, F_LARGE, c1, mw, "%s", b->words);
    if (hl[0] || rain[0]) {
        float xx = col;
        if (hl[0]) xx = text_fit(col, rnd(cy + l1 + 8), F_MED, c2, mw, "%s", hl) + 28;
        if (rain[0] && xx < col + mw - 60) text_fit(xx, rnd(cy + l1 + 8), F_MED, 0x8ec9ffff, col + mw - xx, "%s", rain);
    }
}

/* The hero's icon (design 3.9): layers of the L set that move, by
 * fractions of a pixel (bilinear), slowly and over a wider range since the
 * round of 2026-10-14 (Lucas: "a bit bigger and more range of movement"):
 * the sun turns once a minute and breathes 3 % on 5 s; the cloud drifts 14
 * px on 8 s, a grey one behind it 10 px on 11 s; the rain falls 70 px; the
 * snow falls 70 px and sways; the fog's bars slide 18 px; the moon sways 4
 * deg on 7 s with two stars that twinkle. k: WK_*. */
static void hero_icon(int k, int day, float cx, float cy, float box)
{
    float s = box / 168, t = (float)T;
    int still = fx_reduced();
    float drift = still ? 0 : 14 * s * sinf(2 * PI_F * t / 8), drift2 = still ? 0 : 10 * s * sinf(2 * PI_F * t / 11);
    float breathe = still ? 1 : 1 + 0.03f * sinf(2 * PI_F * t / 5);
    if (k == WK_NONE) return;
    if (k == WK_CLEAR) {
        if (day) {
            float a = still ? 0 : 2 * PI_F * (float)fmod(T, 60.0) / 60;   /* one turn a minute */
            icon_rot(IC_L_SUN_FILL, cx, cy, a, s * breathe, 0xffd36b55);
            icon_rot(IC_L_SUN_LINE, cx, cy, a, s * breathe, SUN);
        } else {
            float sw = still ? 0 : 4 * PI_F / 180 * sinf(2 * PI_F * t / 7);
            icon_rot(IC_L_MOON_FILL, cx - 4 * s, cy, sw, s * 0.86f, 0xe9edf540);
            icon_rot(IC_L_MOON_LINE, cx - 4 * s, cy, sw, s * 0.86f, MOON_LIT);
            for (int i = 0; i < 2; i++) {                    /* two small stars that twinkle */
                float tw = still ? 1 : 0.55f + 0.45f * sinf(t * (1.7f + (float)i) + (float)i * 2);
                aa_disc(cx + (i ? 64 : 52) * s, cy + (i ? 18 : -48) * s, i ? 3 : 5, (WHITE & ~0xffu) | (u32)(0xff * tw));
            }
        }
        return;
    }
    float ccx = cx - 6 * s + drift, ccy = cy - 6 * s;
    if (k == WK_PARTLY) {
        if (day) {
            float a = still ? 0 : 2 * PI_F * (float)fmod(T, 60.0) / 60;
            icon_rot(IC_L_SUN_FILL, cx + 30 * s, cy - 32 * s, a, s * 0.6f * breathe, 0xffd36b55);
            icon_rot(IC_L_SUN_LINE, cx + 30 * s, cy - 32 * s, a, s * 0.6f * breathe, SUN);
        } else {
            icon_rot(IC_L_MOON_FILL, cx + 30 * s, cy - 32 * s, 0, s * 0.55f, 0xe9edf540);
            icon_rot(IC_L_MOON_LINE, cx + 30 * s, cy - 32 * s, 0, s * 0.55f, MOON_LIT);
        }
        ccy = cy + 8 * s;
    } else {                                                 /* a second, smaller cloud behind, in GREY */
        icon_rot(IC_L_CLOUD_FILL, cx + 18 * s + drift2, cy - 30 * s, 0, s * 0.6f, 0x9ba7ba50);
        icon_rot(IC_L_CLOUD_LINE, cx + 18 * s + drift2, cy - 30 * s, 0, s * 0.6f, GREY);
    }
    if (k == WK_RAIN || k == WK_STORM) {                     /* 10 streaks under the cloud that fall 70 px and fade */
        for (int i = 0; i < 10; i++) {
            float ph = still ? 0.3f : (float)fmod(T / 1.1 + i * 0.37, 1.0);
            float x = ccx - 52 * s + (float)i * 11.5f * s, y = ccy + 34 * s + ph * 70 * s;
            aa_line(x, y, x - 4 * s, y + 20 * s, 3, (BLUE & ~0xffu) | (u32)(0xd0 * (1 - ph)));
        }
    }
    if (k == WK_SNOW)                                        /* 6 flakes that fall 70 px and sway */
        for (int i = 0; i < 6; i++) {
            float ph = still ? 0.3f + 0.1f * (float)i : (float)fmod(T / 3.0 + i * 0.29, 1.0);
            float x = ccx - 46 * s + (float)i * 18 * s + 8 * s * sinf(t * 1.6f + (float)i), y = ccy + 34 * s + ph * 70 * s;
            icon_rot(IC_L_SNOWFLAKE_LINE, x, y, (float)i + t * 0.4f, s * 0.16f, (WHITE & ~0xffu) | (u32)(0xff * (1 - ph * 0.8f)));
        }
    if (k == WK_FOG)                                         /* three bars that slide 18 px, anti-aliased (no whole-pixel steps) */
        for (int i = 0; i < 3; i++) {
            float dx = still ? 0 : 18 * s * sinf(t * 0.6f + (float)i * 2);
            float x0 = cx - 62 * s + dx + (float)(i & 1) * 14 * s, y0 = cy + 40 * s + (float)i * 16 * s;
            aa_line(x0, y0, x0 + 110 * s, y0, 7 * s, DIM);
        }
    icon_rot(IC_L_CLOUD_FILL, ccx, ccy, 0, s * 0.86f, 0xb8c4d670);
    icon_rot(IC_L_CLOUD_LINE, ccx, ccy, 0, s * 0.86f, WHITE);
    if (k == WK_STORM) {                                     /* the lightning layer flashes with a bolt, and on its own */
        int on = sky_bolt_on() || (!still && fmod(T, 6.5) < 0.16);
        icon_rot(IC_L_LIGHTNING_FILL, ccx + 4 * s, ccy + 50 * s, 0, s * 0.5f, on ? 0xc9a4ffc0 : 0xc9a4ff40);
        icon_rot(IC_L_LIGHTNING_LINE, ccx + 4 * s, ccy + 50 * s, 0, s * 0.5f, on ? WHITE : (STORM & ~0xffu) | 0x90);
    }
}

/* The wind dial (design 1.4): a ring with ticks, N E S W (N only narrow),
 * the arrow where the wind goes (a slow 2 deg wobble), the speed, km/h, and
 * the words under the ring. */
static void dial(float cx, float cy, float R, const board_data *b)
{
    char sp[16], words[48];
    int dir = board_have && b->has_fx ? b->fx_dir : -1, gusts = board_have && b->has_fx ? b->fx_gusts : -1, v;
    if (board_have && b->has_fx && b->fx_speed >= 0) snprintf(sp, sizeof sp, "%d", b->fx_speed);
    else if (board_have && b->has_wx && temp_num(b->wind, &v)) snprintf(sp, sizeof sp, "%d", v);   /* wx_now's "10 km/h" */
    else snprintf(sp, sizeof sp, "-");
    float tk[20 * 4];
    int nt = 0;
    aa_ring(cx, cy, R, 3, 0xffffff40);
    for (int k = 0; k < 24; k++) {                           /* the ticks inside the ring, to its inner edge (no overlap); */
        if (k % 6 == 0) continue;                            /* N, E, S and W have their letters (stage 9's 2 px there hid under the ring) */
        float a = (float)k * PI_F / 12, r0 = R - 6, r1 = R - 1.5f;
        tk[4 * nt] = cx + sinf(a) * r0;
        tk[4 * nt + 1] = cy - cosf(a) * r0;
        tk[4 * nt + 2] = cx + sinf(a) * r1;
        tk[4 * nt + 3] = cy - cosf(a) * r1;
        nt++;
    }
    aa_lines(tk, nt, 2, 0xffffff60);
    const char *nesw = wide ? "NESW" : "N";
    for (int k = 0; nesw[k]; k++) {
        float a = (float)k * PI_F / 2;
        text_c(rnd(cx + sinf(a) * (R - 18)), rnd(cy - cosf(a) * (R - 18) - 14), F_SMALL, DIM, "%c", nesw[k]);
    }
    if (dir >= 0) {
        float wob = fx_reduced() ? 0 : 2 * sinf((float)T * 0.5f);
        float to = ((float)dir + 180 + wob) * PI_F / 180, L = R - 4;
        float ax = cx + sinf(to) * L, ay = cy - cosf(to) * L;
        float pa = to + 150 * PI_F / 180, pl = to - 150 * PI_F / 180;
        float hd[6] = {ax, ay, ax + sinf(pa) * 22, ay - cosf(pa) * 22, ax + sinf(pl) * 22, ay - cosf(pl) * 22};
        float bx = (hd[2] + hd[4]) / 2, by = (hd[3] + hd[5]) / 2;   /* the shaft to the head's base: the two do not overlap */
        aa_line(cx - sinf(to) * L, cy + cosf(to) * L, bx, by, 4, (BLUE & ~0xffu) | 0x50);
        aa_poly(hd, 3, BLUE);
    }
    text_c(cx, rnd(cy - 26), F_MED | TNUM, WHITE, "%s", sp);
    text_c(cx, rnd(cy + 8), F_SMALL, GREY, "km/h");
    wind_words(words, sizeof words, dir, gusts);
    text_c(cx, rnd(cy + R + 10), F_SMALL, GREY, "%s", words[0] ? words : "-");
}

/* The feed's warning in words (stale or none, else ""): the reason and the data's age. */
static int feed_warn(char *buf, int n, const board_feed *f)
{
    char a[24];
    buf[0] = 0;
    if (!board_have) return 0;
    if (!strcmp(f->state, "stale")) {
        age_words(a, sizeof a, f->age + board_age());
        snprintf(buf, (size_t)n, "%s, %s old", f->reason[0] ? f->reason : "stale", a);
        return 1;
    }
    if (!strcmp(f->state, "none")) {
        snprintf(buf, (size_t)n, "%s", f->reason[0] ? f->reason : "no data");
        return 1;
    }
    return 0;
}

/* The 12 h curve (design 1.4): the line and the fill under it, a BLUE
 * column of each hour's rain chance, the hours under the points, the
 * temperature over every second (narrow: third) point. */
static void curve(float x0, float x1, float ytop, float ybase, const board_data *b)
{
    float pts[2 * HOURS_MAX], cx[HOURS_MAX];
    int n = board_have ? b->n_hours : 0, lo = 999, hi = -999, np = 0;
    for (int i = 0; i < HOURS_MAX; i++) cx[i] = rnd(x0 + (x1 - x0) * (float)i / (HOURS_MAX - 1));
    rect(x0 - 16, ybase, x1 - x0 + 32, 1, 0xffffff26);
    if (n < 2) {                                             /* no wx_hours: the axis and "-" */
        text_c((x0 + x1) / 2, rnd((ytop + ybase) / 2 - 14), F_SMALL, DIM, "12 h: -");
        return;
    }
    for (int i = 0; i < n; i++)
        if (b->hour_temp[i] != TEMP_NONE) {
            if (b->hour_temp[i] < lo) lo = b->hour_temp[i];
            if (b->hour_temp[i] > hi) hi = b->hour_temp[i];
        }
    float span = (float)(hi - lo + 2), top = ytop + 34;     /* room for the labels over the points */
    for (int i = 0; i < n; i++) {
        if (b->hour_pop[i] > 0) {
            float h = (float)b->hour_pop[i] / 100.0f * (wide ? 40 : 30) * sc;
            rect(cx[i] - 10, rnd(ybase - h), 20, rnd(h), (BLUE & ~0xffu) | 0x90);
        }
        text_c(cx[i], ybase + 4, F_SMALL | TNUM, DIM, "%02d", (b->hours_start + i) % 24);
        if (b->hour_temp[i] == TEMP_NONE) continue;
        float y = ybase - 12 - (ybase - 12 - top) * (float)(b->hour_temp[i] - lo + 1) / span;
        pts[2 * np] = cx[i];
        pts[2 * np + 1] = y;
        np++;
        if (i % (wide ? 2 : 3) == 0) text_c(cx[i], rnd(y - 31), F_SMALL | TNUM, WHITE, "%d%s", b->hour_temp[i], deg);
    }
    if (np < 2) return;
    area(pts, np, ybase, 0xffd36b40, 0xffd36b00);
    aa_strip(pts, np, 3, SUN);
    aa_disc(pts[0], pts[1], 6, SUN);
}

static void hero(float x, float y, float w, float h, const board_data *b)
{
    char t[16], lo[16], hi[16], warn[160];
    int has = board_have && b->has_wx;
    int k = board_have && b->has_fx ? wmo_kind(b->fx_code) : WK_NONE, day = has ? b->is_day : fx_sc.elev > 0;
    int tsize = wide ? D_XL : D_L;
    float ic = wide ? 184 : 140, R = wide ? 76 : 58;     /* the icon's box (stage 9: 168 and 132) */
    float dx = x + w - 40 - 2 * R, dcx = x + w - 40 - R, dcy = y + 30 + R;
    tile(x, y, w, h);
    if (!board_have) {                                       /* before the first reply: the skeleton (design 3.9) */
        float sx = x + 28 + ic + 24;
        skel_bar(x + 40, y + 40, ic - 24, ic - 40);
        skel_bar(sx, y + 34, wide ? 230 : 170, wide ? 110 : 80);
        skel_bar(sx + (wide ? 260 : 200), y + 52, 150, 30);
        skel_bar(sx + (wide ? 260 : 200), y + 96, 210, 24);
        aa_ring(dcx, dcy, R, 3, 0xffffff26);
        float band = rnd((wide ? 170 : 130) * sc), yb = rnd(y + h - band - 14);
        skel_bar(x + 48, yb, 260, 26);
        skel_bar(x + 48, yb + 50, w - 96, band - 74);
        return;
    }
    hero_icon(k, day, x + 28 + ic / 2, y + 22 + ic / 2, ic);
    float tx = k == WK_NONE ? x + 32 : x + 28 + ic + 24;     /* no code: the temperature takes the icon's place */
    temp_digits(t, sizeof t, has ? b->temp : "-");
    float ty = y + (wide ? 6 : 22) * sc;
    float te = big(rnd(tx), rnd(ty), tsize, has ? WHITE : DIM, t);
    float tx2 = te + 28, tw = dx - 24 - tx2;
    text_fit(tx2, rnd(y + (wide ? 52 : 34) * sc), F_MED, WHITE, tw, "%s", has && !miss(b->words) ? b->words : "-");
    char fl[64];
    snprintf(fl, sizeof fl, "Feels like %s, humidity %s", has ? b->feels : "-", has ? b->humidity : "-");
    if (wide && text_w(F_BODY, fl) <= tw) text(tx2, rnd(y + 96 * sc), F_BODY, GREY, "%s", fl);
    else {                                                   /* narrow, or a row too short (a wide temperature): two lines */
        float l1 = wide ? 92 * sc : 74;
        text_fit(tx2, rnd(y + l1), F_BODY, GREY, tw, "Feels like %s", has ? b->feels : "-");
        text_fit(tx2, rnd(y + l1 + 32), F_BODY, GREY, tw, "Humidity %s", has ? b->humidity : "-");
    }
    /* the min/max bar: today's min, 8 steps from TEMP_COLD to TEMP_HOT, the now marker, today's max */
    int vmin, vmax, vnow;
    const char *smin = board_have && b->n_day ? b->day[0].min : "-", *smax = board_have && b->n_day ? b->day[0].max : "-";
    float by = rnd(y + (wide ? 196 * sc : 150)), bx = wide ? tx2 : tx;
    snprintf(lo, sizeof lo, "%s", smin);
    snprintf(hi, sizeof hi, "%s", smax);
    float b0 = text(bx, by, F_BODY | TNUM, miss(lo) ? DIM : WHITE, "%s", lo) + 14;
    float bw = dx - 90 - b0;
    if (bw > 260) bw = 260;
    if (bw > 40) {
        float my = by + (float)line_h(F_BODY) / 2;
        aa_disc(b0, my, 4, TEMP_COLD);                      /* round ends, under the steps (the steps cover their inner half) */
        aa_disc(b0 + bw, my, 4, TEMP_HOT);
        for (int i = 0; i < 8; i++) {
            float f = (float)i / 7;
            u32 c = 0xff;
            for (int sh = 24; sh >= 8; sh -= 8) {
                float c0 = (float)(TEMP_COLD >> sh & 0xff), c1 = (float)(TEMP_HOT >> sh & 0xff);
                c |= (u32)(c0 + (c1 - c0) * f + 0.5f) << sh;
            }
            float sx0 = rnd(b0 + bw * (float)i / 8), sx1 = i == 7 ? b0 + bw : rnd(b0 + bw * (float)(i + 1) / 8);
            rect(sx0, my - 4, sx1 - sx0, 8, c);
        }
        if (has && temp_num(smin, &vmin) && temp_num(smax, &vmax) && temp_num(b->temp, &vnow) && vmax > vmin) {
            float mx = rnd(b0 + bw * clampf((float)(vnow - vmin) / (float)(vmax - vmin), 0, 1));
            aa_disc(mx, my, 9, 0x050a14ff);
            aa_disc(mx, my, 7, WHITE);
        }
        text(b0 + bw + 14, by, F_BODY | TNUM, miss(hi) ? DIM : WHITE, "%s", hi);
    }
    dial(dcx, dcy, R, b);
    /* the bottom band: the rain line, then the curve */
    float band = rnd((wide ? 170 : 130) * sc), yb = rnd(y + h - band - 14);
    int wet = board_have && b->has_rain && b->rain_wet;
    icon(IC_S_DROP, x + 42, yb + (float)line_h(F_BODY) / 2, wet ? BLUE : board_have && b->has_rain ? GREY : DIM);
    float ww = 0, xw = x + w - 32;
    if (feed_warn(warn, sizeof warn, &b->wx_feed)) {         /* the weather is stale or none: why, at the row's right */
        ww = (float)text_w(F_SMALL, warn);
        if (ww > w * 0.45f) ww = w * 0.45f;
        xw -= ww;
        text_fit(xw, yb + 3, F_SMALL, YELLOW, ww, "%s", warn);
    }
    text_fit(x + 64, yb, F_BODY, wet ? BLUE : board_have && b->has_rain ? GREY : DIM, xw - 30 - (x + 64), "%s",
             board_have && b->has_rain ? b->rain : "Rain: -");
    curve(x + 48, x + w - 48, yb + 34, rnd(yb + band - 34), b);
}

/* ---- the sky tile -------------------------------------------------------------------- */

static void sky_tile(float x, float y, float w, float h, const board_data *b)
{
    char a[32], line[64], nx[48];
    long utc = clock_utc(), to_next = 0;
    int off;
    clock_offset(&off);
    if (board_have && b->has_offset) off = b->offset;
    tile(x, y, w, h);
    if (!board_have) {
        skel_bar(x + 24, y + 16, 220, 26);
        skel_bar(x + 50, y + h - 46, w / 2 - 100, 12);
        skel_bar(x + w / 2 + 40, y + (h - 80) / 2, 80, 80);
        skel_bar(x + w / 2 + 150, y + h / 2 - 36, 200, 28);
        skel_bar(x + w / 2 + 150, y + h / 2 + 8, 120, 22);
        return;
    }
    float half = w / 2, ax0 = x + 50, ax1 = x + half - 50, aw = ax1 - ax0, ah = h - (wide ? 88 : 80);
    float acx = (ax0 + ax1) / 2, acy = rnd(y + h - 34);
    int rise = board_have && b->has_sun ? hm_parse(b->sunrise) : -1, set = board_have && b->has_sun ? hm_parse(b->sunset) : -1;
    double frac = 0;
    int st = sun_arc(utc, off, rise, set, &frac, &to_next);
    age_words(a, sizeof a, to_next);
    if (st == 1) snprintf(line, sizeof line, "Sunset in %s", a);
    else if (st == 0) snprintf(line, sizeof line, "Sunrise in %s", a);
    else snprintf(line, sizeof line, "Sunset in -");
    text_fit(x + 24, y + 12, wide ? F_BODY : F_SMALL, st < 0 ? DIM : WHITE, half - 40, "%s", line);
    float px[49], py[49];
    for (int k = 0; k <= 48; k++) {
        float an = PI_F * (float)k / 48;
        px[k] = acx - cosf(an) * aw / 2;
        py[k] = acy - sinf(an) * ah;
    }
    rect(ax0 - 20, acy, aw + 40, 1, 0xffffff30);
    float ds[24 * 4], sp[2 * 50];
    int nd = 0, ns = 0;
    for (int k = 0; k < 48; k += 2)                          /* the dashes after the sun (all of them by night) */
        if (!(st == 1 && (float)k / 48 < frac)) {
            ds[4 * nd] = px[k], ds[4 * nd + 1] = py[k], ds[4 * nd + 2] = px[k + 1], ds[4 * nd + 3] = py[k + 1];
            nd++;
        }
    aa_lines(ds, nd, 2, DIM);
    if (st >= 0) {
        float an = PI_F * (float)frac, sx = acx - cosf(an) * aw / 2, sy = acy - sinf(an) * ah;
        if (st == 1) {                                       /* solid from sunrise to the sun's place */
            for (int k = 0; k < 48 && (float)k / 48 < frac; k++) sp[2 * ns] = px[k], sp[2 * ns + 1] = py[k], ns++;
            if (!ns || fabsf(sx - sp[2 * ns - 2]) + fabsf(sy - sp[2 * ns - 1]) > 0.5f) sp[2 * ns] = sx, sp[2 * ns + 1] = sy, ns++;
            aa_strip(sp, ns, 3, SUN);
            const gfx_tex *g = sky_glow_tex();
            if (g) tex_sub(g, sx - 50, sy - 50, 100, 100, 0, 0, 1, 1, 0xffd36b70);
            aa_disc(sx, sy, 10, SUN);
        } else aa_disc(sx, sy, 7, 0xe9edf5c0);              /* night: a small moon at the night's fraction */
    }
    text_c(ax0 - 10, acy + 4, F_SMALL | TNUM, rise < 0 ? DIM : GREY, "%s", rise < 0 ? "-" : b->sunrise);
    text_c(ax1 + 10, acy + 4, F_SMALL | TNUM, set < 0 ? DIM : GREY, "%s", set < 0 ? "-" : b->sunset);
    /* the moon: the sphere at the real phase, its words (no key needed: the clock) */
    double age = moon_age(utc);
    float md = wide ? 112 * (sc < 1 ? sc : 1) : 96, mx = x + half + 40, my = rnd(y + (h - md) / 2);
    const gfx_tex *m = sky_moon_tex();
    if (m) tex_sub(m, mx, my, md, md, 0, 0, 1, 1, 0xffffffff);
    else disc(mx + md / 2, my + md / 2, md / 2, 0x1a2234ff);
    float tx = mx + md + 28, tw = x + w - 24 - tx, cy = y + h / 2;
    text_fit(tx, rnd(cy - 50), F_MED, WHITE, tw, "%s", moon_phase_name[moon_phase(age)]);
    text_fit(tx, rnd(cy - 8), F_BODY, GREY, tw, "%d%% lit", (int)(moon_lit(age) * 100 + 0.5));
    moon_next_words(nx, sizeof nx, utc, off);
    text_fit(tx, rnd(cy + 28), F_SMALL, DIM, tw, "%s", nx);
}

/* ---- the three days ------------------------------------------------------------------ */

/* The three days: the day, the M icon at 1.25 times (80 px) that bobs 4 px
 * on 5 s, sways 3 px on 7 s and tilts 2.5 deg on 6 s, each card a third of
 * a period apart, by fractions of a pixel (icon_rot, bilinear); max and
 * min; the rain chip. Before the first reply: the skeleton. */
static void days(float x, float y, float w, float h, const board_data *b)
{
    float cw = (w - 2 * G) / 3;
    for (int i = 0; i < 3; i++) {
        const board_day *d = board_have && i < b->n_day ? &b->day[i] : NULL;
        float cx = rnd(x + (float)i * (cw + G));
        tile(cx, y, rnd(cw), h);
        if (!board_have) {
            skel_bar(cx + 24, y + 20, 90, 28);
            skel_bar(cx + 24, y + h - 70, 130, 26);
            continue;
        }
        text_fit(cx + 24, y + 18, wide ? F_MED : F_BODY, d ? WHITE : DIM, cw - 130, "%s", d ? d->label : "-");
        int k = d && i < b->n_day_code ? wmo_kind(b->day_code[i]) : WK_NONE;
        float ph = (float)i / 3, still = (float)fx_reduced();
        float bob = still ? 0 : 4 * sinf(2 * PI_F * ((float)T / 5 + ph)), sway = still ? 0 : 3 * sinf(2 * PI_F * ((float)T / 7 + ph));
        float tilt = still ? 0 : 2.5f * PI_F / 180 * sinf(2 * PI_F * ((float)T / 6 + ph));
        wx_icon_m(wx_icon(k, 1), cx + cw - 66 + sway, y + h / 2 - 2 + bob, tilt, wide ? 1.25f : 1.1f);
        float yy = rnd(y + h - (wide ? 76 : 66)), xx;
        xx = text(cx + 24, yy, F_MED | TNUM, d && !miss(d->max) ? WHITE : DIM, "%s", d ? d->max : "-");
        text(xx + 8, yy + (float)base_dy(F_MED, F_BODY), F_BODY | TNUM, GREY, "/ %s", d ? d->min : "-");
        int has_rain = d && !miss(d->rain);
        icon(IC_S_DROP, cx + 36, yy + 52, has_rain ? BLUE : DIM);
        text(cx + 56, yy + 38, F_SMALL | TNUM, has_rain ? BLUE : DIM, "%s", has_rain ? d->rain : "-");
    }
}

/* ---- the money ----------------------------------------------------------------------- */

static const board_usd *feature(const board_data *b)
{
    for (int i = 0; board_have && i < b->n_usd; i++)
        if (!strcmp(b->usd[i].label, "Blue")) return &b->usd[i];
    return NULL;
}

/* Blue's tile (design 1.4): the price, Buy and the time, the chip, the 30
 * days' range and sparkline. */
static void blue(float x, float y, float w, float h, const board_data *b)
{
    char cnt[32], warn[160], r[48];
    const board_usd *u = feature(b);
    const char *label = u ? u->label : "Blue";
    int stale = feed_warn(warn, sizeof warn, &b->usd_feed);
    tile(x, y, w, h);
    icon(IC_S_CURRENCY_DOLLAR, x + 40, y + 37, GREEN);
    text(x + 62, y + 18, F_MED, WHITE, "%s", label);
    if (!board_have) {
        skel_bar(x + 30, rnd(y + (wide ? 70 * sc : 56)), 300, wide ? 70 : 56);
        skel_bar(x + w - 220, y + 70, 196, 26);
        skel_bar(x + 30, y + h - (wide ? 50 : 36), w - 60, 18);
        return;
    }
    float cl = chip(x + w - 24, y + 20, board_have ? board_chg_find(b, 0, label) : NULL, 0);
    const board_spark *sp = board_have ? board_spark_find(b, label) : NULL;
    int lo = 0, hi = 0, nv = sp ? spark_range(sp, &lo, &hi) : 0;
    float rr = cl < x + w - 24 ? cl - 16 : x + w - 24;
    if (stale) text_fit(x + 160, y + 23, F_SMALL, YELLOW, rr - x - 160, "%s", warn);
    else {
        board_num a = {lo, 0, 0, 0, 0}, c = {hi, 0, 0, 0, 0};
        char sl[24], sh[24];
        num_format(sl, sizeof sl, &a);
        num_format(sh, sizeof sh, &c);
        if (nv) snprintf(r, sizeof r, "30 d: %s to %s", sl, sh);
        else snprintf(r, sizeof r, "30 d: -");
        text_r(rr, y + 23, F_SMALL, DIM, "%s", r);
    }
    float py0 = rnd(y + (wide ? 52 * sc : 40));
    const char *v = u && !miss(u->sell) ? shown(label, u->sell, cnt, sizeof cnt) : "-";
    value(x + 30, py0, D_M, 0, F_LARGE, v[0] == '-' && !v[1] ? DIM : WHITE, v);
    text_r(x + w - 24, rnd(py0 + 18), F_BODY | TNUM, GREY, "Buy %s", u ? u->buy : "-");
    text_r(x + w - 24, rnd(py0 + 54), F_SMALL, stale ? YELLOW : DIM, "%s", u ? u->when : "-");
    /* the sparkline: the present days, a faint area, the last value's dot */
    float sx0 = x + 30, sx1 = x + w - 30, sy1 = rnd(y + h - (wide ? 26 : 18)), sy0 = sy1 - (wide ? 56 : 36);
    float floor_y = py0 + big_asc(D_M) + 12;
    if (sy0 < floor_y) sy0 = floor_y;
    if (!nv) {
        batch_begin(DIM);
        for (float xx = sx0; xx < sx1; xx += 16) batch_line(xx, sy1, xx + 8, sy1, 2);
        batch_end();
        return;
    }
    float pts[2 * SPARK_MAX];
    int np = 0;
    for (int i = 0; i < sp->n; i++) {
        if (sp->v[i] < 0) continue;
        pts[2 * np] = sx0 + (sx1 - sx0) * (float)i / (float)(sp->n > 1 ? sp->n - 1 : 1);
        pts[2 * np + 1] = hi > lo ? sy1 - (sy1 - sy0) * (float)(sp->v[i] - lo) / (float)(hi - lo) : (sy0 + sy1) / 2;
        np++;
    }
    if (np >= 2) {
        area(pts, np, sy1, 0x4fd98240, 0x4fd98200);
        aa_strip(pts, np, 2, GREEN);
    }
    aa_disc(pts[2 * np - 2], pts[2 * np - 1], 5, WHITE);
}

/* A small tile's words: the label (and in narrow form its first word),
 * the foot: "buy 1.485,00, Fri 15:00" for a rate, the date for an extra
 * ("TNA, 14:47" when the label ends in "(TNA)"). */
typedef struct {
    const char *label, *val, *when, *buy;
    const board_chg *chg;
    int money, none, stale;
    const char *reason;
} item;

static void small_tile(float x, float y, float w, float h, const item *it)
{
    char lab[48], foot[96], paren[24] = "", cnt[32];
    const char *p = strstr(it->label, " (");
    int ln = p ? (int)(p - it->label) : (int)strlen(it->label);
    if (p) {                                                 /* "Plazo fijo BNA (TNA)": the label, and "TNA" to the foot */
        snprintf(paren, sizeof paren, "%s", p + 2);
        char *e = strchr(paren, ')');
        if (e) *e = 0;
    }
    snprintf(lab, sizeof lab, "%.*s", ln, it->label);
    tile(x, y, w, h);
    float cl = chip(x + w - 16, y + (wide ? 12 : 8), it->chg, it->money);
    u32 fc = it->stale ? YELLOW : DIM;
    if (it->none) snprintf(foot, sizeof foot, "%s", it->reason[0] ? it->reason : "no data");
    else if (!it->money) snprintf(foot, sizeof foot, "buy %s, %s", it->buy, it->when);
    else if (paren[0]) snprintf(foot, sizeof foot, "%s, %s", paren, it->when);
    else snprintf(foot, sizeof foot, "%s", it->when);
    if (wide) {
        /* 0.3.2: two rows, the label and, on its right, the time or the date (the chip when the value
         * moved), then the value; a rate's buy price is the Blue card's only */
        if (!it->money && !it->none) snprintf(foot, sizeof foot, "%s", it->when);
        if (cl >= x + w - 16 && text_w(F_SMALL, lab) + text_w(F_SMALL, foot) + 14 > w - 44) {   /* no room: a shorter foot */
            char *cm = strstr(foot, ", "), *sp = strchr(foot, ' ');
            if (cm) memmove(foot, cm + 2, strlen(cm + 2) + 1);                /* "TNA, 14:47" -> "14:47" */
            else if (sp) *sp = 0;                                             /* "Aug 2026" -> "Aug" */
        }
        float fw = cl < x + w - 16 ? 0 : (float)text_w(F_SMALL, foot), lw = (cl < x + w - 16 ? cl - 10 : x + w - 22 - fw - 14) - (x + 22);
        text_fit(x + 22, y + 14, F_SMALL, GREY, lw, "%s", lab);
        if (fw > 0) text_fit(rnd(x + w - 22 - fw), y + 14, F_SMALL, fc, fw, "%s", foot);
    } else {
        /* narrow (no room for a foot row): the label's first word and the time or the date on the label row */
        const char *sp = strchr(lab, ' ');
        int fw = sp ? (int)(sp - lab) : (int)strlen(lab);
        const char *t = it->none ? "" : it->when;
        if (!it->money && (sp = strrchr(it->when, ' '))) t = sp + 1;   /* a rate: "Fri 15:00" -> "15:00" */
        float room = cl - x - 32, lx = text(x + 22, y + 10, F_SMALL, GREY, "%.*s", fw, lab);
        if (t[0] && lx + 10 + text_w(F_SMALL, t) <= x + 22 + room) text(lx + 10, y + 10, F_SMALL, fc, "%s", t);
    }
    const char *v = it->none || miss(it->val) ? "-" : shown(it->label, it->val, cnt, sizeof cnt);
    /* wide: the value centred in the room under the label row */
    float r0 = y + 14 + (float)line_h(F_SMALL), vy = wide ? rnd(r0 + (y + h - r0 - (float)line_h(F_LARGE)) / 2 - 2) : rnd(y + 36);
    value(x + 22, vy, -1, F_LARGE, F_BODY, v[0] == '-' && !v[1] ? DIM : WHITE, v);
}

static int items(const board_data *b, item *out, int max)
{
    int n = 0, stale = !strcmp(b->usd_feed.state, "stale");
    const board_usd *f = feature(b);
    for (int i = 0; board_have && i < b->n_usd && n < max; i++) {
        const board_usd *u = &b->usd[i];
        if (u == f) continue;
        out[n++] = (item){u->label, u->sell, u->when, u->buy, board_chg_find(b, 0, u->label), 0, 0, stale, ""};
    }
    for (int i = 0; board_have && i < b->n_money && n < max; i++) {
        const board_money *m = &b->money[i];
        out[n++] = (item){m->label, m->value, m->when, "", board_chg_find(b, 1, m->label), 1, !strcmp(m->state, "none"),
                          !strcmp(m->state, "stale"), m->reason};
    }
    return n;
}

static void money(float x, float y, float w, float h, const board_data *b)
{
    item it[8];
    static int told;
    float fh = wide ? rnd(236 * sc) : 196, rh = (h - fh - RG - 3 * RG) / 4, gw = (w - RG) / 2;
    int n = items(b, it, 8);
    int more = board_have ? b->n_usd - (feature(b) ? 1 : 0) + b->n_money - n : 0;
    if (more > 0 && !told) {
        told = 1;
        couch_log("board: %d values do not fit the 8 tiles and are left off", more);
    }
    /* the halos first: a neighbour's glass covers a halo's inner part */
    const board_usd *f = feature(b);
    halo(x, y, w, fh, f ? f->label : "Blue");
    for (int i = 0; i < 8 && i < n; i++)
        halo(rnd(x + (float)(i % 2) * (gw + RG)), rnd(y + fh + RG + (float)(i / 2) * (rh + RG)), rnd(gw), rnd(rh), it[i].label);
    blue(x, y, w, fh, b);
    for (int i = 0; i < 8; i++) {
        float cx = rnd(x + (float)(i % 2) * (gw + RG)), cy = rnd(y + fh + RG + (float)(i / 2) * (rh + RG));
        if (i < n) small_tile(cx, cy, rnd(gw), rnd(rh), &it[i]);
        else if (!board_have) {                              /* no reply yet: the skeleton */
            tile(cx, cy, rnd(gw), rnd(rh));
            skel_bar(cx + 22, cy + 12, 110, 22);
            skel_bar(cx + 22, cy + (wide ? rh / 2 - 8 : 40), rnd(gw) * 0.6f, wide ? 40 : 36);
        }
    }
}

/* ---- the ticker and the bottom bar --------------------------------------------------- */

/* The ticker (design 1.8): a glass band over the screen's width (it moves
 * with its page in a slide: page_dx), the headlines moving right to left
 * at TICKER_PX_S by fractions of a pixel (marquee.c: the text as a
 * bilinear picture), apart by a dot, the pill of the headline at the pill's
 * place fixed at the canvas's left over them. */
static void ticker(float y, float h, const board_data *b)
{
    const float sep = 56;
    float ox, oy, wsum = 0, ws[BOARD_NEWS];
    screen_origin(&ox, &oy);
    ox += (float)page_dx;
    fx_glass(ox, y, 1920, h, 0, (glass_tint() & ~0xffu) | 0xa8);
    rect(ox, y, 1920, 1, GLASS_EDGE);
    for (int i = 0; i < b->n_news; i++) {                    /* measured as text() draws it: its format buffer holds 320 bytes */
        char t[320];
        snprintf(t, sizeof t, "%s", b->news[i].title);
        wsum += ws[i] = (float)text_w(F_BODY, t) + sep;
    }
    float pill = (float)text_w(F_SMALL, "Infobae") + 28;     /* one width for every source */
    float start = MG + pill + 20, off = (float)ticker_offset(T, TICKER_PX_S, (double)wsum);
    float ty = rnd(y + (h - (float)line_h(F_BODY)) / 2), x = start - off;
    while (x > ox) x -= wsum;                                /* from the screen's left edge */
    const char *under = b->news[0].source;
    for (int i = 0; x < ox + 1920; i = (i + 1) % b->n_news) {
        if (x + ws[i] > ox) {
            marquee_title(i, x, ty, WHITE);
            aa_disc(x + ws[i] - sep / 2, y + h / 2, 3, GREY);
        }
        if (x <= MG + pill && MG + pill < x + ws[i]) under = b->news[i].source;   /* the headline at the pill's right end */
        x += ws[i];
    }
    /* the text goes under the pill: a dark block from past the screen's left edge to the pill's right */
    round_rect(ox - 40, y + 6, MG + pill + 12 - (ox - 40), h - 12, (h - 12) / 2, GLASS_TINT | 0xff);
    aa_corners(ox - 40, y + 6, MG + pill + 12 - (ox - 40), h - 12, (h - 12) / 2, GLASS_TINT | 0xff);
    round_rect(MG, y + 10, pill, h - 20, (h - 20) / 2, source_colour(under));
    aa_corners(MG, y + 10, pill, h - 20, (h - 20) / 2, source_colour(under));
    text_fit(MG + 14, rnd(y + (h - (float)line_h(F_SMALL)) / 2), F_SMALL, WHITE, pill - 20, "%s", under);
}

/* The bottom bar (design 1.2): the status and the schedule on the left
 * (board_url.txt's note after them in YELLOW), the buttons on the right;
 * msg in place of the status when it is not NULL. */
void board_bar(float y, const char *msg, u32 msg_color)
{
    static const char hints[] = "L1/R1 pages  SQUARE refresh  SELECT info  START exit";
    char st[300], nx[48], line[400];
    u32 c = msg_color;
    if (msg) snprintf(line, sizeof line, "%s", msg);
    else {
        c = board_status_words(st, sizeof st);
        board_next_words(nx, sizeof nx);
        if (!board_fetches && !strncmp(nx, "Fetching", 8)) nx[0] = 0;   /* the status says it already */
        int n = (int)strlen(st);
        if (n && st[n - 1] == '.') st[--n] = 0;
        if (nx[0] && !strncmp(st, "Board updated", 13)) {
            nx[0] = (char)(nx[0] >= 'A' && nx[0] <= 'Z' ? nx[0] + 32 : nx[0]);
            snprintf(line, sizeof line, "%s, %s", st, nx);
        } else if (nx[0]) snprintf(line, sizeof line, "%s. %s.", st, nx);
        else snprintf(line, sizeof line, "%s.", st);
    }
    bar_draw(y, line, c, board_url_note, hints);
}

/* ---- the page ------------------------------------------------------------------------ */

/* The Board's body (design 1.4): the left column, the right column and the
 * ticker, each rising into place in the start animation (start.h). */
void board_body(void)
{
    const board_data *b = &board_cur;
    T = sky_time();
    deg = utf8_rc == 0 ? "\xc2\xb0" : "";
    wide = page_wide();
    changes();
    int news = board_have && b->n_news > 0;
    float top = page_top(), tick_h = wide ? 52 : 48, ty = SH - (wide ? 112 : 100);
    float bot = news ? SH - (wide ? 124 : 112) : ty + tick_h - 12, ch = bot - top;
    float Wi = SW - 2 * MG, lw = rnd(0.585f * Wi), rw = Wi - lw - G;
    sc = wide ? ch / 800 : 1;
    float hero_h = wide ? rnd(440 * sc) : 360, sky_h = wide ? rnd(160 * sc) : 128;   /* narrow: the strip takes the rest */
    float strip_h = ch - hero_h - sky_h - 2 * RG;
    shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_LEFT))));
    hero(MG, top, lw, hero_h, b);
    sky_tile(MG, top + hero_h + RG, lw, sky_h, b);
    days(MG, top + hero_h + sky_h + 2 * RG, lw, strip_h, b);
    shift_pop();
    shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_RIGHT))));
    money(MG + lw + G, top, rw, ch, b);
    shift_pop();
    if (news) {
        shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_BOTTOM))));
        ticker(ty, tick_h, b);
        shift_pop();
    }
}
