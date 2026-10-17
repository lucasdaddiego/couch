/* Text and glass that scroll by fractions of a pixel (stext.h). The atlases
 * are read as marquee.c reads them (ps3gfx's archive members fonts.bin and
 * latin.bin, the block layout of art/make_font.py); a byte or a code point
 * finds its glyph as ui.c's glyph and glyph_utf8 find it (marquee.c's
 * lmap: text_utf8.c's map). One texture per font: the font's ASCII cells,
 * then its Latin cells, 16 to a row, each cell one texel larger on every
 * side than the biggest of the two atlases' cells, its texels at (1, 1) of
 * its place: a bilinear sample at a cell's edge mixes in a clear texel,
 * never a neighbour's ink. A4R4G4B4, white also where the alpha is 0 (no
 * black at an edge). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fx.h>
#include "couch.h"
#include "page.h"
#include "spu_fx_run.h"
#include "stext.h"

extern const u8 fonts_bin[];                 /* ps3gfx's archive members (bin2s) */
extern const u32 fonts_bin_size;
extern const u8 latin_bin[];
extern const u32 latin_bin_size;

#define NF 4                                 /* the fonts kept: F_SMALL, F_BODY, F_MED, F_TITLE */
#define COLS 16
#define LATIN_MAX 128
#define BANDS_A_FRAME 4                      /* rows of cells built a frame (about 1 ms) */
static const int font_of[NF] = {F_SMALL, F_BODY, F_MED, F_TITLE};

typedef struct {
    int cw, ch, base, pad, asc, aw, count;
    const u8 *adv, *px;                      /* the advances, the 4-bit alphas */
} atlas;

static struct {
    atlas a[2];                              /* 0 fonts.bin, 1 latin.bin */
    int cellw, cellh, rows;
    gfx_tex tex;
    u16 *dst;                                /* the texture in RSX memory */
} F[NF];

static int first_char, have_latin, ncp, square = -1;
static u16 cps[LATIN_MAX];
static int state;                            /* 0 not started, 1 building, 2 ready, -1 off */
static int b_font, b_row;                    /* the next row of cells to build */
static u16 row_buf[16 * 60 * 64];            /* one row of cells: at most 16 x 60 texels wide, 64 rows */

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
static const u8 *al4(const u8 *base, const u8 *p) { return p + (4 - (p - base) % 4) % 4; }

/* The font blocks of a file: font k's fields into *a. 0 when the file is short. */
static int font_block(const u8 *base, const u8 *end, const u8 *p, int nfonts, int count, int want, atlas *a)
{
    for (int k = 0; k < nfonts; k++) {
        if (end - p < 16 + count) return 0;
        atlas f = {(int)rd16(p), (int)rd16(p + 2), (int)rd16(p + 4), (int)rd16(p + 6), (int)rd16(p + 8), (int)rd16(p + 12), count,
                   p + 16, NULL};
        int ah = (int)rd16(p + 14);
        p = al4(base, p + 16 + count);
        long npx = (long)f.aw * ah;
        if (!npx || (npx & 1) || end - p < npx / 2) return 0;
        f.px = p;
        p = al4(base, p + npx / 2);
        if (k == want) {
            *a = f;
            return 1;
        }
    }
    return 0;
}

static int parse(void)
{
    const u8 *p = fonts_bin, *end = fonts_bin + fonts_bin_size;
    if (fonts_bin_size < 12 || memcmp(p, "PHF1", 4)) return 0;
    int n = (int)rd16(p + 4), count = (int)rd16(p + 8), cols = (int)rd16(p + 10);
    first_char = (int)rd16(p + 6);
    if (n <= F_TITLE || cols != COLS || count > 112) return 0;
    for (int i = 0; i < NF; i++)
        if (!font_block(fonts_bin, end, p + 12, n, count, font_of[i], &F[i].a[0])) return 0;
    p = latin_bin;
    end = latin_bin + latin_bin_size;
    if (utf8_rc == 0 && latin_bin_size >= 12 && !memcmp(p, "PHL1", 4)) {
        int ln = (int)rd16(p + 4), lc = (int)rd16(p + 6), lcols = (int)rd16(p + 8);
        if (ln > F_TITLE && lc >= 1 && lc <= LATIN_MAX && lcols == COLS && end - (p + 12) >= 2 * lc) {
            for (int i = 0; i < lc; i++) cps[i] = (u16)rd16(p + 12 + 2 * i);
            ncp = lc;
            have_latin = 1;
            for (int i = 0; i < NF && have_latin; i++)
                have_latin = font_block(latin_bin, end, al4(latin_bin, p + 12 + 2 * lc), ln, lc, font_of[i], &F[i].a[1]);
        }
    }
    for (int i = 0; i < NF; i++) {
        int cw = F[i].a[0].cw, ch = F[i].a[0].ch, g = F[i].a[0].count;
        if (have_latin) {
            if (F[i].a[1].cw > cw) cw = F[i].a[1].cw;
            if (F[i].a[1].ch > ch) ch = F[i].a[1].ch;
            g += F[i].a[1].count;
        }
        F[i].cellw = cw + 2;
        F[i].cellh = ch + 2;
        F[i].rows = (g + COLS - 1) / COLS;
        if (F[i].cellw * COLS > 16 * 60 || F[i].cellh > 64) return 0;
    }
    return 1;
}

