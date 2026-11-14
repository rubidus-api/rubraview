/*
 * RV-281 phase 2 (owner, 2026-11-14: "디지털서명, 보안 ... 파일 속성 윈도우의 모든 기능들을 자체 구현"; "감사는 별도 프로세스에서
 * 관리자 권한 획득하여 자체 구현한 동작을 수행하는 것으로"): a file's Digital Signatures and Security through Windows'
 * own system calls (wintrust, crypt32, advapi32, authz; no other program's code). The audit entries (SACL) are read and
 * written only by the elevated child (`--shell-menu-host audit-read|audit-write`), which also writes a permission
 * change Windows refused the user (`sec-write`).
 */
#ifdef _WIN32
#include "rubraview/pal/pal_fileprops.h"
#include "rubraview/pal/pal_wide.h"

#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <aclapi.h>
#include <sddl.h>
#include <authz.h>

#define WPATH 2048

static bool to_wide(const char *text, WCHAR *out, int cap) {
    return text && MultiByteToWideChar(CP_UTF8, 0, text, -1, out, cap) > 0;
}

static void to_utf8(const WCHAR *text, char *out, size_t cap) {
    if (!cap) return;
    if (!text || WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)cap, NULL, NULL) <= 0) out[0] = '\0';
}

static int64_t unix_of(FILETIME ft) {
    ULARGE_INTEGER u = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    if (u.QuadPart == 0) return 0;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

static bool say_error(DWORD err, char *why, size_t cap) {
    WCHAR text[300];
    if (why && cap) {
        if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, text, 300, NULL) == 0) text[0] = 0;
        size_t len = rubraview_wlen(text);
        while (len > 0 && (text[len - 1] == L'\n' || text[len - 1] == L'\r' || text[len - 1] == L' ')) text[--len] = 0;
        to_utf8(text, why, cap);
    }
    return false;
}

/* ---- Digital Signatures ---- */

/* The Windows 8 fields of WINTRUST_DATA this MinGW's header does not have, laid out as wintrust.h of the SDK. */
typedef struct wt_sig_settings {
    DWORD cbStruct, dwIndex, dwFlags, cSecondarySigs, dwVerifiedSigIndex;
    void *pCryptoPolicy;
} wt_sig_settings_t;
typedef struct wt_data_ex {
    WINTRUST_DATA base;
    wt_sig_settings_t *pSignatureSettings;
} wt_data_ex_t;
#define WT_VERIFY_SPECIFIC 0x1

static const char OID_NESTED[] = "1.3.6.1.4.1.311.2.4.1";
static const char OID_RFC3161[] = "1.3.6.1.4.1.311.3.3.1";

/* WinVerifyTrust on the signature at `index` (0 the primary), offline: no revocation look-up. */
static int32_t verify(const WCHAR *w, DWORD index, uint32_t *error) {
    WINTRUST_FILE_INFO file = { .cbStruct = sizeof file, .pcwszFilePath = w };
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    wt_sig_settings_t settings = { .cbStruct = sizeof settings, .dwIndex = index, .dwFlags = WT_VERIFY_SPECIFIC };
    wt_data_ex_t data = {0};
    data.base.cbStruct = sizeof data;
    data.base.dwUIChoice = WTD_UI_NONE;
    data.base.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.base.dwUnionChoice = WTD_CHOICE_FILE;
    data.base.pFile = &file;
    data.base.dwStateAction = WTD_STATEACTION_VERIFY;
    data.base.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    data.pSignatureSettings = &settings;
    LONG r = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    data.base.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    *error = (uint32_t)r;
    if (r == 0) return RUBRAVIEW_SIG_OK;
    if ((uint32_t)r == 0x800B0109u /* CERT_E_UNTRUSTEDROOT */ || (uint32_t)r == 0x800B010Au /* CERT_E_CHAINING */) return RUBRAVIEW_SIG_UNTRUSTED;
    return RUBRAVIEW_SIG_BAD;
}

