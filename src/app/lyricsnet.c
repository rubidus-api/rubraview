/* RV-323 (R369, R370, D-411): a song's words from LRCLIB, kept beside it as a .lrc; a search typed and a record
   picked from a list; the .lrc edited in Notepad and read again when it is saved; the words uploaded to LRCLIB
   (no account: a proof of work for each upload). One request at a time, on a worker; the window polls it. */
#include "app.h"
#include "rubraview/lyricsearch.h"
#include "rubraview/pal/pal_http.h"
#include "rubraview/pal/pal_process.h"
#include <windows.h>

enum { JOB_FIND, JOB_SEARCH, JOB_UPLOAD };
#define SONG_PATH 2048
#define HITS_MAX 20
#define WORDS_MAX (1u << 20)

typedef struct job {
    int kind;
    char song[SONG_PATH];                       /* the song it is for */
    rubraview_lyrics_query_t q[RUBRAVIEW_LYRICS_MAX_QUERIES];
    size_t nq;
    char typed[RUBRAVIEW_LYRICS_FIELD];         /* JOB_SEARCH: the words typed (empty: the first query) */
    uint8_t *body;                              /* JOB_UPLOAD: what is sent */
    size_t body_size;
    volatile LONG done, stop;
    HANDLE thread;
    /* what came back */
    bool answered;                              /* the service answered at all */
    int status;
    char why[160];
    uint8_t *json;                              /* JOB_SEARCH: the records; JOB_FIND: the one taken */
    size_t json_size;
    double score;
} job_t;

static job_t *g_job;
static char g_song[SONG_PATH];                  /* the song on screen */
static bool g_song_words;                       /* it has words of its own */
static char g_pending[SONG_PATH];               /* a song to look for once the worker is free */
static uint64_t g_tried[256];                   /* the songs looked for this run, by their paths' hashes */
static size_t g_tried_n;
/* the last search's records, for the list picked from */
static rubraview_lrclib_hit_t g_hits[HITS_MAX];
static size_t g_hits_n;
static uint8_t *g_hits_mem;
static char g_hits_song[SONG_PATH];
/* the .lrc open in Notepad, read again when it changes */
static char g_watch[SONG_PATH];
static int64_t g_watch_mtime;
static uint64_t g_watch_size;
static double g_watch_next;
static double g_upload_armed;

static proven_u8str_view_t view_of(const char *s) { return proven_u8str_view_from_cstr(s); }

static uint64_t hash_of(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; s[i]; ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ull; }
    return h;
}

static bool tried(const char *song) {
    uint64_t h = hash_of(song);
    for (size_t i = 0; i < g_tried_n && i < 256; ++i) if (g_tried[i] == h) return true;
    return false;
}

static void mark_tried(const char *song) {
    g_tried[g_tried_n % 256] = hash_of(song);
    g_tried_n++;
}

static bool online(app_state_t *app) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("lyrics_online")) > 0.5;
}

static void copy_text(char *dst, size_t cap, proven_u8str_view_t s) {
    size_t n = s.size < cap - 1 ? s.size : cap - 1;
    if (n) proven_sys_mem_copy(dst, s.ptr, n);
    dst[n] = 0;
}

/* "<song without its ending>.lrc" */
static bool lrc_path(const char *song, char *out, size_t cap) {
    proven_u8str_view_t s = view_of(song), ext = rubraview_path_ext(rubraview_path_basename(s));
    size_t stem = s.size - ext.size;
    if (stem == 0 || stem + 5 > cap) return false;
    proven_sys_mem_copy(out, song, stem);
    proven_sys_mem_copy(out + stem, ".lrc", 5);
    return true;
}

/* ---- the worker ---- */

static bool ask(job_t *j, const char *method, const char *url, const char *type, const uint8_t *body, size_t size,
                const char *header, rubraview_http_reply_t *r) {
    bool ok = rubraview_pal_http(method, url, type, body, size, header, 15000, 4u << 20, r);
    if (ok) j->answered = true;
    else rubraview_fmt(j->why, sizeof j->why, "{}", PROVEN_ARG(r->why));
    return ok;
}

static void keep_json(job_t *j, rubraview_http_reply_t *r) {
    j->json = r->body;
    j->json_size = r->size;
    r->body = NULL;
}

