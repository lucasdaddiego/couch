/* The Board (board.h). The thread makes only the calls of PS3 Health's
 * download, which ran on this console in its job thread: DNS, a blocking
 * connect with SO_RCVTIMEO and SO_SNDTIMEO, one send, recv until the close
 * or the end line. It reads its requests and board_on every 50 ms and
 * between the steps of a fetch. The exit (board_stop) shuts its socket down
 * and joins it with no time limit, so no network call is left when net_end
 * runs. Since 0.3.1 it also writes couch.txt's queued lines (main.c's
 * couch_log_drain: file calls only, the ones the main thread made) and
 * makes the reader's requests (reader.c); since 0.5.0 the Futbol page's
 * (futbol.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "spu_fx_run.h"
#include "reader.h"
#include "futbol.h"
#include "econ.h"
#include <ps3gfx/image.h>

#define PERIOD_US 300000000LL        /* a fetch every 5 min */
#define RETRY_US 60000000LL          /* 60 s after a failure */
#define GAP_US 10000000LL            /* SQUARE: at most once in 10 s */
#define LOG_FETCHES 50               /* fetch lines in couch.txt per start */
#define LAST APP_DIR "/board_last.txt"   /* the last whole Board reply: the next start shows it until its first fetch */
#define LAST_MAX_S 86400L            /* an older one is not shown */

char board_url[200];
char board_url_note[160];
static http_url url;                 /* set before the thread starts, only read after */
static char news_path[160];          /* /news and /article?id= beside the Board's path (url_sibling) */
static char art_base[160];
static char fut_base[160];           /* /futbol?d= (0.5.0) */
static char econ_path[160];          /* /economia (0.6.0) */
static char img_base[160];           /* /image?id= (0.6.0) */

/* The handoff: the thread writes the slot only while ready is 0 and sets
 * ready after a fence; the main thread parses it and clears ready after a
 * fence (Cell Bench's run.c). One body for every kind: the largest reply,
 * /news's 32 KB. The thread parses a news or article reply into the slot
 * and makes the article's QR code (no frame pays for them). */
static struct {
    int kind;                        /* REQ_* */
    char id[9];                      /* an article's id */
    int day;                         /* the futbol day */
    char body[NEWS_MAX + 1];
    http_result r;
    s64 at, us;                      /* the reply's system time, the fetch's duration */
    int prc;
    news_data news;
    article_data art;
    futbol_data fut;
    econ_data econ;
    int img_ok, img_w, img_h, img_rc;   /* an image reply: decoded into img_px; img_rc the decoder's (-3: the body was cut) */
    int qr_ok;
    u8 qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    u8 qr_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
} slot;
static volatile int ready, want, board_on, board_left;
static volatile int busy = -1;       /* the request in flight (REQ_*), -1 none */
/* The reader's requests: want_news 1 asks for /news; want_art is the
 * number of the latest article request (0: none), art_req its id. The main
 * thread writes the id, then a fence, then the number; the thread reads the
 * number, copies the id, and takes the request with a compare-and-swap of
 * the number to 0: a request that changed during the copy stays for the
 * next round (a torn id at most gives a reply that reader_take drops: its
 * id is not the one open). */
static volatile int want_news, want_art;
static volatile int want_fut;        /* the futbol day asked for, plus 1 (0: none) */
static volatile int want_econ;       /* 1 asks for /economia (0.6.0) */
static volatile int want_img;        /* the number of the latest image request (0: none), as want_art */
static volatile char img_req[9];
static int img_seq;
/* the photo's buffers (0.6.0): the bytes as they come, the texels, the decoder's arena */
static unsigned char img_raw[IMG_MAX] __attribute__((aligned(128)));
static u32 img_px[PHOTO_W * PHOTO_H] __attribute__((aligned(128)));
static unsigned char img_arena[IMAGE_ARENA_MIN] __attribute__((aligned(16)));
static volatile char art_req[9];
static int art_seq;
static volatile s64 next_us, last_us;   /* the thread's schedule: the next fetch, the last one's start */
static sys_ppu_thread_t tid;
static int started;

