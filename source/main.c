/* Couch 0.3.1: the Board on the living-room TV, drawn by ps3gfx. The clock
 * in the console's time zone, the dollar rates and the money extras, the
 * weather with the rain, the sun and the moon, and the news, from the Worker
 * at couch.daddiego.com.ar (design: ps3tools_private's notes/couch-design/design.md (private),
 * cut to the Board on 2026-10-10). Three pages: the Board, the News, the
 * Info screen; L1 and R1 move between the Board and the News, SELECT opens
 * the Info page and CIRCLE closes it.
 * The News page is a reader since 0.3.1 (reader.h): the list of 30
 * headlines, an article and its QR code. After 10 min with no button (20
 * with an article open) the ambient mode shows the aurora (page.c).
 * Each start appends a block to couch.txt in the app folder, and each step
 * of the start and the exit a line to boot.txt (boot.c), the first one as
 * main's first statement. The start shows the loading screen from the first
 * frame after ui_init (start.c), then the start animation.
 *
 * The frame loop: read_pad, the ambient mode's wake, the exit, board_take
 * (a new reply from the Board thread), the input, the ambient mode's step,
 * the draw. Only this thread calls journal_*; the Board thread fills one
 * record (board.c) and, since 0.3.1, writes couch.txt's queued lines
 * (couch_log below), never another file. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/systime.h>
#include <sys/systime.h>
#include <sys/file.h>
#include <sysutil/sysutil.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "http.h"
#include "board.h"
#include "clock.h"
#include "info.h"
#include "page.h"
#include "parse.h"
#include "spu_fx_run.h"
#include "sky.h"
#include <ps3gfx/digits.h>
#include <ps3gfx/icons.h>
#include "boot.h"
#include "start.h"
#include "marquee.h"
#include "reader.h"
#include "stext.h"

#ifndef PS3GFX_COMMIT
#define PS3GFX_COMMIT "unknown"      /* the preview: the Makefile defines it on the console */
#endif

#define LOG APP_DIR "/couch.txt"
#define AUTO APP_DIR "/auto.txt"
#define LOG_MAX 65536                /* couch.txt is emptied at start past 64 KB */
#define HOLD_US 30000000             /* an unattended start stays in game at least 30 s (the Mac's round polls every 15 s) */

int fs_ok, fs_mk, fs_op, fs_wr;
int utf8_rc = -1;
int digits_rc = -1, icons_rc = -1;
int unattended = -1;
s64 t_start;
unsigned frame_ev;

/* couch.txt's queue (couch.h): a ring of LOGQ bytes, lq_head and lq_tail
 * counting the bytes put in (the main thread) and written out (the writer)
 * since the start. The writer is the Board thread (couch_log_drain) or the
 * main thread (a line written at once): lq_owner, taken by an atomic swap,
 * keeps one at a time from the read of the queue to the file's close. */
#define LOGQ 16384
#define LOG_LINE 400
static char logq[LOGQ];
static volatile unsigned lq_head, lq_tail;
static volatile int lq_owner;
static char lq_out[LOGQ + LOG_LINE];         /* the owner's: the queue and a line, one write */
volatile int log_async;

/* The queued bytes into lq_out from k: the new k. The owner only. */
static int lq_take(int k)
{
    unsigned t = lq_tail, h = lq_head;
    __sync_synchronize();                    /* the bytes before the head was read */
    for (; t != h; t++) lq_out[k++] = logq[t % LOGQ];
    __sync_synchronize();
    lq_tail = t;
    return k;
}

void couch_log_drain(void)
{
    if (lq_head == lq_tail || __sync_lock_test_and_set(&lq_owner, 1)) return;
    int k = lq_take(0);
    if (k) fs_write_file(LOG, lq_out, (u64)k, 1);
    __sync_lock_release(&lq_owner);
}

/* A millisecond's sleep, out of line: inlined in log_now's loop, gcc put
 * sysUsleep's "li r11,141" after its sc (the loop's turn), and Health's
 * syscalls.py could not read the number (0.1.0's first ELF had the same). */