static DWORD WINAPI job_main(LPVOID arg) {
    job_t *j = (job_t *)arg;
    char url[2048];
    rubraview_http_reply_t r;
    if (j->kind == JOB_FIND) {
        /* the exact record first, for each guess; then a search by the first, taken when it fits well */
        for (size_t i = 0; i < j->nq && !j->stop && !j->json; ++i) {
            if (!j->q[i].artist[0] || !rubraview_lrclib_get_url(&j->q[i], url, sizeof url)) continue;
            if (!ask(j, "GET", url, NULL, NULL, 0, NULL, &r)) break;
            if (r.status == 200) {
                uint8_t mem[64u << 10];
                proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = sizeof mem });
                rubraview_lrclib_hit_t h;
                if (r.size < sizeof mem / 2 && rubraview_lrclib_parse(&a, (proven_u8str_view_t){ (const proven_byte_t *)r.body, r.size }, &h, 1) == 1 &&
                    rubraview_lyrics_score(&j->q[i], &h) >= 0.5) { j->score = 1.0; keep_json(j, &r); }
                else if (r.size >= sizeof mem / 2) { j->score = 1.0; keep_json(j, &r); }   /* long words: read on the window's side */
            }
            rubraview_pal_http_free(&r);
        }
        if (!j->json && !j->stop && j->nq > 0 && rubraview_lrclib_search_url(&j->q[0], PROVEN_LIT(""), url, sizeof url) &&
            ask(j, "GET", url, NULL, NULL, 0, NULL, &r)) {
            if (r.status == 200) keep_json(j, &r);
            j->score = 0.0;   /* chosen on the window's side, by the score */
            rubraview_pal_http_free(&r);
        }
    } else if (j->kind == JOB_SEARCH) {
        size_t n = rubraview_lrclib_search_url(j->nq ? &j->q[0] : NULL, view_of(j->typed), url, sizeof url);
        if (n && ask(j, "GET", url, NULL, NULL, 0, NULL, &r)) {
            j->status = r.status;
            if (r.status == 200) keep_json(j, &r);
            rubraview_pal_http_free(&r);
        }
    } else if (j->kind == JOB_UPLOAD) {
        if (ask(j, "POST", "https://lrclib.net/api/request-challenge", NULL, NULL, 0, NULL, &r)) {
            char prefix[128];
            uint8_t target[32];
            bool have = r.status == 200 && rubraview_lrclib_challenge((proven_u8str_view_t){ (const proven_byte_t *)r.body, r.size }, prefix, target);
            rubraview_pal_http_free(&r);
            uint64_t nonce = 0, from = 0;
            bool solved = false;
            while (have && !solved && !j->stop && from < 4000000000ull) {   /* a few million tries: seconds to a minute */
                solved = rubraview_lrclib_solve(prefix, target, from, 200000, &nonce);
                from += 200000;
            }
            if (!have) rubraview_fmt(j->why, sizeof j->why, "{}", PROVEN_ARG("no challenge came"));
            else if (solved) {
                char token[192];
                rubraview_fmt(token, sizeof token, "X-Publish-Token: {}:{}", PROVEN_ARG(prefix), PROVEN_ARG(nonce));
                if (ask(j, "POST", "https://lrclib.net/api/publish", "application/json", j->body, j->body_size, token, &r)) {
                    j->status = r.status;
                    if (r.status != 201 && r.size) copy_text(j->why, sizeof j->why, (proven_u8str_view_t){ (const proven_byte_t *)r.body, r.size });
                    rubraview_pal_http_free(&r);
                }
            }
        }
    }
    InterlockedExchange(&j->done, 1);
    return 0;
}

static void job_free(job_t *j) {
    if (!j) return;
    if (j->thread) { WaitForSingleObject(j->thread, INFINITE); CloseHandle(j->thread); }
    rubraview_free(j->json);
    rubraview_free(j->body);
    rubraview_free(j);
}

static bool job_start(job_t *j) {
    j->thread = CreateThread(NULL, 0, job_main, j, 0, NULL);
    if (!j->thread) { job_free(j); return false; }
    g_job = j;
    return true;
}

static job_t *job_new(app_state_t *app, int kind, const char *song) {
    if (g_job) return NULL;
    job_t *j = (job_t *)rubraview_alloc_zero(1, sizeof *j);
    if (!j) return NULL;
    j->kind = kind;
    copy_text(j->song, sizeof j->song, view_of(song));
    if (!proven_u8str_view_eq(view_of(song), view_of(g_song))) return j;
    j->nq = rubraview_lyrics_queries(&app->music_tags, view_of(song), j->q, RUBRAVIEW_LYRICS_MAX_QUERIES);
    double length = app->media_info.duration_seconds > 0.0 ? app->media_info.duration_seconds : app->music_tags.duration_seconds;
    for (size_t i = 0; i < j->nq; ++i) if (length > 0.0) j->q[i].seconds = length;
    return j;
}

