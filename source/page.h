/* Couch's pages (page.c, since 0.2.0; the frame of 0.3.0's stage 10): the
 * Board, the News and the Info screen, L1 and R1 between them with a slide,
 * the dots in the header. The frame every page shares: the sky (the
 * effects), the header (the flip clock and the date on the left; the
 * page's name and its dots, the city and the internet's state on the
 * right), the page's body, the bottom bar (the status on the left, the
 * buttons on the right), the start animation. The ambient mode: after
 * AMBIENT_SEC with no button the aurora (the SPUs) with the clock, the date
 * and a line that change place each minute; in RSX-only mode 0.2.0's dimmed
 * screen that moves a few pixels and changes page every AMBIENT_STEP_SEC
 * (parse.h). Any button wakes it and does nothing else. */
#ifndef PAGE_H
#define PAGE_H

#include <ppu-types.h>

enum { PAGE_BOARD, PAGE_NEWS, PAGE_INFO, PAGE_N };
#define PAGE_CYCLE 2                 /* L1/R1 cycle the Board and the News; SELECT opens the Info page */
extern int page;                     /* the page on the TV (during a slide: the page that comes in) */
extern const char *const page_name[PAGE_N];

/* The layout every page shares (design 1.2): wide (SH >= 980) or narrow;
 * the content's top under the header; the bottom bar's top. */
int page_wide(void);
float page_top(void);
float page_bar_y(void);
#define G 24                         /* the gap between the columns (0.3.2: 16 before) */
#define RG 16                        /* the gap between rows of tiles (0.3.2: 12 before) */
#define TR 26                        /* a tile's corner radius (0.3.2: 20 before) */

/* The frame of the page on the TV (the frame loop's draw): the sky, the
 * header, the body (two during a slide), the bottom bar, the start
 * animation's fade; or the ambient mode. */
void page_draw(void);
/* The Board with msg in the bottom bar (an unattended start that does not run). */
void page_draw_msg(const char *msg, u32 color);

/* A page change by the pad (main.c): the slide from the page on the TV to
 * to (dir 1: the new page comes in from the right, -1 from the left), 280
 * ms, ease-out cubic; page_slide_end ends one at once (a button). */
void page_slide(int from, int to, int dir);
void page_slide_end(void);

/* The canvas moved by whole pixels while a part draws (a slide, the
 * start's rise): the viewport, as the visible area moves it, so the glass
 * follows its tile. At most 4 deep. page_dx: the sum of the x moves now
 * (the ticker's full-width band moves with its page). */
void shift_push(int dx, int dy);
void shift_pop(void);
extern int page_dx;

/* What the pages share: a glass tile (design 3.4: the blurred sky, the
 * tint by the sky's light, a 1 px edge and the bottom line; a solid panel in
 * RSX-only mode), the glass tint, a skeleton bar (before the first reply:
 * a grey bar with a light band that crosses the screen every 1.6 s), a
 * source's colour (the News pills, the ticker), and the bottom bar's two
 * parts. */
void glass_tile(float x, float y, float w, float h);
u32 glass_tint(void);
void skel_bar(float x, float y, float w, float h);
u32 source_colour(const char *source);
void bar_draw(float y, const char *left, u32 color, const char *note, const char *hints);

/* The ambient mode, from the frame loop. ambient_input after read_pad: any
 * pressed button resets the idle time, and when the mode is on, it ends the
 * mode, puts back the page of before and clears pressed (1: the press woke
 * the screen, so it does nothing else). ambient_tick each frame: the mode
 * starts after AMBIENT_SEC of no press (AMBIENT_READ_SEC while an article
 * is open: reader.h), and in RSX-only mode each step sets the page. */
extern int ambient;                  /* 1 while the mode is on */
int ambient_input(void);
void ambient_tick(void);
int ambient_aurora(void);            /* 1: the mode shows the SPUs' picture (the sky or the aurora), not a dimmed page */
int ambient_day(void);               /* 0.3.2: 1 while the sun is up (the sky), 0 at night (the aurora) */
int board_wx_icon(float cx, float cy, float box);   /* board_page.c: the weather icon of now; 0 when none */
void board_ambient_wx(float x, float y, float w, float h, u32 c1, u32 c2);   /* board_page.c: the waiting screen's weather card */

/* The bodies and the bars of the pages (board_page.c, reader.c, info.c):
 * the body between the header and the bottom bar, the bar at y. */
void board_body(void);
void board_bar(float y, const char *msg, u32 msg_color);
void news_body(void);
void news_bar(float y);
void info_body(void);
void info_bar(float y);

#endif
