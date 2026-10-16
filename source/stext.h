/* Text and glass that scroll by fractions of a pixel (0.3.1, stage 11: the
 * news reader's list and article). ps3gfx draws text pixel exact (nearest
 * filtering at 1080p, each glyph on a whole pixel): text that scrolls
 * through an eased motion steps by whole pixels at its slow end. Couch
 * keeps its own copy of four fonts' atlases (F_SMALL, F_BODY, F_MED and
 * F_TITLE of fonts.bin and latin.bin, as marquee.c reads them), each glyph
 * in a cell with a clear texel all around, in a texture drawn with
 * bilinear filtering: a glyph lands at any fractional y, and at a whole
 * pixel its texels land 1:1 (the pixels of text()). The quads are ps3gfx's
 * run() quads, glyph for glyph.
 *
 * ps3gfx has no scissor, so the reader draws its scrolling part inside a
 * band (stext_band): every glyph quad and every glass polygon is cut at
 * the band's top and bottom, and fades over the band's fade lengths
 * (per-vertex alpha, so the fade is smooth and nothing pops in at the
 * band's edge). What the kit draws (a pill, a ring, an icon) takes
 * stext_alpha: 0 when it crosses the band's edge.
 *
 * The atlases cost 3.4 MB of RSX memory; stext_tick builds them after
 * ui_flip, a few rows of cells a frame (about 20 frames), on the first
 * call. Until then, and when the memory is short, stext draws text() at
 * the nearest pixel, a line only when it is inside the band. The main
 * thread only. */
#ifndef STEXT_H
#define STEXT_H

#include <ppu-types.h>

/* After ui_flip, each frame (the frame loop): the next rows of the
 * atlases until they are built. 1 when they are ready. */
int stext_tick(void);
int stext_ready(void);

/* The band: canvas y top to bot, faded over fade_top px under its top and
 * fade_bot px over its bottom (0: a hard cut). stext_band_off: no band. */
void stext_band(float top, float bot, float fade_top, float fade_bot);
void stext_band_off(void);
/* The alpha factor (0 to 1) of an element from y0 to y1: 0 when it is not
 * wholly inside the band, else the fade at its middle. A text inside such
 * an element (a pill's name) takes the band's fade itself: it is drawn
 * only when the element is (k > 0), at its own colour. */
float stext_alpha(float y0, float y1);
/* rgba with its alpha times k. */
u32 alpha_times(u32 rgba, float k);

/* n bytes of s (UTF-8 when text_utf8 is on) at pen x (rounded to a whole
 * pixel) and line top y (any fraction), in font (F_SMALL, F_BODY, F_MED,
 * F_TITLE; another font draws as text() does), cut and faded by the band;
 * the pen's end. The width is ps3gfx's text_w. */
float stext(float x, float y, int font, u32 rgba, const char *s, int n);
/* The same cut to maxw with "..." at the end when it is wider (text_fit's rule). */
float stext_fit(float x, float y, int font, u32 rgba, float maxw, const char *s);
/* The same right-aligned at xr. */
float stext_r(float xr, float y, int font, u32 rgba, const char *s);
/* text_w of n bytes. */
float stext_w(int font, const char *s, int n);

/* A glass tile (page.h's glass_tile: the frosted sky, the tint, the 1 px
 * edge, the bottom line) at any fractional y, cut and faded by the band;
 * glass_tile itself when it is wholly inside the band and its fades. tint
 * is the glass tint (glass_tint(), or a lighter one for the selection). */
void mglass(float x, float y, float w, float h, float r, u32 tint);

#endif
