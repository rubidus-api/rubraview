/* RV-314: what the choosing process is asked and answers, and its rules (include/rubraview/choose.h). */
#include "rubraview/choose.h"
#include "rubraview/glob.h"

void rubraview_choose_put_request(rubraview_jobwire_out_t *out, const rubraview_choose_request_t *req) {
    uint32_t nf = req->filter_count < RUBRAVIEW_CHOOSE_FILTERS ? req->filter_count : RUBRAVIEW_CHOOSE_FILTERS;
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_CHOOSE);
    rubraview_jobwire_put_u8(out, req->mode);
    rubraview_jobwire_put_u32(out, req->id);
    rubraview_jobwire_put_u64(out, req->reply_to);
    rubraview_jobwire_put_u64(out, req->over);
    rubraview_jobwire_put_str(out, req->title);
    rubraview_jobwire_put_str(out, req->start_dir);
    rubraview_jobwire_put_str(out, req->name);
    rubraview_jobwire_put_u32(out, nf);
    rubraview_jobwire_put_u32(out, req->filter);
    for (uint32_t i = 0; i < nf; ++i) {
        rubraview_jobwire_put_str(out, req->filter_name[i]);
        rubraview_jobwire_put_str(out, req->filter_pattern[i]);
    }
    (void)rubraview_jobwire_finish(out);
}

bool rubraview_choose_get_request(const uint8_t *data, size_t len, rubraview_choose_request_t *req) {
    rubraview_jobwire_msg_t msg;
    size_t used = 0;
    if (!req || rubraview_jobwire_take(data, len, &msg, &used) != 1 || msg.type != RUBRAVIEW_JOBWIRE_CHOOSE) return false;
    *req = (rubraview_choose_request_t){ 0 };
    req->mode = rubraview_jobwire_get_u8(&msg);
    req->id = rubraview_jobwire_get_u32(&msg);
    req->reply_to = rubraview_jobwire_get_u64(&msg);
    req->over = rubraview_jobwire_get_u64(&msg);
    req->title = rubraview_jobwire_get_str(&msg);
    req->start_dir = rubraview_jobwire_get_str(&msg);
    req->name = rubraview_jobwire_get_str(&msg);
    req->filter_count = rubraview_jobwire_get_u32(&msg);
    req->filter = rubraview_jobwire_get_u32(&msg);
    if (req->filter_count > RUBRAVIEW_CHOOSE_FILTERS) return false;
    for (uint32_t i = 0; i < req->filter_count; ++i) {
        req->filter_name[i] = rubraview_jobwire_get_str(&msg);
        req->filter_pattern[i] = rubraview_jobwire_get_str(&msg);
    }
    if (req->filter_count > 0 && req->filter >= req->filter_count) req->filter = 0;
    return rubraview_jobwire_done(&msg) && req->mode >= RUBRAVIEW_CHOOSE_FOLDER && req->mode <= RUBRAVIEW_CHOOSE_SAVE;
}

void rubraview_choose_put_answer(rubraview_jobwire_out_t *out, const rubraview_choose_answer_t *answer,
                                 const proven_u8str_view_t *paths, size_t count) {
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_CHOOSE_ANSWER);
    rubraview_jobwire_put_u32(out, answer->id);
    rubraview_jobwire_put_u8(out, answer->accepted ? 1u : 0u);
    rubraview_jobwire_put_u32(out, answer->filter);
    rubraview_jobwire_put_u32(out, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) rubraview_jobwire_put_str(out, paths[i]);
    (void)rubraview_jobwire_finish(out);
}

