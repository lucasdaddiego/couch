/* The ticker's text as a picture (marquee.h). The atlases are
 * fontarch.h's (ps3gfx's archive members fonts.bin and latin.bin), and the
 * glyph of a byte or a code point, its cell and its advance are ui.c's
 * (fa_glyph; run_utf8). Two cells that overlap mix as two quads of the
 * RSX's blend do: 1 - (1 - a) (1 - b).
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
#include "fontarch.h"
#include "marquee.h"

#define MQ_W 4096                            /* the picture's width: a headline at most */
#define MQ_PAD 16                            /* the pen's start in a band: room for the first cell's pad */
#define MQ_FONT F_BODY
#define MQ_CLEAR 96                          /* the rows cleared past a headline's ink (raster) */

static fa_font fa, la;                       /* F_BODY of fonts.bin and of latin.bin */
static int have_latin, parsed;
static int top0, band_h;                     /* the band's first row from the line top, its rows */
static tex_pair pic;
static int pic_ok = -1;
static u16 row_buf[64 * MQ_W];               /* one band's rows (band_h <= 64): 512 KB */
static u32 shown_sum, want_sum, build_sum;   /* the headlines' hash: in the shown picture, in the reply, in the one being drawn */
static int sum_gen = -1;                     /* the reply (board_gen) of want_sum */
static int shown_n, building = -1;           /* headlines in the shown picture; the next one to draw, -1 idle */
static float wid[BOARD_NEWS];                /* each headline's width in the picture (its text_w) */

/* F_BODY of both files (latin.bin's when text_utf8 is on) and the band's rows. */
static int parse(void)
{
    if (fa_parse() <= 0 || !fa_get(0, MQ_FONT, &fa)) return 0;
    have_latin = fa_latin && fa_get(1, MQ_FONT, &la);
    top0 = fa.asc - fa.base;
    int bot = top0 + fa.ch;
    if (have_latin) {
        if (la.asc - la.base < top0) top0 = la.asc - la.base;
        if (la.asc - la.base + la.ch > bot) bot = la.asc - la.base + la.ch;
    }
    band_h = bot - top0;
    return band_h > 0 && band_h <= 64;
}

/* One cell into the band at pen x: the atlas's 4-bit alphas, mixed as two blended quads */
static void cell(const fa_font *a, int slot, int px)
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
 * at 319 bytes, as text()'s format buffer cuts it); its advance. The rows
 * are cleared only up to MQ_PAD + its text_w + MQ_CLEAR texels (0.5.1's
 * review: all 4,096 of each row for each headline). The readers of those
 * texels: cell's blend, up to the last cell's right edge (F_BODY's cell
 * reaches 24 px past the pen at most), and marquee_tick's copy, up to
 * MQ_PAD + the advance + 65. */
static int raster(const char *title)
{
    char s[320];
    strncpy(s, title, sizeof s - 1);
    s[sizeof s - 1] = 0;
    int n = (int)strlen(s), px = MQ_PAD, clear = MQ_PAD + text_w(MQ_FONT, s) + MQ_CLEAR;
    if (clear > MQ_W) clear = MQ_W;
    for (int y = 0; y < band_h; y++)
        for (int i = 0; i < clear; i++) row_buf[y * MQ_W + i] = 0x0fff;
    for (int i = 0, len = 1; i < n && s[i]; i += len) {
        int slot = 0, which = fa_glyph(s, n, i, have_latin, &len, &slot);
        if (which < 0) continue;
        const fa_font *a = which ? &la : &fa;
        if (px + a->cw + a->pad >= MQ_W) break;      /* the band's end: the rest is cut */
        if (!(which == 0 && s[i] == ' ')) cell(a, slot, px);
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
    if (!parsed) parsed = parse() ? 1 : -1;
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
