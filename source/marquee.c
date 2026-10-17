/* The ticker's text as a picture (marquee.h). The atlases are read where
 * ps3gfx keeps them, its archive members fonts.bin and latin.bin (the
 * symbols bin2s gives them), in the block layout of art/make_font.py that
 * ui.c's fonts_init and text_utf8.c read: a big-endian header, then per font
 * the cell, the base line, the pad, the ascent, the line, the atlas size,
 * the advances and the 4-bit alphas (the left pixel in the high nibble).
 * The glyph of a byte or a code point, its cell and its advance are ui.c's
 * (glyph, glyph_utf8, run_utf8) and text_utf8.c's (map): an ASCII
 * character from fonts.bin ('?' for a byte with no slot), a code point from
 * U+0080 from latin.bin, a letter of Latin Extended-A as its base letter, a
 * combining mark or a zero-width code point nothing, any other the white
 * square. Two cells that overlap mix as two quads of the RSX's blend do: 1 -
 * (1 - a) (1 - b).
 *
 * The picture: A4R4G4B4, white also where the alpha is 0 (a bilinear
 * sample then mixes no black into an edge), MQ_W texels wide, one band of
 * rows per headline. A headline longer than the band is cut (the Worker cuts
 * a title to 110 code points: about 1,500 px in F_BODY). */
#include <string.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fx.h>
#include "couch.h"
#include "board.h"
#include "marquee.h"

extern const u8 fonts_bin[];                 /* ps3gfx's archive members (bin2s) */
extern const u32 fonts_bin_size;
extern const u8 latin_bin[];
extern const u32 latin_bin_size;

#define MQ_W 4096                            /* the picture's width: a headline at most */
#define MQ_PAD 16                            /* the pen's start in a band: room for the first cell's pad */
#define MQ_FONT F_BODY
#define LATIN_MAX 128

typedef struct {
    int cw, ch, base, pad, asc, aw, cols, count, first;
    const u8 *adv, *px;                      /* the advances, the alphas */
} atlas;

static atlas fa, la;                         /* F_BODY of fonts.bin and of latin.bin */
static u16 cps[LATIN_MAX];
static int ncp, square = -1, have_latin, parsed;
static int top0, band_h;                     /* the band's first row from the line top, its rows */
static tex_pair pic;
static int pic_ok = -1;
static u16 row_buf[64 * MQ_W];               /* one band's rows (band_h <= 64): 512 KB */
static u32 shown_sum, want_sum, build_sum;   /* the headlines' hash: in the shown picture, in the reply, in the one being drawn */
static int sum_gen = -1;                     /* the reply (board_gen) of want_sum */
static int shown_n, building = -1;           /* headlines in the shown picture; the next one to draw, -1 idle */
static float wid[BOARD_NEWS];                /* each headline's width in the picture (its text_w) */

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
static const u8 *al4(const u8 *base, const u8 *p) { return p + (4 - (p - base) % 4) % 4; }

