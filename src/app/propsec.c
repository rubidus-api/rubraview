/*
 * RV-281 phase 2 (owner, 2026-11-14: "디지털서명, 보안 ... 모든 기능들을 자체 구현"; "감사는 별도 프로세스에서 관리자 권한
 * 획득하여 자체 구현한 동작을 수행하는 것으로"): the properties window's Digital Signatures and Security tabs.
 *
 * Signatures and permissions are read and written through Windows' system calls in the viewer (pal_fileprops.h); the
 * audit entries, and a permission change Windows refused the user, through the elevated child (UAC asks each time).
 * The model (rights from a mask, a box clicked, the ACEs written back) is core's (fileprops.h, test_fileprops).
 */
#include "propsview.h"
#include "rubraview/pal/pal_shellverbs.h"

/* ---- reading ---- */

static void people_from_aces(props_view_t *v) {
    v->people_count = rubraview_props_principals_from(v->aces, v->ace_count, v->people, PROPS_PEOPLE_MAX);
    if (v->person_sel >= (int32_t)v->people_count) v->person_sel = v->people_count ? 0 : -1;
}

void propsec_read(app_state_t *app) {
    props_view_t *v = V(app);
    v->sig_count = v->count == 1 ? rubraview_pal_fileprops_signatures(v->paths[0], v->sigs, PROPS_SIGS_MAX) : 0;
    v->sig_sel = v->sig_count ? 0 : -1;
    v->ace_count = 0;
    v->people_count = 0;
    v->person_sel = 0;
    v->people_changed = false;
    v->protect_want = -1;
    v->owner_sid_want[0] = v->owner_want[0] = '\0';
    v->sec_failed = false;
    if (v->count == 1) {
        v->ace_count = rubraview_pal_fileprops_security_read(v->paths[0], &v->sec, v->aces, PROPS_ACES_MAX);
        people_from_aces(v);
    }
    app->props_dirty = true;
}

bool propsec_sig_tab(const props_view_t *v) { return v->count == 1 && v->sig_count > 0; }

void propsec_free(props_view_t *v) {
    if (v->sec_asking) rubraview_pal_shellhost_forget(v->sec_ticket);
    if (v->audit_state == ASK_WAITING || v->audit_writing) rubraview_pal_shellhost_forget(v->audit_ticket);
}

static bool is_folder(const props_view_t *v) { return v->count == 1 && v->files[0].is_folder; }

bool propsec_changed(const props_view_t *v) {
    return v->people_changed || v->protect_want >= 0 || v->owner_sid_want[0] != '\0';
}

/* ---- Apply ---- */

/* The lines for the elevated child: the owner, the inheritance and every explicit permission. */
static char *sec_request(props_view_t *v, size_t *len) {
    static rubraview_props_ace_t out[PROPS_ACES_MAX];
    size_t n = rubraview_props_explicit_aces(v->aces, v->ace_count, v->people, v->people_count, is_folder(v), out, PROPS_ACES_MAX);
    rubraview_props_sec_t sec = { .protect = v->protect_want >= 0 ? v->protect_want : (v->sec.protect ? 1 : 0),
                                  .write_dacl = v->people_changed || v->protect_want >= 0 };
    (void)rubraview_fmt(sec.owner_sid, sizeof sec.owner_sid, "{}", PROVEN_ARG(v->owner_sid_want));
    size_t cap = 4096 + n * 1200;
    char *text = (char *)rubraview_alloc(cap);
    if (text && !rubraview_props_sec_lines(&sec, out, n, text, cap)) { rubraview_free(text); text = NULL; }
    *len = text ? proven_cstr_len(text) : 0;
    return text;
}

bool propsec_apply(app_state_t *app) {
    props_view_t *v = V(app);
    if (!propsec_changed(v) || v->count != 1) return true;
    static rubraview_props_ace_t out[PROPS_ACES_MAX];
    size_t n = rubraview_props_explicit_aces(v->aces, v->ace_count, v->people, v->people_count, is_folder(v), out, PROPS_ACES_MAX);
    rubraview_props_sec_t change = { .protect = v->protect_want >= 0 ? v->protect_want : (v->sec.protect ? 1 : 0) };
    (void)rubraview_fmt(change.owner_sid, sizeof change.owner_sid, "{}", PROVEN_ARG(v->owner_sid_want));
    char why[256] = "";
    bool dacl = v->people_changed || v->protect_want >= 0;
    if (!rubraview_pal_fileprops_security_write(v->paths[0], &change, out, n, dacl, why, sizeof why)) {
        (void)rubraview_fmt(v->message, sizeof v->message, T("Windows refused the change: {} Apply as administrator to make it."), PROVEN_ARG(why));
        v->sec_failed = true;
        app_log(app, "properties", TU8("security: refused, administrator offered"));
        app->props_dirty = true;
        return false;
    }
    app_log(app, "properties", TU8("security changed"));
    pw_say(v, T("the permissions are changed"));
    propsec_read(app);
    return true;
}

static void apply_elevated(app_state_t *app) {
    props_view_t *v = V(app);
    size_t len = 0;
    char *lines = sec_request(v, &len);
    if (!lines) return;
    v->sec_asking = rubraview_pal_shellhost_ask_elevated("sec-write", (const char *const *)v->paths, 1, rubraview_bytes_view(lines, len),
                                                         &v->sec_ticket, rubraview_pal_window_native_handle(app->props_window));
    rubraview_free(lines);
    v->sec_at = rubraview_pal_time_now_seconds();
    pw_say(v, v->sec_asking ? T("waiting for administrator rights...") : T("administrator rights were not given"));
}