static const CRYPT_ATTRIBUTE *find_attr(const CRYPT_ATTRIBUTES *attrs, const char *oid) {
    for (DWORD i = 0; attrs && i < attrs->cAttr; ++i)
        if (proven_u8str_view_eq(proven_u8str_view_from_cstr(attrs->rgAttr[i].pszObjId), proven_u8str_view_from_cstr(oid))) return &attrs->rgAttr[i];
    return NULL;
}

/* A signer info's signing time attribute, as Unix seconds (0 when it has none). */
static int64_t signing_time(const CMSG_SIGNER_INFO *si) {
    const CRYPT_ATTRIBUTE *a = find_attr(&si->AuthAttrs, szOID_RSA_signingTime);
    if (!a || a->cValue == 0) return 0;
    FILETIME ft;
    DWORD n = sizeof ft;
    if (!CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_RSA_signingTime, a->rgValue[0].pbData, a->rgValue[0].cbData, 0, &ft, &n)) return 0;
    return unix_of(ft);
}

static CMSG_SIGNER_INFO *signer_info(HCRYPTMSG msg) {
    DWORD n = 0;
    if (!CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &n)) return NULL;
    CMSG_SIGNER_INFO *si = (CMSG_SIGNER_INFO *)rubraview_alloc(n);
    if (si && !CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si, &n)) { rubraview_free(si); return NULL; }
    return si;
}

static PCCERT_CONTEXT signer_cert(HCERTSTORE store, const CMSG_SIGNER_INFO *si) {
    CERT_INFO ci = { .Issuer = si->Issuer, .SerialNumber = si->SerialNumber };
    return CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, NULL);
}

static void cert_name(PCCERT_CONTEXT c, DWORD type, DWORD flags, char *out, size_t cap) {
    WCHAR w[256];
    if (!c || CertGetNameStringW(c, type, flags, NULL, w, 256) <= 1) { if (cap) out[0] = '\0'; return; }
    to_utf8(w, out, cap);
}

/* The countersignature's signer and time: the old kind (PKCS #9) or RFC 3161's token. */
static void countersign(const CMSG_SIGNER_INFO *si, HCERTSTORE outer, rubraview_props_sig_t *s) {
    const CRYPT_ATTRIBUTE *a = find_attr(&si->UnauthAttrs, szOID_RSA_counterSign);
    if (a && a->cValue > 0) {
        DWORD n = 0;
        if (CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS7_SIGNER_INFO, a->rgValue[0].pbData, a->rgValue[0].cbData, 0, NULL, &n)) {
            CMSG_SIGNER_INFO *cs = (CMSG_SIGNER_INFO *)rubraview_alloc(n);
            if (cs && CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS7_SIGNER_INFO, a->rgValue[0].pbData, a->rgValue[0].cbData, 0, cs, &n)) {
                s->signed_at = signing_time(cs);
                PCCERT_CONTEXT c = signer_cert(outer, cs);
                cert_name(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, s->countersigner, sizeof s->countersigner);
                if (c) CertFreeCertificateContext(c);
            }
            rubraview_free(cs);
        }
        return;
    }
    a = find_attr(&si->UnauthAttrs, OID_RFC3161);
    if (!a || a->cValue == 0) return;
    HCRYPTMSG token = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    if (!token) return;
    if (CryptMsgUpdate(token, a->rgValue[0].pbData, a->rgValue[0].cbData, TRUE)) {
        CMSG_SIGNER_INFO *ts = signer_info(token);
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MSG, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, token);
        /* the time is the token's own (TSTInfo's genTime); a signing time attribute is optional in RFC 3161 */
        DWORD cn = 0;
        if (CryptMsgGetParam(token, CMSG_CONTENT_PARAM, 0, NULL, &cn) && cn > 0) {
            BYTE *content = (BYTE *)rubraview_alloc(cn);
            CRYPT_TIMESTAMP_INFO *info = NULL;
            DWORD in = 0;
            if (content && CryptMsgGetParam(token, CMSG_CONTENT_PARAM, 0, content, &cn) &&
                CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, TIMESTAMP_INFO, content, cn, CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &in) && info) {
                s->signed_at = unix_of(info->ftTime);
                LocalFree(info);
            }
            rubraview_free(content);
        }
        if (ts) {
            if (!s->signed_at) s->signed_at = signing_time(ts);
            PCCERT_CONTEXT c = store ? signer_cert(store, ts) : NULL;
            cert_name(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, s->countersigner, sizeof s->countersigner);
            if (c) CertFreeCertificateContext(c);
            rubraview_free(ts);
        }
        if (store) CertCloseStore(store, 0);
    }
    CryptMsgClose(token);
}