/* text_utf8.c's map (marquee.c's lmap): a code point from U+0080 as its
 * slot in latin.bin, 0x100 + an ASCII base letter, or -1 */
static const char base_a[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
                             "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static int lmap(unsigned cp)
{
    int lo = 0, hi = ncp - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (cps[m] == cp) return m;
        if (cps[m] < cp) lo = m + 1;
        else hi = m - 1;
    }
    if ((cp >= 0x300 && cp < 0x370) || (cp >= 0x200b && cp <= 0x200f) || cp == 0x2060 || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff)
        return -1;
    if (cp >= 0x100 && cp < 0x180) return 0x100 + base_a[cp - 0x100];
    return square;
}

/* ui.c's utf8_at: the code point at s[i] (n bytes), its length */
static unsigned utf8_at(const char *s, int n, int i, int *len)
{
    const unsigned char *p = (const unsigned char *)s + i;
    unsigned c = p[0], cp;
    int k = c >= 0xf5 ? 0 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 0;
    *len = 1;
    if (c < 0x80) return c;
    if (!k) return 0xfffd;
    cp = c & (0x7f >> k);
    for (int j = 1; j < k; j++) {
        if (i + j >= n || !p[j]) {
            *len = j;
            return 0;
        }
        if ((p[j] & 0xc0) != 0x80) return 0xfffd;
        cp = cp << 6 | (p[j] & 0x3f);
    }
    if ((k == 3 && cp < 0x800) || (k == 4 && (cp < 0x10000 || cp > 0x10ffff)) || (cp >= 0xd800 && cp < 0xe000)) return 0xfffd;
    *len = k;
    return cp;
}

/* The glyph at s[i]: its atlas (0, 1; -1 for none), its slot, its length. */
static int glyph_at(const char *s, int n, int i, int *len, int *slot)
{
    unsigned c = (unsigned char)s[i];
    int m;
    if (have_latin) {
        unsigned cp = utf8_at(s, n, i, len);
        m = !cp ? -1 : cp < 0x80 ? 0x100 + (int)cp : lmap(cp);
    } else {
        *len = 1;
        m = 0x100 + (int)c;                  /* bytes: ui.c's glyph() ('?' for a byte with no slot) */
    }
    if (m < 0) return -1;
    if (m >= 0x100) {
        int k = (m - 0x100) - first_char;
        *slot = k < 0 || k >= F[0].a[0].count ? '?' - first_char : k;
        return 0;
    }
    *slot = m;
    return 1;
}

/* ---- the build ------------------------------------------------------------------------- */

/* Row r of cells of font i into the texture: the clear texels, then each cell's alphas. */
static void build_row(int i, int r)
{
    int W = F[i].cellw * COLS, H = F[i].cellh, nascii = F[i].a[0].count, total = nascii + (have_latin ? F[i].a[1].count : 0);
    for (int k = 0; k < W * H; k++) row_buf[k] = 0x0fff;
    for (int c = 0; c < COLS; c++) {
        int g = r * COLS + c;
        if (g >= total) break;
        const atlas *a = g < nascii ? &F[i].a[0] : &F[i].a[1];
        int slot = g < nascii ? g : g - nascii;
        int sx = slot % COLS * a->cw, sy = slot / COLS * a->ch;
        for (int y = 0; y < a->ch; y++) {
            u16 *d = row_buf + (1 + y) * W + c * F[i].cellw + 1;
            long row = (long)(sy + y) * a->aw + sx;
            for (int x = 0; x < a->cw; x++) {
                long idx = row + x;
                unsigned v = (a->px[idx >> 1] >> ((idx & 1) ? 0 : 4)) & 15;
                d[x] = (u16)(v << 12 | 0x0fff);
            }
        }
    }
    u8 *base = (u8 *)F[i].dst + (u32)(r * H) * F[i].tex.pitch;
    for (int y = 0; y < H; y++) {                /* 32-bit stores into RSX memory (marquee.c's) */
        volatile u32 *d = (volatile u32 *)(base + (u32)y * F[i].tex.pitch);
        const u16 *src = row_buf + y * W;
        for (int x = 0; x < W / 2; x++) {
            u32 v;
            memcpy(&v, src + 2 * x, 4);
            d[x] = v;
        }
    }
}