static void __attribute__((noinline)) nap_ms(void) { sysUsleep(1000); }

/* The queue and line (n bytes) written now, the queue first. The Board
 * thread holds the queue through the file close. Wait for it before taking
 * lq_out so both the shared buffer and the append-only file stay serialized. */
static void log_now(const char *line, int n)
{
    while (__sync_lock_test_and_set(&lq_owner, 1)) nap_ms();
    int k = lq_take(0);
    memcpy(lq_out + k, line, (size_t)n);
    fs_write_file(LOG, lq_out, (u64)(k + n), 1);
    __sync_lock_release(&lq_owner);
}

void couch_log(const char *fmt, ...)
{
    char line[LOG_LINE];
    va_list ap;
    if (!fs_ok) return;
    frame_ev |= EV_LOG;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    unsigned h = lq_head;
    if (!log_async || h - lq_tail + (unsigned)n > LOGQ) {
        log_now(line, n);                    /* no writer thread, the exit, or a full queue */
        return;
    }
    for (int i = 0; i < n; i++) logq[(h + (unsigned)i) % LOGQ] = line[i];
    __sync_synchronize();                    /* the bytes before the new head */
    lq_head = h + (unsigned)n;
}

/* The three slowest frames of the Board's loop (the main thread's busy time,
 * ui_stats), with what each did besides its draw (frame_ev), for couch.txt
 * at the exit: a frame near 16.7 ms says which work to move. */
#define SLOW_N 3
static struct {
    u32 us, frame;
    double t;
    unsigned ev;
} slow[SLOW_N];
static u32 loop_n;

/* After each draw: the frame that its ui_flip ended (a shot after it counts
 * in the next frame, as its events do). */
static void slow_note(void)
{
    ui_stat s;
    unsigned ev = frame_ev;
    frame_ev = 0;
    loop_n++;
    ui_stats(&s, 0);
    int k = SLOW_N;
    while (k > 0 && s.busy_us > slow[k - 1].us) k--;
    if (k >= SLOW_N) return;
    memmove(&slow[k + 1], &slow[k], (SLOW_N - 1 - (size_t)k) * sizeof slow[0]);
    slow[k].us = s.busy_us;
    slow[k].frame = loop_n;
    slow[k].t = (double)(sysGetSystemTime() - t_start) / 1e6;
    slow[k].ev = ev;
}

