/* Couch's sky arithmetic (design 2026-10-13, sections 3.2, 3.6, 3.7): the
 * sun's and the moon's place, the moon's age and phase, the sun arc's
 * fraction. Pure functions of their arguments: the C library and libm only,
 * so the host check (tools/hostcheck) compiles astro.c as it is. Times are
 * Unix seconds (UTC), angles degrees, an elevation is geometric (no
 * refraction), an azimuth runs from north through east (0 to 360). */
#ifndef ASTRO_H
#define ASTRO_H

/* The sun's elevation and azimuth at utc for a place (NOAA's short formula,
 * "General Solar Position Calculations": about 0.2 deg off the full
 * algorithm). */
void sun_pos(long utc, double lat, double lon, double *elev, double *azim);

/* The sun arc of the sky tile: the local day from today's sunrise to its
 * sunset, rise_min and set_min in minutes after local midnight (wx_sun's
 * "06:24" through hm_parse). 1 by day, *frac = (now - rise) / (set - rise);
 * 0 by night, *frac = the fraction of the night from the set to the next
 * rise (today's rise and set stand for every day: a minute off at most);
 * -1 when rise or set is missing or the set is not after the rise. *to_next:
 * the seconds to the next set (day) or rise (night). offset: the place's
 * offset from UTC in seconds (the reply's offset). */
int sun_arc(long utc, int offset, int rise_min, int set_min, double *frac, long *to_next);

/* The moon's age in days since the mean new moon, 0 to MOON_SYNODIC: the
 * Worker's formula (board.js moonPhase: the new moon of 2000-01-06 18:14
 * UTC, the mean synodic month). A true new moon is within about 0.6 day of
 * age 0. */
#define MOON_SYNODIC 29.530588853
#define MOON_NEW_2000 947182440L
double moon_age(long utc);

/* The phase of an age, 0 to 7 (the Worker's eight phases of 3.7 days, each
 * centred on its moment: 0 new, 2 first quarter, 4 full, 6 last quarter),
 * and its words ("Waxing crescent"; the Worker's wx_moon adds " moon" to the
 * six that lack it). */
int moon_phase(double age);
extern const char *const moon_phase_name[8];

/* The lit fraction of the disc, 0 to 1: (1 - cos(2 pi age / month)) / 2. */
double moon_lit(double age);

/* The next full or new moon after utc, whichever comes first: its time, and
 * 1 for full, 0 for new. moon_next_words writes "Full moon 26 Oct" or "New
 * moon 9 Nov" on the place's date (offset in seconds). */
long moon_next(long utc, int *full);
void moon_next_words(char *buf, int n, long utc, int offset);

/* The moon's geocentric ecliptic longitude and latitude and its horizontal
 * parallax (the Astronomical Almanac's low-precision formulae: about 0.3 deg
 * in longitude, 0.2 in latitude). */
void moon_ecl(long utc, double *lon_ecl, double *lat_ecl, double *parallax);

/* The moon's topocentric elevation (the parallax taken off) and azimuth at
 * utc for a place: about 0.5 deg. The moon's centre is on the horizon of a
 * rise or a set at -0.83 deg, as the sun's. */
void moon_pos(long utc, double lat, double lon, double *elev, double *azim);

#endif
