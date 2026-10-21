#ifndef RUBRAVIEW_PLUGIN_FILTER_H
#define RUBRAVIEW_PLUGIN_FILTER_H

/*
 * RFC-0007 (RV-138): a filter from outside Rubraview — a 64-bit Windows DLL named `*.rvf.dll` that exports
 * the two functions below. Rubraview runs it in a child process of its own, so a filter that crashes or
 * hangs takes nothing else with it. Pixels are BGRA, 8 bits a channel, alpha not multiplied in, rows
 * `stride` bytes apart; `out` is the same size as `in` and is the filter's to fill whole. A filter allocates
 * nothing it hands back. This header is MIT-licensed with the rest of Rubraview: copy it into a filter's own
 * source.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define RVF_ABI 1
#define RVF_MAX_PARAMS 8

enum { RVF_INT = 0, RVF_FLOAT = 1, RVF_BOOL = 2, RVF_CHOICE = 3 };

typedef struct rvf_param {
    const char *name;      /* English; shown as it is */
    int kind;              /* RVF_INT, RVF_FLOAT, RVF_BOOL, RVF_CHOICE */
    double min, max, step; /* RVF_CHOICE: 0 .. the number of choices - 1 */
    double value;          /* the default */
    const char *choices;   /* RVF_CHOICE: "first|second|third" */
} rvf_param;

typedef struct rvf_info {
    int abi;               /* RVF_ABI */
    const char *name, *author, *version, *licence;
    int param_count;       /* 0 .. RVF_MAX_PARAMS */
    const rvf_param *params;
} rvf_info;

typedef struct rvf_image {
    unsigned char *pixels;
    int width, height, stride;
} rvf_image;

/* `progress` takes 0..1 and returns nonzero when the user cancelled: stop and return nonzero. */
typedef int (*rvf_progress)(void *context, double done);

#if defined(_WIN32)
#define RVF_EXPORT __declspec(dllexport)
#else
#define RVF_EXPORT
#endif

/* What the filter is and takes; called before anything else, maybe more than once. */
RVF_EXPORT const rvf_info *rvf_describe(void);

/* The filter over the picture with the parameters' values (as many as it described, each within its range).
   0 when done; anything else is a failure, and Rubraview keeps the picture as it was. */
RVF_EXPORT int rvf_apply(const rvf_image *in, rvf_image *out, const double *params, rvf_progress progress, void *context);

#ifdef __cplusplus
}
#endif

#endif
