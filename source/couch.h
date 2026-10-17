/* What Couch's files share: the folder's state, the start's facts and the
 * log, couch.txt in the app folder (main.c). */
#ifndef COUCH_H
#define COUCH_H

#include <ppu-types.h>
#include "version.h"

extern int fs_ok;                    /* APP_DIR is writable: the freeze journal works */
extern int fs_mk, fs_op, fs_wr;      /* the file test's rcs, for the Info screen when fs_ok is 0 */
extern int utf8_rc;                  /* text_utf8's rc: 0, or -1 (the Board's text is folded to ASCII) */
extern int digits_rc, icons_rc;      /* digits_init's and icons_init's rcs: 0, or -1 (no display digits: the
                                        Board's big numbers in F_LARGE; no icons: none drawn) */
extern int unattended;               /* auto.txt: -1 a start by hand, 0 a file that is not a number, else the seconds */
extern s64 t_start;                  /* sysGetSystemTime at the start */

/* Couch's colour tokens, RGBA; ps3gfx's (ui.h) stay. */
#define GLASS_TINT  0x0b142600       /* the glass over the blurred sky: the alpha by the sky's light, 0x73 to 0x9c */
#define GLASS_EDGE  0xffffff22       /* a tile's 1 px edge */
#define GLASS_SHADE 0x0000002e       /* its bottom line */
#define UP          0xffb35cff       /* a value went up (amber: neutral, not "good") */
#define DOWN        0x4fd1c5ff       /* a value went down (teal) */
#define SUN         0xffd36bff       /* the sun, the arc, the curve */
#define MOON_LIT    0xe9edf5ff       /* the moon's lit side */
#define STORM       0xc9a4ffff       /* lightning */
#define TEMP_COLD   0x6cb8ffff       /* the min/max bar's cold end */
#define TEMP_HOT    0xff8a5cff       /* its hot end */

/* x clamped to lo .. hi (board_page.c, sky.c, spu_fx_run.c: one copy since 0.5.1). */
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* One line appended to couch.txt, with its line end; nothing when the folder
 * is not writable. The main thread only.
 *
 * Since 0.3.1 (the console round of 0.3.0: the slowest frame, 16.2 ms, was
 * a Board reply and a couch_log line, whose open, write, fsync and close
 * ran on the main thread) the line goes to a queue while the Board thread
 * runs (log_async 1, from board_start until leave() clears it), and the
 * thread writes the queue between its requests (couch_log_drain, at most
 * every 50 ms, or after the request in flight: up to its 10 s timeouts).
 * Before the thread starts (the start's lines), after the exit cleared
 * log_async (the exit's marks of a hang, which must be on the disk before
 * their call), with no thread, and when the queue is full, the line is
 * written at once as before, after the queued ones: couch.txt keeps the
 * order of the calls. The journal (ps3gfx's journal.h) is not queued: its
 * lines are on the disk before their call, as always. */
void couch_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern volatile int log_async;       /* 1: couch_log queues (the Board thread writes the queue) */
/* The queue into couch.txt, in one write: the Board thread between its
 * requests. It does nothing while the main thread writes. */
void couch_log_drain(void);

/* What a frame did besides its draw, for the slowest frames in couch.txt
 * (main.c reads and clears the bits once a frame): a reply taken, a line
 * of couch.txt, a journal line, a check row read back from RSX memory, the
 * moon's picture uploaded, a texture made, a page change, a shot. */
enum { EV_REPLY = 1, EV_LOG = 2, EV_JOURNAL = 4, EV_CHECK = 8, EV_MOON = 16, EV_TEX = 32, EV_PAGE = 64, EV_SHOT = 128 };
extern unsigned frame_ev;

#endif
