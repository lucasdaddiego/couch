/* Couch's pure functions: no PSL1GHT header and no state, so the host check
 * (tools/hostcheck) compiles parse.c as it is. The Board reply (format
 * version 1: the design's section 4), the end line, the head of an HTTP
 * answer, the Board URL, the text of a field and its fold to ASCII, the
 * clock's date and words, an age in words, auto.txt, the internet's class
 * of a fetch, the ambient mode's shift and page. Since 0.3.0 (the design of
 * 2026-10-13, section 5): the new keys, the change chips and the series by
 * label, the numbers in the Worker's LOCALE text, the sky's colours, the
 * weather codes' icons, the wind's words. The sun and the moon: astro.h. */
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

/* A change against the previous calendar day's close (design 5.1, since
 * 0.3.0): usd_chg=label|dir|pct|abs|date and money_chg=... with the same
 * fields. dir: CHG_UP, CHG_DOWN, CHG_FLAT (the chip's grey dash), CHG_NONE
 * for another word (no chip). pct and abs are the Worker's LOCALE text with
 * a sign ("+0,3%", "-3,20"), "-" when missing; when: the compared close's
 * date ("12 Oct"). */
enum { CHG_NONE, CHG_UP, CHG_DOWN, CHG_FLAT };
typedef struct {
    char label[48];
    int dir;
    char pct[16], abs[24], when[16];
} board_chg;

/* A 30-day series (design 5.1): spark=label|yyyy-mm-dd|v1 v2 ... the daily
 * sells in whole pesos, oldest first, from the date given; "-" a missing
 * day (v[i] -1). start_day: the first date as days since 1970-01-01, -1 when
 * it is not a date. */
#define BOARD_SPARKS 2               /* spark lines kept, at most (the Worker sends Blue's) */
#define SPARK_MAX 31                 /* values kept per line, at most (the Worker sends 30) */
typedef struct {
    char label[24];
    long start_day;
    int n;
    int v[SPARK_MAX];
} board_spark;

#define HOURS_MAX 12                 /* wx_hours' values per list */
#define TEMP_NONE (-999)             /* a missing temperature (deg C can be below 0) */

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
    /* since 0.3.0 (design 5.1), keys a Couch 0.1.0 or 0.2.0 skips. Numbers
     * in the C form (a dot for decimals); "-" or a value out of range is
     * missing: -1 (TEMP_NONE for a temperature). A repeated geo, wx_fx,
     * wx_hours or days_fx line replaces the one before. */
    int has_geo;                     /* geo=lat|lon */
    double lat, lon;                 /* degrees, -90 to 90 and -180 to 180 */
    int has_fx;                      /* wx_fx=code|cover|mm|dir|speed|gusts */
    int fx_code;                     /* the WMO code now, 0 to 99 */
    int fx_cover;                    /* the cloud cover %, 0 to 100 */
    double fx_mm;                    /* the precipitation of the last hour, mm (0 to 1000) */
    int fx_dir;                      /* where the wind comes from, degrees 0 to 359 */
    int fx_speed, fx_gusts;          /* km/h, 0 to 500, whole */
    int n_hours;                     /* wx_hours=start|12 temps|12 chances|12 codes: the temps' count */
    int hours_start;                 /* the first hour (local), 0 to 23 */
    int hour_temp[HOURS_MAX];        /* deg C whole, -90 to 70 */
    int hour_pop[HOURS_MAX];         /* the chance of precipitation %, 0 to 100 */
    int hour_code[HOURS_MAX];        /* WMO codes, 0 to 99 */
    int n_day_code;                  /* days_fx=c1|c2|c3: the WMO code of each day line, in order */
    int day_code[BOARD_DAYS];
    int n_usd_chg;
    board_chg usd_chg[BOARD_USD];    /* matched to a usd line by its label (board_chg_find) */
    int n_money_chg;
    board_chg money_chg[BOARD_MONEY];/* matched to a money line by its label */
    int n_spark;
    board_spark spark[BOARD_SPARKS]; /* matched to a usd line by its label (board_spark_find) */
    /* since 0.3.2: holiday=YYYY-MM-DD|name|days (the Worker of 2026-10-16) */
    int has_holiday;
    int hol_year, hol_month, hol_day;/* 1 to 12, 1 to 31 */
    int hol_days;                    /* days to it, 0 (today) to 400 */
    char hol_name[96];
} board_data;

enum { BP_OK, BP_NOT_BOARD, BP_VERSION, BP_CUT };