static void ask_audit(app_state_t *app, bool write) {
    props_view_t *v = V(app);
    char *lines = NULL;
    size_t len = 0;
    if (write) {
        size_t cap = 4096 + v->audit_count * 1200;
        lines = (char *)rubraview_alloc(cap);
        rubraview_props_sec_t sec = { .protect = v->audit_protect ? 1 : 0 };
        if (!lines || !rubraview_props_sec_lines(&sec, v->audits, v->audit_count, lines, cap)) { rubraview_free(lines); return; }
        len = proven_cstr_len(lines);
    }
    bool ok = rubraview_pal_shellhost_ask_elevated(write ? "audit-write" : "audit-read", (const char *const *)v->paths, 1,
                                                   rubraview_bytes_view(lines ? lines : "", len), &v->audit_ticket,
                                                   rubraview_pal_window_native_handle(app->props_window));
    rubraview_free(lines);
    v->audit_at = rubraview_pal_time_now_seconds();
    if (!ok) { pw_say(v, T("administrator rights were not given")); return; }
    if (write) v->audit_writing = true; else v->audit_state = ASK_WAITING;
    pw_say(v, T("waiting for administrator rights..."));
}

/* The elevated child's answers, looked for each pass (a person may take long to answer Windows' question). */
void propsec_poll(app_state_t *app) {
    props_view_t *v = V(app);
    double now = rubraview_pal_time_now_seconds();
    char *text = NULL;
    size_t n = 0;
    if (v->sec_asking) {
        if (rubraview_pal_shellhost_answer(v->sec_ticket, &text, &n) == 1) {
            rubraview_props_sec_t back;
            (void)rubraview_props_sec_read(rubraview_bytes_view(text, n), &back, NULL, 0);
            rubraview_free(text);
            v->sec_asking = false;
            if (back.error[0]) {
                (void)rubraview_fmt(v->message, sizeof v->message, T("not changed, even as administrator: {}"), PROVEN_ARG(back.error));
            } else {
                propsec_read(app);
                pw_say(v, T("the permissions are changed (as administrator)"));
            }
            app_log(app, "properties", back.error[0] ? TU8("security as administrator: failed") : TU8("security changed as administrator"));
            app->props_dirty = true;
        } else if (now - v->sec_at > 300.0) {
            rubraview_pal_shellhost_forget(v->sec_ticket);
            v->sec_asking = false;
            pw_say(v, T("the administrator's helper did not answer"));
            app->props_dirty = true;
        }
    }
    if (v->audit_state == ASK_WAITING || v->audit_writing) {
        if (rubraview_pal_shellhost_answer(v->audit_ticket, &text, &n) == 1) {
            rubraview_props_sec_t back;
            size_t got = rubraview_props_sec_read(rubraview_bytes_view(text, n), &back, v->audits, PROPS_PEOPLE_MAX);
            rubraview_free(text);
            if (v->audit_writing) {
                v->audit_writing = false;
                if (back.error[0]) (void)rubraview_fmt(v->message, sizeof v->message, T("the auditing was not changed: {}"), PROVEN_ARG(back.error));
                else { pw_say(v, T("the auditing is changed")); v->audit_changed = false; }
            } else if (back.error[0]) {
                v->audit_state = ASK_FAILED;
                (void)rubraview_fmt(v->message, sizeof v->message, T("the auditing could not be read: {}"), PROVEN_ARG(back.error));
            } else {
                v->audit_state = ASK_DONE;
                v->audit_count = got;
                v->audit_protect = back.protect == 1;
                v->audit_sel = got ? 0 : -1;
                v->audit_changed = false;
                pw_say(v, "");
            }
            app->props_dirty = true;
        } else if (now - v->audit_at > 300.0) {
            rubraview_pal_shellhost_forget(v->audit_ticket);
            if (v->audit_writing) v->audit_writing = false; else v->audit_state = ASK_FAILED;
            pw_say(v, T("the administrator's helper did not answer"));
            app->props_dirty = true;
        }
    }
}

/* ---- the name field ---- */

void propsec_name_done(app_state_t *app, const char *typed) {
    props_view_t *v = V(app);
    char sid[192], name[256];
    if (!rubraview_pal_fileprops_account(typed, sid, sizeof sid, name, sizeof name)) {
        (void)rubraview_fmt(v->message, sizeof v->message, T("no user or group is called \"{}\""), PROVEN_ARG(typed));
        return;
    }
    if (v->name_for == NAME_OWNER) {
        (void)rubraview_fmt(v->owner_sid_want, sizeof v->owner_sid_want, "{}", PROVEN_ARG(sid));
        (void)rubraview_fmt(v->owner_want, sizeof v->owner_want, "{}", PROVEN_ARG(name));
        pw_say(v, T("the new owner is set when you press Apply or OK"));
        return;
    }
    if (v->name_for == NAME_AUDIT) {
        if (v->audit_count >= PROPS_PEOPLE_MAX) return;
        rubraview_props_ace_t *a = &v->audits[v->audit_count];
        *a = (rubraview_props_ace_t){ .allow = true, .audit = 2, .mask = rubraview_props_right_mask(RUBRAVIEW_RIGHT_MODIFY),
                                      .flags = is_folder(v) ? (RUBRAVIEW_ACE_OBJECT_INHERIT | RUBRAVIEW_ACE_CONTAINER_INHERIT) : 0 };
        (void)rubraview_fmt(a->sid, sizeof a->sid, "{}", PROVEN_ARG(sid));
        (void)rubraview_fmt(a->name, sizeof a->name, "{}", PROVEN_ARG(name));
        v->audit_sel = (int32_t)v->audit_count++;
        v->audit_changed = true;
        return;
    }
    for (size_t i = 0; i < v->people_count; ++i)   /* already there: chosen */
        if (proven_u8str_view_eq(proven_u8str_view_from_cstr(v->people[i].sid), proven_u8str_view_from_cstr(sid))) { v->person_sel = (int32_t)i; return; }
    if (v->people_count >= PROPS_PEOPLE_MAX) return;
    rubraview_props_principal_t *p = &v->people[v->people_count];
    *p = (rubraview_props_principal_t){ .allow = rubraview_props_right_mask(RUBRAVIEW_RIGHT_READ_EXECUTE), .changed = true };   /* Windows' first boxes */
    (void)rubraview_fmt(p->sid, sizeof p->sid, "{}", PROVEN_ARG(sid));
    (void)rubraview_fmt(p->name, sizeof p->name, "{}", PROVEN_ARG(name));
    v->person_sel = (int32_t)v->people_count++;
    v->people_changed = true;
}

