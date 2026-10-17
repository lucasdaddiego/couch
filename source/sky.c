/* The sky's own objects (sky.h). The PPU makes three textures once (the
 * round glow, the tile's halo) or every 6 h (the moon's sphere, 16 rows a
 * frame into main memory, then into the texture that no frame shows, then
 * the swap: design 3.6). The rule of an alpha texture on this RSX (design
 * 3.3): the RGB is set also where the alpha is 0, and an alpha under 0x30
 * gets a 4 x 4 ordered dither, so the alpha test at 0x10 makes grain, not
 * a step. */
#include <math.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fx.h>
#include "couch.h"
#include "sky.h"
#include "astro.h"
#include "board.h"
#include "clock.h"
#include "parse.h"
#include "spu_fx_run.h"

#define MOON_PX 160                  /* the sphere's texture */
#define MOON_ROWS 16                 /* rows a frame */
#define MOON_STEP 0.25               /* days of age between two pictures */
#define MOON_SKY 64                  /* the disc in the sky, px */
#define STARS 200
#define BOLT_N 24                    /* the bolt's points */
#define PI_F 3.14159265f

static const u32 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

double sky_time(void) { return (double)((s64)sysGetSystemTime() - t_start) / 1e6; }

/* A small integer hash: the stars' places, the bolts' shapes and gaps. */
static u32 hash(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static float hashf(u32 x) { return (float)(hash(x) & 0xffffff) / 16777216.0f; }

/* One texel: the alpha (0 to 1) with the dither under 0x30, and an RGB. */
static u32 texel(float a, u32 rgb, u32 x, u32 y)
{
    u32 v = (u32)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255.0f + 0.5f);
    if (v < 0x30) v = (v + bayer[y & 3][x & 3]) >> 4 << 4;
    return v << 24 | (rgb & 0xffffffu);
}

int make_tex(gfx_tex *t, u16 w, u16 h, float (*fn)(float, float))
{
    u8 *p = gfx_tex_new(t, w, h, GFX_TEX_A8R8G8B8, 1);
    if (!p) return 0;
    frame_ev |= EV_TEX;
    for (u32 y = 0; y < h; y++) {
        volatile u32 *row = (volatile u32 *)(p + y * t->pitch);
        for (u32 x = 0; x < w; x++) row[x] = texel(fn(((float)x + 0.5f) / w, ((float)y + 0.5f) / h), 0xffffff, x, y);
    }
    return 1;
}

/* The glow: full at the centre, 0 at the edge, a soft square fall-off. */
static float glow_fn(float u, float v)
{
    float dx = u - 0.5f, dy = v - 0.5f, r = sqrtf(dx * dx + dy * dy) * 2.0f;
    return r >= 1 ? 0 : (1 - r) * (1 - r);
}

/* The halo of glow9: a corner's quarter (32 texels) spans 2 pad on the
 * screen, from pad outside the rect (u 0) over the rect's edge (u 0.25) to
 * pad inside (u 0.5), and the other half mirrors it. So pad is 16 texels
 * and the tile's corner radius 13.3 of them (20 px of a 24 px pad); the
 * alpha falls from the rounded rect's edge to 0 at pad outside, and is 0
 * inside. */
static float halo_fn(float u, float v)
{
    const float pad = 16, r = 13.3f;
    float px = (0.5f - fabsf(u - 0.5f)) * 64 - pad, py = (0.5f - fabsf(v - 0.5f)) * 64 - pad;   /* texels inside the edge: > 0 */
    float qx = r - px, qy = r - py;                          /* from the corner circle's centre, r inside both edges */
    float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    float d = sqrtf(mx * mx + my * my) + (in < 0 ? in : 0) - r;   /* the rounded rect's signed distance */
    if (d <= 0) return 0;                                    /* inside the tile */
    float f = 1 - d / pad;
    return f <= 0 ? 0 : f;
}

static gfx_tex glow_t, halo_t;
static int glow_ok = -1, halo_ok = -1;

const gfx_tex *sky_glow_tex(void)
{
    if (glow_ok < 0) glow_ok = make_tex(&glow_t, 128, 128, glow_fn);
    return glow_ok ? &glow_t : NULL;
}

