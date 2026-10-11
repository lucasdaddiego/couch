/* The Board: one thread, "board", made as Cell Bench's temps sampler,
 * fetches the Worker's reply (http.h) at start, every 5 min, 60 s after a
 * failure and at SQUARE (at most once in 10 s). The handoff is one record
 * and a ready word (Cell Bench's run.c). The main thread parses the reply
 * (parse.h), keeps the last good one, logs each fetch and draws the page.
 * The thread never journals and never writes a file. */
#ifndef BOARD_H
#define BOARD_H

#include <ppu-types.h>
#include "parse.h"

#define BOARD_DEFAULT_URL "http://couch.daddiego.com.ar/board"

/* The URL: board_url.txt in the app folder (FTP only, for test rounds),
 * else the default. Before the thread starts. */
void board_init(void);
extern char board_url[200];          /* the URL in use */
extern int board_url_file;           /* 1: from board_url.txt */
extern char board_url_note[160];     /* why board_url.txt was not used, or "" */

int board_start(void);               /* the thread: 0, or sysThreadCreate's rc */
int board_running(void);
/* At exit: board_on 0, the open socket shut down (http_wake), then the
 * join, with no time limit. 0 when the thread ended or never ran, -1 when
 * the join failed and the thread had not left (no net_end then). */
int board_stop(void);
void board_refresh(void);            /* SQUARE: a fetch now, or 10 s after the last one began */
void board_take(void);               /* each frame, in every screen: a new reply when the ready word is set */

/* The last good reply and the fetches, for the clock, the Info screen and the log. */
extern board_data board_cur;
extern int board_have;
extern int board_fetches, board_fails, board_last_ok;
extern char board_last_words[200];   /* the last fetch's result in words ("ok", "DNS failed for ...") */
extern long board_last_utc;          /* the console's clock when the main thread took it */
extern s64 board_last_us;            /* how long it took */
extern int board_last_http, board_last_len;
extern int board_has_skew;
extern long board_skew;              /* the console's clock minus the Worker's now, at the reply */
int board_offset(int *offset);       /* 1 and the last good reply's offset, or 0 */
long board_age(void);                /* seconds since the last good reply came */
/* The schedule in words: "Fetching", "Refresh in 6 s", "Next update in 4 min". */
void board_next_words(char *buf, int n);

/* The page: the clock, the dollar card, the weather card, the status line,
 * the footer; msg replaces the status line when it is not NULL. */
void board_draw(const char *msg, u32 msg_color);

#endif