/* ---- the window's side ---- */

/* The words kept beside the song (with its names on top, as LRC files carry them); shown when it is the one on
   screen. `quiet`: said only when kept. */
static void keep_words(app_state_t *app, const char *song, const rubraview_lrclib_hit_t *h, bool replace) {
    proven_u8str_view_t words = rubraview_lyrics_words(h);
    if (words.size == 0) return;
    size_t cap = words.size + 3 * RUBRAVIEW_LYRICS_FIELD + 64;
    char *text = (char *)rubraview_alloc(cap);
    if (!text) return;
    int n = rubraview_fmt(text, cap, "\xEF\xBB\xBF[ar:{}]\r\n[ti:{}]\r\n[al:{}]\r\n[re:LRCLIB]\r\n",
                          PROVEN_ARG(rubraview_bytes_view(h->artist.ptr, (int)h->artist.size)),
                          PROVEN_ARG(rubraview_bytes_view(h->track.ptr, (int)h->track.size)),
                          PROVEN_ARG(rubraview_bytes_view(h->album.ptr, (int)h->album.size)));
    size_t at = n > 0 ? (size_t)n : 0;
    for (size_t i = 0; i < words.size && at + 2 < cap; ++i) {   /* Windows' line ends, for Notepad */
        if (words.ptr[i] == '\n' && (i == 0 || words.ptr[i - 1] != '\r')) text[at++] = '\r';
        text[at++] = (char)words.ptr[i];
    }
    proven_u8str_view_t body = { (const proven_byte_t *)text, at };
    char lrc[SONG_PATH + 8];
    rubraview_fs_entry_t st;
    bool there = lrc_path(song, lrc, sizeof lrc) && rubraview_pal_fs_stat(app->arena, view_of(lrc), &st);
    bool kept = lrc_path(song, lrc, sizeof lrc) && (replace || !there) && rubraview_pal_fs_write_file_atomic(view_of(lrc), body);
    if (proven_u8str_view_eq(view_of(song), view_of(g_song))) {
        music_lyrics_take(app, body);
        g_song_words = true;
    }
    char line[256];
    int k = kept ? rubraview_fmt(line, sizeof line, T("lyrics from LRCLIB, kept beside the song: {}"), PROVEN_ARG(rubraview_path_basename(view_of(lrc))))
                 : rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("lyrics from LRCLIB — this folder cannot be written, so they are shown, not kept")));
    if (k > 0) osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)k });
    rubraview_free(text);
}

static bool hits_read(job_t *j) {
    rubraview_free(g_hits_mem);
    g_hits_mem = NULL;
    g_hits_n = 0;
    if (!j->json) return false;
    size_t room = j->json_size * 2 + (64u << 10);
    g_hits_mem = (uint8_t *)rubraview_alloc(room);
    if (!g_hits_mem) return false;
    proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = g_hits_mem, .size = room });
    g_hits_n = rubraview_lrclib_parse(&a, (proven_u8str_view_t){ (const proven_byte_t *)j->json, j->json_size }, g_hits, HITS_MAX);
    copy_text(g_hits_song, sizeof g_hits_song, view_of(j->song));
    return g_hits_n > 0;
}

static void say_failed(app_state_t *app, const job_t *j) {
    char line[256];
    int k = rubraview_fmt(line, sizeof line, T("LRCLIB did not answer: {}"), PROVEN_ARG(j->why[0] ? j->why : "?"));
    if (k > 0) osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)k });
}

static void show_list(app_state_t *app) {
    static char rows[HITS_MAX][96];
    const char *ptrs[HITS_MAX];
    size_t n = 0;
    for (size_t i = 0; i < g_hits_n; ++i) {
        const rubraview_lrclib_hit_t *h = &g_hits[i];
        if (h->plain.size == 0 && h->synced.size == 0) continue;
        int64_t s = (int64_t)(h->seconds + 0.5);
        rubraview_fmt(rows[n], sizeof rows[n], "{}{} \xE2\x80\x94 {}  {}:{:02}", PROVEN_ARG(h->synced.size ? "\xE2\x99\xAA " : ""),
                      PROVEN_ARG(rubraview_bytes_view(h->track.ptr, (int)h->track.size)),
                      PROVEN_ARG(rubraview_bytes_view(h->artist.ptr, (int)h->artist.size)), PROVEN_ARG(s / 60), PROVEN_ARG(s % 60));
        ptrs[n] = rows[n];
        if (i != n) g_hits[n] = g_hits[i];
        n++;
    }
    g_hits_n = n;
    if (n == 0) { osd_say(app, TU8("no lyrics found at LRCLIB for that")); return; }
    chooser_open_lyrics(app, ptrs, n);
}

