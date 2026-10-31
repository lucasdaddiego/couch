/* Couch's pure functions: no PSL1GHT header and no state, so the host check
 * (tools/hostcheck) compiles parse.c as it is. The Board reply (format
 * version 1: the design's section 4), the end line, the head of an HTTP
 * answer, the Board URL, the text of a field and its fold to ASCII, the
 * clock's date and words, an age in words, auto.txt, the internet's class
 * of a fetch, the ambient mode's shift and page. Since 0.3.0 (the design of
 * 2026-10-13, section 5): the new keys, the change chips and the series by
 * label, the numbers in the Worker's LOCALE text, the sky's colours, the
 * weather codes' icons, the wind's words. The sun and the moon: astro.h.
 * Since 0.5.0: the rain of the next 2 h, the air, the futbol line,
 * night.txt and the Worker's /futbol reply. */
#ifndef PARSE_H
#define PARSE_H

#define BOARD_MAX 4096               /* the reply's body, at most (the Worker's MAX_BYTES) */
#define BOARD_RATES 8                  /* rate lines kept, at most */
#define BOARD_DAYS 3                 /* day lines kept, at most */
#define BOARD_NEWS 8                 /* news lines kept, at most (the Worker's NEWS_MAX) */

/* rate=label|buy|value|date: the buy price is not kept (0.5.1: the
 * Worker writes "-", and the Board shows the value only). */
typedef struct {
    char label[24], sell[16], when[16];
} board_rate;

typedef struct {
    char label[16], words[24], max[12], min[12], rain[12];
} board_day;

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

/* A change against the previous calendar day's close:
 * rate_chg=label|dir|pct|abs|date, the amount and the date not kept (0.5.1:
 * no chip shows them). dir: CHG_UP, CHG_DOWN, CHG_FLAT (no chip), CHG_NONE
 * for another word (no chip). pct is the Worker's LOCALE text with a sign
 * ("+0,3%"), "-" when missing. The label is cut as a rate line's. */
enum { CHG_NONE, CHG_UP, CHG_DOWN, CHG_FLAT };
typedef struct {
    char label[24];
    int dir;
    char pct[16];
} board_chg;

/* A 30-day series: spark=label|yyyy-mm-dd|v1 v2 ... the daily
 * values, oldest first, from the date given (not kept: the sparkline has
 * no dates); "-" a missing day (v[i] -1). */
#define BOARD_SPARKS 2               /* spark lines kept, at most (the Worker sends the hero tile's) */
#define SPARK_MAX 31                 /* values kept per line, at most (the Worker sends 30) */
typedef struct {
    char label[24];
    int dec;                         /* the values are times 10^dec (since 0.4.0) */
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
    int n_rate;
    board_rate rate[BOARD_RATES];
    /* feature=label|symbol (since 0.4.0): the hero tile's rate and its
     * symbol ("$" Couch's dollar icon, other text as text); "" when absent */
    char feature[24], feature_sym[12];
    board_feed rate_feed;
    int has_wx;                      /* a wx_now line came */
    /* words: the weather in words, which the Worker cuts to
     * 24 code points when it has no English words for it (2 bytes each in
     * Latin-1, and the NUL) */
    char temp[12], feels[12], humidity[12], words[52], wind[16];
    int is_day;                      /* wx_now's 6th field: 1 day, 0 night (a 7th, from an older Worker, is skipped) */
    int n_day;
    board_day day[BOARD_DAYS];
    board_feed wx_feed;
    /* since 2026-10-13, keys a Couch 0.1.0 skips: the sky
     * (wx_rain=rain|words or dry|words, wx_sun=rise|set; an older Worker's
     * wx_moon is skipped: astro.h has the moon) and the news; each absent
     * from an older Worker's reply (0.5.1: the extras, extra= and
     * extra_chg=, are skipped too: no Worker sends them) */
    int has_rain, rain_wet;
    char rain[64];
    int has_sun;
    char sunrise[8], sunset[8];
    int n_news;
    board_news news[BOARD_NEWS];
    board_feed news_feed;            /* state "" when the reply had no news_state: the Worker has no feeds */
    /* since 0.3.0, keys a Couch 0.1.0 or 0.2.0 skips. Numbers
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
    int n_rate_chg;
    board_chg rate_chg[BOARD_RATES];    /* matched to a rate line by its label (board_chg_find) */
    int n_spark;
    board_spark spark[BOARD_SPARKS]; /* matched to a rate line by its label (board_spark_find) */
    /* since 0.3.2: holiday=YYYY-MM-DD|name|days (the Worker of 2026-10-16;
     * days, 0 to 400, is only checked: holiday_parts counts them from the
     * date) */
    int has_holiday;
    int hol_year, hol_month, hol_day;/* 1 to 12, 1 to 31 */
    char hol_name[96];
    /* since 0.5.0, keys a Couch 0.4.1 skips: wx_soon=words (the rain of
     * the next 2 h from Open-Meteo's 15-minute values: "Rain from 18:45",
     * "Rain until 19:30"), air=aqi|uv (whole numbers, "-" missing: -1) and
     * futbol=live|words (the private Worker: "2 live", "Next: Lions-Bears
     * 21:00"); each absent from an older Worker's reply */
    int has_soon;
    char soon[64];
    int has_air;
    int air_aqi;                     /* the US AQI now, 0 to 999 */
    int air_uv;                      /* the UV index now, 0 to 30 */
    int has_futbol;
    int fut_live;                    /* games live now, 0 to 999 */
    char fut_words[128];
} board_data;