/* ---- buttons ---- */

bool propsec_button(app_state_t *app, int which) {
    props_view_t *v = V(app);
    switch (which) {
        case PB_CERT: {
            char kind[32];
            (void)rubraview_fmt(kind, sizeof kind, "certificate:{}", PROVEN_ARG(v->sig_sel < 0 ? 0 : v->sig_sel));
            if (!rubraview_pal_shellhost_show(kind, (const char *const *)v->paths, 1, rubraview_pal_window_native_handle(app->props_window)))
                pw_say(v, T("Windows' certificate window could not be started"));
            break;
        }
        case PB_ADVANCED: v->advanced = !v->advanced; break;
        case PB_ADD: pw_name_begin(app, NAME_ADD); break;
        case PB_OWNER: pw_name_begin(app, NAME_OWNER); break;
        case PB_AUDIT_ADD: pw_name_begin(app, NAME_AUDIT); break;
        case PB_REMOVE: {
            if (v->person_sel < 0 || (size_t)v->person_sel >= v->people_count) break;
            rubraview_props_principal_t *p = &v->people[v->person_sel];
            if (p->allow_inherited || p->deny_inherited) {   /* Windows: it inherits these; turn inheritance off first */
                pw_say(v, T("it holds permissions passed on from the folder above: turn inheritance off in Advanced first"));
                break;
            }
            for (size_t i = (size_t)v->person_sel; i + 1 < v->people_count; ++i) v->people[i] = v->people[i + 1];
            v->people_count--;
            if (v->person_sel >= (int32_t)v->people_count) v->person_sel = (int32_t)v->people_count - 1;
            v->people_changed = true;
            break;
        }
        case PB_INH_COPY:   /* inheritance off, what was passed on kept as the file's own */
        case PB_INH_DROP: { /* inheritance off, what was passed on dropped */
            for (size_t i = 0; i < v->ace_count; ) {
                if (!(v->aces[i].flags & RUBRAVIEW_ACE_INHERITED)) { ++i; continue; }
                if (which == PB_INH_COPY) { v->aces[i].flags &= (uint8_t)~RUBRAVIEW_ACE_INHERITED; ++i; continue; }
                for (size_t k = i; k + 1 < v->ace_count; ++k) v->aces[k] = v->aces[k + 1];
                v->ace_count--;
            }
            /* the people from the ACEs again, the clicks kept for those changed */
            rubraview_props_principal_t keep[PROPS_PEOPLE_MAX];
            size_t kept = 0;
            for (size_t i = 0; i < v->people_count; ++i) if (v->people[i].changed) keep[kept++] = v->people[i];
            people_from_aces(v);
            for (size_t k = 0; k < kept; ++k) {
                bool found = false;
                for (size_t i = 0; i < v->people_count; ++i)
                    if (proven_u8str_view_eq(proven_u8str_view_from_cstr(v->people[i].sid), proven_u8str_view_from_cstr(keep[k].sid))) {
                        v->people[i].allow = keep[k].allow; v->people[i].deny = keep[k].deny; v->people[i].changed = true; found = true;
                    }
                if (!found && v->people_count < PROPS_PEOPLE_MAX) { keep[k].allow_inherited = keep[k].deny_inherited = 0; v->people[v->people_count++] = keep[k]; }
            }
            v->protect_want = 1;
            v->people_changed = true;
            break;
        }
        case PB_INH_ON: v->protect_want = 0; break;
        case PB_ELEVATE: apply_elevated(app); break;
        case PB_AUDIT: ask_audit(app, false); break;
        case PB_AUDIT_REMOVE:
            if (v->audit_sel < 0 || (size_t)v->audit_sel >= v->audit_count) break;
            if (v->audits[v->audit_sel].flags & RUBRAVIEW_ACE_INHERITED) { pw_say(v, T("an entry passed on from the folder above stays")); break; }
            for (size_t i = (size_t)v->audit_sel; i + 1 < v->audit_count; ++i) v->audits[i] = v->audits[i + 1];
            v->audit_count--;
            if (v->audit_sel >= (int32_t)v->audit_count) v->audit_sel = (int32_t)v->audit_count - 1;
            v->audit_changed = true;
            break;
        case PB_AUDIT_APPLY: {
            /* only the explicit entries are written; the inherited ones are Windows' */
            size_t w = 0;
            for (size_t i = 0; i < v->audit_count; ++i) if (!(v->audits[i].flags & RUBRAVIEW_ACE_INHERITED)) v->audits[w++] = v->audits[i];
            v->audit_count = w;
            if (v->audit_sel >= (int32_t)w) v->audit_sel = (int32_t)w - 1;
            ask_audit(app, true);
            break;
        }
        default: return false;
    }
    app->props_dirty = true;
    return true;
}