int stext_tick(void)
{
    if (state == 2 || state < 0) return state == 2;
    if (state == 0) {
        if (!parse()) {
            state = -1;
            couch_log("stext: the atlases did not parse, the reader's text moves by whole pixels");
            return 0;
        }
        if (have_latin) square = lmap(0x25a1) >= 0 && lmap(0x25a1) < ncp ? lmap(0x25a1) : -1;
        u32 bytes = 0;
        for (int i = 0; i < NF; i++) {
            F[i].dst = gfx_tex_new(&F[i].tex, (u16)(F[i].cellw * COLS), (u16)(F[i].cellh * F[i].rows), GFX_TEX_A4R4G4B4, 1);
            if (!F[i].dst) {
                state = -1;
                couch_log("stext: no RSX memory for the atlases, the reader's text moves by whole pixels");
                return 0;
            }
            bytes += (u32)F[i].tex.pitch * F[i].tex.h;
        }
        frame_ev |= EV_TEX;
        couch_log("stext: the reader's atlases, %u KB of RSX memory%s", (unsigned)(bytes / 1024), have_latin ? "" : " (ASCII only)");
        state = 1;
        b_font = b_row = 0;
    }
    for (int k = 0; k < BANDS_A_FRAME && b_font < NF; k++) {
        build_row(b_font, b_row);
        if (++b_row >= F[b_font].rows) {
            b_row = 0;
            b_font++;
        }
    }
    frame_ev |= EV_TEX;
    if (b_font >= NF) state = 2;
    return state == 2;
}

/* ---- the band ----------------------------------------------------------------------------- */

static int band_on;
static float bt, bb, ft, fb;

void stext_band(float top, float bot, float fade_top, float fade_bot)
{
    band_on = 1;
    bt = top;
    bb = bot;
    ft = fade_top > 0 ? fade_top : 0;
    fb = fade_bot > 0 ? fade_bot : 0;
}

void stext_band_off(void) { band_on = 0; }

/* The fade at y: 1 inside, falling to 0 at the band's edges over the fades. */
static float mask(float y)
{
    if (!band_on) return 1;
    float k = 1;
    if (ft > 0 && y < bt + ft) k = (y - bt) / ft;
    if (fb > 0 && y > bb - fb) {
        float k2 = (bb - y) / fb;
        if (k2 < k) k = k2;
    }
    return k < 0 ? 0 : k > 1 ? 1 : k;
}

float stext_alpha(float y0, float y1)
{
    if (!band_on) return 1;
    if (y0 < bt || y1 > bb) return 0;
    return mask((y0 + y1) / 2);              /* the fade at its middle: about the text's mean in it */
}

u32 alpha_times(u32 rgba, float k)
{
    if (k >= 1) return rgba;
    if (k <= 0) return rgba & ~0xffu;
    return (rgba & ~0xffu) | (u32)((float)(rgba & 0xff) * k + 0.5f);
}

/* ---- the text ----------------------------------------------------------------------------- */

static int slot_of_font(int font)
{
    for (int i = 0; i < NF; i++)
        if (font_of[i] == (font & 0xff) && !(font & TNUM)) return i;
    return -1;
}

float stext_w(int font, const char *s, int n)
{
    char b[320];
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    memcpy(b, s, (size_t)n);
    b[n] = 0;
    return (float)text_w(font, b);
}

/* text() at the nearest pixel, the line inside the band only (no atlases yet). */
static float plain(float x, float y, int font, u32 rgba, const char *s, int n)
{
    float yr = floorf(y + 0.5f), k = stext_alpha(yr, yr + (float)line_h(font));
    if (n > 319) n = 319;
    if (k <= 0) return floorf(x + 0.5f) + stext_w(font, s, n);
    return text(x, yr, font, alpha_times(rgba, k), "%.*s", n, s);
}