static void serial_hex(const CRYPT_INTEGER_BLOB *b, char *out, size_t cap) {
    static const char HEX[] = "0123456789abcdef";
    size_t at = 0;
    for (DWORD i = b->cbData; i > 0 && at + 3 < cap; --i) {   /* little-endian in the blob, shown most significant first */
        out[at++] = HEX[b->pbData[i - 1] >> 4];
        out[at++] = HEX[b->pbData[i - 1] & 15];
    }
    if (cap) out[at < cap ? at : cap - 1] = '\0';
}

static void fill_sig(HCRYPTMSG msg, HCERTSTORE store, rubraview_props_sig_t *s) {
    CMSG_SIGNER_INFO *si = signer_info(msg);
    if (!si) return;
    PCCERT_CONTEXT c = signer_cert(store, si);
    cert_name(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, s->signer, sizeof s->signer);
    cert_name(c, CERT_NAME_EMAIL_TYPE, 0, s->email, sizeof s->email);
    cert_name(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, s->issuer, sizeof s->issuer);
    if (c) {
        s->valid_from = unix_of(c->pCertInfo->NotBefore);
        s->valid_to = unix_of(c->pCertInfo->NotAfter);
        serial_hex(&c->pCertInfo->SerialNumber, s->serial, sizeof s->serial);
        CertFreeCertificateContext(c);
    }
    PCCRYPT_OID_INFO oi = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, si->HashAlgorithm.pszObjId, 0);
    if (oi && oi->pwszName) to_utf8(oi->pwszName, s->algorithm, sizeof s->algorithm);
    for (char *p = s->algorithm; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');   /* "sha256", as Windows lists it */
    countersign(si, store, s);
    rubraview_free(si);
}

size_t rubraview_pal_fileprops_signatures(const char *path, rubraview_props_sig_t *out, size_t cap) {
    static WCHAR w[WPATH];
    if (!out || cap == 0 || !to_wide(path, w, WPATH)) return 0;
    HCERTSTORE store = NULL;
    HCRYPTMSG msg = NULL;
    DWORD enc = 0, content = 0, format = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, w, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
                          &enc, &content, &format, &store, &msg, NULL))
        return 0;   /* not signed in itself (a catalogue's file has no tab in Windows either) */
    size_t n = 0;
    out[n] = (rubraview_props_sig_t){0};
    fill_sig(msg, store, &out[n]);
    out[n].status = verify(w, 0, &out[n].error);
    n++;
    /* the nested signatures (a second, often sha256 beside sha1) */
    CMSG_SIGNER_INFO *si = signer_info(msg);
    const CRYPT_ATTRIBUTE *nest = si ? find_attr(&si->UnauthAttrs, OID_NESTED) : NULL;
    for (DWORD v = 0; nest && v < nest->cValue && n < cap; ++v) {
        HCRYPTMSG inner = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
        if (!inner) continue;
        if (CryptMsgUpdate(inner, nest->rgValue[v].pbData, nest->rgValue[v].cbData, TRUE)) {
            HCERTSTORE is = CertOpenStore(CERT_STORE_PROV_MSG, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, inner);
            out[n] = (rubraview_props_sig_t){0};
            fill_sig(inner, is ? is : store, &out[n]);
            out[n].status = verify(w, (DWORD)n, &out[n].error);
            n++;
            if (is) CertCloseStore(is, 0);
        }
        CryptMsgClose(inner);
    }
    rubraview_free(si);
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return n;
}

