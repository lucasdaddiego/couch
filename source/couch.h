/* What Couch's files share: the folder's state, the start's facts and the
 * log, couch.txt in the app folder (main.c). */
#ifndef COUCH_H
#define COUCH_H

#include <ppu-types.h>
#include "version.h"

extern int fs_ok;                    /* APP_DIR is writable: the freeze journal works */
extern int fs_mk, fs_op, fs_wr;      /* the file test's rcs, for the Info screen when fs_ok is 0 */
extern int utf8_rc;                  /* text_utf8's rc: 0, or -1 (the Board's text is folded to ASCII) */
extern int unattended;               /* auto.txt: -1 a start by hand, 0 a file that is not a number, else the seconds */
extern s64 t_start;                  /* sysGetSystemTime at the start */

/* One line appended to couch.txt, with its line end; nothing when the folder
 * is not writable. The main thread only. */
void couch_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