static void job_finished(app_state_t *app, job_t *j) {
    if (j->kind == JOB_FIND) {
        if (!j->answered) return;   /* no network: quiet, as it was asked for no one */
        if (!hits_read(j)) return;
        rubraview_lyrics_query_t *q = j->nq ? &j->q[0] : NULL;
        double score = 0.0;
        int32_t best = q ? rubraview_lyrics_best(q, g_hits, g_hits_n, &score) : -1;
        if (best >= 0 && (j->score >= 1.0 || score >= RUBRAVIEW_LYRICS_SURE)) keep_words(app, j->song, &g_hits[best], false);
    } else if (j->kind == JOB_SEARCH) {
        if (!j->answered) { say_failed(app, j); return; }
        hits_read(j);
        if (proven_u8str_view_eq(view_of(j->song), view_of(g_song))) show_list(app);
    } else if (j->kind == JOB_UPLOAD) {
        if (j->status == 201) osd_say(app, TU8("the lyrics are on LRCLIB now"));
        else if (j->status > 0) {
            char line[256];
            int k = rubraview_fmt(line, sizeof line, T("LRCLIB refused the upload ({}): {}"), PROVEN_ARG(j->status), PROVEN_ARG(j->why));
            if (k > 0) osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)k });
        } else say_failed(app, j);
    }
}

static void find_start(app_state_t *app, const char *song) {
    job_t *j = job_new(app, JOB_FIND, song);
    if (!j) return;
    mark_tried(song);
    if (j->nq == 0) { job_free(j); return; }
    job_start(j);
}

void lyricsnet_song(app_state_t *app, proven_u8str_view_t path, bool has_words) {
    copy_text(g_song, sizeof g_song, path);
    g_song_words = has_words;
    if (has_words || !online(app) || path.size == 0 || path.size >= SONG_PATH || tried(g_song)) return;
    rubraview_fs_entry_t st;   /* a file of the disk, not a page in an archive or a station */
    if (!rubraview_pal_fs_stat(app->arena, path, &st) || st.is_directory) return;
    if (g_job) { copy_text(g_pending, sizeof g_pending, path); return; }
    find_start(app, g_song);
}

void lyricsnet_poll(app_state_t *app) {
    if (g_job && InterlockedCompareExchange(&g_job->done, 0, 0)) {
        job_t *j = g_job;
        g_job = NULL;
        job_finished(app, j);
        job_free(j);
    }
    if (!g_job && g_pending[0]) {
        char song[SONG_PATH];
        copy_text(song, sizeof song, view_of(g_pending));
        g_pending[0] = 0;
        if (proven_u8str_view_eq(view_of(song), view_of(g_song)) && !g_song_words && online(app) && !tried(song)) find_start(app, song);
    }
    double now = rubraview_pal_time_now_seconds();
    if (g_watch[0] && now >= g_watch_next) {   /* the .lrc in Notepad: read again when saved */
        g_watch_next = now + 1.0;
        rubraview_fs_entry_t st;
        if (rubraview_pal_fs_stat(app->arena, view_of(g_watch), &st) && (st.mtime != g_watch_mtime || st.size_bytes != g_watch_size)) {
            g_watch_mtime = st.mtime;
            g_watch_size = st.size_bytes;
            char lrc[SONG_PATH + 8];
            if (lrc_path(g_song, lrc, sizeof lrc) && proven_u8str_view_eq(view_of(lrc), view_of(g_watch))) {
                uint8_t *mem = (uint8_t *)rubraview_alloc(WORDS_MAX + 4096);
                if (mem) {
                    proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = WORDS_MAX + 4096 });
                    proven_u8str_view_t text = rubraview_pal_fs_read_file(&a, view_of(g_watch), WORDS_MAX);
                    music_lyrics_take(app, text);
                    g_song_words = text.size > 0;
                    osd_say(app, TU8("the lyrics read again"));
                    rubraview_free(mem);
                }
            }
        }
    }
}