/* ---- the pointer ---- */

bool propsec_press(app_state_t *app, double x, double y) {
    props_view_t *v = V(app);
    if (v->tab == RUBRAVIEW_PROPS_SIGNATURES) {
        for (size_t i = 0; i < v->sig_count; ++i)
            if (v->sig_rect[i].width > 0 && pal_rect_has(v->sig_rect[i], x, y)) { v->sig_sel = (int32_t)i; app->props_dirty = true; return true; }
        return false;
    }
    if (v->advanced) {
        for (size_t i = 0; i < v->audit_count; ++i)
            if (v->audit_rect[i].width > 0 && pal_rect_has(v->audit_rect[i], x, y)) { v->audit_sel = (int32_t)i; app->props_dirty = true; return true; }
        if (v->audit_sel >= 0 && (size_t)v->audit_sel < v->audit_count && !(v->audits[v->audit_sel].flags & RUBRAVIEW_ACE_INHERITED)) {
            rubraview_props_ace_t *a = &v->audits[v->audit_sel];
            for (int k = 0; k < 2; ++k)
                if (v->audit_kind_box[k].width > 0 && pal_rect_has(v->audit_kind_box[k], x, y)) {
                    uint8_t bit = (uint8_t)(k == 0 ? 1 : 2);
                    if ((a->audit & bit) && (a->audit & ~bit)) a->audit &= (uint8_t)~bit; else a->audit |= bit;   /* never neither */
                    v->audit_changed = true; app->props_dirty = true; return true;
                }
            for (int32_t r = 0; r < RUBRAVIEW_RIGHT_SPECIAL; ++r)
                if (v->right_box[0][r].width > 0 && pal_rect_has(v->right_box[0][r], x, y)) {
                    bool held = rubraview_props_right_held(rubraview_props_map_generic(a->mask), r, is_folder(v));
                    uint32_t m = rubraview_props_right_set(rubraview_props_map_generic(a->mask), r, !held, is_folder(v));
                    if (m) { a->mask = m; v->audit_changed = true; }
                    app->props_dirty = true;
                    return true;
                }
        }
        return false;
    }
    for (size_t i = 0; i < v->people_count; ++i)
        if (v->person_rect[i].width > 0 && pal_rect_has(v->person_rect[i], x, y)) { v->person_sel = (int32_t)i; app->props_dirty = true; return true; }
    if (v->person_sel < 0 || (size_t)v->person_sel >= v->people_count) return false;
    rubraview_props_principal_t *p = &v->people[v->person_sel];
    bool folder = is_folder(v);
    for (int col = 0; col < 2; ++col)
        for (int32_t r = 0; r < RUBRAVIEW_RIGHT_SPECIAL; ++r) {
            if (v->right_box[col][r].width <= 0 || !pal_rect_has(v->right_box[col][r], x, y)) continue;
            uint32_t *mine = col == 0 ? &p->allow : &p->deny;
            uint32_t *other = col == 0 ? &p->deny : &p->allow;
            bool held = rubraview_props_right_held(*mine, r, folder);
            *mine = rubraview_props_right_set(*mine, r, !held, folder);
            if (!held) {   /* Windows: ticking Allow clears that Deny, and the other way round */
                uint32_t o = rubraview_props_right_mask(r);
                for (int32_t q = 0; q < RUBRAVIEW_RIGHT_SPECIAL; ++q)
                    if (rubraview_props_right_held(*other, q, folder) && (rubraview_props_right_mask(q) & o)) *other = rubraview_props_right_set(*other, q, false, folder);
            }
            p->changed = true;
            v->people_changed = true;
            app->props_dirty = true;
            return true;
        }
    return false;
}

/* ---- drawing ---- */

static double dpi_of(app_state_t *app) { return rubraview_pal_window_dpi_scale(app->props_window); }

static double width_of(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->props_window, &w, &h);
    return (double)w;
}

/* A row of a list: lit when chosen, its rectangle kept for the pointer. */
static void list_row(app_state_t *app, rubraview_pal_rect_t *keep, double y, double h, bool chosen) {
    double dpi = dpi_of(app);
    rubraview_pal_rect_t r = { 18.0 * dpi, y, width_of(app) - 36.0 * dpi, h };
    *keep = r;
    bool over = pal_rect_has(r, app->props_px, app->props_py);
    if (chosen || over) rubraview_pal_render_fill_rect(app->props_renderer, r, chosen ? 0xFF2F4A6Au : 0xFF262B32u, 3.0 * dpi);
}