enum { BP_OK, BP_NOT_BOARD, BP_VERSION, BP_CUT };

/* The reply's body (n bytes) into *b: BP_OK when line 1 is "couch-board 1"
 * and an "end" line came; BP_VERSION (b->version holds the number) for
 * another version; BP_CUT with no end line; BP_NOT_BOARD when line 1 is not
 * a Board's. Unknown keys and lines with no '=' are skipped, a repeated key
 * keeps its order, lines after "end" are not read. fold: 1 when text_utf8
 * failed, so every text becomes ASCII (text_field). */
int board_parse(const char *body, int n, int fold, board_data *b);

/* The change line of a rate line's label (compared as the parse wrote
 * both), or NULL: no chip. The first match counts. */
const board_chg *board_chg_find(const board_data *b, const char *label);
/* The spark line of a rate label, or NULL. */
const board_spark *board_spark_find(const board_data *b, const char *label);
/* The lowest and the highest present value of a series (the "30 d: 1.540 to
 * 1.565" words): the count of present values, 0 (and *lo, *hi untouched)
 * when none. */
int spark_range(const board_spark *s, int *lo, int *hi);

/* A number in the Worker's LOCALE text (numfmt): an optional sign, the whole
 * part with a thousands mark every three digits (or none), an optional decimal mark and 1 to 4
 * decimals, an optional '%' ("1.540,00", "589", "+0,3%", "-3,20"). At most
 * 12 digits. v: the value times 10^dec. plus and neg: the sign written. */
typedef struct {
    long long v;
    int dec, pct, plus, neg;
} board_num;
/* The Worker's separators (the numfmt line, since 0.4.0): th the thousands
 * mark (0 for none), dc the decimal mark; es-AR's '.' and ',' until set. */
void num_style(char th, char dc);
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

/* The sky gradient's two colours, 0xRRGGBB: the stops at the
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

/* A WMO weather code as a kind, WK_NONE for a missing code
 * (below 0); a code with no row is WK_CLOUD. */
enum { WK_NONE = -1, WK_CLEAR, WK_PARTLY, WK_CLOUD, WK_FOG, WK_RAIN, WK_SNOW, WK_STORM };
int wmo_kind(int code);
/* 1 for rain or a storm (the sky darkens, the hero shows streaks). */
int wmo_wet(int code);
/* The rain or the snow of the effects from the WMO code now and
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

/* The Economía page's reply (0.6.0, GET /economia): "couch-economia 1",
 * now, an ind line per indicator (key|label|section|unit|value|date|prev|
 * prev date|source: the numbers in the C form, "-" for no previous
 * reading), hist=key|v1,v2,... (the last months of one indicator, oldest
 * first), econ_missing (the sources that gave nothing), econ_age,
 * econ_state, end. At most ECON_MAX. */
