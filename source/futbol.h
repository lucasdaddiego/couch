/* The Futbol page (0.5.0): the games of today or tomorrow from the private
 * Worker's /futbol?d=0 or d=1 (parse.h's futbol_data), grouped by league:
 * each game's time or minute, the teams, the score and the TV channels, a
 * live game marked. UP and DOWN scroll the list (L2 and R2 by a page),
 * LEFT and RIGHT pick today or tomorrow, SQUARE asks again.
 *
 * The list comes when the page opens (unless that day came in the last
 * 60 s), every 10 min while the page shows, every 60 s while a game of the
 * day is live, and 60 s after a failure; not while the waiting screen
 * shows. A day that changed at midnight is asked again. The Board thread
 * makes the requests (board.h) and parses the replies; futbol_take keeps
 * them. The main thread only. */
#ifndef FUTBOL_H
#define FUTBOL_H

#include "board.h"

/* Each frame, before the draw: the scroll's motion and the schedule. */
void futbol_tick(void);
/* The page's pad (main.c): 1 when the page used the press. */
int futbol_input(void);
/* SQUARE on the page: the shown day again (at most once in 10 s). */
void futbol_refresh(void);
/* A futbol reply (board_take). */
void futbol_take(const board_reply *b);

#endif
