#include "rubraview/encoding.h"
#include "rubraview/utf8.h"

rubraview_encoding_verdict_t rubraview_archive_filename_detect(u8str_t raw_name, bool utf8_flag) {
    if (utf8_flag) {
        return RUBRAVIEW_ENCODING_UTF8_FLAGGED;
    }
    if (rubraview_utf8_validate(raw_name)) {
        return RUBRAVIEW_ENCODING_UTF8_VALID;
    }
    return RUBRAVIEW_ENCODING_NEEDS_FALLBACK;
}

uint32_t rubraview_codepage_id(rubraview_codepage_t codepage) {
    switch (codepage) {
        case RUBRAVIEW_CODEPAGE_UTF8:                 return 65001;
        case RUBRAVIEW_CODEPAGE_KOREAN:               return 949;
        case RUBRAVIEW_CODEPAGE_JAPANESE:             return 932;
        case RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE:   return 936;
        case RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE:  return 950;
        case RUBRAVIEW_CODEPAGE_WESTERN:              return 1252;
        case RUBRAVIEW_CODEPAGE_AUTO:
        default:                                      return 0; /* the host's active code page */
    }
}

u8str_t rubraview_codepage_label(rubraview_codepage_t codepage) {
    switch (codepage) {
        case RUBRAVIEW_CODEPAGE_UTF8:                 return U8("UTF-8");
        case RUBRAVIEW_CODEPAGE_KOREAN:               return U8("Korean (CP949)");
        case RUBRAVIEW_CODEPAGE_JAPANESE:             return U8("Japanese (Shift-JIS)");
        case RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE:   return U8("Simplified Chinese (GBK)");
        case RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE:  return U8("Traditional Chinese (Big5)");
        case RUBRAVIEW_CODEPAGE_WESTERN:              return U8("Western European (CP1252)");
        case RUBRAVIEW_CODEPAGE_AUTO:
        default:                                      return U8("Auto-Detect");
    }
}

bool rubraview_archive_filename_plan(u8str_t raw_name, bool utf8_flag,
                                     rubraview_codepage_t override_choice,
                                     uint32_t *out_codepage_id) {
    if (out_codepage_id) *out_codepage_id = 0;

    /* An explicit override is the reader overruling the evidence, which
       is why the option exists — a mis-flagged archive is exactly the
       case the menu entry is for. */
    if (override_choice == RUBRAVIEW_CODEPAGE_UTF8) return false;
    if (override_choice != RUBRAVIEW_CODEPAGE_AUTO) {
        if (out_codepage_id) *out_codepage_id = rubraview_codepage_id(override_choice);
        return true;
    }

    /* AUTO: steps 1 and 2 first, and only bytes that fail both need the
       host code page. */
    if (rubraview_archive_filename_detect(raw_name, utf8_flag) != RUBRAVIEW_ENCODING_NEEDS_FALLBACK) {
        return false;
    }
    if (out_codepage_id) *out_codepage_id = 0; /* the host's active code page */
    return true;
}
