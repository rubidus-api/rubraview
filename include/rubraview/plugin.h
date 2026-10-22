#ifndef RUBRAVIEW_PLUGIN_H
#define RUBRAVIEW_PLUGIN_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * RFC-0007 (RV-138, D-186): filters from outside. What the viewer knows of a plug-in, the block it shares
 * with the child process that runs one, and the small rules both sides keep. The ABI a plug-in is written
 * against is `rubraview/plugin_filter.h`.
 */

#define RUBRAVIEW_PLUGIN_MAX        32
#define RUBRAVIEW_PLUGIN_PARAMS     8    /* RVF_MAX_PARAMS */
#define RUBRAVIEW_PLUGIN_FILE_BYTES 96   /* "posterise.rvf.dll" */

typedef struct rubraview_plugin_param {
    char    name[64];
    int32_t kind;                 /* RVF_INT, RVF_FLOAT, RVF_BOOL, RVF_CHOICE */
    double  min, max, step, value;
    char    choices[160];         /* "a|b|c" */
} rubraview_plugin_param_t;

typedef struct rubraview_plugin {
    char     file[RUBRAVIEW_PLUGIN_FILE_BYTES];   /* the DLL's name in the folder */
    char     path[520];                           /* UTF-8, whole */
    uint8_t  sha256[32];
    bool     usable;
    const char *why;                              /* not usable: why (an N_ literal) */
    char     name[64], author[64], version[32], licence[64];
    int32_t  param_count;
    rubraview_plugin_param_t params[RUBRAVIEW_PLUGIN_PARAMS];
} rubraview_plugin_t;

/* ---- the block shared with the child ---- */

#define RUBRAVIEW_PLUGIN_MAGIC 0x31465652u   /* "RVF1" */
enum { RUBRAVIEW_PLUGIN_OP_DESCRIBE = 1, RUBRAVIEW_PLUGIN_OP_APPLY = 2 };
enum { RUBRAVIEW_PLUGIN_ASKED = 0, RUBRAVIEW_PLUGIN_RUNNING = 1, RUBRAVIEW_PLUGIN_DONE = 2, RUBRAVIEW_PLUGIN_FAILED = 3 };

typedef struct rubraview_plugin_shared {
    uint32_t magic;
    uint32_t header_bytes;        /* where the pictures start */
    int32_t  op, state;
    volatile int32_t cancel;      /* the viewer's: stop */
    volatile double  progress;    /* the child's: 0..1 */
    int32_t  width, height;       /* apply: the pictures, BGRA, width * 4 a row, `in` then `out` */
    int32_t  param_count;
    double   params[RUBRAVIEW_PLUGIN_PARAMS];
    char     error[256];
    /* describe: what the plug-in said of itself */
    int32_t  abi;
    char     name[64], author[64], version[32], licence[64];
    int32_t  described;
    rubraview_plugin_param_t described_params[RUBRAVIEW_PLUGIN_PARAMS];
} rubraview_plugin_shared_t;

/** The block's bytes for pictures of this size (the header, then two pictures); 0 when too large. */
size_t rubraview_plugin_shared_bytes(int32_t width, int32_t height);
/** Its header made ready for `op`. */
void rubraview_plugin_shared_init(rubraview_plugin_shared_t *shared, int32_t op, int32_t width, int32_t height);
uint8_t *rubraview_plugin_shared_in(rubraview_plugin_shared_t *shared);
uint8_t *rubraview_plugin_shared_out(rubraview_plugin_shared_t *shared);

/** What a describe left in the block, into `out` (its file, path and hash are the caller's); false and
    `*why` when it is not a plug-in this viewer can run (another ABI, too many parameters, a bad range). */
bool rubraview_plugin_take_description(const rubraview_plugin_shared_t *shared, rubraview_plugin_t *out, const char **why);

/** `values` made what the plug-in takes: missing ones its defaults, each within its range, whole numbers
    for int / bool / choice. */
void rubraview_plugin_params_clamp(const rubraview_plugin_t *plugin, double *values, int32_t given);

int32_t rubraview_plugin_choice_count(const char *choices);
/** Choice `index`'s word into `out`; false past the last. */
bool rubraview_plugin_choice_word(const char *choices, int32_t index, char *out, size_t capacity);

/** A plug-in's file named as the user names it: "posterise", "Posterise.rvf.dll". */
bool rubraview_plugin_file_matches(const char *file, proven_u8str_view_t name);

/** `--plugin=NAME[:V1,V2,...]`'s value: the name and up to RUBRAVIEW_PLUGIN_PARAMS numbers. */
bool rubraview_plugin_arg_parse(proven_u8str_view_t value, char *name, size_t capacity, double *params, int32_t *count);

/** FIPS 180-4 SHA-256. */
void rubraview_sha256(const uint8_t *data, size_t size, uint8_t out[32]);

#endif