/* The reply's body (n bytes) into *b: BP_OK when line 1 is "couch-board 1"
 * and an "end" line came; BP_VERSION (b->version holds the number) for
 * another version; BP_CUT with no end line; BP_NOT_BOARD when line 1 is not
 * a Board's. Unknown keys and lines with no '=' are skipped, a repeated key
 * keeps its order, lines after "end" are not read. fold: 1 when text_utf8
 * failed, so every text becomes ASCII (text_field). */
int board_parse(const char *body, int n, int fold, board_data *b);

/* The change line of a label (a usd or money line's label, compared as the
 * parse wrote both), or NULL: no chip. money 0 searches usd_chg, 1
 * money_chg. The first match counts. */
const board_chg *board_chg_find(const board_data *b, int money, const char *label);
/* The spark line of a usd label, or NULL. */
const board_spark *board_spark_find(const board_data *b, const char *label);
/* The lowest and the highest present value of a series (the "30 d: 1.540 to
 * 1.565" words): the count of present values, 0 (and *lo, *hi untouched)
 * when none. */
int spark_range(const board_spark *s, int *lo, int *hi);

/* A number in the Worker's LOCALE text (es-AR): an optional sign, the whole
 * part with a '.' every three digits (or none), an optional ',' and 1 to 4
 * decimals, an optional '%' ("1.540,00", "589", "+0,3%", "-3,20"). At most
 * 12 digits. v: the value times 10^dec. plus and neg: the sign written. */
typedef struct {
    long long v;
    int dec, pct, plus, neg;
} board_num;
/* 1 and *x for a number, 0 for anything else ("-", "", "1,2,3", "1.23"). */
int num_parse(const char *s, board_num *x);
/* x as the Worker writes it: the thousands '.', dec decimals after ',', the
 * sign as parsed ("+0,0%" stays), the '%'. */
void num_format(char *buf, int n, const board_num *x);
/* The counting value between a (t 0) and b (t 1), t clamped to 0..1, with
 * b's decimals, '%' and sign style: a value that counts to b ends as b. */
void num_mix(const board_num *a, const board_num *b, double t, board_num *out);

/* "06:24" or "6:24" as minutes after midnight, -1 for anything else. */
int hm_parse(const char *s);

/* The sky gradient's two colours (design 3.2), 0xRRGGBB: the stops at the
 * middle of each elevation band of the table (-12 night and below, -9, -3,
 * 3, 13, 20 and above), linear between them; then the cloud cover (0 to
 * 100, -1 none) moves both toward grey by cover / 100 x 0.7; then wet (rain
 * or a storm now: wmo_wet) darkens both by 25 %. The grey is the design's
 * (5d6b80 top, 8a96a8 horizon) from 6 deg up; below, it is scaled down by
 * the light, to the luminance of the clear colour under it at most, so an
 * overcast night is not brighter than a clear one (the design's rule made
 * it 434e61 against 050a18). The scale is the larger of (elev + 6) / 12 and
 * that luminance ratio, so it moves with no step. */
void sky_colors(double elev, int cover, int wet, unsigned *top, unsigned *horizon);

/* A WMO weather code as a kind (design 1.11), WK_NONE for a missing code
 * (below 0); a code with no row is WK_CLOUD. */
enum { WK_NONE = -1, WK_CLEAR, WK_PARTLY, WK_CLOUD, WK_FOG, WK_RAIN, WK_SNOW, WK_STORM };
int wmo_kind(int code);
/* 1 for rain or a storm (the sky darkens, the hero shows streaks). */
int wmo_wet(int code);
/* The rain or the snow of the effects (design 3.5) from the WMO code now and
 * the precipitation of the last hour (mm, -1 missing): the count of drops
 * (*kind WP_DROPS) or flakes (WP_FLAKES), 0 when dry. A snow code gives 300
 * flakes. Drops: the larger of the code's count (drizzle 51 to 57: 300;
 * slight rain 61, 66, 80: 700; moderate 63, 81: 1,100; heavy 65, 67, 82
 * and a storm 95 to 99: 1,500) and the hour's (under 0.5 mm: 300, from 0.5:
 * 700, from 2.5: 1,100, from 7.6: 1,500; 0 or missing: 0). */
enum { WP_DROPS, WP_FLAKES };
int wx_precip(int code, double mm, int *kind);
/* The icon of a kind by day or night, as Couch's own ids (the drawing maps
 * them to ps3gfx's IC_ ids): WI_NONE for WK_NONE. */
enum { WI_NONE = -1, WI_SUN, WI_MOON_STARS, WI_CLOUD_SUN, WI_CLOUD_MOON, WI_CLOUD, WI_CLOUD_FOG, WI_CLOUD_RAIN, WI_CLOUD_SNOW,
       WI_CLOUD_LIGHTNING };
int wx_icon(int kind, int is_day);