/* One font block of a file: the fields of the font MQ_FONT. 0 when the file is short. */
static int font_block(const u8 *base, const u8 *end, const u8 *p, int nfonts, int count, atlas *a)
{
    for (int k = 0; k < nfonts; k++) {
        if (end - p < 16 + count) return 0;
        atlas f = {(int)rd16(p), (int)rd16(p + 2), (int)rd16(p + 4), (int)rd16(p + 6), (int)rd16(p + 8), (int)rd16(p + 12), 0, count,
                   0, p + 16, NULL};
        int ah = (int)rd16(p + 14);
        p = al4(base, p + 16 + count);
        long npx = (long)f.aw * ah;
        if (!npx || (npx & 1) || end - p < npx / 2) return 0;
        f.px = p;
        p = al4(base, p + npx / 2);
        if (k == MQ_FONT) {
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
    int n = (int)rd16(p + 4), first = (int)rd16(p + 6), count = (int)rd16(p + 8), cols = (int)rd16(p + 10);
    if (n <= MQ_FONT || !cols || count > 112) return 0;
    if (!font_block(fonts_bin, end, p + 12, n, count, &fa)) return 0;
    fa.cols = cols;
    fa.first = first;
    p = latin_bin;
    end = latin_bin + latin_bin_size;
    if (utf8_rc == 0 && latin_bin_size >= 12 && !memcmp(p, "PHL1", 4)) {
        int ln = (int)rd16(p + 4), lc = (int)rd16(p + 6), lcols = (int)rd16(p + 8);
        if (ln > MQ_FONT && lc >= 1 && lc <= LATIN_MAX && lcols && end - (p + 12) >= 2 * lc) {
            for (int i = 0; i < lc; i++) cps[i] = (u16)rd16(p + 12 + 2 * i);
            ncp = lc;
            if (font_block(latin_bin, end, al4(latin_bin, p + 12 + 2 * lc), ln, lc, &la)) {
                la.cols = lcols;
                have_latin = 1;
            }
        }
    }
    top0 = fa.asc - fa.base;
    int bot = top0 + fa.ch;
    if (have_latin) {
        if (la.asc - la.base < top0) top0 = la.asc - la.base;
        if (la.asc - la.base + la.ch > bot) bot = la.asc - la.base + la.ch;
    }
    band_h = bot - top0;
    return band_h > 0 && band_h <= 64;
}

/* text_utf8.c's map: a code point from U+0080 as its slot in latin.bin, 0x100 + an ASCII base letter, or -1 */
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

/* One cell into the band at pen x: the atlas's 4-bit alphas, mixed as two blended quads */
static void cell(const atlas *a, int slot, int px)
{
    int x0 = px - a->pad, y0 = a->asc - a->base - top0;
    int sx = slot % a->cols * a->cw, sy = slot / a->cols * a->ch;
    for (int y = 0; y < a->ch; y++) {
        u16 *dst = row_buf + (y0 + y) * MQ_W;
        long row = (long)(sy + y) * a->aw + sx;
        for (int x = 0; x < a->cw; x++) {
            int bx = x0 + x;
            if (bx < 0 || bx >= MQ_W) continue;
            long idx = row + x;
            unsigned v = (a->px[idx >> 1] >> ((idx & 1) ? 0 : 4)) & 15;
            if (!v) continue;
            unsigned o = dst[bx] >> 12;
            unsigned m = o ? 15 - ((15 - o) * (15 - v) + 7) / 15 : v;
            dst[bx] = (u16)(m << 12 | 0x0fff);
        }
    }
}

/* A headline into row_buf as ui.c's run() draws it from pen MQ_PAD (cut
 * at 319 bytes, as text()'s format buffer cuts it); its advance. */
static int raster(const char *title)
{
    char s[320];
    strncpy(s, title, sizeof s - 1);
    s[sizeof s - 1] = 0;
    int n = (int)strlen(s), px = MQ_PAD;
    for (int i = 0; i < band_h * MQ_W; i++) row_buf[i] = 0x0fff;
    for (int i = 0, len = 1; i < n && s[i]; i += len) {
        unsigned cp = have_latin ? utf8_at(s, n, i, &len) : (unsigned char)s[i];
        int m;
        if (!have_latin) len = 1, m = 0x100 + (int)cp;
        else m = !cp ? -1 : cp < 0x80 ? 0x100 + (int)cp : lmap(cp);
        if (m < 0) continue;
        const atlas *a = m >= 0x100 ? &fa : &la;
        int slot = m >= 0x100 ? (m - 0x100) - fa.first : m;
        if (m >= 0x100 && (slot < 0 || slot >= fa.count)) slot = '?' - fa.first;
        if (px + a->cw + a->pad >= MQ_W) break;      /* the band's end: the rest is cut */
        if (!(m >= 0x100 && m - 0x100 == ' ')) cell(a, slot, px);
        px += a->adv[slot];
    }
    return px - MQ_PAD;
}

static u32 titles_sum(void)
{
    u32 h = 2166136261u;
    for (int i = 0; board_have && i < board_cur.n_news; i++)
        for (const unsigned char *p = (const unsigned char *)board_cur.news[i].title;; p++) {
            h = (h ^ *p) * 16777619u;
            if (!*p) break;
        }
    return h ^ (u32)(board_have ? board_cur.n_news : 0);
}

int marquee_ready(void) { return pic_ok == 1 && building < 0 && shown_sum == want_sum && shown_n == (board_have ? board_cur.n_news : 0); }

void marquee_tick(void)
{
    if (!parsed) {
        parsed = parse() ? 1 : -1;
        if (have_latin) square = lmap(0x25a1) >= 0 && lmap(0x25a1) < ncp ? lmap(0x25a1) : -1;
    }
    if (parsed < 0) return;
    if (sum_gen != board_gen) {                         /* the hash once per reply, not each frame */
        sum_gen = board_gen;
        want_sum = titles_sum();
    }
    int n = board_have ? board_cur.n_news : 0;
    if (building < 0 && (shown_sum != want_sum || shown_n != n) && n > 0) {
        if (pic_ok < 0) {
            pic_ok = tex_pair_new(&pic, MQ_W, (u16)(band_h * BOARD_NEWS), GFX_TEX_A4R4G4B4, 1) == 0;
            frame_ev |= EV_TEX;
        }
        if (!pic_ok) return;
        building = 0;
        build_sum = want_sum;
    }
    if (building < 0) return;
    if (want_sum != build_sum || building >= n) {       /* a new reply while building: start again (0.5.1: the */
        building = -1;                                   /* test before compared two hashes of one frame) */
        return;
    }
    /* one headline a frame: after ui_flip, into the texture no frame draws */
    int w = raster(board_cur.news[building].title);
    wid[building] = (float)w;
    const gfx_tex *t = tex_pair_wtex(&pic);
    u8 *base = (u8 *)tex_pair_written(&pic) + (u32)(building * band_h) * t->pitch;
    int words = (MQ_PAD + w + 64 + 1) / 2;                  /* the ink and a margin, two texels a store */
    if (words > MQ_W / 2) words = MQ_W / 2;
    for (int y = 0; y < band_h; y++) {
        volatile u32 *dst = (volatile u32 *)(base + (u32)y * t->pitch);
        const u16 *src = row_buf + y * MQ_W;
        for (int x = 0; x < words; x++) {
            u32 v;
            memcpy(&v, src + 2 * x, 4);
            dst[x] = v;
        }
        for (int x = words; x < (int)t->pitch / 4 && x < words + 32; x++) dst[x] = 0x0fff0fffu;   /* past the ink: clear */
    }
    frame_ev |= EV_TEX;
    building++;
    if (building >= n) {
        tex_pair_swap(&pic);
        shown_sum = want_sum;
        shown_n = n;
        building = -1;
    }
}

void marquee_title(int i, float x, float y, u32 rgba)
{
    if (!marquee_ready() || i < 0 || i >= shown_n) {
        char t[320];
        if (board_have && i >= 0 && i < board_cur.n_news) {
            strncpy(t, board_cur.news[i].title, sizeof t - 1);
            t[sizeof t - 1] = 0;
            text(floorf(x + 0.5f), y, MQ_FONT, rgba, "%s", t);
        }
        return;
    }
    const gfx_tex *t = tex_pair_shown(&pic);
    float w = wid[i] + MQ_PAD + 32, x0 = x - MQ_PAD, yt = floorf(y + 0.5f) + (float)top0;
    if (w > MQ_W) w = MQ_W;
    float v0 = (float)(i * band_h) / (float)t->h, v1 = (float)((i + 1) * band_h) / (float)t->h;
    tex_sub(t, x0, yt, w, (float)band_h, 0, v0, w / MQ_W, v1, rgba);
}
