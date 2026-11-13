#include "rubraview/core.h"
#include "rubraview/fileinfo.h"
#include "rubraview/lang.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static proven_u8str_view_t keep(proven_arena_t *arena, const char *text, size_t n) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(res.err)) return PROVEN_LIT("");
    if (n) proven_sys_mem_copy(res.value.ptr, text, n);
    res.value.ptr[n] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = n };
}

static void push(proven_arena_t *arena, rubraview_info_t *info, rubraview_info_line_t line) {
    if (!arena || !info) return;
    if (info->count == info->capacity) {
        size_t cap = info->capacity ? info->capacity * 2 : 32;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, cap, sizeof(rubraview_info_line_t));
        if (!proven_is_ok(res.err)) return;
        rubraview_info_line_t *grown = (rubraview_info_line_t*)(void*)res.value.ptr;
        if (info->count) proven_sys_mem_copy(grown, info->lines, info->count * sizeof(*grown));
        info->lines = grown;
        info->capacity = cap;
    }
    info->lines[info->count++] = line;
}

void rubraview_info_heading(proven_arena_t *arena, rubraview_info_t *info, const char *title) {
    push(arena, info, (rubraview_info_line_t){ .heading = true, .label = keep(arena, title, proven_cstr_len(title)) });
}

void rubraview_info_add(proven_arena_t *arena, rubraview_info_t *info, const char *label, proven_u8str_view_t value) {
    if (value.size == 0) return;
    push(arena, info, (rubraview_info_line_t){ .label = keep(arena, label, proven_cstr_len(label)),
                                               .value = keep(arena, (const char *)value.ptr, value.size) });
}

void rubraview_info_addf_args(proven_arena_t *arena, rubraview_info_t *info, const char *label, const char *format, const proven_arg_t *fmt_args, size_t fmt_count) {
    char buf[512];
    int n = rubraview_fmt_length(proven_fmt_to_internal(buf, sizeof(buf), format, fmt_args, fmt_count));
    if (n <= 0) return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    rubraview_info_add(arena, info, label, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = len });
}

static size_t done(char *buf, size_t cap, int n) {
    if (n < 0) { buf[0] = '\0'; return 0; }
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t rubraview_info_bytes(char *buf, size_t cap, uint64_t bytes) {
    char digits[32], grouped[48];
    int d = rubraview_fmt(digits, sizeof(digits), "{}", PROVEN_ARG((unsigned long long)bytes));
    size_t g = 0;
    for (int i = 0; i < d; ++i) {
        if (i > 0 && (d - i) % 3 == 0) grouped[g++] = ',';
        grouped[g++] = digits[i];
    }
    grouped[g] = '\0';
    if (bytes < 1024) return done(buf, cap, rubraview_fmt(buf, cap, T("{} bytes"), PROVEN_ARG(grouped)));
    const char *const UNIT[] = { T("KB"), T("MB"), T("GB"), T("TB") };
    double v = (double)bytes / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    return done(buf, cap, rubraview_fmt(buf, cap, T("{:.2} {} ({} bytes)"), PROVEN_ARG((double)v), PROVEN_ARG(UNIT[u]), PROVEN_ARG(grouped)));
}

size_t rubraview_info_exposure(char *buf, size_t cap, uint32_t num, uint32_t den) {
    if (den == 0) { buf[0] = '\0'; return 0; }
    double s = (double)num / (double)den;
    if (s >= 1.0 || num == 0) return done(buf, cap, rubraview_fmt(buf, cap, "{} s", PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(s), 6))));
    return done(buf, cap, rubraview_fmt(buf, cap, "1/{:.0} s", PROVEN_ARG((double)(1.0 / s))));
}

static size_t dms(char *buf, size_t cap, double v, char positive, char negative) {
    char hemisphere = v < 0 ? negative : positive;
    v = fabs(v);
    int deg = (int)v;
    double rest = (v - deg) * 60.0;
    int min = (int)rest;
    double sec = (rest - min) * 60.0;
    if (sec >= 59.95) { sec = 0.0; if (++min == 60) { min = 0; ++deg; } }
    return done(buf, cap, rubraview_fmt(buf, cap, "{}\xC2\xB0{}'{:.1}\"{}", PROVEN_ARG((int)deg), PROVEN_ARG((int)min), PROVEN_ARG((double)sec), PROVEN_ARG((char)hemisphere)));
}

