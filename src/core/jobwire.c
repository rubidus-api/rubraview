/* RV-289: the file job's pipe, written and read (include/rubraview/jobwire.h). */
#include "rubraview/jobwire.h"

/* ---- writing ---- */

static bool grow(rubraview_jobwire_out_t *out, size_t more) {
    if (out->failed) return false;
    if (more > RUBRAVIEW_JOBWIRE_MAX || out->len > SIZE_MAX - more) { out->failed = true; return false; }
    size_t need = out->len + more;
    if (need <= out->cap) return true;
    size_t cap = out->cap ? out->cap : 256;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { out->failed = true; return false; }
        cap *= 2;
    }
    uint8_t *bigger = (uint8_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)out->data, .size = cap });
    if (!bigger) { out->failed = true; return false; }
    out->data = bigger;
    out->cap = cap;
    return true;
}

static void put_le(rubraview_jobwire_out_t *out, uint64_t v, size_t bytes) {
    if (!grow(out, bytes)) return;
    for (size_t i = 0; i < bytes; ++i) out->data[out->len++] = (uint8_t)(v >> (8 * i));
}

void rubraview_jobwire_begin(rubraview_jobwire_out_t *out, uint8_t type) {
    if (!out) return;
    out->start = out->len;
    put_le(out, 0, 4);   /* the length, written by finish */
    put_le(out, type, 1);
}

void rubraview_jobwire_put_u8(rubraview_jobwire_out_t *out, uint8_t v) { if (out) put_le(out, v, 1); }
void rubraview_jobwire_put_u32(rubraview_jobwire_out_t *out, uint32_t v) { if (out) put_le(out, v, 4); }
void rubraview_jobwire_put_u64(rubraview_jobwire_out_t *out, uint64_t v) { if (out) put_le(out, v, 8); }
void rubraview_jobwire_put_i32(rubraview_jobwire_out_t *out, int32_t v) { if (out) put_le(out, (uint32_t)v, 4); }
void rubraview_jobwire_put_i64(rubraview_jobwire_out_t *out, int64_t v) { if (out) put_le(out, (uint64_t)v, 8); }

void rubraview_jobwire_put_str(rubraview_jobwire_out_t *out, proven_u8str_view_t s) {
    if (!out) return;
    if (s.size > RUBRAVIEW_JOBWIRE_MAX) { out->failed = true; return; }
    put_le(out, (uint32_t)s.size, 4);
    if (s.size == 0 || !grow(out, s.size)) return;
    proven_sys_mem_copy(out->data + out->len, s.ptr, s.size);
    out->len += s.size;
}

bool rubraview_jobwire_finish(rubraview_jobwire_out_t *out) {
    if (!out) return false;
    size_t body = out->len >= out->start + 4 ? out->len - out->start - 4 : 0;
    if (out->failed || body == 0 || body > RUBRAVIEW_JOBWIRE_MAX) {   /* taken back whole */
        out->len = out->start;
        out->failed = false;
        return false;
    }
    for (size_t i = 0; i < 4; ++i) out->data[out->start + i] = (uint8_t)(body >> (8 * i));
    out->start = out->len;
    return true;
}

void rubraview_jobwire_out_drop(rubraview_jobwire_out_t *out, size_t n) {
    if (!out || n == 0) return;
    if (n >= out->len) { out->len = 0; out->start = 0; return; }
    proven_sys_mem_move(out->data, out->data + n, out->len - n);
    out->len -= n;
    out->start = out->start > n ? out->start - n : 0;
}

void rubraview_jobwire_out_free(rubraview_jobwire_out_t *out) {
    if (!out) return;
    rubraview_free(out->data);
    *out = (rubraview_jobwire_out_t){0};
}

/* ---- reading ---- */