/* ---- Security ---- */

static void sid_text(PSID sid, char *out, size_t cap) {
    LPWSTR s = NULL;
    if (sid && ConvertSidToStringSidW(sid, &s)) { to_utf8(s, out, cap); LocalFree(s); } else if (cap) out[0] = '\0';
}

static void sid_name(PSID sid, char *out, size_t cap) {
    WCHAR name[256], domain[256];
    DWORD nn = 256, dn = 256;
    SID_NAME_USE use;
    if (sid && LookupAccountSidW(NULL, sid, name, &nn, domain, &dn, &use)) {
        WCHAR full[520];
        full[0] = 0;
        if (domain[0] && rubraview_wcat(full, 520, domain)) rubraview_wcat(full, 520, L"\\");
        rubraview_wcat(full, 520, name);
        to_utf8(full, out, cap);
    } else {
        sid_text(sid, out, cap);   /* an account deleted, or of a domain not reached: its SID, as Windows */
    }
}

/* Each ACE of an ACL, allowed / denied (DACL) or audited (SACL), into `aces`. */
static size_t aces_of(PACL acl, bool audit, rubraview_props_ace_t *aces, size_t cap) {
    size_t n = 0;
    ACL_SIZE_INFORMATION info;
    if (!acl || !GetAclInformation(acl, &info, sizeof info, AclSizeInformation)) return 0;
    for (DWORD i = 0; i < info.AceCount && n < cap; ++i) {
        ACE_HEADER *h = NULL;
        if (!GetAce(acl, i, (void **)&h)) continue;
        PSID sid = NULL;
        ACCESS_MASK mask = 0;
        bool allow = true;
        uint8_t kind = 0;
        if (!audit && h->AceType == ACCESS_ALLOWED_ACE_TYPE) { ACCESS_ALLOWED_ACE *a = (ACCESS_ALLOWED_ACE *)h; sid = &a->SidStart; mask = a->Mask; }
        else if (!audit && h->AceType == ACCESS_DENIED_ACE_TYPE) { ACCESS_DENIED_ACE *a = (ACCESS_DENIED_ACE *)h; sid = &a->SidStart; mask = a->Mask; allow = false; }
        else if (audit && h->AceType == SYSTEM_AUDIT_ACE_TYPE) {
            SYSTEM_AUDIT_ACE *a = (SYSTEM_AUDIT_ACE *)h; sid = &a->SidStart; mask = a->Mask;
            kind = (uint8_t)(((h->AceFlags & SUCCESSFUL_ACCESS_ACE_FLAG) ? 1 : 0) | ((h->AceFlags & FAILED_ACCESS_ACE_FLAG) ? 2 : 0));
        } else continue;   /* object and callback ACEs: not on files */
        rubraview_props_ace_t *o = &aces[n++];
        *o = (rubraview_props_ace_t){ .allow = allow, .mask = (uint32_t)mask, .flags = (uint8_t)(h->AceFlags & 0x1F), .audit = kind };
        sid_text(sid, o->sid, sizeof o->sid);
        sid_name(sid, o->name, sizeof o->name);
    }
    return n;
}

