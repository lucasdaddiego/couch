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

/* Couch's colour tokens (design 1.10), RGBA; ps3gfx's (ui.h) stay. */
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
#define CLARIN      0xe5484dff       /* Clarin's pill */
#define INFOBAE     0xf28c28ff       /* Infobae's pill */

/* One line appended to couch.txt, with its line end; nothing when the folder
 * is not writable. The main thread only. */
void couch_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* What a frame did besides its draw, for the slowest frames in couch.txt
 * (main.c reads and clears the bits once a frame): a reply taken, a line
 * of couch.txt, a journal line, a check row read back from RSX memory, the
 * moon's picture uploaded, a texture made, a page change, a shot. */
enum { EV_REPLY = 1, EV_LOG = 2, EV_JOURNAL = 4, EV_CHECK = 8, EV_MOON = 16, EV_TEX = 32, EV_PAGE = 64, EV_SHOT = 128 };
extern unsigned frame_ev;

#endif