#define ECON_MAX 8192
#define ECON_N 24
#define ECON_HIST 13
typedef struct {
    char key[24];
    char label[124];                 /* 40 code points of 3 bytes and the NUL */
    char section[32], unit[8];
    double value, prev;
    int has_prev;
    char date[12], prev_date[12];
    char source[24];
} econ_ind;
typedef struct {
    int version;
    long now;
    int n, more;                     /* more: ind lines past ECON_N */
    econ_ind ind[ECON_N];
    int n_hist;
    double hist[ECON_HIST];
    char hist_key[24];
    char missing[160];               /* econ_missing's text, "" for none */
    board_feed feed;                 /* econ_age and econ_state */
} econ_data;
/* The body into *d, with the return values of news_parse (line 1
 * "couch-economia 1"). fold as board_parse's. An ind line with fewer than
 * nine fields or a value that is not a number is skipped. */
int econ_parse(const char *body, int n, int fold, econ_data *d);

/* 1 when the body holds its end line with the line end ("\nend\n" or
 * "\nend\r\n"): the thread stops reading there, without the server's close. */
int board_has_end(const char *body, int n);

/* A text field from n bytes of src into dst (size bytes with the NUL): the
 * bytes below 0x20 and 0x7f dropped, the spaces at both ends trimmed, cut on
 * a code point boundary. With fold, the UTF-8 becomes ASCII: a Latin-1 letter
 * its base letter (U+00E1 a, U+00F1 n), the degree sign nothing (each
 * temperature has one), a no-break space a space, an en or em dash a '-'
 * (0.5.0: the futbol line's "Lions-Bears"), any other code point and a
 * byte that starts no valid sequence '?'. */
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

/* night.txt (0.5.0): "off" or "HH-HH", the hours of night mode, from
 * the start to the end ("22-07": 22:00 to 07:00, past midnight). 1 and the
 * hours (0 to 23) for a span, 0 for "off" (case not counted), -1 for
 * anything else; a start equal to the end is no span (-1). Spaces at the
 * ends are allowed. */
int night_parse(const char *s, int n, int *start, int *end);
/* 1 when hour (0 to 23) is in the span: from start, up to but not with end. */
int night_in(int start, int end, int hour);

/* The internet as one fetch shows it: NQ_DOWN when the fetch got no
 * answer (answered 0: DNS, the connect, the send or the recv failed),
 * NQ_SLOW when its connect (the DNS lookup and the TCP handshake, http.h's
 * connect_us) took NQ_SLOW_US or more, else NQ_OK. Since 0.5.0's review
 * the connect, not the whole fetch: the answer's time holds the Worker's
 * own waits for its upstreams (up to 4 s), which say nothing of this
 * line. The handshake is one round trip to Cloudflare's edge; 1.5 s leaves
 * room for a DNS lookup that is not cached. */
enum { NQ_NONE, NQ_OK, NQ_SLOW, NQ_DOWN };
#define NQ_SLOW_US 1500000LL
int net_quality(int answered, long long connect_us);

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

/* The waiting screen's block (the TV's burn-in guard): its place for the
 * k-th span of AMB_MOVE_S (page.c), at most AMBIENT_DRIFT_X and
 * AMBIENT_DRIFT_Y px from the centre, whole pixels; span 0 is the centre.
 * Since 0.4.1 the block moves only while it is faded out (0.3.2 drifted a
 * whole pixel every few seconds, which looked like a fault). */
#define AMBIENT_DRIFT_X 40
#define AMBIENT_DRIFT_Y 24
void ambient_spot(long k, float *x, float *y);

/* The ticker's offset in px at t seconds for a speed of px_s px a second,
 * wrapped to [0, period): any fraction (the marquee draws it bilinear). */
double ticker_offset(double t, double px_s, double period);

/* ---- the reader (0.3.1, stage 11): the Worker's /news and /article -------------- */

/* GET /news (worker/README.md, "The news reader"): "couch-news 1",
 * now= (not kept), item=id|source|category|age|title (0 to 30, the newest first; a
 * 6th field, the summary of an older Worker, is skipped), news_age=,
 * news_state=, end. At most NEWS_MAX bytes (the Worker's
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
} news_item;
typedef struct {
    int version;
    int n;
    news_item item[NEWS_ITEMS];
    board_feed feed;                 /* news_age and news_state ("" when absent) */
} news_data;
/* The body (n bytes) into *d: BP_OK with line 1 "couch-news 1" and the end
 * line, BP_VERSION, BP_CUT (no end line: the items so far are in *d),
 * BP_NOT_BOARD (line 1 is not a news reply). A line with no title is not
 * an item. fold as board_parse's. */
