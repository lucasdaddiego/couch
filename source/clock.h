/* The Board's clock: the console's UTC (sysGetCurrentTime) plus the offset
 * of the first source that works : the console's time
 * zone setting (tz_params: TIMEZONE in minutes plus 60 when SUMMERTIME is
 * on), then the Board's offset for its city, else UTC. TIME_FORMAT picks 12
 * or 24 hours. */
#ifndef CLOCK_H
#define CLOCK_H

#include <ppu-types.h>

enum { CLK_UTC, CLK_CONSOLE, CLK_BOARD };
/* The read of the three settings: not run yet, read, skipped at the prompt,
 * frozen before (the journal), or not run because the folder is not
 * writable (no journal to record a freeze). */
enum { TZ_NOT_RUN, TZ_READ, TZ_SKIPPED, TZ_FROZEN, TZ_NO_JOURNAL };
extern int tz_state;
extern s32 tz_rc[3], tz_val[3];      /* TIME_FORMAT, TIMEZONE, SUMMERTIME: each call's rc and value */

/* The main thread: the read, once per session, journaled as tz_params at its
 * first use; behind the first-run prompt when the journal does not know the
 * call (an unattended round is the consent). 0, or -1 for START at the
 * prompt. */
int clock_tz(void);
int clock_offset(int *offset);       /* CLK_* and the offset in seconds */
int clock_h24(void);                 /* 0 for TIME_FORMAT 0 (12 h), else 1 */
long clock_utc(void);                /* the console's clock, Unix seconds */
/* The local time of day of utc as "09:05" or "9:05 AM", with the seconds
 * when secs is 1. */
void clock_words(char *buf, int n, long utc, int secs);
/* The clock's source in words, for the Board and the Info screen. */
void clock_source(char *buf, int n);
/* The clock, the date and the source line at x, y, within w (the header's
 * left part); the y under them. */
float clock_draw(float x, float y, float w);

#endif