const gfx_tex *sky_halo_tex(void)
{
    if (halo_ok < 0) halo_ok = make_tex(&halo_t, 64, 64, halo_fn);
    return halo_ok ? &halo_t : NULL;
}

/* ---- the moon's sphere -------------------------------------------------- */

static u32 moon_buf[MOON_PX * MOON_PX];
static tex_pair moon_tp;
static int moon_tex = -1, moon_have, moon_row = -1, moon_south;
static double moon_done = -1000, moon_doing;
static float moon_lx, moon_lz;

/* The maria: a few darker discs (x, y, radius on the unit disc), fixed. */
static const float maria[7][3] = {{-0.30f, -0.25f, 0.24f}, {0.15f, -0.35f, 0.18f}, {0.30f, 0.05f, 0.22f}, {-0.10f, 0.20f, 0.15f},
                                  {-0.45f, 0.30f, 0.14f},  {0.10f, 0.45f, 0.12f},  {0.40f, -0.12f, 0.13f}};

/* Rows [y0, y1) of the sphere: lit by the sun at the phase angle, the
 * terminator soft over about 3 px, the maria, earthshine at 6 % on the
 * dark side, the rim smoothed over a pixel, alpha 0 outside (with the dark
 * side's RGB there). */
static void moon_rows(int y0, int y1)
{
    const float R = MOON_PX / 2.0f;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < MOON_PX; x++) {
            float nx = ((float)x + 0.5f - R) / R, ny = ((float)y + 0.5f - R) / R, rr = nx * nx + ny * ny;
            float rim = clampf((1 - sqrtf(rr)) * R + 0.5f, 0, 1);
            float nz = sqrtf(rr < 1 ? 1 - rr : 0);
            float lit = clampf((nx * moon_lx + nz * moon_lz + 0.019f) / 0.0375f, 0, 1);
            float alb = 1;
            for (int k = 0; k < 7; k++) {
                float dx = nx - maria[k][0], dy = ny - maria[k][1], d = sqrtf(dx * dx + dy * dy);
                alb -= 0.11f * clampf((maria[k][2] - d) / 0.05f, 0, 1);
            }
            float v = 0.94f * lit * alb * (0.75f + 0.25f * nz), e = 0.06f * (1 - lit) * alb * (0.6f + 0.4f * nz);
            u32 r = (u32)clampf(233 * v + 200 * e, 0, 255), g = (u32)clampf(237 * v + 215 * e, 0, 255), b = (u32)clampf(245 * v + 245 * e, 0, 255);
            if (rr >= 1) r = 12, g = 13, b = 15;
            moon_buf[y * MOON_PX + x] = texel(rr >= 1.05f ? 0 : rim, r << 16 | g << 8 | b, (u32)x, (u32)y);
        }
}

const gfx_tex *sky_moon_tex(void) { return moon_have ? tex_pair_shown(&moon_tp) : NULL; }

static void moon_tick(long utc)
{
    if (moon_tex < 0) moon_tex = tex_pair_new(&moon_tp, MOON_PX, MOON_PX, GFX_TEX_A8R8G8B8, 1) == 0;
    if (!moon_tex) return;
    double age = moon_age(utc);
    if (moon_row < 0) {
        int south = board_have && board_cur.has_geo && board_cur.lat < 0;   /* the north's view when there is no geo */
        double d = fabs(age - moon_done);
        if (d > MOON_SYNODIC / 2) d = MOON_SYNODIC - d;
        /* a new picture also when the place's side of the equator changed (0.5.1's review: the first picture,
         * made before the first reply in the north's view, stayed mirrored for up to 6 h in the south) */
        if (moon_have && d < MOON_STEP && south == moon_south) return;
        double th = 2 * 3.14159265358979 * age / MOON_SYNODIC;
        moon_south = south;
        moon_lx = (float)((moon_south ? -1 : 1) * sin(th));  /* waxing: lit on the right in the north, on the left in the south */
        moon_lz = (float)-cos(th);
        moon_doing = age;
        moon_row = 0;
    }
    int y1 = moon_row + MOON_ROWS > MOON_PX ? MOON_PX : moon_row + MOON_ROWS;
    moon_rows(moon_row, y1);
    moon_row = y1;
    if (moon_row < MOON_PX) return;
    tex_upload(tex_pair_wtex(&moon_tp), tex_pair_written(&moon_tp), moon_buf, MOON_PX);   /* after ui_flip: no frame draws it */
    tex_pair_swap(&moon_tp);
    frame_ev |= EV_MOON;
    moon_have = 1;
    moon_done = moon_doing;
    moon_row = -1;
}