/* What the user running this may do to the file (the Advanced view's effective access). */
static uint32_t effective_of(PSECURITY_DESCRIPTOR sd) {
    AUTHZ_RESOURCE_MANAGER_HANDLE rm = NULL;
    AUTHZ_CLIENT_CONTEXT_HANDLE cc = NULL;
    HANDLE token = NULL;
    uint32_t got = 0;
    LUID none = {0};
    if (AuthzInitializeResourceManager(AUTHZ_RM_FLAG_NO_AUDIT, NULL, NULL, NULL, NULL, &rm) &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
        AuthzInitializeContextFromToken(0, token, rm, NULL, none, NULL, &cc)) {
        AUTHZ_ACCESS_REQUEST req = { .DesiredAccess = MAXIMUM_ALLOWED };
        ACCESS_MASK granted = 0;
        DWORD error = 0;
        AUTHZ_ACCESS_REPLY reply = { .ResultListLength = 1, .GrantedAccessMask = &granted, .Error = &error };
        if (AuthzAccessCheck(0, cc, &req, NULL, sd, NULL, 0, &reply, NULL) && error == 0) got = (uint32_t)granted;
    }
    if (cc) AuthzFreeContext(cc);
    if (rm) AuthzFreeResourceManager(rm);
    if (token) CloseHandle(token);
    return got;
}

size_t rubraview_pal_fileprops_security_read(const char *path, rubraview_props_security_t *sec, rubraview_props_ace_t *aces, size_t cap) {
    static WCHAR w[WPATH];
    *sec = (rubraview_props_security_t){0};
    if (!to_wide(path, w, WPATH)) return 0;
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD r = GetNamedSecurityInfoW(w, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                    &owner, NULL, &dacl, NULL, &sd);
    if (r != ERROR_SUCCESS) { say_error(r, sec->why, sizeof sec->why); return 0; }
    sec->read_ok = true;
    sid_text(owner, sec->owner_sid, sizeof sec->owner_sid);
    sid_name(owner, sec->owner, sizeof sec->owner);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (GetSecurityDescriptorControl(sd, &control, &revision)) sec->protect = (control & SE_DACL_PROTECTED) != 0;
    sec->no_dacl = dacl == NULL;   /* everyone may do everything */
    sec->effective = effective_of(sd);
    size_t n = aces_of(dacl, false, aces, cap);
    LocalFree(sd);
    return n;
}

static bool sid_from(const char *text, PSID *out) {
    WCHAR w[200];
    *out = NULL;
    return to_wide(text, w, 200) && ConvertStringSidToSidW(w, out);
}

/* An ACL of the entries (all permissions, or all audits), in the order given. NULL on failure (LocalFree it). */
static PACL acl_of(const rubraview_props_ace_t *aces, size_t count, bool audit, DWORD *err) {
    DWORD size = sizeof(ACL);
    PSID *sids = (PSID *)rubraview_alloc_zero(count + 1, sizeof(PSID));
    if (!sids) { *err = ERROR_NOT_ENOUGH_MEMORY; return NULL; }
    PACL acl = NULL;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        if (!sid_from(aces[i].sid, &sids[i])) { ok = false; *err = GetLastError(); break; }
        size += (DWORD)(sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sids[i]));
    }
    if (ok && (acl = (PACL)LocalAlloc(LPTR, size)) != NULL && InitializeAcl(acl, size, ACL_REVISION)) {
        for (size_t i = 0; i < count && ok; ++i) {
            const rubraview_props_ace_t *a = &aces[i];
            DWORD flags = a->flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE);
            if (audit) ok = AddAuditAccessAceEx(acl, ACL_REVISION, flags, a->mask, sids[i], (a->audit & 1) != 0, (a->audit & 2) != 0);
            else if (a->allow) ok = AddAccessAllowedAceEx(acl, ACL_REVISION, flags, a->mask, sids[i]);
            else ok = AddAccessDeniedAceEx(acl, ACL_REVISION, flags, a->mask, sids[i]);
            if (!ok) *err = GetLastError();
        }
    } else if (ok) {
        ok = false;
        *err = GetLastError();
    }
    for (size_t i = 0; i < count; ++i) if (sids[i]) LocalFree(sids[i]);
    rubraview_free(sids);
    if (!ok && acl) { LocalFree(acl); acl = NULL; }
    return acl;
}