board_data board_cur;
int board_have;
board_data board_prev;
int board_prev_have;
int board_gen;
int board_fetches, board_fails, board_last_ok;
char board_last_words[200];
long board_last_utc;
s64 board_last_us;
int board_last_http, board_last_len;
int board_has_skew;
long board_skew;
int board_net_q = NQ_NONE;
s64 board_net_us, board_connect_us = -1;
long board_net_down_utc;
static board_data tmp;               /* the parse of the last reply, good or not */
static s64 cur_at;                   /* the system time of the reply in board_cur */
static int logged;
/* The words of the stale status line: a redirect without its Location, a
 * short line (the Info screen and couch.txt keep the Location). Since
 * 0.5.1 the data's time comes first in that line: a long reason cut it at
 * 1600 wide. */
static char stale_words[200];

void board_init(void)
{
    char line[200];
    int n = fs_read_line(APP_DIR "/board_url.txt", line, sizeof line);
    board_url_note[0] = 0;
    if (n > 0) {
        int rc = url_split(line, url.host, sizeof url.host, &url.port, url.path, sizeof url.path);
        if (rc == 0) {
            snprintf(board_url, sizeof board_url, "%s", line);
            return;
        }
        snprintf(board_url_note, sizeof board_url_note, "%s",
                 rc == -2 ? "board_url.txt: Couch has no TLS, so only http:// works."
                          : "board_url.txt holds no http:// URL.");
    }
    board_url[0] = 0;
    url.host[0] = 0;
}

/* The reader's paths beside the Board's (board_init's URL): the main
 * thread, before the thread starts. */
static void reader_paths(void)
{
    if (url_sibling(url.path, "news", news_path, sizeof news_path)) snprintf(news_path, sizeof news_path, "/news");
    if (url_sibling(url.path, "article?id=", art_base, sizeof art_base) || strlen(art_base) + 8 >= sizeof url.path)
        snprintf(art_base, sizeof art_base, "/article?id=");
    if (url_sibling(url.path, "futbol?d=", fut_base, sizeof fut_base) || strlen(fut_base) + 2 >= sizeof url.path)
        snprintf(fut_base, sizeof fut_base, "/futbol?d=");
    if (url_sibling(url.path, "economia", econ_path, sizeof econ_path)) snprintf(econ_path, sizeof econ_path, "/economia");
    if (url_sibling(url.path, "image?id=", img_base, sizeof img_base) || strlen(img_base) + 8 >= sizeof url.path)
        snprintf(img_base, sizeof img_base, "/image?id=");
}

void board_ask_news(void) { want_news = 1; }

void board_ask_futbol(int day) { want_fut = (day ? 1 : 0) + 1; }

void board_ask_econ(void) { want_econ = 1; }

void board_ask_image(const char *id)
{
    for (int i = 0; i < 9; i++) img_req[i] = id[i];
    __sync_synchronize();
    if (++img_seq <= 0) img_seq = 1;
    want_img = img_seq;
}

void board_ask_article(const char *id)
{
    for (int i = 0; i < 9; i++) art_req[i] = id[i];
    __sync_synchronize();
    if (++art_seq <= 0) art_seq = 1;
    want_art = art_seq;
}


/* ---- the thread -------------------------------------------------------------- */

/* The article's request, when one waits: 1 and its id in slot.id. */
static int take_article(void)
{
    int s = want_art;
    if (!s) return 0;
    __sync_synchronize();
    for (int i = 0; i < 9; i++) slot.id[i] = art_req[i];
    slot.id[8] = 0;
    __sync_synchronize();
    return __sync_bool_compare_and_swap(&want_art, s, 0);
}

/* A news or article reply: the parse and the QR code, in this thread. The
 * QR code: the smallest version at ECC low (big modules for a phone across
 * the room), its ECC raised where that version has room (Health's call). */
/* The image's request, when one waits: 1 and its id in slot.id (as take_article). */
static int take_image(void)
{
    int s = want_img;
    if (!s) return 0;
    __sync_synchronize();
    for (int i = 0; i < 9; i++) slot.id[i] = img_req[i];
    slot.id[8] = 0;
    __sync_synchronize();
    return __sync_bool_compare_and_swap(&want_img, s, 0);
}