size_t rubraview_info_gps(char *buf, size_t cap, double latitude, double longitude) {
    char a[48], b[48];
    dms(a, sizeof(a), latitude, 'N', 'S');
    dms(b, sizeof(b), longitude, 'E', 'W');
    return done(buf, cap, rubraview_fmt(buf, cap, "{} {}", PROVEN_ARG(a), PROVEN_ARG(b)));
}

size_t rubraview_info_duration(char *buf, size_t cap, double seconds) {
    if (!(seconds >= 0.0)) seconds = 0.0;
    long long ms = llround(seconds * 1000.0);
    long long h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, f = ms % 1000;
    if (h > 0) return done(buf, cap, rubraview_fmt(buf, cap, "{}:{:02}:{:02}.{:03}", PROVEN_ARG((long long)h), PROVEN_ARG((long long)m), PROVEN_ARG((long long)s), PROVEN_ARG((long long)f)));
    return done(buf, cap, rubraview_fmt(buf, cap, "{}:{:02}.{:03}", PROVEN_ARG((long long)m), PROVEN_ARG((long long)s), PROVEN_ARG((long long)f)));
}

size_t rubraview_info_fourcc(char *buf, size_t cap, uint32_t fourcc) {
    char c[4] = { (char)(fourcc & 0xFF), (char)((fourcc >> 8) & 0xFF), (char)((fourcc >> 16) & 0xFF), (char)(fourcc >> 24) };
    bool printable = true;
    for (int i = 0; i < 4; ++i) printable = printable && c[i] >= 0x20 && c[i] < 0x7F;
    if (!printable) return done(buf, cap, rubraview_fmt(buf, cap, "0x{:08X}", PROVEN_ARG((unsigned)fourcc)));
    int len = 4;
    while (len > 1 && c[len - 1] == ' ') --len;   /* 'VP9 ' reads "VP9" */
    return done(buf, cap, rubraview_fmt(buf, cap, "{}", PROVEN_ARG(rubraview_bytes_view(c, len))));
}

uint32_t rubraview_info_fourcc_or_name(uint32_t fourcc, const char *name) {
    bool printable = true;
    for (int i = 0; i < 4; ++i) {
        uint32_t c = (fourcc >> (8 * i)) & 0xFF;
        printable = printable && c >= 0x20 && c < 0x7F;
    }
    if (printable || !name || !name[0]) return fourcc;
    uint32_t made = 0;
    bool ended = false;
    for (int i = 0; i < 4; ++i) {
        char c = ended ? ' ' : name[i];
        if (c == '\0') { ended = true; c = ' '; }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 0x20 || c >= 0x7F) c = '?';
        made |= (uint32_t)(unsigned char)c << (8 * i);
    }
    return made;
}

static const char *orientation_words(int32_t o) {
    switch (o) {
        case 1: return T("1 (upright)");
        case 2: return T("2 (mirrored)");
        case 3: return T("3 (upside down)");
        case 4: return T("4 (flipped)");
        case 5: return T("5 (mirrored, turned a quarter to the left)");
        case 6: return T("6 (turned a quarter to the right)");
        case 7: return T("7 (mirrored, turned a quarter to the right)");
        case 8: return T("8 (turned a quarter to the left)");
        default: return NULL;
    }
}

static proven_u8str_view_t lit(const char *s) { return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = proven_cstr_len(s) }; }

/* The EXIF enumerations in words; NULL for "not given" and for values the
   standard does not name. */
static const char *pick(uint32_t v, const char *const *names, uint32_t count) {
    return v < count && names[v] ? rubraview_tr(names[v]) : NULL;
}
static const char *const PROGRAM[] = { NULL, N_("manual"), N_("normal program"), N_("aperture priority"), N_("shutter priority"),
                                       N_("creative (depth of field)"), N_("action (fast shutter)"), N_("portrait"), N_("landscape") };