/* Appends to w (n bytes) at k: the new end, at most n - 1 (a cut line, never past w). */
static int cat(char *w, int n, int k, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int cat(char *w, int n, int k, const char *fmt, ...)
{
    va_list ap;
    if (k >= n - 1) return k;
    va_start(ap, fmt);
    int r = vsnprintf(w + k, (size_t)(n - k), fmt, ap);
    va_end(ap);
    return r < 0 ? k : k + r >= n ? n - 1 : k + r;
}

static void slow_log(void)
{
    static const char *const ev_words[] = {"a reply", "a log line", "a journal line", "a check row", "the moon's upload", "a texture made",
                                           "a page change", "a shot"};
    char w[360];
    int k = 0;
    for (int i = 0; i < SLOW_N && slow[i].us; i++) {
        k = cat(w, sizeof w, k, "%s%.2f ms (frame %u at %.1f s:", i ? "; " : "", slow[i].us / 1000.0, (unsigned)slow[i].frame, slow[i].t);
        int any = 0;
        for (int b = 0; b < 8; b++)
            if (slow[i].ev & (1u << b)) k = cat(w, sizeof w, k, "%s %s", any++ ? "," : "", ev_words[b]);
        k = cat(w, sizeof w, k, "%s)", any ? "" : " the draw only");
    }
    if (k) couch_log("slowest frames: %s", w);
}

/* Every exit path: START, Quit Game from the PS button menu, and exit().
 * exit() runs this once (atexit), after main returned. The Board thread
 * ends first (its socket shut down, then the join), then the SPU group
 * (fx_stop: the exit reports and the join, or the terminate and the join;
 * its threads write into textures and the vertex area, so it is down
 * before ui_end), then the network (net_end), then the picture (ui_end). A
 * Board thread that did not end keeps the network as it is: no net_end
 * under a network call. */
static void leave(void)
{
    boot_mark("leave");                      /* boot.txt: the step about to run */
    log_async = 0;                           /* couch.txt's lines at once from here: the marks of a hang must be on the disk */
    int board = board_stop();
    boot_mark("fx_stop");
    fx_stop();
    boot_mark("net_end");
    if (board == 0) net_end();
    else couch_log("net_end: not run, the Board thread did not end");
    if (fs_ok) {
        start_log_late();                    /* no fetch came: the start's times now */
        ui_stat s;
        ui_stats(&s, 0);
        double sec = (double)(s.busy_sum_us + s.wait_sum_us) / 1e6, n = s.frames ? (double)s.frames : 1;
        couch_log("ui_stats: %u frames in %.1f s, %.2f fps; busy %.3f ms mean, %.3f max; wait %.3f ms mean; period %.3f ms max; %u over 25 ms; "
                  "%u vertices and %u draws max",
                  (unsigned)s.frames, sec, sec > 0 ? s.frames / sec : 0.0, (double)s.busy_sum_us / n / 1000, s.busy_max_us / 1000.0,
                  (double)s.wait_sum_us / n / 1000, s.period_max_us / 1000.0, (unsigned)s.slow, (unsigned)s.vertices_max,
                  (unsigned)s.draws_max);
        slow_log();
        couch_log("exit at %.1f s\n", (double)(sysGetSystemTime() - t_start) / 1e6);
    }
    boot_mark("ui_end");
    ui_end();
    boot_mark("left");
}

/* auto.txt (Cell Bench's unattended start): the seconds of a round, read
 * before the visible-area screen, which waits for the pad. */
static int auto_read(void)
{
    char b[32];
    int n = fs_read_file(AUTO, b, sizeof b - 1);
    return n <= 0 ? -1 : auto_parse(b, n);
}

static void log_start(void)
{
    sysFSStat st;
    civil_time c;
    if (!fs_ok) return;
    if (sysLv2FsStat(LOG, &st) == 0 && st.st_size > LOG_MAX) fs_write_file(LOG, "", 0, 0);
    civil_from_unix(clock_utc(), &c);
    couch_log("couch %s on ps3gfx %s: start at %04d-%02d-%02d %02d:%02d:%02d UTC, %s", APP_VERSION, PS3GFX_COMMIT, c.year, c.month, c.day,
              c.hour, c.min, c.sec, unattended > 0 ? "unattended" : unattended == 0 ? "unattended, auto.txt is not a number" : "by hand");
    couch_log("text_utf8: rc %d, digits_init: rc %d, icons_init: rc %d", utf8_rc, digits_rc, icons_rc);
    couch_log("board: %s (%s)%s%s", board_url, board_url_file ? "board_url.txt" : "the default", board_url_note[0] ? ". " : "", board_url_note);
}

/* The Board opens, at start and after the Info screen: the effects' start
 * (design 6.3: once, and again after a clear of the journal), the network
 * start (also after a clear of the journal), the clock's settings, the
 * thread.
 * -1 for a quit (Quit Game from the PS button menu) and for START at the
 * clock's prompt. The quit is read before the network start and again
 * after it, since the start's job screen draws frames, and each frame reads
 * the system's events. */
static int board_enter(void)
{
    sysUtilCheckCallback();                  /* a Quit Game sent before the first frame (no read_pad since the start) */
    if (quit) {
        couch_log("quit before the Board opened: %s", net_state == NET_UP ? "the network was up" : "no network start");
        return -1;
    }
    start_begin(SS_FX);                      /* the loading screen names the step (the first start only) */
    fx_start();                              /* the SPUs behind their job screen, or RSX only */
    start_end(SS_FX);
    if (net_state != NET_UP) {
        int before = net_state;
        start_begin(SS_NET);
        net_start(fs_ok);
        start_end(SS_NET);
        if (net_state == NET_UP || net_state == NET_FAILED)
            couch_log("net_start: rc 0x%08x, address %s; the netctl module rc 0x%08x, netCtlInit rc 0x%08x", (unsigned)net_rc, net_ip,
                      (unsigned)net_mod_rc, (unsigned)net_ctl_rc);
        else if (before != NET_BLOCKED) couch_log("net_start: not run, the journal holds it as %s", journal_state("net_start") == J_SKIPPED ? "skipped" : "frozen");
    }
    if (quit) {
        couch_log("quit during the network start: the Board thread not started");
        return -1;
    }
    if (clock_tz() < 0) return -1;
    if (net_state == NET_UP && !board_running()) board_start();
    return 0;
}

/* A page change by the pad. Leaving the Info page opens the Board as CIRCLE
 * did in 0.1.0 (the network start runs again after a clear of the journal):
 * -1 for a quit there. */
static int go(int to, int dir)
{
    int from = page;
    if (to == from) return 0;
    page = to;
    page_slide(from, to, dir);               /* design 3.8: the old page out, the new one in */
    if (to == PAGE_INFO) info_open();
    if (from == PAGE_INFO) return board_enter();
    return 0;
}

/* SQUARE on the Board: a refresh, or the network start and the thread
 * again; on the News page the list too. */
static int square(void)
{
    if (net_state == NET_UP && board_running()) {
        board_refresh();
        if (page == PAGE_NEWS) reader_refresh();
        return 0;
    }
    return board_enter();
}

static void shot(const char *path)
{
    frame_ev |= EV_SHOT;
    s32 rc = ui_shot(path);
    couch_log("shot %s: rc 0x%08x", path, (unsigned)rc);
}

/* An unattended round's steps, in seconds after the start: the Board's
 * screenshot, the Info page and its screenshot, the Board again. The round
 * ends at auto.txt's seconds (60 to 600): the ambient mode, at 600 s with no
 * button, cannot start before. */
enum { T_SHOT_BOARD, T_INFO, T_SHOT_INFO, T_BOARD, T_DONE };
static const int t_sec[T_DONE] = {30, 40, 45, 50};

int main(void)
{
    boot_mark("main");                       /* boot.txt: the first statement, before any other call */
    start_main();
    ui_init(&(const ui_app){"Couch", APP_VERSION, APP_DIR});
    t_start = sysGetSystemTime();
    start_end(SS_UI);                        /* the loading screen's first frame, at once */
    start_begin(SS_UTF8);
#ifdef COUCH_UTF8_OFF
    utf8_rc = -1;                            /* the preview's utf8_off build: a console whose text_utf8 failed */
#else
    utf8_rc = text_utf8();                   /* the Board's text is UTF-8 (the degree sign, a city's accents) */
#endif
    start_end(SS_UTF8);
    start_begin(SS_DIGITS);
    digits_rc = digits_init();               /* the Board's big numbers (design 6.3: after text_utf8) */
    start_end(SS_DIGITS);
    start_begin(SS_ICONS);
    icons_rc = icons_init();                 /* the weather's and the chips' icons */
    start_end(SS_ICONS);
    start_begin(SS_FS);
    fs_ok = fs_selftest(APP_DIR, &fs_mk, &fs_op, &fs_wr) == 0;
    start_end(SS_FS);
    unattended = auto_read();
    if (unattended >= 0 && fs_ok) fs_write_file(AUTO, "0\n", 2, 0);   /* at once: a later start by hand, also after a freeze, is not a round */
    if (!safe_load() && fs_ok && unattended < 0) safe_calibrate(1);   /* the first start: the visible area of this TV */
    atexit(leave);
    board_init();
    log_start();
    start_log_early();
    if (unattended == 0 || (unattended > 0 && !fs_ok)) {
        const char *why = !fs_ok ? "the app cannot write its folder" : "auto.txt does not hold a number of seconds";
        char line[160];
        couch_log("unattended start, no run: %s", why);
        snprintf(line, sizeof line, "Unattended start, no run: %s. Couch quits to the XMB.", why);
        while (!quit && sysGetSystemTime() - t_start < HOLD_US) {
            read_pad();
            if (ui_leave() < 0) break;
            page_draw_msg(line, YELLOW);
        }
        return 0;                            /* to the XMB: exit() runs leave() */
    }
    if (board_enter() < 0) return 0;
    ui_stat st;
    ui_stats(&st, 1);                        /* the frame times from here: the Board's loop */
    int step = 0, stext_on = 0;
    page = PAGE_BOARD;
    start_anim_begin();                      /* the loading screen hands over to the start animation */
    while (!quit) {
        int was = page;
        read_pad();
        ambient_input();                     /* a press that wakes the screen is cleared: START then does not exit */
        start_anim_input(pressed);           /* any button ends the start animation */
        if (pressed) page_slide_end();       /* and a slide (design 3.8) */
        int lv = ui_leave();
        if (lv < 0) break;
        board_take();
        if (board_fetches) {                 /* once: the first fetch's boot.txt mark and the start's times in couch.txt */
            start_end(SS_FETCH);
            start_log_late();
        }
        if ((pressed & (BTN_L1 | BTN_R1)) && page < PAGE_CYCLE) {
            int r = pressed & BTN_R1 ? 1 : -1;
            if (go((page + (r > 0 ? 1 : PAGE_CYCLE - 1)) % PAGE_CYCLE, r) < 0) break;
        } else if (page == PAGE_INFO) {
            if (lv == 0) {                   /* CIRCLE: back to the Board */
                if (go(PAGE_BOARD, -1) < 0) break;
            } else info_input();
        } else if (page == PAGE_NEWS && reader_input(lv == 0)) {
                                             /* the reader's pad: the selection, CROSS, an article's scroll, CIRCLE back */
        } else if (pressed & BTN_SELECT) {
            go(PAGE_INFO, 1);
        } else if ((pressed & BTN_SQUARE) && square() < 0) break;
        s64 t = sysGetSystemTime() - t_start;
        if (unattended > 0) {
            if (t >= (s64)unattended * 1000000) break;
            if (step == T_INFO && t >= t_sec[T_INFO] * 1000000LL) {
                page = PAGE_INFO;
                info_open();
                step++;
            } else if (step == T_BOARD && t >= t_sec[T_BOARD] * 1000000LL) {
                page = PAGE_BOARD;
                step++;
            }
        }
        ambient_tick();
        fx_tick(ambient && !ambient_day() ? FXM_AURORA : FXM_SKY);   /* 0.3.2: the ambient mode's sky by day */   /* after the last ui_flip: a picture's swap, the next picture */
        sky_tick();                          /* the moon's sphere (into the texture no frame shows), the bolts */
        marquee_tick();                      /* the ticker's text: a headline a frame into the picture no frame shows */
        if (page == PAGE_NEWS) stext_on = 1;
        if (stext_on) stext_tick();          /* the reader's atlases: from the first News page, a few rows a frame until built */
        reader_tick();                       /* the reader's scrolls and the news list's schedule */
        if (page != was) frame_ev |= EV_PAGE;
        page_draw();
        start_end(SS_FRAME);                 /* once: the first Board frame (boot.txt's mark) */
        slow_note();                         /* the frame that this draw's ui_flip ended, and what it did */
        if (unattended > 0 && (step == T_SHOT_BOARD || step == T_SHOT_INFO) && t >= t_sec[step] * 1000000LL) {
            shot(step == T_SHOT_BOARD ? APP_DIR "/board.ppm" : APP_DIR "/info.ppm");   /* between ui_flip and the next begin_frame */
            step++;
        }
    }
    return 0;                                /* exit() runs leave() */
}