static void reader_work(void)
{
    int fold = utf8_rc != 0;                 /* set before the thread started */
    slot.prc = -1;
    slot.qr_ok = 0;
    slot.img_ok = 0;
    if (slot.r.step != H_OK) return;
    if (slot.kind == REQ_IMAGE) {            /* the decode on this thread: a second or two for a big JPEG, no frame pays */
        int whole = slot.r.expect < 0 || slot.r.len >= slot.r.expect;
        slot.img_rc = whole ? image_decode(img_raw, slot.r.len, img_px, PHOTO_W, PHOTO_H, &slot.img_w, &slot.img_h, img_arena, sizeof img_arena) : -3;
        slot.img_ok = slot.img_rc == 0;
        slot.prc = slot.img_ok ? BP_OK : BP_NOT_BOARD;
        return;
    }
    if (slot.kind == REQ_NEWS) {
        slot.prc = news_parse(slot.body, slot.r.len, fold, &slot.news);
        return;
    }
    if (slot.kind == REQ_FUTBOL) {
        slot.prc = futbol_parse(slot.body, slot.r.len, fold, &slot.fut);
        return;
    }
    if (slot.kind == REQ_ECON) {
        slot.prc = econ_parse(slot.body, slot.r.len, fold, &slot.econ);
        return;
    }
    slot.prc = article_parse(slot.body, slot.r.len, fold, &slot.art);
    if (slot.prc == BP_OK && slot.art.link[0])
        slot.qr_ok = qrcodegen_encodeText(slot.art.link, slot.qr_tmp, slot.qr, qrcodegen_Ecc_LOW, 1, QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
}

/* board_last.txt: a line "couch-last 1|<the console's clock>|<the URL>",
 * then the reply. The thread writes it before it sets ready, so the body
 * does not change during the write. A cut write has no end line, and the
 * load drops it. */
static void save_last(const char *body, int len)
{
    char h[260];
    int n = snprintf(h, sizeof h, "couch-last 1|%ld|%s\n", clock_utc(), board_url);
    if (n > 0 && n < (int)sizeof h && fs_write_file(LAST, h, (uint64_t)n, 0) == 0) fs_write_file(LAST, body, (uint64_t)len, 1);
}

void board_load_last(void)
{
    static char buf[BOARD_MAX + 300];
    char why[80] = "";
    long saved = 0, age = 0;
    int n = fs_ok && board_url[0] ? fs_read_file(LAST, buf, (int)sizeof buf - 1) : -1;
    if (n <= 0) return;
    buf[n] = 0;
    char *nl = strchr(buf, '\n'), *bar;
    if (!nl || strncmp(buf, "couch-last 1|", 13) || !(bar = strchr(buf + 13, '|')) || bar > nl) snprintf(why, sizeof why, "no header");
    else {
        *nl = 0;
        saved = strtol(buf + 13, NULL, 10);
        age = clock_utc() - saved;
        if (strcmp(bar + 1, board_url)) snprintf(why, sizeof why, "another URL");
        else if (age < 0 || age > LAST_MAX_S) snprintf(why, sizeof why, "%ld s old", age);
        else if (board_parse(nl + 1, n - (int)(nl + 1 - buf), utf8_rc != 0, &tmp) != BP_OK) snprintf(why, sizeof why, "no whole reply");
        else {
            board_cur = tmp;
            board_have = 1;
            board_gen++;
            cur_at = sysGetSystemTime() - (s64)age * 1000000;
        }
    }
    if (why[0]) couch_log("board_last.txt: not shown, %s", why);
    else couch_log("board_last.txt: the reply of %ld s ago, shown until the first fetch", age);
}

static void board_thread(void *unused)
{
    (void)unused;
    s64 next = 0;                            /* the first fetch at once */
    while (board_on) {
        couch_log_drain();                   /* couch.txt's queued lines, between the requests */
        s64 now = sysGetSystemTime();
        int kind = -1;
        if (!ready) {
            if (take_article()) kind = REQ_ARTICLE;     /* the viewer waits for it: first */
            else if (take_image()) kind = REQ_IMAGE;    /* its photo (0.6.0): right after */
            else if (want_fut) kind = REQ_FUTBOL;   /* the page waits for it: before the Board (0.5.0's review) and the news list */
            else if (want_econ) kind = REQ_ECON;    /* the Economía page waits for it (0.6.0) */
            else if (now >= next || (want && now - last_us >= GAP_US)) kind = REQ_BOARD;
            else if (want_news) kind = REQ_NEWS;
        }
        if (kind >= 0) {
            http_url u = url;
            int max = BOARD_MAX;
            slot.kind = kind;
            if (kind == REQ_BOARD) {
                want = 0;
                last_us = now;
            } else if (kind == REQ_NEWS) {
                want_news = 0;
                snprintf(u.path, sizeof u.path, "%s", news_path);
                max = NEWS_MAX;
            } else if (kind == REQ_FUTBOL) {
                slot.day = want_fut - 1;
                want_fut = 0;
                snprintf(u.path, sizeof u.path, "%s%d", fut_base, slot.day);
                max = FUTBOL_MAX;
            } else if (kind == REQ_ECON) {
                want_econ = 0;
                snprintf(u.path, sizeof u.path, "%s", econ_path);
                max = ECON_MAX;
            } else if (kind == REQ_IMAGE) {
                snprintf(u.path, sizeof u.path, "%s%s", img_base, slot.id);
                max = IMG_MAX;
            } else {
                snprintf(u.path, sizeof u.path, "%s%s", art_base, slot.id);
                max = ARTICLE_MAX;
            }
            busy = kind;
            if (kind == REQ_IMAGE) http_get_bin(&u, img_raw, IMG_MAX, &slot.r, &board_on);
            else http_get(&u, slot.body, max, &slot.r, &board_on);
            slot.at = sysGetSystemTime();
            slot.us = slot.at - now;
            if (kind != REQ_BOARD && board_on) reader_work();
            busy = -1;
            if (!board_on) break;
            if (kind == REQ_BOARD) {
                /* a whole 200 reply waits 5 min, also one of another version: a retry would not change it */
                int whole = slot.r.step == H_OK && board_has_end(slot.body, slot.r.len);
                next = slot.at + (whole ? PERIOD_US : RETRY_US);
                next_us = next;
                if (whole && fs_ok) save_last(slot.body, slot.r.len);
            }
            __sync_synchronize();
            ready = 1;
            couch_log_drain();
        }
        sysUsleep(50000);
    }
    board_left = 1;
    sysThreadExit(0);
}

int board_start(void)
{
    if (started) return 0;
    reader_paths();
    board_on = 1;
    board_left = 0;
    s32 rc = sysThreadCreate(&tid, board_thread, NULL, 1500, 0x20000, THREAD_JOINABLE, "board");
    if (rc) {
        board_on = 0;
        couch_log("board thread: sysThreadCreate rc 0x%08x", (unsigned)rc);
        return rc;
    }
    started = 1;
    log_async = 1;                           /* couch_log queues its lines from now: the thread writes them */
    return 0;
}

int board_running(void) { return started; }

/* No time limit on the join: one that gave up would leave the thread in a
 * network call while net_end and the process exit end the network under it.
 * The wait, by where the thread is: in a recv or a send, the shutdown ends
 * the call at once (SO_RCVTIMEO and SO_SNDTIMEO end it within 10 s in any
 * case); in its 50 ms sleep, the sleep; in a DNS lookup or a connect, which
 * a shutdown does not end, the resolver's or the TCP stack's own timeout,
 * not measured on this console. http_get reads board_on after each step and
 * makes no further call. */
int board_stop(void)
{
    if (!started) return 0;
    int in_fetch = busy >= 0;
    s64 t0 = sysGetSystemTime();
    couch_log("board thread: the exit stops it%s", in_fetch ? ", it is in a fetch" : "");   /* the mark of a hang in the join */
    board_on = 0;
    http_wake();                             /* the fence, then netShutdown on the open socket */
    u64 rv = 0;
    s32 rc = sysThreadJoin(tid, &rv);
    double sec = (double)(sysGetSystemTime() - t0) / 1e6;
    if (rc == 0 || board_left) started = 0;  /* a thread that may still run stays started: a later call joins again, no net_end */
    if (rc == 0) couch_log("board thread: joined in %.2f s%s", sec, in_fetch ? ", it was in a fetch" : "");
    else couch_log("board thread: sysThreadJoin rc 0x%08x after %.2f s, %s", (unsigned)rc, sec, board_left ? "the thread had left" : "the thread may still run");
    return rc == 0 || board_left ? 0 : -1;
}

void board_refresh(void)
{
    if (started) want = 1;
}

/* ---- the main thread's side ---------------------------------------------------- */

/* brief: a redirect without its Location, for the stale status line.
 * kind: whose reply (the words of a bad one, the size cap); version: the
 * number of a reply of another version. */
static void fetch_words(char *w, int n, const http_result *r, int prc, int brief, int kind, int version)
{
    static const char *const what[6] = {"Board", "news list", "article", "futbol list", "economy data", "photo"};
    static const int cap_kb[6] = {BOARD_MAX / 1024, NEWS_MAX / 1024, ARTICLE_MAX / 1024, FUTBOL_MAX / 1024, ECON_MAX / 1024, IMG_MAX / 1024};
    char e[24];
    if (kind < REQ_BOARD || kind > REQ_IMAGE) kind = REQ_BOARD;
    switch (r->step) {
    case H_OK:
        if (prc == BP_OK) snprintf(w, (size_t)n, "ok");
        else if (prc == BP_VERSION) snprintf(w, (size_t)n, "%s%s format %d: update Couch", kind ? "the " : "", what[kind], version);
        else if (prc == BP_CUT) snprintf(w, (size_t)n, "the reply was cut (no end line)");
        else snprintf(w, (size_t)n, "the answer is not a%s %s reply", kind == REQ_ARTICLE ? "n" : "", what[kind]);
        break;
    case H_DNS: snprintf(w, (size_t)n, "DNS failed for %s", url.host); break;
    case H_SOCKET: snprintf(w, (size_t)n, "no socket (%s)", net_err_words((int)r->err, e, sizeof e)); break;
    case H_CONNECT: snprintf(w, (size_t)n, "no connection to %s:%d (%s)", url.host, url.port, net_err_words((int)r->err, e, sizeof e)); break;
    case H_SEND: snprintf(w, (size_t)n, "the request was not sent (%s)", net_err_words((int)r->err, e, sizeof e)); break;
    case H_RECV:
        if (r->err == 35 || r->err == 60) snprintf(w, (size_t)n, "no answer in 10 s");
        else snprintf(w, (size_t)n, "the answer broke off (%s)", net_err_words((int)r->err, e, sizeof e));
        break;
    case H_CLOSED: snprintf(w, (size_t)n, "the connection closed before an answer"); break;
    case H_NOT_HTTP: snprintf(w, (size_t)n, "the answer is not HTTP"); break;
    case H_HEAD: snprintf(w, (size_t)n, "the answer's head is over 4 KB"); break;
    case H_STATUS:
        if (r->http >= 300 && r->http < 400 && r->location[0])
            snprintf(w, (size_t)n, "HTTP %d%s%s: the host still redirects plain HTTP", r->http, brief ? "" : " to ", brief ? "" : r->location);
        else snprintf(w, (size_t)n, "HTTP %d", r->http);
        break;
    case H_CHUNKED: snprintf(w, (size_t)n, "the answer is chunked, Couch reads a plain body"); break;
    case H_OVER: snprintf(w, (size_t)n, "the reply is over %d KB", cap_kb[kind]); break;
    default: snprintf(w, (size_t)n, "stopped"); break;
    }
}

/* The internet as any reply shows it (the Board's, the news list's, an
 * article's, a futbol list's): no answer when the DNS lookup, the socket,
 * the connect, the send or the recv failed (also the 10 s timeout); any
 * answer, also an HTTP error or a bad reply, reached the Worker. Slow or
 * ok by the connect's time since 0.5.0's review (parse.h's net_quality):
 * a reply's whole time holds the Worker's own fetches (the Board's
 * upstreams and a futbol list's source up to 4 s, an article's page up to
 * 8 s), which said "slow" of a fast line. */
static void net_seen(const http_result *r, long utc)
{
    if (r->step == H_STOPPED) return;
    int answered = r->step != H_DNS && r->step != H_SOCKET && r->step != H_CONNECT && r->step != H_SEND && r->step != H_RECV;
    board_connect_us = r->connect_us;
    board_net_q = net_quality(answered, (long long)r->connect_us);
    board_net_us = slot.us;
    if (answered) board_net_down_utc = 0;
    else if (!board_net_down_utc) board_net_down_utc = utc;
}

/* A news or article reply: reader_take (reader.c) copies what it keeps and
 * logs it; a futbol reply: futbol_take (futbol.c). */
static void take_reader(void)
{
    board_reply b;
    memset(&b, 0, sizeof b);
    b.kind = slot.kind;
    memcpy(b.id, slot.id, sizeof b.id);
    b.day = slot.day;
    b.r = &slot.r;
    b.at = slot.at;
    b.us = slot.us;
    b.prc = slot.prc;
    b.news = &slot.news;
    b.art = &slot.art;
    b.fut = &slot.fut;
    b.econ = &slot.econ;
    b.img_ok = slot.img_ok;
    b.img_rc = slot.img_rc;
    b.img_w = slot.img_w;
    b.img_h = slot.img_h;
    b.img = img_px;
    b.qr_ok = slot.qr_ok;
    b.qr = slot.qr;
    fetch_words(b.words, sizeof b.words, &slot.r, slot.prc, 0, slot.kind,
                slot.kind == REQ_NEWS ? slot.news.version : slot.kind == REQ_FUTBOL ? slot.fut.version : slot.kind == REQ_ECON ? slot.econ.version
                : slot.kind == REQ_IMAGE ? 1 : slot.art.version);
    net_seen(&slot.r, clock_utc());
    if (net_state == NET_UP) net_addr();    /* the thread is idle while ready is set (board_take's note) */
    if (slot.kind == REQ_FUTBOL) futbol_take(&b);
    else if (slot.kind == REQ_ECON) econ_take(&b);
    else if (slot.kind == REQ_IMAGE) reader_take_image(&b);
    else reader_take(&b);
}

void board_take(void)
{
    if (!ready) return;
    __sync_synchronize();
    frame_ev |= EV_REPLY;
    if (slot.kind != REQ_BOARD) {
        take_reader();
        __sync_synchronize();
        ready = 0;
        return;
    }
    const http_result *r = &slot.r;
    int prc = -1;
    if (r->step == H_OK) {
        prc = board_parse(slot.body, r->len, utf8_rc != 0, &tmp);
        if (prc == BP_OK) {
            board_prev = board_cur;          /* the reply before, for the changed tiles (board_page.c) */
            board_prev_have = board_have;
            board_cur = tmp;                 /* swapped in only whole: version 1 and the end line */
            board_have = 1;
            board_gen++;
            cur_at = slot.at;
            board_has_skew = tmp.now > 0;
            if (board_has_skew) board_skew = clock_utc() - tmp.now;
        }
    }
    board_fetches++;
    board_last_ok = prc == BP_OK;
    if (!board_last_ok) board_fails++;
    fetch_words(board_last_words, sizeof board_last_words, r, prc, 0, REQ_BOARD, tmp.version);
    fetch_words(stale_words, sizeof stale_words, r, prc, 1, REQ_BOARD, tmp.version);
    board_last_utc = clock_utc();
    board_last_us = slot.us;
    board_last_http = r->http;
    board_last_len = r->len;
    net_seen(r, board_last_utc);             /* the internet's state */
    /* The address again, for the status line and the Info screen (DHCP may have
     * answered since): here the thread is idle, since it starts no fetch while
     * ready is set, so no two network calls overlap. */
    if (net_state == NET_UP) net_addr();
    if (logged < LOG_FETCHES) {
        logged++;
        char t[24];
        clock_words(t, sizeof t, board_last_utc, 1);
        char cn[24] = "no connection", fx[96];
        if (r->connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)r->connect_us / 1e6);
        fx_log_words(fx, sizeof fx);         /* the last job serials: a freeze hours in leaves a trace */
        if (board_last_ok)
            couch_log("fetch %d at %s (%.1f s): %.2f s (%s), HTTP %d, %d bytes: ok, now %ld, offset %d, console - Worker %+ld s, %d news%s",
                      board_fetches, t, (double)(slot.at - t_start) / 1e6, (double)slot.us / 1e6, cn, r->http, r->len, tmp.now, tmp.offset,
                      board_skew, tmp.n_news, fx);
        else
            couch_log("fetch %d at %s (%.1f s): %.2f s (%s), HTTP %d, %d bytes: %s%s", board_fetches, t, (double)(slot.at - t_start) / 1e6,
                      (double)slot.us / 1e6, cn, r->http, r->len, board_last_words, fx);
    }
    __sync_synchronize();
    ready = 0;
}