static void heading(app_state_t *app, double y, const char *words) {
    double dpi = dpi_of(app);
    pw_text_at(app->props_renderer, words, (rubraview_pal_rect_t){ 18.0 * dpi, y, width_of(app) - 36.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_HEADING);
}

void propsec_draw_signatures(app_state_t *app, double y, double bottom) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), w = width_of(app);
    double c1 = 24.0 * dpi, c2 = w * 0.55, c3 = w * 0.72;
    heading(app, y, T("Signature list"));
    y += 24.0 * dpi;
    pw_text_at(r, T("Name of signer:"), (rubraview_pal_rect_t){ c1, y, c2 - c1, 18.0 * dpi }, 12.0 * dpi, PW_DIM);
    pw_text_at(r, T("Digest algorithm"), (rubraview_pal_rect_t){ c2, y, c3 - c2, 18.0 * dpi }, 12.0 * dpi, PW_DIM);
    pw_text_at(r, T("Timestamp"), (rubraview_pal_rect_t){ c3, y, w - c3 - 18.0 * dpi, 18.0 * dpi }, 12.0 * dpi, PW_DIM);
    y += 22.0 * dpi;
    char when[96];
    for (size_t i = 0; i < PROPS_SIGS_MAX; ++i) v->sig_rect[i] = (rubraview_pal_rect_t){0};
    for (size_t i = 0; i < v->sig_count; ++i) {
        const rubraview_props_sig_t *s = &v->sigs[i];
        list_row(app, &v->sig_rect[i], y, 22.0 * dpi, (int32_t)i == v->sig_sel);
        pw_text_at(r, s->signer[0] ? s->signer : T("(unknown)"), (rubraview_pal_rect_t){ c1, y + 2.0 * dpi, c2 - c1 - 4.0 * dpi, 18.0 * dpi }, 13.0 * dpi, PW_TEXT);
        pw_text_at(r, s->algorithm, (rubraview_pal_rect_t){ c2, y + 2.0 * dpi, c3 - c2, 18.0 * dpi }, 13.0 * dpi, PW_TEXT);
        if (s->signed_at) info_local_time(when, sizeof when, s->signed_at);
        else (void)rubraview_fmt(when, sizeof when, "{}", PROVEN_ARG(T("Not available")));
        pw_text_at(r, when, (rubraview_pal_rect_t){ c3, y + 2.0 * dpi, w - c3 - 18.0 * dpi, 18.0 * dpi }, 13.0 * dpi, PW_TEXT);
        y += 24.0 * dpi;
    }
    y = pw_separator(app, y + 4.0 * dpi);
    if (v->sig_sel < 0 || (size_t)v->sig_sel >= v->sig_count) return;
    const rubraview_props_sig_t *s = &v->sigs[v->sig_sel];
    heading(app, y, T("Digital Signature Information"));
    y += 24.0 * dpi;
    uint32_t color = s->status == RUBRAVIEW_SIG_OK ? 0xFF7FD08Au : s->status == RUBRAVIEW_SIG_UNCHECKED ? PW_DIM : 0xFFE88080u;
    pw_text_at(r, rubraview_tr(rubraview_props_sig_status_words(s->status)), (rubraview_pal_rect_t){ 18.0 * dpi, y, w - 36.0 * dpi, 20.0 * dpi }, 13.0 * dpi, color);
    y += 28.0 * dpi;
    heading(app, y, T("Signer information"));
    y += 24.0 * dpi;
    char a[96], b[96], text[256];
    y = pw_info_row(app, y, T("Name:"), s->signer[0] ? s->signer : T("(unknown)"));
    y = pw_info_row(app, y, T("E-mail:"), s->email[0] ? s->email : T("Not available"));
    if (s->signed_at) info_local_time(a, sizeof a, s->signed_at); else (void)rubraview_fmt(a, sizeof a, "{}", PROVEN_ARG(T("Not available")));
    y = pw_info_row(app, y, T("Signing time:"), a);
    y = pw_info_row(app, y, T("Issued by:"), s->issuer[0] ? s->issuer : T("(unknown)"));
    info_local_time(a, sizeof a, s->valid_from);
    info_local_time(b, sizeof b, s->valid_to);
    (void)rubraview_fmt(text, sizeof text, T("{} to {}"), PROVEN_ARG(a), PROVEN_ARG(b));
    y = pw_info_row(app, y, T("Valid:"), text);
    y = pw_info_row(app, y, T("Serial number:"), s->serial);
    y = pw_separator(app, y);
    heading(app, y, T("Countersignatures"));
    y += 24.0 * dpi;
    if (s->countersigner[0]) {
        y = pw_info_row(app, y, T("Name of signer:"), s->countersigner);
        if (s->signed_at) { info_local_time(a, sizeof a, s->signed_at); y = pw_info_row(app, y, T("Timestamp:"), a); }
    } else {
        y = pw_info_row(app, y, T("Name of signer:"), T("Not available"));
    }
    if (y + 34.0 * dpi < bottom)
        pw_button_at(app, PB_CERT, T("View Certificate"), (rubraview_pal_rect_t){ w - 168.0 * dpi, y + 6.0 * dpi, 150.0 * dpi, 26.0 * dpi }, false, true);
}

/* A box of a right in a column: ticked, dim when it holds only by inheritance (not to be unticked here). */
static void right_box(app_state_t *app, int col, int32_t right, double x, double y, bool own, bool inherited, bool enabled) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), s = 16.0 * dpi;
    rubraview_pal_rect_t box = { x, y + 2.0 * dpi, s, s };
    v->right_box[col][right] = enabled ? (rubraview_pal_rect_t){ x - 6.0 * dpi, y, s + 12.0 * dpi, 20.0 * dpi } : (rubraview_pal_rect_t){0};
    rubraview_pal_render_fill_rect(r, box, 0xFF15181Cu, 2.0 * dpi);
    rubraview_pal_render_stroke_rect(r, box, enabled ? 0xFF6A737Du : PW_LINE, 1.0, 2.0 * dpi);
    if (own || inherited)
        rubraview_pal_render_draw_text(r, PROVEN_LIT("\xe2\x9c\x93"), box, 14.0 * dpi, own ? PW_TEXT : PW_DIM, RUBRAVIEW_TEXT_CENTER);
}

