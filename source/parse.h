/* Couch's pure functions: no PSL1GHT header and no state, so the host check
 * (tools/hostcheck) compiles parse.c as it is. The Board reply (format
 * version 1: the design's section 4), the end line, the head of an HTTP
 * answer, the Board URL, the text of a field and its fold to ASCII, the
 * clock's date and words, an age in words, auto.txt, the internet's class
 * of a fetch, the ambient mode's shift and page. */
#ifndef PARSE_H
#define PARSE_H

#define BOARD_MAX 4096               /* the reply's body, at most (the Worker's MAX_BYTES) */
#define BOARD_USD 8                  /* rate lines kept, at most */
#define BOARD_DAYS 3                 /* day lines kept, at most */
#define BOARD_MONEY 4                /* money lines kept, at most (the Worker sends 3) */
#define BOARD_NEWS 8                 /* news lines kept, at most (the Worker's NEWS_MAX) */

typedef struct {
    char label[24], buy[16], sell[16], when[16];
} board_usd;

typedef struct {
    char label[16], words[24], max[12], min[12], rain[12];
} board_day;

/* A money extra (since 2026-10-13): label|value|date|state|reason, state
 * "ok", "stale" (the value and date are old: the reason says why) or "none"
 * (value and date "-"). */
typedef struct {
    char label[48], value[24], when[16], state[8], reason[96];
} board_money;

/* A headline: source|age|title, the age in seconds at the Worker's now, -1
 * for an item with no date. The Worker cuts a title to 110 code points: 336
 * bytes hold 110 three-byte ones and the NUL. */
typedef struct {
    char source[32];
    long age;
    char title[336];
} board_news;

/* One upstream's state: "ok", "stale" or "none", "" when the reply had no
 * state line (Couch then treats it as ok). */
typedef struct {
    char state[8];
    char reason[96];
    long age;                        /* seconds since the Worker got the data, -1 when absent */
} board_feed;

typedef struct {
    int version;                     /* the number on line 1 */
    long now;                        /* the Worker's clock, Unix seconds, 0 when absent */
    int has_offset, offset;          /* the city's offset from UTC in seconds */
    char city[128];
    int n_usd;
    board_usd usd[BOARD_USD];
    board_feed usd_feed;
    int has_wx;                      /* a wx_now line came */
    /* words: Open-Meteo's words, or SMN's sky state, which the Worker cuts to
     * 24 code points when it has no English words for it (2 bytes each in
     * Latin-1, and the NUL) */
    char temp[12], feels[12], humidity[12], words[52], wind[16];
    int is_day;                      /* wx_now's 6th field: 1 day, 0 night */
    char wx_src[24];                 /* wx_now's 7th field, the now line's source: "SMN 23:00" or
                                        "Open-Meteo"; "" when absent (a Worker before 2026-10-12) */
    int n_day;
    board_day day[BOARD_DAYS];
    board_feed wx_feed;
    /* since 2026-10-13, keys a Couch 0.1.0 skips: the money extras, the
     * sky (wx_rain=rain|words or dry|words, wx_sun=rise|set, wx_moon=words)
     * and the news; each absent from an older Worker's reply */
    int n_money;
    board_money money[BOARD_MONEY];
    int has_rain, rain_wet;
    char rain[64];
    int has_sun;
    char sunrise[8], sunset[8];
    char moon[32];
    int n_news;
    board_news news[BOARD_NEWS];
    board_feed news_feed;            /* state "" when the reply had no news_state: the Worker has no feeds */
} board_data;

enum { BP_OK, BP_NOT_BOARD, BP_VERSION, BP_CUT };

/* The reply's body (n bytes) into *b: BP_OK when line 1 is "couch-board 1"
 * and an "end" line came; BP_VERSION (b->version holds the number) for
 * another version; BP_CUT with no end line; BP_NOT_BOARD when line 1 is not
 * a Board's. Unknown keys and lines with no '=' are skipped, a repeated key
 * keeps its order, lines after "end" are not read. fold: 1 when text_utf8
 * failed, so every text becomes ASCII (text_field). */
int board_parse(const char *body, int n, int fold, board_data *b);

