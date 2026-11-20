/*
 * RV-310: what settingswin.c and settings_keys.c share of Settings › Keys.
 */
#ifndef RUBRAVIEW_APP_SETTINGS_KEYS_H
#define RUBRAVIEW_APP_SETTINGS_KEYS_H

#include "app.h"
#include "rubraview/keyboard.h"

#define KEY_ROWS_MAX 1024
#define KEYS_LAYOUT_ROWS 33   /* RV-244: 29 for the keyboard (12.5 key units, 2.3 rows a key), the dots' key, what is not on it */
typedef struct { size_t binding, combo; } key_combo_row_t;

extern bool g_keys_by_key;                        /* RV-184: by key, else by function */
extern bool g_keys_by_row;                        /* RV-303: by keyboard row */
extern bool g_keys_layout;                        /* RV-217: the keyboard drawn */
extern int32_t g_keys_dot_hover;                  /* RV-252: the circle the pointer rests on */
extern rubraview_keyrow_entry_t g_key_rows[KEY_ROWS_MAX];
extern size_t g_key_row_count;

size_t keys_table_row(app_state_t *app, size_t index, char *buffer, size_t capacity);
int32_t keymap_row_binding(int32_t row);          /* a table row's binding, -1 for a heading */
bool keys_layout_click(app_state_t *app, double px, double py);
int32_t keys_dot_under(app_state_t *app, double px, double py);
void keys_layout_draw(app_state_t *app);

#endif