static uint64_t le(const uint8_t *p, size_t bytes) {
    uint64_t v = 0;
    for (size_t i = 0; i < bytes; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

int rubraview_jobwire_take(const uint8_t *data, size_t len, rubraview_jobwire_msg_t *msg, size_t *used) {
    if (!data || len < 4) return 0;
    uint64_t body = le(data, 4);
    if (body == 0 || body > RUBRAVIEW_JOBWIRE_MAX) return -1;
    if (len - 4 < body) return 0;
    if (msg) *msg = (rubraview_jobwire_msg_t){ .type = data[4], .p = data + 5, .left = (size_t)body - 1 };
    if (used) *used = 4 + (size_t)body;
    return 1;
}

static uint64_t get_le(rubraview_jobwire_msg_t *msg, size_t bytes) {
    if (!msg || msg->bad || msg->left < bytes) { if (msg) msg->bad = true; return 0; }
    uint64_t v = le(msg->p, bytes);
    msg->p += bytes;
    msg->left -= bytes;
    return v;
}

uint8_t  rubraview_jobwire_get_u8(rubraview_jobwire_msg_t *msg)  { return (uint8_t)get_le(msg, 1); }
uint32_t rubraview_jobwire_get_u32(rubraview_jobwire_msg_t *msg) { return (uint32_t)get_le(msg, 4); }
uint64_t rubraview_jobwire_get_u64(rubraview_jobwire_msg_t *msg) { return get_le(msg, 8); }
int32_t  rubraview_jobwire_get_i32(rubraview_jobwire_msg_t *msg) { return (int32_t)(uint32_t)get_le(msg, 4); }
int64_t  rubraview_jobwire_get_i64(rubraview_jobwire_msg_t *msg) { return (int64_t)get_le(msg, 8); }

proven_u8str_view_t rubraview_jobwire_get_str(rubraview_jobwire_msg_t *msg) {
    uint32_t n = rubraview_jobwire_get_u32(msg);
    if (!msg || msg->bad || msg->left < n) { if (msg) msg->bad = true; return (proven_u8str_view_t){0}; }
    proven_u8str_view_t s = { .ptr = (const proven_byte_t *)msg->p, .size = n };
    msg->p += n;
    msg->left -= n;
    return s;
}

bool rubraview_jobwire_done(const rubraview_jobwire_msg_t *msg) { return msg && !msg->bad && msg->left == 0; }

/* ---- the messages ---- */

void rubraview_jobwire_put_start(rubraview_jobwire_out_t *out, const rubraview_jobwire_start_t *s,
                                 const proven_u8str_view_t *sources, size_t count) {
    if (!out || !s || count > UINT32_MAX) return;
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_START);
    rubraview_jobwire_put_u8(out, s->moving);
    rubraview_jobwire_put_str(out, s->dir);
    rubraview_jobwire_put_i32(out, s->basket);
    rubraview_jobwire_put_u8(out, s->from_cut);
    rubraview_jobwire_put_i32(out, s->viewer_page);
    rubraview_jobwire_put_str(out, s->viewer_path);
    rubraview_jobwire_put_str(out, s->temp_dir);
    rubraview_jobwire_put_u8(out, s->same_recycles_copy);
    rubraview_jobwire_put_str(out, s->lang_file);
    rubraview_jobwire_put_str(out, s->ui_font);
    rubraview_jobwire_put_u32(out, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) rubraview_jobwire_put_str(out, sources[i]);
    (void)rubraview_jobwire_finish(out);
}

void rubraview_jobwire_get_start(rubraview_jobwire_msg_t *msg, rubraview_jobwire_start_t *s) {
    if (!s) return;
    s->moving = rubraview_jobwire_get_u8(msg) != 0;
    s->dir = rubraview_jobwire_get_str(msg);
    s->basket = rubraview_jobwire_get_i32(msg);
    s->from_cut = rubraview_jobwire_get_u8(msg) != 0;
    s->viewer_page = rubraview_jobwire_get_i32(msg);
    s->viewer_path = rubraview_jobwire_get_str(msg);
    s->temp_dir = rubraview_jobwire_get_str(msg);
    s->same_recycles_copy = rubraview_jobwire_get_u8(msg) != 0;
    s->lang_file = rubraview_jobwire_get_str(msg);
    s->ui_font = rubraview_jobwire_get_str(msg);
    s->count = rubraview_jobwire_get_u32(msg);
}

void rubraview_jobwire_put_progress(rubraview_jobwire_out_t *out, const rubraview_jobwire_progress_t *p) {
    if (!out || !p) return;
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_PROGRESS);
    const uint64_t v[7] = { p->total, p->done, p->failed, p->skipped, p->bytes_total, p->bytes_done, p->clashes };
    for (size_t i = 0; i < 7; ++i) rubraview_jobwire_put_u64(out, v[i]);
    (void)rubraview_jobwire_finish(out);
}

void rubraview_jobwire_get_progress(rubraview_jobwire_msg_t *msg, rubraview_jobwire_progress_t *p) {
    if (!p) return;
    p->total = rubraview_jobwire_get_u64(msg);
    p->done = rubraview_jobwire_get_u64(msg);
    p->failed = rubraview_jobwire_get_u64(msg);
    p->skipped = rubraview_jobwire_get_u64(msg);
    p->bytes_total = rubraview_jobwire_get_u64(msg);
    p->bytes_done = rubraview_jobwire_get_u64(msg);
    p->clashes = rubraview_jobwire_get_u64(msg);
}