int board_offset(int *offset)
{
    if (!board_have || !board_cur.has_offset) return 0;
    *offset = board_cur.offset;
    return 1;
}

long board_age(void) { return board_have ? (long)((sysGetSystemTime() - cur_at) / 1000000) : 0; }

void board_next_words(char *buf, int n)
{
    char a[24];
    s64 now = sysGetSystemTime();
    buf[0] = 0;
    if (!started) return;
    if (busy == REQ_BOARD) snprintf(buf, (size_t)n, "Fetching the Board");
    else if (want && now - last_us < GAP_US) {
        age_words(a, sizeof a, (long)((last_us + GAP_US - now + 999999) / 1000000));
        snprintf(buf, (size_t)n, "Refresh in %s", a);
    } else if (board_fetches && next_us > now) {
        age_words(a, sizeof a, (long)((next_us - now + 999999) / 1000000));
        snprintf(buf, (size_t)n, "%s in %s", board_last_ok ? "Next update" : "Next try", a);
    }
}

/* ---- the page -------------------------------------------------------------------- */

/* The time of the data in board_cur, "23:50", with its date before it
 * ("16 Oct 23:50") when that is not today's (0.5.1's review: an old reply's
 * time read as today's). */
static void data_words(char *buf, int n)
{
    int off;
    civil_time a, c;
    char t[24];
    long at = clock_utc() - board_age();
    clock_offset(&off);
    civil_from_unix(at + off, &a);
    civil_from_unix(clock_utc() + off, &c);
    clock_words(t, sizeof t, at, 0);
    if (a.year == c.year && a.month == c.month && a.day == c.day) snprintf(buf, (size_t)n, "%s", t);
    else snprintf(buf, (size_t)n, "%d %.3s %s", a.day, month_name[a.month - 1], t);
}