/* ---- the tick ------------------------------------------------------------------------ */

static long moon_sec = -1;
static int moon_fetches = -1;
static double moon_elev = -90, moon_azim;
static int storm;
static double bolt_next = 6;         /* the bolts' times from the start: 6 s, then gaps of 6 to 20 s */
static double bolt_t0 = -1;          /* sky_time of the bolt that shows, -1: none */
static u32 bolt_k;
static float bolt[BOLT_N * 2];

/* The bolt's points (screen pixels from the screen's top left): from a top
 * point down to 60 % of the height, by midpoint displacement. */
static void bolt_split(int a, int b, u32 seed)
{
    if (b - a < 2) return;
    int m = (a + b) / 2;
    float len = fabsf(bolt[2 * b + 1] - bolt[2 * a + 1]) + fabsf(bolt[2 * b] - bolt[2 * a]);
    float t = (float)(m - a) / (float)(b - a);
    bolt[2 * m] = bolt[2 * a] + (bolt[2 * b] - bolt[2 * a]) * t + (hashf(seed * 31 + (u32)m) - 0.5f) * len * 0.45f;
    bolt[2 * m + 1] = bolt[2 * a + 1] + (bolt[2 * b + 1] - bolt[2 * a + 1]) * t;
    bolt_split(a, m, seed);
    bolt_split(m, b, seed);
}

static void bolt_make(u32 k)
{
    bolt[0] = 260 + hashf(k * 7 + 1) * 1400;
    bolt[1] = -20;
    bolt[2 * (BOLT_N - 1)] = bolt[0] + (hashf(k * 7 + 2) - 0.5f) * 500;
    bolt[2 * (BOLT_N - 1) + 1] = 1080 * 0.6f;
    bolt_split(0, BOLT_N - 1, k + 1);
}

/* The bolt's times after its start (3 frames on, 2 off, 2 on,
 * the flash 2 frames at 60 Hz; since the round of 2026-10-14 as times, so
 * a slow frame does not stretch them, each bound half a frame from the
 * frames): on to 42 ms, off to 75 ms, on to 108 ms; the flash to 25 ms. */
#define BOLT_ON1 0.042
#define BOLT_ON2 0.075
#define BOLT_END 0.108
#define BOLT_FLASH 0.025
static double bolt_age(void) { return bolt_t0 < 0 ? -1 : sky_time() - bolt_t0; }
int sky_bolt_on(void)
{
    double p = bolt_age();
    return p >= 0 && (p < BOLT_ON1 || (p >= BOLT_ON2 && p < BOLT_END));
}

void sky_tick(void)
{
    long utc = clock_utc();
    double t = sky_time();
    if (fx_state == FXS_NONE) return;
    moon_tick(utc);
    if (utc != moon_sec || board_fetches != moon_fetches) {   /* the moon's place each second and at each reply, as the sun's (scene()) */
        moon_sec = utc;                                       /* (0.5.1's review: once a minute, its disc jumped about 3 px) */
        moon_fetches = board_fetches;
        if (board_have && board_cur.has_geo) moon_pos(utc, board_cur.lat, board_cur.lon, &moon_elev, &moon_azim);
        else moon_elev = -90;
    }
    storm = board_have && board_cur.has_fx && board_cur.fx_code >= 95 && board_cur.fx_code <= 99;
    while (t >= bolt_next) {
        if (storm && t - bolt_next < 0.5) {                 /* a bolt now: a time long past (a pause) makes none */
            bolt_make(bolt_k);
            bolt_t0 = t;
            fx_flash(1);                                     /* the next pictures' clouds lit, fading */
        }
        bolt_k++;
        bolt_next += 6 + hash(bolt_k) % 15;
    }
    if (bolt_age() >= BOLT_END) bolt_t0 = -1;
}