static const char *const METERING[] = { NULL, N_("average"), N_("centre-weighted"), N_("spot"), N_("multi-spot"), N_("pattern"), N_("partial") };
static const char *const WHITE_BALANCE[] = { NULL, N_("auto"), N_("manual") };        /* stored + 1 */
static const char *const EXPOSURE_MODE[] = { NULL, N_("auto"), N_("manual"), N_("auto bracket") };   /* stored + 1 */
static const char *const SCENE[] = { NULL, N_("standard"), N_("landscape"), N_("portrait"), N_("night") };    /* stored + 1 */
/* RV-130 */
static const char *const LEVEL_SOFT[] = { NULL, N_("normal"), N_("soft"), N_("hard") };     /* sharpness, stored + 1 */
static const char *const LEVEL_LOW[] = { NULL, N_("normal"), N_("low"), N_("high") };       /* contrast, saturation, stored + 1 */
static const char *const GAIN[] = { NULL, N_("none"), N_("low gain up"), N_("high gain up"), N_("low gain down"), N_("high gain down") };
static const char *const DISTANCE[] = { NULL, N_("unknown"), N_("macro"), N_("close"), N_("distant") };
static const char *const RENDERED[] = { NULL, N_("normal"), N_("custom") };
static const char *const SENSING[] = { NULL, NULL, N_("one-chip colour area"), N_("two-chip colour area"), N_("three-chip colour area"),
                                       N_("colour sequential area"), NULL, N_("trilinear"), N_("colour sequential linear") };   /* as written */
static const char *const FILE_SOURCE[] = { NULL, N_("film scanner"), N_("print scanner"), N_("digital camera") };

static const char *light_words(uint16_t stored) {   /* stored + 1 */
    if (stored == 0) return NULL;
    switch (stored - 1) {
        case 0: return T("unknown");          case 1: return T("daylight");      case 2: return T("fluorescent");
        case 3: return T("tungsten");         case 4: return T("flash");         case 9: return T("fine weather");
        case 10: return T("cloudy");          case 11: return T("shade");
        case 12: case 13: case 14: case 15: case 16: return T("fluorescent");
        case 17: return T("standard light A"); case 18: return T("standard light B"); case 19: return T("standard light C");
        case 20: return "D55";                case 21: return "D65";             case 22: return "D75";   case 23: return "D50";
        case 24: return T("studio tungsten"); case 255: return T("other");
        default: return NULL;
    }
}

/* The flash's bits in words: fired or not, its mode, red-eye, the return light. */
static size_t flash_words(char *b, size_t cap, uint16_t f) {
    if (f & 0x20) return done(b, cap, rubraview_fmt(b, cap, "{}", PROVEN_ARG(T("no flash"))));
    const char *mode = ((f >> 3) & 3) == 1 ? T(", forced on") : ((f >> 3) & 3) == 2 ? T(", forced off") : ((f >> 3) & 3) == 3 ? T(", auto") : "";
    const char *ret = ((f >> 1) & 3) == 2 ? T(", no return light seen") : ((f >> 1) & 3) == 3 ? T(", return light seen") : "";
    return done(b, cap, rubraview_fmt(b, cap, "{}{}{}{}", PROVEN_ARG((f & 1) ? T("fired") : T("did not fire")), PROVEN_ARG(mode),
                                 PROVEN_ARG((f & 0x40) ? T(", red-eye reduction") : ""), PROVEN_ARG(ret)));
}

