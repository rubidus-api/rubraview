#ifndef RUBRAVIEW_DEFAULT_KEYMAP_H
#define RUBRAVIEW_DEFAULT_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The built-in key bindings, used when no keymap.ini is present
 * (RFC-0001 §3.7.5). It covers every row of the §3.7.2 hotkey table that
 * milestone M3 is responsible for — Navigation, Zoom & Fit, Book &
 * Manga, Slideshow and UI & Windows — which is what §11.2 requires
 * before M3 is done. Media Playback rows belong to M5, and the curves
 * and batch entries to M6.
 *
 * Living in core rather than in the application lets the test suite
 * check that coverage mechanically (T032) instead of by eye on Windows.
 */
const char *rubraview_default_keymap(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_DEFAULT_KEYMAP_H */