/* The 8-point compass word of a wind direction in degrees ("N", "NE", ...
 * "NW"), "" when it is below 0. */
const char *wind_compass(int deg);
/* The dial's foot: "SE, gusts 22", "SE", "Gusts 22" or "". */
void wind_words(char *buf, int n, int deg, int gusts);

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

/* The path of a sibling of the Board's path (0.3.1's reader: "news",
 * "article?id=..." beside "board"): path up to its last '/' (a query cut
 * first), then leaf, into out (n bytes). 0, or -1 when it does not fit. */
int url_sibling(const char *path, const char *leaf, char *out, int n);

/* The date and time of t (Unix seconds) in UTC fields; wday 0 is Sunday. */
typedef struct {
    int year, month, day, wday, hour, min, sec;
} civil_time;
void civil_from_unix(long t, civil_time *c);
/* The days from 1970-01-01 to a civil date (month 1 to 12). */
long days_from_civil(long y, int m, int d);
/* The header's and the ambient mode's holiday words (0.3.2) for the local
 * time local_now (Unix seconds plus the offset): "Today is a holiday: X",
 * "Tomorrow is a holiday: X" or "Next holiday: Monday 9 November, X (in 25
 * days)". 0 and no words when b has none or its date has passed (a stale
 * reply), or it is over 400 days away. */
int holiday_words(const board_data *b, long local_now, char *out, int n);
/* Its days from local_now's date and its weekday (0 Sunday); 0 as above. */
int holiday_parts(const board_data *b, long local_now, long *days, int *wday);
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
#ifndef AMBIENT_SEC                  /* the preview's build sets 60 and 120 (tools/preview/build.zsh) */
#define AMBIENT_SEC 600
#endif
#ifndef AMBIENT_READ_SEC
#define AMBIENT_READ_SEC 1200        /* 0.3.1: while an article is open (reader.h), 20 min */
#endif
#define AMBIENT_STEP_SEC 60
#define AMBIENT_PX 6
#define AMBIENT_SHIFTS 9
void ambient_shift(long step, int *dx, int *dy);
/* The page of a step: 0 the Board, 1 the News (odd steps, when the reply has
 * headlines). */
int ambient_page(long step, int has_news);

/* The ambient block over the aurora (0.3.0, design 3.12): the clock, the
 * date and a line, at the next of 9 places each AMBIENT_STEP_SEC (the
 * places of ambient_shift, times AMBIENT_BLOCK_X and AMBIENT_BLOCK_Y px from
 * the canvas's centre), gliding to it over AMBIENT_GLIDE_S (ease in and
 * out, so the text moves by whole pixels for a moment only). t: seconds
 * since the mode began; *x, *y the offset. The line of the minute: 0 the
 * weather, 1 Blue's price, 2 the newest headline (by minute, then the next
 * that has data: has is a mask of 1 << line). */
#define AMBIENT_BLOCK_X 300
#define AMBIENT_BLOCK_Y 150
#define AMBIENT_GLIDE_S 6.0
void ambient_place(double t, float *x, float *y);
/* 0.3.2's centred block: a slow drift of at most AMBIENT_DRIFT_X and
 * AMBIENT_DRIFT_Y px from the centre, whole pixels, periods of 7 and 5 min
 * (the TV's burn-in guard; a pixel step every few seconds). */
#define AMBIENT_DRIFT_X 40
#define AMBIENT_DRIFT_Y 24
void ambient_drift(double t, float *x, float *y);
int ambient_line(long minute, unsigned has);

/* A headline's category from its source's label (0.3.0's News page): the
 * Worker sends no category (its feed weights name them, but a new key is not
 * for this stage), so only a source with one feed tells it: "Economy" for
 * Ambito (its economy feed), "World" for BBC (its world feed); NULL for any
 * other (Clarin has politics and world, Infobae comes from a search). The
 * label as the reply has it, UTF-8 or folded ("\xc3\x81mbito", "Ambito"). */
const char *news_category(const char *source);

/* The ticker's offset in px at t seconds for a speed of px_s px a second,
 * wrapped to [0, period): any fraction (the marquee draws it bilinear). */
double ticker_offset(double t, double px_s, double period);

/* ---- the reader (0.3.1, stage 11): the Worker's /news and /article -------------- */

/* GET /news (couch/worker/README.md, "The news reader"): "couch-news 1",
 * now=, item=id|source|category|age|title|summary (0 to 30, the newest
 * first), news_age=, news_state=, end. At most NEWS_MAX bytes (the Worker's
 * NEWS_BYTES). The same rules as the Board's: unknown keys and lines with
 * no '=' skipped, nothing after "end" read, the version on line 1. */