u32 board_status_words(char *buf, int n)
{
    char t[40];
    if (!board_url[0]) {                                     /* since 0.4.0: no default URL */
        snprintf(buf, (size_t)n, "No Board URL yet: the Board page has the steps.");
        return YELLOW;
    }
    switch (net_state) {
    case NET_BLOCKED:
        snprintf(buf, (size_t)n, "%s",
                 journal_state("net_start") == J_SKIPPED
                     ? "Network not started: net_start was skipped before. SELECT info, then SQUARE twice clears the journal."
                     : "Network not started: net_start froze this console before. SELECT info, then SQUARE twice clears the journal.");
        return YELLOW;
    case NET_FAILED: snprintf(buf, (size_t)n, "No network: netInitialize failed (rc 0x%08x). SQUARE tries again.", (unsigned)net_rc); return RED;
    case NET_NONE: snprintf(buf, (size_t)n, "The network is not started yet."); return GREY;
    }
    if (!started) {
        snprintf(buf, (size_t)n, "The Board thread did not start (couch.txt has the rc). SQUARE tries again.");
        return RED;
    }
    if (!board_fetches) {
        if (board_have) {                                    /* since 0.4.1: board_last.txt's reply */
            data_words(t, sizeof t);
            snprintf(buf, (size_t)n, "Fetching the Board ... It shows the data of %s.", t);
        } else snprintf(buf, (size_t)n, "Fetching the Board ...");
        return GREY;
    }
    if (!board_last_ok && !net_has_ip()) {
        snprintf(buf, (size_t)n, "No network: the console has no IP address.");
        return YELLOW;
    }
    if (!board_last_ok && board_have) {
        data_words(t, sizeof t);
        snprintf(buf, (size_t)n, "The Board shows the data of %s. The last fetch failed: %s.", t, stale_words);
        return YELLOW;
    }
    if (!board_last_ok) {
        snprintf(buf, (size_t)n, "No Board data yet: %s.", board_last_words);
        return YELLOW;
    }
    data_words(t, sizeof t);
    snprintf(buf, (size_t)n, "Board updated at %s.", t);
    return GREY;
}