/* ---- the draws ----------------------------------------------------------------------- */

/* A place in the sky as a screen point (spu_fx_run.c's rule for the sun):
 * 180 deg of azimuth over the width, facing the equator; elevation 0 at 80
 * % of the height, 60 deg at the top. */
static void sky_xy(double elev, double azim, float *x, float *y)
{
    float da = (float)azim - (board_have && board_cur.has_geo && board_cur.lat < 0 ? 0.0f : 180.0f);
    while (da > 180) da -= 360;
    while (da < -180) da += 360;
    *x = 960.0f + da / 90.0f * 960.0f;
    *y = 864.0f - (float)elev / 60.0f * 864.0f;
}

/* The stars: each its own brightness in every frame, so the twinkle is
 * smooth (stage 9 drew 7 levels, and a star jumped a level at a time);
 * one quad each, 64 to a primitive, all one draw. Each star's place, size,
 * brightness and twinkle come from its hash, made once (no division or
 * int-to-float conversion a frame). */
static struct {
    float b, f, p, s, x, y;          /* the brightness, the twinkle's rate and phase, the size, the place */
} star[STARS];
static int stars_made;

static void stars(float ox, float oy)
{
    float vis = clampf((float)(-fx_sc.elev - 4) / 8.0f, 0, 1) * (1.0f - 0.95f * fx_sc.cover);
    if (vis < 0.02f) return;
    float t = (float)sky_time();
    int still = fx_reduced();
    if (!stars_made) {
        for (u32 i = 0; i < STARS; i++) {
            u32 h = hash(i + 0x5a17);
            star[i].b = 0.3f + 0.7f * hashf(h);
            star[i].f = 1.1f + (float)(h % 7) * 0.35f;
            star[i].p = (float)(h >> 8 & 63);
            star[i].s = (h >> 20 & 15) < 2 ? 3 : 2;
            star[i].x = (float)(h % 1920);
            star[i].y = (float)((h >> 11) % 864);
        }
        stars_made = 1;
    }
    gfx_texture(NULL);
    for (u32 i0 = 0; i0 < STARS; i0 += 64) {
        u32 n = STARS - i0 < 64 ? STARS - i0 : 64;
        gfx_vtx *q = gfx_prim(GFX_QUADS, (int)(4 * n));
        for (u32 i = i0; i < i0 + n; i++) {
            float b = star[i].b;
            if (!still) b *= 0.65f + 0.35f * sinf(t * star[i].f + star[i].p);
            u32 c = 0xebf0ff00u | (u32)(b * 0xe0 * vis + 0.5f);
            float s = star[i].s, x = ox + star[i].x, y = oy + star[i].y;
            q = gfx_put(q, x, y, c, 0, 0);
            q = gfx_put(q, x + s, y, c, 0, 0);
            q = gfx_put(q, x + s, y + s, c, 0, 0);
            q = gfx_put(q, x, y + s, c, 0, 0);
        }
    }
}