#define NEWS_MAX 32768
#define NEWS_ITEMS 30                /* item lines kept, at most (the Worker's NEWS_LIST) */
typedef struct {
    char id[9];                      /* 8 characters 0-9a-f, "" when the line's id is not one (the item cannot open) */
    char source[32];
    char category[20];               /* the Worker's word ("economy"), "" for "-" or a missing field */
    long age;                        /* seconds at the Worker's now, -1 for "-" */
    char title[336];                 /* 110 code points of 3 bytes and the NUL */
    char summary[848];               /* 280 code points of 3 bytes and the NUL; "" when none */
} news_item;
typedef struct {
    int version;
    long now;
    int n;
    news_item item[NEWS_ITEMS];
    board_feed feed;                 /* news_age and news_state ("" when absent) */
} news_data;
/* The body (n bytes) into *d: BP_OK with line 1 "couch-news 1" and the end
 * line, BP_VERSION, BP_CUT (no end line: the items so far are in *d),
 * BP_NOT_BOARD (line 1 is not a news reply). A line with no title is not
 * an item. fold as board_parse's. */
int news_parse(const char *body, int n, int fold, news_data *d);

/* GET /article?id=<id>: "couch-article 1", now=, id=, source=, category=,
 * title=, date=words|age, link=, state=, p= (0 or more), end. At most
 * ARTICLE_MAX bytes; the p= lines with "p=" and LF at most 12288 bytes. */
#define ARTICLE_MAX 16384
#define ART_TEXT 12288               /* the paragraphs' bytes, each with its NUL */
#define ART_PARAS 400                /* p= lines kept, at most (a 12 KB text of 30-byte lines) */
#define ART_LINK 1024                /* the link: at most 1024 bytes with "link=" */
typedef struct {
    int version;
    long now;
    char id[9], source[32], category[20];
    char title[904];                 /* 300 code points of 3 bytes and the NUL */
    char date[48];                   /* "Wed 14 Oct 12:12", "" for "-" */
    long age;                        /* seconds, -1 for "-" */
    char link[ART_LINK];             /* never folded: the QR code carries its bytes; "" when none */
    char state[8];                   /* "ok", "cut", "summary" or "none"; another word is none */
    char reason[96];                 /* the reason after '|' ("bbc.co.uk: HTTP 403") */
    int n_para;
    int para_off[ART_PARAS];         /* each paragraph's start in text, NUL-ended */
    int text_cut;                    /* 1 when a p= line did not fit in text (or past ART_PARAS) */
    char text[ART_TEXT];
} article_data;
/* The body into *a, with the return values of news_parse (line 1
 * "couch-article 1"). Empty p= lines are skipped. */
int article_parse(const char *body, int n, int fold, article_data *a);

/* A word-wrapped text (the article's paragraphs, its title): the lines of
 * s (n bytes) that fit maxw, as ps3gfx's text_wrap breaks them: at the last
 * space that fits, a word wider than maxw at the last code point that fits
 * (one code point at least), the spaces at a break dropped. width(s, len,
 * ctx) measures len bytes (the reader passes ps3gfx's text_w); widths add
 * up, so a line is measured word by word. off and len get each line's
 * start and length (no line of len 0 but for an empty text, which gives
 * none); at most max lines: the count (max when it is cut). */
typedef float (*wrap_width_fn)(const char *s, int len, void *ctx);
int wrap_lines(const char *s, int n, float maxw, wrap_width_fn width, void *ctx, int *off, int *len, int max);

/* The News list's grid (stage 10's look): item 0 the featured card across
 * the width, then rows of two cards. The move of a selection: dir 0 up, 1
 * down, 2 left, 3 right; the new index (the same at an edge). Down from a
 * left card whose row below has only its left card goes to it; down from
 * a right card to a row with no right card goes to that row's left card. */
enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT };
int grid_move(int sel, int n, int dir);
int grid_row(int i);                 /* 0 for the featured card, then 1 + (i - 1) / 2 */

/* The scroll (the list's and the article's): the target that shows y0 to
 * y1 of the content (view px tall, the content total px) with margin px
 * around it where the content goes on, from the target now: unchanged when
 * the span is in view. Clamped to 0 .. total - view (0 when it fits). */
double scroll_keep(double cur, double y0, double y1, double view, double total, double margin);
/* The scroll clamped to 0 .. total - view. */
double scroll_clamp(double s, double view, double total);
/* One frame of the eased scroll: from s toward target, the rest falling
 * by e^(-dt / tau) (dt and tau in seconds), so a new target mid-way keeps
 * the motion smooth; the target itself when within 0.02 px. */
double scroll_follow(double s, double target, double dt, double tau);

#endif
