/* The news reader (0.3.1, stage 11; Lucas, 2026-10-14: "add more news and
 * also, select a new and enter it? and maybe scroll down for more"): the
 * News page as a list of up to 30 headlines from the Worker's /news, a
 * selection that UP, DOWN, LEFT and RIGHT move over the cards (stage 10's
 * look: the featured card for the top item, then rows of two glass cards),
 * the page scrolling by fractions of a pixel (eased) to keep it in view,
 * with a scroll bar; CROSS opens the article (/article?id=): the title,
 * the source, the category, the date and the paragraphs in a column, UP
 * and DOWN scroll it (L2, R2, LEFT and RIGHT by a page), a QR code of the
 * link in the corner for a phone; CIRCLE goes back to the list with its
 * selection.
 *
 * The list comes when the News page opens (unless one came in the last
 * 60 s) and every 5 min while the page shows (60 s after a failure), not
 * while an article is open or the ambient mode is on; until the first one,
 * the Board's 8 headlines fill the page (they cannot open: they have no
 * id). The Board thread makes the requests (board.h), one at a time, and
 * parses the replies; reader_take keeps them. The main thread only. */
#ifndef READER_H
#define READER_H

#include "board.h"

/* Each frame, before the draw: the motion of the scrolls, and the news
 * list's schedule. */
void reader_tick(void);
/* The News page's pad (main.c, after the page changes): circle 1 for
 * CIRCLE (ui_leave's 0). 1 when the reader used the press. */
int reader_input(int circle);
/* 1 while an article is open on the News page (loading, shown or its
 * error): the ambient mode waits AMBIENT_READ_SEC then (page.c). */
int reader_reading(void);
/* SQUARE on the News page: the list again (at most once in 10 s). */
void reader_refresh(void);
/* A news or article reply (board_take). */
void reader_take(const board_reply *b);

#endif
