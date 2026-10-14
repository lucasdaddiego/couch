/* The sky's own objects (design 2026-10-13, sections 3.6, 3.7, 3.9, 3.11):
 * what the PPU and the RSX draw between the gradient and the clouds (the
 * stars, the moon's disc, the sun's glow and rays), a lightning bolt and
 * its flash over the clouds, the scrims behind the header and the bottom bar, and
 * the textures the pages share (the moon's sphere, a round glow, a tile's
 * halo). None of them needs the SPUs: each is the same in RSX-only mode.
 * Every motion takes its time from sky_time (the system clock since the
 * start: the preview's frame clock), never from a reply's landing time.
 * The main thread only. */
#ifndef SKY_H
#define SKY_H

#include <ppu-types.h>
#include <ps3gfx/gfx.h>

/* Seconds since Couch started (sysGetSystemTime): the clock of every motion. */
double sky_time(void);

/* Each frame, after fx_tick: the moon's sphere (16 rows a frame, a new
 * picture when the age moved by 0.25 day), the moon's place each minute,
 * the lightning's schedule (a storm code now: a bolt every 6 to 20 s). */
void sky_tick(void);

/* Between fx_draw_gradient and fx_draw_clouds, over the whole screen: the
 * stars (night), the moon's disc (while it is up), the sun's glow and its
 * rays (day, the rays with a cover under 60 %). */
void sky_behind(void);
/* After the clouds and the rain: a bolt (its 3 lines) and its flash (2
 * frames). */
void sky_front(void);
/* The scrims (design 1.1): dark at the screen's top and bottom, so the
 * header and the bottom bar read on a bright sky. */
void sky_scrims(void);

/* The moon's sphere at the real phase (160 x 160, lit from the side the
 * place sees: mirrored south of the equator), NULL before the first
 * picture. */
const gfx_tex *sky_moon_tex(void);
/* A round glow, white with the alpha falling from the centre (the sun's
 * glow, the hero's stars), and a tile's halo for glow9 (a rounded rect's
 * fall-off: clear inside, 0 at pad outside). NULL when RSX memory is short. */
const gfx_tex *sky_glow_tex(void);
const gfx_tex *sky_halo_tex(void);

/* 1 while a bolt shows: the hero's lightning layer flashes with it. */
int sky_bolt_on(void);

#endif