/* The rights of a mask (or two: Allow, Deny) as rows of boxes. */
static double rights_table(app_state_t *app, double y, const rubraview_props_principal_t *p, const rubraview_props_ace_t *audit, bool enabled) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), w = width_of(app);
    double ca = w - 150.0 * dpi, cd = w - 80.0 * dpi;
    bool folder = is_folder(v);
    pw_text_at(r, audit ? T("Audited") : T("Allow"), (rubraview_pal_rect_t){ ca - 18.0 * dpi, y, 60.0 * dpi, 18.0 * dpi }, 12.0 * dpi, PW_DIM);
    if (!audit) pw_text_at(r, T("Deny"), (rubraview_pal_rect_t){ cd - 14.0 * dpi, y, 60.0 * dpi, 18.0 * dpi }, 12.0 * dpi, PW_DIM);
    y += 20.0 * dpi;
    for (int col = 0; col < 2; ++col) for (int32_t q = 0; q < RUBRAVIEW_RIGHT_COUNT; ++q) v->right_box[col][q] = (rubraview_pal_rect_t){0};
    for (int32_t q = 0; q < RUBRAVIEW_RIGHT_COUNT; ++q) {
        if (!rubraview_props_right_shown(q, folder)) continue;
        pw_text_at(r, rubraview_tr(rubraview_props_right_name(q)), (rubraview_pal_rect_t){ 30.0 * dpi, y, ca - 60.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_TEXT);
        bool special = q == RUBRAVIEW_RIGHT_SPECIAL;   /* shown, never set from here (Windows: Advanced) */
        if (audit) {
            uint32_t m = rubraview_props_map_generic(audit->mask);
            right_box(app, 0, q, ca, y, rubraview_props_right_held(m, q, folder), false, enabled && !special);
        } else {
            right_box(app, 0, q, ca, y, rubraview_props_right_held(p->allow, q, folder) || (special && p->elsewhere),
                      rubraview_props_right_held(p->allow_inherited, q, folder), enabled && !special);
            right_box(app, 1, q, cd, y, rubraview_props_right_held(p->deny, q, folder), rubraview_props_right_held(p->deny_inherited, q, folder),
                      enabled && !special);
        }
        y += 24.0 * dpi;
    }
    return y;
}