void rubraview_info_add_exif(proven_arena_t *arena, rubraview_info_t *info, const rubraview_exif_info_t *x) {
    if (!x || !x->found) return;
    rubraview_info_heading(arena, info, T("EXIF"));
    char b[160];
    /* "Canon EOS R5", once: many cameras put the maker in the model too */
    size_t ml = proven_cstr_len(x->make);
    if (ml > 0 && rubraview_cstr_cmp_n(x->model, rubraview_bytes_view(x->make, ml)) != 0) rubraview_info_addf(arena, info, T("Camera"), "{} {}", PROVEN_ARG(x->make), PROVEN_ARG(x->model));
    else rubraview_info_add(arena, info, T("Camera"), lit(x->model[0] ? x->model : x->make));
    if (x->lens_make[0] && x->lens[0] && rubraview_cstr_cmp_n(x->lens, rubraview_bytes_view(x->lens_make, proven_cstr_len(x->lens_make))) != 0)
        rubraview_info_addf(arena, info, T("Lens"), "{} {}", PROVEN_ARG(x->lens_make), PROVEN_ARG(x->lens));
    else rubraview_info_add(arena, info, T("Lens"), lit(x->lens));
    rubraview_info_add(arena, info, T("Serial number"), lit(x->serial));
    rubraview_info_add(arena, info, T("Description"), lit(x->description));
    if (x->date_taken[0] && x->offset_time[0]) rubraview_info_addf(arena, info, T("Taken"), "{} ({})", PROVEN_ARG(x->date_taken), PROVEN_ARG(x->offset_time));
    else rubraview_info_add(arena, info, T("Taken"), lit(x->date_taken));
    if (rubraview_cstr_cmp(x->date_digitized, x->date_taken) != 0) rubraview_info_add(arena, info, T("Digitized"), lit(x->date_digitized));
    rubraview_info_add(arena, info, T("Changed"), lit(x->date_modified));
    if (x->has_exposure) rubraview_info_add(arena, info, T("Exposure"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_exposure(b, sizeof(b), x->exposure_num, x->exposure_den) });
    if (x->has_fnumber) rubraview_info_addf(arena, info, T("Aperture"), "f/{:.1}", PROVEN_ARG((double)x->fnumber));
    if (x->iso) rubraview_info_addf(arena, info, T("ISO"), "{}", PROVEN_ARG((unsigned)x->iso));
    if (x->has_focal) {
        if (x->focal_35mm) rubraview_info_addf(arena, info, T("Focal length"), T("{:.0} mm ({} mm in 35 mm terms)"), PROVEN_ARG((double)x->focal_mm), PROVEN_ARG((unsigned)x->focal_35mm));
        else rubraview_info_addf(arena, info, T("Focal length"), T("{:.0} mm"), PROVEN_ARG((double)x->focal_mm));
    }
    if (x->has_bias) rubraview_info_addf(arena, info, T("Exposure bias"), T("{:+.2} EV"), PROVEN_ARG((double)x->exposure_bias));
    if (x->has_flash) rubraview_info_add(arena, info, T("Flash"), (proven_u8str_view_t){ (const proven_byte_t *)b, flash_words(b, sizeof(b), x->flash) });
    if (x->has_max_aperture) rubraview_info_addf(arena, info, T("Widest aperture"), "f/{:.1}", PROVEN_ARG((double)x->max_aperture));
    if (x->has_lens_spec) {
        double *s = (double*)x->lens_spec;
        int n = s[1] > s[0] + 0.01 ? rubraview_fmt(b, sizeof(b), "{:.0}-{:.0} mm", PROVEN_ARG((double)s[0]), PROVEN_ARG((double)s[1])) : rubraview_fmt(b, sizeof(b), "{:.0} mm", PROVEN_ARG((double)s[0]));
        if (n > 0 && (size_t)n < sizeof(b) && s[2] > 0) {
            if (s[3] > s[2] + 0.01) rubraview_fmt(b + n, sizeof(b) - (size_t)n, " f/{:.1}-{:.1}", PROVEN_ARG((double)s[2]), PROVEN_ARG((double)s[3]));
            else rubraview_fmt(b + n, sizeof(b) - (size_t)n, " f/{:.1}", PROVEN_ARG((double)s[2]));
        }
        rubraview_info_add(arena, info, T("Lens range"), lit(b));
    }
    if (x->has_subject_distance) {
        if (x->subject_distance_m < 0) rubraview_info_add(arena, info, T("Subject distance"), lit(T("infinity")));
        else if (x->subject_distance_m > 0) rubraview_info_addf(arena, info, T("Subject distance"), "{:.2} m", PROVEN_ARG((double)x->subject_distance_m));
    }
    if (x->has_brightness) rubraview_info_addf(arena, info, T("Brightness"), T("{:.2} EV"), PROVEN_ARG((double)x->brightness));
    const char *w;
    if ((w = pick(x->exposure_program, PROGRAM, 9)))      rubraview_info_add(arena, info, T("Program"), lit(w));
    if ((w = pick(x->exposure_mode, EXPOSURE_MODE, 4)))   rubraview_info_add(arena, info, T("Exposure mode"), lit(w));
    if ((w = pick(x->metering, METERING, 7)))             rubraview_info_add(arena, info, T("Metering"), lit(w));
    if ((w = pick(x->white_balance, WHITE_BALANCE, 3)))   rubraview_info_add(arena, info, T("White balance"), lit(w));
    if ((w = pick(x->scene_type, SCENE, 5)))              rubraview_info_add(arena, info, T("Scene"), lit(w));
    if ((w = light_words(x->light_source)))                rubraview_info_add(arena, info, T("Light source"), lit(w));
    if ((w = pick(x->contrast, LEVEL_LOW, 4)))            rubraview_info_add(arena, info, T("Contrast"), lit(w));
    if ((w = pick(x->saturation, LEVEL_LOW, 4)))          rubraview_info_add(arena, info, T("Saturation"), lit(w));
    if ((w = pick(x->sharpness, LEVEL_SOFT, 4)))          rubraview_info_add(arena, info, T("Sharpness"), lit(w));
    if ((w = pick(x->gain_control, GAIN, 6)))             rubraview_info_add(arena, info, T("Gain control"), lit(w));
    if ((w = pick(x->distance_range, DISTANCE, 5)))       rubraview_info_add(arena, info, T("Distance range"), lit(w));
    if ((w = pick(x->custom_rendered, RENDERED, 3)))      rubraview_info_add(arena, info, T("Processing"), lit(w));
    if ((w = pick(x->sensing_method, SENSING, 9)))        rubraview_info_add(arena, info, T("Sensor"), lit(w));
    if ((w = pick(x->file_source, FILE_SOURCE, 4)))       rubraview_info_add(arena, info, T("Source"), lit(w));
    if (x->has_digital_zoom && x->digital_zoom > 1.0)     rubraview_info_addf(arena, info, T("Digital zoom"), "{}x", PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(x->digital_zoom), 2)));
    if (x->color_space == 1) rubraview_info_add(arena, info, T("Colour space"), lit(T("sRGB")));
    else if (x->color_space == 0xFFFF) rubraview_info_add(arena, info, T("Colour space"), lit(T("uncalibrated (often Adobe RGB)")));
    const char *o = orientation_words(x->orientation);
    if (o) rubraview_info_add(arena, info, T("Orientation"), lit(o));
    if (x->has_resolution) {
        double yr = x->y_resolution > 0 ? x->y_resolution : x->x_resolution;
        const char *unit = x->resolution_unit == 3 ? T("pixels a cm") : x->resolution_unit == 1 ? T("(no unit)") : "dpi";
        if (fabs(yr - x->x_resolution) < 0.01) rubraview_info_addf(arena, info, T("Resolution (EXIF)"), "{:.0} {}", PROVEN_ARG((double)x->x_resolution), PROVEN_ARG(unit));
        else rubraview_info_addf(arena, info, T("Resolution (EXIF)"), "{:.0} x {:.0} {}", PROVEN_ARG((double)x->x_resolution), PROVEN_ARG((double)yr), PROVEN_ARG(unit));
    }
    if (x->pixel_x && x->pixel_y) rubraview_info_addf(arena, info, T("Pixels (EXIF)"), "{} x {}", PROVEN_ARG((unsigned)x->pixel_x), PROVEN_ARG((unsigned)x->pixel_y));
    if (x->has_gps) rubraview_info_add(arena, info, T("Location"), (proven_u8str_view_t){ (const proven_byte_t *)b, rubraview_info_gps(b, sizeof(b), x->latitude, x->longitude) });
    /* The same place in decimal degrees, which a map's search box takes as typed. */
    if (x->has_gps) rubraview_info_addf(arena, info, T("Coordinates"), "{:.6}, {:.6}", PROVEN_ARG((double)x->latitude), PROVEN_ARG((double)x->longitude));
    if (x->has_altitude) rubraview_info_addf(arena, info, T("Altitude"), "{:.0} m", PROVEN_ARG((double)x->altitude_m));
    if (x->has_direction) rubraview_info_addf(arena, info, T("Facing"), T("{:.0} degrees ({} north)"), PROVEN_ARG((double)x->direction),
                                              PROVEN_ARG(x->direction_ref == 'M' ? T("magnetic") : T("true")));
    if (x->has_speed) rubraview_info_addf(arena, info, T("Moving at"), "{:.1} {}", PROVEN_ARG((double)x->speed),
                                          PROVEN_ARG(x->speed_ref == 'M' ? T("mph") : x->speed_ref == 'N' ? T("knots") : T("km/h")));
    rubraview_info_add(arena, info, T("GPS time"), lit(x->gps_time));
    rubraview_info_add(arena, info, T("Map datum"), lit(x->gps_datum));
    rubraview_info_add(arena, info, T("Software"), lit(x->software));
    rubraview_info_add(arena, info, T("Artist"), lit(x->artist));
    rubraview_info_add(arena, info, T("Copyright"), lit(x->copyright));
    rubraview_info_add(arena, info, T("Owner"), lit(x->owner));
    rubraview_info_add(arena, info, T("Comment"), lit(x->user_comment));
    rubraview_info_add(arena, info, T("Picture ID"), lit(x->unique_id));
    if (x->exif_version[0]) rubraview_info_addf(arena, info, T("EXIF version"), "{}", PROVEN_ARG(x->exif_version));
}