void rubraview_jobwire_put_item(rubraview_jobwire_out_t *out, const rubraview_jobwire_item_t *it) {
    if (!out || !it) return;
    rubraview_jobwire_put_str(out, it->source);
    rubraview_jobwire_put_str(out, it->target);
    rubraview_jobwire_put_str(out, it->why);
    rubraview_jobwire_put_u8(out, it->did);
    rubraview_jobwire_put_u8(out, it->state);
    rubraview_jobwire_put_u8(out, it->overwrote);
    rubraview_jobwire_put_i32(out, it->parent);
}

void rubraview_jobwire_get_item(rubraview_jobwire_msg_t *msg, rubraview_jobwire_item_t *it) {
    if (!it) return;
    it->source = rubraview_jobwire_get_str(msg);
    it->target = rubraview_jobwire_get_str(msg);
    it->why = rubraview_jobwire_get_str(msg);
    it->did = rubraview_jobwire_get_u8(msg);
    it->state = rubraview_jobwire_get_u8(msg);
    it->overwrote = rubraview_jobwire_get_u8(msg) != 0;
    it->parent = rubraview_jobwire_get_i32(msg);
}

void rubraview_jobwire_put_end(rubraview_jobwire_out_t *out, const rubraview_jobwire_end_t *e) {
    if (!out || !e) return;
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_END);
    rubraview_jobwire_put_u8(out, e->moving);
    rubraview_jobwire_put_u64(out, e->skipped);
    rubraview_jobwire_put_u64(out, e->failed);
    rubraview_jobwire_put_u8(out, e->stopped);
    (void)rubraview_jobwire_finish(out);
}

void rubraview_jobwire_get_end(rubraview_jobwire_msg_t *msg, rubraview_jobwire_end_t *e) {
    if (!e) return;
    e->moving = rubraview_jobwire_get_u8(msg) != 0;
    e->skipped = rubraview_jobwire_get_u64(msg);
    e->failed = rubraview_jobwire_get_u64(msg);
    e->stopped = rubraview_jobwire_get_u8(msg) != 0;
}

void rubraview_jobwire_put_row(rubraview_jobwire_out_t *out, const rubraview_jobwire_row_t *r) {
    if (!out || !r) return;
    rubraview_jobwire_put_u32(out, r->owner);
    rubraview_jobwire_put_u64(out, r->index);
    rubraview_jobwire_put_str(out, r->source);
    rubraview_jobwire_put_str(out, r->target);
    rubraview_jobwire_put_u64(out, r->ssize);
    rubraview_jobwire_put_u64(out, r->tsize);
    rubraview_jobwire_put_i64(out, r->smtime);
    rubraview_jobwire_put_i64(out, r->tmtime);
}

void rubraview_jobwire_get_row(rubraview_jobwire_msg_t *msg, rubraview_jobwire_row_t *r) {
    if (!r) return;
    r->owner = rubraview_jobwire_get_u32(msg);
    r->index = rubraview_jobwire_get_u64(msg);
    r->source = rubraview_jobwire_get_str(msg);
    r->target = rubraview_jobwire_get_str(msg);
    r->ssize = rubraview_jobwire_get_u64(msg);
    r->tsize = rubraview_jobwire_get_u64(msg);
    r->smtime = rubraview_jobwire_get_i64(msg);
    r->tmtime = rubraview_jobwire_get_i64(msg);
}

void rubraview_jobwire_put_answer(rubraview_jobwire_out_t *out, const rubraview_jobwire_answer_t *a) {
    if (!out || !a) return;
    rubraview_jobwire_begin(out, RUBRAVIEW_JOBWIRE_ANSWER);
    rubraview_jobwire_put_u32(out, a->owner);
    rubraview_jobwire_put_u64(out, a->index);
    rubraview_jobwire_put_u8(out, a->all);
    rubraview_jobwire_put_u8(out, a->answer);
    (void)rubraview_jobwire_finish(out);
}

void rubraview_jobwire_get_answer(rubraview_jobwire_msg_t *msg, rubraview_jobwire_answer_t *a) {
    if (!a) return;
    a->owner = rubraview_jobwire_get_u32(msg);
    a->index = rubraview_jobwire_get_u64(msg);
    a->all = rubraview_jobwire_get_u8(msg) != 0;
    a->answer = rubraview_jobwire_get_u8(msg);
}