static void name_field(app_state_t *app, double y, const char *label) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), w = width_of(app);
    pw_text_at(r, label, (rubraview_pal_rect_t){ 18.0 * dpi, y, 120.0 * dpi, 22.0 * dpi }, 13.0 * dpi, PW_DIM);
    rubraview_pal_rect_t box = { 140.0 * dpi, y - 2.0 * dpi, w - 158.0 * dpi, 24.0 * dpi };
    rubraview_pal_render_fill_rect(r, box, 0xFF15181Cu, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(r, box, PW_ACCENT, 1.0, 3.0 * dpi);
    char shown[600];
    proven_u8str_view_t t = rubraview_textedit_text(&v->edit);
    (void)rubraview_fmt(shown, sizeof shown, "{}{}|", PROVEN_ARG(t), PROVEN_ARG(v->composing));
    pw_text_at(r, shown, (rubraview_pal_rect_t){ box.x + 6.0 * dpi, box.y + 2.0 * dpi, box.width - 12.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_TEXT);
    pw_text_at(r, T("a user or group (Guests, a PC's name\\user); Enter adds, Esc stops"),
               (rubraview_pal_rect_t){ 140.0 * dpi, y + 24.0 * dpi, w - 158.0 * dpi, 18.0 * dpi }, 11.0 * dpi, PW_DIM);
}

static void draw_advanced(app_state_t *app, double y, double bottom) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), w = width_of(app);
    bool folder = is_folder(v);
    /* the owner */
    char text[600];
    (void)rubraview_fmt(text, sizeof text, "{}", PROVEN_ARG(v->owner_want[0] ? v->owner_want : v->sec.owner));
    y = pw_info_row(app, y, T("Owner:"), text);
    pw_button_at(app, PB_OWNER, T("Change..."), (rubraview_pal_rect_t){ w - 104.0 * dpi, y - 26.0 * dpi, 86.0 * dpi, 24.0 * dpi }, false, !v->sec_asking);
    /* the inheritance */
    bool off = v->protect_want >= 0 ? v->protect_want == 1 : v->sec.protect;
    y = pw_info_row(app, y, T("Inheritance:"), off ? T("off: only its own permissions") : T("on: the folder above's permissions pass on"));
    if (off) pw_button_at(app, PB_INH_ON, T("Enable inheritance"), (rubraview_pal_rect_t){ 140.0 * dpi, y, 170.0 * dpi, 24.0 * dpi }, false, true);
    else {
        pw_button_at(app, PB_INH_COPY, T("Turn off, keeping them"), (rubraview_pal_rect_t){ 140.0 * dpi, y, 170.0 * dpi, 24.0 * dpi }, false, true);
        pw_button_at(app, PB_INH_DROP, T("Turn off, removing them"), (rubraview_pal_rect_t){ 318.0 * dpi, y, 170.0 * dpi, 24.0 * dpi }, false, true);
    }
    y = pw_separator(app, y + 28.0 * dpi);
    /* the entries, as Advanced lists them */
    heading(app, y, T("Permission entries"));
    y += 22.0 * dpi;
    for (size_t i = 0; i < v->ace_count && y < bottom - 280.0 * dpi; ++i) {
        const rubraview_props_ace_t *a = &v->aces[i];
        uint32_t m = rubraview_props_map_generic(a->mask);
        const char *access = T("Special");
        for (int32_t q = 0; q < RUBRAVIEW_RIGHT_SPECIAL; ++q)
            if (rubraview_props_right_shown(q, folder) && m == (rubraview_props_right_mask(q) | (m & 0x100000u)) && rubraview_props_right_mask(q)) { access = rubraview_tr(rubraview_props_right_name(q)); break; }
        (void)rubraview_fmt(text, sizeof text, "{}  {}  {}{}", PROVEN_ARG(a->allow ? T("Allow") : T("Deny")), PROVEN_ARG(a->name), PROVEN_ARG(access),
                            PROVEN_ARG((a->flags & RUBRAVIEW_ACE_INHERITED) ? T("  (inherited)") : (a->flags & RUBRAVIEW_ACE_INHERIT_ONLY) ? T("  (subfolders and files only)") : ""));
        pw_text_at(r, text, (rubraview_pal_rect_t){ 24.0 * dpi, y, w - 42.0 * dpi, 18.0 * dpi }, 12.0 * dpi, PW_TEXT);
        y += 19.0 * dpi;
    }
    y = pw_separator(app, y + 2.0 * dpi);
    /* what the user running this may do */
    heading(app, y, T("Your effective access"));
    y += 22.0 * dpi;
    double cw = (w - 36.0 * dpi) / 2.0;
    for (int32_t i = 0; i < RUBRAVIEW_SPECIAL_COUNT; ++i) {
        bool has = v->sec.no_dacl || (v->sec.effective & rubraview_props_special_bit(i)) != 0;
        (void)rubraview_fmt(text, sizeof text, "{} {}", PROVEN_ARG(has ? "\xe2\x9c\x93" : "\xe2\x80\x93"), PROVEN_ARG(rubraview_tr(rubraview_props_special_name(i, folder))));
        double x = 24.0 * dpi + (double)(i % 2) * cw;
        pw_text_at(r, text, (rubraview_pal_rect_t){ x, y, cw - 8.0 * dpi, 18.0 * dpi }, 12.0 * dpi, has ? PW_TEXT : PW_DIM);
        if (i % 2 == 1 || i == RUBRAVIEW_SPECIAL_COUNT - 1) y += 19.0 * dpi;
    }
    y = pw_separator(app, y + 2.0 * dpi);
    /* the audit entries: administrator only, read and written by the elevated child */
    heading(app, y, T("Auditing"));
    for (size_t i = 0; i < PROPS_PEOPLE_MAX; ++i) v->audit_rect[i] = (rubraview_pal_rect_t){0};
    v->audit_kind_box[0] = v->audit_kind_box[1] = (rubraview_pal_rect_t){0};
    if (v->audit_state != ASK_DONE) {
        pw_button_at(app, PB_AUDIT, T("Auditing (administrator)..."), (rubraview_pal_rect_t){ w - 228.0 * dpi, y - 2.0 * dpi, 210.0 * dpi, 24.0 * dpi }, false,
                     v->audit_state != ASK_WAITING);
        return;
    }
    pw_button_at(app, PB_AUDIT_ADD, T("Add..."), (rubraview_pal_rect_t){ w - 290.0 * dpi, y - 2.0 * dpi, 80.0 * dpi, 24.0 * dpi }, false, true);
    pw_button_at(app, PB_AUDIT_REMOVE, T("Remove"), (rubraview_pal_rect_t){ w - 204.0 * dpi, y - 2.0 * dpi, 80.0 * dpi, 24.0 * dpi }, false, v->audit_sel >= 0);
    pw_button_at(app, PB_AUDIT_APPLY, T("Write"), (rubraview_pal_rect_t){ w - 118.0 * dpi, y - 2.0 * dpi, 100.0 * dpi, 24.0 * dpi }, v->audit_changed,
                 v->audit_changed && !v->audit_writing);
    y += 26.0 * dpi;
    if (v->audit_count == 0) { pw_text_at(r, T("(no audit entries)"), (rubraview_pal_rect_t){ 24.0 * dpi, y, w, 18.0 * dpi }, 12.0 * dpi, PW_DIM); return; }
    for (size_t i = 0; i < v->audit_count && y < bottom - 24.0 * dpi; ++i) {
        const rubraview_props_ace_t *a = &v->audits[i];
        list_row(app, &v->audit_rect[i], y, 20.0 * dpi, (int32_t)i == v->audit_sel);
        const char *kind = a->audit == 3 ? T("All") : a->audit == 2 ? T("Fail") : T("Success");
        (void)rubraview_fmt(text, sizeof text, "{}  {}{}", PROVEN_ARG(kind), PROVEN_ARG(a->name), PROVEN_ARG((a->flags & RUBRAVIEW_ACE_INHERITED) ? T("  (inherited)") : ""));
        pw_text_at(r, text, (rubraview_pal_rect_t){ 24.0 * dpi, y + 1.0 * dpi, w - 42.0 * dpi, 18.0 * dpi }, 12.0 * dpi, PW_TEXT);
        y += 21.0 * dpi;
    }
    if (v->audit_sel >= 0 && (size_t)v->audit_sel < v->audit_count && y + 200.0 * dpi < bottom) {
        const rubraview_props_ace_t *a = &v->audits[v->audit_sel];
        bool own = !(a->flags & RUBRAVIEW_ACE_INHERITED);
        double sx = 24.0 * dpi;
        rubraview_tristate_t succ = (a->audit & 1) ? RUBRAVIEW_TRI_ON : RUBRAVIEW_TRI_OFF, fail = (a->audit & 2) ? RUBRAVIEW_TRI_ON : RUBRAVIEW_TRI_OFF;
        pw_check_at(app, 7, T("Success"), succ, sx, y + 4.0 * dpi, own);
        v->audit_kind_box[0] = own ? v->box[7] : (rubraview_pal_rect_t){0};
        pw_check_at(app, 7, T("Failure"), fail, sx + 120.0 * dpi, y + 4.0 * dpi, own);
        v->audit_kind_box[1] = own ? v->box[7] : (rubraview_pal_rect_t){0};
        v->box[7] = (rubraview_pal_rect_t){0};
        (void)rights_table(app, y + 28.0 * dpi, NULL, a, own);
    }
}

