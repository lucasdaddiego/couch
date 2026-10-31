/* The Economía page (0.6.0): the indicators of the Argentine economy from
 * the Worker's /economia (parse.h's econ_data), by section: Precios,
 * Dólar, Tasas y BCRA, and Actividad with the inflation chart. Each row is
 * the value in the Argentine style, the move since the previous reading
 * (points for a rate, a percentage for a level) and the reading's date.
 * SQUARE asks again.
 *
 * The reply comes when the page opens (unless it came in the last 10 min),
 * every 30 min while the page shows, and 60 s after a failure; not while
 * the waiting screen shows. The Board thread makes the request (board.h)
 * and parses the reply; econ_take keeps it. The main thread only. */
#ifndef ECON_H
#define ECON_H

#include "board.h"

void econ_tick(void);                /* each frame, before the draw: the schedule */
void econ_refresh(void);             /* SQUARE on the page: the reply again (at most once in 10 s) */
void econ_take(const board_reply *b);   /* an economia reply (board_take) */

#endif
