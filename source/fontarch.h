/* ps3gfx's font atlases as Couch reads them for its own pictures of text
 * (marquee.c's ticker, stext.c's scrolls; since 0.5.1 one reader for both:
 * the review found two copies). The atlases are ps3gfx's archive members
 * fonts.bin and latin.bin (the symbols bin2s gives them), in the block
 * layout of art/make_font.py that ui.c's fonts_init and text_utf8.c read: a
 * big-endian header, then per font the cell, the base line, the pad, the
 * ascent, the line, the atlas size, the advances and the 4-bit alphas (the
 * left pixel in the high nibble). The glyph of a byte or a code point is
 * ui.c's (glyph, glyph_utf8) and text_utf8.c's (map): an ASCII character
 * from fonts.bin ('?' for a byte with no slot), a code point from U+0080
 * from latin.bin, a letter of Latin Extended-A as its base letter, a
 * combining mark or a zero-width code point nothing, any other the white
 * square. The main thread only, after text_utf8 (utf8_rc). */
#ifndef FONTARCH_H
#define FONTARCH_H

#include <ppu-types.h>

#define FA_LATIN_MAX 128             /* latin.bin's code points, at most */

/* One font of a file: the cell, the base line, the pad, the ascent, the
 * atlas's width in pixels, the file's columns and glyphs, the advances,
 * the 4-bit alphas. */
typedef struct {
    int cw, ch, base, pad, asc, aw, cols, count;
    const u8 *adv, *px;
} fa_font;

/* The two headers, read once (the first call reads them, the next ones
 * give its result): 1 when fonts.bin's is whole (112 glyphs at most, its
 * columns not 0). fa_latin: latin.bin's is whole too and text_utf8 is on
 * (utf8_rc 0). */
int fa_parse(void);
extern int fa_latin;

/* Font want (F_SMALL ...) of fonts.bin (latin 0) or latin.bin (latin 1)
 * into *f: 1, or 0 when the file has no such font or is short. */
int fa_get(int latin, int want, fa_font *f);

/* The glyph at s[i] (n bytes), with latin.bin (latin 1: s is UTF-8) or
 * without (bytes): its file (0 fonts.bin, 1 latin.bin; -1 for none: a
 * combining mark, a zero-width code point, a cut sequence), its slot and
 * its length in bytes. */
int fa_glyph(const char *s, int n, int i, int latin, int *len, int *slot);

#endif