static void sun(float ox, float oy)
{
    const gfx_tex *g = sky_glow_tex();
    float gain = fx_sc.sun_gain;
    if (fx_sc.elev < -2 || gain <= 0.01f) return;
    float x = ox + fx_sc.sun_x * 4, y = oy + fx_sc.sun_y * 4;
    if (g) tex_sub(g, x - 260, y - 260, 520, 520, 0, 0, 1, 1, 0xffd36b00u | (u32)(0x70 * gain));
    aa_disc(x, y, 30, 0xffe9a800u | (u32)(0xff * gain * (1 - fx_sc.cover)));   /* the disc only through a thin cover */
    if (fx_sc.cover >= 0.6f || fx_sc.elev <= 0 || fx_reduced()) return;
    /* the rays: 9 that turn 1 deg a second, each a soft wedge: its middle
     * line 0x30 at 40 px from the sun to 0 at its end, its two sides 0
     * (stage 9's hard-edged triangles crawled a pixel at a time as they
     * turned; now no edge has a step), the inner end under the disc */
    float turn = (float)sky_time() * PI_F / 180.0f;
    const float r0 = 40, L = 1300, w = 0.022f, h0 = 6;
    u32 c0 = 0xffd36b00u | (u32)(0x30 * gain), c1 = 0xffd36b00u;
    gfx_texture(NULL);
    gfx_vtx *q = gfx_prim(GFX_TRIANGLES, 9 * 12);
    for (int k = 0; k < 9; k++) {
        float a = turn + (float)k * (2 * PI_F / 9), ca = cosf(a), sa = sinf(a);
        float mx0 = x + ca * r0, my0 = y + sa * r0, mx1 = x + ca * L, my1 = y + sa * L;
        float l0x = mx0 - sa * h0, l0y = my0 + ca * h0, r0x = mx0 + sa * h0, r0y = my0 - ca * h0;
        float l1x = x + cosf(a - w) * L, l1y = y + sinf(a - w) * L, r1x = x + cosf(a + w) * L, r1y = y + sinf(a + w) * L;
        /* the near side: (l0, m0, m1) and (l0, m1, l1); the far side: (m0, r0, r1) and (m0, r1, m1) */
        q = gfx_put(q, l0x, l0y, c1, 0, 0), q = gfx_put(q, mx0, my0, c0, 0, 0), q = gfx_put(q, mx1, my1, c1, 0, 0);
        q = gfx_put(q, l0x, l0y, c1, 0, 0), q = gfx_put(q, mx1, my1, c1, 0, 0), q = gfx_put(q, l1x, l1y, c1, 0, 0);
        q = gfx_put(q, mx0, my0, c0, 0, 0), q = gfx_put(q, r0x, r0y, c1, 0, 0), q = gfx_put(q, r1x, r1y, c1, 0, 0);
        q = gfx_put(q, mx0, my0, c0, 0, 0), q = gfx_put(q, r1x, r1y, c1, 0, 0), q = gfx_put(q, mx1, my1, c1, 0, 0);
    }
}

void sky_behind(void)
{
    float ox, oy;
    if (fx_state == FXS_NONE) return;
    screen_origin(&ox, &oy);
    stars(ox, oy);
    const gfx_tex *m = sky_moon_tex();
    if (m && moon_elev > -1) {                               /* under the clouds: they pass in front of it */
        float x, y;
        sky_xy(moon_elev, moon_azim, &x, &y);
        u32 a = (u32)(0xff - 0x90 * fx_sc.light);
        tex_sub(m, ox + x - MOON_SKY / 2, oy + y - MOON_SKY / 2, MOON_SKY, MOON_SKY, 0, 0, 1, 1, 0xffffff00u | a);
    }
    sun(ox, oy);
}

void sky_front(void)
{
    float ox, oy;
    double p = bolt_age();
    screen_origin(&ox, &oy);
    if (sky_bolt_on()) {                                     /* over the clouds: a storm's deck would hide it */
        static const float wd[3] = {8, 4, 2};
        static const u32 col[3] = {0xc9a4ff30, 0xd8c4ff70, WHITE};
        for (int k = 0; k < 3; k++) {
            batch_begin(col[k]);
            for (int i = 0; i + 1 < BOLT_N; i++)
                batch_line(ox + bolt[2 * i], oy + bolt[2 * i + 1], ox + bolt[2 * i + 2], oy + bolt[2 * i + 3], wd[k]);
            batch_end();
        }
    }
    if (p >= 0 && p < BOLT_FLASH) rect(ox, oy, 1920, 1080, 0xffffff50);
}

void sky_scrims(void)
{
    float ox, oy;
    screen_origin(&ox, &oy);
    /* strong behind the header (to canvas y 120) and the bottom bar (from
     * canvas y SH - 130), then fading over 110 px, at any visible area (the
     * design's 0x70 and 0x80 from the screen's edges were too light on a
     * clear day at the narrow area) */
    rect_v(ox, oy, 1920, 120 - oy, 0x050a14a8, 0x050a1490);
    rect_v(ox, 120, 1920, 110, 0x050a1490, 0x050a1400);
    rect_v(ox, SH - 240, 1920, 110, 0x050a1400, 0x050a1490);
    rect_v(ox, SH - 130, 1920, oy + 1080 - (SH - 130), 0x050a1490, 0x050a14b0);
}
