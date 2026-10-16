/* Couch's sky arithmetic (astro.h). The C library and libm only. */
#include <math.h>
#include <stdio.h>
#include "astro.h"
#include "parse.h"

#define PI_D 3.14159265358979323846
#define RAD(d) ((d) * (PI_D / 180.0))
#define DEG(r) ((r) * (180.0 / PI_D))

/* x into 0 to 360 */
static double wrap360(double x)
{
    x = fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}

/* The azimuth from north through east of a body at hour angle ha and
 * declination dec (degrees), for a latitude. */
static double azimuth(double ha, double dec, double lat)
{
    double a = atan2(sin(RAD(ha)), cos(RAD(ha)) * sin(RAD(lat)) - tan(RAD(dec)) * cos(RAD(lat)));
    return wrap360(DEG(a) + 180.0);
}

static double elevation(double ha, double dec, double lat)
{
    double s = sin(RAD(lat)) * sin(RAD(dec)) + cos(RAD(lat)) * cos(RAD(dec)) * cos(RAD(ha));
    return DEG(asin(s < -1 ? -1 : s > 1 ? 1 : s));
}

/* ---- the sun ------------------------------------------------------------------ */

void sun_pos(long utc, double lat, double lon, double *elev, double *azim)
{
    civil_time c;
    civil_from_unix(utc, &c);
    int leap = (c.year % 4 == 0 && c.year % 100 != 0) || c.year % 400 == 0;
    long doy = days_from_civil(c.year, c.month, c.day) - days_from_civil(c.year, 1, 1) + 1;
    double hour = c.hour + c.min / 60.0 + c.sec / 3600.0;
    double g = 2.0 * PI_D / (leap ? 366 : 365) * ((double)doy - 1 + (hour - 12) / 24);   /* the fractional year */
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g) -
                  0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
    double tst = hour * 60 + eqtime + 4 * lon;                 /* the true solar time in minutes (UTC: no zone) */
    double ha = tst / 4 - 180;
    *elev = elevation(ha, DEG(decl), lat);
    *azim = azimuth(ha, DEG(decl), lat);
}

int sun_arc(long utc, int offset, int rise_min, int set_min, double *frac, long *to_next)
{
    *frac = 0;
    *to_next = 0;
    if (rise_min < 0 || set_min < 0 || rise_min >= 1440 || set_min >= 1440 || set_min <= rise_min) return -1;
    long now = (utc + offset) % 86400;
    if (now < 0) now += 86400;
    long rise = rise_min * 60L, set = set_min * 60L;
    if (now >= rise && now < set) {
        *frac = (double)(now - rise) / (double)(set - rise);
        *to_next = set - now;
        return 1;
    }
    long night = rise + 86400 - set, gone = now >= set ? now - set : now + 86400 - set;
    *frac = (double)gone / (double)night;
    *to_next = night - gone;
    return 0;
}

/* ---- the moon ----------------------------------------------------------------- */

const char *const moon_phase_name[8] = {"New moon",   "Waxing crescent", "First quarter", "Waxing gibbous",
                                        "Full moon",  "Waning gibbous",  "Last quarter",  "Waning crescent"};

double moon_age(long utc)
{
    double d = (double)(utc - MOON_NEW_2000) / 86400.0;
    double a = d - MOON_SYNODIC * floor(d / MOON_SYNODIC);
    return a < 0 ? 0 : a >= MOON_SYNODIC ? 0 : a;
}

int moon_phase(double age) { return (int)floor(age / MOON_SYNODIC * 8 + 0.5) % 8; }

double moon_lit(double age) { return (1 - cos(2 * PI_D * age / MOON_SYNODIC)) / 2; }

long moon_next(long utc, int *full)
{
    double age = moon_age(utc), half = MOON_SYNODIC / 2;
    double to_full = age < half ? half - age : MOON_SYNODIC + half - age, to_new = MOON_SYNODIC - age;
    *full = to_full < to_new;
    return utc + (long)floor((*full ? to_full : to_new) * 86400.0 + 0.5);
}

void moon_next_words(char *buf, int n, long utc, int offset)
{
    int full;
    civil_time c;
    civil_from_unix(moon_next(utc, &full) + offset, &c);
    snprintf(buf, (size_t)n, "%s %d %.3s", full ? "Full moon" : "New moon", c.day, month_name[c.month - 1]);
}

void moon_ecl(long utc, double *lon_ecl, double *lat_ecl, double *parallax)
{
    double t = ((double)utc - 946728000.0) / 86400.0 / 36525.0;   /* Julian centuries from J2000 */
    *lon_ecl = wrap360(218.32 + 481267.881 * t + 6.29 * sin(RAD(135.0 + 477198.87 * t)) - 1.27 * sin(RAD(259.3 - 413335.36 * t)) +
                       0.66 * sin(RAD(235.7 + 890534.22 * t)) + 0.21 * sin(RAD(269.9 + 954397.74 * t)) -
                       0.19 * sin(RAD(357.5 + 35999.05 * t)) - 0.11 * sin(RAD(186.5 + 966404.03 * t)));
    *lat_ecl = 5.13 * sin(RAD(93.3 + 483202.02 * t)) + 0.28 * sin(RAD(228.2 + 960400.89 * t)) - 0.28 * sin(RAD(318.3 + 6003.15 * t)) -
               0.17 * sin(RAD(217.6 - 407332.21 * t));
    *parallax = 0.9508 + 0.0518 * cos(RAD(135.0 + 477198.87 * t)) + 0.0095 * cos(RAD(259.3 - 413335.36 * t)) +
                0.0078 * cos(RAD(235.7 + 890534.22 * t)) + 0.0028 * cos(RAD(269.9 + 954397.74 * t));
}

void moon_pos(long utc, double lat, double lon, double *elev, double *azim)
{
    double l, b, p, eps = RAD(23.439);
    moon_ecl(utc, &l, &b, &p);
    double ra = DEG(atan2(sin(RAD(l)) * cos(eps) - tan(RAD(b)) * sin(eps), cos(RAD(l))));
    double s = sin(RAD(b)) * cos(eps) + cos(RAD(b)) * sin(eps) * sin(RAD(l));
    double dec = DEG(asin(s < -1 ? -1 : s > 1 ? 1 : s));
    double gmst = 280.46061837 + 360.98564736629 * (((double)utc - 946728000.0) / 86400.0);
    double ha = wrap360(gmst + lon - ra);
    double alt = elevation(ha, dec, lat);
    *elev = alt - DEG(asin(sin(RAD(p)) * cos(RAD(alt))));    /* seen from the surface, not the centre */
    *azim = azimuth(ha, dec, lat);
}
