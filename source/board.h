/* The Board: one thread, "board", made as Cell Bench's temps sampler,
 * fetches the Worker's reply (http.h) at start, every 5 min, 60 s after a
 * failure and at SQUARE (at most once in 10 s). The handoff is one record
 * and a ready word (Cell Bench's run.c). The main thread parses the reply
 * (parse.h), keeps the last good one, logs each fetch and draws the page.
 *
 * Since 0.3.1 (stage 11, the reader) the same thread also makes the
 * reader's requests, one at a time: GET /news and GET /article?id=<id>
 * beside the Board's path. An article goes first (the viewer waits for it),
 * then the Board when it is due, then the news list. The thread parses
 * those two replies itself (pure code, parse.h) and makes the article's QR
 * code, so the frame only copies them (reader.c takes them). It also
 * writes couch.txt's queued lines (couch.h's couch_log_drain) between its
 * requests. It never journals. Since 0.5.0 also GET /futbol?d=0 or d=1
 * (the Futbol page, futbol.h), after an article and, since 0.5.0's review,
 * before the Board and the news list (the page waits for it). */
#ifndef BOARD_H
#define BOARD_H

#include <ppu-types.h>
#include "parse.h"
#include "http.h"
#include "qrcodegen.h"

/* The URL: one http:// line in board_url.txt in the app folder. There is
 * no default (since 0.4.0): without the file, or with a bad line, board_url
 * is "", the thread never starts and the Board shows the setup steps.
 * Before the thread starts. */
void board_init(void);
extern char board_url[200];          /* the URL in use, "" when none */
extern char board_url_note[160];     /* why board_url.txt was not used, or "" */

/* The thread's requests. board_ask_news and board_ask_article (the
 * reader, the main thread) ask for one at the thread's next free moment;
 * a new article replaces one that has not started. */
enum { REQ_BOARD, REQ_NEWS, REQ_ARTICLE, REQ_FUTBOL, REQ_ECON, REQ_IMAGE };
/* The reader's photo (0.6.0): the open article's picture from /image?id=,
 * IMG_MAX bytes at most, decoded on the thread into PHOTO_W x PHOTO_H
 * texels at most (ps3gfx's image.h, the aspect kept). */
#define IMG_MAX 1572864
#define PHOTO_W 960
#define PHOTO_H 540
void board_ask_news(void);
void board_ask_article(const char *id);
void board_ask_futbol(int day);      /* 0 today, 1 tomorrow; a new day replaces one that has not started */
void board_ask_econ(void);           /* the Economía page's reply (0.6.0) */
void board_ask_image(const char *id);   /* the article's photo (0.6.0): as board_ask_article, a new id replaces one not started */

/* A news or article reply as board_take hands it to reader_take (a futbol
 * reply to futbol_take): the request, the GET's result, the parse (the
 * thread's: BP_* of parse.h, -1 for no 200 answer), the result in words,
 * the QR code of the article's link (qrcodegen's buffer, qr_ok 0 when
 * none). Valid during the call. */
#define QR_MAX_VERSION 25            /* PS3 Health's: a link of 1019 bytes fits at ECC low */
typedef struct {
    int kind;                        /* REQ_NEWS, REQ_ARTICLE or REQ_FUTBOL */
    char id[9];                      /* the id asked for */
    int day;                         /* the futbol day asked for */
    const http_result *r;
    s64 at, us;                      /* the reply's system time, the GET's time */
    int prc;
    const news_data *news;
    const article_data *art;
    const futbol_data *fut;
    const econ_data *econ;           /* an economia reply (0.6.0) */
    int img_ok, img_w, img_h, img_rc;   /* an image reply (0.6.0): decoded, its size, the decoder's rc (-3: the body was cut); img the texels, row y at img + y * img_w */
    const u32 *img;
    int qr_ok;
    const unsigned char *qr;
    char words[160];                 /* "ok", "HTTP 404", "no answer in 10 s", ... */
} board_reply;

/* The last whole reply that the thread saved (board_last.txt, same URL, at
 * most 24 h old) as board_cur, at start before the thread: the first frame
 * has the Board's data. */
void board_load_last(void);
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
/* The good reply before it (board_prev_have 0: none), and the count of good
 * replies: the Board page's changed tiles (the glow, the counting). */
extern board_data board_prev;
extern int board_prev_have;
extern int board_gen;
extern int board_fetches, board_fails, board_last_ok;
extern char board_last_words[200];   /* the last fetch's result in words ("ok", "DNS failed for ...") */
extern long board_last_utc;          /* the console's clock when the main thread took it */
extern s64 board_last_us;            /* how long it took */
extern int board_last_http, board_last_len;
extern int board_has_skew;
extern long board_skew;              /* the console's clock minus the Worker's now, at the reply */
int board_offset(int *offset);       /* 1 and the last good reply's offset, or 0 */
long board_age(void);                /* seconds since the last good reply came */
/* The futbol line and the rain of the next 2 h go once the reply is older
 * than this (0.5.0's review), the air's chips too (0.5.1's): the Worker's
 * FUT_MAX_AGE (30 min), well inside the 2 h that the rain's words cover. */
#define BOARD_FRESH_S 1800
/* The schedule in words: "Fetching", "Refresh in 6 s", "Next update in 4 min". */
void board_next_words(char *buf, int n);

/* The internet as the fetches show it (parse.h's net_quality: by the
 * connect's time), for the header: the last fetch's class, its time, its
 * connect time (-1 with no connection), and the console's clock at the
 * first fetch with no answer since the last answered one (0 while the
 * fetches get answers). */
extern int board_net_q;
extern s64 board_net_us, board_connect_us;
extern long board_net_down_utc;

/* The status in words and its colour, for the bottom bar of the Board and
 * the News pages (page.h has the pages' bodies and bars). */
u32 board_status_words(char *buf, int n);

#endif