void propsec_draw_security(app_state_t *app, double y, double bottom) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = dpi_of(app), w = width_of(app);
    for (size_t i = 0; i < PROPS_PEOPLE_MAX; ++i) v->person_rect[i] = (rubraview_pal_rect_t){0};
    for (int col = 0; col < 2; ++col) for (int32_t q = 0; q < RUBRAVIEW_RIGHT_COUNT; ++q) v->right_box[col][q] = (rubraview_pal_rect_t){0};
    y = pw_info_row(app, y, T("Object name:"), v->paths[0]);
    if (!v->sec.read_ok) {
        char text[400];
        (void)rubraview_fmt(text, sizeof text, T("The permissions could not be read: {}"), PROVEN_ARG(v->sec.why));
        pw_text_at(r, text, (rubraview_pal_rect_t){ 18.0 * dpi, y + 6.0 * dpi, w - 36.0 * dpi, 20.0 * dpi }, 13.0 * dpi, 0xFFE8C46Fu);
        pw_button_at(app, PB_ADVANCED, T("Advanced"), (rubraview_pal_rect_t){ w - 118.0 * dpi, bottom - 30.0 * dpi, 100.0 * dpi, 26.0 * dpi }, false, false);
        return;
    }
    pw_button_at(app, PB_ADVANCED, v->advanced ? T("Back") : T("Advanced"), (rubraview_pal_rect_t){ w - 118.0 * dpi, bottom - 30.0 * dpi, 100.0 * dpi, 26.0 * dpi },
                 false, true);
    if (v->sec_failed || v->sec_asking)
        pw_button_at(app, PB_ELEVATE, T("\xf0\x9f\x9b\xa1 Apply as administrator"), (rubraview_pal_rect_t){ 18.0 * dpi, bottom - 30.0 * dpi, 220.0 * dpi, 26.0 * dpi },
                     true, !v->sec_asking);
    bottom -= 36.0 * dpi;
    if (v->editing == EDIT_NAME) {
        name_field(app, y + 4.0 * dpi, v->name_for == NAME_OWNER ? T("New owner:") : T("Add:"));
        y += 52.0 * dpi;
    }
    if (v->advanced) { draw_advanced(app, y, bottom); return; }
    if (v->sec.no_dacl) {
        pw_text_at(r, T("It has no permission list: everyone may do everything with it."), (rubraview_pal_rect_t){ 18.0 * dpi, y, w - 36.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_TEXT);
        y += 24.0 * dpi;
    }
    heading(app, y, T("Group or user names:"));
    pw_button_at(app, PB_ADD, T("Add..."), (rubraview_pal_rect_t){ w - 204.0 * dpi, y - 2.0 * dpi, 90.0 * dpi, 24.0 * dpi }, false, !v->sec_asking);
    pw_button_at(app, PB_REMOVE, T("Remove"), (rubraview_pal_rect_t){ w - 108.0 * dpi, y - 2.0 * dpi, 90.0 * dpi, 24.0 * dpi }, false,
                 v->person_sel >= 0 && !v->sec_asking);
    y += 26.0 * dpi;
    double list_bottom = y + 7.0 * 22.0 * dpi;
    for (size_t i = 0; i < v->people_count && y < list_bottom; ++i) {
        list_row(app, &v->person_rect[i], y, 21.0 * dpi, (int32_t)i == v->person_sel);
        pw_text_at(r, v->people[i].name, (rubraview_pal_rect_t){ 24.0 * dpi, y + 1.0 * dpi, w - 48.0 * dpi, 19.0 * dpi }, 13.0 * dpi, PW_TEXT);
        y += 22.0 * dpi;
    }
    y = pw_separator(app, list_bottom + 2.0 * dpi);
    if (v->person_sel < 0 || (size_t)v->person_sel >= v->people_count) return;
    const rubraview_props_principal_t *p = &v->people[v->person_sel];
    char text[400];
    (void)rubraview_fmt(text, sizeof text, T("Permissions for {}"), PROVEN_ARG(p->name));
    heading(app, y, text);
    y = rights_table(app, y + 4.0 * dpi, p, NULL, !v->sec_asking);
    if (y + 20.0 * dpi < bottom && !(v->sec.effective & 0x40000u) && !v->sec.no_dacl)   /* WRITE_DAC */
        pw_text_at(r, T("You may not change these yourself: Apply will offer administrator rights."),
                   (rubraview_pal_rect_t){ 18.0 * dpi, y + 4.0 * dpi, w - 36.0 * dpi, 18.0 * dpi }, 11.0 * dpi, PW_DIM);
}