/* The menu's "Find lyrics online...": the box to type in, the song's guess in it. */
void lyricsnet_find(app_state_t *app) {
    if (!g_song[0]) { osd_say(app, TU8("no song is playing")); return; }
    if (app->rename_active) rename_end(app);
    rename_field_begin(app);
    rubraview_lyrics_query_t q[RUBRAVIEW_LYRICS_MAX_QUERIES];
    size_t n = rubraview_lyrics_queries(&app->music_tags, view_of(g_song), q, RUBRAVIEW_LYRICS_MAX_QUERIES);
    char guess[2 * RUBRAVIEW_LYRICS_FIELD + 4] = { 0 };
    if (n) rubraview_fmt(guess, sizeof guess, "{}{}{}", PROVEN_ARG(q[0].artist), PROVEN_ARG(q[0].artist[0] ? " " : ""), PROVEN_ARG(q[0].title));
    rubraview_textedit_set(&app->rename_ec.te, view_of(guess));
    rubraview_textedit_select_all(&app->rename_ec.te);
    app->rename_active = true;
    app->rename_is_lyrics = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

void lyricsnet_typed(app_state_t *app) {
    char typed[RUBRAVIEW_LYRICS_FIELD];
    copy_text(typed, sizeof typed, rubraview_textedit_text(&app->rename_ec.te));
    rename_end(app);
    if (g_job) { osd_say(app, TU8("still asking LRCLIB: a moment")); return; }
    job_t *j = job_new(app, JOB_SEARCH, g_song);
    if (!j) return;
    proven_sys_mem_copy(j->typed, typed, sizeof typed);
    if (job_start(j)) osd_say(app, TU8("asking LRCLIB..."));
}

void lyricsnet_pick(app_state_t *app, size_t index) {
    if (index >= g_hits_n || !g_hits_song[0]) return;
    keep_words(app, g_hits_song, &g_hits[index], true);   /* chosen: it replaces what was beside the song */
}

/* The menu's "Edit lyrics": the .lrc beside the song in Notepad (made from the words shown when there is none). */
void lyricsnet_edit(app_state_t *app) {
    char lrc[SONG_PATH + 8];
    if (!g_song[0] || !lrc_path(g_song, lrc, sizeof lrc)) { osd_say(app, TU8("no song is playing")); return; }
    rubraview_fs_entry_t st;
    if (!rubraview_pal_fs_stat(app->arena, view_of(lrc), &st)) {
        size_t cap = 4096;
        for (size_t i = 0; i < app->music_lyrics.count; ++i) cap += app->music_lyrics.lines[i].text.size + 16;
        char *text = (char *)rubraview_alloc(cap);
        if (!text) return;
        int n = rubraview_fmt(text, cap, "\xEF\xBB\xBF[ar:{}]\r\n[ti:{}]\r\n",
                              PROVEN_ARG(rubraview_bytes_view(app->music_tags.artist.ptr, (int)app->music_tags.artist.size)),
                              PROVEN_ARG(rubraview_bytes_view(app->music_tags.title.ptr, (int)app->music_tags.title.size)));
        size_t at = n > 0 ? (size_t)n : 0;
        for (size_t i = 0; i < app->music_lyrics.count; ++i) {
            const rubraview_lyric_line_t *l = &app->music_lyrics.lines[i];
            int k = l->time_seconds >= 0.0 && !app->music_lyrics.untimed
                ? rubraview_fmt(text + at, cap - at, "[{:02}:{:05.2}]{}\r\n", PROVEN_ARG((int64_t)(l->time_seconds / 60.0)),
                                PROVEN_ARG(l->time_seconds - 60.0 * (double)(int64_t)(l->time_seconds / 60.0)), PROVEN_ARG(l->text))
                : rubraview_fmt(text + at, cap - at, "{}\r\n", PROVEN_ARG(l->text));
            if (k > 0) at += (size_t)k;
        }
        bool made = rubraview_pal_fs_write_file_atomic(view_of(lrc), (proven_u8str_view_t){ (const proven_byte_t *)text, at });
        rubraview_free(text);
        if (!made) { osd_say(app, TU8("this folder cannot be written: the lyrics cannot be kept beside the song")); return; }
    }
    char command[SONG_PATH + 40];
    int n = rubraview_fmt(command, sizeof command, "notepad.exe \"{}\"", PROVEN_ARG(lrc));
    for (int i = 0; i < n; ++i) if (command[i] == '/') command[i] = '\\';
    if (n <= 0 || !rubraview_pal_process_start((proven_u8str_view_t){ (const proven_byte_t *)command, (size_t)n })) {
        osd_say(app, TU8("Notepad would not start"));
        return;
    }
    copy_text(g_watch, sizeof g_watch, view_of(lrc));
    if (rubraview_pal_fs_stat(app->arena, view_of(lrc), &st)) { g_watch_mtime = st.mtime; g_watch_size = st.size_bytes; }
    g_watch_next = rubraview_pal_time_now_seconds() + 1.0;
    osd_say(app, TU8("the lyrics are open in Notepad: what is saved there shows here"));
}

/* The menu's "Upload lyrics to LRCLIB...": the .lrc beside the song, its names and length; asked twice, as it is
   public. */
void lyricsnet_upload(app_state_t *app) {
    char lrc[SONG_PATH + 8];
    if (!g_song[0] || !lrc_path(g_song, lrc, sizeof lrc)) { osd_say(app, TU8("no song is playing")); return; }
    if (g_job) { osd_say(app, TU8("still asking LRCLIB: a moment")); return; }
    uint8_t *mem = (uint8_t *)rubraview_alloc(4 * WORDS_MAX);
    if (!mem) return;
    proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = 4 * WORDS_MAX });
    proven_u8str_view_t text = rubraview_pal_fs_read_file(&a, view_of(lrc), WORDS_MAX);
    if (text.size >= 3 && text.ptr[0] == 0xEF && text.ptr[1] == 0xBB && text.ptr[2] == 0xBF) { text.ptr += 3; text.size -= 3; }
    proven_u8str_view_t plain = rubraview_lyrics_plain_of(&a, text);
    if (plain.size == 0) { rubraview_free(mem); osd_say(app, TU8("no lyrics beside this song to upload: find or edit them first")); return; }
    rubraview_lyrics_query_t song = { 0 };
    copy_text(song.title, sizeof song.title, app->music_tags.title);
    copy_text(song.artist, sizeof song.artist, app->music_tags.artist);
    copy_text(song.album, sizeof song.album, app->music_tags.album);
    if (!song.title[0] || !song.artist[0]) {   /* the file's name when the tags say nothing */
        rubraview_lyrics_query_t q[RUBRAVIEW_LYRICS_MAX_QUERIES];
        size_t n = rubraview_lyrics_queries(&app->music_tags, view_of(g_song), q, RUBRAVIEW_LYRICS_MAX_QUERIES);
        for (size_t i = 0; i < n && (!song.title[0] || !song.artist[0]); ++i)
            if (q[i].artist[0]) { proven_sys_mem_copy(song.title, q[i].title, sizeof song.title); proven_sys_mem_copy(song.artist, q[i].artist, sizeof song.artist); }
    }
    song.seconds = app->media_info.duration_seconds > 0.0 ? app->media_info.duration_seconds : app->music_tags.duration_seconds;
    proven_u8str_view_t body = rubraview_lrclib_publish_body(&a, &song, plain, rubraview_lyrics_has_times(text) ? text : PROVEN_LIT(""));
    if (body.size == 0) { rubraview_free(mem); osd_say(app, TU8("to upload, the song needs an artist, a title and a length")); return; }
    double now = rubraview_pal_time_now_seconds();
    if (now - g_upload_armed > 8.0) {
        g_upload_armed = now;
        char line[512];
        int k = rubraview_fmt(line, sizeof line, T("upload \"{} — {}\" to LRCLIB? Anyone will be able to read the words: choose Upload again to send them"),
                              PROVEN_ARG(song.artist), PROVEN_ARG(song.title));
        if (k > 0) osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)k });
        rubraview_free(mem);
        return;
    }
    g_upload_armed = 0.0;
    job_t *j = job_new(app, JOB_UPLOAD, g_song);
    if (!j) { rubraview_free(mem); return; }
    j->body = (uint8_t *)rubraview_alloc(body.size);
    if (!j->body) { job_free(j); rubraview_free(mem); return; }
    proven_sys_mem_copy(j->body, body.ptr, body.size);
    j->body_size = body.size;
    rubraview_free(mem);
    if (job_start(j)) osd_say(app, TU8("uploading to LRCLIB (it asks the computer for a short proof of work first)..."));
}
