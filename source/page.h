/* Couch's pages (page.c, since 0.2.0): the Board, the News and the Info
 * screen, L1 and R1 between them, the dots in the header. The header every
 * page shares: the clock and the date on the left; the page's name and its
 * dots, the city and the internet's state on the right. The ambient mode:
 * after AMBIENT_SEC with no button the screen dims, the picture moves a few
 * pixels and the page changes between the Board and the News every
 * AMBIENT_STEP_SEC (parse.h); any button wakes it and does nothing else. */
#ifndef PAGE_H
#define PAGE_H

#include <ppu-types.h>

enum { PAGE_BOARD, PAGE_NEWS, PAGE_INFO, PAGE_N };
extern int page;                     /* the page on the TV */
extern const char *const page_name[PAGE_N];

/* The content's top: under the header (the cards start here). */
#define CONTENT_Y 140

/* The frame of a page: page_begin applies the ambient shift, starts the
 * frame (the background) and draws the header, and returns CONTENT_Y;
 * page_end draws the dim layer when the mode is on, shows the frame and
 * takes the shift back. */
float page_begin(void);
void page_end(void);

/* The ambient mode, from the frame loop. ambient_input after read_pad: any
 * pressed button resets the idle time, and when the mode is on, it ends the
 * mode, puts back the page of before and clears pressed (1: the press woke
 * the screen, so it does nothing else). ambient_tick each frame: the mode
 * starts after AMBIENT_SEC of no press, and each step sets the page. */
extern int ambient;                  /* 1 while the screen is dimmed */
int ambient_input(void);
void ambient_tick(void);

/* The News page: the headlines with their source and age. */
void news_draw(void);

#endif
