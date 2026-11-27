#ifndef RUBRAVIEW_AUDIO_EXTS_H
#define RUBRAVIEW_AUDIO_EXTS_H

/*
 * RV-319 (R349, D-398): the one table of a sound file's extensions. Every list of them reads this
 * table or is a copy that a test compares with it (ops/tests/test_shellreq.c).
 *
 *   ext       without the dot, lower case
 *   opens     1: listed in a folder and opened as media (AUDIO_FILTER); 0: only known by name
 *   shell     1: one of our file types: the shell.ext_* and shell.menu_* settings, the "music"
 *             shell group and the Store package's [assoc.Music_*] and menus (copies, compared)
 *   lossless  1: tried first when a cue sheet's audio file is not where the sheet says
 *
 * Every row is a song by name (rubraview_tags_is_music_name), wears the music icon in Explorer and is
 * a cue sheet's possible audio file. The rows' order is the order of the lists made from them.
 */
#define RUBRAVIEW_AUDIO_EXTS(X) \
    X("mp3",  1, 1, 0) \
    X("flac", 1, 1, 1) \
    X("wav",  1, 1, 1) \
    X("ogg",  1, 1, 0) \
    X("opus", 1, 1, 0) \
    X("m4a",  1, 1, 0) \
    X("aac",  1, 1, 0) \
    X("wma",  1, 1, 0) \
    X("oga",  1, 1, 0) \
    X("m4b",  1, 1, 0) \
    X("w64",  1, 0, 1) \
    X("aiff", 1, 0, 1) \
    X("aif",  1, 0, 1) \
    X("ape",  1, 0, 1) \
    X("wv",   1, 0, 1) \
    X("tta",  1, 0, 1) \
    X("tak",  1, 0, 1) \
    X("dsf",  1, 0, 1) \
    X("dff",  1, 0, 1) \
    X("mka",  1, 0, 0) \
    X("alac", 0, 0, 1) \
    X("ra",   1, 0, 0)

/* the rest when the flag (a row's literal 0 or 1) is 1; the rest may hold commas */
#define RUBRAVIEW_AUDIO_IF_0(...)
#define RUBRAVIEW_AUDIO_IF_1(...) __VA_ARGS__
#define RUBRAVIEW_AUDIO_IF(flag, ...) RUBRAVIEW_AUDIO_IF_##flag(__VA_ARGS__)

/* "*.mp3;*.flac;...;": the extensions that open, each followed by ';' (a glob list skips the empty last) */
#define RUBRAVIEW_AUDIO_FILTER_ROW(ext, opens, shell, lossless) RUBRAVIEW_AUDIO_IF(opens, "*." ext ";")
#define RUBRAVIEW_AUDIO_FILTER RUBRAVIEW_AUDIO_EXTS(RUBRAVIEW_AUDIO_FILTER_ROW)

typedef struct rubraview_audio_ext {
    const char *ext;
    bool opens, shell, lossless;
} rubraview_audio_ext_t;

#define RUBRAVIEW_AUDIO_EXT_ROW(ext, opens, shell, lossless) { ext, opens, shell, lossless },
[[maybe_unused]] static const rubraview_audio_ext_t RUBRAVIEW_AUDIO_EXT_TABLE[] = { RUBRAVIEW_AUDIO_EXTS(RUBRAVIEW_AUDIO_EXT_ROW) };
#define RUBRAVIEW_AUDIO_EXT_COUNT (sizeof(RUBRAVIEW_AUDIO_EXT_TABLE) / sizeof(RUBRAVIEW_AUDIO_EXT_TABLE[0]))

#endif