float stext(float x, float y, int font, u32 rgba, const char *s, int n)
{
    int fi = slot_of_font(font);
    if (fi < 0 || state != 2) return plain(x, y, font, rgba, s, n);
    float px = floorf(x + 0.5f), lo = band_on ? bt : -1e9f, hi = band_on ? bb : 1e9f;
    if (y + 80 < lo || y - 80 > hi) return px + stext_w(font, s, n);   /* far outside: the pen only */
    float W = (float)F[fi].tex.w, H = (float)F[fi].tex.h;
    int nascii = F[fi].a[0].count;
    gfx_texture(&F[fi].tex);
    for (int i = 0, len = 1; i < n && s[i]; i += len) {
        int slot = 0, which = glyph_at(s, n, i, &len, &slot);
        if (which < 0) continue;
        const atlas *a = &F[fi].a[which];
        int g = which ? nascii + slot : slot;
        if (!(which == 0 && s[i] == ' ')) {
            float x0 = px - (float)a->pad, x1 = x0 + (float)a->cw;
            float y0 = y + (float)(a->asc - a->base), y1 = y0 + (float)a->ch;
            float u0 = (float)(g % COLS * F[fi].cellw + 1) / W, u1 = u0 + (float)a->cw / W;
            float v0 = (float)(g / COLS * F[fi].cellh + 1) / H, v1 = v0 + (float)a->ch / H;
            if (y1 > lo && y0 < hi) {
                if (y0 < lo) {                   /* the band's cut: the quad and its texels */
                    v0 += (v1 - v0) * (lo - y0) / (y1 - y0);
                    y0 = lo;
                }
                if (y1 > hi) {
                    v1 -= (v1 - v0) * (y1 - hi) / (y1 - y0);
                    y1 = hi;
                }
                u32 c0 = alpha_times(rgba, mask(y0)), c1 = alpha_times(rgba, mask(y1));
                if ((c0 | c1) & 0xff) {
                    gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
                    q = gfx_put(q, x0, y0, c0, u0, v0);
                    q = gfx_put(q, x1, y0, c0, u1, v0);
                    q = gfx_put(q, x1, y1, c1, u1, v1);
                    gfx_put(q, x0, y1, c1, u0, v1);
                }
            }
        }
        px += (float)a->adv[slot];
    }
    return px;
}

float stext_fit(float x, float y, int font, u32 rgba, float maxw, const char *s)
{
    int n = (int)strlen(s);
    if (stext_w(font, s, n) <= maxw) return stext(x, y, font, rgba, s, n);
    float ell = (float)text_w(font, "...");
    char b[330];
    int k = 0;
    for (int i = 0, len; i < n && i < 300; i += len) {     /* whole code points that fit with the dots */
        len = 1;
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xc2 && c <= 0xf4) len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
        if (i + len > n) break;
        if (stext_w(font, s, i + len) + ell > maxw) break;
        k = i + len;
    }
    memcpy(b, s, (size_t)k);
    while (k > 0 && b[k - 1] == ' ') k--;
    memcpy(b + k, "...", 4);
    return stext(x, y, font, rgba, b, k + 3);
}

float stext_r(float xr, float y, int font, u32 rgba, const char *s)
{
    int n = (int)strlen(s);
    float x = floorf(xr - stext_w(font, s, n) + 0.5f);
    stext(x, y, font, rgba, s, n);
    return x;
}

/* ---- the glass ----------------------------------------------------------------------------- */

#define PMAX 64

/* pts (n points) cut to y in [a, b]: the points of the convex part into out; its count. */
static int clip_y(const float *p, int n, float a, float b, float *out)
{
    float tmp[2 * PMAX];
    int m = 0;
    for (int i = 0; i < n && m < PMAX - 1; i++) {            /* y >= a */
        const float *P = p + 2 * i, *Q = p + 2 * ((i + 1) % n);
        int pin = P[1] >= a, qin = Q[1] >= a;
        if (pin) tmp[2 * m] = P[0], tmp[2 * m + 1] = P[1], m++;
        if (pin != qin) {
            float t = (a - P[1]) / (Q[1] - P[1]);
            tmp[2 * m] = P[0] + (Q[0] - P[0]) * t, tmp[2 * m + 1] = a, m++;
        }
    }
    int k = 0;
    for (int i = 0; i < m && k < PMAX - 1; i++) {            /* y <= b */
        const float *P = tmp + 2 * i, *Q = tmp + 2 * ((i + 1) % m);
        int pin = P[1] <= b, qin = Q[1] <= b;
        if (pin) out[2 * k] = P[0], out[2 * k + 1] = P[1], k++;
        if (pin != qin) {
            float t = (b - P[1]) / (Q[1] - P[1]);
            out[2 * k] = P[0] + (Q[0] - P[0]) * t, out[2 * k + 1] = b, k++;
        }
    }
    return k;
}