void rubraview_info_add_iptc(proven_arena_t *arena, rubraview_info_t *info, const rubraview_iptc_info_t *x) {
    if (!x || !x->found) return;
    rubraview_info_heading(arena, info, T("IPTC"));
    rubraview_info_add(arena, info, T("Title"), lit(x->title));
    rubraview_info_add(arena, info, T("Headline"), lit(x->headline));
    rubraview_info_add(arena, info, T("Caption"), lit(x->caption));
    rubraview_info_add(arena, info, T("Keywords"), lit(x->keywords));
    rubraview_info_add(arena, info, T("Category"), lit(x->category));
    rubraview_info_add(arena, info, T("Created"), lit(x->date_created));
    if (x->byline[0] && x->byline_title[0]) rubraview_info_addf(arena, info, T("By"), "{} ({})", PROVEN_ARG(x->byline), PROVEN_ARG(x->byline_title));
    else rubraview_info_add(arena, info, T("By"), lit(x->byline));
    rubraview_info_add(arena, info, T("Caption writer"), lit(x->writer));
    rubraview_info_add(arena, info, T("Credit"), lit(x->credit));
    rubraview_info_add(arena, info, T("Source"), lit(x->source));
    rubraview_info_add(arena, info, T("Copyright"), lit(x->copyright));
    /* The place, smallest first: "Myeong-dong, Seoul, Seoul, South Korea (KOR)". */
    char b[320];
    size_t n = 0;
    const char *parts[] = { x->sublocation, x->city, x->state, x->country };
    for (size_t i = 0; i < 4; ++i) {
        if (!parts[i][0] || n >= sizeof(b)) continue;
        int k = rubraview_fmt(b + n, sizeof(b) - n, "{}{}", PROVEN_ARG(n ? ", " : ""), PROVEN_ARG(parts[i]));
        if (k > 0) n += (size_t)k < sizeof(b) - n ? (size_t)k : sizeof(b) - n - 1;
    }
    if (x->country_code[0] && n + 8 < sizeof(b)) {
        int k = rubraview_fmt(b + n, sizeof(b) - n, n ? " ({})" : "{}", PROVEN_ARG(x->country_code));
        if (k > 0) n += (size_t)k;
    }
    if (n) rubraview_info_add(arena, info, T("Place"), (proven_u8str_view_t){ (const proven_byte_t *)b, n < sizeof(b) ? n : sizeof(b) - 1 });
    rubraview_info_add(arena, info, T("Job"), lit(x->job_id));
    rubraview_info_add(arena, info, T("Instructions"), lit(x->instructions));
}

