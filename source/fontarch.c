/* The font atlases' reader (fontarch.h): the copies that marquee.c and
 * stext.c had, with the font as an argument, the headers read once. */
#include <string.h>
#include <ps3gfx/ui.h>
#include "couch.h"
#include "fontarch.h"

extern const u8 fonts_bin[];                 /* ps3gfx's archive members (bin2s) */
extern const u32 fonts_bin_size;
extern const u8 latin_bin[];
extern const u32 latin_bin_size;

int fa_latin;
static int parsed;                           /* 0 not yet, 1 fonts.bin whole, -1 not */
static int first, cols, count, nfonts;       /* fonts.bin's header */
static int lcols, lcount, lnfonts;           /* latin.bin's */
static const u8 *blocks, *lblocks;           /* each file's first font block */
static u16 cps[FA_LATIN_MAX];                /* latin.bin's code points, in order */
static int square = -1;                      /* the white square's slot in latin.bin, -1 none */

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
static const u8 *al4(const u8 *base, const u8 *p) { return p + (4 - (p - base) % 4) % 4; }

/* The font blocks of a file from p: font want's fields into *f. 0 when the file is short. */
static int font_block(const u8 *base, const u8 *end, const u8 *p, int n, int glyphs, int fcols, int want, fa_font *f)
{
    for (int k = 0; k < n; k++) {
        if (end - p < 16 + glyphs) return 0;
        fa_font g = {(int)rd16(p), (int)rd16(p + 2), (int)rd16(p + 4), (int)rd16(p + 6), (int)rd16(p + 8), (int)rd16(p + 12), fcols, glyphs,
                     p + 16, NULL};
        int ah = (int)rd16(p + 14);
        p = al4(base, p + 16 + glyphs);
        long npx = (long)g.aw * ah;
        if (!npx || (npx & 1) || end - p < npx / 2) return 0;
        g.px = p;
        p = al4(base, p + npx / 2);
        if (k == want) {
            *f = g;
            return 1;
        }
    }
    return 0;
}

int fa_get(int latin, int want, fa_font *f)
{
    if (fa_parse() <= 0 || (latin && !fa_latin)) return 0;
    if (latin) return font_block(latin_bin, latin_bin + latin_bin_size, lblocks, lnfonts, lcount, lcols, want, f);
    return font_block(fonts_bin, fonts_bin + fonts_bin_size, blocks, nfonts, count, cols, want, f);
}

/* text_utf8.c's map: a code point from U+0080 as its slot in latin.bin, 0x100 + an ASCII base letter, or -1 */
static const char base_a[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
                             "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static int lmap(unsigned cp)
{
    int lo = 0, hi = lcount - 1;
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

int fa_parse(void)
{
    if (parsed) return parsed;
    parsed = -1;
    const u8 *p = fonts_bin, *end;
    if (fonts_bin_size < 12 || memcmp(p, "PHF1", 4)) return parsed;
    nfonts = (int)rd16(p + 4), first = (int)rd16(p + 6), count = (int)rd16(p + 8), cols = (int)rd16(p + 10);
    if (nfonts < 1 || !cols || count > 112) return parsed;
    blocks = p + 12;
    parsed = 1;
    p = latin_bin;
    end = latin_bin + latin_bin_size;
    if (utf8_rc == 0 && latin_bin_size >= 12 && !memcmp(p, "PHL1", 4)) {
        int ln = (int)rd16(p + 4), lc = (int)rd16(p + 6), lk = (int)rd16(p + 8);
        if (ln >= 1 && lc >= 1 && lc <= FA_LATIN_MAX && lk && end - (p + 12) >= 2 * lc) {
            for (int i = 0; i < lc; i++) cps[i] = (u16)rd16(p + 12 + 2 * i);
            lnfonts = ln, lcount = lc, lcols = lk;
            lblocks = al4(latin_bin, p + 12 + 2 * lc);
            fa_latin = 1;
            int sq = lmap(0x25a1);
            square = sq >= 0 && sq < lcount ? sq : -1;
        }
    }
    return parsed;
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

int fa_glyph(const char *s, int n, int i, int latin, int *len, int *slot)
{
    int m;
    if (latin) {
        unsigned cp = utf8_at(s, n, i, len);
        m = !cp ? -1 : cp < 0x80 ? 0x100 + (int)cp : lmap(cp);
    } else {
        *len = 1;
        m = 0x100 + (unsigned char)s[i];     /* bytes: ui.c's glyph() ('?' for a byte with no slot) */
    }
    if (m < 0) return -1;
    if (m >= 0x100) {
        int k = (m - 0x100) - first;
        *slot = k < 0 || k >= count ? '?' - first : k;
        return 0;
    }
    *slot = m;
    return 1;
}