/* The privileges an owner change or a SACL needs, turned on in this process when it has them. */
static void privilege(const WCHAR *name) {
    HANDLE token = NULL;
    TOKEN_PRIVILEGES tp = { .PrivilegeCount = 1 };
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid)) {
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(token);
}

bool rubraview_pal_fileprops_security_write(const char *path, const rubraview_props_sec_t *change, const rubraview_props_ace_t *aces,
                                            size_t count, bool write_dacl, char *why, size_t cap) {
    static WCHAR w[WPATH];
    if (why && cap) why[0] = '\0';
    if (!to_wide(path, w, WPATH)) return false;
    if (change && change->owner_sid[0]) {
        PSID owner = NULL;
        if (!sid_from(change->owner_sid, &owner)) return say_error(GetLastError(), why, cap);
        privilege(L"SeTakeOwnershipPrivilege");
        privilege(L"SeRestorePrivilege");
        DWORD r = SetNamedSecurityInfoW(w, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, owner, NULL, NULL, NULL);
        LocalFree(owner);
        if (r != ERROR_SUCCESS) return say_error(r, why, cap);
    }
    if (!write_dacl) return true;
    DWORD err = 0;
    PACL acl = acl_of(aces, count, false, &err);
    if (!acl) return say_error(err, why, cap);
    SECURITY_INFORMATION what = DACL_SECURITY_INFORMATION;
    if (change && change->protect == 1) what |= PROTECTED_DACL_SECURITY_INFORMATION;
    else what |= UNPROTECTED_DACL_SECURITY_INFORMATION;
    DWORD r = SetNamedSecurityInfoW(w, SE_FILE_OBJECT, what, NULL, NULL, acl, NULL);
    LocalFree(acl);
    if (r != ERROR_SUCCESS) return say_error(r, why, cap);
    return true;
}

size_t rubraview_pal_fileprops_audit_read(const char *path, rubraview_props_ace_t *aces, size_t cap, bool *protect, char *why, size_t why_cap) {
    static WCHAR w[WPATH];
    if (why && why_cap) why[0] = '\0';
    if (!to_wide(path, w, WPATH)) return 0;
    privilege(L"SeSecurityPrivilege");
    PACL sacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD r = GetNamedSecurityInfoW(w, SE_FILE_OBJECT, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd);
    if (r != ERROR_SUCCESS) { say_error(r, why, why_cap); return 0; }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (protect) *protect = GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_SACL_PROTECTED);
    size_t n = aces_of(sacl, true, aces, cap);
    LocalFree(sd);
    return n;
}

bool rubraview_pal_fileprops_audit_write(const char *path, const rubraview_props_ace_t *aces, size_t count, bool protect, char *why, size_t cap) {
    static WCHAR w[WPATH];
    if (why && cap) why[0] = '\0';
    if (!to_wide(path, w, WPATH)) return false;
    privilege(L"SeSecurityPrivilege");
    DWORD err = 0;
    PACL acl = acl_of(aces, count, true, &err);
    if (!acl) return say_error(err, why, cap);
    SECURITY_INFORMATION what = SACL_SECURITY_INFORMATION | (protect ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION);
    DWORD r = SetNamedSecurityInfoW(w, SE_FILE_OBJECT, what, NULL, NULL, NULL, acl);
    LocalFree(acl);
    return r == ERROR_SUCCESS ? true : say_error(r, why, cap);
}

bool rubraview_pal_fileprops_account(const char *typed, char *sid, size_t sid_cap, char *name, size_t name_cap) {
    WCHAR w[300], domain[256];
    BYTE buf[SECURITY_MAX_SID_SIZE];
    DWORD sn = sizeof buf, dn = 256;
    SID_NAME_USE use;
    if (!to_wide(typed, w, 300) || !LookupAccountNameW(NULL, w, buf, &sn, domain, &dn, &use)) return false;
    sid_text((PSID)buf, sid, sid_cap);
    sid_name((PSID)buf, name, name_cap);
    return sid[0] != '\0';
}

#endif /* _WIN32 */