static int32_t gcd32(int32_t a, int32_t b) { while (b) { int32_t r = a % b; a = b; b = r; } return a; }

size_t rubraview_info_aspect(char *buf, size_t cap, int32_t w, int32_t h) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (w <= 0 || h <= 0) return 0;
    int32_t g = gcd32(w, h), a = w / g, c = h / g;
    double r = (double)w / (double)h;
    if (a <= 32 && c <= 32) return done(buf, cap, rubraview_fmt(buf, cap, "{}:{} ({:.3})", PROVEN_ARG((int)a), PROVEN_ARG((int)c), PROVEN_ARG((double)r)));
    /* Not a small ratio: the nearest common one within 1 %, if any. */
    static const int32_t COMMON[][2] = { {1,1}, {5,4}, {4,3}, {3,2}, {16,10}, {5,3}, {16,9}, {2,1}, {21,9}, {32,9}, {3,1} };
    for (size_t i = 0; i < sizeof(COMMON) / sizeof(COMMON[0]); ++i) {
        for (int flip = 0; flip < 2; ++flip) {
            double cr = flip ? (double)COMMON[i][1] / COMMON[i][0] : (double)COMMON[i][0] / COMMON[i][1];
            if (fabs(r - cr) / cr < 0.01) {
                return done(buf, cap, rubraview_fmt(buf, cap, T("{:.3} (about {}:{})"), PROVEN_ARG((double)r),
                                               PROVEN_ARG((int)COMMON[i][flip]), PROVEN_ARG((int)COMMON[i][!flip])));
            }
        }
    }
    return done(buf, cap, rubraview_fmt(buf, cap, "{:.3}:1", PROVEN_ARG((double)r)));
}

