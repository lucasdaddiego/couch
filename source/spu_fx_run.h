/* The effects (design 2026-10-13, sections 3 and 6): ps3gfx's SPU program
 * spu_fx (<ps3gfx/spu_fx.h>) on a group of six SPU threads, and the RSX-only
 * fallback of each effect. Threads 0 to 4 make the pictures, a band each:
 * the clouds and the frosted glass (FX_SKY) or the aurora (FX_AURORA), 30 a
 * second into two textures that take turns; thread 5 makes the rain or the
 * snow (FX_RAIN) every frame, into the frame's vertex area. The name is not
 * fx.c: an app object with an archive member's base name hides the member.
 *
 * Every LV2 SPU call is journaled by the main thread (jgo before, jend
 * after): fx_init, fx_import, fx_group (the create and the six threads; a
 * failed thread destroys the group inside the entry), fx_start, fx_first
 * (open from the start until the first sky picture is drawn and its check
 * row read back equal, and 60 frames of rain), fx_aurora (the same for the
 * first aurora picture, once per journal), fx_terminate, fx_join,
 * fx_exit_status, fx_destroy, fx_close. No prompt (Lucas's choice): an
 * entry that froze before, or a folder that cannot be written, means no SPU
 * call at all; SQUARE twice on the Info page clears the journal, and the
 * next start runs the SPUs again.
 *
 * RSX-only mode (design 6.5) after a frozen entry, a failed call, no
 * writable folder, a late report (rain 50 ms, a picture 2 s), a check that
 * did not match, or the last step of the frame budget: the gradient, four
 * cloud sprites, the PPU's rain (300 drops), solid panels for the glass, no
 * aurora. The Board stays complete.
 *
 * The main thread only. */
#ifndef SPU_FX_RUN_H
#define SPU_FX_RUN_H

#include <ppu-types.h>
#include <ps3gfx/gfx.h>

/* The effects' state, for the Info page and the pages' choices. */
enum {
    FXS_NONE,                        /* fx_start did not run yet */
    FXS_SPU,                         /* the SPUs make the pictures and the rain */
    FXS_RSX                          /* RSX only: fx_why has the reason */
};
extern int fx_state;
extern char fx_why[120];             /* why RSX only, "" while the SPUs run */

/* The scene the effects show, from the last good reply and the clock
 * (fx_tick recomputes it each minute and at each new reply). Colours are
 * 0xRRGGBB. Stage 9's pages read it for the tints. */
typedef struct {
    double elev, azim;               /* the sun, degrees (no geo: -20, 0) */
    unsigned top, horizon;           /* the gradient (sky_colors) */
    unsigned sky[4];                 /* the gradient at 0, 55, 80 and 100 % of the height */
    float cover;                     /* 0 to 1 */
    float wind_x, wind_y;            /* the clouds' drift, picture pixels a second (1 picture pixel = 4 screen pixels) */
    float rain_wind;                 /* the rain's slant, px/s (fx_rain_wind) */
    int precip, kind;                /* drops or flakes (wx_precip), FX_DROPS or FX_FLAKES; 0: dry */
    unsigned lit, shade, edge;       /* the clouds' colours */
    float edge_amt, sun_x, sun_y, sun_gain;   /* sun_x, sun_y in picture pixels */
    int wet;                         /* rain or a storm now */
} fx_scene;
extern fx_scene fx_sc;

/* The start (design 6.3), at each opening of the Board before the network
 * start: the textures, then the SPU calls behind run_job("Starting the
 * effects"), the six start reports (2 s) and their known-answer test. It
 * runs once, and again only after an entry that the journal held was
 * cleared (SQUARE twice on the Info page, as net_start). With no writable
 * folder, a pending quit or a frozen entry: RSX only, no SPU call.
 * couch.txt gets a line per step. */
void fx_start(void);

/* The exit (design 6.4), in leave() after board_stop and before net_end:
 * the exit flag and a mailbox word to each thread whose last report came,
 * up to 1 s for the six exit reports, then the join and the exit statuses;
 * else the terminate, then the join (a failed terminate whose job counts
 * still move: no join, no destroy, "stuck" in couch.txt). Then the destroy
 * and the image close. The jobs' busy times go to couch.txt. */
void fx_stop(void);

/* Each frame at the top of the loop, after the last ui_flip and before
 * page_begin: the frame budget, the scene, and the pictures. A picture's
 * five reports swap its textures in (the frame after shows it), then the
 * next picture is sent; mode is what the frame shows: */
enum { FXM_SKY, FXM_AURORA };
void fx_tick(int mode);

/* The draws, between begin_frame and ui_flip, over the whole screen
 * (screen_origin): the gradient (three rect_v bands), then whatever sits
 * between the sky and the clouds (stage 9: the moon, the stars, the sun's
 * glow), then the clouds (the SPU's picture, or four sprites); the rain or
 * the snow after the background layers (thread 5's block, or the PPU's);
 * the aurora in place of the sky (0 when none: RSX only, or no picture
 * yet). fx_draw_sky is the gradient and the clouds. */
void fx_draw_gradient(void);
void fx_draw_clouds(void);
void fx_draw_sky(void);
void fx_draw_rain(float alpha);      /* alpha 0 to 1: every particle's alpha times this */
int fx_draw_aurora(void);

/* The frosted glass of a tile (design 3.4): the blurred sky behind the rect
 * x, y, w, h (canvas units) in a rounded rect of radius r, then tint over it
 * (0xRRGGBBAA); in RSX-only mode, or before the first picture, the tint
 * alone with its alpha 0x30 higher (a solid panel). fx_glass_tex: the shown
 * glass texture, NULL when there is none. */
void fx_glass(float x, float y, float w, float h, float r, u32 tint);
const gfx_tex *fx_glass_tex(void);

/* Lightning (design 3.11): the next picture's clouds lit by level (0 to 1). */
void fx_flash(float level);

/* The frame budget's step (design 3.13), 0 to 5: 0 at 30 pictures a second;
 * 1 at 20; 2 at 15; 3 the rain at half its drops; 4 no rays, no twinkle,
 * no hero motion (the pages read fx_reduced); 5 RSX only. */
extern int fx_step;
int fx_reduced(void);                /* 1 from step 4 */

/* The Info page's line, and the fetch line's suffix in couch.txt (the last
 * job serials and the late frames: a freeze hours in leaves a trace). */
void fx_words(char *buf, int n);
void fx_log_words(char *buf, int n);

#endif