int news_parse(const char *body, int n, int fold, news_data *d);

/* GET /article?id=<id>: "couch-article 1", now=, id= (neither kept: the
 * reader matches a reply by its request's id), source=, category=, title=,
 * date=words|age, link=, state=, p= (0 or more), end. At most ARTICLE_MAX
 * bytes; the p= lines with "p=" and LF at most 12288 bytes. */
#define ARTICLE_MAX 16384
#define ART_TEXT 12288               /* the paragraphs' bytes, each with its NUL */
#define ART_PARAS 400                /* p= lines kept, at most (a 12 KB text of 30-byte lines) */
#define ART_LINK 1024                /* the link: at most 1024 bytes with "link=" */
typedef struct {
    int version;
    char source[32], category[20];
    char title[904];                 /* 300 code points of 3 bytes and the NUL */
    char date[48];                   /* "Wed 14 Oct 12:12", "" for "-" */
    long age;                        /* seconds, -1 for "-" */
    char link[ART_LINK];             /* never folded: the QR code carries its bytes; "" when none */
    char state[8];                   /* "ok", "cut", "summary" or "none"; another word is none */
    char reason[96];                 /* the reason after '|' ("bbc.co.uk: HTTP 403") */
    int n_para;
    int para_off[ART_PARAS];         /* each paragraph's start in text, NUL-ended */
    int text_cut;                    /* 1 when a p= line did not fit in text (or past ART_PARAS) */
    int has_photo;                   /* photo=1 (0.6.0): the Worker has a picture for it, /image?id= gives it */
    char text[ART_TEXT];
} article_data;
/* The body into *a, with the return values of news_parse (line 1
 * "couch-article 1"). Empty p= lines are skipped. */
int article_parse(const char *body, int n, int fold, article_data *a);

/* ---- the futbol page (0.5.0): the private Worker's /futbol ---------------------- */

/* GET /futbol?d=0 (today) or d=1 (tomorrow): "couch-futbol 1", now= (not
 * kept), day=words ("Today, Fri 16 Oct"), league=name|country, then each game of
 * that league as game=when|home|away|score|score|tv|kind, more=leagues|games
 * (what the Worker's cap left out), futbol_age=, futbol_state=, end. when:
 * "21:00", "67'", "Final", "Postponed"; a score "-" before the game; kind
 * pre, live, end or off (another word is off). At most FUTBOL_MAX bytes
 * (the Worker's FUTBOL_BYTES). A game before any league line is not kept;
 * the games and leagues past the arrays count in more (a league that
 * comes when the games' array is full too). */
#define FUTBOL_MAX 16384             /* 12288 before 0.5.0's review; a Saturday of 87 games made 5.8 KB */
#define FUT_LEAGUES 40
#define FUT_GAMES 240
enum { FK_PRE, FK_LIVE, FK_END, FK_OFF };
typedef struct {
    int kind;                        /* FK_* */
    char when[24];
    char home[88], away[88];         /* 28 code points of 3 bytes and the NUL */
    int sh, sa;                      /* the scores, 0 to 99, -1 for "-" */
    char tv[124];                    /* 40 code points; "" for "-" */
} fut_game;
typedef struct {
    char name[148];                  /* 48 code points of 3 bytes and the NUL */
    char country[100];               /* 32 code points; "" for "-" */
    int first, n;                    /* its games in game[] */
} fut_league;
typedef struct {
    int version;
    char day[48];
    int n_league, n_game;
    fut_league league[FUT_LEAGUES];
    int more_leagues, more_games;    /* left out, 0 when none */
    board_feed feed;                 /* futbol_age and futbol_state ("" when absent) */
    fut_game game[FUT_GAMES];        /* last (0.5.1): a copy of a reply ends after its n_game games (futbol_take) */
} futbol_data;
/* The body into *d, with the return values of news_parse (line 1
 * "couch-futbol 1"). fold as board_parse's. */
int futbol_parse(const char *body, int n, int fold, futbol_data *d);
/* The live games of *d. */
int futbol_live(const futbol_data *d);

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
