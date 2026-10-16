/* The waiting screen's fade (0.4.1): every colour that the draws below get
 * is times amb_ga. page.c sets amb_ga around the waiting screen's block, so
 * the block fades out, moves and fades in (no visible step); it is 1
 * everywhere else, and then the colours pass as they are. Function-like
 * macros over ps3gfx's calls, so a file includes this after its other
 * headers; (name)(...) is the real call. The background (rect) is not faded. */
#ifndef FADE_H
#define FADE_H

#include <ppu-types.h>
#include "stext.h"

extern float amb_ga;
static inline u32 ga_c(u32 c) { return amb_ga >= 1.0f ? c : alpha_times(c, amb_ga); }

#define text(x, y, f, c, ...) (text)(x, y, f, ga_c(c), __VA_ARGS__)
#define text_r(x, y, f, c, ...) (text_r)(x, y, f, ga_c(c), __VA_ARGS__)
#define text_c(x, y, f, c, ...) (text_c)(x, y, f, ga_c(c), __VA_ARGS__)
#define text_fit(x, y, f, c, w, ...) (text_fit)(x, y, f, ga_c(c), w, __VA_ARGS__)
#define text_wrap(x, y, f, c, w, n, s) (text_wrap)(x, y, f, ga_c(c), w, n, s)
#define digits(x, y, z, c, s) (digits)(x, y, z, ga_c(c), s)
#define round_rect(x, y, w, h, r, c) (round_rect)(x, y, w, h, r, ga_c(c))
#define round_frame(x, y, w, h, r, t, c) (round_frame)(x, y, w, h, r, t, ga_c(c))
#define aa_corners(x, y, w, h, r, c) (aa_corners)(x, y, w, h, r, ga_c(c))
#define aa_disc(x, y, r, c) (aa_disc)(x, y, r, ga_c(c))
#define aa_line(x0, y0, x1, y1, w, c) (aa_line)(x0, y0, x1, y1, w, ga_c(c))
#define icon_rot(i, x, y, a, s, c) (icon_rot)(i, x, y, a, s, ga_c(c))

#endif