size_t rubraview_info_print_size(char *buf, size_t cap, int32_t w, int32_t h, double dpi_x, double dpi_y) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (w <= 0 || h <= 0 || !(dpi_x > 0)) return 0;
    if (!(dpi_y > 0)) dpi_y = dpi_x;
    double in_w = (double)w / dpi_x, in_h = (double)h / dpi_y;
    return done(buf, cap, rubraview_fmt(buf, cap, "{:.1} x {:.1} cm ({:.2} x {:.2} in)", PROVEN_ARG((double)(in_w * 2.54)), PROVEN_ARG((double)(in_h * 2.54)), PROVEN_ARG((double)in_w), PROVEN_ARG((double)in_h)));
}

uint32_t rubraview_info_unique_colours(proven_arena_t *arena, const rubraview_pixbuf_t *p) {
    if (!arena || !p || !p->pixels || p->width <= 0 || p->height <= 0 ||
        (p->format != RUBRAVIEW_PIXFMT_RGBA8 && p->format != RUBRAVIEW_PIXFMT_BGRA8)) return 0;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (1u << 24) / 8u);
    if (!proven_is_ok(res.err)) return 0;
    uint8_t *seen = (uint8_t*)res.value.ptr;
    proven_sys_mem_set(seen, 0, (1u << 24) / 8u);
    uint32_t count = 0;
    for (int32_t y = 0; y < p->height; ++y) {
        const uint8_t *row = p->pixels + (size_t)y * (size_t)p->stride;
        for (int32_t x = 0; x < p->width; ++x) {
            const uint8_t *q = row + (size_t)x * 4u;
            uint32_t c = ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];   /* the order does not change the count */
            uint8_t bit = (uint8_t)(1u << (c & 7u));
            if (!(seen[c >> 3] & bit)) { seen[c >> 3] |= bit; ++count; }
        }
    }
    return count;
}

size_t rubraview_info_rows(proven_arena_t *arena, const rubraview_info_t *info, int32_t label_cols,
                           proven_u8str_view_t *rows, size_t capacity) {
    if (!arena || !info || !rows) return 0;
    size_t n = 0;
    for (size_t i = 0; i < info->count && n < capacity; ++i) {
        const rubraview_info_line_t *l = &info->lines[i];
        if (l->heading) { rows[n++] = l->label; continue; }
        /* Padded by the columns the label takes on screen, not its bytes: a
           Korean label's characters are three bytes and two columns each. */
        size_t cols = rubraview_text_columns(l->label);
        size_t pad = label_cols > (int32_t)cols ? (size_t)label_cols - cols : 1;
        size_t len = 2 + l->label.size + pad + l->value.size;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
        if (!proven_is_ok(res.err)) break;
        char *p = (char*)res.value.ptr;
        proven_sys_mem_copy(p, "  ", 2);
        proven_sys_mem_copy(p + 2, l->label.ptr, l->label.size);
        proven_sys_mem_set(p + 2 + l->label.size, ' ', pad);
        proven_sys_mem_copy(p + 2 + l->label.size + pad, l->value.ptr, l->value.size);
        p[len] = '\0';
        rows[n++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)p, .size = len };
    }
    return n;
}

