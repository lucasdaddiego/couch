/* The ticker's text as a picture (marquee.c): Lucas, 2026-10-14, "the text
 * marquee is fast", then "an slower marquee". ps3gfx draws text pixel exact
 * (nearest filtering at 1080p, each glyph on a whole pixel), so text that
 * moves at a speed that is not a whole number of pixels a frame judders.
 * Couch draws the headlines once into a texture of its own, from the same
 * atlases (fonts.bin, and latin.bin when text_utf8 is on), glyph by glyph as
 * ui.c's run() places them, and draws that texture with bilinear filtering
 * at any fractional x: at a whole pixel its texels land 1:1 (the text of
 * text()), between two pixels each column mixes its two neighbours, so the
 * ticker moves by fractions of a pixel at any speed.
 *
 * Two textures take turns (fx.h's tex_pair): when the reply's headlines
 * change, marquee_tick draws one headline a frame into the texture no frame
 * shows (after ui_flip), and swaps when all are in. Until then the ticker
 * draws the new headlines with text() on whole pixels. The main thread
 * only. */
#ifndef MARQUEE_H
#define MARQUEE_H

#include <ppu-types.h>

/* Each frame after ui_flip (the frame loop, with fx_tick): one headline of
 * the next picture, or nothing when the shown one has the reply's
 * headlines. */
void marquee_tick(void);

/* Headline i of the reply's (board_cur) with its pen at x (any fraction)
 * and its line top at y (a whole pixel), in F_BODY: from the picture when
 * it holds the reply's headlines, else text() at the nearest pixel. */
void marquee_title(int i, float x, float y, u32 rgba);

/* 1 when the shown picture holds the reply's headlines. */
int marquee_ready(void);

#endif