/* A convex polygon cut by the band and drawn in up to three slabs (the top
 * fade, the middle, the bottom fade), so the fade is linear in each one: a
 * triangle's colours then give it exactly. t: the texture (NULL: colour
 * only); uv: u = uv[0] + x * uv[1], v = uv[2] + y * uv[3]. */
static void poly(const float *p, int n, const gfx_tex *t, const float *uv, u32 rgba)
{
    float cut[4] = {bt, bt + ft, bb - fb, bb}, q[2 * PMAX];
    if (cut[1] > cut[2]) cut[1] = cut[2] = (bt + bb) / 2;
    for (int s = 0; s < 3; s++) {
        if (cut[s + 1] <= cut[s]) continue;
        int m = clip_y(p, n, cut[s], cut[s + 1], q);
        if (m < 3) continue;
        gfx_texture(t);
        gfx_vtx *v = gfx_prim(GFX_TRIANGLES, 3 * (m - 2));
        if (!v) continue;
        for (int k = 1; k + 1 < m; k++) {
            const int idx[3] = {0, k, k + 1};
            for (int j = 0; j < 3; j++) {
                float x = q[2 * idx[j]], y = q[2 * idx[j] + 1];
                v = gfx_put(v, x, y, alpha_times(rgba, mask(y)), t ? uv[0] + x * uv[1] : 0, t ? uv[2] + y * uv[3] : 0);
            }
        }
    }
}

/* The outline of a rounded rect, clockwise from the top left corner's start: 4 x 7 points. */
#define ARC 6
static int outline(float x, float y, float w, float h, float r, float *p)
{
    static const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
    int n = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int c = 0; c < 4; c++) {
        float ox = x + (cx[c] ? w - r : r), oy = y + (cy[c] ? h - r : r), a0 = 3.14159265f * (1.0f + 0.5f * (float)c);
        for (int k = 0; k <= ARC; k++) {
            float a = a0 + 1.5707963f * (float)k / ARC;
            p[2 * n] = ox + r * cosf(a);
            p[2 * n + 1] = oy + r * sinf(a);
            n++;
        }
    }
    return n;
}

void mglass(float x, float y, float w, float h, float r, u32 tint)
{
    if (!band_on || (y >= bt + ft && y + h <= bb - fb)) {    /* wholly in the band: page.c's glass_tile */
        fx_glass(x, y, w, h, r, tint);
        round_frame(x, y, w, h, r, 1, GLASS_EDGE);
        rect(x + r, y + h - 1, w - 2 * r, 1, GLASS_SHADE);
        return;
    }
    if (y + h <= bt || y >= bb) return;
    float o[2 * 4 * (ARC + 1)], in[2 * 4 * (ARC + 1)];
    int n = outline(x, y, w, h, r, o);
    const gfx_tex *g = fx_glass_tex();
    if (g) {                                 /* fx_glass: the blurred sky behind, then the tint */
        float u0, v0, u1, v1, uv[4];
        glass_uv(x, y, w, h, &u0, &v0, &u1, &v1);
        uv[1] = (u1 - u0) / w;
        uv[0] = u0 - x * uv[1];
        uv[3] = (v1 - v0) / h;
        uv[2] = v0 - y * uv[3];
        poly(o, n, g, uv, 0xffffffff);
        poly(o, n, NULL, NULL, tint);
    } else {                                 /* RSX only: the tint as a solid panel (fx_glass's 0x30 more) */
        u32 a = (tint & 255) + 0x30;
        poly(o, n, NULL, NULL, (tint & ~0xffu) | (a > 255 ? 255 : a));
    }
    outline(x + 1, y + 1, w - 2, h - 2, r - 1, in);          /* the 1 px edge: a quad per outline step */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float qd[8] = {o[2 * i], o[2 * i + 1], o[2 * j], o[2 * j + 1], in[2 * j], in[2 * j + 1], in[2 * i], in[2 * i + 1]};
        poly(qd, 4, NULL, NULL, GLASS_EDGE);
    }
    float sh[8] = {x + r, y + h - 1, x + w - r, y + h - 1, x + w - r, y + h, x + r, y + h};
    poly(sh, 4, NULL, NULL, GLASS_SHADE);
}