bool rubraview_choose_get_answer(const uint8_t *data, size_t len, rubraview_choose_answer_t *answer, rubraview_jobwire_msg_t *msg) {
    size_t used = 0;
    if (!answer || !msg || rubraview_jobwire_take(data, len, msg, &used) != 1 || msg->type != RUBRAVIEW_JOBWIRE_CHOOSE_ANSWER) return false;
    answer->id = rubraview_jobwire_get_u32(msg);
    answer->accepted = rubraview_jobwire_get_u8(msg) != 0;
    answer->filter = rubraview_jobwire_get_u32(msg);
    answer->count = rubraview_jobwire_get_u32(msg);
    return !msg->bad && answer->count <= msg->left / 4u;   /* each path takes its length at least */
}

bool rubraview_choose_hex(const uint8_t *data, size_t n, char *out, size_t cap) {
    static const char D[] = "0123456789abcdef";
    if (!out || cap == 0 || n > (cap - 1) / 2) return false;
    for (size_t i = 0; i < n; ++i) { out[2 * i] = D[data[i] >> 4]; out[2 * i + 1] = D[data[i] & 15]; }
    out[2 * n] = '\0';
    return true;
}

static int nibble(proven_byte_t c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool rubraview_choose_unhex(proven_u8str_view_t text, uint8_t *out, size_t cap, size_t *n) {
    if (text.size % 2 != 0 || text.size / 2 > cap) return false;
    for (size_t i = 0; i < text.size / 2; ++i) {
        int h = nibble(text.ptr[2 * i]), l = nibble(text.ptr[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)(h << 4 | l);
    }
    if (n) *n = text.size / 2;
    return true;
}

static bool every_file(proven_u8str_view_t pattern) {
    return pattern.size == 0 || proven_u8str_view_eq(pattern, PROVEN_LIT("*")) || proven_u8str_view_eq(pattern, PROVEN_LIT("*.*"));
}

bool rubraview_choose_shows(const rubraview_choose_request_t *req, uint32_t filter, proven_u8str_view_t name, bool is_directory) {
    if (is_directory) return true;
    if (!req || req->mode == RUBRAVIEW_CHOOSE_FOLDER) return false;
    if (req->filter_count == 0 || filter >= req->filter_count) return true;
    proven_u8str_view_t p = req->filter_pattern[filter];
    return every_file(p) || rubraview_glob_match_list(name, p);
}

size_t rubraview_choose_save_name(proven_u8str_view_t typed, proven_u8str_view_t pattern, char *out, size_t cap) {
    size_t a = 0, b = typed.size;
    while (a < b && (typed.ptr[a] == ' ' || typed.ptr[a] == '\t')) ++a;
    while (b > a && (typed.ptr[b - 1] == ' ' || typed.ptr[b - 1] == '\t' || typed.ptr[b - 1] == '.')) --b;   /* Windows drops them */
    if (a == b || !out || cap == 0) return 0;
    for (size_t i = a; i < b; ++i) {
        proven_byte_t c = typed.ptr[i];
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') return 0;
    }
    proven_u8str_view_t name = { .ptr = typed.ptr + a, .size = b - a };
    /* the kind's first "*.ext": added when the name matches none of the kind's */
    proven_u8str_view_t ext = { 0 };
    if (!every_file(pattern) && !rubraview_glob_match_list(name, pattern)) {
        size_t s = 0;
        while (s < pattern.size && pattern.ptr[s] == ' ') ++s;
        if (s + 2 < pattern.size && pattern.ptr[s] == '*' && pattern.ptr[s + 1] == '.') {
            size_t e = s + 1;
            while (e < pattern.size && pattern.ptr[e] != ';' && pattern.ptr[e] != '*' && pattern.ptr[e] != '?') ++e;
            if (e == pattern.size || pattern.ptr[e] == ';') ext = (proven_u8str_view_t){ .ptr = pattern.ptr + s + 1, .size = e - s - 1 };
        }
    }
    if (name.size + ext.size + 1 > cap) return 0;
    proven_sys_mem_copy(out, name.ptr, name.size);
    if (ext.size) proven_sys_mem_copy(out + name.size, ext.ptr, ext.size);
    out[name.size + ext.size] = '\0';
    return name.size + ext.size;
}