void rubraview_info_add_live(proven_arena_t *arena, rubraview_info_t *info, const rubraview_live_t *live) {
    if (!arena || !info || !live) return;
    rubraview_info_heading(arena, info, T("Live, once a second"));
    if (!(live->seconds > 0.0)) {
        rubraview_info_addf(arena, info, T("Frames drawn"), "{}", PROVEN_ARG(T("measuring")));
        return;
    }
    double span = live->seconds;
    rubraview_info_addf(arena, info, T("Frames drawn"), T("{:.0} a second, the slowest gap {:.0} ms"),
                        PROVEN_ARG((double)(live->frames / span)), PROVEN_ARG((double)(live->slowest * 1000.0)));
    if (live->frames > 0)
        rubraview_info_addf(arena, info, T("Drawing"), T("{:.1} ms a frame, waiting for the screen {:.1} ms"),
                            PROVEN_ARG((double)(live->draw * 1000.0 / live->frames)), PROVEN_ARG((double)(live->present * 1000.0 / live->frames)));
    if (live->film) {
        rubraview_info_addf(arena, info, T("Film pictures"), T("{:.0} shown, {:.0} dropped a second, {} decoded ahead"),
                            PROVEN_ARG((double)(live->shown / span)), PROVEN_ARG((double)(live->dropped / span)), PROVEN_ARG((unsigned)live->ready));
        if (live->resample > 0.0 && fabs(live->resample - 1.0) > 1e-6)
            rubraview_info_addf(arena, info, T("Matched to the screen"), T("{:.3}x for {} Hz (the sound too)"), PROVEN_ARG((double)live->resample), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(live->refresh_hz), 4)));
        else if (live->refresh_hz > 0.0 && live->frame_rate > 0.0)   /* looked at, and nothing to match (or more than 1 % off) */
            rubraview_info_addf(arena, info, T("Matched to the screen"), T("as it is: {:.3} frames a second on {} Hz"), PROVEN_ARG((double)live->frame_rate), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(live->refresh_hz), 4)));
        if (live->shown > 0)
            rubraview_info_addf(arena, info, T("Picture late"), T("{:.1} ms behind the clock on average"), PROVEN_ARG((double)(live->late * 1000.0 / live->shown)));
        if (live->taken > 0)
            rubraview_info_addf(arena, info, T("Taking a picture in"), T("{:.1} ms"), PROVEN_ARG((double)(live->taking * 1000.0 / live->taken)));
    }
    if (live->read_known) {
        double bytes = (double)live->read_bytes / span;
        if (live->film && live->film_bits > 0.0)
            rubraview_info_addf(arena, info, T("Reading files"), T("{:.1} MB/s ({:.0} Mbit/s); the film needs {:.0} Mbit/s"),
                                PROVEN_ARG((double)(bytes / 1e6)), PROVEN_ARG((double)(bytes * 8.0 / 1e6)), PROVEN_ARG((double)(live->film_bits / 1e6)));
        else
            rubraview_info_addf(arena, info, T("Reading files"), T("{:.1} MB/s ({:.0} Mbit/s)"), PROVEN_ARG((double)(bytes / 1e6)), PROVEN_ARG((double)(bytes * 8.0 / 1e6)));
    }
    if (live->cached)   /* RV-216 (D-249) */
        rubraview_info_addf(arena, info, T("Network film"), T("{:.1} MB/s from the copy, {:.1} MB/s from the network; the copy {:.0} % here"),
                            PROVEN_ARG((double)live->from_copy / span / 1e6), PROVEN_ARG((double)live->from_network / span / 1e6),
                            PROVEN_ARG((double)(live->copy_blocks ? 100.0 * (double)live->copy_filled / (double)live->copy_blocks : 0.0)));
    if (live->memory_room > 0)   /* RV-261 */
        rubraview_info_addf(arena, info, T("Film in memory"), T("{} of {} MB; {:.1} MB/s read from it"),
                            PROVEN_ARG((unsigned long long)(live->memory_held >> 20)), PROVEN_ARG((unsigned long long)(live->memory_room >> 20)),
                            PROVEN_ARG((double)live->from_memory / span / 1e6));
    rubraview_info_addf(arena, info, T("Timer"), "{}", PROVEN_ARG(live->fine_timer == 2 ? T("high-resolution waits")
                                                             : live->fine_timer == 1 ? T("1 ms") : T("Windows' default (15.6 ms)")));
    if (live->passes > 0)   /* RV-273 */
        rubraview_info_addf(arena, info, T("Wake-ups"), T("{:.0} a second, {:.0} of them waiting for the film's next frame"),
                            PROVEN_ARG((double)live->passes / span), PROVEN_ARG((double)live->frame_waits / span));
}