/* 1 when the body holds its end line with the line end ("\nend\n" or
 * "\nend\r\n"): the thread stops reading there, without the server's close. */
int board_has_end(const char *body, int n);

/* A text field from n bytes of src into dst (size bytes with the NUL): the
 * bytes below 0x20 and 0x7f dropped, the spaces at both ends trimmed, cut on
 * a code point boundary. With fold, the UTF-8 becomes ASCII: a Latin-1 letter
 * its base letter (U+00E1 a, U+00F1 n), the degree sign nothing (each
 * temperature has one), a no-break space a space, any other code point and
 * a byte that starts no valid sequence '?'. */
void text_field(char *dst, int size, const char *src, int n, int fold);

/* The head of an HTTP answer in buf (n bytes): its length up to and with the
 * blank line, 0 while the blank line has not come, -1 when buf is not an HTTP
 * answer. *status, the Location value (cut to loc_size - 1 bytes), and
 * *chunked (a Transfer-Encoding with "chunked"). */
int http_head(const char *buf, int n, int *status, char *loc, int loc_size, int *chunked);

/* An http:// URL (or one with no scheme) as host, port and path, as PS3
 * Health's pick_url: 0, -2 for another scheme (https: Couch has no TLS), -1
 * for no host, a space or control byte, a bad port, or a host or path longer
 * than hs - 1 or ps - 1. */
int url_split(const char *url, char *host, int hs, int *port, char *path, int ps);

/* The date and time of t (Unix seconds) in UTC fields; wday 0 is Sunday. */
typedef struct {
    int year, month, day, wday, hour, min, sec;
} civil_time;
void civil_from_unix(long t, civil_time *c);
extern const char *const weekday_name[7];
extern const char *const month_name[12];

/* "09:05" (24 h, *ampm NULL) or "9:05" with *ampm "AM" or "PM" (12 h). */
void clock_hm(char *buf, int n, int hour, int min, int h24, const char **ampm);

/* "UTC-3", "UTC+5:30", "UTC" for an offset in seconds. */
void utc_label(char *buf, int n, int offset);

/* The console's TIMEZONE (minutes east of UTC) and SUMMERTIME as an offset in
 * seconds: 1, or 0 when TIMEZONE is outside -720 to 840 (PS3 Health's rule). */
int tz_offset(int tz_min, int summer, int *offset);

/* An age in words: "42 s", "5 min", "1 h 6 min", "2 d 7 h"; below 0 is 0. */
void age_words(char *buf, int n, long s);

/* A net_errno value in words ("refused", "timed out", "error 22"). */
const char *net_err_words(int e, char *buf, int n);

/* auto.txt's text: -1 for a start by hand (no file, blank or 0), 0 for text
 * that is not a number of seconds, else the seconds, 60 to 600. */
int auto_parse(const char *s, int n);

/* The internet as one Board fetch shows it: NQ_DOWN when the fetch got no
 * answer (answered 0: DNS, the connect, the send or the recv failed),
 * NQ_SLOW when the answer took NQ_SLOW_US or more, else NQ_OK. Round 1 on
 * the console fetched the Board in 0.75 s; 3 s is four times that, and far
 * under the 10 s socket timeouts. */
enum { NQ_NONE, NQ_OK, NQ_SLOW, NQ_DOWN };
#define NQ_SLOW_US 3000000LL
int net_quality(int answered, long long total_us);

/* The ambient mode (page.c): after AMBIENT_SEC with no button, the screen
 * dims, and each AMBIENT_STEP_SEC after that the picture moves to the next
 * of AMBIENT_SHIFTS offsets (at most AMBIENT_PX pixels from its place) and
 * the page changes between the Board and the News. step counts the steps
 * since the mode began (0 at its start). */
#define AMBIENT_SEC 600
#define AMBIENT_STEP_SEC 60
#define AMBIENT_PX 6
#define AMBIENT_SHIFTS 9
void ambient_shift(long step, int *dx, int *dy);
/* The page of a step: 0 the Board, 1 the News (odd steps, when the reply has
 * headlines). */
int ambient_page(long step, int has_news);

#endif
